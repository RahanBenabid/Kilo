/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <_stdio.h>
#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/_types/_ssize_t.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define KILO_VERSION "0.0.1"

/*
 * a macro that does bitwise-AND with the value 00011111 (sets the last 3 bits
 * to 0) which mirrors what the `Ctrl` key does, ASCII is designed like this on
 * purpose ()
 */
#define CTRL_KEY(k) ((k) & 0x1f)

/*
 * we set a representation for arrow keys that doesn't conflict with other keys
 * like wasd, we give them a large `int` value, which will be out of range of a
 * `char` representation
 *
 * by setting the first const to 1000, the others will get incremented values of
 * 1001 -> 1003
 */
enum editorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};

/*** data ***/

/*
 * datatype to store a row of text in the editor
 * stands for editor row, stores a line of text to the dynamically allocated
 *
 * character data
 * `typedef` helps us refer to the type sa erow and not struct erow, so cleaner
 * code pretty much
 */
typedef struct erow {
  int size;
  char *chars;
} erow;

struct termios orig_termios;

struct editorConfig {
  // coordinates of the cursor
  int cx, cy;
  // for scrolling, init at 0 which means we'll start at the top of the file
  int rowoff;
  int screenrows;
  int screencols;
  int numrows;
  erow *row;
  struct termios orig_termios;
};

// global variable containing our editor state
struct editorConfig E;

/*** terminal ***/

/*
 * prints an error message then exists the program
 *
 * most c library functions that fail will set the global errno variable to
 * indicate what the error was
 *
 * so when the other functions return -1 in failure, we call this function
 */
void die(const char *s) {
  // for a clean exit in case of an error, same thing in the ctrl+q below
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}

void disableRawMode(void) {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
    die("tcsetattr");
  }
  /*
   * the TSCAFLUSH discards any unread input
   * before applying the changes to the terminal
   *
   * and expects standard input to be a terminal, feeding the program a pipe
   will cause it to fail
   */
}

// gets us fully into raw mode
void enableRawMode(void) {
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    die("tcgetattr");

  /*
   * called automatically when the program exits using `atexit()` this is to
   * make sure we store the original terminal attributes of the user
   */
  atexit(disableRawMode);

  struct termios raw = E.orig_termios;
  raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | ICRNL | IXON);
  /*
   *  modify input flags
   *
   * disable ctrl-s and ctrl-q
   * this comes from termios.h the I stands for input flag XON comes from  the
   * names of the two control characters ctrl-s and ctrl-q produce
   *
   * ICRNL diables ctrl-m, I stands for input flag
   * CT stands for carriage return
   * NL stands for new line
   *
   * the other flags aren't really important for modern terminals but better be
   * safe than sorry
   */

  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  /*
   * modify local flag
   *
   * for the operation, we use bitwise NOT then bitwise AND
   * the ECHO feature causes each key to be printed in the terminal in here we
   * used by programs like `sudo`
   *
   * we also turn off canonical mode, to be able to read input byte-by-byte now
   * the program quits as soon as you press `q`
   *
   * ISIG is to disable ctrl-c and ctrl-z and ctrl-y
   *
   * IEXTEN  diables ctrl-v and fixes ctrl-o
   */

  raw.c_oflag &= ~(OPOST);
  /*
   * modify output flag
   *
   * in this case we only have the case where it translates a "\n" into a "\r\n"
   * like old typewriters
   */

  raw.c_cflag |= (CS8);
  /*
   * this isn't a flag but more of a mask with multiple bits that we set using
   * the bitwise OR, it sets the character size to be 8 bits per byte, which is
   * already the case for most systems
   */

  raw.c_cc[VMIN] = 0;
  /*
   * this sets the min number of bytes of input needed before read() can return
   * when we set it to 0, read() returns as soon as there is any input to be
   * read
   */

  raw.c_cc[VTIME] = 1;
  /*
   * The maximum time to wait for input before read() returns,
   * measured in tenths of a second (1 = 100 milliseconds timeout).
   */

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
  /*
   * Apply changes after all pending output is written, and discard any
   * unread input
   */
}

/*
 * wait for one keypress then return it later
 */
int editorReadKey(void) {
  int nread;
  char c;
  // wait for keypress
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    /*
     * If it's a real failure, crash. If it just timed out because the user
     * hasn't pressed a key yet, ignore it and keep trying
     */
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }

  // we detect an escape character
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
            // \x1b[1~
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

    // not an escape sequence we recognize, return a normal escape press
    return '\x1b';
  } else {
    // return the character that was pressed
    return c;
  }
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;

  /*
   * ask the terminal for the cursor coordinates by sending the query "\x1b[6n",
   * via stdout, the terminal emulator parses them, creates the coordinates
   * (e.g.
   * "\x1b[24;80R") and injects them into the stdin through the pseudo-terminal
   * master device thanks to the OS, which will be read later using `read()`
   */
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;

  // put the previously returned value inside a buffer for parsing later
  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[i] == 'R')
      break;
    i++;
  }
  buf[i] = '\0';

  // we make sure out buffer holds an escape sequence
  if (buf[0] != '\x1b' || buf[1] != '[')
    return -1;
  /*
   * parse the two number using `sscanf()`
   * we pass a pointer to the third character to skip '\x1b['
   * then we parse to integers separated by a `;` and put them into rows and
   * cols
   */
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    return -1;

  return 0;
}

/*
 * returns multiple values (pass values by int references) and the return code
 * for error handling later this is a common pattern in C
 */
int getWindowSize(int *rows, int *cols) {
  struct winsize ws;

  /*
   * the operation inside the if is both an affectation and a test
   *
   * error return with the code -1
   * check when values aren't 0 so it doesn't create an error on success, we
   * pass the values back by setting the int references that were passed to the
   * function
   */
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    // (fallback method) move the cursor to bottom right and get coordinates
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1;
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/*** row operations ***/

void editorAppendRow(char *s, size_t len) {
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

  int at = E.numrows;
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';
  E.numrows++;
}

/*** file i/o ***/

/*
 * this will be for opening and reading file from disk
 */
void editorOpen(char *filename) {
  FILE *fp = fopen(filename, "r");
  if (!fp)
    die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  /*
   * the `getline()` function is useful when we don't know how much memory
   * we should allocate for each line, so it takes care of mem management
   *
   * passing it a NULL line and linecap of 0 makes it allocate new memory for
   * the next line it reads
   *
   * returns the length of the line it reads, or -1 if it's at the end of the
   * file and there are no more file to read
   */

  // continuously read the file line by line until we reach the EOF
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    /*
     * strip the newline character, because we know each line ends with a
     * newline, so there is no use keeping it
     */
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;
    editorAppendRow(line, linelen);
  }

  free(line);
  fclose(fp);
}

/*** append buffer ***/

/*
 * create a dynamic string that supports appending this consists of a pointer to
 * our buffer in memory and a lengthABUF_INIT represents an empty buffer, it
 * acts as a constructor
 */
struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}

/*
 * we use realloc to make sure enough memory to available to hold the new string
 *
 * realloc either extends the size of the memory block or allocates a new one
 * somewhere else in the heap
 *
 * then we use a memcpy to copy the string s after the end of the current `new`
 * buffer
 *
 * then we update the pointer and length of the abuf using the new values
 */
void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL)
    return;

  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

// acts like a destructor to deallocate the dynamic memory used by abuf
void abFree(struct abuf *ab) { free(ab->b); }

/*** output ***/

void editorScroll(void) {
  /*
   * check if the cursor is above the visible window, if so, scroll up to where
   * the cursor is
   */
  if (E.cy < E.rowoff)
    E.rowoff = E.cy;

  /*
   * same thing with the bottom, scroll down, this has a little more complicated
   * logic, since E.rowoff refers to what's at the top of the screen, we have to
   * get the screen rows
   */
  if (E.cy >= E.rowoff + E.screenrows)
    E.rowoff = E.cy - E.screenrows + 1;
}

void drawWelcomeMessage(struct abuf *ab) {
  char welcome[80];
  int welcomelen = snprintf(welcome, sizeof(welcome),
                            "Kilo editor -- version %s", KILO_VERSION);
  if (welcomelen > E.screencols)
    welcomelen = E.screencols;

  int padding = (E.screencols - welcomelen) / 2;
  if (padding) {
    abAppend(ab, "~", 1);
    padding--;
  }
  while (padding--)
    abAppend(ab, " ", 1);

  abAppend(ab, welcome, welcomelen);
}

void editorDrawWelcomeMessage(struct abuf *ab) {
  char welcome[80];

  // `snprintf` is good for formatting buffers
  int welcomelen = snprintf(welcome, sizeof(welcome),
                            "Kilo editor -- version %s", KILO_VERSION);

  if (welcomelen > E.screencols)
    welcomelen = E.screencols;

  int padding = (E.screencols - welcomelen) / 2;

  if (padding) {
    abAppend(ab, "~", 1);
    padding--;
  }

  while (padding--)
    abAppend(ab, " ", 1);

  abAppend(ab, welcome, welcomelen);
}

void editorDrawRows(struct abuf *ab) {
  // draw the edges using the E struct for something dynamic
  for (int y = 0; y < E.screenrows; y++) {
    // to get the row of the file we want to display
    int filerow = y + E.rowoff;

    if (filerow >= E.numrows) {
      // welcome message
      if (E.numrows == 0 && y == E.screenrows / 3) {
        editorDrawWelcomeMessage(ab);
      } else {
        abAppend(ab, "~", 1);
      }
    } else {
      int len = E.row[filerow].size;
      if (len > E.screencols)
        len = E.screencols;
      abAppend(ab, E.row[filerow].chars, len);
    }

    // clear each line
    abAppend(ab, "\x1b[K", 3);
    if (y < E.screenrows - 1) {
      abAppend(ab, "\r\n", 2);
    }
  }
}

void editorRefreshScreen(void) {
  editorScroll();

  struct abuf ab = ABUF_INIT;

  /*
   * there could be a possible flicker effect when trying to draw the scene, the
   * cursor might appear in the middle of the screen for a split second while
   * the terminal is drawing to the screen so we hide the cursor before
   * refreshing the screen then display it again
   * the `h` and `l` commands are for set and reset mode
   */
  abAppend(&ab, "\x1b[?25l", 6);

  // put the cursor back at the top leftmost of the screen
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  /*
   * move back the cursor to the stored position by constructing our own escape
   * sequence then applying it (puts the cursor in the given position)
   */
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, E.cx + 1);
  abAppend(&ab, buf, strlen(buf));

  // re-display the cursor
  abAppend(&ab, "\x1b[?25h", 6);

  // write the buffer's contents our to standard output & free memory
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

/*** input ***/

void editorMoveCursor(int key) {
  switch (key) {
  case ARROW_LEFT:
    if (E.cx != 0)
      E.cx--;
    break;
  case ARROW_RIGHT:
    if (E.cx != E.screencols - 1)
      E.cx++;
    break;
  case ARROW_UP:
    if (E.cy != 0)
      E.cy--;
    break;
  case ARROW_DOWN:
    if (E.cy < E.numrows)
      E.cy++;
    break;
  }
}

/*
 * waits for a keypress then processes it later...
 *
 * we will map various ctrl key combinations or special keys to different
 * editor functions
 */
void editorProcessKeypress(void) {
  int c = editorReadKey();

  switch (c) {
  case CTRL_KEY('q'):
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;

  case HOME_KEY:
    E.cx = 0;
    break;

  case END_KEY:
    E.cx = E.screencols - 1;
    break;

  case PAGE_UP:
  case PAGE_DOWN: {
    int times = E.screenrows;
    while (times--)
      editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
  }

  case ARROW_UP:
  case ARROW_DOWN:
  case ARROW_LEFT:
  case ARROW_RIGHT:
    editorMoveCursor(c);
    break;
  }
}

/*** init ***/

/*
 * initialize all the fields in the E struct
 */
void initEditor(void) {
  E.cx = 0;
  E.cy = 0;
  E.rowoff = 0;
  E.numrows = 0;
  E.row = NULL;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");
}

int main(int argc, char *argv[]) {
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

// source https://viewsourcecode.org/snaptoken/kilo/
/*
 * TODO:
 * - add blinking using an event loop (poll api) for text blinking and other
 * stuff
 * - handle forced shutdown (if the code gets stuck anywhere that's not waiting
 * for a keystroke, the user can't exit the editor unless he closes the window
 * or kills the process in another window)
 */
