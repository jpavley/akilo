/** includes **/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/** defines **/

#define CTRL_KEY(k) ((k) & 0x1f) // bitwise-AND char with mask 00011111

/** data **/

// store global state
struct editorConfig {
	// screen size
	int screenrows;
	int screencols;
	
	// copy of the original terminal settings
	struct termios orig_termios;
};

struct editorConfig E;

/** terminal **/

// clear screen and home the cursor
void clearScreen() {
	write(STDOUT_FILENO, "\x1b[2J", 4); // erase all display
	write(STDOUT_FILENO, "\x1b[H", 3); // move cursor to r1, c1
}

// prints an error message and exits
void die(const char *s) {
	clearScreen();

  perror(s);
  exit(1);
}

// restore the original terminal settings
void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
		die("tcsetattr");
}

// turns off cononical mode so we can process each key press
void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = E.orig_termios;
  // input modes
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  // output modes
  raw.c_oflag &= ~(OPOST);
  // control modes
  raw.c_cflag |= (CS8);
  // local modes
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  // control characters
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

char editorReadKey() {
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) die("read");
	}
	return c;
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
	unsigned int i = 0;

	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

	while (i < sizeof(buf) - 1) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
		if (buf[i] == 'R') break;
		i++;
	}
	buf[i] = '\0';

	if (buf[0] != '\x1b' || buf[1] != '[') return -1;
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

	return 0;
}

// get the row count and col count
int getWindowSize(int *rows, int *cols) {
	struct winsize ws;

	if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
		return getCursorPosition(rows, cols); 
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0; // no error 
	}
}

/** output **/

// draw chars on first col of each row
void editorDrawRows() {
	int y;
	for (y = 0; y < E.screenrows; y++) {
		write(STDOUT_FILENO, "~\r\n", 3);
	}
}

// update screen
void editorRefreshScreen() {
	clearScreen();
	editorDrawRows();
	write(STDOUT_FILENO, "\x1b[H", 3);
}

/** input **/

void editorProcessKeypress() {
	char c = editorReadKey();

	switch (c) {
		case CTRL_KEY('q'):
			clearScreen();
			exit(0);
			break;
	}
}

/** init **/

void initEditor() {
	if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
		die("getWindowSize");
	}
}

// main entry point of program
int main() {
  enableRawMode(); // turn off cononical mode
	initEditor(); // get the window info

  // process key press forever!
  while(1) {
		editorRefreshScreen();
		editorProcessKeypress();
  }
 
  return 0;
}
