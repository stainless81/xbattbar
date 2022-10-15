/*
 * $Id: xbattbar.c,v 1.16.2.4 2001/02/02 05:25:29 suguru Exp $
 *
 * xbattbar: yet another battery watcher for X11
 */

/*
 * Copyright (c) 1998-2001 Suguru Yamaguchi <suguru@wide.ad.jp>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Modified 2009 by Dmitry E. Oboukhov <unera@debian.org>:
 *   SRC has been split to two parts: bar and battery-check
 *   This part contains bar part of xbattbar
 */

static char *ReleaseVersion="1.4.2";

#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <X11/Xlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

#define PollingInterval 10	/* APM polling interval in sec */
#define BI_THICKNESS    3	/* battery indicator thickness in pixels */

#define BI_Bottom	0
#define BI_Top		1
#define BI_Left		2
#define BI_Right	3
#define BI_Horizontal	((bi_direction & 2) == 0)
#define BI_Vertical	((bi_direction & 2) == 2)

#define myEventMask (ExposureMask|EnterWindowMask|LeaveWindowMask|VisibilityChangeMask)
#define DefaultFont "fixed"
#define DiagXMergin 20
#define DiagYMergin 5

/*
 * Global variables
 */

int ac_line = -1;               /* AC line status */
int battery_level = -1;         /* battery level */

unsigned long onin, onout;      /* indicator colors for AC online */
unsigned long offin, offout;    /* indicator colors for AC offline */

int elapsed_time = 0;           /* for battery remaining estimation */

/* indicator default colors */
char *ONIN_C   = "green";
char *ONOUT_C  = "olive drab";
char *OFFIN_C  = "blue";
char *OFFOUT_C = "red";

char *EXTERNAL_CHECK = "/usr/lib/xbattbar/xbattbar-check-apm";
char *EXTERNAL_CHECK_ACPI = "/usr/lib/xbattbar/xbattbar-check-acpi";
char *EXTERNAL_CHECK_SYS = "/usr/lib/xbattbar/xbattbar-check-sys";

int alwaysontop = False;

struct itimerval IntervalTimer;     /* APM polling interval timer */

int bi_direction = BI_Bottom;       /* status bar location */
int bi_height;                      /* height of Battery Indicator */
int bi_width;                       /* width of Battery Indicator */
int bi_x;                           /* x coordinate of upper left corner */
int bi_y;                           /* y coordinate of upper left corner */
int bi_thick = BI_THICKNESS;        /* thickness of Battery Indicator */
int bi_interval = PollingInterval;  /* interval of polling APM */

Display *disp;
Window winbar;                  /* bar indicator window */
Window winstat = -1;            /* battery status window */
GC gcbar;
GC gcstat;
unsigned int width,height;
XEvent theEvent;

/*
 * function prototypes
 */
void InitDisplay(void);
Status AllocColor(char *, unsigned long *);
void battery_check(void);
void plug_proc(int);
void battery_proc(int);
void redraw(void);
void showdiagbox(void);
void disposediagbox(void);
void usage(char **);
void about_this_program(void);
void estimate_remain(void);

/*
 * usage of this command
 */
void about_this_program()
{
  fprintf(stderr,
	  "This is xbattbar version %s, "
	  " Copyright (c) 1998-2001 Suguru Yamaguchi\n"
	  "  modified 2009 by Dmitry E. Oboukhov <unera@debian.org>\n",
	  ReleaseVersion);
}

void usage(char **argv)
{
  fprintf(stderr,
    "\n"
    "usage:\t%s [-a] [-h|v] [-p sec] [-t thickness]\n"
    "\t\t[-I color] [-O color] [-i color] [-o color]\n"
    "\t\t[ top | bottom | left | right ]\n"
    "-a:         always on top.\n"
    "-v, -h:     show this message.\n"
    "-t:         bar (indicator) thickness. [def: 3 pixels]\n"
    "-p:         polling interval. [def: 10 sec.]\n"
    "-I, -O:     bar colors in AC on-line. [def: \"green\" & \"olive drab\"]\n"
    "-i, -o:     bar colors in AC off-line. [def: \"blue\" and \"red\"]\n"
    "top, bottom, left, right: bar localtion. [def: \"bottom\"]\n"
    "\n"
    "-c:         use ACPI checker for getting battery status\n"
    "-s script:  use external script for getting battery status\n",
    argv[0]);
  _exit(0);
}

/*
 * AllocColor:
 * convert color name to pixel value
 */
Status AllocColor(char *name, unsigned long *pixel)
{
  XColor color,exact;
  int status;

  status = XAllocNamedColor(disp, DefaultColormap(disp, 0),
                           name, &color, &exact);
  *pixel = color.pixel;

  return(status);
}

/*
 * InitDisplay:
 * create small window in top or bottom
 */
void InitDisplay(void)
{
  Window root;
  int x,y;
  unsigned int border,depth;
  XSetWindowAttributes att;

  if((disp = XOpenDisplay(NULL)) == NULL) {
      fprintf(stderr, "xbattbar: can't open display.\n");
      _exit(1);
  }

  if(XGetGeometry(disp, DefaultRootWindow(disp), &root, &x, &y,
                 &width, &height, &border, &depth) == 0) {
    fprintf(stderr, "xbattbar: can't get window geometry\n");
    _exit(1);
  }

  if (!AllocColor(ONIN_C,&onin) ||
       !AllocColor(OFFOUT_C,&offout) ||
       !AllocColor(OFFIN_C,&offin) ||
       !AllocColor(ONOUT_C,&onout)) {
    fprintf(stderr, "xbattbar: can't allocate color resources\n");
    _exit(1);
  }

  switch (bi_direction) {
  case BI_Top: /* (0,0) - (width, bi_thick) */
    bi_width = width;
    bi_height = bi_thick;
    bi_x = 0;
    bi_y = 0;
    break;
  case BI_Bottom:
    bi_width = width;
    bi_height = bi_thick;
    bi_x = 0;
    bi_y = height - bi_thick;
    break;
  case BI_Left:
    bi_width = bi_thick;
    bi_height = height;
    bi_x = 0;
    bi_y = 0;
    break;
  case BI_Right:
    bi_width = bi_thick;
    bi_height = height;
    bi_x = width - bi_thick;
    bi_y = 0;
  }

  winbar = XCreateSimpleWindow(disp, DefaultRootWindow(disp),
                              bi_x, bi_y, bi_width, bi_height,
                              0, BlackPixel(disp,0), WhitePixel(disp,0));

  /* make this window without its titlebar */
  att.override_redirect = True;
  XChangeWindowAttributes(disp, winbar, CWOverrideRedirect, &att);

  XMapWindow(disp, winbar);

  gcbar = XCreateGC(disp, winbar, 0, 0);
}

main(int argc, char **argv)
{
  extern char *optarg;
  extern int optind;
  int ch;

  about_this_program();
  while ((ch = getopt(argc, argv, "at:f:hI:i:O:o:p:vs:cr")) != -1)
    switch (ch) {
    case 'c':
      EXTERNAL_CHECK = EXTERNAL_CHECK_ACPI;
      break;

    case 'r':
      EXTERNAL_CHECK = EXTERNAL_CHECK_SYS;
      break;

    case 's':
      EXTERNAL_CHECK = optarg;
      break;

    case 'a':
      alwaysontop = True;
      break;

    case 't':
    case 'f':
      bi_thick = atoi(optarg);
      break;

    case 'I':
      ONIN_C = optarg;
      break;
    case 'i':
      OFFIN_C = optarg;
      break;
    case 'O':
      ONOUT_C = optarg;
      break;
    case 'o':
      OFFOUT_C = optarg;
      break;

    case 'p':
      bi_interval = atoi(optarg);
      break;

    case 'h':
    case 'v':
      usage(argv);
      break;
    }
  argc -= optind;
  argv += optind;

  if (argc > 0) {
    if (strcasecmp(*argv, "top") == 0)
      bi_direction = BI_Top;
    else if (strcasecmp(*argv, "bottom") == 0)
      bi_direction = BI_Bottom;
    else if (strcasecmp(*argv, "left") == 0)
      bi_direction = BI_Left;
    else if (strcasecmp(*argv, "right") == 0)
      bi_direction = BI_Right;
  }

  /*
   * set APM polling interval timer
   */
  if (!bi_interval) {
    fprintf(stderr,"xbattbar: can't set interval timer\n");
    _exit(1);
  }
  alarm(bi_interval);

  /*
   * X Window main loop
   */
  InitDisplay();
  signal(SIGALRM, (void *)(battery_check));
  battery_check();
  XSelectInput(disp, winbar, myEventMask);
  while (1) {
    XWindowEvent(disp, winbar, myEventMask, &theEvent);
    switch (theEvent.type) {
    case Expose:
      /* we redraw our window since our window has been exposed. */
      redraw();
      break;

    case EnterNotify:
      /* create battery status message */
      showdiagbox();
      break;

    case LeaveNotify:
      /* destroy status window */
      disposediagbox();
      break;

    case VisibilityNotify:
      if (alwaysontop) XRaiseWindow(disp, winbar);
      break;

    default:
      /* for debugging */
      fprintf(stderr,
	      "xbattbar: unknown event (%d) captured\n",
	      theEvent.type);
    }
  }
}

void redraw(void)
{
  if (ac_line) {
    plug_proc(battery_level);
  } else {
    battery_proc(battery_level);
  }
  estimate_remain();
}


void showdiagbox(void)
{
  XSetWindowAttributes att;
  XFontStruct *fontp;
  XGCValues theGC;
  int pixw, pixh;
  int boxw, boxh;
  char diagmsg[64];

  /* compose diag message and calculate its size in pixels */
  sprintf(diagmsg,
         "AC %s-line: battery level is %d%%",
         ac_line ? "on" : "off", battery_level);
  fontp = XLoadQueryFont(disp, DefaultFont);
  pixw = XTextWidth(fontp, diagmsg, strlen(diagmsg));
  pixh = fontp->ascent + fontp->descent;
  boxw = pixw + DiagXMergin * 2;
  boxh = pixh + DiagYMergin * 2;

  /* create status window */
  if(winstat != -1) disposediagbox();
  winstat = XCreateSimpleWindow(disp, DefaultRootWindow(disp),
                               (width-boxw)/2, (height-boxh)/2,
                               boxw, boxh,
                               2, BlackPixel(disp,0), WhitePixel(disp,0));

  /* make this window without time titlebar */
  att.override_redirect = True;
  XChangeWindowAttributes(disp, winstat, CWOverrideRedirect, &att);
  XMapWindow(disp, winstat);
  theGC.font = fontp->fid;
  gcstat = XCreateGC(disp, winstat, GCFont, &theGC);
  XDrawString(disp, winstat,
             gcstat,
             DiagXMergin, fontp->ascent+DiagYMergin,
             diagmsg, strlen(diagmsg));
}

void disposediagbox(void)
{
  if ( winstat != -1 ) {
    XDestroyWindow(disp, winstat);
    winstat = -1;
  }
}

void battery_proc(int left)
{
  int pos;
  if (BI_Horizontal) {
    pos = width * left / 100;
    XSetForeground(disp, gcbar, offin);
    XFillRectangle(disp, winbar, gcbar, 0, 0, pos, bi_thick);
    XSetForeground(disp, gcbar, offout);
    XFillRectangle(disp, winbar, gcbar, pos, 0, width, bi_thick);
  } else {
    pos = height * left / 100;
    XSetForeground(disp, gcbar, offin);
    XFillRectangle(disp, winbar, gcbar, 0, height-pos, bi_thick, height);
    XSetForeground(disp, gcbar, offout);
    XFillRectangle(disp, winbar, gcbar, 0, 0, bi_thick, height-pos);
  }
  XFlush(disp);
}

void plug_proc(int left)
{
  int pos;

  if (BI_Horizontal) {
    pos = width * left / 100;
    XSetForeground(disp, gcbar, onin);
    XFillRectangle(disp, winbar, gcbar, 0, 0, pos, bi_thick);
    XSetForeground(disp, gcbar, onout);
    XFillRectangle(disp, winbar, gcbar, pos+1, 0, width, bi_thick);
  } else {
    pos = height * left / 100;
    XSetForeground(disp, gcbar, onin);
    XFillRectangle(disp, winbar, gcbar, 0, height-pos, bi_thick, height);
    XSetForeground(disp, gcbar, onout);
    XFillRectangle(disp, winbar, gcbar, 0, 0, bi_thick, height-pos);
  }
  XFlush(disp);
}


/*
 * estimating time for battery remaining / charging
 */

#define CriticalLevel  5

void estimate_remain()
{
  static int battery_base = -1;
  int diff;
  int remain;

  /* static value initialize */
  if (battery_base == -1) {
    battery_base = battery_level;
    return;
  }

  diff = battery_base - battery_level;

  if (diff == 0) return;

  /* estimated time for battery remains */
  if (diff > 0) {
    remain = elapsed_time * (battery_level - CriticalLevel) / diff ;
    remain = remain * bi_interval;  /* in sec */
    if (remain < 0 ) remain = 0;
    printf("battery remain: %2d hr. %2d min. %2d sec.\n",
	   remain / 3600, (remain % 3600) / 60, remain % 60);
    elapsed_time = 0;
    battery_base = battery_level;
    return;
  }

  /* estimated time of battery charging */
  remain = elapsed_time * (battery_level - 100) / diff;
  remain = remain * bi_interval;  /* in sec */
  printf("charging remain: %2d hr. %2d min. %2d sec.\n",
	 remain / 3600, (remain % 3600) / 60, remain % 60);
  elapsed_time = 0;
  battery_base = battery_level;
}

#define TEMP_BUFFER_SIZE 4096

int read_pipe(int pipe, char *buffer)
{
	int rd = read(pipe, buffer, TEMP_BUFFER_SIZE - 1);
	if (rd == -1) {
		perror("read pipe");
		return 0;
	}
	buffer[rd] = 0;
	return rd;
}

#define BATTERY_STRING		"battery="
#define AC_LINE_STRING		"ac_line="
void print_script_error(void)
{
	fprintf(stderr, "\nExternal script must print two strings:\n"
		"\t" BATTERY_STRING "value between 0 and 100\n"
		"\t" AC_LINE_STRING "on|off\n"
		"example 1:\n"
		"\t" BATTERY_STRING "25\n"
		"\t" AC_LINE_STRING "on\n"
		"example 2:\n"
		"\t" BATTERY_STRING "75\n"
		"\t" AC_LINE_STRING "off\n"
	);

}

void battery_check(void)
{
	int p[2], pid;
	char buffer[TEMP_BUFFER_SIZE];

	if (pipe(p) != 0) {
		perror("error create pipe");
		goto exit_check;
	}

	pid = fork();

	if (pid == -1) {
		perror("fork error");
		close(p[0]);
		close(p[1]);
		goto exit_check;
	}

	if (pid) { /* parent */
		int len, status;
		close(p[1]);
		char *str, *end;

		pid = waitpid(pid, &status, 0);
		if (pid == -1) {
			perror("waitpid");
			close(p[0]);
			goto exit_check;
		}

		if (status != 0) {
			fprintf(stderr,
				"child process (%s) has returned "
				"non-zero code: %d\n",
				EXTERNAL_CHECK, WEXITSTATUS(status));
			len = read_pipe(p[0], buffer);
			if (len > 0)
				printf("%s\n", buffer);
			close(p[0]);
			goto exit_check;
		}

		len = read_pipe(p[0], buffer);
		close(p[0]);
		if (!len) {
			print_script_error();
			goto exit_check;
		}

		str = strstr(buffer, BATTERY_STRING);
		if (!str) {
			print_script_error();
			goto exit_check;
		}

		str += sizeof(BATTERY_STRING) - 1;
		if (!*str) {
			print_script_error();
			goto exit_check;
		}

		status = strtol(str, &end, 10);
		if ((*end != '\n' && *end != '.' &&
			*end != '\0' &&
			*end != ' ' && *end != '%') || end == str) {
			print_script_error();
			goto exit_check;
		}
		battery_level = status;
		if (battery_level > 100)
			fprintf(stderr, "Incorrect battery level "
			" has been received: %d%%\n", battery_level);

		if (strstr(buffer, AC_LINE_STRING "on"))
			ac_line = 1;
		else
			ac_line = 0;

/*                 printf("Received battery level: %d%%, ac_line: %s\n", */
/*                         battery_level, ac_line?"on":"off"); */

	} else { /* child */
		char *argv[] = { EXTERNAL_CHECK, NULL };
		close(p[0]);
		if (dup2(p[1], fileno(stdout)) == -1) {
			perror("dup2 error");
			_exit(errno);
		}
		execvp(EXTERNAL_CHECK, argv);
		fprintf(stderr, "Exec %s error: %s\n",
			EXTERNAL_CHECK, strerror(errno));
		_exit(-1);
	}

	exit_check:
	elapsed_time++;
	redraw();
	signal(SIGALRM, (void *)(battery_check));
	alarm(bi_interval);
}


