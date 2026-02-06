/*
 * Copyright (c) 2026, Ae-foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * software name includes “ae”.
 *
 * THE SOFTWARE IS PROVIDED “AS IS” AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE
 * FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY
 * DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdnoreturn.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <ncurses.h>
#include <sys/types.h>
#include <time.h>
#include <stdlib.h>
#include <getopt.h>

#define SHORTOPTS	"sLn:lrhS"
#define DEFAULTNPROMPT	30
#define MAXPATHS	128
#define MODESHORT	0
#define MODELINE	1
#define MODELONG	2
#define DEFAULTMODE	MODESHORT

/*
 *	_ _ E X E _ T
 *
 * structure for representing an
 * executable file
 */
typedef struct __exe_t exe_t;
struct __exe_t
{
	char	name[2048];
	char	path[2048];
	size_t	siz;
};

int		nprompt;		/* -n */
int		mode;			/* -slLr */
char		*pv[MAXPATHS];		/* paths from args*/
size_t		pvsiz;			/* number paths */
exe_t		*ev;			/* executables */
size_t		evsiz;			/* number executables */
size_t		evcap;			/* for realloc() */
exe_t		*last;			/* last exe */
size_t		totsiz;			/* total size all binares */
u_char		Sflag;			/* -S */




/*
 *	F I N I S H
 *
 * terminates the program, clears
 * memory, and returns the terminal
 * to normal mode.
 */
inline static void noreturn finish(int sig)
{
	(void)sig;
	endwin();
	if (ev)
		free(ev);
	exit(0);
}




/*
 *	B Y T E S F M T
 *
 * convert <n> bytes to formatted
 * string presintation.
 */
inline static const char *bytesfmt(size_t n)
{
	const char *units[]={"B","KiB","MiB","GiB",
				"TiB","PiB","EiB"};
	double		c=(double)n;
	static char	fmt[32];

	for (n=0;c>=1024&&n<6;n++) 
		c/=1024;

	snprintf(fmt,sizeof(fmt),
		"%.2f %s",c,units[n]);

	return fmt;
}




/*
 *		E X E C
 *
 * this function create new fork, execute
 * <last>, and close this process
 */
inline static void exec(void)
{
	pid_t	pid;

	if (!last)
		return;
	pid=fork();
	if (pid==-1)
		finish(0);
	else if (pid==0) {
		if (setsid()==-1)
			_exit(1);

		/* Is this closing threads, opening
		 * /dev/null really necessary? */
		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
		open("/dev/null",O_RDONLY);
		open("/dev/null",O_WRONLY);
		open("/dev/null",O_WRONLY);

		/* execute! */
		execl(last->path,last->path,(char*)NULL);
		if (ev)
			free(ev);
		endwin();
		perror("execl");
		_exit(127);
	}

	finish(0);
}




/*
 *		S E A R C H
 *
 * searches for a program by name from <in>,
 * outputs the necessary information according
 * to the mode
 */
inline static void search(char *in)
{
	size_t	fi;
	size_t	sum;
	size_t	n;
	int	y,x;
	u_char	s;

	sum=s=0;
	getyx(stdscr,y,x);
	for (n=0;n<evsiz;n++)
		if (strstr(ev[n].name,in))
			++sum;
	fi=1;
	for (n=0;n<evsiz&&fi<=nprompt;n++) {
		if (!strcmp(ev[n].name,in)) {
			last=&ev[n];
			++s;
		}
		if (strstr(ev[n].name,in)) {
			if (!s)
				last=&ev[n];
			if (mode==MODELONG||mode==MODESHORT)
				mvprintw((Sflag)?0:1,0,"exec %s (%s)"
					" %ld\n",last->path,
					bytesfmt(ev[n].siz),sum);
			if (mode==MODELONG) {
				mvhline((Sflag)?2:3,0,ACS_HLINE,45);
				mvprintw(fi+((Sflag)?2:3),0,"%s\n",
					ev[n].name);
			}
			++fi;
		}
	}

	move(y,x);
}




/*
 *			I N I T
 *
 * collects information about all executable files in
 * the directories specified by the user, stores them
 * in <exe_t> in the <ev> array,
 *
 * allocating memory to it and increasing its size
 * every <EXECS_STEP> by <EXECS_STEP>.
 */
inline static void init(void)
{
	exe_t		exe;
	char		buf[65535];
	struct stat	st;
	struct dirent	*d;
	DIR		*dir;
	size_t		n,t1;

	d=NULL;
	dir=NULL;

	for (n=0;n<pvsiz;n++) {
		if (!(dir=opendir(pv[n])))
			continue;
		for (;(d=readdir(dir));) {
			if (!strcmp(d->d_name, ".")||!strcmp(d->d_name, ".."))
				continue;

			bzero(buf,sizeof(buf));
			snprintf(buf,sizeof(buf),"%s/%s",pv[n],d->d_name);

			if (stat(buf,&st)==-1)
				continue;

			if (evsiz==evcap) {
#define EXECS_STEP 512
				t1=evcap+EXECS_STEP;
				exe_t *t=realloc(ev,t1*sizeof(exe_t));
				if (!t)
					finish(0);
				ev=t;
				evcap=t1;
			}

			bzero(&exe,sizeof(exe));
			snprintf(exe.name,sizeof(exe.name),"%s",d->d_name);
			snprintf(exe.path,sizeof(exe.path),"%s",buf);
			exe.siz=st.st_size;
			totsiz+=st.st_size;

			ev[evsiz++]=exe;
		}
		closedir(dir);
	}
}




/*
 *		L O O P
 *
 * The main loop of the program, where it takes
 * the unbuffered input in <in> and does the
 * appropriate things on top of it.
 */
inline static int loop(void)
{
	char	in[2048];
	chtype	c;
	int	n;
	int	cpos;

	bzero(in,sizeof(in));
	cpos=(mode==MODELINE)?0:(Sflag)?1:2;
	n=0;

	switch (mode) {
		case MODELONG:
		case MODESHORT:
			if (!Sflag)
				mvprintw(0, 0, "loaded %ld files from"
					" %ld paths (%s)\n",evsiz,
					pvsiz,bytesfmt(totsiz));
			mvprintw((Sflag)?0:1,0,"exec %s (%s)"
				" %ld\n",ev->path,
				bytesfmt(ev->siz),evsiz);
			break;
	}

	mvprintw(cpos,0,": ");
	refresh();

	for (;;) {
		c=getch();
		if (c=='\n')
			break;
		else if (c==KEY_BACKSPACE||c==127) {
			if (n>0) {
				in[--n]='\0';
				move(cpos,(2+n));
				delch();
			}
		} else if (n<sizeof(in)-1) {
			in[n++]=c;
			addch(c);
		}
		in[n]='\0';
		search(in);
	}
	exec();

	/* NOTREACHED */
	return 0;
}




/*
 *	M A I N ()
 */
int main(int c, char **av)
{
	int	rez;
	int	n;
	int	j;

	signal(SIGINT,finish);
	srand(time(NULL));

	mode=DEFAULTMODE;
	Sflag=0;
	totsiz=0;
	evsiz=0;
	nprompt=DEFAULTNPROMPT;
	last=NULL;
	evcap=0;
	ev=NULL;
	pvsiz=0;
	*pv=NULL;

	if (c<=1) {
L0:		fprintf(stderr,"Usage %s [options] <path ...,>\n",*av);
		fprintf(stderr,"  -s \t\tenable short display mode\n");
		fprintf(stderr,"  -L \t\tenable long display mode\n");
		fprintf(stderr,"  -n <max> \tspecify the maximum number"
			" of prompts\n");
		fprintf(stderr,"  -l \t\tenable line display mode\n");
		fprintf(stderr,"  -r \t\tspecify random display mode\n");
		fprintf(stderr,"  -S \t\tskip the very first loading info\n");
		fprintf(stderr,"  -h \t\tshow this menu and exit\n");
		fprintf(stderr,"\nReleased in %s %s\n",__DATE__,__TIME__);
		finish(0);
	}

	while ((rez=getopt(c,av,SHORTOPTS))!=EOF) {
		switch (rez) {
		case 'S':
			++Sflag;
			break;
		case 's':
			mode=MODESHORT;
			break;
		case 'l':
			mode=MODELINE;
			break;
		case 'L':
			mode=MODELONG;
			break;
		case 'r':
			mode=rand()%3;
			break;
		case 'n': {
			unsigned long long val;
			char *endp;

			errno=0;
			val=strtoull(optarg,&endp,10);
			if (errno==ERANGE||val>
				(unsigned long long)	
				SIZE_MAX) {
L1:					fprintf(stderr,"Failed convert"
					" \"%s\" to num\n",optarg);
				finish(0);
			}
			while (isspace((u_char)*endp))
				endp++;
			if (*endp!='\0')
				goto L1;
			if (val<1||val>INT_MAX)
				goto L1;

			nprompt=(int)val;
			break;
		}
		case 'h': case '?': default:
			goto L0;
		}
	}

	n=c-optind;
	if (n<=0)
		goto L0;
	if (n>MAXPATHS) {
		fprintf(stderr,"Too many paths!\n");
		finish(0);
	}
	pvsiz=n;
	n=optind;
	j=0;
	while (n<c) {
		pv[j++]=av[n];
		++n;
	}

	init();
	if (evsiz==0) {
		fprintf(stderr,"Not found files in paths!\n");
		finish(0);
	}

	initscr();
	cbreak();
	noecho();
	keypad(stdscr,TRUE);
	
	return loop();
}
