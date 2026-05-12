/*
 * game.h - shared interface between the kernel driver and the user
 *          application for the Maze Chase Game (CSEE 4840 Final Project).
 *
 * The file follows the same pattern as lab3/lab3-sw/vga_ball.h: it
 * defines the ioctl numbers and the argument structures that travel
 * through them.
 */

#ifndef _GAME_H
#define _GAME_H

#include <linux/ioctl.h>

#ifndef __KERNEL__
#include <stdint.h>
#else
#include <linux/types.h>
typedef __u8  uint8_t;
typedef __u32 uint32_t;
#endif

/* ------------------------------------------------------------------ */
/* Game-logic constants (mirror the Verilog parameters)               */
/* ------------------------------------------------------------------ */
#define GAME_MAZE_COLS  30
#define GAME_MAZE_ROWS  20
#define GAME_NUM_TILES   6

#define GAME_CELL_WALL     0
#define GAME_CELL_PATH     1
#define GAME_CELL_SPECIAL  2
#define GAME_CELL_GATE     3

/* GAME_STATUS bit positions (also the bit layout of the FPGA register) */
#define GAME_BIT_GATE_OPEN   0x1
#define GAME_BIT_EVADER_WIN  0x2
#define GAME_BIT_GHOST_WIN   0x4
#define GAME_BIT_RESET       0x8

/* ------------------------------------------------------------------ */
/* Position type used by the ioctls                                    */
/* ------------------------------------------------------------------ */
typedef struct {
    uint8_t col;     /* 0 .. 29 */
    uint8_t row;     /* 0 .. 19 */
} game_pos_t;

/* Snapshot of the writable portion of the game state.  The user app
 * sends this once per movement tick; the driver issues three 32-bit
 * writes to the EVADER_POS / GHOST0_POS / GHOST1_POS registers, plus
 * one optional write to GAME_STATUS.                                   */
typedef struct {
    game_pos_t evader;
    game_pos_t ghost0;
    game_pos_t ghost1;
    uint32_t   game_status;     /* see GAME_BIT_* above                 */
    uint32_t   write_status;    /* 0 = leave GAME_STATUS untouched,
                                   nonzero = write game_status field    */
} game_write_t;

/* Snapshot of everything the FPGA reports back to the application.    */
typedef struct {
    uint32_t tile_status;                /* bits[5:0] activated flags  */
    uint32_t tile_time[GAME_NUM_TILES];  /* per-tile frame counts      */
    uint32_t game_status;                /* current GAME_STATUS reg    */
    uint32_t frame_count;                /* rolling 16-bit frame ctr   */
} game_read_t;

/* ------------------------------------------------------------------ */
/* ioctls                                                              */
/* ------------------------------------------------------------------ */
#define GAME_MAGIC 'g'

#define GAME_WRITE_STATE _IOW (GAME_MAGIC, 1, game_write_t)
#define GAME_READ_STATE  _IOR (GAME_MAGIC, 2, game_read_t )
#define GAME_RESET_GAME  _IO  (GAME_MAGIC, 3)
#define GAME_WAIT_VSYNC  _IOR (GAME_MAGIC, 4, uint32_t)

#endif /* _GAME_H */
