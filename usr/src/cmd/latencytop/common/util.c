/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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
 * Copyright (c) 2008-2009, Intel Corporation.
 * All Rights Reserved.
 */

#include <unistd.h>
#include <libintl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <procfs.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "latencytop.h"

/* pipe that breaks the event loop (and exit early) */
static int signal_pipe[2];

/*
 * Get current system time in milliseconds (1e-3).
 */
uint64_t
lt_millisecond(void)
{
	struct timeval p;
	(void) gettimeofday(&p, NULL);
	return ((uint64_t)p.tv_sec * 1000 + p.tv_usec / 1000);
}

/*
 * Wrapper of gettext().
 */
const char *
lt_text(const char *text)
{
	if (text == NULL) {
		return ("");
	}

	return (gettext(text));
}

/*
 * Checks if OOM happens by comparing pointers with NULL in various places.
 */
void
lt_check_null(void *p)
{
	if (p == NULL) {
		(void) printf("Out of memory!\n");
		g_assert(0);
		exit(2);
	}
}

/*
 * Safe malloc.
 */
void *
lt_malloc(size_t size)
{
	void *ret = malloc(size);

	lt_check_null(ret);

	return (ret);
}

/*
 * Safe alloc with memory cleared.
 * Named it "zalloc" because its signature is different from
 * calloc() in stdlib.
 */
void *
lt_zalloc(size_t size)
{
	void *ret = lt_malloc(size);
	(void) memset(ret, 0, size);
	return (ret);
}

/*
 * Safe strdup.
 */
char *
lt_strdup(const char *str)
{
	char *ret = strdup(str);

	lt_check_null(ret);

	return (ret);
}

/*
 * Get string for current time, e.g. YYYY-MM-DD
 */
void
lt_time_str(char *buffer, int len)
{
	struct tm tms;
	time_t t;
	int i;

	(void) time(&t);
	(void) gmtime_r(&t, &tms);
	(void) asctime_r(&tms, buffer, len);

	for (i = strlen(buffer)-1; i > 0; --i) {
		if (isspace(buffer[i])) {
			buffer[i] = 0;
		} else	{
			break;
		}
	}
}

/*
 * Retrieves process exeutable name etc. from /proc.
 */
char *
lt_get_proc_field(pid_t pid, lt_field_t field)
{
	char name[PATH_MAX];
	int fd;
	int ret;
	psinfo_t psinfo;

	(void) snprintf(name, PATH_MAX, "/proc/%d/psinfo", (int)pid);
	fd = open(name, O_RDONLY);
	if (fd == -1) {
		return (NULL);
	}

	ret = read(fd, (char *)&psinfo, sizeof (psinfo_t));
	(void) close(fd);
	if (ret < 0) {
		return (NULL);
	}

	switch (field) {
	case LT_FIELD_FNAME:
		return (lt_strdup(psinfo.pr_fname));
	case LT_FIELD_PSARGS:
		return (lt_strdup(psinfo.pr_psargs));
	}
	return (NULL);
}

/*
 * Help function to update the data structure.
 */
void
lt_update_stat_value(lt_stat_data_t *entry,
    lt_stat_type_t type, uint64_t value)
{
	switch (type) {
	case LT_STAT_COUNT:
		entry->count += value;
		break;
	case LT_STAT_SUM:
		entry->total += value;
		break;
	case LT_STAT_MAX:
		if (value > entry->max) {
			entry->max = value;
		}
		break;
	default:
		break;
	}
}

/*
 * Help function to sort by total.
 */
int
lt_sort_by_total_desc(lt_stat_entry_t *a, lt_stat_entry_t *b)
{
	g_assert(a != NULL && b != NULL);
	/*
	 * ->data.total is int64, so we can't simply return
	 * b->data.total - a->data.total
	 */
	if (b->data.total > a->data.total) {
		return (1);
	} else if (b->data.total < a->data.total) {
		return (-1);
	} else	{
		return (0);
	}
}

/*
 * Help function to sort by max.
 */
int
lt_sort_by_max_desc(lt_stat_entry_t *a, lt_stat_entry_t *b)
{
	g_assert(a != NULL && b != NULL);

	if (b->data.max > a->data.max) {
		return (1);
	} else if (b->data.max < a->data.max) {
		return (-1);
	} else	{
		return (0);
	}
}

/*
 * Help function to sort by count.
 */
int
lt_sort_by_count_desc(lt_stat_entry_t *a, lt_stat_entry_t *b)
{
	g_assert(a != NULL && b != NULL);

	if (b->data.count > a->data.count) {
		return (1);
	} else if (b->data.count < a->data.count) {
		return (-1);
	} else	{
		return (0);
	}
}

/*
 * Help function to sort by average.
 */
int
lt_sort_by_avg_desc(lt_stat_entry_t *a, lt_stat_entry_t *b)
{
	double avg_a, avg_b;

	g_assert(a != NULL && b != NULL);

	avg_a = (double)a->data.total / a->data.count;
	avg_b = (double)b->data.total / b->data.count;

	if (avg_b > avg_a) {
		return (1);
	} else if (avg_b < avg_a) {
		return (-1);
	} else	{
		return (0);
	}
}

/*
 * Create pipe for signal handler and wakeup.
 */
void
lt_gpipe_init(void)
{
	(void) pipe(signal_pipe);
}

/*
 * Release pipe used in signal handler.
 */
void
lt_gpipe_deinit(void)
{
	(void) close(signal_pipe[0]);
	(void) close(signal_pipe[1]);
}

/*
 * Break from main loop early.
 */
void
lt_gpipe_break(const char *ch)
{
	(void) write(signal_pipe[1], ch, 1);
}

/*
 * Returns fd# used to detect "break main loop".
 */
int
lt_gpipe_readfd(void)
{
	return (signal_pipe[0]);
}

/*
 * Check if a file exists.
 */
int
lt_file_exist(const char *name)
{
	struct stat64 st;

	if (stat64(name, &st) == 0) {
		return (1);
	} else	{
		return (0);
	}
}
