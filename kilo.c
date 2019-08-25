#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

// copy of the original terminal settings
struct termios orig_termios;

// restore the original terminal settings
void disableRawMode() {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

// prints an error message and exits
void die(const char *s) {
  perror(s);
  exit(1);
}

// turns off cononical mode so we can process each key press
void enableRawMode() {
  tcgetattr(STDIN_FILENO, &orig_termios);
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

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// main entry point of program
int main() {
  // turn off cononical mode
  enableRawMode();

  // process key press
  while(1) {
	char c = '\0';
	read(STDIN_FILENO, &c, 1); 
    if (iscntrl(c)) {
      printf("%d\r\n", c); // print ASCII code of control char
    } else {
      printf("%d ('%c')\r\n", c, c); // print both ASCII code and char
    }
	if (c == 'q') break;
  }
 
  return 0;
}
