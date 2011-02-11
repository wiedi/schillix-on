/* @(#)inp.c	1.10 11/01/30 2011 J. Schilling */
#ifndef lint
static	char sccsid[] =
	"@(#)inp.c	1.10 11/01/30 2011 J. Schilling";
#endif
/*
 *	Copyright (c) 1986, 1988 Larry Wall
 *	Copyright (c) 2011 J. Schilling
 *
 *	This program may be copied as long as you don't try to make any
 *	money off of it, or pretend that you wrote it.
 */

#define	EXT	extern
#include "common.h"
#include "util.h"
#include "pch.h"
#undef	EXT
#define	EXT
#include "inp.h"

/* Input-file-with-indexable-lines abstract type */

static off_t i_size;			/* size of the input file */
static char *i_womp;			/* plan a buffer for entire file */
static char **i_ptr;			/* pointers to lines in i_womp */

static int tifd = -1;			/* plan b virtual string array */
static char *tibuf[2];			/* plan b buffers */
static LINENUM tiline[2] = {-1, -1};	/* 1st line in each buffer */
static LINENUM lines_per_buf;		/* how many lines per buffer */
static int tireclen;			/* length of records in tmp file */

static	void	plan_b __PR((char *filename));
static	bool	rev_in_string __PR((char *string));

/* New patch--prepare to edit another file. */

void
re_input()
{
	if (using_plan_a) {
		i_size = 0;
		if (i_ptr != Null(char **))
			free((char *)i_ptr);
		if (i_womp != Nullch)
			free(i_womp);
		i_womp = Nullch;
		i_ptr = Null(char **);
	} else {
		using_plan_a = TRUE;	/* maybe the next one is smaller */
		Close(tifd);
		tifd = -1;
		free(tibuf[0]);
		free(tibuf[1]);
		tibuf[0] = tibuf[1] = Nullch;
		tiline[0] = tiline[1] = -1;
		tireclen = 0;
	}
}

/* Constuct the line index, somehow or other. */

void
scan_input(filename)
	char *filename;
{
	if (!plan_a(filename))
		plan_b(filename);
	if (verbose) {
		say(_("Patching file %s using Plan %s...\n"), filename,
		    (using_plan_a ? "A" : "B"));
	}
}

/* Try keeping everything in memory. */

bool
plan_a(filename)
	char *filename;
{
	int ifd;
	char *s;
	LINENUM iline;

	if (ok_to_create_file && stat(filename, &file_stat) < 0) {
		if (verbose)
			say(_("(Creating file %s...)\n"), filename);
		makedirs(filename, TRUE);
		close(creat(filename, 0666));
	}
	if (stat(filename, &file_stat) < 0) {
		Sprintf(buf, "RCS/%s%s", filename, RCSSUFFIX);
		if (stat(buf, &file_stat) >= 0 ||
		    stat(buf+4, &file_stat) >= 0) {
			Sprintf(buf, CHECKOUT, filename);
			if (verbose)
				say(
_("Can't find %s--attempting to check it out from RCS.\n"),
				    filename);
			if (system(buf) || stat(filename, &file_stat))
				fatal(_("Can't check out %s.\n"), filename);
		} else {
			Sprintf(buf+20, "SCCS/%s%s", SCCSPREFIX, filename);
			if (stat(s = buf+20, &file_stat) >= 0 ||
			    stat(s = buf+25, &file_stat) >= 0) {
				Sprintf(buf, GET, s);
				if (verbose)
					say(
_("Can't find %s--attempting to get it from SCCS.\n"),
					    filename);
				if (system(buf) || stat(filename, &file_stat))
					fatal(_("Can't get %s.\n"), filename);
			} else {
				fatal(_("Can't find %s.\n"), filename);
			}
		}
	}
	filemode = file_stat.st_mode;
	if ((filemode & S_IFMT) & ~S_IFREG)
		fatal(_("%s is not a normal file--can't patch.\n"), filename);
	i_size = file_stat.st_size;
	if (out_of_mem) {
		set_hunkmax();		/* allocate dynamic arrays */
		out_of_mem = FALSE;
		return (FALSE);		/* force plan b because plan a bombed */
	}
	i_womp = malloc((MEM)(i_size+2)); /* lint says: may alloc less than */
					/* i_size, I thing that's okay */
	if (i_womp == Nullch)
		return (FALSE);
	if ((ifd = open(filename, 0)) < 0)
		fatal(_("Can't open file %s\n"), filename);
	if (read(ifd, i_womp, (int)i_size) != i_size) {
		Close(ifd);	/* probably means i_size > 15 / 16 bits worth */
		free(i_womp);	/* at this point it doesn't matter if i_womp */
		return (FALSE);	/* was undersized. */
	}
	Close(ifd);
	if (i_size && i_womp[i_size-1] != '\n')
		i_womp[i_size++] = '\n';
	i_womp[i_size] = '\0';

	/* count the lines in the buffer so we know how many pointers we need */

	iline = 0;
	for (s = i_womp; *s; s++) {
		if (*s == '\n')
			iline++;
	}
	i_ptr = (char **)malloc((MEM)((iline + 2) * sizeof (char *)));
	if (i_ptr == Null(char **)) {	/* shucks, it was a near thing */
		free((char *)i_womp);
		return (FALSE);
	}

	/* now scan the buffer and build pointer array */

	iline = 1;
	i_ptr[iline] = i_womp;
	for (s = i_womp; *s; s++) {
		if (*s == '\n')
			i_ptr[++iline] = s+1; /* NOT null terminated */
	}
	input_lines = iline - 1;

	/* now check for revision, if any */

	if (revision != Nullch) {
		if (!rev_in_string(i_womp)) {
			if (force) {
				if (verbose)
				    say(
_("Warning: this file doesn't appear to be the %s version--patching anyway.\n"),
					revision);
			} else {
				ask(
_("This file doesn't appear to be the %s version--patch anyway? [n] "),
				    revision);
				if (*buf != 'y')
					fatal(_("Aborted.\n"));
			}
		} else if (verbose) {
			say(
_("Good.  This file appears to be the %s version.\n"),
				revision);
		}
	}
	return (TRUE);			/* plan a will work */
}

/* Keep (virtually) nothing in memory. */

static void
plan_b(filename)
	char *filename;
{
	FILE *ifp;
	int i = 0;
	int maxlen = 1;
	bool found_revision = (revision == Nullch);

	using_plan_a = FALSE;
	if ((ifp = fopen(filename, "r")) == Nullfp)
		fatal(_("Can't open file %s\n"), filename);
	if ((tifd = creat(TMPINNAME, 0666)) < 0)
		fatal(_("Can't open file %s\n"), TMPINNAME);
	while (fgets(buf, sizeof (buf), ifp) != Nullch) {
		if (revision != Nullch && !found_revision && rev_in_string(buf))
			found_revision = TRUE;
		if ((i = strlen(buf)) > maxlen)
			maxlen = i;	/* find longest line */
	}
	if (revision != Nullch) {
		if (!found_revision) {
			if (force) {
				if (verbose)
					say(
_("Warning: this file doesn't appear to be the %s version--patching anyway.\n"),
						revision);
			} else {
				ask(
_("This file doesn't appear to be the %s version--patch anyway? [n] "),
				    revision);
				if (*buf != 'y')
					fatal(_("Aborted.\n"));
			}
		} else if (verbose) {
			say(
_("Good.  This file appears to be the %s version.\n"),
			revision);
		}
	}
	Fseek(ifp, (off_t)0, 0);	/* rewind file */
	lines_per_buf = BUFFERSIZE / maxlen;
	tireclen = maxlen;
	tibuf[0] = malloc((MEM)(BUFFERSIZE + 1));
	tibuf[1] = malloc((MEM)(BUFFERSIZE + 1));
	if (tibuf[1] == Nullch)
		fatal(_("Can't seem to get enough memory.\n"));

	for (i = 1; ; i++) {
		if (! (i % lines_per_buf)) /* new block */
			if (write(tifd, tibuf[0], BUFFERSIZE) < BUFFERSIZE)
				fatal(_("patch: can't write temp file.\n"));
		if (fgets(tibuf[0] + maxlen * (i%lines_per_buf),
		    maxlen + 1, ifp) == Nullch) {
			input_lines = i - 1;
			if (i % lines_per_buf) {
				if (write(tifd, tibuf[0], BUFFERSIZE) <
				    BUFFERSIZE) {
					fatal(
					_("patch: can't write temp file.\n"));
				}
			}
			break;
		}
	}
	Fclose(ifp);
	Close(tifd);
	if ((tifd = open(TMPINNAME, 0)) < 0) {
		fatal(_("Can't reopen file %s\n"), TMPINNAME);
	}
}

/* Fetch a line from the input file, \n terminated, not necessarily \0. */

char *
ifetch(line, whichbuf)
	LINENUM line;
	int whichbuf;			/* ignored when file in memory */
{
	if (line < 1 || line > input_lines)
		return ("");
	if (using_plan_a) {
		return (i_ptr[line]);
	} else {
		LINENUM offline = line % lines_per_buf;
		LINENUM baseline = line - offline;

		if (tiline[0] == baseline)
			whichbuf = 0;
		else if (tiline[1] == baseline)
			whichbuf = 1;
		else {
			tiline[whichbuf] = baseline;
			Lseek(tifd, baseline / lines_per_buf * BUFFERSIZE, 0);
			if (read(tifd, tibuf[whichbuf], BUFFERSIZE) < 0) {
				fatal(_("Error reading tmp file %s.\n"),
				    TMPINNAME);
			}
		}
		return (tibuf[whichbuf] + (tireclen*offline));
	}
}

/* True if the string argument contains the revision number we want. */

static bool
rev_in_string(string)
	char *string;
{
	char *s;
	int patlen;

	if (revision == Nullch)
		return (TRUE);
	patlen = strlen(revision);
	if (strnEQ(string, revision, patlen) && isspace(string[patlen]))
		return (TRUE);
	for (s = string; *s; s++) {
		if (isspace(*s) && strnEQ(s+1, revision, patlen) &&
		    isspace(s[patlen+1])) {
			return (TRUE);
		}
	}
	return (FALSE);
}
