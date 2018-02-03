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
#include <sys/types.h>		/* stat */
#include <sys/stat.h>
#include <unistd.h>		/* write, fcntl */
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <syslog.h>
#include "main.h"
#include "proto.h"

/* global config */
extern CONFIG cnf;
extern const char long_version_string[];

#define ONE_SIZE  1024

typedef struct lsdir_item_tag {
	int _type[2];		/* lstat type and stat type */
	char entry[FNAMESIZE];	/* original dir entry */
	char one_line[ONE_SIZE];
} LSDIRITEM;

/* local proto */
static int one_lsdir_line (const char *fullname, struct stat *test, LSDIRITEM *item);
static int lsdir_item_cmp (const void *p1, const void *p2);
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
	return (0);
}

/*
** version - show the version string
*/
int
version (void)
{
	tracemsg ("%s", long_version_string);

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
	if (regexp_match(dataline, "([0-9]+)", 1, xmatch) == 0) {
		rx = atoi(xmatch);
	} else if (regexp_match(dataline, "[[:blank:]]+bookmark[[:blank:]]+([0-9]+):", 1, xmatch) == 0) {
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
			res = csere(&word, &wl, 0, 0, dn, dl);
		} else {
			res = csere(&word, &wl, 0, 0, dn, dl);
			res |= csere(&word, &wl, wl, 0, "/", 1);
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
one_lsdir_line (const char *fullname, struct stat *test, LSDIRITEM *item)
{
	int perm=0;
	long long size;
	time_t t;
	struct tm *tm_p;
	char tbuff[20];
	int nlink, uid, gid;
	int got;
	char linkname[FNAMESIZE];
	linkname[0] = '\0';

/* yet another magic format string - the directory listing */
#define MAGICFORMAT  "%04o %4d %6d %6d %12lld %s"

	if (cnf.lsdir_opts & LSDIR_L) {
		perm = test->st_mode & 07777;
		nlink = test->st_nlink;
		uid = test->st_uid;
		gid = test->st_gid;
		size = test->st_size;

		t = test->st_mtime;
		tm_p = localtime(&t);
		strftime(tbuff, 19, "%Y-%m-%d %H:%M", tm_p);
		tbuff[19] = '\0';

		if (item->_type[0] == 'l') {
			if (stat(fullname, test)) {
				/* dead link, the referred file cannot be stat-ed */
				return 1;
			}
			/* st_mode must be correct */
			memset(linkname, 0, sizeof(linkname));
			got = readlink(fullname, linkname, sizeof(linkname)-1);
			if (got < 1) {
				strncpy(linkname, "...\0", 4); /* cannot happen? */
			}
		}

		if (S_ISDIR(test->st_mode)) {
			item->_type[1] = 'd';
			if (item->_type[0] == 'l') {
				snprintf(item->one_line, ONE_SIZE, "l" MAGICFORMAT " %s -> %s/\n",
					perm, nlink, uid, gid, size, tbuff, item->entry, linkname);
			} else {
				snprintf(item->one_line, ONE_SIZE, "d" MAGICFORMAT " %s/\n",
					perm, nlink, uid, gid, size, tbuff, item->entry);
			}
			item->one_line[ONE_SIZE-1] = '\0';
		} else if (S_ISREG(test->st_mode)) {
			item->_type[1] = '-';
			if (item->_type[0] == 'l') {
				snprintf(item->one_line, ONE_SIZE, "l" MAGICFORMAT " %s -> %s\n",
					perm, nlink, uid, gid, size, tbuff, item->entry, linkname);
			} else {
				snprintf(item->one_line, ONE_SIZE, "-" MAGICFORMAT " %s\n",
					perm, nlink, uid, gid, size, tbuff, item->entry);
			}
			item->one_line[ONE_SIZE-1] = '\0';
		} else {
			snprintf(item->one_line, ONE_SIZE, "?" MAGICFORMAT " ???\n",
				perm, nlink, uid, gid, size, tbuff);
			item->one_line[ONE_SIZE-1] = '\0';
			/* char special device or block device or whatever */
			return 2;
		}

	} else {
		if (item->_type[0] == 'l') {
			if (stat(fullname, test)) {
				return 1;
			}
			/* st_mode must be correct */
		}
		if (S_ISDIR(test->st_mode)) {
			item->_type[1] = 'd';
			snprintf(item->one_line, ONE_SIZE, "%s/\n", item->entry);
		} else {
			item->_type[1] = '-';
			snprintf(item->one_line, ONE_SIZE, "%s\n", item->entry);
		}
		item->one_line[ONE_SIZE-1] = '\0';
	}

	return 0;
}

/*
*/
static int
lsdir_item_cmp (const void *p1, const void *p2)
{
	const LSDIRITEM *item1=p1;
	const LSDIRITEM *item2=p2;
	/* check the stat() types */
	if (item1->_type[1] == item2->_type[1]) {
		return (strncmp(item1->entry, item2->entry, FNAMESIZE));
	} else if (item1->_type[1] == 'd') {
		return -1;
	} else {
		return 1;
	}
}

/* lsdir is the worker behind lsdir_cmd and general_parser
*/
int
lsdir (const char *dirpath)
{
	DIR *dir = NULL;
	struct dirent *de = NULL;
	char fullpath[FNAMESIZE*2];
	int length = 0;
	struct stat test;
	char one_line[ONE_SIZE];
	LSDIRITEM *items=NULL, *last_item=NULL;
	int ii=0, item_count=0;
	LINE *lp=NULL, *lx=NULL;
	int ret = 0;

	memset(fullpath, 0, sizeof(fullpath));

	clean_buffer();
	/* additional flags */
	CURR_FILE.fflag |= FSTAT_SPECW;
	/* disable inline editing, adding lines */
	CURR_FILE.fflag |= (FSTAT_NOEDIT | FSTAT_NOADDLIN);

	/* header
	*/
	if (cnf.lsdir_opts) {
		length = 9;
		strncpy(one_line, "$ lsdir -", length+1);
		if (cnf.lsdir_opts & LSDIR_L)
			one_line[length++] = 'l';
		if (cnf.lsdir_opts & LSDIR_A)
			one_line[length++] = 'a';
		one_line[length++] = ' ';
		one_line[length] = '\0';
	} else {
		length = 8;
		strncpy(one_line, "$ lsdir ", length+1);
	}
	snprintf(one_line+length, ONE_SIZE-length-1, "%s\n", dirpath);
	if ((lp = insert_line_before (CURR_FILE.bottom, one_line)) != NULL) {
		CURR_FILE.num_lines++;
	} else {
		return 1;
	}

	/* keep length and dirpath in fullpath */
	length = strlen(dirpath);
	if (length >= FNAMESIZE-1) {
		return 1;
	}
	strncpy(fullpath, dirpath, FNAMESIZE-1);
	fullpath[FNAMESIZE-1] = '\0';

	dir = opendir (dirpath);
	if (dir == NULL) {
		tracemsg("Cannot read %s directory", dirpath);
		return 3;
	}

	/* dir items
	*/
	while ((de = readdir (dir)) != NULL) {
		if ((de->d_name[0] == '.') && ((de->d_name[1] == '\0') || ((cnf.lsdir_opts & LSDIR_A) == 0)))
		{	/* hidden items not required */
			continue;
		}

		item_count++;
		items = (LSDIRITEM *) REALLOC((void *)items, sizeof(LSDIRITEM)*item_count);
		if (items == NULL) {
			break;
		}

		last_item = &items[item_count-1];
		last_item->_type[0] = last_item->_type[1] = '\0';
		strncpy(fullpath+length, de->d_name, FNAMESIZE);
		fullpath[2*FNAMESIZE-1] = '\0';
		strncpy(last_item->entry, de->d_name, FNAMESIZE); /* d_name must fit into entry */
		last_item->entry[FNAMESIZE-1] = '\0';

		if (lstat(fullpath, &test)) {
			item_count--;
			PD_LOG(LOG_DEBUG, "lstat(%s) failed [%s], item_count decr --> (%d)", fullpath, strerror(errno), item_count);
			continue;
		} else if (S_ISLNK(test.st_mode)) {
			last_item->_type[0] = 'l';
		}

		if (one_lsdir_line (fullpath, &test, last_item)) {
			item_count--;
			PD_LOG(LOG_DEBUG, "entry [%s] item_count decr --> (%d)", last_item->entry, item_count);
			continue;
		}
	}
	closedir (dir);

	/* qsort() and append_line() calls
	*/
	if (items != NULL) {
		qsort (items, item_count, sizeof(LSDIRITEM), lsdir_item_cmp);

		for (ii=0; ii<item_count; ii++) {
			lx = append_line (lp, items[ii].one_line);
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
	/* ungreedy match */
	strncpy(patt1, "^(.+?)([:-])([0-9]+)\\2", 100);

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
			strncpy (strz, &lp->buff[beg], len);
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
	if ((CURR_LINE->llen == 0) || (CURR_LINE->buff[0] == '$')) {
		return;
	}

	als = ALLOCSIZE(CURR_LINE->llen+10);
	dataline = (char *) MALLOC (als);
	if (dataline == NULL) {
		return;
	}
	strncpy(dataline, CURR_LINE->buff, CURR_LINE->llen);
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
** is_special - show buffer type or set buffer type,
**	regular file types (c/cpp/c++, tcl/tk, perl, python, bash/shell, text) and
**	special buffers (sh, ls, make, find, diff, configured VCS tools)
**	regular filenames can be changed to equivalent name of same inode
*/
int
is_special (const char *special)
{
	unsigned slen=0, i=0;
	char tfname[FNAMESIZE];
	int ri = cnf.ring_curr;
	struct stat test;

	/* prepare tfname for comparison as well as for target filename */
	slen=0;
	if (special != NULL && special[0] != '\0') {
		if ((stat(special, &test) == 0) && (CURR_FILE.stat.st_ino == test.st_ino)) {
			/* give an equivalent name to the regular file */
			slen = strlen(special);
			if (slen > FNAMESIZE-1) {
				tracemsg("filename is very long");
				return (1);
			}

			/* change CURR_FILE.fname */
			strncpy(CURR_FILE.fname, special, FNAMESIZE);
			CURR_FILE.fname[FNAMESIZE-1] = '\0';

			/* update */
			CURR_FILE.stat = test;

			return (0);	/*done*/

		} else {
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
				case TCL_FILETYPE:
					tracemsg("Tcl/Tk");
					break;
				case PYTHON_FILETYPE:
					tracemsg("Python");
					break;
				case SHELL_FILETYPE:
					tracemsg("Bash/Shell");
					break;
				case TEXT_FILETYPE:
					tracemsg("Text");
					break;
				case UNKNOWN_FILETYPE:
				default:
					tracemsg("Unknown");
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
	} else if ((strncmp(tfname, "*tcl*", slen) == 0) || (strncmp(tfname, "*tk*", slen) == 0)) {
		CURR_FILE.ftype = TCL_FILETYPE;
	} else if (strncmp(tfname, "*python*", slen) == 0) {
		CURR_FILE.ftype = PYTHON_FILETYPE;
	} else if ((strncmp(tfname, "*bash*", slen) == 0) || (strncmp(tfname, "*shell*", slen) == 0)) {
		CURR_FILE.ftype = SHELL_FILETYPE;
	} else if (strncmp(tfname, "*text*", slen) == 0) {
		CURR_FILE.ftype = TEXT_FILETYPE;
	} else if (strncmp(tfname, "*unknown*", slen) == 0) {
		CURR_FILE.ftype = UNKNOWN_FILETYPE;

	/* change basename, according to tfname, only for special buffers */
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
			/* tfname may already exist in the ring, like *diff* or *sh*, multiple times? */

			CURR_FILE.fflag |= (FSTAT_SPECW | FSTAT_SCRATCH);
			CURR_FILE.fflag |= (FSTAT_NOEDIT | FSTAT_NOADDLIN);

			CURR_FILE.basename[0] = '\0';
			CURR_FILE.dirname[0] = '\0';

			strncpy (CURR_FILE.fname, tfname, FNAMESIZE);
			CURR_FILE.fname[FNAMESIZE-1] = '\0';

			memset (&cnf.fdata[ri].stat, 0, sizeof(struct stat));
			cnf.fdata[ri].ftype = UNKNOWN_FILETYPE;

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
		if (ix > 0 && ix+slen < sizeof(cnf.find_opts)) {
			strncpy(cnf.find_opts, fpath, ix+1);
			strncat(cnf.find_opts, suffix, slen);
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
	slen = iy;

	p = strstr(cnf.find_opts, "-type f ");
	q = strstr(cnf.find_opts, " -exec egrep");
	ix = iy = 0;
	if (p!=NULL && q!=NULL && p<q) {
		ix = p - cnf.find_opts + 8;
		iy = strlen(q);
	} else {
		tracemsg("cannot change find_opts setting, type and/or exec missing");
		PD_LOG(LOG_ERR, "find_opts does not contain '-type f' and/or '-exec egrep' patterns");
		return (1);
	}
	strncpy(suffix, q, iy);
	suffix[iy] = '\0';

	if (slen == 0) {
		cnf.find_opts[ix] = '\0';
		strncat(cnf.find_opts+ix, suffix, iy);
	} else if (ix +1 +slen +1 +iy < sizeof(cnf.find_opts)) {
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
	int ring_i;
	int origin;
	static int find_first=0;
	const char *fname_once=NULL, *fname_other=NULL, *fname_try=NULL;

	/* jump from spec.buffer to file by origin */
	if (strncmp(CURR_FILE.fname, "*find*", 6) == 0) {
		origin = CURR_FILE.origin;
		if (origin >= 0 && origin < RINGSIZE && (cnf.fdata[origin].fflag & FSTAT_OPEN)) {
			PD_LOG(LOG_DEBUG, "*find* : spec.buffer %d --> file %d",
				cnf.ring_curr, origin);
			cnf.ring_curr = origin;
			find_first = 1;
		}
	} else if (strncmp(CURR_FILE.fname, "*make*", 6) == 0) {
		origin = CURR_FILE.origin;
		if (origin >= 0 && origin < RINGSIZE && (cnf.fdata[origin].fflag & FSTAT_OPEN)) {
			PD_LOG(LOG_DEBUG, "*make* : spec.buffer %d --> file %d",
				cnf.ring_curr, origin);
			cnf.ring_curr = origin;
			find_first = 0;
		}
	} else if (strncmp(CURR_FILE.fname, "*sh*", 4) == 0) {
		origin = CURR_FILE.origin;
		if (origin >= 0 && origin < RINGSIZE && (cnf.fdata[origin].fflag & FSTAT_OPEN)) {
			PD_LOG(LOG_DEBUG, "*sh* : spec.buffer %d --> file %d",
				cnf.ring_curr, origin);
			cnf.ring_curr = origin;
			find_first = -1;
		}

	/* jump back to spec.buffer by checking them in order */
	} else {
		origin = cnf.ring_curr;
		if (find_first == 1) {
			fname_once="*find*";
			fname_other="*make*";
			fname_try="*sh*";
		} else if (find_first == 0) {
			fname_once="*make*";
			fname_other="*find*";
			fname_try="*sh*";
		} else {
			fname_once="*sh*";
			fname_other="*find*";
			fname_try="*make*";
		}

		ring_i = query_scratch_fname (fname_once);
		if (ring_i == -1) {
			ring_i = query_scratch_fname (fname_other);
			if (ring_i == -1) {
				ring_i = query_scratch_fname (fname_try);
			}
		}

		if (ring_i != -1) {
			/* in the ring */
			PD_LOG(LOG_DEBUG, "%s %s %s : from file %d --> spec.buffer %d",
				fname_once, fname_other, fname_try, origin, ring_i);
			cnf.ring_curr = ring_i;
			CURR_FILE.origin = origin;
		} else {
			/* failed */
			tracemsg("jump back to special buffer failed");
		}
	}

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

	/* internal function only with GSTAT_AUTOTITLE */
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
		strncpy (xtbuf+blen, xtitle, 100);
		blen += 100;
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
		PD_LOG(LOG_ERR, "fcntl F_SETFL failed: %s", strerror(errno));
		return (-1);
	}
	wlen = write(1, (void *)xtbuf, blen);
	if (wlen != blen) {
		PD_LOG(LOG_ERR, "write failed: (%d!=%d) (%s)", wlen, blen, strerror(errno));
	}

	return (wlen != blen);
}

/*
** rotate_palette - change color palette setting in cycle
*/
int
rotate_palette (void)
{
	if (cnf.palette < PALETTE_MAX)
		cnf.palette++;
	else
		cnf.palette = 0;

	init_colors (cnf.palette);

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
	int ris_ = cnf.ring_size;
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
		tracemsg("empty diff");
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
		tracemsg("cannot load target file");
		PD_LOG(LOG_DEBUG, "cannot load target file [%s], given up", xmatch);
		return (4);
	}
	if (ris_ < cnf.ring_size) {
		/* added buffer is new in the ring */
		filter_less("");
	}
	*ring_tg = cnf.ring_curr;	/* save target file ring index */
	cnf.ring_curr = ri_;		/* back to diff file */

	/* PD_LOG(LOG_DEBUG, "(type %d) done, target fname %s", *diff_type, xmatch); */
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
	int ris_ = cnf.ring_size;
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
		tracemsg("not a (supported) diff");
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
		tracemsg("cannot load target file");
		PD_LOG(LOG_DEBUG, "cannot load target file [%s], given up", xmatch);
		return (4);
	}
	if (ris_ < cnf.ring_size) {
		/* added buffer is new in the ring */
		filter_less("");
	}
	*ring_tg = cnf.ring_curr;	/* save target file ring index */
	cnf.ring_curr = ri_;		/* back to diff file */

	/* PD_LOG(LOG_DEBUG, "(type %d) done, target fname %s", *diff_type, xmatch); */
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

	ret = set_diff_section (&diff_type, &ring_tg);
	if (ret) {
		PD_LOG(LOG_DEBUG, "incomplete processing, given up");
		if (ret == 4)
			filter_more(patt1);
		return (ret);
	}
	lx_ = CURR_LINE;
	lineno_ = CURR_FILE.lineno;

	/* process down the diff section (hidden lines)
	*/
	if (diff_type == 1) {	/* unified diff */
		/* range: (number) | (begin,length) */

		if (regcomp (&reg1, patt1, REGCOMP_OPTION)) {
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
		PD_LOG(LOG_DEBUG, "not supported diff type");
		tracemsg("unified diff type required");
		return (1);
	}

	/* call once for the target */
	cnf.ring_curr = ring_tg;
	filter_more("function");
	update_focus(CENTER_FOCUSLINE, cnf.ring_curr);
	cnf.ring_curr = ri_;

	return (0);
}
