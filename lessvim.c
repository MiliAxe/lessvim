#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <asm-generic/errno-base.h>
#include <asm-generic/ioctls.h>
#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#define VIM_VERSION "0.0.1"

#define CTRL_KEY(k) ((k)&0x1f)

#define ABUF_INIT                                                              \
  { NULL, 0 }

enum escapeKey {
  ARROW_UP = 'A',
  ARROW_DOWN = 'B',
  ARROW_RIGHT = 'C',
  ARROW_LEFT = 'D',
  ESCAPE_HOME = 'H',
  ESCAPE_END = 'F'
};

enum editorKey {
  KEY_LEFT = 'h',
  KEY_RIGHT = 'l',
  KEY_DOWN = 'j',
  KEY_UP = 'k',
  KEY_HOME = 1000,
  KEY_END
};

typedef struct erow {
  int size;
  char *chars;
} erow;

struct editorConfigStruct {
  int cursorX;
  int cursorY;
  struct termios originalTerminalAttributes;
  int screenRows;
  int screenCols;
  int rowoff;
  int numrows;
  erow *row;
};

struct editorConfigStruct editorConfig;

struct aBuffer {
  char *buffer;
  int len;
};

void editorScrollDown() { editorConfig.rowoff++; }
void editorScrollUp() { editorConfig.rowoff--; }
void editorScrollVertical() {
  if (editorConfig.cursorY >= editorConfig.screenRows &&
      editorConfig.rowoff <= editorConfig.numrows - editorConfig.screenRows) {
    editorScrollDown();
  }
  if (editorConfig.cursorY < 0 && editorConfig.rowoff > 0) {
    editorScrollUp();
  }
}

void appendBuffer(struct aBuffer *ab, const char *s, int len) {
  char *newBuf = realloc(ab->buffer, ab->len + len);

  if (newBuf == NULL)
    return;

  memcpy(&newBuf[ab->len], s, len);

  ab->buffer = newBuf;
  ab->len += len;
}
void showErrorAndExit(const char *message) {
  perror(message);
  exit(1);
}

void freeBuffer(struct aBuffer *ab) { free(ab->buffer); }

void editorDrawTildes(struct aBuffer *ab) {
  for (int y = 0; y < editorConfig.screenRows; y++) {
    if (y + 1 > editorConfig.numrows) {
      appendBuffer(ab, "\x1b[K", 3);
      if (y == editorConfig.screenRows - 1) {
        appendBuffer(ab, "~", 1);
      } else {
        appendBuffer(ab, "~\r\n", 3);
      }
    }
  }
}

void editorAppendRow(char *s, size_t len) {
  // editorConfig.row->size = len;
  // editorConfig.row->chars = malloc(len + 1);
  // memcpy(editorConfig.row->chars, s, len);
  // editorConfig.row->chars[len] = '\0';
  // editorConfig.numrows = 1;
  editorConfig.row =
      realloc(editorConfig.row, sizeof(erow) * (editorConfig.numrows + 1));

  int at = editorConfig.numrows;
  editorConfig.row[at].size = len;
  editorConfig.row[at].chars = malloc(len + 1);
  memcpy(editorConfig.row[at].chars, s, len);
  editorConfig.row[at].chars[len] = '\0';
  editorConfig.numrows++;
}

void editorOpen(char *filename) {
  FILE *filePtr = fopen(filename, "r");
  if (!filePtr)
    showErrorAndExit("Failed to read file");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;

  while ((linelen = getline(&line, &linecap, filePtr)) != -1) {
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
      linelen--;
    }
    editorAppendRow(line, linelen);
  }
  free(line);
  fclose(filePtr);
}

void bufferAddWelcome(struct aBuffer *ab) {
  // Step 1: find the appropiate location to add the message
  const int MESSAGE_ROW = editorConfig.screenRows / 3;
  const char DELIM = '\r';

  // i is the index of the location to add the message to
  int i = 0;
  int delimeterCount = 0;

  while (i < ab->len) {
    if (ab->buffer[i] == DELIM) {
      delimeterCount++;
    }

    if (delimeterCount == MESSAGE_ROW) {
      break;
    }
    i++;
  }

  // Step 2: add the message
  char welcomeString[80];
  int welcomeStringLen = snprintf(welcomeString, sizeof(welcomeString),
                                  "lessvim -- Version %s", VIM_VERSION);

  // Step 3: calculate the padding and add it before our message
  int padding = (editorConfig.screenCols - welcomeStringLen) / 2 - 1;
  char paddingString[padding];
  memset(paddingString, ' ', padding);

  // Step 4: copy the buffer to a temp buffer and add the message
  char tempStr[ab->len + sizeof(welcomeString) + sizeof(paddingString)];
  memcpy(tempStr, ab->buffer, i);
  memcpy(tempStr + i, paddingString, padding);
  memcpy(tempStr + i + padding, welcomeString, welcomeStringLen);
  memcpy(tempStr + i + padding + welcomeStringLen, ab->buffer + i, ab->len - i);

  // Step 5: copy the temp buffer to the original buffer
  appendBuffer(ab, tempStr, welcomeStringLen + ab->len);
}

void bufferAddFileLines() {
  int len;

  for (int i = editorConfig.rowoff;
       i < editorConfig.screenRows + editorConfig.rowoff; i++) {
    len = editorConfig.row[i].size;
    appendBuffer(ab, "\x1b[K", 3);
    appendBuffer(ab, editorConfig.row[i].chars, len);
    if (i != editorConfig.screenRows + editorConfig.rowoff - 1) {
      appendBuffer(ab, "\r\n", 2);
    }
  }
}

void editorDrawRows(struct aBuffer *ab) {
  if (editorConfig.numrows == 0) {
    bufferAddWelcome(ab);
  }

  bufferAddFileLines();
  editorDrawTildes(ab);
}

void buffMoveCursor(struct aBuffer *ab) {
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", editorConfig.cursorY + 1,
           editorConfig.cursorX + 1);
  appendBuffer(ab, buf, strlen(buf));
}

void editorRefreshScreen() {
  struct aBuffer ab = ABUF_INIT;

  // Hide cursor
  appendBuffer(&ab, "\x1b[?25l", 6);
  appendBuffer(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);
  buffMoveCursor(&ab);

  // Show cursor
  appendBuffer(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.buffer, ab.len);

  freeBuffer(&ab);
}

void getCursorPosition(int *rows, int *cols) {
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
    showErrorAndExit("Failed to get cursor position");
  }

  char buffer[32];

  for (unsigned int i = 0; i < sizeof(buffer) - 1; i++) {
    if (read(STDIN_FILENO, &buffer[i], 1) != 1)
      break;
    if (buffer[i] == 'R')
      break;
  }

  buffer[31] = '\0';

  if (buffer[0] != '\x1b' || buffer[1] != '[') {
    showErrorAndExit("Failed to get cursor position");
  }
  if (sscanf(&buffer[2], "%d:%d", rows, cols) != 2) {
    showErrorAndExit("Failed to parse curosr postion");
  }
}

void getWindowSize(int *rows, int *columns) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    showErrorAndExit("Get window size failed");
  } else {
    *rows = ws.ws_row;
    *columns = ws.ws_col;
  }
}

void initEditor() {
  editorConfig.cursorX = 0;
  editorConfig.cursorY = 0;
  editorConfig.numrows = 0;
  editorConfig.row = NULL;
  editorConfig.rowoff = 0;

  getWindowSize(&editorConfig.screenRows, &editorConfig.screenCols);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH,
                &editorConfig.originalTerminalAttributes) == -1) {
    showErrorAndExit("Failed to disable raw mode");
  };
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &editorConfig.originalTerminalAttributes) == -1)
    showErrorAndExit("Failed to get terminal attributes");

  atexit(disableRawMode);

  struct termios terminalAttributes = editorConfig.originalTerminalAttributes;

  terminalAttributes.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
  terminalAttributes.c_oflag &= ~(OPOST);
  terminalAttributes.c_cflag &= ~(CS8);
  terminalAttributes.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

  terminalAttributes.c_cc[VMIN] = 0;
  terminalAttributes.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &terminalAttributes))
    showErrorAndExit("Failed to set terminal attributes");
}

int editorReadKey() {
  int readExitCode;
  char c;
  while ((readExitCode = read(STDIN_FILENO, &c, 1)) != 1) {
    if (readExitCode == -1 && errno != EAGAIN)
      showErrorAndExit("Terminal read failed");
  }

  if (c == '\x1b') {
    char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';

    // Check for arrow keys
    if (seq[0] == '[') {
      switch (seq[1]) {
      case ARROW_UP:
        return KEY_UP;
      case ARROW_DOWN:
        return KEY_DOWN;
      case ARROW_RIGHT:
        return KEY_RIGHT;
      case ARROW_LEFT:
        return KEY_LEFT;

      case ESCAPE_END:
        return KEY_END;
      case ESCAPE_HOME:
        return KEY_HOME;
      }
    }
  }
  return c;
}

void printInput(char c) {
  if (iscntrl(c)) {
    printf("%d\r\n", c);
  } else {
    printf("%d ('%c')\r\n", c, c);
  }
}

void fixCursorIfOutScreen() {
  if (editorConfig.cursorX >= editorConfig.screenCols) {
    editorConfig.cursorX = editorConfig.screenCols - 1;
  }
  if (editorConfig.cursorY >= editorConfig.screenRows) {
    editorConfig.cursorY = editorConfig.screenRows - 1;
  }

  if (editorConfig.cursorX < 0) {
    editorConfig.cursorX = 0;
  }
  if (editorConfig.cursorY < 0) {
    editorConfig.cursorY = 0;
  }
}

void editorMoveCursor(char key) {
  switch (key) {
  case KEY_LEFT:
    editorConfig.cursorX--;
    break;
  case KEY_RIGHT:
    editorConfig.cursorX++;
    break;
  case KEY_DOWN:
    editorConfig.cursorY++;
    break;
  case KEY_UP:
    editorConfig.cursorY--;
    break;
  }
}

void moveCursorStartOfLine() { editorConfig.cursorX = 0; }

void moveCursorEndOfLine() {
  editorConfig.cursorX = editorConfig.screenCols - 1;
}

void editorProcessKeypress() {
  int key = editorReadKey();

  switch (key) {
  case CTRL_KEY('q'):
    editorRefreshScreen();
    exit(0);
    break;

  case CTRL_KEY('r'):
    editorRefreshScreen();
    break;

  case KEY_UP:
  case KEY_DOWN:
  case KEY_RIGHT:
  case KEY_LEFT:
    editorMoveCursor(key);
    editorScrollVertical();
    fixCursorIfOutScreen();
    editorRefreshScreen();
    break;

  case KEY_HOME:
    moveCursorStartOfLine();
    break;

  case KEY_END:
    moveCursorEndOfLine();
    break;

  case 'm':
    editorScrollDown();
    editorRefreshScreen();
    break;

  case 'n':
    editorScrollUp();
    editorRefreshScreen();
    break;

  default:
    // printInput(key);
    break;
  }
}

int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  if (1 || argc >= 2) {
    editorOpen("lessvim.c");
  }
  editorRefreshScreen();

  while (1) {
    editorProcessKeypress();
  }

  // disableRawMode();
  return EXIT_SUCCESS;
}
