//continue from step 85

/*** defines ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE
#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT {NULL, 0}
#define KILO_VERSION "0.0.1"
#define KILO_TABSTOP 4

/*** includes ***/
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <sys/ioctl.h>

enum editorKey{//integer identifiers to common escape sequence keypresses
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

typedef struct erow { //store a line of text as a pointer to the dynamically allocated character data and length
	int size;
	int rsize;
	char *chars;
	char *render;
} erow;

struct editorConfig{
	int screenrows;	//rows of terminal
	int screencols; //columns of terminal
	int cx,cy; //cursor position
	int rowoff;
	int coloff;
	int numrows; //number of rows
	erow *row; //data structure to storea line of text
	struct termios orig_termios;//original terminal i/o configuration structure
};

/*** terminal ***/

struct editorConfig E;

void die(const char *s){//kill the program
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO,"\x1b[H",3);
	perror(s);
	exit(1);
}

void disableRawMode(){
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios)==-1) die("tcsetattr");//restore terminal attributes to their original values
}

void enableRawMode(){//enable raw input mode in terminal so that our keyboard input is sent to the program immediately 
	if(tcgetattr(STDIN_FILENO,&E.orig_termios)==-1) die("tcsetattr");//read terminal attributes into 'orig_termios' 
	atexit(disableRawMode);
	struct termios raw = E.orig_termios;//copy original terminal attr into 'raw'
	raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);//disables ctrl+s, ctrl+q, ctrl+m signals
	raw.c_cflag &= ~(CS8);
	raw.c_oflag &= ~(OPOST);//disable output processing
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG );//disable echoing in terminal (key presses wont be printed on screen) 
			       		//with a bitwise NOT, then a bitwise AND to set 4th bit to zero
					//also disables canonical mode so program reads byte by byte
					//also disables ctrl+c, ctrl+z, ctrl+v 
	if(tcsetattr(STDIN_FILENO,TCSAFLUSH,&raw)==-1)die("tcsetattr");//write terminal attricbutes
}

int editorReadKey(){//function for low level keypress reading. reads a keypress and returns it 
	int nread; 
	char c; 
	while((nread=read(STDIN_FILENO, &c, 1))!= 1){
		if(nread == -1 && errno != EAGAIN) die("read");
	}
	if(c=='\x1b'){//allow editor to interpret 'escape sequence' inputs as a single keypress
		char seq[3];//if we read an escape character, check for two more bytes, and if those timeout we assume it was just 
			    //the user pressing 'escape'. If there are two more bytes, we look to see if its an arrow key
		if(read(STDIN_FILENO, &seq[0],1) != 1) return '\x1b';
		if(read(STDIN_FILENO, &seq[1],1) != 1) return '\x1b';
		if(seq[0] == '['){//handle certain escape sequences here
			if(seq[1] >= '0' && seq[1] <= '9'){
				if(read(STDIN_FILENO,&seq[2],1)!=1) return '\x1b';
				if(seq[2] == '~'){
					switch (seq[1]){
						case '1': return HOME_KEY;
						case '3': return DEL_KEY;
						case '4': return END_KEY;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME_KEY;
						case '8': return END_KEY;
					}
				}
			}else{
				switch (seq[1]){
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
				}
			}
		}else if(seq[0] == '0'){
			switch(seq[1]){
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
			}
		}
		return '\x1b';
	} else{
	  return c;
	}
}

int getCursorPosition(int *rows, int *cols){
	char buf[32];
	unsigned int i = 0;

	if(write(STDOUT_FILENO, "\x1b[6n",4) != 4) return -1;

	while(i<sizeof(buf)-1){
		if(read(STDIN_FILENO, &buf[i], 1)) break;
		if(buf[i] == 'R') break;
		i++;
	}
	buf[i]='\0';
	
	if (buf[0] != '\x1b' || buf[1] != '[') return -1;
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

	return 0;
}

int getWindowSize(int *rows, int *cols){
	struct winsize ws;
	if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){//place the rows and cols of terminal into winsize struct 
		if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B",12)!=12) return -1;
		return getCursorPosition(rows, cols);
	}else{
		*cols = ws.ws_col;//set the int references passed to function
		*rows = ws.ws_row;
		return 0;
	}
}

/*** row operations ***/

void editorUpdateRow(erow *row){
	int tabs=0;
	int j;
	for(j=0; j<row->size;j++){
		if(row->chars[j] == '\t') tabs++;
	}
		
	free(row->render);
	row->render = malloc(row->size+tabs*(KILO_TABSTOP - 1)+1);

	int idx=0;
	for(j=0; j<row->size;j++){
		if(row->chars[j] == '\t'){
			row->render[idx++] = ' ';
			while(idx % KILO_TABSTOP != 0) row->render[idx++] = ' ';
		} else{
			row->render[idx++] = row->chars[j];
		}
	}
	row->render[idx] = '\0';
	row->rsize=idx;
}

void editorAppendRow(char *s, size_t len){
	E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

	int at = E.numrows;
	E.row[at].size = len;
	E.row[at].chars = malloc(len+1);
	memcpy(E.row[at].chars,s,len);
	E.row[at].chars[len] = '\0';
	
	E.row[at].rsize=0;
	E.row[at].render=NULL;
	editorUpdateRow(&E.row[at]);

	E.numrows++;	
}

/*** file i/o ***/

void editorOpen(char *filename) {//open an actual file
	FILE *fp = fopen(filename, "r");
	if(!fp) die("fopen");

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	while((linelen = getline(&line, &linecap, fp)) != -1){
		while(linelen > 0 && (line[linelen-1] == '\n' || line[linelen - 1] == '\r')) linelen--;
		editorAppendRow(line, linelen);
	}
	
	free(line);
	fclose(fp);
}

/*** append buffer ***/

struct abuf{
	char *b;
	int len;
};

void abAppend(struct abuf *ab, const char *s, int len) {
	  char *new = realloc(ab->b, ab->len + len);
	    if (new == NULL) return;
	      memcpy(&new[ab->len], s, len);
	        ab->b = new;
		  ab->len += len;
}

void abFree(struct abuf *ab) {
	  free(ab->b);
}


/*** input ***/

void editorMoveCursor(int key){
	erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
	switch(key){
		case ARROW_LEFT:
			if(E.cx != 0){
				E.cx--;
			}else if(E.cy>0){//scroll left to previous line
				E.cy--;
				E.cx = E.row[E.cy].size;
			}
			break;
		case ARROW_RIGHT:
			if(row && E.cx < row->size){
				E.cx++;
			}else if(row && E.cx == row->size){//scroll right to next line
				E.cy++;
				E.cx = 0;
			}
			break;
		case ARROW_UP:
			if(E.cy != 0){
				E.cy--;
			}
			break;
		case ARROW_DOWN:
			if(E.cy < E.numrows){
				E.cy++;
			}
			break;	
	}
	row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
	int rowlen = row ? row->size : 0;
	if(E.cx > rowlen){
			E.cx = rowlen;
	}
}

void editorProcessKeypress(){//function to map keypresses to editor operations
	int c=editorReadKey();

	switch(c){
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO,"\x1b[H",3);
			exit(0);
			break;
		case HOME_KEY:
			E.cx=0;
			break;
		case END_KEY:
			E.cx=E.screencols-1;
			break;
		
		case PAGE_UP:
		case PAGE_DOWN:
			{
				int times = E.screenrows;
				while(times--){
					editorMoveCursor(c==PAGE_UP ? ARROW_UP : ARROW_DOWN);//simulate user pressing up or down 
											     //enough times to reach bottom or top
				}
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

/*** output ***/

void editorScroll(){//if cursor moved outside of visible window, adjust row offset
	if(E.cy < E.rowoff){
		E.rowoff = E.cy;
	}
	if(E.cy >= E.rowoff + E.screenrows){
		E.rowoff = E.cy - E.screenrows + 1;
	}
	if(E.cx < E.coloff){
		E.coloff = E.cx;
	}
	if(E.cx >= E.coloff + E.screencols){
		E.coloff = E.cx - E.screencols + 1;
	}
}

void editorDrawRows(struct abuf *ab){//draw the rows in the editor program
	int y; 
	for (y=0;y<E.screenrows;y++){
		int filerow = y + E.rowoff;	
		if(filerow >= E.numrows){
			if(E.numrows == 0 && y==E.screenrows/3){//display welcome message only when text buffer is empty
				char welcome[80];
				int welcomelen = snprintf(welcome, sizeof(welcome),
					"Kilo editor -- version %s", KILO_VERSION);//display welcome message
				if(welcomelen > E.screencols) welcomelen = E.screencols;
				int padding = (E.screencols - welcomelen)/2;//center the 'welcomne' message
				if(padding){
					abAppend(ab,"~",1);
					padding--;
				}
				while(padding--) abAppend(ab, " ", 1);//add spaces before message to 'center' it
				abAppend(ab, welcome, welcomelen);
			}
			else{
				abAppend(ab, "~", 1);//draw ~ on the left for each row like in vim
			}
		}else{
			int len = E.row[filerow].rsize - E.coloff;
			if(len < 0) len = 0;
			if(len > E.screencols) len = E.screencols;
			abAppend(ab, &E.row[filerow].render[E.coloff], len);
		}

		abAppend(ab, "\x1b[K",3);
		if(y<E.screenrows-1){
			abAppend(ab, "\r\n",2);
		}
	}
}

void editorRefreshScreen(){//refresh the screen
	editorScroll();
	
	struct abuf ab = ABUF_INIT;//append buffer
	
	abAppend(&ab, "\x1b[?25l",6);
	abAppend(&ab, "\x1b[H",3);
	
	editorDrawRows(&ab);
	
	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff)+1, (E.cx - E.coloff) + 1);
	abAppend(&ab,buf,strlen(buf));
	
	abAppend(&ab, "\x1b[?25h",6);
	
	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}


/*** init ***/

void initEditor(){//initialize the editor
	E.cx=0;// initialize cursor position
	E.cy=0;
	E.rowoff=0;
	E.coloff=0;
	E.numrows = 0;// initialize the number of rows
	E.row = NULL;
	if(getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(int argc, char *argv[]){//main function 
	enableRawMode();
	initEditor();
  	if(argc >= 2){
		editorOpen(argv[1]); //if the user passed in a file name when calling kilo, open that file, otherwise start with a blank file
	}

	while(1){
		editorRefreshScreen();
		editorProcessKeypress();
	}
	return 0;
}


