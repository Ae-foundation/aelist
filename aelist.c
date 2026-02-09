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

#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <locale.h>
#include <ncurses.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SHORTOPTS      "sLn:lrhSP"
#define DEFAULTNPROMPT 30
#define MAXPATHS       512
#define MODESHORT      0
#define MODELINE       1
#define MODELONG       2
#define DEFAULTMODE    MODESHORT

/*
 *	_ _ E X E _ T
 *
 * structure for representing an
 * executable file
 */
typedef struct __exe_t exe_t;
struct __exe_t {
	char name[2048], path[2048];
	size_t siz;
};

static int mode = DEFAULTMODE;	     /* -slLr */
static char *pv[MAXPATHS];	     /* paths from args*/
static size_t psiz;		     /* number paths */
static exe_t *ev;		     /* executables */
static size_t evsiz;		     /* number executables */
static int nprompt = DEFAULTNPROMPT; /* -n */
static size_t evcap;		     /* for realloc() */
static exe_t *last;		     /* last exe */
static size_t totsiz;		     /* total size all binares */
static u_char Sflag;		     /* -S */
static u_char Pflag;		     /* -P */

/*
 *	F I N I S H
 *
 * terminates the program, clears
 * memory, and returns the terminal
 * to normal mode.
 */
static void noreturn
finish(int sig)
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
inline static const char *
bytesfmt(size_t n)
{
	const char *units[] = { "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB" };
	double c = (double)n;
	static char fmt[32];

	for (n = 0; c >= 1024 && n < 6; n++)
		c /= 1024;

	snprintf(fmt, sizeof(fmt), "%.2f %s", c, units[n]);

	return fmt;
}

/*
 *		E X E C
 *
 * this function create new fork, execute
 * <last>, and close this process
 */
static void
exec(void)
{
	if (!last)
		return;
	pid_t pid = fork();
	if (pid < 0)
		finish(0);
	else if (pid == 0) {
		if (setsid() < 0)
			_exit(1);

		int fd = open("/dev/null", O_RDWR);
		if (fd != -1) {
			dup2(fd, STDIN_FILENO);
			dup2(fd, STDOUT_FILENO);
			dup2(fd, STDERR_FILENO);
			if (fd > 2)
				close(fd);
		}

		execl(last->path, last->path, NULL);
		_exit(1);
	}

	finish(0);
}

/*
 *		P A R S E P A T H
 *
 * parses the PATH environment variable and places
 * the paths from it into the pv array, starting
 * with pvsiz and gradually increasing it. if at
 * any point the number of paths becomes greater
 * than or equal to the maximum, the process is
 * terminated
 */
static void
parsepath(void)
{
	char *p = getenv("PATH");
	char *token = strtok(p, ":");
	while (token) {
		pv[psiz++] = token;
		if (psiz >= MAXPATHS)
			return;
		token = strtok(NULL, ":");
	}
}

/*
 *		S E A R C H
 *
 * searches for a program by name from <in>,
 * outputs the necessary information according
 * to the mode
 */
static void
search(char *in)
{
	size_t fi = 1, sum, n;
	int y, x;
	bool s = 0;

	getyx(stdscr, y, x);
	n = sum = evsiz;
	while (n--)
		if (!strstr(ev[n].name, in))
			--sum;

	for (n = 0; n < evsiz && fi <= nprompt; n++) {
		if (!strcmp(ev[n].name, in)) {
			*last = ev[n];
			s = 1;
		}

		if (strstr(ev[n].name, in)) {
			if (!s)
				last = &ev[n];
			if (mode == MODELONG || mode == MODESHORT)
				mvprintw((Sflag) ? 0 : 1, 0,
				    "exec %s (%s) %ld\n", last->path,
				    bytesfmt(ev[n].siz), sum);
			if (mode == MODELONG) {
				mvhline((Sflag) ? 2 : 3, 0, ACS_HLINE, 45);
				mvprintw(fi + ((Sflag) ? 2 : 3), 0, "%s\n",
				    ev[n].name);
			}
			++fi;
		}
	}
	while (fi <= nprompt) {
		move(fi++ + ((Sflag) ? 2 : 3), 0);
		clrtoeol();
	}

	move(y, x);
}

/*
 *			I N I T
 *
 * collects information about all executable files in
 * the directories specified by the user, stores them
 * in <exe_t> in the <ev> array,
 *
 * allocating memory to it and increasing its size
 * every <STEP> by <STEP>.
 */
static void
init(void)
{
	exe_t exe;
	char buf[4096];
	struct stat st;
	struct dirent *d;
	DIR *dir;
	size_t n;

	for (n = 0; n < psiz; n++) {
		if (!(dir = opendir(pv[n])))
			continue;
		while ((d = readdir(dir))) {
			if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, ".."))
				continue;

			snprintf(buf, sizeof(buf), "%s/%s", pv[n], d->d_name);
			if (access(buf, X_OK) != 0)
				continue;
			if (stat(buf, &st) < 0)
				continue;

			if (evsiz == evcap) {
				evcap = evcap + 1024;
				exe_t *t = realloc(ev, evcap * sizeof(exe_t));
				if (!t)
					finish(0);
				ev = t;
			}

			snprintf(exe.name, sizeof(exe.name), "%s", d->d_name);
			snprintf(exe.path, sizeof(exe.path), "%s", buf);
			exe.siz = st.st_size;

			totsiz += st.st_size;
			ev[evsiz++] = exe;
		}
		closedir(dir);
	}

	if (evsiz == 0) {
		fprintf(stderr, "Not found files in paths!\n");
		finish(0);
	}
}

/*
 *		L O O P
 *
 * The main loop of the program, where it takes
 * the unbuffered input in <in> and does the
 * appropriate things on top of it.
 */
static int
loop(void)
{
	int n = 0, pos = (mode == MODELINE) ? 0 : (Sflag) ? 1 : 2;
	char in[2048];
	chtype c;

	switch (mode) {
	case MODELONG:
	case MODESHORT:
		if (!Sflag)
			mvprintw(0, 0, "loaded %ld files from %ld paths (%s)\n",
			    evsiz, psiz, bytesfmt(totsiz));
		mvprintw((Sflag) ? 0 : 1, 0, "exec %s (%s) %ld\n", ev->path,
		    bytesfmt(ev->siz), evsiz);
		break;
	}

	mvprintw(pos, 0, ": ");
	refresh();

	while ((c = getch()) != '\n') {
		switch (c) {
		case KEY_BACKSPACE:
		case 127:
			if (n > 0) {
				in[--n] = 0;
				move(pos, (2 + n));
				delch();
			}
			break;
		default:
			if (n < sizeof(in) - 1) {
				in[n++] = c;
				addch(c);
			}
			break;
		}
		in[n] = 0;
		search(in);
	}

	exec();

	/* NOTREACHED */
	return 0;
}

/*
 *	A E L I S T
 */
int
main(int c, char **av)
{
	char **ptr;
	int n;

	signal(SIGINT, finish);
	srand(time(NULL));
	setlocale(0, "");

	if (c < 1) {
	usage:
		fprintf(stderr, "Usage %s [options] <path ...,>\n", *av);
		fprintf(stderr, "  -s \t\tenable short display mode\n");
		fprintf(stderr, "  -L \t\tenable long display mode\n");
		fprintf(stderr,
		    "  -n <max> \tspecify the maximum number"
		    " of prompts\n");
		fprintf(stderr, "  -l \t\tenable line display mode\n");
		fprintf(stderr, "  -r \t\tspecify random display mode\n");
		fprintf(stderr, "  -S \t\tskip the very first loading info\n");
		fprintf(stderr, "  -P \t\tload $PATH in paths\n");
		fprintf(stderr, "  -h \t\tshow this menu and exit\n");
		fprintf(stderr, "\nReleased in %s %s\n", __DATE__, __TIME__);
		finish(0);
	}

	while ((n = getopt(c, av, SHORTOPTS)) != EOF) {
		switch (n) {
		case 'S':
			++Sflag;
			break;
		case 'P':
			++Pflag;
			break;
		case 's':
			mode = MODESHORT;
			break;
		case 'l':
			mode = MODELINE;
			break;
		case 'L':
			mode = MODELONG;
			break;
		case 'r':
			mode = rand() % 3;
			break;
		case 'n': {
			unsigned long long val;
			char *endp;

			errno = 0;
			val = strtoull(optarg, &endp, 10);
			if (errno == ERANGE) {
			L1:
				fprintf(stderr,
				    "Failed convert"
				    " \"%s\" to num\n",
				    optarg);
				finish(0);
			}
			while (isspace((u_char)*endp))
				endp++;
			if (*endp != '\0')
				goto L1;
			if (val < 1 || val > INT_MAX)
				goto L1;

			nprompt = (int)val;
			break;
		}
		case 'h':
		case '?':
		default:
			goto usage;
		}
	}

	c -= optind;
	psiz += c;

	if (psiz <= 0 || Pflag)
		parsepath();
	if (psiz > MAXPATHS) {
		fprintf(stderr, "Too many paths!\n");
		finish(0);
	}

	av += optind;
	ptr = pv;

	while (c--)
		*ptr++ = *av++;

	init();
	initscr();
	cbreak();
	noecho();
	keypad(stdscr, TRUE);

	return loop();
}
