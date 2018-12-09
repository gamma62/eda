/*
* cmdlib.c
* advanced editing commands and later extensions, various parsers, directory list, macros and tools, VCS diff support
*
* Copyright 2003-2016 Attila Gy. Molnar
*
* This file is part of eda project.
*
* Eda is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* Eda is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with Eda.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>		/* lstat */
#include <time.h>		/* localtime strftime */
#include <unistd.h>		/* write fcntl readlink */
#include <fcntl.h>
#include <dirent.h>		/* opendir readdir */
#include <glob.h>		/* glob globfree */
#include <errno.h>
#include <syslog.h>
#include "main.h"
#include "proto.h"

/* global config */
extern CONFIG cnf;
extern const char long_version_string[];

#define ONE_SIZE  1024

typedef struct lsdir_item_tag {
	int _type;		/* lstat */
	char entry[FNAMESIZE];	/* original d_name or gl_pathv */
	unsigned _size;		/* for sorting */
	time_t _mtime;		/* for sorting */
	char one_line[ONE_SIZE];	/* the outcome */
} LSDIRITEM;

/* local proto */
static int one_lsdir_line (const char *fullpath, unsigned offset, LSDIRITEM **items, unsigned *item_count);
static int lsdir_item_cmp_entry (const void *p1, const void *p2);
static int lsdir_item_cmp_mtime (const void *p1, const void *p2);
static int lsdir_item_cmp_size (const void *p1, const void *p2);
static int set_diff_section (int *diff_type, int *ring_tg);
static int select_diff_section (int *diff_type, int *ring_tg);
static int unhide_udiff_range (int ri, const char *range, int *target_line, int *target_length);
static void mark_diff_line (int ri, int lineno);

/*
** nop - no operation, does nothing, only for testing
*/
int
nop (void)
{
	cnf.gstat |= GSTAT_UPDNONE;
	return (0);
}

/*
** version - show the version string
*/
int
version (void)
{
	tracemsg ("%s", long_version_string);
	tracemsg ("%s", curses_version());

	return (0);
}

/*
** pwd - print working directory name
*/
int
pwd (void)
{
	tracemsg ("_pwd     %s", cnf._pwd);
	tracemsg ("_altpwd  %s", cnf._altpwd);

	return (0);
}

/*
** uptime - show the output of the uptime command
*/
int
uptime (void)
{
	char cmd[100], buffer[100], buffer2[100];
	int len;

	read_extcmd_line ("uptime", 1, buffer, sizeof(buffer));
	tracemsg ("%s", buffer);

	snprintf(cmd, sizeof(cmd), "ps -o start -p %d", cnf.pid);
	read_extcmd_line (cmd, 2, buffer, sizeof(buffer));
	len = strlen(buffer);
	strip_blanks (STRIP_BLANKS_FROM_END|STRIP_BLANKS_FROM_BEGIN, buffer, &len);

	snprintf(cmd, sizeof(cmd), "ps -o etime -p %d", cnf.pid);
	read_extcmd_line (cmd, 2, buffer2, sizeof(buffer2));
	len = strlen(buffer2);
	strip_blanks (STRIP_BLANKS_FROM_END|STRIP_BLANKS_FROM_BEGIN, buffer2, &len);

	tracemsg ("pid %d, started %s, elapsed time %s", cnf.pid, buffer, buffer2);

	return (0);
}

/*
** delete_lines - delete all or selected lines,
**	parameter must be "all" or "selection"
*/
int
delete_lines (const char *args)
{
	unsigned len;
	int ret=1, changes=0;
	LINE *lp;
	int lineno, cnt;

	/* return if file do not accept line deletion */
	if (CURR_FILE.fflag & FSTAT_NODELIN)
		return (1);

	len = strlen(args);
	if (len > 0) {
		if (strncmp(args, "all", len)==0) {
			/* remove all visible text lines */
			lp = CURR_FILE.top;
			lineno = 0;
			next_lp (cnf.ring_curr, &lp, &cnt);
			lineno += cnt;
			while (TEXT_LINE(lp)) {
				changes++;
				clr_opt_bookmark(lp);
				lp = lll_rm(lp);		/* delete_lines() */
				CURR_FILE.num_lines--;
				if (HIDDEN_LINE(cnf.ring_curr,lp)) {
					next_lp (cnf.ring_curr, &lp, &cnt);
					lineno += cnt;
				}
			}
			if (changes > 0) {
				/* update! */
				CURR_LINE = CURR_FILE.top;
				CURR_FILE.fflag |= FSTAT_CHANGE;
				CURR_FILE.lineno = 0;
				go_home();
				update_focus(CENTER_FOCUSLINE, cnf.ring_curr);
			}
			ret=0;

		} else if (strncmp(args, "selection", len)==0) {
			if (cnf.select_ri == cnf.ring_curr) {
				ret = rm_select();
				/* errors tracemsg'd there */
			} else {
				ret = 1;
			}
		}
	}

	return (ret);
}

/*
** strip_lines - strip trailing whitechars (space, tab, CR) from the lines,
**	according to the parameter, either "all" or "selection"
*/
int
strip_lines (const char *args)
{
	unsigned len;
	int ret=1, changes=0, i;
	LINE *lp;
	int condition=0;

	/* return if don't accepted */
	if (CURR_FILE.fflag & FSTAT_NOEDIT)
		return (1);

	len = strlen(args);
	if (len > 0) {
		if (strncmp(args, "all", len)==0) {
			condition = 'a';
		} else if (strncmp(args, "selection", len)==0) {
			if (cnf.select_ri == cnf.ring_curr) {
				condition = 's';
			}
		}
		if (condition) {
			lp = CURR_FILE.top;
			next_lp (cnf.ring_curr, &lp, NULL);
			/* lines */
			while (TEXT_LINE(lp)) {
				if ((condition=='a') || (lp->lflag & LSTAT_SELECT)) {
					/* strip */
					i = lp->llen-1;
					while (i > 0 &&
					(lp->buff[i-1]==' ' || lp->buff[i-1]=='\t' || lp->buff[i-1]=='\r'))
					{
						i--;
					}
					if (i < lp->llen-1) {
						/* realloc, downsiz */
						(void) milbuff (lp, i, lp->llen-1-i, "", 0);
						changes++;
						lp->lflag |= LSTAT_CHANGE;
					}
				}
				/* next */
				next_lp (cnf.ring_curr, &lp, NULL);
			}
			if (CURR_FILE.lncol > CURR_LINE->llen-1)
				go_end();
			if (changes > 0)
				CURR_FILE.fflag |= FSTAT_CHANGE;
			ret=0;
		}
	}

	return (ret);
}

/*
* ringlist_parser - parse ring index or bookmark from the dataline
*/
int
ringlist_parser (const char *dataline)
{
	int len, rx, bmx, ret=1;
	char xmatch[TAGSTR_SIZE];

	len = strlen(dataline);
	if (len == 0) {
		return (1);
	}

	rx = bmx = -1;
	if (regexp_match(dataline, "^([0-9]+)", 1, xmatch) == 0) {
		rx = atoi(xmatch);
	} else if (regexp_match(dataline, "^\tbookmark ([0-9]+):", 1, xmatch) == 0) {
		bmx = atoi(xmatch);
	}

	if (rx != -1) {
		if (rx >= 0 && rx < RINGSIZE && (cnf.fdata[rx].fflag & FSTAT_OPEN)) {
			PD_LOG(LOG_DEBUG, "switch to ri=%d", rx);
			/* do not set lineno here */
			cnf.ring_curr = rx;		/* jump */
			ret = 0;
		} else {
			PD_LOG(LOG_DEBUG, "ri=%d is not open", rx);
		}
	} else if (bmx != -1) {
		PD_LOG(LOG_DEBUG, "try bookmark %d", bmx);
		ret = jump2_bookmark(bmx);
	}

	return (ret);
}

/*
* dirlist_parser - get dirname/filename from dataline,
* return parsed file/dir name or NULL
*
*/
char *
dirlist_parser (const char *dataline)
{
	int dlen=0;
	char *word = NULL;
	char *fn = NULL;
	char *dn = NULL;
	int dl=0, wl=0, res=0;
	LINE *lx=NULL;

	if (dataline == NULL) {
		return NULL;
	}

/* yet another magic number - the begin of "filename item" in the directory listing */
#define MAGIC55   55
/* yet another magic number - the begin of "base directory" in the header */
#define MAGIC12   12

	dlen = strlen(dataline);
	if (dlen <= MAGIC55) {
		PD_LOG(LOG_ERR, "dataline %d is short, not usable!", dlen);
		return (NULL);
	}
	if (strncmp(dataline, "$ lsdir", 7) == 0) {
		/* header */
		PD_LOG(LOG_DEBUG, "dataline is the header, nothing todo");
		return (NULL);
	}

	res = get_rest_of_line(&word, &wl, dataline, MAGIC55, dlen);
	if (res || word == NULL) {
		PD_LOG(LOG_ERR, "cannot get rest of line");
		FREE(word); word=NULL;
		return (NULL);
	}

	if (word[0] == '/') {
		/* already absolute path */
		fn = canonicalpath(word); /* maybe NULL, the return value */
		PD_LOG(LOG_DEBUG, "abs.path [%s]", fn);
		FREE(word); word = NULL;
	} else {
		/* relative path */
		lx = CURR_FILE.top->next;
		res = get_rest_of_line(&dn, &dl, lx->buff, MAGIC12, lx->llen);
		if (res || dn == NULL) {
			PD_LOG(LOG_ERR, "cannot get rest of header line");
			FREE(word); word = NULL;
			return (NULL);
		}
		/* get dirname from first (header) line */
		if (dl > 1 && dn[dl-1] == '/') {
			res = csere (&word, &wl, 0, 0, dn, dl);
		} else {
			/* append '/' */
			res = csere (&word, &wl, 0, 0, dn, dl) || csere (&word, &wl, wl, 0, "/", 1);
		}
		FREE(dn); dn = NULL;
		if (res) {
			PD_LOG(LOG_ERR, "cannot get dirname from header");
			FREE(word); word = NULL;
			return (NULL);
		}
		/* compiled absolute path */
		fn = canonicalpath(word); /* maybe NULL, the return value */
		/* orig. word was rel.path */
		PD_LOG(LOG_DEBUG, "rel.path [%s] -> abs.path [%s]", word, fn);
		FREE(word); word = NULL;
	}

	return (fn);
}

/*
*/
static int
one_lsdir_line (const char *fullpath, unsigned offset, LSDIRITEM **items, unsigned *item_count)
{
	LSDIRITEM *last;
	struct stat test;
	time_t t;
	struct tm *tm_p;
	char tbuff[20];
	char addition[FNAMESIZE], linkname[FNAMESIZE];

/* yet another magic format string - the directory listing */
#define MAGICFORMAT  "%c%04o %4d %6d %6d %12lld %s %s%s\n"

	++*item_count;
	last = (LSDIRITEM *) REALLOC((void *)*items, sizeof(LSDIRITEM) * (*item_count));
	if (last == NULL) {
		ERRLOG(0xE006);
		*item_count = 0;
		PD_LOG(LOG_ERR, "malloc (%s) - lost LSDIRITEM array", strerror(errno));
		return 1;
	}
	*items = last;
	last = &last[*item_count -1];

	strncpy(last->entry, fullpath+offset, FNAMESIZE);
	last->entry[FNAMESIZE-1] = '\0';
	if (lstat(fullpath, &test)) {
		PD_LOG(LOG_ERR, "lstat(%s) failed (%s)", fullpath, strerror(errno));
		snprintf(last->one_line, ONE_SIZE, "%s (failed)\n", last->entry);
		return 1;
	}
	last->_mtime = test.st_mtime;
	last->_size = test.st_size;

	t = test.st_mtime;
	tm_p = localtime(&t);
	strftime(tbuff, 19, "%Y-%m-%d %H:%M", tm_p);
	tbuff[19] = '\0';

	addition[0] = '\0';
	if (S_ISLNK(test.st_mode)) {
		last->_type = 'l';
		strncat(addition, " -> ", 10);
		memset(linkname, '\0', sizeof(linkname));
		if (readlink(fullpath, linkname, sizeof(linkname)-1) > 1)
			strncat(addition, linkname, sizeof(addition)-10);
		else
			strncat(addition, "(failed)", sizeof(addition)-10);
		/* this can be a dead link, but no room here to follow the chain */
	}
	else if (S_ISREG(test.st_mode))
		last->_type = '-';
	else if (S_ISDIR(test.st_mode))
		last->_type = 'd';
	else
		last->_type = '?';

	snprintf(last->one_line, ONE_SIZE, MAGICFORMAT,
		last->_type,
		test.st_mode & 07777,
		(int)test.st_nlink,
		test.st_uid,
		test.st_gid,
		(long long)test.st_size,
		tbuff,
		last->entry, addition);

	return 0;
}

/* sort by name, lexicographical, directories first
*/
static int
lsdir_item_cmp_entry (const void *p1, const void *p2)
{
	const LSDIRITEM *item1=p1;
	const LSDIRITEM *item2=p2;
	if (item1->_type == item2->_type) {
		/* sort by name between same types */
		return (strncmp(item1->entry, item2->entry, FNAMESIZE));
	} else if (item1->_type == 'd') {
		return -1;
	} else if (item2->_type == 'd') {
		return 1;
	} else {
		/* all non-dirs come later, sorted by name */
		return (strncmp(item1->entry, item2->entry, FNAMESIZE));
	}
}

/* sort by mtime, oldest first, newest last, regardless of type
*/
static int
lsdir_item_cmp_mtime (const void *p1, const void *p2)
{
	const LSDIRITEM *item1=p1;
	const LSDIRITEM *item2=p2;
	if (item1->_mtime < item2->_mtime)
		return -1;
	if (item1->_mtime > item2->_mtime)
		return 1;
	return 0;
}

/* sort by size, smallest first, largest last - directories before other items
*/
static int
lsdir_item_cmp_size (const void *p1, const void *p2)
{
	const LSDIRITEM *item1=p1;
	const LSDIRITEM *item2=p2;
	if (item1->_type == item2->_type) {
		if (item1->_size < item2->_size)
			return -1;
		if (item1->_size > item2->_size)
			return 1;
		return 0;
	} else if (item1->_type == 'd') {
		return -1;
	} else if (item2->_type == 'd') {
		return 1;
	} else {
		if (item1->_size < item2->_size)
			return -1;
		if (item1->_size > item2->_size)
			return 1;
		return 0;
	}

}

/* lsdir is the worker behind lsdir_cmd
*/
int
lsdir (const char *dirpath)
{
	DIR *dir = NULL;
	struct dirent *de = NULL;
	char fullpath[FNAMESIZE*2];
	unsigned len, length, i, item_count=0;
	char one_line[ONE_SIZE];
	LSDIRITEM *items=NULL;
	glob_t globbuf;
	LINE *lp=NULL, *lx=NULL;
	struct stat test;
	int use_readdir=1, ret=0;
	char dotdot[3] = "..\0";

	clean_buffer();
	/* additional flags */
	CURR_FILE.fflag |= FSTAT_SPECW;
	/* disable inline editing, adding lines */
	CURR_FILE.fflag |= (FSTAT_NOEDIT | FSTAT_NOADDLIN);

	use_readdir = (stat(dirpath, &test)==0 && (S_ISDIR(test.st_mode)));

	/* keep length and fullpath for stat() calls
	*/
	memset(fullpath, 0, sizeof(fullpath));
	if (use_readdir) {
		strncpy(fullpath, dirpath, FNAMESIZE-2);
		fullpath[FNAMESIZE-2] = '\0';
		length = strlen(fullpath);
		/* make sure terminal '/' is there */
		if (fullpath[length-1] != '/') {
			fullpath[length++] = '/';
			fullpath[length] = '\0';
		}
	} else {
		strncpy(fullpath, cnf._pwd, FNAMESIZE-2);
		fullpath[FNAMESIZE-2] = '\0';
		strncat(fullpath, "/", 1);
		length = cnf.l1_pwd+1;
	}
	if (length >= FNAMESIZE-1)
		return 1;

	/* header
	*/
	if (cnf.lsdir_opts & LSDIR_L) {
		snprintf(one_line, 30, "$ lsdir -l%s ", (cnf.lsdir_opts & LSDIR_A) ? "a" : "");
	} else {
		snprintf(one_line, 20, "$ lsdir ");
	}
	len = strlen(one_line);
	snprintf(one_line+len, (size_t)(ONE_SIZE-len-1), "%s\n", fullpath);
	if ((lp = insert_line_before (CURR_FILE.bottom, one_line)) != NULL) {
		CURR_FILE.num_lines++;
	} else {
		return 1;
	}

	items = NULL;
	item_count = 0;
	if (use_readdir) {
		if ((dir = opendir (dirpath)) == NULL) {
			tracemsg("opendir %s failed (%s)", dirpath, strerror(errno));
			PD_LOG(LOG_ERR, "opendir %s failed (%s)", dirpath, strerror(errno));
			return 3;
		}

		while ((de = readdir (dir)) != NULL) {
			if (de->d_name[0] == '.' && de->d_name[1] == '\0')
				continue;
			if (de->d_name[0] == '.' && (cnf.lsdir_opts & LSDIR_A) == 0) {
				if (!(de->d_name[1] == '.' && de->d_name[2] == '\0'))
					continue;
			}

			strncpy(fullpath+length, de->d_name, FNAMESIZE);
			fullpath[2*FNAMESIZE-1] = '\0';

			ret += one_lsdir_line (fullpath, length, &items, &item_count);
		}
		closedir (dir);
	} else {
		if (dirpath[0] == '/') {
			fullpath[0] = '\0';
			length = 0;
		}

		globbuf.gl_offs = 1;
		/* without GLOB_MARK -- one_lsdir_line() will append it anyway */
		if (glob(dirpath, GLOB_DOOFFS | GLOB_ERR, NULL, &globbuf)) {
			tracemsg("glob %s failed (%s)", dirpath, strerror(errno));
			PD_LOG(LOG_ERR, "glob %s failed (%s)", dirpath, strerror(errno));
			globfree(&globbuf);
			return 4;
		}
		globbuf.gl_pathv[0] = dotdot;

		for (i = 0; i < globbuf.gl_offs+globbuf.gl_pathc; i++) {
			if (globbuf.gl_pathv[i] == NULL) {
				PD_LOG(LOG_ERR, "skipped NULL at i=%d\n", i);
				continue;
			}

			strncpy(fullpath+length, globbuf.gl_pathv[i], FNAMESIZE);
			fullpath[2*FNAMESIZE-1] = '\0';

			ret += one_lsdir_line (fullpath, length, &items, &item_count);
		}
		globfree(&globbuf);
	}
	PD_LOG(LOG_INFO, "finished %d items (err %d) dirsort:%d", item_count, ret, cnf.lsdirsort);

	/* qsort() and append_line() calls
	*/
	if (items != NULL) {
		if (cnf.lsdirsort == 2) {
			qsort (items, item_count, sizeof(LSDIRITEM), lsdir_item_cmp_size);
		} else if (cnf.lsdirsort == 1) {
			qsort (items, item_count, sizeof(LSDIRITEM), lsdir_item_cmp_mtime);
		} else {
			qsort (items, item_count, sizeof(LSDIRITEM), lsdir_item_cmp_entry);
		}
		for (i=0; i < item_count; i++) {
			lx = append_line (lp, items[i].one_line);
			if (lx != NULL) {
				CURR_FILE.num_lines++;
				lp = lx;
			} else {
				ret = 1;
				break;
			}
		}
		FREE(items); items = NULL;
	}

	/* pull and update
	*/
	CURR_LINE = CURR_FILE.top->next;
	CURR_FILE.lineno = 1;
	if (TEXT_LINE(CURR_LINE)) { /* skip to the ".." line */
		CURR_LINE = CURR_LINE->next;
		CURR_FILE.lineno = 2;
	}
	update_focus(FOCUS_ON_2ND_LINE, cnf.ring_curr);
	CURR_FILE.fflag &= ~FSTAT_CMD; /* select text area */

	return ret;
}

/*
* simple_parser - try to parse filename and linenum from the dataline, and jump to that position
* patterns (make, egrep, find outputs):
*	[path/]<filename>SEP<lineno>SEP
* SEP can be ':' or even '-'
* returns 0 on success, 1 if lineno not found, 2 if file is scratch and -1 if filename open failed
*/
int
simple_parser (const char *dataline, int jump_mode)
{
	int ret=0, len, split;
	char filename[FNAMESIZE+LINESIZE_INIT];
	char patt0[TAGSTR_SIZE];
	char patt1[TAGSTR_SIZE];
	LINE *lx = NULL;
	int linenum;

	len = strlen(dataline);
	if (len == 0 || len >= FNAMESIZE+LINESIZE_INIT) {
		return (1);
	}

	/* optional-path/filename:12345: ... */
	/* optional-path/filename-12345- ... */

	/* simple match */
	strncpy(patt0, "^([^:]+):([0-9]+)", 100);
	/* missing ungreedy match */
	strncpy(patt1, "^(.+)[-]([0-9]+)[-]", 100);

	if (len>2 && dataline[0] == '.' && dataline[1] == '/') {
		dataline += 2;	/* cut useless prefix */
		len -= 2;
	}

	filename[0] = '\0';
	linenum = -1;
	split = -1;
	if (!regexp_match(dataline, patt0, 1, filename)) {
		split = strlen(filename);
		linenum = strtol(&dataline[split+1], NULL, 10);
		if (cnf.bootup) {
			PD_LOG(LOG_DEBUG, "split=%d --> [%s] :%d", split, filename, linenum);
		}
	} else if (!regexp_match(dataline, patt1, 1, filename)) {
		split = strlen(filename);
		linenum = strtol(&dataline[split+1], NULL, 10);
		if (cnf.bootup) {
			PD_LOG(LOG_DEBUG, "split=%d --> [%s] :%d", split, filename, linenum);
		}
	}

	ret = -1;
	if (split > 0 && filename[0] != '\0') {
		if (linenum < 0)
			linenum = 0;	/* fallback */

		/* open file or switch to
		 */
		if (add_file(filename) == 0) {
			ret = 2;
			if (!(CURR_FILE.fflag & FSTAT_SCRATCH)) {
				ret = 1;
				if (jump_mode == SIMPLE_PARSER_WINKIN) {
					mhist_push (cnf.ring_curr, CURR_FILE.lineno);
				}
				lx = lll_goto_lineno (cnf.ring_curr, linenum);
				if (lx != NULL) {	/* maybe bottom */
					set_position (cnf.ring_curr, linenum, lx);
					ret = 0;
				}
			}
		}
	}

	return (ret);
}

/*
* python_parser - try to parse filename and lineno from the dataline, and jump to that position
* very experimental, jump back is based on origin and regex patterns used for lineno
* returns 0 on success, 2 if the buffer is not open or scratch, 1 if lineno not found and -1 if pattern does not match
*/
int
python_parser (const char *dataline)
{
	int ret=0, len;
	char strz[FNAMESIZE+LINESIZE_INIT];
	char patt0[TAGSTR_SIZE];
	char patt1[TAGSTR_SIZE];
	char patt2[TAGSTR_SIZE];
	char patt3[TAGSTR_SIZE];
	LINE *lx = NULL;
	int linenum, origin, safeback;

	len = strlen(dataline);
	if (len == 0 || len >= FNAMESIZE+LINESIZE_INIT) {
		return (1);
	}

	/* pyflakes ...
	* /^<filename>:(\d+):/
	*/
	strncpy(patt0, "^[^:]+:([[:digit:]]+):", 100);

	/* pylint -E ...
	* /^E:(\d+),\d+:/
	*/
	strncpy(patt1, "^E:[[:blank:]]*([[:digit:]]+),[[:blank:]]*[[:digit:]]+:", 100);

	/* python -m py_compile ...
	*  /^  File "<filename>", line (\d+)/
	*  /.* <filename>, line (\d+)$/
	*/
	strncpy(patt2, "^[[:blank:]]*File[[:blank:]]+\"[^\"]+\", line[[:blank:]]+([[:digit:]]+)", 100);
	strncpy(patt3, "\\([^,]+, line[[:blank:]]+([[:digit:]]+)\\)$", 100);

	if (len>2 && dataline[0] == '.' && dataline[1] == '/') {
		dataline += 2;	/* cut useless prefix */
		len -= 2;
	}

	strz[0] = '\0';
	linenum = -1;
	if (!regexp_match(dataline, patt0, 1, strz)) {
		linenum = strtol(strz, NULL, 10);
		PD_LOG(LOG_DEBUG, "patt0 --> :%d", linenum);
	} else if (!regexp_match(dataline, patt1, 1, strz)) {
		linenum = strtol(strz, NULL, 10);
		PD_LOG(LOG_DEBUG, "patt1 --> :%d", linenum);
	} else if (!regexp_match(dataline, patt2, 1, strz)) {
		linenum = strtol(strz, NULL, 10);
		PD_LOG(LOG_DEBUG, "patt2 --> :%d", linenum);
	} else if (!regexp_match(dataline, patt3, 1, strz)) {
		linenum = strtol(strz, NULL, 10);
		PD_LOG(LOG_DEBUG, "patt3 --> :%d", linenum);
	}

	ret = -1;
	if (linenum > 0) {
		/* switch to the origin buffer, if possible
		 */
		ret = 2;
		origin = CURR_FILE.origin;
		if (origin >= 0 && origin < RINGSIZE && \
		    (cnf.fdata[origin].fflag & FSTAT_OPEN) && \
		    !(cnf.fdata[origin].fflag & FSTAT_SCRATCH))
		{
			PD_LOG(LOG_DEBUG, "try switch to origin %d", origin);
			safeback = cnf.ring_curr;
			cnf.ring_curr = origin;
			lx = lll_goto_lineno (cnf.ring_curr, linenum);
			if (lx != NULL) {	/* maybe bottom */
				set_position (cnf.ring_curr, linenum, lx);
				ret = 0;
			} else {
				PD_LOG(LOG_ERR, "switch to origin %d failed, safeback %d", origin, safeback);
				cnf.ring_curr = safeback;
				ret = 1;
			}
		}
	}

	return (ret);
}

/*
** parse_open - get nonspace characters around cursor, assuming that is a filename
**	try to open file
*/
int
parse_open (void)
{
	LINE *lp;
	char strz[FNAMESIZE];
	int beg=0, end=0, lncol, len;

#define FN(ch)	( ((ch) >= 'a' && (ch) <= 'z') || ((ch) >= 'A' && (ch) <= 'Z') || \
		((ch) >= '0' && (ch) <= '9') || ((ch) == '_') || \
		((ch) == '.') || ((ch) == '+') || ((ch) == '-') || ((ch) == '/') )

	lp = CURR_LINE;
	lncol = CURR_FILE.lncol;
	if ( !TEXT_LINE(lp) || lncol >= lp->llen-1 || !FN(lp->buff[lncol]) ) {
		return (0);
	}
	strz[0]='\0';

	for (beg = lncol; beg >= 0 && FN(lp->buff[beg]); beg--)
		;
	++beg;
	for (end = lncol; end < lp->llen-1 && FN(lp->buff[end]); end++)
		;
	--end;
	PD_LOG(LOG_DEBUG, "lncol %d beg %d end %d", lncol, beg, end);
	if (beg <= end) {
		len = end-beg+1;
		if (len < FNAMESIZE) {
			strncpy (strz, &lp->buff[beg], (size_t)len);
			strz[len] = '\0';
			PD_LOG(LOG_DEBUG, "strz [%s]", strz);
			add_file(strz);
		}
	}

	return (0);
}

/*
* diff_parser - parser for diff files of type subversion and cleartool
*/
int
diff_parser (const char *dataline)
{
	int len=0, ret=0, diff_type=0, ring_tg=0;
	int tgline = 0, tglen = 0;
	char patt1[TAGSTR_SIZE];
	char xmatch[TAGSTR_SIZE];

	len = strlen(dataline);
	if (len == 0) {
		return (1);
	}

	ret = select_diff_section (&diff_type, &ring_tg);
	if (ret) {
		PD_LOG(LOG_DEBUG, "select_diff_section failed, ret=%d", ret);
		return (ret);
	}

	/* parse and jump to
	*/
	if (diff_type == 1) {	/* unified diff */
		/* range: (number) | (begin,length) */

		/* @@ -RANGE0 +RANGE1 @@ */
		strncpy(patt1, "^@@ \\-[0-9,]+ \\+([0-9,]+) @@", 100);

		ret = 2;
		if (CURR_LINE->llen > 10 && CURR_LINE->buff[0] == '@' && CURR_LINE->buff[1] == '@')
		{
			if (!regexp_match(CURR_LINE->buff, patt1, 1, xmatch)) {
				if (!unhide_udiff_range(ring_tg, xmatch, &tgline, &tglen)) {
					cnf.ring_curr = ring_tg;
					ret = 0;
				}
			}
		} else {
			PD_LOG(LOG_DEBUG, "diff parser only for @@... lines");
			tracemsg("diff parser only for @@... lines");
		}

	} else {
		ret = 1;
	}

	if (ret==0) {
		update_focus(CENTER_FOCUSLINE, cnf.ring_curr);
		go_home(); // sane position
	}
	return (ret);
}

/*
* general_parser - general internal parser for SPECIAL buffers,
*	ring list, directory list, find/grep or make output
*/
void
general_parser (void)
{
	char *fname = NULL;
	char *dataline = NULL;
	struct stat test;
	unsigned als=0;

	if (!TEXT_LINE(CURR_LINE)) {
		return;
	}
	if ((CURR_LINE->llen <= 0) || (CURR_LINE->buff[0] == '$')) {
		return;
	}

	als = ALLOCSIZE(CURR_LINE->llen+10);
	dataline = (char *) MALLOC (als);
	if (dataline == NULL) {
		ERRLOG(0xE033);
		return;
	}
	strncpy(dataline, CURR_LINE->buff, (size_t)CURR_LINE->llen);
	dataline[CURR_LINE->llen] = '\0';
	if (CURR_LINE->llen > 0) {
		dataline[CURR_LINE->llen-1] = '\0';	/* newline */
	}

	if (strncmp(CURR_FILE.fname, "*ring*", 6) == 0) {
		ringlist_parser (dataline);
	}
	else if (strncmp(CURR_FILE.fname, "*ls*", 4) == 0) {
		fname = dirlist_parser (dataline);
		if (fname != NULL) {
			if (stat(fname, &test) == 0) {
				if (S_ISDIR(test.st_mode)) {
					/* add trailing slash later */
					lsdir(fname);
				} else if (S_ISREG(test.st_mode)) {
					/* read can fail (no permission, etc) */
					PD_LOG(LOG_DEBUG, "try open %s", fname);
					add_file(fname);
				} else {
					tracemsg("Cannot handle %s", fname);
					PD_LOG(LOG_ERR, "cannot handle filesystem item, name=%s mode=0x%x", fname, test.st_mode);
				}
			} else {
				tracemsg("Cannot stat %s", fname);
				PD_LOG(LOG_ERR, "cannot stat %s (%s)", fname, strerror(errno));
			}
			FREE(fname); fname = NULL;
		} else {
			tracemsg("directory list parser failed");
			PD_LOG(LOG_ERR, "dirlist_parser failed");
		}
	}
	else if ((strncmp(CURR_FILE.fname, "*find*", 6) == 0) ||
		(strncmp(CURR_FILE.fname, "*make*", 6) == 0)) {
		simple_parser (dataline, SIMPLE_PARSER_JUMP);
	}
	else if (strncmp(CURR_FILE.fname, "*sh*", 4) == 0) {
		python_parser (dataline);
	}
	else if (strncmp(CURR_FILE.fname, "*diff*", 6) == 0) {
		diff_parser (dataline);
	}

	FREE(dataline); dataline = NULL;
	return;
}

/*
** is_special - show buffer or set buffer ftype, for
**	regular file types (c/cpp/c++, perl, python, bash/shell, text) and
**	special buffers (sh, ls, make, find, diff, configured VCS tools);
**	or change fname to special buffer
*/
int
is_special (const char *special)
{
	unsigned slen=0, i=0;
	char tfname[FNAMESIZE];
	int ri = cnf.ring_curr, other_ri;

	/* prepare tfname for comparison as well as for target filename */
	slen=0;
	if (special != NULL && special[0] != '\0') {
		/* create a buffer name, to use it later */
		tfname[slen++] = '*';
		for (i=0; special[i] != '\0' && i < sizeof(tfname)-2; i++) {
			if (special[i] == '*') {
				; /* skip */
			} else if (special[i] >= 'A' && special[i] <= 'Z') {
				tfname[slen] = special[i] - 'A' +'a';
				slen++;
			} else {
				tfname[slen] = special[i];
				slen++;
			}
		}
		tfname[slen++] = '*';
	}
	tfname[slen] = '\0';

	if (slen <= 2) {
		/* show name, there were no parameter passed
		*/
		if (strncmp(CURR_FILE.fname, "*sh*", 4) == 0) {
			tracemsg ("shell command (output) buffer");
		} else if (strncmp(CURR_FILE.fname, "*make*", 6) == 0) {
			tracemsg ("make (output) buffer");
		} else if (strncmp(CURR_FILE.fname, "*find*", 6) == 0) {
			tracemsg ("find (output) buffer");
		} else if (strncmp(CURR_FILE.fname, "*ls*", 4) == 0) {
			tracemsg ("directory listing buffer");
		} else if (strncmp(CURR_FILE.fname, "*diff*", 6) == 0) {
			tracemsg ("diff buffer");
		} else if (strncmp(CURR_FILE.fname, "*ring*", 6) == 0) {
			tracemsg ("list of open buffers");
		} else if (strncmp(CURR_FILE.fname, "*cmds*", 6) == 0) {
			tracemsg ("list of commands");
		}
		else if ((CURR_FILE.fflag & (FSTAT_SPECW | FSTAT_SCRATCH)) == 0) {
			switch (CURR_FILE.ftype)
			{
				case C_FILETYPE:
					tracemsg("C/C++");
					break;
				case PERL_FILETYPE:
					tracemsg("Perl");
					break;
				case PYTHON_FILETYPE:
					tracemsg("Python");
					break;
				case SHELL_FILETYPE:
					tracemsg("Bash/Shell");
					break;
				case TEXT_FILETYPE:
				default:
					tracemsg("Text");
					break;
			}
		} else {
			for(i=0; i < 10; i++) {
				slen = strlen(cnf.vcs_tool[i]);
				if (slen>0 && !strncmp(CURR_FILE.fname+1, cnf.vcs_tool[i], slen)) {
					tracemsg ("version control tool, %s, output buffer", cnf.vcs_tool[i]);
					break;
				}
			}
			if (i == 10) {
				tracemsg ("Unknown");
			}
		}

		return (0);
	}

	if (CURR_FILE.pipe_output != 0) {
		tracemsg("do not change name, background process is running");
		return (0);
	}

	/* change ftype, internal value */
	if ((strncmp(tfname, "*c*", slen) == 0) || (strncmp(tfname, "*C*", slen) == 0) ||
	    (strncmp(tfname, "*cpp*", slen) == 0) || (strncmp(tfname, "*c++*", slen) == 0))
	{
		CURR_FILE.ftype = C_FILETYPE;
	} else if (strncmp(tfname, "*perl*", slen) == 0) {
		CURR_FILE.ftype = PERL_FILETYPE;
	} else if (strncmp(tfname, "*python*", slen) == 0) {
		CURR_FILE.ftype = PYTHON_FILETYPE;
	} else if ((strncmp(tfname, "*bash*", slen) == 0) || (strncmp(tfname, "*shell*", slen) == 0)) {
		CURR_FILE.ftype = SHELL_FILETYPE;
	} else if (strncmp(tfname, "*text*", slen) == 0) {
		CURR_FILE.ftype = TEXT_FILETYPE;

	/* change fname, according to tfname, only for special buffers */
	} else {
		for(i=0; i < 10; i++) {
			slen = strlen(cnf.vcs_tool[i]);
			if (slen>0 && !strncmp(tfname+1, cnf.vcs_tool[i], slen))
				break;
		}
		if ((strncmp(tfname, "*sh*", 4) == 0) ||
			   (strncmp(tfname, "*make*", 6) == 0) ||
			   (strncmp(tfname, "*find*", 6) == 0) ||
			   (strncmp(tfname, "*ls*", 4) == 0) ||
			   (i < 10)  /* vcs tool */ ||
			   (strncmp(tfname, "*diff*", 6) == 0) ||
			   (strncmp(tfname, "*ring*", 6) == 0))
		{
			/* tfname may already exist in the ring, *diff* should be not doubled */
			if (strncmp(tfname, "*diff*", 6) == 0) {
				other_ri = query_scratch_fname (tfname);
				if (other_ri != -1) {
					cnf.ring_curr = other_ri;
					drop_file();
					cnf.ring_curr = ri;
				}
			}

			CURR_FILE.fflag |= (FSTAT_SPECW | FSTAT_SCRATCH);
			CURR_FILE.fflag |= (FSTAT_NOEDIT | FSTAT_NOADDLIN);

			CURR_FILE.fpath[0] = '\0';
			strncpy (CURR_FILE.fname, tfname, FNAMESIZE);
			CURR_FILE.fname[FNAMESIZE-1] = '\0';
			CURR_FILE.ftype = TEXT_FILETYPE;

			/* ri not changed but the content, update titles and headers */
			cnf.gstat |= GSTAT_REDRAW;

			memset (&cnf.fdata[ri].stat, 0, sizeof(struct stat));
			cnf.fdata[ri].ftype = TEXT_FILETYPE;

			CURR_FILE.fflag |= FSTAT_CMD | FSTAT_OPEN | FSTAT_FMASK;
		}
	}

	return (0);
}

/*
** search_word - start immediate find search with the word under cursor
*/
int
search_word (void)
{
	char pattern[TAGSTR_SIZE];
	char *word=NULL;
	int length=0;

	reset_search();

	memset(pattern, 0, sizeof(pattern));
	word = select_word(CURR_LINE, CURR_FILE.lncol);
	if (word != NULL && word[0] != '\0') {
		/* set search pattern */
		pattern[0] = '\0';
		strncat(pattern, "/\\<", 4);
		if (word[0] == '.' || word[0] == '>') {
			strncat(pattern, word+1, sizeof(pattern)-8);
		} else {
			strncat(pattern, word, sizeof(pattern)-8);
		}
		strncat(pattern, "\\>/", 4);
		/* save */
		length = strlen(pattern);
		cmdline_to_clhistory(pattern, length);
		/* lets run */
		go_home();	/* don't leave this item */
		search(pattern);
	} else {
		reset_search();
	}
	FREE(word); word = NULL;

	return (0);
}

/*
** tag_line_byword - mark lines with color containing the word under cursor (tagging the lines)
*/
int
tag_line_byword (void)
{
	char pattern[TAGSTR_SIZE];
	char *word=NULL;
	int length=0;

	memset(pattern, 0, sizeof(pattern));
	word = select_word(CURR_LINE, CURR_FILE.lncol);
	if (word != NULL && word[0] != '\0') {
		/* set tag pattern */
		pattern[0] = '\0';
		strncat(pattern, "tag /\\<", 8);
		if (word[0] == '.' || word[0] == '>') {
			strncat(pattern, word+1, sizeof(pattern)-12);
		} else {
			strncat(pattern, word, sizeof(pattern)-12);
		}
		strncat(pattern, "\\>/", 4);
		/* save */
		length = strlen(pattern);
		cmdline_to_clhistory(pattern, length);
		/* lets run */
		color_tag(&pattern[4]);
	} else {
		/* reset */
		color_tag("");
	}
	FREE(word); word = NULL;

	return (0);
}

/*
** prefix_macro - set on/off the prefix of the lines
*/
int
prefix_macro (void)
{
	if (cnf.gstat & GSTAT_PREFIX) {
		set("prefix off");
	} else {
		set("prefix on");
	}
	/* cnf.gstat &= ~(GSTAT_UPDNONE | GSTAT_UPDFOCUS); */
	return (0);
}

/*
** tabhead_macro - set on/off the tab header under status line
*/
int
tabhead_macro (void)
{
	if (cnf.gstat & GSTAT_TABHEAD) {
		set("tabhead off");
	} else {
		set("tabhead on");
		cnf.gstat |= GSTAT_REDRAW;
	}
	/* cnf.gstat &= ~(GSTAT_UPDNONE | GSTAT_UPDFOCUS); */
	return (0);
}

/*
** smartind_macro - set on/off the smartindent setting
*/
int
smartind_macro (void)
{
	if (cnf.gstat & GSTAT_SMARTIND) {
		set("smartindent off");
	} else {
		set("smartindent on");
	}
	return (0);
}

/*
** shadow_macro - set on/off the shadow marker (hidden lines counter)
*/
int
shadow_macro (void)
{
	if (cnf.gstat & GSTAT_SHADOW) {
		set("shadow off");
	} else {
		set("shadow on");
	}
	/* cnf.gstat &= ~(GSTAT_UPDNONE | GSTAT_UPDFOCUS); */
	return (0);
}

/*
** incr_filter_cycle - increment filter level in cycle
*/
int
incr_filter_cycle (void)
{
	int cnt;

	CURR_FILE.flevel++;
	if (FMASK(CURR_FILE.flevel) == 0) {
		/* reset */
		CURR_FILE.flevel = 1;
	}

	if (HIDDEN_LINE(cnf.ring_curr,CURR_LINE)) {
		next_lp (cnf.ring_curr, &(CURR_LINE), &cnt);
		CURR_FILE.lineno += cnt;
		CURR_FILE.lncol = get_col(CURR_LINE, CURR_FILE.curpos);
	}
	update_focus(FOCUS_AVOID_BORDER, cnf.ring_curr);

	return (0);
}

/*
** fw_option_switch - switch on/off the full word option for egrep/find
*/
int
fw_option_switch (void)
{
	char *p = NULL;
	unsigned n, shift;

	p = strstr(cnf.find_opts, "-w");
	if (p == NULL) {
		/* concatenate */
		n = strlen(cnf.find_opts);
		if (n+4 < sizeof(cnf.find_opts)) {
			cnf.find_opts[n++] = ' ';
			cnf.find_opts[n++] = '-';
			cnf.find_opts[n++] = 'w';
			cnf.find_opts[n++] = '\0';
		}
	} else if (p > cnf.find_opts) {
		/* check space */
		if (*(p-1) == ' ') {
			shift = 3;		/* space before */
			--p;
		} else if (*(p+1) == ' ') {
			shift = 3;		/* space after */
		} else {
			shift = 2;		/* no space */
		}
		/* remove */
		while (*(p+shift) != '\0') {
			*p = *(p+shift);
			p++;
		}
		*p = '\0';
	}

	tracemsg ("find_opts [%s]", cnf.find_opts);
	return (0);
}

/*
** fsearch_path_macro - change the path elements of the find command,
**	and show the find_opts setting
*/
int
fsearch_path_macro (const char *fpath)
{
	char suffix[sizeof(cnf.find_opts)];
	unsigned ix=0, slen=0;

	if (fpath[0] == '\0')
		return(0);

	if (fpath[0] != '\0') {
		for (ix=0; cnf.find_opts[ix] != '\0'; ix++) {
			if (slen > 0) {
				suffix[slen++] = cnf.find_opts[ix];
			} else if (cnf.find_opts[ix]==' ' && cnf.find_opts[ix+1]=='-') {
				suffix[slen++] = cnf.find_opts[ix];
			}

		}
		suffix[slen] = '\0';

		ix = strlen(fpath);
		if (ix > 0 && ix+slen+1 < sizeof(cnf.find_opts)) {
			strncpy(cnf.find_opts, fpath, ix+1);
			strncat(cnf.find_opts, suffix, slen); /* ...+slen+1 */
		} else {
			tracemsg ("find search path very large");
			return (1);
		}
	}

	tracemsg ("find_opts [%s]", cnf.find_opts);
	return (0);
}

/*
** fsearch_args_macro - change the name patterns of find command,
**	like "fsea *.[ch] *.sh" or "fsea *.py" and show the find_opts setting
*/
int
fsearch_args_macro (const char *fparams)
{
	char suffix[sizeof(cnf.find_opts)];
	char *p=NULL;
	char *q=NULL;
	unsigned ix=0, iy=0, slen=0;
	char patterns[1024];

	if (fparams[0] == '\0')
		return(0);

	/*
	 * replace "w1 w2 w3" by
	 * " -name 'w1' -o -name 'w2' -o -name 'w3' "
	*/
	memset(patterns, '\0', sizeof(patterns));
	slen = sizeof(patterns);
	ix = iy = 0;
	while (fparams[ix] != '\0' && iy+15 < slen) {
		if (ix == 0) {
			strncat(&patterns[iy], " -name '", 8);
			iy += 8;
			patterns[iy++] = fparams[ix++];
		} else if (fparams[ix] == ' ') {
			while (fparams[ix] == ' ') {
				ix++;
			}
			strncat(&patterns[iy], "' -o -name '", 12);
			iy += 12;
			patterns[iy++] = fparams[ix++];
		} else {
			patterns[iy++] = fparams[ix++];
		}
		if (fparams[ix] == '\0') {
			patterns[iy++] = 0x27;
			patterns[iy++] = ' ';
		}
	}
	if (ix > 0 && fparams[ix] != '\0') {
		tracemsg("parameter string very large, cannot create patterns");
		PD_LOG(LOG_ERR, "parameter string very large, cannot create patterns");
		return (1);
	}
	patterns[iy] = '\0';
	slen = iy; /* length of text to be inserted */

	p = strstr(cnf.find_opts, "-type f ");
	q = strstr(cnf.find_opts, " -exec egrep");
	ix = iy = 0;
	if (p!=NULL && q!=NULL && p<q) {
		ix = p - cnf.find_opts + 8; /* length of left side */
		iy = strlen(q); /* length of right side */
	} else {
		tracemsg("cannot change find_opts setting, type and/or exec missing");
		PD_LOG(LOG_ERR, "find_opts does not contain '-type f' and/or '-exec egrep' patterns");
		return (1);
	}
	strncpy(suffix, q, iy);
	suffix[iy] = '\0';

	if (slen == 0) { // and ix+iy < sizeof(cnf.find_opts)
		cnf.find_opts[ix] = '\0';
		strncat(cnf.find_opts+ix, suffix, iy);
	} else if (ix +1 +slen +1 +iy +1 < sizeof(cnf.find_opts)) {
		cnf.find_opts[ix] = '(';
		cnf.find_opts[ix+1] = '\0';
		strncat(cnf.find_opts+ix+1, patterns, slen);
		cnf.find_opts[ix+1+slen] = ')';
		cnf.find_opts[ix+1+slen+1] = '\0';
		strncat(cnf.find_opts+ix+1+slen+1, suffix, iy);
	} else {
		tracemsg("replacement for find_opts very long");
		PD_LOG(LOG_ERR, "replacement for find_opts very long");
		return (1);
	}

	tracemsg ("find_opts [%s]", cnf.find_opts);
	return (0);
}


/*
** mouse_support - switch on/off experimental mouse support,
**	(cursor repositioning by mouse click, text selection by mouse requires shift key)
*/
int
mouse_support (void)
{
	int Xpid;

	if (!cnf.bootup) {
		if ((Xpid = pidof("X")) < 0)
			Xpid = pidof("Xorg");
		if ((Xpid > 0) && (cnf.gstat & GSTAT_MOUSE)) {
			(void) mousemask(BUTTON1_CLICKED, NULL);
		} else {
			(void) mousemask(0, NULL);
			cnf.gstat &= ~(GSTAT_MOUSE);
		}
	} else {
		if (cnf.gstat & GSTAT_MOUSE) {
			cnf.gstat &= ~(GSTAT_MOUSE);
			(void) mousemask(0, NULL);
			tracemsg ("mouse_support off");
		} else {
			if ((Xpid = pidof("X")) < 0)
				Xpid = pidof("Xorg");
			if (Xpid > 0) {
				cnf.gstat |= (GSTAT_MOUSE);
				(void) mousemask(BUTTON1_CLICKED, NULL);
				tracemsg ("mouse_support on, repositioning by mouse click, but selection requires shift key");
			} else {
				tracemsg ("mouse_support off, no pointing, selection works as usual");
			}
		}
	}

	return (0);
}

/*
** locate_find_switch - switch between external (find) or internal (locate) search method,
**	for multiple file search
*/
int
locate_find_switch (void)
{
	if (cnf.gstat & GSTAT_LOCATE) {
		cnf.gstat &= ~(GSTAT_LOCATE);
		tracemsg("use external search method, find/egrep");
	} else {
		cnf.gstat |= GSTAT_LOCATE;
		tracemsg("use internal search method, locate");
	}
	return (0);
}

/*
** recording_switch - switch macro recording on/off to the temporary logfile
**	~/.eda/macro.log, each recording session will overwrite
*/
int
recording_switch (void)
{
	if (cnf.gstat & GSTAT_RECORD) {
		cnf.gstat &= ~(GSTAT_RECORD);
		tracemsg("macro recording finished");
	} else {
		cnf.gstat |= GSTAT_RECORD;
		record(NULL, NULL);
		tracemsg("macro recording initiated");
	}
	return (0);
}

/*
** multisearch_cmd - multiple file search with external or internal method,
**	depending on the locate switch setting, external by default
*/
int
multisearch_cmd (void)
{
	if (cnf.gstat & GSTAT_LOCATE) {
		return locate_cmd("");
	} else {
		return find_cmd("");
	}
}

/*
** find_window_switch - jump to the file search, usually find, output window or back
*/
int
find_window_switch (void)
{
	int ri, origin, i, candidates[] = {-1, -1, -1, -1, -1, -1}, other;

	if (strncmp(CURR_FILE.fname, "*find*", 6) == 0) {
		origin = CURR_FILE.origin;
	} else if (strncmp(CURR_FILE.fname, "*make*", 6) == 0) {
		origin = CURR_FILE.origin;
	} else if (strncmp(CURR_FILE.fname, "*sh*", 4) == 0) {
		origin = CURR_FILE.origin;
	} else {

		/* jump back to spec.buffer */
		origin = cnf.ring_curr;
		for (ri = 0; ri < RINGSIZE; ri++) {
			if ((cnf.fdata[ri].fflag & (FSTAT_OPEN | FSTAT_SPECW)) == (FSTAT_OPEN | FSTAT_SPECW)) {
				other = (cnf.fdata[ri].origin == origin) ? 0 : 3;
				if (strncmp(cnf.fdata[ri].fname, "*find*", 6) == 0) {
					candidates[0+other] = ri;
					PD_LOG(LOG_NOTICE, "jump back: ri=%d %scandidate -- find", ri, other ? "other " : "");
				} else if (strncmp(cnf.fdata[ri].fname, "*make*", 6) == 0) {
					candidates[1+other] = ri;
					PD_LOG(LOG_NOTICE, "jump back: ri=%d %scandidate -- make", ri, other ? "other " : "");
				} else if (strncmp(cnf.fdata[ri].fname, "*sh*", 4) == 0) {
					candidates[2+other] = ri;
					PD_LOG(LOG_NOTICE, "jump back: ri=%d %scandidate -- sh", ri, other ? "other " : "");
				}
			}
		}
		for (i = 0; i < 6; i++) {
			if (candidates[i] != -1) {
				cnf.ring_curr = candidates[i];
				break;
			}
		}
		/* do nothing if special window already closed */
		return (0);

	}

	/* jump from spec.buffer to file by origin */
	if (origin >= 0 && origin < RINGSIZE && (cnf.fdata[origin].fflag & FSTAT_OPEN)) {
		PD_LOG(LOG_NOTICE, "jump to origin=%d (from %d %s)", origin, cnf.ring_curr, CURR_FILE.fname);
		cnf.ring_curr = origin;
	}
	/* do nothing if original window already closed */
	return (0);
}

/*
** word_case - switch the case of characters in the word under cursor
*/
int
word_case (void)
{
	int pos=0;
	int ret=0, mod=0;
	int type=0;

	if ( !TEXT_LINE(CURR_LINE) || CURR_FILE.lncol >= CURR_LINE->llen-1 ||
	     !IS_ID(CURR_LINE->buff[CURR_FILE.lncol]) ) {
		return (0);
	}

	CURR_FILE.fflag &= ~FSTAT_CMD;

	/* get the word begin */
	for (pos = CURR_FILE.lncol; pos >= 0 && IS_ID(CURR_LINE->buff[pos]); pos--) {
		/* select conversion */
		if (CURR_LINE->buff[pos] >= 'A' && CURR_LINE->buff[pos] <= 'Z') {
			type = 1;
		} else if (CURR_LINE->buff[pos] >= 'a' && CURR_LINE->buff[pos] <= 'z') {
			type = 2;
		}
	}
	pos++;

	/* along the word */
	for (; pos < CURR_LINE->llen-1 && IS_ID(CURR_LINE->buff[pos]); pos++) {
		switch (type) {
		case 1:		/* to lower */
			if (CURR_LINE->buff[pos] >= 'A' && CURR_LINE->buff[pos] <= 'Z') {
				CURR_LINE->buff[pos] += 'a' - 'A';
				mod++;
			}
			break;
		case 2:		/* to upper */
			if (CURR_LINE->buff[pos] >= 'a' && CURR_LINE->buff[pos] <= 'z') {
				CURR_LINE->buff[pos] += 'A' - 'a';
				mod++;
			}
			break;
		default:	/* nothing found in the word */
			ret = 1;
			break;
		}
	}

	if (mod) {
		CURR_LINE->lflag |= LSTAT_CHANGE;
		CURR_FILE.fflag |= FSTAT_CHANGE;
	}

	return (ret);
}

/*
** ins_varname - insert word into command line
*/
int
ins_varname (void)
{
	char *vn=NULL;
	int ret=0;

	vn = select_word(CURR_LINE, CURR_FILE.lncol);
	if (vn != NULL && vn[0] != '\0') {
		if (vn[0] == '.' || vn[0] == '>') {
			ret = type_cmd(vn+1);
		} else {
			ret = type_cmd(vn);
		}
	} else {
		tracemsg("cannot identify word");
	}
	FREE(vn); vn = NULL;

	return (ret);
}

/*
** ins_bname - insert block/function name into command line or focus line in text area
*/
int
ins_bname (void)
{
	char *bn=NULL;
	int ret = 0;

	bn = block_name(cnf.ring_curr);
	if (bn != NULL && bn[0] != '\0') {
		if (CURR_FILE.fflag & FSTAT_CMD) {
			ret = type_cmd(bn);
		} else {
			ret = type_text(bn);
		}
	}
	FREE(bn); bn = NULL;

	return (ret);
}

/*
** view_bname - view block/function name in notification, try even from make and find buffers
*/
int
view_bname (void)
{
	char *bn = NULL;
	char *dataline = NULL;
	int orig_ri = cnf.ring_curr;
	int jump = 0;

	if (CURR_FILE.fflag & FSTAT_SPECW) {
		/* special buffer */
		if ((strncmp(CURR_FILE.fname, "*find*", 6) == 0) ||
		    (strncmp(CURR_FILE.fname, "*make*", 6) == 0)) {
			if (!TEXT_LINE(CURR_LINE)) {
				return (0);
			}
			if ((CURR_LINE->llen == 0) || (CURR_LINE->buff[0] == '$')) {
				return (0);
			}

			/* copy and strip newline -- more space? */
			if (csere0(&dataline, 0, 0, CURR_LINE->buff, CURR_LINE->llen-1)) {
				return (1);
			}

			/* open file and jump to */
			jump = simple_parser (dataline, SIMPLE_PARSER_WINKIN);
			if (jump == 0 || jump == 1) {
				bn = block_name(cnf.ring_curr);
				mhist_pop(); /* restore original position in target */
			}
			FREE(dataline); dataline = NULL;
			cnf.ring_curr = orig_ri;
		} else {
			/* not supported */
			return (0);
		}

	} else {
		/* regular buffer */
		bn = block_name(cnf.ring_curr);
	}

	if (bn != NULL && bn[0] != '\0') {
		tracemsg("%s", bn);
	} else {
		tracemsg("unknown");
	}
	FREE(bn); bn = NULL;

	return (0);
}

/*
** ins_filename - insert original filename of buffer into command line or text area
*/
int
ins_filename (void)
{
	const char *fn;
	int ret=0;

	fn = CURR_FILE.fname;
	if (fn[0] != '\0') {
		if (CURR_FILE.fflag & FSTAT_CMD) {
			ret = type_cmd(fn);
		} else {
			ret = type_text(fn);
		}
	}

	return (ret);
}

/*
** xterm_title - replace xterm title with given string
*/
int
xterm_title (const char *xtitle)
{
	char xtbuf[100+10]; /* magic number! 100 */
	int blen=0, wlen=0, slen=0;

	/* only with X, checking DISPLAY is easier than ps */
	if (getenv("DISPLAY") == NULL) {
		cnf.gstat &= ~GSTAT_AUTOTITLE;
		return (0);
	}

	if (xtitle[0] == '\0') {
		return (0);
	}

	xtbuf[0] = 033;
	xtbuf[1] = ']';
	xtbuf[2] = '0';
	xtbuf[3] = ';';
	blen = 4;

	/* output string max 100... this limitation is ugly, but safe */

	slen = strlen(xtitle);
	if (slen < 100) {
		strncpy (xtbuf+blen, xtitle, (size_t)slen);
		blen += slen;
	} else {
		xtbuf[blen++] = '.';
		xtbuf[blen++] = '.';
		xtbuf[blen++] = '.';
		strncpy (xtbuf+blen, &xtitle[slen-97], 97);
		blen += 97;
	}
	xtbuf[blen++] = 007;
	xtbuf[blen] = '\0';

	if (fcntl(1, F_SETFL, O_NONBLOCK) == -1) {
		ERRLOG(0xE0B3);
		return (2);
	}
	wlen = write(1, (void *)xtbuf, (size_t)blen);
	if (wlen != blen) {
		ERRLOG(0xE0AC);
	}

	return (wlen != blen);
}

/*
** rotate_palette - change color palette setting in cycle, if there are more than the default
*/
int
rotate_palette (void)
{
	if (cnf.palette >= 0 && cnf.palette < cnf.palette_count) {
		if (cnf.palette < cnf.palette_count-1)
			cnf.palette++;
		else
			cnf.palette = 0;

		init_colors_and_cpal();
		tracemsg("palette -> %d [%s]", cnf.palette, cnf.cpal.name);
	}

	return (0);
}

/*
** bm_set - add bookmark to current file and line position,
**	the parameter maybe the bookmark number (1...9) or if omitted, the first free number is selected
*/
int
bm_set (const char *args)
{
	int bm_i;

	if (args[0] == '\0') {
		/* select the next free slot */
		for (bm_i=1; bm_i < 10; bm_i++) {
			if (cnf.bookmark[bm_i].ring == -1) {
				set_bookmark (bm_i);
				break;
			}
		}
		if (bm_i == 10) {
			tracemsg("no more free bookmark!");
		}
	} else {
		bm_i = atoi(&args[0]);
		if (bm_i > 0 && bm_i < 10) {
			/* messages in the setter */
			set_bookmark (bm_i);
		}
	}

	return (0);
}

/*
** bm_clear - clear the given bookmark or all bookmarks,
**	the parameter maybe the bookmark number (1...9) or "*" to remove all
*/
int
bm_clear (const char *args)
{
	int bm_i;

	if (args[0] == '\0')
		return (0);

	if (args[0] == '*') {
		clear_bookmarks(-1);		/* all open files, all bookmarks */
	} else {
		bm_i = atoi(&args[0]);
		if (bm_i > 0 && bm_i < 10)
			clr_bookmark (bm_i);	/* clear this [bm_i] from the right file */
	}

	return (0);
}

/*
** bm_jump1 - jump to 1st bookmark if possible,
**	show bookmark info in notification before jump
*/
int
bm_jump1 (void)
{
	return (jump2_bookmark(1));
}

/*
** bm_jump2 - jump to 2nd bookmark if possible,
**	show bookmark info in notification before jump
*/
int
bm_jump2 (void)
{
	return (jump2_bookmark(2));
}

/*
** bm_jump3 - jump to 3rd bookmark if possible,
**	show bookmark info in notification before jump
*/
int
bm_jump3 (void)
{
	return (jump2_bookmark(3));
}

/*
** bm_jump4 - jump to 4th bookmark if possible,
**	show bookmark info in notification before jump
*/
int
bm_jump4 (void)
{
	return (jump2_bookmark(4));
}

/*
** bm_jump5 - jump to 5th bookmark if possible,
**	show bookmark info in notification before jump
*/
int
bm_jump5 (void)
{
	return (jump2_bookmark(5));
}

/*
** bm_jump6 - jump to 6th bookmark if possible,
**	show bookmark info in notification before jump
*/
int
bm_jump6 (void)
{
	return (jump2_bookmark(6));
}

/*
** bm_jump7 - jump to 7th bookmark if possible,
**	show bookmark info in notification before jump
*/
int
bm_jump7 (void)
{
	return (jump2_bookmark(7));
}

/*
** bm_jump8 - jump to 8th bookmark if possible,
**	show bookmark info in notification before jump
*/
int
bm_jump8 (void)
{
	return (jump2_bookmark(8));
}

/*
** bm_jump9 - jump to 9th bookmark if possible,
**	show bookmark info in notification before jump
*/
int
bm_jump9 (void)
{
	return (jump2_bookmark(9));
}

/*
* set_diff_section - selects all the target file lines in (current) diff file,
*	gets target name from section header and tries to open target file
*	returns 0 if passed or nonzero if failed
*/
static int
set_diff_section (int *diff_type, int *ring_tg)
{
	int ri_ = cnf.ring_curr;
	char pattern[TAGSTR_SIZE];
	char xmatch[TAGSTR_SIZE];
	int orig_lineno = -1;
	LINE *lx = NULL;
	int cnt = 0;

	*diff_type = -1;
	*ring_tg = -1;

	/* check if this is a diff file
	*/
	if ((strncmp(CURR_FILE.fname, "*diff*", 6) != 0))
	{
		tracemsg("not a *diff* buffer");
		return (1);
	}
	if (CURR_FILE.num_lines < 5) {
		tracemsg("no difference in version control");
		return (1);
	}

	/* get section headers
	*/
	if (TEXT_LINE(CURR_LINE)) {
		orig_lineno = CURR_FILE.lineno;
	} else {
		if (CURR_FILE.lineno < 1) {
			CURR_LINE = CURR_FILE.top->next;
			CURR_FILE.lineno = 1;
		} else {
			CURR_LINE = CURR_FILE.bottom->prev;
			CURR_FILE.lineno = CURR_FILE.num_lines;
		}
	}
	strncpy(pattern, "'^([+]{3}|[-]{3}) '", 100);
	filter_all(pattern);

	if (orig_lineno == CURR_FILE.lineno) {
		if (TEXT_LINE(CURR_LINE->next) && !HIDDEN_LINE(ri_, CURR_LINE->next)) {
			CURR_LINE = CURR_LINE->next;
			CURR_FILE.lineno++;
		}
	} else {
		if (TEXT_LINE(CURR_LINE->prev) && HIDDEN_LINE(ri_, CURR_LINE->prev)) {
			lx = CURR_LINE;
			prev_lp (ri_, &lx, &cnt);
			if (TEXT_LINE(lx) && !HIDDEN_LINE(ri_, lx)) {
				CURR_LINE = lx;
				CURR_FILE.lineno -= cnt;
			} else {
				CURR_LINE = CURR_LINE->next;
				CURR_FILE.lineno++;
			}
		}
	}
	if (!TEXT_LINE(CURR_LINE) || HIDDEN_LINE(ri_, CURR_LINE)) {
		tracemsg("cannot select diff header");
		return (2);
	}

	/* the type of diff
	*/
	strncpy(pattern, "^[+]{3} ([^[:blank:]]+)\t", 100);
	if (regexp_match(CURR_LINE->buff, pattern, 1, xmatch) == 0) {
		*diff_type = 1;
	} else {
		tracemsg("cannot match target file");
		return (3);
	}

	/* open target file of diff (or switch to)
	*/
	if (try_add_file(xmatch)) {
		tracemsg("cannot load target file [%s]", xmatch);
		return (4);
	}
	*ring_tg = cnf.ring_curr;	/* save target file ring index */
	cnf.ring_curr = ri_;		/* back to diff file */

	return (0);
}

/*
* select_diff_section - selects prev header line in (current) diff file,
*	gets target name from section header and tries to open target file
*	returns 0 if passed or nonzero if failed
*/
static int
select_diff_section (int *diff_type, int *ring_tg)
{
	int ri_ = cnf.ring_curr;
	LINE *lx = CURR_LINE;
	int lineno = CURR_FILE.lineno;
	int found = 0;
	regex_t reg1;
	const char *pattern;
	char xmatch[TAGSTR_SIZE];

	*diff_type = -1;
	*ring_tg = -1;

	/* check if this is a diff file
	*/
	if ((strncmp(CURR_FILE.fname, "*diff*", 6) != 0))
	{
		tracemsg("not a *diff* buffer");
		return (1);
	}
	if (CURR_FILE.num_lines < 5) {
		tracemsg("empty diff");
		return (1);
	}

	/* get appropriate section header (go upwards)
	*/
	pattern = "^([+]{3}|[-]{3}) ";
	if (regcomp (&reg1, pattern, REGCOMP_OPTION)) {
		ERRLOG(0xE084);
		return (1); /* internal regcomp failed */
	}
	while (!found) {
		if (regexec(&reg1, lx->buff, 0, NULL, 0) == 0) {
			found = 1;
		} else if (TEXT_LINE(lx->prev)) {
			lx = lx->prev;
			lineno--;
		} else {
			break;
		}
	}
	regfree (&reg1);
	if (!found) {
		tracemsg("cannot select diff header, not found");
		return (2);
	}

	/* determine diff type
	*/
	if (strncmp(lx->buff, "+++ ", 4) == 0) {
		*diff_type = 1;
	} else if (strncmp(lx->buff, "--- ", 4) == 0) {
		if ((TEXT_LINE(lx->next)) && (strncmp(lx->next->buff, "+++ ", 4) == 0)) {
			lx = lx->next;
			lineno++;
			*diff_type = 1;
		}
	}

	/* the type of diff
	*/
	pattern = "^[+]{3} ([^[:blank:]]+)\t";
	if (regexp_match(lx->buff, pattern, 1, xmatch) == 0) {
		*diff_type = 1;
	} else {
		tracemsg("cannot match target file");
		return (3);
	}

	/* open target file of diff (or switch to)
	*/
	if (try_add_file(xmatch)) {
		tracemsg("cannot load target file [%s]", xmatch);
		return (4);
	}
	*ring_tg = cnf.ring_curr;	/* save target file ring index */
	cnf.ring_curr = ri_;		/* back to diff file */

	return (0);
}

/*
* tool for unified type diff
* range: (number) | (begin,length)
*/
static int
unhide_udiff_range (int ri, const char *range, int *target_line, int *target_length)
{
	int lineno = 0, length = -1;
	int *ip = &lineno;
	int pos = 0;
	LINE *lp = NULL;

	/* length > 0 is mandatory */
	for (pos=0; range[pos] != '\0'; pos++) {
		if (range[pos] >= '0' && range[pos] <= '9') {
			*ip = *ip * 10 + range[pos] - '0';
		} else if (range[pos] == ',' && length == -1) {
			length = 0;
			ip = &length;
		} else {
			break;
		}
	}
	/* range parsed: begin %d length %d", lineno, length */

	/* save values also for caller */
	*target_line = lineno;
	*target_length = length;

	if ((lp = lll_goto_lineno (ri, lineno)) == NULL) {
		return (1);
	}
	cnf.fdata[ri].curr_line = lp;
	cnf.fdata[ri].lineno = lineno;

	while (length > 0 && TEXT_LINE(lp)) {
		lp->lflag &= ~FMASK(cnf.fdata[ri].flevel);	/* unhide */
		lp = lp->next;
		length--;
	}

	return (0);
}

/*
* tool for unified type diff
* used to mark changed/inserted lines
*/
static void
mark_diff_line (int ri, int lineno)
{
	LINE *lp = NULL;

	if ((lp = lll_goto_lineno (ri, lineno)) != NULL) {
		lp->lflag |= LSTAT_TAG1;	/* tag */
	}

	return;
}

/*
** process_diff - process the unified diff output,
**	make preparation to review additions and changes of target files
*/
int
process_diff (void)
{
	int ret=0, diff_type=0, ring_tg=0;
	int tgline = 0, tglen = 0;
	LINE *lx_ = NULL;
	int lineno_ = 0;
	int ri_ = cnf.ring_curr;
	regex_t reg1;
	const char *patt1;
	regmatch_t pmatch[10];	/* match and sub match */
	char xmatch[TAGSTR_SIZE];
	int iy, ix;

	/* @@ -RANGE0 +RANGE1 @@ */
	patt1 = "^@@ \\-[0-9,]+ \\+([0-9,]+) @@";

	/* some processing moved to another function
	*/
	ret = set_diff_section (&diff_type, &ring_tg);
	if (ret) {
		if (CURR_FILE.num_lines < 5) {
			quit_file();
			/* no change */
			update_focus(CENTER_FOCUSLINE, cnf.ring_curr);
			return (0);
		}
		tracemsg("processing diff failed", ret); /* given up */
		return (ret);
	}
	lx_ = CURR_LINE;
	lineno_ = CURR_FILE.lineno;

	/* process down the diff section (hidden lines)
	*/
	if (diff_type == 1) {	/* unified diff */
		/* range: (number) | (begin,length) */

		if (regcomp (&reg1, patt1, REGCOMP_OPTION)) {
			ERRLOG(0xE083);
			return (1); /* internal regcomp failed */
		}

		tgline = tglen = 0;
		while (TEXT_LINE(lx_->next) && HIDDEN_LINE(ri_, lx_->next)) {
			lx_ = lx_->next;
			lineno_++;

			if (lx_->llen > 10 && lx_->buff[0] == '@' && lx_->buff[1] == '@')
			{
				/* unhide diff change */
				lx_->lflag &= ~FMASK(cnf.fdata[ri_].flevel);

				if (!regexec(&reg1, lx_->buff, 10, pmatch, 0) &&
					pmatch[1].rm_so >= 0 && pmatch[1].rm_so < pmatch[1].rm_eo)
				{
					iy = 0;
					for (ix = pmatch[1].rm_so; ix < pmatch[1].rm_eo; ix++) {
						xmatch[iy++] = lx_->buff[ix];
						if (iy >= TAGSTR_SIZE-1)
							break;
					}
					xmatch[iy] = '\0';
					unhide_udiff_range(ring_tg, xmatch, &tgline, &tglen);
				}
			}
			else if (tglen > 0) {
				if (lx_->buff[0] == '+') {
					mark_diff_line(ring_tg, tgline);
					tgline++;
					tglen--;
				} else if (lx_->buff[0] == ' ') {
					tgline++;
					tglen--;
				}
			}
		}
		regfree (&reg1);

	} else {
		tracemsg("not supported diff type, unified diff required");
		return (1);
	}

	/* call once for the target */
	cnf.ring_curr = ring_tg;
	filter_more("function");
	update_focus(CENTER_FOCUSLINE, cnf.ring_curr);
	cnf.ring_curr = ri_;
	update_focus(FOCUS_ON_2ND_LINE, ri_);

	/* and colorize the *diff* also */
	bm_set("9");
	filter_more("/^@@/");
	color_tag("/^@@/");

	return (0);
}

/*
** internal_hgdiff - run hg diff on current file and process the outcome
*/
int internal_hgdiff (void)
{
	int ret;
	char ext_cmd[25+FNAMESIZE];

	if ((ret = filter_all("function")))
		return (ret);

	snprintf(ext_cmd, sizeof(ext_cmd)-1, "hg diff -p --noprefix %s", CURR_FILE.fname);
	if ((ret = vcstool(ext_cmd)))
		return (ret);

	if ((ret = is_special("diff")))
		return (ret);

	ret = process_diff();

	return (0);
}
