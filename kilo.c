/** includes **/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/** defines **/

#define KILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f) // bitwise-AND char with mask 00011111

// macOS Terminal app hijacks PAGE UP, PAGE DOWN, HOME, END  keys!
// hold down <SHIFT> key for expected VT100 behavior
enum editorKey {
	// arrow keys
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	// delete key
	DEL_KEY,
	// home and end keys
	HOME_KEY,
	END_KEY,
	// page keys
	PAGE_UP,
	PAGE_DOWN
};

/** data **/

typedef struct erow {
	int size;
	char *chars;
} erow;

// store global state
struct editorConfig {
	// cursor position
	int cx, cy;
	// screen size
	int screenrows;
	int screencols;
	// line of text
	int numrows;
	erow row;
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

int editorReadKey() {
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) die("read");
	}

	// look for an <esc> value...
	if (c == '\x1b') { // <esc>
		char seq[3];
		
		// ... if read() times out waiting for the next chars in the sequence
		// assume the user pressed the physical ESC key and return the <esc> code,
		// otherwise it's an <esc> sequence!
		if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

		// no time out so look for special key values
		if (seq[0] == '[') {
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
				if (seq[2] == '~') {
					// special keys: [n~ where n = 0...9 
					switch (seq[1]) {
						case '1': return HOME_KEY; // <home> could be 1, 7
						case '3': return DEL_KEY;
						case '4': return END_KEY; // <end> could be 4, 8
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME_KEY;
						case '8': return END_KEY; // <esc>[8~
					}
				}
			} else {
				// arrow keys: [ followed by A, B, C, or D
				// home & end keys: [ followed by H or F
				switch (seq[1]) {
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
				}
			}
		} else if (seq[0] == 'O') {
			// home & end keys: [O followed by H or F
			switch (seq[1]) {
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
			}
		}

		return '\x1b';
	} else {
		return c;
	}
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
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
			// move cursor forward (999C) and down (999B) 
			// to reach the bottom-right of the screen
			return -1;
		}
		return getCursorPosition(rows, cols); 
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0; // no error 
	}
}

/** file i/o **/

void editorOpen(char *filename) {
	FILE *fp = fopen(filename, "r");
	if (!fp) die("fopen");

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	linelen = getline(&line, &linecap, fp);
	if (linelen != -1) {
		while (linelen > 0 && (line[linelen - 1] == '\n' ||
													 line[linelen - 1] == '\r')) {
			linelen--;
		}
		E.row.size = linelen;
		E.row.chars = malloc(linelen + 1);
		memcpy(E.row.chars, line, linelen);
		E.row.chars[linelen] = '\0';
		E.numrows = 1;
	}
	free(line);
	fclose(fp);
}

/** append buffer **/

struct abuf {
	char *b;
	int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf * ab, const char *s, int len) {
	char *new = realloc(ab->b, ab->len + len);

	if (new == NULL) return;
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void abFree(struct abuf *ab) {
	free(ab->b);
}

/** output **/

// draw chars on first col of each row
void editorDrawRows(struct abuf *ab) {
	int y;
	// for every row on the screen
	for (y = 0; y < E.screenrows; y++) {
		// if the current row is not part of the text buffer
		if (y >= E.numrows) {
			// if the current row is about 1/3 of the screen down
			if (y == E.screenrows / 3) {
				// center vertically
				char welcome[80];
				int welcomelen = snprintf(welcome, sizeof(welcome),
						"Kilo Editor -- Version %s", KILO_VERSION);
				if (welcomelen > E.screencols) welcomelen = E.screencols;
				// center horizonally
				int padding = (E.screencols - welcomelen) / 2;
				if (padding) {
					abAppend(ab, "~", 1);
					padding--;
				}
				while (padding--) abAppend(ab, " ", 1);
				// append message to buffer
				abAppend(ab, welcome, welcomelen);
			} else {
				// the current row is a blank line so print a '~' at the start
				abAppend(ab, "~", 1);
			}
		} else {
			// current line is part of the text buffer
			int len = E.row.size;
			// make the line length fit the lenght of the screen
			if (len > E.screencols) len = E.screencols;
			// append the text buffer line to output buffer
			abAppend(ab, E.row.chars, len);
		}

		// clear each line as it is drawn
		abAppend(ab, "\x1b[K", 3);
		if (y < E.screenrows - 1) {
			abAppend(ab, "\r\n", 2);
		}
	}
}

// update screen
void editorRefreshScreen() {
	struct abuf ab = ABUF_INIT;

	// hide cursor
	abAppend(&ab, "\x1b[?25l", 6);
	// set cursor position
	abAppend(&ab, "\x1b[H", 3);

	editorDrawRows(&ab);

  // move cursor to saved position
	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
	abAppend(&ab, buf, strlen(buf));

	// show cursor
	abAppend(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

/** input **/

void editorMoveCursor(int key) {
	switch (key) {
		case ARROW_LEFT: 
			if (E.cx != 0) {
				E.cx--;
			}
			break;
		case ARROW_RIGHT:
			if (E.cx != E.screencols - 1) {
				E.cx++;
			}
			break;
		case ARROW_UP:
			if (E.cy != 0) {
				E.cy--;
			}
			break;
		case ARROW_DOWN:
			if (E.cy != E.screenrows - 1) {
				E.cy++;
			}
			break;
	}
}

void editorProcessKeypress() {
	int c = editorReadKey();

	switch (c) {
		case CTRL_KEY('q'):
			clearScreen();
			exit(0);
			break;

		case HOME_KEY:
			E.cx = 0;
			break;

		case END_KEY:
			E.cx = E.screencols - 1;
			break;

		case PAGE_UP:
		case PAGE_DOWN:
		{
			int times = E.screenrows;
			while (times--)
				editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
		}
		break;

		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			editorMoveCursor(c);
			break;
	}
}

/** init **/

void initEditor() {
	E.cx = 0;
	E.cy = 0;
	E.numrows = 0;

	if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
		die("getWindowSize");
	}
}

// main entry point of program
int main(int argc, char *argv[]) {
  enableRawMode(); // turn off cononical mode
	initEditor(); // get the window info
	if (argc >= 2) {
		editorOpen(argv[1]); // open and read a file
	}

  // process key press forever!
  while(1) {
		editorRefreshScreen();
		editorProcessKeypress();
  }
 
  return 0;
}
