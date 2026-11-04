/*
 *
 * CSEE 4840 Lab 2
 *
 * Name/UNI: TODO: put your name and UNI here
 */
#include "fbputchar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "usbkeyboard.h"
#include <pthread.h>
#include <stdint.h>
/* Update SERVER_HOST to be the IP address of
 * the chat server you are connecting to
 */
/* arthur.cs.columbia.edu */
#define SERVER_HOST "128.59.19.114"
#define SERVER_PORT 42000

#define BUFFER_SIZE 128

/* Screen/layout settings (characters, not pixels) */
#define SCREEN_COLS 64
#define SCREEN_ROWS 24

/* Reserve bottom two rows for input and one row as a separator */
#define INPUT_ROWS 2
#define SEP_ROW (SCREEN_ROWS - INPUT_ROWS - 1)   /* horizontal divider row */
#define INPUT_START_ROW (SEP_ROW + 1)            /* first input row */
#define LOG_ROWS (SEP_ROW)                       /* rows 0..SEP_ROW-1 for log */

/* Chat log configuration */
#define MAX_LOG_LINES 256
#define MAX_LINE_LEN SCREEN_COLS

/* Input buffer (two rows worth of characters) */
#define INPUT_MAX_CHARS (SCREEN_COLS * INPUT_ROWS)

/*
 * References:
 *
 * https://web.archive.org/web/20130307100215/http://beej.us/guide/bgnet/output/html/singlepage/bgnet.html
 *
 * http://www.thegeekstuff.com/2011/12/c-socket-programming/
 * 
 */

int sockfd; /* Socket file descriptor */

struct libusb_device_handle *keyboard;
uint8_t endpoint_address;

pthread_t network_thread;
void *network_thread_f(void *);

/* Simple mutex to protect framebuffer and log state from concurrent access */
static pthread_mutex_t screen_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Chat log state */
static char log_lines[MAX_LOG_LINES][MAX_LINE_LEN + 1];
static int log_start = 0;      /* index of the oldest stored line */
static int log_count = 0;      /* number of valid lines in the buffer */

/* Partially-constructed line for wrapping and newline handling */
static char current_line[MAX_LINE_LEN + 1];
static int current_line_len = 0;

/* Input editing state */
static char input_buffer[INPUT_MAX_CHARS + 1];
static int input_len = 0;
static int input_cursor = 0;

/* For debouncing USB key presses so we only handle transitions */
static uint8_t last_keycode = 0;

/* Forward declarations */
static void clear_region(int row_start, int row_end);
static void init_screen(void);
static void redraw_log(void);
static void redraw_input_line(void);
static void append_log_text(const char *text);
static char keycode_to_ascii(uint8_t keycode, uint8_t modifiers);
static void handle_key_event(const struct usb_keyboard_packet *packet, int *exit_requested);

/* Special USB HID keycodes we care about */
#define USB_KEY_ENTER      0x28
#define USB_KEY_ESC        0x29
#define USB_KEY_BACKSPACE  0x2a
#define USB_KEY_SPACE      0x2c
#define USB_KEY_LEFT       0x50
#define USB_KEY_RIGHT      0x4f

int main()
{
  int err;

  struct sockaddr_in serv_addr;

  struct usb_keyboard_packet packet;
  int transferred;
  int exit_requested = 0;

  if ((err = fbopen()) != 0) {
    fprintf(stderr, "Error: Could not open framebuffer: %d\n", err);
    exit(1);
  }

  init_screen();

  /* Open the keyboard */
  if ( (keyboard = openkeyboard(&endpoint_address)) == NULL ) {
    fprintf(stderr, "Did not find a keyboard\n");
    exit(1);
  }
    
  /* Create a TCP communications socket */
  if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
    fprintf(stderr, "Error: Could not create socket\n");
    exit(1);
  }

  /* Get the server address */
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(SERVER_PORT);
  if ( inet_pton(AF_INET, SERVER_HOST, &serv_addr.sin_addr) <= 0) {
    fprintf(stderr, "Error: Could not convert host IP \"%s\"\n", SERVER_HOST);
    exit(1);
  }

  /* Connect the socket to the server */
  if ( connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
    fprintf(stderr, "Error: connect() failed.  Is the server running?\n");
    exit(1);
  }

  /* Start the network thread */
  pthread_create(&network_thread, NULL, network_thread_f, NULL);

  /* Look for and handle keypresses */
  for (;;) {
    libusb_interrupt_transfer(keyboard, endpoint_address,
			      (unsigned char *) &packet, sizeof(packet),
			      &transferred, 0);
    if (transferred == sizeof(packet)) {
      handle_key_event(&packet, &exit_requested);
      if (exit_requested) {
	break;
      }
    }
  }

  /* Terminate the network thread */
  pthread_cancel(network_thread);

  /* Wait for the network thread to finish */
  pthread_join(network_thread, NULL);

  return 0;
}

void *network_thread_f(void *ignored)
{
  char recvBuf[BUFFER_SIZE];
  int n;
  /* Receive data */
  while ( (n = read(sockfd, &recvBuf, BUFFER_SIZE - 1)) > 0 ) {
    recvBuf[n] = '\0';
    printf("%s", recvBuf);

    pthread_mutex_lock(&screen_mutex);
    append_log_text(recvBuf);
    redraw_log();
    redraw_input_line();
    pthread_mutex_unlock(&screen_mutex);
  }

  return NULL;
}

/* Clear a region of the framebuffer in character coordinates (inclusive rows) */
static void clear_region(int row_start, int row_end)
{
  int r, c;
  if (row_start < 0) row_start = 0;
  if (row_end >= SCREEN_ROWS) row_end = SCREEN_ROWS - 1;
  for (r = row_start; r <= row_end; r++) {
    for (c = 0; c < SCREEN_COLS; c++) {
      fbputchar(' ', r, c);
    }
  }
}

/* Initialize framebuffer layout: clear screen, draw separator, and initial message */
static void init_screen(void)
{
  int col;

  pthread_mutex_lock(&screen_mutex);

  clear_region(0, SCREEN_ROWS - 1);

  /* Horizontal divider between log and input area */
  for (col = 0; col < SCREEN_COLS; col++) {
    fbputchar('-', SEP_ROW, col);
  }

  /* Initial greeting in the log area */
  append_log_text("Welcome to the CSEE 4840 Lab 2 chat client.\n");
  redraw_log();
  redraw_input_line();

  pthread_mutex_unlock(&screen_mutex);
}

/* Redraw the chat log area from stored lines */
static void redraw_log(void)
{
  int row = 0;
  int i;

  clear_region(0, LOG_ROWS - 1);

  /* Draw each stored line in order, starting from the oldest */
  for (i = 0; i < log_count && row < LOG_ROWS; i++) {
    int idx = (log_start + i) % MAX_LOG_LINES;
    fbputs(log_lines[idx], row, 0);
    row++;
  }

  /* Optionally show the current partially-built line as the last line */
  if (current_line_len > 0 && row < LOG_ROWS) {
    char tmp[MAX_LINE_LEN + 1];
    memcpy(tmp, current_line, current_line_len);
    tmp[current_line_len] = '\0';
    fbputs(tmp, row, 0);
  }
}

/* Redraw the input buffer on the bottom two rows and show a cursor */
static void redraw_input_line(void)
{
  int i;
  int display_index;

  /* Clear the input area (two rows below the separator) */
  clear_region(INPUT_START_ROW, SCREEN_ROWS - 1);

  /* Draw the text in the input buffer */
  for (i = 0; i < input_len && i < INPUT_MAX_CHARS; i++) {
    int row = INPUT_START_ROW + (i / SCREEN_COLS);
    int col = i % SCREEN_COLS;
    if (row >= SCREEN_ROWS) break;
    fbputchar(input_buffer[i], row, col);
  }

  /* Draw a simple underscore cursor at the current insertion point */
  if (input_cursor < 0) input_cursor = 0;
  if (input_cursor > input_len) input_cursor = input_len;
  display_index = input_cursor;
  if (display_index < INPUT_MAX_CHARS) {
    int row = INPUT_START_ROW + (display_index / SCREEN_COLS);
    int col = display_index % SCREEN_COLS;
    if (row < SCREEN_ROWS) {
      fbputchar('_', row, col);
    }
  }
}

/* Append arbitrary text to the log, handling wrapping and newlines */
static void append_log_text(const char *text)
{
  const char *p = text;

  while (*p) {
    char ch = *p++;

    if (ch == '\r') {
      /* Ignore carriage return; handle newline separately */
      continue;
    }

    if (ch == '\n' || current_line_len >= MAX_LINE_LEN) {
      /* Finalize the current line (even if empty) */
      if (current_line_len > MAX_LINE_LEN) current_line_len = MAX_LINE_LEN;
      memcpy(log_lines[(log_start + log_count) % MAX_LOG_LINES],
             current_line, current_line_len);
      log_lines[(log_start + log_count) % MAX_LOG_LINES][current_line_len] = '\0';

      if (log_count < MAX_LOG_LINES) {
        log_count++;
      } else {
        /* Buffer full: drop the oldest line */
        log_start = (log_start + 1) % MAX_LOG_LINES;
      }

      /* Start a new line */
      current_line_len = 0;
      if (ch == '\n') {
        continue;
      }
    }

    if (ch != '\n') {
      if (current_line_len < MAX_LINE_LEN) {
        current_line[current_line_len++] = ch;
      }
    }
  }
}

/* Convert a USB HID keycode and modifier state into an ASCII character (0 if none) */
static char keycode_to_ascii(uint8_t keycode, uint8_t modifiers)
{
  int shifted = (modifiers & (USB_LSHIFT | USB_RSHIFT)) != 0;

  /* Letters a-z */
  if (keycode >= 0x04 && keycode <= 0x1d) {
    char base = shifted ? 'A' : 'a';
    return (char)(base + (keycode - 0x04));
  }

  /* Number row 1-0 and punctuation */
  switch (keycode) {
  case 0x1e: return shifted ? '!' : '1';
  case 0x1f: return shifted ? '@' : '2';
  case 0x20: return shifted ? '#' : '3';
  case 0x21: return shifted ? '$' : '4';
  case 0x22: return shifted ? '%' : '5';
  case 0x23: return shifted ? '^' : '6';
  case 0x24: return shifted ? '&' : '7';
  case 0x25: return shifted ? '*' : '8';
  case 0x26: return shifted ? '(' : '9';
  case 0x27: return shifted ? ')' : '0';
  case USB_KEY_SPACE: return ' ';
  case 0x2d: return shifted ? '_' : '-';
  case 0x2e: return shifted ? '+' : '=';
  case 0x2f: return shifted ? '{' : '[';
  case 0x30: return shifted ? '}' : ']';
  case 0x31: return shifted ? '|' : '\\';
  case 0x33: return shifted ? ':' : ';';
  case 0x34: return shifted ? '"' : '\'';
  case 0x35: return shifted ? '~' : '`';
  case 0x36: return shifted ? '<' : ',';
  case 0x37: return shifted ? '>' : '.';
  case 0x38: return shifted ? '?' : '/';
  default:
    break;
  }

  return 0;
}

/* Handle a single key event from the USB keyboard: editing, cursor, sending, etc. */
static void handle_key_event(const struct usb_keyboard_packet *packet, int *exit_requested)
{
  uint8_t keycode = packet->keycode[0];

  /* Basic debouncing: only react when the keycode changes from the last one */
  if (keycode == last_keycode) {
    return;
  }

  if (keycode == 0) {
    /* Key released; remember this and ignore */
    last_keycode = 0;
    return;
  }

  last_keycode = keycode;

  pthread_mutex_lock(&screen_mutex);

  if (keycode == USB_KEY_ESC) {
    *exit_requested = 1;
    pthread_mutex_unlock(&screen_mutex);
    return;
  }

  if (keycode == USB_KEY_ENTER) {
    /* Send the current input buffer to the server and add to log */
    if (input_len > 0) {
      input_buffer[input_len] = '\0';

      /* Send to server, followed by newline */
      write(sockfd, input_buffer, input_len);
      write(sockfd, "\n", 1);

      append_log_text(input_buffer);
      append_log_text("\n");

      /* Clear the input buffer */
      input_len = 0;
      input_cursor = 0;
    }

    redraw_log();
    redraw_input_line();
    pthread_mutex_unlock(&screen_mutex);
    return;
  }

  if (keycode == USB_KEY_BACKSPACE) {
    if (input_cursor > 0 && input_len > 0) {
      int i;
      /* Delete character before cursor */
      for (i = input_cursor - 1; i < input_len - 1; i++) {
        input_buffer[i] = input_buffer[i + 1];
      }
      input_len--;
      input_cursor--;
    }
    redraw_input_line();
    pthread_mutex_unlock(&screen_mutex);
    return;
  }

  if (keycode == USB_KEY_LEFT) {
    if (input_cursor > 0) {
      input_cursor--;
    }
    redraw_input_line();
    pthread_mutex_unlock(&screen_mutex);
    return;
  }

  if (keycode == USB_KEY_RIGHT) {
    if (input_cursor < input_len) {
      input_cursor++;
    }
    redraw_input_line();
    pthread_mutex_unlock(&screen_mutex);
    return;
  }

  /* Regular printable character */
  {
    char ch = keycode_to_ascii(keycode, packet->modifiers);
    if (ch != 0 && input_len < INPUT_MAX_CHARS) {
      int i;

      /* Insert at the cursor position, shifting the tail right */
      for (i = input_len; i > input_cursor; i--) {
        input_buffer[i] = input_buffer[i - 1];
      }
      input_buffer[input_cursor] = ch;
      input_len++;
      input_cursor++;

      redraw_input_line();
    }
  }

  pthread_mutex_unlock(&screen_mutex);
}