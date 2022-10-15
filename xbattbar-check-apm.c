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
 *   This part contains APM battery-check part of xbattbar
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>

static int ac_line = -1;               /* AC line status */
static int battery_level = -1;         /* battery level */

void battery_check(void);

int main(int argc, char **argv)
{
	battery_check();
	if (battery_level == -1) {
		fprintf(stderr, "Can not get battery level\n");
		return 1;
	}

	printf("battery=%d\nac_line=%s\n", battery_level, ac_line?"on":"off");
	return 0;
}


#ifdef __bsdi__

#include <machine/apm.h>
#include <machine/apmioctl.h>

int first = 1;
void battery_check(void)
{
  int fd;
  struct apmreq ar ;

  ar.func = APM_GET_POWER_STATUS ;
  ar.dev = APM_DEV_ALL ;

  if ((fd = open(_PATH_DEVAPM, O_RDONLY)) < 0) {
    perror(_PATH_DEVAPM) ;
    exit(1) ;
  }
  if (ioctl(fd, PIOCAPMREQ, &ar) < 0) {
    fprintf(stderr, "xbattbar: PIOCAPMREQ: APM_GET_POWER_STATUS error 0x%x\n", ar.err);
  }
  close (fd);

  if (first || ac_line != ((ar.bret >> 8) & 0xff)
      || battery_level != (ar.cret&0xff)) {
    first = 0;
    ac_line = (ar.bret >> 8) & 0xff;
    battery_level = ar.cret&0xff;
  }
}

#endif /* __bsdi__ */

#if defined __FreeBSD__ || defined __FreeBSD_kernel__

#include <sys/fcntl.h>
#include <machine/apm_bios.h>

#define APMDEV21       "/dev/apm0"
#define APMDEV22       "/dev/apm"

#define        APM_STAT_UNKNOWN        255

#define        APM_STAT_LINE_OFF       0
#define        APM_STAT_LINE_ON        1

#define        APM_STAT_BATT_HIGH      0
#define        APM_STAT_BATT_LOW       1
#define        APM_STAT_BATT_CRITICAL  2
#define        APM_STAT_BATT_CHARGING  3

int first = 1;
void battery_check(void)
{
  int fd, r, p;
  struct apm_info     info;

  if ((fd = open(APMDEV21, O_RDWR)) == -1 &&
      (fd = open(APMDEV22, O_RDWR)) == -1) {
    fprintf(stderr, "xbattbar: cannot open apm device\n");
    exit(1);
  }
  if (ioctl(fd, APMIO_GETINFO, &info) == -1) {
    fprintf(stderr, "xbattbar: ioctl APMIO_GETINFO failed\n");
    exit(1);
  }
  close (fd);

  /* get current status */
  if (info.ai_batt_life == APM_STAT_UNKNOWN) {
    switch (info.ai_batt_stat) {
    case APM_STAT_BATT_HIGH:
      r = 100;
      break;
    case APM_STAT_BATT_LOW:
      r = 40;
      break;
    case APM_STAT_BATT_CRITICAL:
      r = 10;
      break;
    default:        /* expected to be APM_STAT_UNKNOWN */
      r = 100;
    }
  } else if (info.ai_batt_life > 100) {
    /* some APM BIOSes return values slightly > 100 */
    r = 100;
  } else {
    r = info.ai_batt_life;
  }

  /* get AC-line status */
  if (info.ai_acline == APM_STAT_LINE_ON) {
    p = APM_STAT_LINE_ON;
  } else {
    p = APM_STAT_LINE_OFF;
  }

  if (first || ac_line != p || battery_level != r) {
    first = 0;
    ac_line = p;
    battery_level = r;
  }
}

#endif /* __FreeBSD__ */

#ifdef __NetBSD__

#include <machine/apmvar.h>

#define _PATH_APM_SOCKET       "/var/run/apmdev"
#define _PATH_APM_CTLDEV       "/dev/apmctl"
#define _PATH_APM_NORMAL       "/dev/apm"

int first = 1;
void battery_check(void)
{
       int fd, r, p;
       struct apm_power_info info;

       if ((fd = open(_PATH_APM_NORMAL, O_RDONLY)) == -1) {
               fprintf(stderr, "xbattbar: cannot open apm device\n");
               exit(1);
       }

       if (ioctl(fd, APM_IOC_GETPOWER, &info) != 0) {
               fprintf(stderr, "xbattbar: ioctl APM_IOC_GETPOWER failed\n");
               exit(1);
       }

       close(fd);

       /* get current remoain */
       if (info.battery_life > 100) {
               /* some APM BIOSes return values slightly > 100 */
               r = 100;
       } else {
               r = info.battery_life;
       }

       /* get AC-line status */
       if (info.ac_state == APM_AC_ON) {
               p = APM_AC_ON;
       } else {
               p = APM_AC_OFF;
       }

       if (first || ac_line != p || battery_level != r) {
               first = 0;
               ac_line = p;
               battery_level = r;
       }
}

#endif /* __NetBSD__ */


#ifdef __linux__

#include <errno.h>
#include <linux/apm_bios.h>

#define APM_PROC	"/proc/apm"

#define        APM_STAT_LINE_OFF       0
#define        APM_STAT_LINE_ON        1

typedef struct apm_info {
   const char driver_version[10];
   int        apm_version_major;
   int        apm_version_minor;
   int        apm_flags;
   int        ac_line_status;
   int        battery_status;
   int        battery_flags;
   int        battery_percentage;
   int        battery_time;
   int        using_minutes;
} apm_info;


int first = 1;
void battery_check(void)
{
  int r,p;
  FILE *pt;
  struct apm_info i;
  char buf[100];

  /* get current status */
  errno = 0;
  if ( (pt = fopen( APM_PROC, "r" )) == NULL) {
    fprintf(stderr, "xbattbar: Can't read proc info: %s\n", strerror(errno));
    _exit(1);
  }

  if (fgets(buf, sizeof(buf) - 1, pt) == NULL) {
    fprintf(stderr, "xbattbar: Can't read proc info: %s\n", strerror(errno));
    _exit(1);
  }
  buf[ sizeof( buf ) - 1 ] = '\0';
  sscanf( buf, "%s %d.%d %x %x %x %x %d%% %d %d\n",
	  &i.driver_version[0],
	  &i.apm_version_major,
	  &i.apm_version_minor,
	  &i.apm_flags,
	  &i.ac_line_status,
	  &i.battery_status,
	  &i.battery_flags,
	  &i.battery_percentage,
	  &i.battery_time,
	  &i.using_minutes );

  fclose (pt);

   /* some APM BIOSes return values slightly > 100 */
   if ( (r = i.battery_percentage) > 100 ){
     r = 100;
   }

   /* get AC-line status */
   if ( i.ac_line_status == APM_STAT_LINE_ON) {
     p = APM_STAT_LINE_ON;
   } else {
     p = APM_STAT_LINE_OFF;
   }

  if (first || ac_line != p || battery_level != r) {
    first = 0;
    ac_line = p;
    battery_level = r;
  }
}

#endif /* linux */

