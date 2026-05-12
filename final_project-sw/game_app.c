/*
 * game_app.c - Maze Chase Game HPS application.
 *
 * Modelled after lab3/lab3-sw/hello.c but much bigger.  Drives the
 * Maze Chase Game peripheral through the /dev/game ioctls defined in
 * game.h.  Implements the full game loop:
 *
 *   * 60 Hz frame loop synced to FPGA VSYNC
 *   * USB keyboard input via libusb-1.0 + a dedicated reader thread
 *     (HID Boot keyboard protocol, ported from lab2/usbkeyboard.c).
 *     We deliberately bypass /dev/input/eventN because composite HID
 *     keyboards expose multiple event devices and the Consumer-Control
 *     sub-device misadvertises KEY_UP without ever delivering arrows.
 *   * 5 Hz movement tick (every 12 frames)
 *   * BFS shortest-path AI for both ghosts
 *   * Win/loss / reset state machine
 *
 * Build with `make` in this directory (see Makefile).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "game.h"
#include "maze_data.h"
#include "usbkeyboard.h"

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */
#define DEV_GAME       "/dev/game"
#define FRAMES_PER_TICK 12   /* 60 Hz / 12 = 5 Hz movement              */

enum { DIR_NONE = 0, DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };

static const game_pos_t INITIAL_EVADER = { .col = 15, .row = 10 };
static const game_pos_t INITIAL_GHOST0 = { .col = 1,  .row = 2  };
static const game_pos_t INITIAL_GHOST1 = { .col = 28, .row = 2  };
static const game_pos_t GATE_POS       = { .col = 15, .row = 1  };

/* ------------------------------------------------------------------ */
/* Globals                                                             */
/* ------------------------------------------------------------------ */
static int g_game_fd  = -1;

/* Keyboard state shared between the USB thread and the main loop.
 * `volatile` is enough on ARMv7 for word-sized loads/stores; we don't
 * need a mutex because the thread is the sole writer and the main
 * loop is the sole reader, and we tolerate a one-frame race. */
static volatile int g_held_dir       = DIR_NONE;
static volatile int g_enter_pressed  = 0;
static volatile int g_kbd_thread_run = 0;

static struct libusb_device_handle *g_keyboard = NULL;
static uint8_t  g_endpoint_address = 0;
static pthread_t g_kbd_thread;

static game_pos_t g_evader, g_ghost0, g_ghost1;
static int        g_gate_open = 0;
static int        g_game_over = 0;
static int        g_game_won  = 0;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static int maze_passable(int col, int row)
{
    if (col < 0 || col >= GAME_MAZE_COLS) return 0;
    if (row < 0 || row >= GAME_MAZE_ROWS) return 0;
    unsigned char c = maze_data[row][col];
    return c != GAME_CELL_WALL;
}

/* The ghost should not be allowed to use the gate as a shortcut. */
static int maze_passable_ghost(int col, int row)
{
    if (!maze_passable(col, row)) return 0;
    if (col == GATE_POS.col && row == GATE_POS.row) return 0;
    return 1;
}

static int pos_eq(game_pos_t a, game_pos_t b)
{
    return a.col == b.col && a.row == b.row;
}

static int popcount6(uint32_t v)
{
    int n = 0;
    for (int i = 0; i < 6; i++) if (v & (1u << i)) n++;
    return n;
}

/* ------------------------------------------------------------------ */
/* Driver wrappers                                                      */
/* ------------------------------------------------------------------ */
static void send_state(uint32_t status, int write_status)
{
    game_write_t w = {
        .evader        = g_evader,
        .ghost0        = g_ghost0,
        .ghost1        = g_ghost1,
        .game_status   = status,
        .write_status  = write_status,
    };
    if (ioctl(g_game_fd, GAME_WRITE_STATE, &w) < 0)
        perror("ioctl(GAME_WRITE_STATE)");
}

static void read_state(game_read_t *r)
{
    if (ioctl(g_game_fd, GAME_READ_STATE, r) < 0)
        perror("ioctl(GAME_READ_STATE)");
}

static void wait_vsync(void)
{
    uint32_t frame;
    if (ioctl(g_game_fd, GAME_WAIT_VSYNC, &frame) < 0)
        perror("ioctl(GAME_WAIT_VSYNC)");
}

static void reset_game_hw(void)
{
    if (ioctl(g_game_fd, GAME_RESET_GAME) < 0)
        perror("ioctl(GAME_RESET_GAME)");
}

/* ------------------------------------------------------------------ */
/* USB keyboard input via libusb (modelled on lab2/usbkeyboard.c).
 *
 * Why not /dev/input/eventN?  HID composite keyboards (like the
 * SIGMACHIP one used in lab) advertise multiple HID collections, so
 * the kernel exposes them as several event devices ("USB Keyboard",
 * "USB Keyboard Consumer Control", "USB Keyboard System Control").
 * The Consumer Control sub-device falsely lists KEY_UP in its bitmap
 * but never delivers arrow events, so any picker that matches on
 * KEY_UP alone latches onto the wrong device.  Talking to the HID
 * Boot keyboard interface directly via libusb removes the ambiguity.
 *
 * Architecture: a dedicated pthread blocks on libusb_interrupt_transfer.
 * Each report is an 8-byte HID Boot packet that lists up to six keys
 * currently held.  We translate that into:
 *   * g_held_dir       = direction of the first arrow key in the
 *                        packet (DIR_NONE when no arrows held)
 *   * g_enter_pressed  = one-shot edge flag when ENTER appears
 * The main loop reads these volatiles without locking; we tolerate a
 * one-frame race since input runs at 16.7 ms granularity already.
 * ------------------------------------------------------------------ */
static int hid_keycode_to_dir(uint8_t kc)
{
    switch (kc) {
    case HID_KEY_UP_ARROW:    return DIR_UP;
    case HID_KEY_DOWN_ARROW:  return DIR_DOWN;
    case HID_KEY_LEFT_ARROW:  return DIR_LEFT;
    case HID_KEY_RIGHT_ARROW: return DIR_RIGHT;
    default:                  return DIR_NONE;
    }
}

static void *keyboard_thread_f(void *arg)
{
    struct usb_keyboard_packet packet;
    int transferred;
    uint8_t prev_keycodes[6] = {0};
    (void)arg;

    while (g_kbd_thread_run) {
        /* 100 ms timeout so the loop can periodically check the run
         * flag.  LIBUSB_ERROR_TIMEOUT just means "no new report"; we
         * don't reset state on timeout because the keyboard only
         * sends a report when a key state changes, and we want the
         * held direction to persist across timeouts. */
        int r = libusb_interrupt_transfer(g_keyboard, g_endpoint_address,
                                          (unsigned char *)&packet,
                                          sizeof(packet),
                                          &transferred, 100);
        if (r == LIBUSB_ERROR_TIMEOUT) continue;
        if (r != 0) {
            fprintf(stderr,
                    "libusb_interrupt_transfer error: %s\n",
                    libusb_error_name(r));
            continue;
        }
        if (transferred != sizeof(packet)) continue;

        /* Recompute the held direction from the entire report.  The
         * first arrow key encountered wins; if the packet contains
         * no arrows, no arrow is held. */
        int new_dir = DIR_NONE;
        int i;
        for (i = 0; i < 6; i++) {
            int d = hid_keycode_to_dir(packet.keycode[i]);
            if (d != DIR_NONE) { new_dir = d; break; }
        }
        g_held_dir = new_dir;

        /* ENTER is edge-triggered: set the one-shot flag only on the
         * transition from "not in previous report" to "in current
         * report".  This avoids retriggering while the key is held. */
        for (i = 0; i < 6; i++) {
            if (packet.keycode[i] != HID_KEY_ENTER) continue;
            int was_held = 0;
            int j;
            for (j = 0; j < 6; j++)
                if (prev_keycodes[j] == HID_KEY_ENTER) { was_held = 1; break; }
            if (!was_held) g_enter_pressed = 1;
            break;
        }
        memcpy(prev_keycodes, packet.keycode, sizeof(prev_keycodes));
    }
    return NULL;
}

static int open_keyboard(void)
{
    g_keyboard = openkeyboard(&g_endpoint_address);
    if (g_keyboard == NULL) {
        fprintf(stderr, "Did not find a USB keyboard\n");
        return -1;
    }
    printf("USB keyboard opened (endpoint 0x%02x)\n", g_endpoint_address);

    g_kbd_thread_run = 1;
    if (pthread_create(&g_kbd_thread, NULL, keyboard_thread_f, NULL) != 0) {
        perror("pthread_create(keyboard)");
        g_kbd_thread_run = 0;
        libusb_close(g_keyboard);
        g_keyboard = NULL;
        return -1;
    }
    return 0;
}

static void close_keyboard(void)
{
    if (g_kbd_thread_run) {
        g_kbd_thread_run = 0;
        pthread_join(g_kbd_thread, NULL);
    }
    if (g_keyboard) {
        libusb_close(g_keyboard);
        g_keyboard = NULL;
    }
    libusb_exit(NULL);
}

/* ------------------------------------------------------------------ */
/* Movement and AI                                                      */
/* ------------------------------------------------------------------ */
static void move_evader(void)
{
    int dc = 0, dr = 0;
    switch (g_held_dir) {
    case DIR_UP:    dr = -1; break;
    case DIR_DOWN:  dr =  1; break;
    case DIR_LEFT:  dc = -1; break;
    case DIR_RIGHT: dc =  1; break;
    default: return;
    }
    int nc = g_evader.col + dc;
    int nr = g_evader.row + dr;
    if (!maze_passable(nc, nr)) return;
    /* The gate cell is only passable for the Evader after the gate is
     * unlocked (cell type GATE).                                       */
    if (nr == GATE_POS.row && nc == GATE_POS.col && !g_gate_open) return;

    g_evader.col = (uint8_t)nc;
    g_evader.row = (uint8_t)nr;
}

/* ------------- BFS over the 30x20 grid ----------------------------- */
/* We re-use a single static workspace to avoid heap pressure.          */
#define BFS_N (GAME_MAZE_ROWS * GAME_MAZE_COLS)

static uint8_t  bfs_visited[BFS_N];
static int16_t  bfs_parent [BFS_N];
static uint16_t bfs_queue  [BFS_N];

static inline int tile_idx(int col, int row)
{
    return row * GAME_MAZE_COLS + col;
}

/* Compute the next tile a ghost should move into to chase the Evader.
 * Returns the source tile (no movement) if no path exists.             */
static game_pos_t bfs_next_step(game_pos_t from, game_pos_t to)
{
    int src = tile_idx(from.col, from.row);
    int dst = tile_idx(to.col,   to.row  );

    memset(bfs_visited, 0, sizeof(bfs_visited));
    for (int i = 0; i < BFS_N; i++) bfs_parent[i] = -1;

    int qh = 0, qt = 0;
    bfs_queue[qt++] = (uint16_t)src;
    bfs_visited[src] = 1;

    static const int dC[4] = {  0,  0, -1,  1 };
    static const int dR[4] = { -1,  1,  0,  0 };

    int found = 0;
    while (qh < qt && !found) {
        int cur = bfs_queue[qh++];
        int cc = cur % GAME_MAZE_COLS;
        int cr = cur / GAME_MAZE_COLS;
        for (int k = 0; k < 4; k++) {
            int nc = cc + dC[k];
            int nr = cr + dR[k];
            if (!maze_passable_ghost(nc, nr)) continue;
            int nidx = tile_idx(nc, nr);
            if (bfs_visited[nidx]) continue;
            bfs_visited[nidx] = 1;
            bfs_parent [nidx] = (int16_t)cur;
            if (nidx == dst) { found = 1; break; }
            bfs_queue[qt++] = (uint16_t)nidx;
        }
    }

    if (!found || bfs_parent[dst] < 0) {
        return from;  /* No path - hold position */
    }

    /* Walk back from dst until we find the tile whose parent is src.   */
    int cur = dst;
    while (bfs_parent[cur] != src && bfs_parent[cur] >= 0)
        cur = bfs_parent[cur];

    game_pos_t out;
    out.col = (uint8_t)(cur % GAME_MAZE_COLS);
    out.row = (uint8_t)(cur / GAME_MAZE_COLS);
    return out;
}

static void move_ghosts(void)
{
    g_ghost0 = bfs_next_step(g_ghost0, g_evader);
    g_ghost1 = bfs_next_step(g_ghost1, g_evader);
}

/* ------------------------------------------------------------------ */
/* Game-state housekeeping                                              */
/* ------------------------------------------------------------------ */
static void reset_logical_state(void)
{
    g_evader = INITIAL_EVADER;
    g_ghost0 = INITIAL_GHOST0;
    g_ghost1 = INITIAL_GHOST1;
    g_gate_open = 0;
    g_game_over = 0;
    g_game_won  = 0;
}

static void check_gate_unlock(uint32_t tile_status)
{
    if (!g_gate_open && popcount6(tile_status) >= 3) {
        g_gate_open = 1;
        send_state(GAME_BIT_GATE_OPEN, 1);
        printf("Gate unlocked!\n");
    }
}

static void check_win_loss(void)
{
    if (g_game_over) return;
    if (pos_eq(g_evader, g_ghost0) || pos_eq(g_evader, g_ghost1)) {
        g_game_over = 1;
        g_game_won  = 0;
        send_state(GAME_BIT_GHOST_WIN | (g_gate_open ? GAME_BIT_GATE_OPEN : 0), 1);
        printf("Ghost catches Evader - you lose.  Press ENTER to restart.\n");
        return;
    }
    if (g_gate_open && pos_eq(g_evader, GATE_POS)) {
        g_game_over = 1;
        g_game_won  = 1;
        send_state(GAME_BIT_EVADER_WIN | GAME_BIT_GATE_OPEN, 1);
        printf("Evader reaches gate - you win!  Press ENTER to restart.\n");
    }
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */
int main(void)
{
    printf("Maze Chase Game starting...\n");

    g_game_fd = open(DEV_GAME, O_RDWR);
    if (g_game_fd < 0) {
        fprintf(stderr, "could not open %s: %s\n", DEV_GAME, strerror(errno));
        return 1;
    }

    if (open_keyboard() < 0) {
        fprintf(stderr, "warning: no keyboard - controls disabled\n");
    }

    reset_game_hw();
    reset_logical_state();
    send_state(0, 1);   /* clear all status bits */

    int move_tick = 0;

    while (1) {
        wait_vsync();

        if (g_game_over) {
            if (g_enter_pressed) {
                g_enter_pressed = 0;
                printf("Restarting...\n");
                reset_game_hw();
                reset_logical_state();
                send_state(0, 1);
                move_tick = 0;
            }
            continue;
        }
        g_enter_pressed = 0;  /* ignore during play */

        /* Read tile_status every frame for gate-unlock check */
        game_read_t st;
        read_state(&st);
        check_gate_unlock(st.tile_status);

        if (++move_tick >= FRAMES_PER_TICK) {
            move_tick = 0;
            move_evader();
            move_ghosts();
            send_state(0, 0);   /* push new positions, leave status */
            check_win_loss();
        }
    }

    close_keyboard();
    close(g_game_fd);
    return 0;
}
