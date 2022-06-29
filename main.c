#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h> /* get terminal size */
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>  // access to POSIX APIs

/* for storing text in a row */
typedef struct erow {
  int size;
  char* chars;
} erow;

// saving original version of terminal attributes
struct editorConfig {
  int cx, cy; /* cursor position */
  int rowoff;    /* vertical scrolling */
  int screenrows;
  int screencols;
  int numrows;
  erow *row;
  struct termios orig_termios;
};

struct editorConfig E;

/*** define ***/
#define CTRL_KEY(k) ((k) & (0x1f))
/* AND key with 00011111 for stripping upper bits */
#define VERSION "0.0.1"

enum editorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  PAGE_UP,
  PAGE_DOWN,
  HOME_KEY,
  END_KEY,
  DEL_KEY
};

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

int editorReadKey() {
  int nread;
  int c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) {
      die("read");
    }
  }

  /* Page Up is sent as <esc>[5~ and Page Down is sent as <esc>[6~.
  The Home key could be sent as <esc>[1~, <esc>[7~, <esc>[H, or <esc>OH.
  Similarly, the End key could be sent as <esc>[4~, <esc>[8~, <esc>[F, or
  <esc>OF. Let’s handle all of these cases. DEL key = <esc>[3~
   */
  if (c == '\x1b') {
    char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
          return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
            case '1':
              return HOME_KEY;
            case '3':
              return DEL_KEY;
            case '4':
              return END_KEY;
            case '5':
              return PAGE_UP;
            case '6':
              return PAGE_DOWN;
            case '7':
              return HOME_KEY;
            case '8':
              return END_KEY;
          }
        }
      } else {
        switch (seq[1]) {
          case 'A':
            return ARROW_UP;
          case 'B':
            return ARROW_DOWN;
          case 'C':
            return ARROW_RIGHT;
          case 'D':
            return ARROW_LEFT;
          case 'H':
            return HOME_KEY;
          case 'F':
            return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
      }
    }
    return '\x1b';
  } else {
    return c;
  }
}

int getCursorPosition(int* rows, int* cols) {
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;
  /* n command can be used to ask terminal info, 6 asks for cursor position */

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[i] == 'R')
      break;
    i++;
  }
  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[')
    return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    return -1;

  editorReadKey();
  return -1;
}

int getWindowSize(int* rows, int* cols) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDIN_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      /* B, C commands stop from exceeding terminal edge. thats why instead of
       * [999;999H */
      return -1;
    return getCursorPosition(rows, cols);
  } else {
    *rows = ws.ws_row;
    *cols = ws.ws_col;
    return 0;
  }
}

/******* ROW OPERATION *********/
void editorAppendRow(char *s, size_t len) {
  E.row = realloc(E.row, sizeof(erow) * (E.numrows+1));
  int at = E.numrows;

  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';
  E.numrows++;
}

/*******  FILE I/O  ********/

void editorOpen(char* filename) {
  FILE* fp = fopen(filename, "r");
  if (!fp)
    die("fopen");

  /* linecap is length of line it read or -1 if no more line to read */
  char* line = NULL;
  size_t linecap = 0;
  ssize_t linelen;

  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
      linelen--;
      editorAppendRow(line, linelen);
    }
  }
  free(line);
  fclose(fp);
}

/*** append buffer ***/
struct abuf {
  char* b;
  int len;
};

#define ABUF_INIT \
  { NULL, 0 }

void abAppend(struct abuf* ab, char* s, int len) {
  char* new = realloc(ab->b, ab->len + len);

  if (new == NULL)
    return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}
/* realloc() and free() comes from stdlib
 * memcpy() comes from string.h
 */

void abFree(struct abuf* ab) {
  free(ab->b);
}

/*** output ***/

void initEditor() {
  E.cx = 0;
  E.cy = 0;
  E.rowoff = 0;
  E.numrows = 0;
  E.row = NULL;
  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");
}

void editorScroll() {
  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.screenrows + E.rowoff) {
    E.rowoff = E.cy - E.screenrows + 1;
  }
}

void editorMoveCursor(int key) {
  switch (key) {
    case ARROW_RIGHT:
      if (E.cx != E.screencols - 1)
        E.cx++;
      break;
    case ARROW_UP:
      if (E.cy != 0)
        E.cy--;
      break;
    case ARROW_LEFT:
      if (E.cx != 0)
        E.cx--;
      break;
    case ARROW_DOWN:
      if (E.cy < E.numrows)
        E.cy++;
      break;
  }
}

void editorProcessKeypress() {
  int c = editorReadKey();

  switch (c) {
    case CTRL_KEY('q'):
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      /* H positions the cursor */
      exit(0);
      break;

    case PAGE_UP:
    case PAGE_DOWN: {
      int times = E.screenrows;
      while (times--) {
        editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      }
    }

    case HOME_KEY:
      E.cx = 0;
      break;

    case END_KEY:
      E.cx = E.screencols - 1;
      break;

    case ARROW_LEFT:
    case ARROW_DOWN:
    case ARROW_UP:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;

    default:
      break;
  }
}

void editorDrawRows(struct abuf* ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff;
    if (filerow >= E.numrows) {
      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen =
            snprintf(welcome, sizeof(welcome), "Text Editor version --- %s",
                     VERSION); /* sprinf() */

        if (welcomelen > E.screencols) {
          welcomelen = E.screencols;
        }

        int padding = (E.screencols - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--)
          abAppend(ab, " ", 1);

        abAppend(ab, welcome, welcomelen);
      } else {
        abAppend(ab, "~", 1);
      }
    } else {
      int len = E.row[filerow].size;
      if (len > E.screencols) {
        len = E.screencols;
      }
      abAppend(ab, E.row[filerow].chars, len);
    }

    abAppend(ab, "\x1b[K", 3); /* clear only one line */
    if (y < E.screenrows - 1)
      abAppend(ab, "\r\n", 2);
  }
}

void editorRefreshScreen() {
  editorScroll();

  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6); /* hide cursor */
  // abAppend(&ab, "\x1b[2J", 4);
  abAppend(&ab, "\x1b[H", 3);
  /* escape sequence to clear entire screen \x1b [2J  == <esc>[2J */
  /* reposition cursor at 1;1 */
  editorDrawRows(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, E.cx + 1);
  abAppend(&ab, buf, strlen(buf));
  /* We add 1 to E.cy and E.cx to convert from 0-indexed values to the 1-indexed
   * values that the terminal uses.
   */

  abAppend(&ab, "\x1b[?25h", 6); /* show cursor */

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

/*** main ***/

int main(int argc, char* argv[]) {
  enableRawMode();
  initEditor();
  if (argc >= 2) {
    editorOpen(argv[1]);
  }

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}
