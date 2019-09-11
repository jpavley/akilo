/** includes **/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/** defines **/

#define CTRL_KEY(k) ((k) & 0x1f) // bitwise-AND char with mask 00011111
/** data **/

// copy of the original terminal settings
struct termios orig_termios;

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
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
		die("tcsetattr");
}

// turns off cononical mode so we can process each key press
void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = orig_termios;
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

/** output **/

void editorRefreshScreen() {
	clearScreen();
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

// main entry point of program
int main() {
  // turn off cononical mode
  enableRawMode();

  // process key press forever!
  while(1) {
		editorRefreshScreen();
		editorProcessKeypress();
  }
 
  return 0;
}
