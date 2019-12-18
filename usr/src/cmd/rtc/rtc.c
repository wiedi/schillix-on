/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.opensource.org/licenses/cddl1.txt
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright 2016 J. Schilling
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/sysi86.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <sys/stat.h>

static char *progname;
static char *zonefile = "/etc/rtc_config";
static FILE *zonefptr;
static char zone_info[256];
static char zone_lag[256];
static char tz[256] = "TZ=";
int debug = 0;
int nowrite = 0;
int lag;
int errors_ok = 0; /* allow "rtc no-args" to be quiet when not configured */
static time_t clock_val;
static char zone_comment[] =
	"#\n"
	"#	This file (%s) contains information used to manage the\n"
	"#	x86 real time clock hardware.  The hardware is kept in\n"
	"#	the machine's local time for compatibility with other x86\n"
	"#	operating systems.  This file is read by the kernel at\n"
	"#	boot time.  It is set and updated by the /usr/sbin/rtc\n"
	"#	command.  The 'zone_info' field designates the local\n"
	"#	time zone.  The 'zone_lag' field indicates the number\n"
	"#	of seconds between local time and Greenwich Mean Time.\n"
	"#\n";

/*
 *	Open the configuration file and extract the
 *	zone_info and the zone_lag.  Return 0 if successful.
 */
int
open_zonefile()
{
	char b[256], *s;
	int lag_hrs;

	if ((zonefptr = fopen(zonefile, "r")) == NULL) {
		if (errors_ok == 0)
			(void) fprintf(stderr,
				"%s: cannot open %s: errno = %d\n",
				progname, zonefile, errno);
		return (1);
	}

	for (;;) {
		if ((s = fgets(b, sizeof (b), zonefptr)) == NULL)
			break;
		if ((s = strchr(s, 'z')) == NULL)
			continue;
		if (strncmp(s, "zone_info", 9) == 0) {
			s += 9;
			while (*s != 0 && *s != '=')
				s++;
			if (*s == '=') {
				s++;
				while (*s != 0 && (*s == ' ' || *s == '\t'))
					s++;
				(void) strncpy(zone_info, s,
				    sizeof (zone_info));
				s = zone_info;
				while (*s != 0 && *s != '\n')
					s++;
				if (*s == '\n')
					*s = 0;
			}
		} else if (strncmp(s, "zone_lag", 8) == 0) {
			s += 8;
			while (*s != 0 && *s != '=')
				s++;
			if (*s == '=') {
				s++;
				while (*s != 0 && (*s == ' ' || *s == '\t'))
					s++;
				(void) strncpy(zone_lag, s, sizeof (zone_lag));
				s = zone_lag;
				while (*s != 0 && *s != '\n')
					s++;
				if (*s == '\n')
					*s = 0;
			}
		}
	}
	lag = atoi(zone_lag);
	lag_hrs = lag / 3600;
	if (zone_info[0] == 0) {
		(void) fprintf(stderr, "%s: zone_info field is invalid\n",
		    progname);
		zone_info[0] = 0;
		zone_lag[0] = 0;
		return (1);
	}
	if (zone_lag[0] == 0) {
		(void) fprintf(stderr, "%s: zone_lag field is invalid\n",
		    progname);
		zone_lag[0] = 0;
		return (1);
	}
	if ((lag_hrs < -24) || (lag_hrs > 24)) {
		(void) fprintf(stderr, "%s: a GMT lag of %d is out of range\n",
		    progname, lag_hrs);
		zone_info[0] = 0;
		zone_lag[0] = 0;
		return (1);
	}
	if (debug)
		(void) fprintf(stderr, "zone_info = %s,   zone_lag = %s\n",
		    zone_info, zone_lag);
	if (debug)
		(void) fprintf(stderr, "lag (decimal) is %d\n", lag);

	(void) fclose(zonefptr);
	zonefptr = NULL;
	return (0);
}

void
display_zone_string(void)
{
	if (open_zonefile() == 0)
		(void) printf("%s\n", zone_info);
	else
		(void) printf("GMT\n");
}

long
set_zone(char *zone_string)
{
	struct tm *tm;
	long current_lag;

	(void) umask(0022);
	if (!nowrite) {
		if ((zonefptr = fopen(zonefile, "w")) == NULL) {
			(void) fprintf(stderr,
				    "%s: cannot open %s: errno = %d\n",
				progname, zonefile, errno);
			return (0);
		}
	}

	tz[3] = 0;
	(void) strncat(tz, zone_string, 253);
	if (debug)
		(void) fprintf(stderr, "Time Zone string is '%s'\n", tz);

	(void) putenv(tz);
	if (debug)
		(void) system("env | grep TZ");

	(void) time(&clock_val);

	tm = localtime(&clock_val);
	current_lag = tm->tm_isdst ? altzone : timezone;
	if (debug)
		(void) printf("%s DST.    Lag is %ld.\n", tm->tm_isdst ? "Is" :
		    "Is NOT",  tm->tm_isdst ? altzone : timezone);

	if (nowrite)
		zonefptr = stdout;
	(void) fprintf(zonefptr, zone_comment, zonefile);
	(void) fprintf(zonefptr, "zone_info=%s\n", zone_string);
	(void) fprintf(zonefptr, "zone_lag=%ld\n",
	    tm->tm_isdst ? altzone : timezone);
	if (!nowrite)
		(void) fclose(zonefptr);
	zonefptr = NULL;
	return (current_lag);
}

void
correct_time_and_lag(long current_lag)
{
	if (nowrite) {
		fprintf(stderr, "No-write mode: Set of RTC prevented.\n");
		return;
	}

	/* correct the lag */
	(void) sysi86(SGMTL, current_lag);

	/*
	 * set the unix time from the rtc,
	 * assuming the rtc was the correct
	 * local time.
	 */
	(void) sysi86(RTCSYNC);
}

void
correct_rtc_and_lag()
{
	struct tm *tm;
	long kernels_lag;
	long current_lag;

	if (open_zonefile())
		return;

	tz[3] = 0;
	(void) strncat(tz, zone_info, 253);
	if (debug)
		(void) fprintf(stderr, "Time Zone string is '%s'\n", tz);

	(void) putenv(tz);
	if (debug)
		(void) system("env | grep TZ");

	(void) time(&clock_val);
	tm = localtime(&clock_val);
	current_lag = tm->tm_isdst ? altzone : timezone;
	(void) sysi86(GGMTL, &kernels_lag);
	if (debug)
		(void) fprintf(stderr,
				"current lag %d, %s lag %d, kernel lag %d\n",
				current_lag, zonefile, lag, kernels_lag);

	if (lag != kernels_lag) {
		/*
		 * The kernel did read a different /etc/rtc_config file
		 * than the one we see. This happens when the boot achive
		 * was not updated after a DST change and the kernel crashed
		 * and rebooted with the old boot archive.
		 *
		 * We fix this situation by setting the lag we read from the
		 * /etc/rtc_config file in the kernel and then tell the kernel
		 * to sync the UNIX time from the RTC.
		 */
		if (debug)
			(void) fprintf(stderr,
				"kernel lag and %s lag disagree\n",
				zonefile);
		if (lag == current_lag) {
			if (debug)
				(void) fprintf(stderr,
					"correcting unix time\n");
			correct_time_and_lag(lag);
			return;
		}
	}

	if (current_lag != lag) {	/* if file is wrong */
		if (debug)
			(void) fprintf(stderr, "correcting file\n");
		(void) set_zone(zone_info);	/* then rewrite file */
	}

	if (current_lag != kernels_lag) {
		if (nowrite) {
			fprintf(stderr,
				"No-write mode: Set of RTC prevented.\n");
			return;
		}
		if (debug)
			(void) fprintf(stderr, "correcting kernel's lag\n");
		(void) sysi86(SGMTL, current_lag);	/* correct the lag */
		(void) sysi86(WTODC);			/* set the rtc to */
							/* new local time */
	}
}

void
initialize_zone(char *zone_string)
{
	long current_lag;

	/* write the config file */
	current_lag = set_zone(zone_string);

	correct_time_and_lag(current_lag);
}

void
usage()
{
	static char Usage[] = "Usage:\n\
rtc [-c] [-d] [-n] [-z time_zone] [-?]\n";

	(void) fprintf(stderr, Usage);
}

void
verbose_usage()
{
	static char Usage1[] = "\
	Options:\n\
	    -c\t\tCheck and correct for daylight savings time rollover.\n\
	    -d\t\tEnable debugging.\n\
	    -n\t\tNo write mode, keep everything as is.\n\
	    -z [zone]\tRecord the zone info in the config file.\n";

	(void) fprintf(stderr, Usage1);
}

int
main(int argc, char *argv[])
{
	int	c;
	int	correct = 0;
	char	*zstring = NULL;

	progname = argv[0];

	if (argc == 1) {
		errors_ok = 1;
		display_zone_string();
	}

	while ((c = getopt(argc, argv, "cz:dn")) != EOF) {
		switch (c) {
		case 'c':
			correct = 1;
			continue;
		case 'z':
			zstring = optarg;
			continue;
		case 'd':
			debug = 1;
			continue;
		case 'n':
			nowrite = 1;
			continue;
		case '?':
			verbose_usage();
			return (0);
		default:
			usage();
			return (1);
		}
	}
	/*
	 * First set the new zone if requested.
	 */
	if (zstring)
		initialize_zone(zstring);
	/*
	 * Now correct the time if requested;
	 */
	if (correct)
		correct_rtc_and_lag();
	return (0);
}
