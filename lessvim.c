#include <asm-generic/errno-base.h>
#include <asm-generic/ioctls.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define VIM_VERSION "0.0.1"

#define CTRL_KEY(k) ((k)&0x1f)

#define ABUF_INIT                                                              \
  { NULL, 0 }

// Editor {{{
struct editorConfigStruct {
  int cursorX;
  int cursorY;
  struct termios originalTerminalAttributes;
  int screenRows;
  int screenCols;
};
// }}}

struct editorConfigStruct editorConfig;

struct aBuffer {
  char *buffer;
  int len;
};

void appendBuffer(struct aBuffer *ab, const char *s, int len) {
  char *newBuf = realloc(ab->buffer, ab->len + len);

  if (newBuf == NULL)
    return;

  memcpy(&newBuf[ab->len], s, len);

  ab->buffer = newBuf;
  ab->len += len;
}

void freeBuffer(struct aBuffer *ab) { free(ab->buffer); }

void editorDrawTildes(struct aBuffer *ab) {
  for (int y = 0; y < editorConfig.screenRows; y++) {
    appendBuffer(ab, "\x1b[K", 3);
    if (y == editorConfig.screenRows - 1) {
      appendBuffer(ab, "~", 1);
    } else {
      appendBuffer(ab, "~\r\n", 3);
    }
  }
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

void editorDrawRows(struct aBuffer *ab) {
  editorDrawTildes(ab);
  bufferAddWelcome(ab);
}

void buffMoveCursor(struct aBuffer *ab) {
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", editorConfig.cursorY + 1,
           editorConfig.cursorX + 1);
  appendBuffer(ab, buf, strlen(buf));
};

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

void showErrorAndExit(const char *message) {
  editorRefreshScreen();
  perror(message);
  exit(1);
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

  getWindowSize(&editorConfig.screenRows, &editorConfig.screenCols);

  editorRefreshScreen();
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

char editorReadKey() {
  int readExitCode;
  char c;
  while ((readExitCode = read(STDIN_FILENO, &c, 1)) != 1) {
    if (readExitCode == -1 && errno != EAGAIN)
      showErrorAndExit("Terminal read failed");
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

void editorMoveCursor(char key) {
  switch (key) {
  case 'h':
    break;
  case 'h':
    break;
  case 'h':
    break;
  case 'h':
    break;
  }
}

void editorProcessKeypress() {
  char c = editorReadKey();

  switch (c) {
  case CTRL_KEY('q'):
    editorRefreshScreen();
    exit(0);
    break;

  case CTRL_KEY('r'):
    editorRefreshScreen();
    break;

  default:
    printInput(c);
    break;
  }
}

int main(void) {
  enableRawMode();
  initEditor();

  while (1) {
    editorProcessKeypress();
  }

  disableRawMode();
  return EXIT_SUCCESS;
}
