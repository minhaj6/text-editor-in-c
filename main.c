#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h> /* get terminal size */
#include <termios.h>
#include <unistd.h>  // access to POSIX APIs

// saving original version of terminal attributes
struct editorConfig {
  int screenrows;
  int screencols;
  struct termios orig_termios;
};

struct editorConfig E;

/*** define ***/
#define CTRL_KEY(k) ((k) & (0x1f))
/* AND key with 00011111 for stripping upper bits */

/*** terminal ***/

void die(const char* s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  /* J command clears the screen. 0J clears from cursor to the end
   * 1J clears from top upto the cursor, 2J clears the entire screen
   */

  /* perror() looks at the global errno variable and prints a descriptive error
   * message for it. It also prints the string given to it before it prints the
   * error message */
  perror(s);  // perror() comes from <stdio.h>
  exit(1);    // exit() comes from <stdlib.h>
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode() {
  /* By default the terminal starts in canonical mode, also called cooked mode.
   * In this mode, keyboard input is only sent to your program when the user
   * presses Enter. Need to change to 'raw' mode.
   */
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    die("tcgetattr");

  atexit(disableRawMode);  // this useful function comes from <stdlib.h>

  struct termios raw = E.orig_termios;
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
  // turn off echo, turn off canonical
  // mode, ISIG for SIGINT/SIGSTP signals off. IEXTEN makes ^v ^o off.
  raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON | ICRNL);
  // turn off software flow control like ^s and ^q
  // ICRNL turns off ^m creating carriage return to new line
  raw.c_oflag &= ~(OPOST); /* turn off output processing */
  raw.c_cflag |= (CS8);

  raw.c_cc[VMIN] = 0; /* minimum byte input before read() can return */
  raw.c_cc[VTIME] = 1;
  /* maximum time before read() returns. it returns number of byte read. if
   * timeout, returns 0 */

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

char editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) {
      die("read");
    }
  }
  return c;
}

int getCursorPosition(int* rows, int* cols) {
  char buf[32];
  unsigned int i=0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;
  /* n command can be used to ask terminal info, 6 asks for cursor position */

  while (i < sizeof(buf)-1) {
    if(read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if(buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
  
  editorReadKey();
  return -1;
}

int getWindowSize(int* rows, int* cols) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDIN_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
    /* B, C commands stop from exceeding terminal edge. thats why instead of [999;999H */
      return -1;
    return getCursorPosition(rows, cols);
  } else {
    *rows = ws.ws_row;
    *cols = ws.ws_col;
    return 0;
  }
}

/*** output ***/

void initEditor() {
  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");
  printf("row: %d, col: %d \r\n", E.screenrows, E.screencols);
}

void editorProcessKeypress() {
  char c = editorReadKey();

  switch (c) {
    case CTRL_KEY('q'):
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      /* H positions the cursor */
      exit(0);
      break;

    default:
      break;
  }
}

void editorDrawRows() {
  int y;
  for (y = 0; y < E.screenrows-1; y++) {
    write(STDOUT_FILENO, "~\r\n", 3);
  }
  write(STDOUT_FILENO, "\r\n", 2);
}

void editorRefreshScreen() {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  /* escape sequence to clear entire screen \x1b [2J  == <esc>[2J */

  write(STDOUT_FILENO, "\x1b[H", 3); /* reposition cursor at 1;1 */

  editorDrawRows();
  write(STDOUT_FILENO, "\x1b[H", 3); /* reposition cursor at 1;1 */
}

/*** main ***/

int main() {
  enableRawMode();
  initEditor();

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}
