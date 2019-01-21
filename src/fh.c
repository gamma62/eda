/*
* fh.c
* file handling, from basic open/save functions upto change watch, smart reload, pipe support
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
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>		/* open, stat, read, write, close */
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>		/* ctime */
#include <syslog.h>
#include "main.h"
#include "proto.h"

/* global config */
extern CONFIG cnf;

/* local proto */
static int next_ri (void);
static int abbrev_filename (char *gname);
static int check_dirname (const char *gname);
static int getxline_filter(char *getbuff);
static int parse_diff_header (const char *ptr, int ra[5]);
static int backup_file (const char *fname, char *backup_name);

/*
* user interface functions call tracemsg() if error occured
* and details go to ERROR log
*/

/*
* Return the next free ring index, or -1 if isn't free ring index.
*/
static int
next_ri (void)
{
	int ri = cnf.ring_curr;

	do {
		if ((cnf.fdata[ri].fflag & FSTAT_OPEN) == 0)
			break;
		ri = (ri<RINGSIZE-1) ?  ri+1 : 0;
	} while (ri != cnf.ring_curr);

	if (cnf.fdata[ri].fflag & FSTAT_OPEN) {
		/* no free ring index */
		ri = (-1);
	}

	return (ri);
}

/*
** next_file - switch to next buffer in the ring, skip hidden unless others closed
*/
int
next_file (void)
{
	int ri=0;

	if (cnf.ring_size <= 0) {
		return (0);	/* accepted failure */
	}

	ri = cnf.ring_curr;
	do {
		ri = (ri<RINGSIZE-1) ?  ri+1 : 0;
		if ((cnf.fdata[ri].fflag & FSTAT_OPEN) && !(cnf.fdata[ri].fflag & FSTAT_HIDDEN)) {
			break;
		}
	} while (ri != cnf.ring_curr);

	if ((ri == cnf.ring_curr) && !(cnf.fdata[ri].fflag & FSTAT_OPEN)) {
		/* try again but allow hidden buffer also */
		do {
			ri = (ri<RINGSIZE-1) ?  ri+1 : 0;
			if (cnf.fdata[ri].fflag & FSTAT_OPEN) {
				cnf.fdata[ri].fflag &= ~FSTAT_HIDDEN;	/* force unhide here */
				break;
			}
		} while (ri != cnf.ring_curr);
	}

	if (!(cnf.fdata[ri].fflag & FSTAT_OPEN)) {
		ERRLOG(0xE077); /* no open buffer in ring */
		CURR_FILE.fflag |= FSTAT_CMD;
		return(1);
	}

	cnf.ring_curr = ri;
	if (cnf.ring_size == 1)
		CURR_FILE.fflag |= FSTAT_CMD;

	return (0);
}

/*
** prev_file - switch to previous buffer in the ring, skip hidden unless others closed
*/
int
prev_file (void)
{
	int ri=0;

	if (cnf.ring_size <= 0) {
		return (0);	/* accepted failure */
	}

	ri = cnf.ring_curr;
	do {
		ri = (ri>0) ? ri-1 : RINGSIZE-1;
		if ((cnf.fdata[ri].fflag & FSTAT_OPEN) && !(cnf.fdata[ri].fflag & FSTAT_HIDDEN)) {
			break;
		}
	} while (ri != cnf.ring_curr);

	if ((ri == cnf.ring_curr) && !(cnf.fdata[ri].fflag & FSTAT_OPEN)) {
		/* try again but allow hidden buffer also */
		do {
			ri = (ri>0) ? ri-1 : RINGSIZE-1;
			if (cnf.fdata[ri].fflag & FSTAT_OPEN) {
				cnf.fdata[ri].fflag &= ~FSTAT_HIDDEN;	/* force unhide here */
				break;
			}
		} while (ri != cnf.ring_curr);
	}

	if (!(cnf.fdata[ri].fflag & FSTAT_OPEN)) {
		ERRLOG(0xE076); /* no open buffer in ring */
		CURR_FILE.fflag |= FSTAT_CMD;
		return(1);
	}

	cnf.ring_curr = ri;
	if (cnf.ring_size == 1)
		CURR_FILE.fflag |= FSTAT_CMD;

	return (0);
}

/*
* query_inode - check the ring if inode used by open (non-special) file
* returns valid ring index if found, -1 otherwise
*/
int
query_inode (ino_t inode)
{
	int ri=0, ret = -1;

	for (ri=0; ri<RINGSIZE; ri++) {
		if ((cnf.fdata[ri].fflag & FSTAT_OPEN) &&
		    !(cnf.fdata[ri].fflag & FSTAT_SCRATCH) &&
		    (cnf.fdata[ri].stat.st_ino == inode)) {
			ret = ri;
			break;
		}
	}

	return (ret);
}

/*
* try_add_file - try to open file by going back one directory level from given path,
* if a regular file found then call add_file();
* simple relative path assumed, like src/main.c or Makefile or test/Makefile
*/
int
try_add_file (const char *path)
{
	int len, i, slashes=0, size=0, split, ri, ret=0;
	struct stat test;
	char *testfname = NULL;

	len = strlen(path);
	if (len < 2 || path[0] == '/' || path[0] == '~')
		return (1);

	for (i=0; i < len; i++) {
		if (path[i] == '/')
			slashes++;
	}

	/* duplicate input with prefix pieces ("../") for backstep */
	if (csere (&testfname, &size, 0, 0, &path[0], 3*slashes + len+1)) {
		FREE(testfname); testfname = NULL;
		return (1);
	}
	testfname[0] = '\0';
	for (i=0; i < slashes; i++) {
		strncat(testfname, "../", 3);
	}
	strncat(testfname, &path[0], (unsigned)len);

	split = 3*slashes;
	while (split >= 0) {
		if (stat(&testfname[split], &test) == 0) {
			if (S_ISREG(test.st_mode)) {
				break;
			}
		}
		split -= 3;
	}
	if (split < 0)
		return (1);

	ri = query_inode (test.st_ino);
	if (ri != -1) {
		/* inode found in the ring, switch to buffer -- do not change fname */
		cnf.ring_curr = ri;
		CURR_FILE.fflag &= ~FSTAT_SCRATCH;
		ret = 0;
	} else {
		ret = add_file( &testfname[split] );
	}

	FREE(testfname); testfname = NULL;
	return (ret);
}

/*
** add_file - add file to new editor buffer or switch to the already opened file
*/
int
add_file (const char *fname)
{
	/* tracemsg about result at this level */
	int ret=0;
	struct stat test;
	char gname[FNAMESIZE];
	int ri;

	if (fname[0]=='\0') {
		return (next_file());
	}

	strncpy(gname, fname, sizeof(gname));
	gname[sizeof(gname)-1] = '\0';
	if (glob_tilde_expansion(gname, sizeof(gname)))
		return (1);

	ret = stat(gname, &test);
	if (ret == 0) {
		/* test: regular file ? */
		if (S_ISREG(test.st_mode)) {
			ri = query_inode (test.st_ino);
			if (ri != -1) {
				/* inode found in the ring, switch to buffer */
				if (strncmp(gname, cnf.fdata[ri].fname, sizeof(gname))) {
					// same inode, but not the same name
					if (cnf.fdata[ri].fname[0] != '~')
						tracemsg ("[%s] opened here as [%s]", cnf.fdata[ri].fname, gname);
					/**/
					strncpy (cnf.fdata[ri].fpath, gname, FNAMESIZE);
					cnf.fdata[ri].fpath[FNAMESIZE-1] = '\0';
					strncpy (cnf.fdata[ri].fname, gname, FNAMESIZE);
					cnf.fdata[ri].fname[FNAMESIZE-1] = '\0';
					abbrev_filename(cnf.fdata[ri].fname);
					cnf.fdata[ri].ftype = fname_ext(cnf.fdata[ri].fname);
					/* ri not changed but the content, update titles and headers */
					cnf.gstat |= GSTAT_REDRAW;
				}
				cnf.ring_curr = ri;
				CURR_FILE.stat = test;
				CURR_FILE.fflag &= ~FSTAT_SCRATCH;
				ret = 0;
			} else {
				/* inode not found, open it in new buffer */
				ret = read_file(gname, &test);
			}
		} else {
			tracemsg ("[%s]: not a regular file.", gname);
			ret = 1;
		}
	} else {
		int saved_errno = errno;
		if ((errno == ENOENT) && (check_dirname(gname) == 0)) {
			/* scratch file, but seems to be Ok */
			ret = scratch_buffer(gname);
			/* place cursor into text area */
			CURR_FILE.fflag &= ~FSTAT_CMD;
		} else {
			tracemsg ("[%s]: %s.", gname, strerror(saved_errno));
		}
	}

	return (ret);
}

/*
** quit_file - quit file if there are no pending changes, drop scratch buffers and read-only buffers anyway,
**	running background process in this buffer will be stopped
*/
int
quit_file (void)
{
	int ret=0;

	/* don't drop if changed, except scratch/special/read-only buffer */
	if ((CURR_FILE.fflag & FSTAT_CHANGE) &&
	!(CURR_FILE.fflag & (FSTAT_SCRATCH | FSTAT_SPECW | FSTAT_RO))) {
		tracemsg ("file changed, use save or qquit.");
		/* select cmdline */
		CURR_FILE.fflag |= FSTAT_CMD;
	} else {
		ret = drop_file ();
	}

	return (ret);
}

/*
** quit_others - quit all other unchanged files or scratch buffers or read-only buffers,
**	but do not close buffer with running background process
*/
int
quit_others (void)
{
	int ri, current;
	int ret=0;

	current = cnf.ring_curr;
	for (ri=0; ri < RINGSIZE && ret==0; ri++) {
		if (ri == current)
			continue;
		if ((cnf.fdata[ri].fflag & FSTAT_OPEN) == 0)
			continue;
		if (cnf.fdata[ri].pipe_output != 0)
			continue;
		cnf.ring_curr = ri;
		if (!(CURR_FILE.fflag & FSTAT_CHANGE) ||
		(CURR_FILE.fflag & (FSTAT_SCRATCH | FSTAT_SPECW | FSTAT_RO))) {
			ret = drop_file ();
		}
	}
	cnf.ring_curr = current;
	cnf.gstat |= GSTAT_UPDNONE;
	if (cnf.ring_size > 1) {
		tracemsg ("quit: %d other remained", cnf.ring_size-1);
	}

	return (ret);
}

/*
** file_file - call save on this file if not scratch or unchanged and quit
*/
int
file_file (void)
{
	int ret=0;
	/* special buffer doesn't need save */
	if (!(CURR_FILE.fflag & FSTAT_SPECW) && (CURR_FILE.fflag & FSTAT_CHANGE)) {
		ret = save_file ("");
	}
	if (ret == 0) {
		cnf.trace = 0; /* do not display the "file saved" message */
		ret = quit_file ();
	}
	return (ret);
}

/*
** quit_all - drop all buffers unconditionally and leave the program
*/
int
quit_all (void)
{
	drop_all();
	/* the ring must be empty, leave() does the rest */
	return (0);
}

/*
** file_all - save all files where necessary and leave the program
*/
int
file_all (void)
{
	int ri;
	for (ri=0; ri < RINGSIZE; ri++) {
		cnf.ring_curr = ri;
		if (CURR_FILE.fflag & FSTAT_OPEN)
			file_file();
	}
	/* the ring must be empty, leave() does the rest */
	return (0);
}

/*
** save_all - save all files where necessary
*/
int
save_all (void)
{
	int ri, current;
	int ret=0;

	current = cnf.ring_curr;
	for (ri=0; ri < RINGSIZE && ret==0; ri++) {
		cnf.ring_curr = ri;
		if ((CURR_FILE.fflag & FSTAT_OPEN) && !(CURR_FILE.fflag & FSTAT_SPECW) && (CURR_FILE.fflag & FSTAT_CHANGE)) {
			ret = save_file ("");
			if (ret == 0)
				cnf.trace = 0; /* do not display the "file saved" message */
		}
	}
	cnf.ring_curr = current;
	cnf.gstat |= GSTAT_UPDNONE;

	return(ret);
}

/*
** hide_file - hide regular file buffer, unhide any
*/
int
hide_file (void)
{
	if (CURR_FILE.fflag & FSTAT_HIDDEN) {
		/* unhide */
		CURR_FILE.fflag &= ~FSTAT_HIDDEN;
		cnf.gstat |= GSTAT_UPDNONE;
	} else {
		if ( !(CURR_FILE.fflag & (FSTAT_SPECW | FSTAT_SCRATCH)) ) {
			/* regular file */
			CURR_FILE.fflag |= FSTAT_HIDDEN;
			next_file();
		}
	}

	return 0;
}

/*
* append_line - append one line with the given extbuff after the given lp to the chain,
* return NULL on malloc failure (and nothing changed)
*/
LINE *
append_line (LINE *lp, const char *extbuff)
{
	LINE *lx=NULL;
	int elen=0;

	elen = strlen(extbuff);
	if ((lx = lll_add(lp)) != NULL) {
		if (milbuff (lx, 0, 0, extbuff, elen)) {
			return (NULL);
		}
		if ((elen > 0) && (extbuff[elen-1] != '\n')) {
			lx->lflag |= LSTAT_TRUNC;
		}
	}

	return (lx);
}

/*
* insert_line_before - insert one line with the given extbuff before the given lp to the chain,
* return NULL on malloc failure (and nothing changed)
*/
LINE *
insert_line_before (LINE *lp, const char *extbuff)
{
	LINE *lx=NULL;
	int elen=0;

	elen = strlen(extbuff);
	if ((lx = lll_add_before(lp)) != NULL) {
		if (milbuff (lx, 0, 0, extbuff, elen)) {
			return (NULL);
		}
		if ((elen > 0) && (extbuff[elen-1] != '\n')) {
			lx->lflag |= LSTAT_TRUNC;
		}
	}

	return (lx);
}

/*
* abbrev_filename - abbreviate filename if it is absolute path,
* with PWD -> relative path, with HOME -> tilde
* return 0 if Ok
*/
static int
abbrev_filename (char *gname)
{
	unsigned i, j, glen;

	if (gname[0] != '/') {
		return 0;
	}
	glen = strlen(gname);

	if (cnf.l1_pwd > 0 && glen > cnf.l1_pwd && strncmp(gname, cnf._pwd, cnf.l1_pwd) == 0) {
		for (i=0, j=cnf.l1_pwd+1; gname[j] != '\0'; j++) {
			gname[i++] = gname[j];
		}
		gname[i] = '\0';
		// with PWD

	} else if (cnf.l2_altpwd > 0 && glen > cnf.l2_altpwd && strncmp(gname, cnf._altpwd, cnf.l2_altpwd) == 0) {
		for (i=0, j=cnf.l2_altpwd+1; gname[j] != '\0'; j++) {
			gname[i++] = gname[j];
		}
		gname[i] = '\0';
		// with alt.PWD

	} else if (cnf.l2_althome > 0 && glen > cnf.l2_althome && strncmp(gname, cnf._althome, cnf.l2_althome) == 0) {
		gname[0] = '~';
		for (i=1, j=cnf.l2_althome; gname[j] != '\0'; j++) {
			gname[i++] = gname[j];
		}
		gname[i] = '\0';
		// with alt.HOME

	} else if (glen > cnf.l1_home && strncmp(gname, cnf._home, cnf.l1_home) == 0) {
		gname[0] = '~';
		for (i=1, j=cnf.l1_home; gname[j] != '\0'; j++) {
			gname[i++] = gname[j];
		}
		gname[i] = '\0';
		// with HOME

	}

	return 0;
}

/*
* check_dirname - check dirname and basename of globbed filename
* return 0 if Ok
*/
static int
check_dirname (const char *gname)
{
	struct stat test;
	int ret=0;
	char fn[FNAMESIZE];
	char dn[FNAMESIZE];

	mybasename(fn, gname, FNAMESIZE);
	mydirname(dn, gname, FNAMESIZE);
	if (fn[0]=='\0' || dn[0]=='\0') {
		ret = 3; /*error*/
	} else {
		if (strncmp(fn, "..", 3)==0 || strncmp(fn, ".", 2)==0) {
			ret = 2;	/* invalid filenames */
		} else {
			ret = stat(dn, &test);
			if (ret==0 && S_ISDIR(test.st_mode)) {
				ret = 0;
			} else {
				ret = 1;
			}
		}
	}

	return (ret);
}

/*
* check_files - check files on disk if changed, loop of restat_file calls
*/
int
check_files (void)
{
	int ri, ret=0;
	for (ri=0; ri<RINGSIZE; ri++) {
		ret |= restat_file (ri);
	}
	return (ret);
}

/*
* restat_file - redo the file stat
*/
int
restat_file (int ring_i)
{
	struct stat test;
	TEST_ACCESS_TYPE ta_check = TEST_ACCESS_NONE;
	int ret=0;

	if (ring_i>=0 && ring_i<RINGSIZE &&
		(cnf.fdata[ring_i].fflag & FSTAT_OPEN) &&
		!(cnf.fdata[ring_i].fflag & FSTAT_SCRATCH))
	{
		ret = stat(cnf.fdata[ring_i].fpath, &test);
		if (ret == 0) {
			if (cnf.fdata[ring_i].stat.st_ino != test.st_ino) {
				tracemsg("file %s on disk has new inode", cnf.fdata[ring_i].fpath);
				; /* query_inode() requires up-to-date inode values, update it */

			} else if (cnf.fdata[ring_i].stat.st_mtime < test.st_mtime) {
				if (!(cnf.fdata[ring_i].fflag & FSTAT_EXTCH)) {
					tracemsg("file %s modified on disk!!", cnf.fdata[ring_i].fpath);
					cnf.fdata[ring_i].fflag |= FSTAT_EXTCH;
					ret = 1;
				}
			} else if (cnf.fdata[ring_i].fflag & FSTAT_EXTCH) {
				cnf.fdata[ring_i].fflag &= ~FSTAT_EXTCH;
				ret = 2;
			}

			/* additionally, check R/W status
			*/
			ta_check = testaccess(&test);
			if (ta_check == TEST_ACCESS_RW_OK) {
				if ((cnf.fdata[ring_i].fflag & FSTAT_RO)) {
					cnf.fdata[ring_i].fflag &= ~FSTAT_RO;
					ret |= 16;
				}
				cnf.fdata[ring_i].fflag &= ~FSTAT_SCRATCH;
			} else if (ta_check == TEST_ACCESS_R_OK) {
				if (!(cnf.fdata[ring_i].fflag & FSTAT_RO)) {
					cnf.fdata[ring_i].fflag |= FSTAT_RO;
					ret |= 4;
				}
				cnf.fdata[ring_i].fflag &= ~FSTAT_SCRATCH;
			} else {
				if (!(cnf.fdata[ring_i].fflag & FSTAT_RO)) {
					cnf.fdata[ring_i].fflag |= FSTAT_RO;
					ret |= 4;
				}
				if (!(cnf.fdata[ring_i].fflag & FSTAT_SCRATCH)) {
					cnf.fdata[ring_i].fflag |= FSTAT_SCRATCH;
					ret |= 8;
				}
			}

			/* do not update cnf.fdata[ring_i].stat, FSTAT_EXTCH disappears
			*/
		} else {
			tracemsg ("cannot stat %s file!", cnf.fdata[ring_i].fpath);
			cnf.fdata[ring_i].fflag |= FSTAT_SCRATCH;
			ret = 0x100;
		}
	}

	return (ret);
}

/* check rw or r/o permission of stat'd file, comparing with euid/egid and supplementary groups
*/
TEST_ACCESS_TYPE
testaccess (struct stat *test)
{
	int groupsize=0;
	unsigned check_group=0;
	int i=0;
	TEST_ACCESS_TYPE ta_return = TEST_ACCESS_NONE;

	if ((S_ISREG(test->st_mode)) == 0) {
		/* not a regular file */
		return ta_return;
	}

	if (cnf.euid == 0) {
		return TEST_ACCESS_RW_OK;
	}

	groupsize = (sizeof(cnf.groups) / sizeof(cnf.groups[0]));

	if (test->st_uid == cnf.euid) {
		if ((test->st_mode & (S_IRUSR | S_IWUSR)) == (S_IRUSR | S_IWUSR)) {
			ta_return = TEST_ACCESS_RW_OK;
		} else if ((test->st_mode & (S_IRUSR | S_IWUSR)) == (S_IRUSR)) {
			ta_return = TEST_ACCESS_R_OK;
		} else if ((test->st_mode & (S_IRUSR | S_IWUSR)) == (S_IWUSR)) {
			ta_return = TEST_ACCESS_W_OK;
		}
		return ta_return;
	}

	if (test->st_gid == cnf.egid) {
		check_group = cnf.egid;
	}
	for (i=0; i<groupsize && cnf.groups[i]>0; i++) {
		if (test->st_gid == cnf.groups[i]) {
			check_group = cnf.groups[i];
			break;
		}
	}
	if (check_group) {
		if ((test->st_mode & (S_IRGRP | S_IWGRP)) == (S_IRGRP | S_IWGRP)) {
			ta_return = TEST_ACCESS_RW_OK;
		} else if ((test->st_mode & (S_IRGRP | S_IWGRP)) == (S_IRGRP)) {
			ta_return = TEST_ACCESS_R_OK;
		} else if ((test->st_mode & (S_IRGRP | S_IWGRP)) == (S_IWGRP)) {
			ta_return = TEST_ACCESS_W_OK;
		}
		return ta_return;
	}

	if ((test->st_mode & (S_IROTH | S_IWOTH)) == (S_IROTH | S_IWOTH)) {
		ta_return = TEST_ACCESS_RW_OK;
	} else if ((test->st_mode & (S_IROTH | S_IWOTH)) == (S_IROTH)) {
		ta_return = TEST_ACCESS_R_OK;
	} else if ((test->st_mode & (S_IROTH | S_IWOTH)) == (S_IWOTH)) {
		ta_return = TEST_ACCESS_W_OK;
	}
	return ta_return;
}

/* getxline_filter - keep TAB, handle BACKSPACE if possible,
* drop all CR or keep them, depends on GSTAT_FIXCR
* keep printables, drop control chars
*/
static int
getxline_filter(char *getbuff)
{
	int ir=0, iw=0, changed=0;

	if (getbuff == NULL)
		return 0;

	while (getbuff[ir] != '\0') {
		if (getbuff[ir] == '\n') {
			getbuff[iw++] = getbuff[ir];
			break;
		} else if (getbuff[ir] == '\r') {
			if (cnf.gstat & GSTAT_FIXCR)
				changed++;
			else
				getbuff[iw++] = getbuff[ir];
		} else if (getbuff[ir] == 0x09) {
			/* tab */
			getbuff[iw++] = getbuff[ir];
		} else if (getbuff[ir] == 0x08) {
			/* backspace */
			if (iw > 0)
				getbuff[--iw] = '\0';
			changed++;
		} else if ((unsigned char)getbuff[ir] >= 0x20 && (unsigned char)getbuff[ir] != 0x7f) {
			/* printable (but maybe utf-8) */
			getbuff[iw++] = getbuff[ir];
		} else {
			changed++;
		}
		ir++;
	}
	getbuff[iw] = '\0';

	return changed;
}

/*
* read_lines - read lines from open stream into memory buffer,
* return: 0:ok, 1:file error, 2:memory error
*/
int
read_lines (FILE *fp, LINE **linep, int *lineno, int *fflag)
{
	char *s=NULL, *getbuff=NULL;
	LINE *lp=NULL, *lx=NULL;
	int lno=0, fixcount=0;
	int ret=0, changed=0;

	if ((getbuff = (char *) MALLOC(LINESIZE_INIT)) == NULL) {
		ERRLOG(0xE03B);
		return (2);
	}
	lp = *linep;
	lno = *lineno;

	while (ret==0)
	{
		s = fgets (getbuff, LINESIZE_INIT, fp);
		if (s == NULL) {
			if (ferror(fp)) {
				ERRLOG(0xE099);
				ret=1;
			}
			if (lp->lflag & LSTAT_BOTTOM) {
				lp = lp->prev;	/* another bugfix:041101 */
			}
			break;
		}

		changed = getxline_filter(getbuff);

		if ((lx = append_line(lp, getbuff)) == NULL) {
			ret=2;
			break;
		}
		lp = lx;
		if (changed) {
			lp->lflag |= LSTAT_CHANGE;
			/* .fflag |= FSTAT_CHANGE; */
			fixcount++;
		}

		/* increment line number counter */
		lno++;
	}

	FREE(getbuff); getbuff = NULL;

	/* give back the updated pointer */
	*linep = lp;
	*lineno = lno;

	/* propagate line-end fix */
	if (fixcount)
		*fflag |= FSTAT_CHANGE;

	return (ret);
}

/*
* read_file - read lines from file into memory buffer,
* (after checks: file is not in the ring but existing and readable)
* return: 0:ok, else: error
*/
int
read_file (const char *fname, const struct stat *test)
{
	FILE *fp = NULL;
	int lno, ret=0, fflag=0;
	LINE *lp = NULL;

	if ((ret = scratch_buffer(fname)) != 0) {
		return (ret);
	}
	/* cnf.ring_curr is set -- CURR_FILE and CURR_LINE ok */
	/* keep FSTAT_OPEN and FSTAT_SCRATCH ... */
	CURR_FILE.stat = *test;

	ret = 1;
	if ((fp = fopen(CURR_FILE.fpath,"r+")) != NULL) {
		ret = 0;
		CURR_FILE.fflag &= ~FSTAT_RO;
	} else if ((fp = fopen(CURR_FILE.fpath,"r")) != NULL) {
		ret = 0;
		CURR_FILE.fflag |= FSTAT_RO;
	}

	if (ret==0) {
		lp = CURR_FILE.top;
		lno = 0;
		ret = read_lines (fp, &lp, &lno, &fflag);
		fclose(fp);
	}

	if (ret) {
		FH_LOG(LOG_ERR, "failure, ri=%d ret=%d", cnf.ring_curr, ret);
		drop_file();
		tracemsg ("read file [%s] failed!", CURR_FILE.fpath);
	} else {
		CURR_FILE.num_lines = lno;
		CURR_FILE.fflag &= ~FSTAT_SCRATCH;

		if (CURR_FILE.curr_line == NULL || CURR_FILE.top == NULL || CURR_FILE.bottom == NULL) {
			ERRLOG(0xE075); /* curr_line, top or bottom line is pointer NULL */
			tracemsg ("read file [%s] failed!", CURR_FILE.fpath);
			drop_file();
			return -1;
		}

		go_top();

		/* better to start on command line */
		CURR_FILE.fflag |= FSTAT_CMD;

		/* if line-end truncated then this must be visible */
		if (fflag & FSTAT_CHANGE) {
			CURR_FILE.fflag |= FSTAT_CHANGE;
		}
	}

	return (ret);
}

/*
* query_scratch_fname - check the ring if fname used by open (special) file
* returns valid ring index if found, -1 otherwise
*/
int
query_scratch_fname (const char *fname)
{
	int ri, ret = -1;

	for (ri=0; ri<RINGSIZE; ri++) {
		if ((cnf.fdata[ri].fflag & FSTAT_OPEN) &&
		    (cnf.fdata[ri].fflag & FSTAT_SCRATCH) &&
		    (strncmp(cnf.fdata[ri].fname, fname, FNAMESIZE) == 0)) {
			ret = ri;
			break;
		}
	}

	return (ret);
}

/*
* scratch_buffer - initialize the buffer for regular and special use
* return: 0 if ok
* ok: ring_size, ring_curr
*/
int
scratch_buffer (const char *fname)
{
	int ring_i=0;
	int ret=0;
	int origin = cnf.ring_curr;

	/* test first: not in the ring ? */
	ring_i = query_scratch_fname (fname);
	if (ring_i != -1) {
		/* already in the ring */
		cnf.ring_curr = ring_i;
		return (0);
	}

	ring_i = next_ri();
	if (ring_i == -1) {
		tracemsg("cannot add new buffer: no free ring index");
		return 1;
	}

	memset (&cnf.fdata[ring_i].stat, 0, sizeof(struct stat));
	if (fname[0] == '*') {
		strncpy (cnf.fdata[ring_i].fname, fname, FNAMESIZE);
		cnf.fdata[ring_i].fname[FNAMESIZE-1] = '\0';
		cnf.fdata[ring_i].fpath[0] = '\0';
		cnf.fdata[ring_i].ftype = fname_ext(cnf.fdata[ring_i].fname);
	} else {
		strncpy (cnf.fdata[ring_i].fpath, fname, FNAMESIZE);
		cnf.fdata[ring_i].fpath[FNAMESIZE-1] = '\0';
		strncpy (cnf.fdata[ring_i].fname, fname, FNAMESIZE);
		cnf.fdata[ring_i].fname[FNAMESIZE-1] = '\0';
		abbrev_filename(cnf.fdata[ring_i].fname);
		cnf.fdata[ring_i].ftype = fname_ext(cnf.fdata[ring_i].fname);
	}

	cnf.fdata[ring_i].fflag = FSTAT_SCRATCH;
	cnf.fdata[ring_i].num_lines = 0;
	cnf.fdata[ring_i].lineno = 0;		/* top */
	cnf.fdata[ring_i].lncol = 0;
	cnf.fdata[ring_i].lnoff = 0;
	cnf.fdata[ring_i].focus = 0;
	cnf.fdata[ring_i].curpos = 0;
	cnf.fdata[ring_i].curr_line = NULL;
	cnf.fdata[ring_i].top = NULL;
	cnf.fdata[ring_i].bottom = NULL;
	cnf.fdata[ring_i].flevel = 1;
	cnf.fdata[ring_i].origin = origin;	/* maybe scratch buffer */

	cnf.fdata[ring_i].pipe_input = 0;
	cnf.fdata[ring_i].pipe_output = 0;
	cnf.fdata[ring_i].readbuff = NULL;
	cnf.fdata[ring_i].chrw = -1;

	if (!ret) {
		cnf.fdata[ring_i].top = append_line (NULL, TOP_MARK);
		if (cnf.fdata[ring_i].top != NULL) {
			cnf.fdata[ring_i].top->lflag |= LSTAT_TOP;
		} else {
			ret=2;
		}
	}

	if (!ret) {
		cnf.fdata[ring_i].bottom = append_line (cnf.fdata[ring_i].top, BOTTOM_MARK);
		if (cnf.fdata[ring_i].bottom != NULL) {
			cnf.fdata[ring_i].bottom->lflag |= LSTAT_BOTTOM;
		} else {
			ret=3;
		}
	}

	if (!ret) {
		cnf.ring_size++;
		cnf.ring_curr = ring_i;
		CURR_FILE.curr_line = CURR_FILE.bottom;
		CURR_FILE.lineno = 1;
		memset (CURR_FILE.search_expr, 0, sizeof(CURR_FILE.search_expr));
		CURR_FILE.fflag |= FSTAT_CMD | FSTAT_OPEN | FSTAT_FMASK;
	}

	return ret;
}

/*
** reload_file - read file from disk and replace lines in regular buffer
*/
int
reload_file (void)
{
	FILE *fp = NULL;
	struct stat test;
	int lno, ret = -1, fflag=0;
	int keep_lineno;
	LINE *lp = NULL;

	if (!(CURR_FILE.fflag & FSTAT_OPEN) || (CURR_FILE.fflag & FSTAT_SPECW)) {
		/* not for special buffers */
		return (0);
	}

	keep_lineno = CURR_FILE.lineno;
	ret = 1;
	if ((fp = fopen(CURR_FILE.fpath,"r+")) != NULL) {
		ret = 0;
		CURR_FILE.fflag &= ~FSTAT_RO;
	} else if ((fp = fopen(CURR_FILE.fpath,"r")) != NULL) {
		ret = 0;
		CURR_FILE.fflag |= FSTAT_RO;
	}
	if (ret==0) {
		clean_buffer ();
		if (fstat(fileno(fp), &test) == 0) {
			CURR_FILE.stat = test;
		}
		lp = CURR_FILE.top;
		lno = 0;
		ret = read_lines (fp, &lp, &lno, &fflag);
		fclose(fp);
	}

	if (ret) {
		tracemsg ("Cannot reload file [%s]", CURR_FILE.fpath);
		CURR_FILE.fflag |= FSTAT_SCRATCH;
		return (ret);
	}

	CURR_LINE = CURR_FILE.top;
	CURR_FILE.num_lines = lno;
	CURR_FILE.fflag &= ~(FSTAT_EXTCH | FSTAT_SCRATCH);

	/* restore line position */
	if (keep_lineno > CURR_FILE.num_lines)
		keep_lineno = CURR_FILE.num_lines;
	lp = lll_goto_lineno (cnf.ring_curr, keep_lineno);
	if (lp != NULL) {
		set_position (cnf.ring_curr, keep_lineno, lp);
	}

	/* if line-end truncated then this must be visible */
	if (fflag & FSTAT_CHANGE) {
		CURR_FILE.fflag |= FSTAT_CHANGE;
		tracemsg ("file reload: success -- some line-ends fixed");
	} else {
		tracemsg ("file reload: success");
	}

	return (ret);
}

/* parse standard diff header line into ranges and action
*/
static int
parse_diff_header (const char *ptr, int ra[5])
{
	char *endptr;
	ra[0] = ra[1] = ra[2] = ra[3] = ra[4] = 0;

	ra[0] = strtol(ptr, &endptr, 10);
	ra[1] = ra[0];
	if (*endptr == ',') {
		ptr = endptr+1;
		ra[1] = strtol(ptr, &endptr, 10);
	}

	ra[2] = *endptr;
	ptr = endptr+1;
	if ((ra[2]!='a' && ra[2]!='c' && ra[2]!='d') || (*ptr == '\0')) {
		return 1;
	}

	ra[3] = strtol(ptr, &endptr, 10);
	ra[4] = ra[3];
	if (*endptr == ',') {
		ptr = endptr+1;
		ra[4] = strtol(ptr, &endptr, 10);
	}

	return 0;
}

/*
** show_diff - diff file on disk with buffer; parameters like '-w -b' maybe added on command line
*/
int
show_diff (const char *diff_opts)
{
	int ret=0;
	char ext_argstr[CMDLINESIZE];

	if (!(CURR_FILE.fflag & FSTAT_OPEN) || (CURR_FILE.fflag & FSTAT_SPECW)) {
		/* not for special buffers */
		return (0);
	}

	if (cnf.diff_path[0] == '\0') {
		tracemsg("diff path not configured");
		return (1);
	}

	memset(ext_argstr, 0, sizeof(ext_argstr));
	/* added -s option, diff says "identical" */
	snprintf(ext_argstr, sizeof(ext_argstr)-1, "diff -s -u %s %s -", diff_opts, CURR_FILE.fpath);

	/* OPT_REDIR_ERR for BSD diff */
	ret = read_pipe ("*diff*", cnf.diff_path, ext_argstr, (OPT_NOAPP | OPT_REDIR_ERR | OPT_IN_OUT_REAL_ALL));

	return (ret);
}

/*
** reload_bydiff - reload regular file from disk smoothly based on content differences,
**	keep line attributes, bookmarks, tagging where possible
*/
int
reload_bydiff (void)
{
	int ret=0;
	char ext_argstr[CMDLINESIZE];
	char *rb=NULL;
	int ni=0;
	LINE *lp=NULL;
	FILE *fp=NULL;
	struct stat test;
	int original_lineno = CURR_FILE.lineno;
	int range[5];
	int action='.', target=0, cnt_to=0, cnt_from=0;
	int actions_counter=0;
	int orig_fixcr, changed, changed_crlf;

	if (!(CURR_FILE.fflag & FSTAT_OPEN) || (CURR_FILE.fflag & FSTAT_SPECW)) {
		/* not for special buffers */
		return (0);
	}

	/* do clean up */
	for (lp=CURR_FILE.top->next; TEXT_LINE(lp); lp=lp->next) {
		if (lp->lflag & LSTAT_CHANGE) {
			lp->lflag |= LSTAT_ALTER;
			lp->lflag &= ~LSTAT_CHANGE;
		}
	}

	ret = 1;
	if ((fp = fopen(CURR_FILE.fpath,"r+")) != NULL) {
		ret = 0;
		CURR_FILE.fflag &= ~FSTAT_RO;
	} else if ((fp = fopen(CURR_FILE.fpath,"r")) != NULL) {
		ret = 0;
		CURR_FILE.fflag |= FSTAT_RO;
	}
	if (ret==0) {
		if (fstat(fileno(fp), &test) == 0) {
			CURR_FILE.stat = test;
		}
		fclose(fp);
	} else {
		tracemsg ("Cannot reload file [%s]", CURR_FILE.fpath);
		CURR_FILE.fflag |= FSTAT_SCRATCH;
		return(1);
	}

	memset(ext_argstr, 0, sizeof(ext_argstr));
	snprintf(ext_argstr, sizeof(ext_argstr)-1, "diff - %s", CURR_FILE.fpath);

	/* OPT_REDIR_ERR for BSD diff */
	ret = read_pipe ("*notused*", cnf.diff_path, ext_argstr, (OPT_NOSCRATCH | OPT_REDIR_ERR | OPT_IN_OUT_REAL_ALL));
	if (ret) {
		tracemsg("cannot reload file (reading diff pipe fail)");
		return (ret);
	}

	/* raw data from readout_pipe is important to know about crlf changes */
	orig_fixcr = (cnf.gstat & GSTAT_FIXCR);
	changed_crlf = 0;

	go_home();
	while (ret==0 && CURR_FILE.pipe_output)
	{
		if (orig_fixcr)
			cnf.gstat &= ~GSTAT_FIXCR;
		ret = readout_pipe (cnf.ring_curr);
		if (orig_fixcr)
			cnf.gstat |= GSTAT_FIXCR;
		if (ret) {
			if (ret==1)
				ret=0;
			/* pipe empty (ready) */
			break;
		}
		rb = CURR_FILE.readbuff;
		ni = CURR_FILE.rb_nexti;
		if (ni <= 0)
			continue;
		rb[ni] = '\0';

		PD_LOG(LOG_NOTICE, ">>> rb [%s] ni %d", rb, ni);
		if (action == '.') {
			actions_counter++;
			if (ni >= 3 && !parse_diff_header (rb, range)) {
				action = range[2];
				target = range[3];
				cnt_to = range[4] - range[3] + 1;
				cnt_from = range[1] - range[0] + 1;
				PD_LOG(LOG_NOTICE, "dot, action %c target %d (cnt %d) source %d (cnt %d)",
					action, target, cnt_to, range[0], cnt_from);

				lp = lll_goto_lineno (cnf.ring_curr, target);
				if (lp == NULL) {
					PD_LOG(LOG_ERR, "cannot jump to line %d", target);
					ret = -1;
				}
				CURR_FILE.lineno = target;
				CURR_LINE = lp;

				if (action == 'a' && target < original_lineno) {
					original_lineno += cnt_to;
				} else if (action == 'd' && target < original_lineno) {
					original_lineno -= cnt_from;
				} else if (action == 'c') {
					if (cnt_to - cnt_from > 0) {
						/* like an add action */
						if (target < original_lineno) {
							original_lineno += cnt_to - cnt_from;
						}
					} else if (cnt_from - cnt_to > 0) {
						/* like a delete action */
						if (target < original_lineno) {
							original_lineno -= cnt_from - cnt_to;
						}
					}
				}
			} else {
				ret = 1;
			}
		} else if (action == 'a') {
			if (rb[0] == '>' && cnt_to > 0 && ni >= 3) {
				changed = getxline_filter(&rb[2]);
				if ((lp = insert_line_before (CURR_LINE, &rb[2])) != NULL) {
					CURR_FILE.num_lines++;
					CURR_FILE.lineno++;
					lp->lflag &= ~LSTAT_CHANGE;
					if (changed) {
						lp->lflag |= LSTAT_CHANGE;
						changed_crlf++;
					}
				} else {
					PD_LOG(LOG_ERR, "insert line failed");
					ret = -2;
				}
				if (--cnt_to == 0) {
					action = '.';
				}
			} else {
				PD_LOG(LOG_ERR, "invalid input, a, >, %d, %d", cnt_to, ni);
				ret = 2;
			}
		} else if (action == 'd') {
			if (rb[0] == '<' && cnt_from > 0) {
				lp = CURR_LINE->next;
				if (TEXT_LINE(lp)) {
					clr_opt_bookmark(lp);
					lp = lll_rm(lp);	/* in reload_bydiff() */
					CURR_FILE.num_lines--;
				} else {
					PD_LOG(LOG_ERR, "delete line failed");
					ret = -3;
				}
				if (--cnt_from == 0) {
					action = '.';
				}
			} else {
				PD_LOG(LOG_ERR, "invalid input, d, <, %d, %d", cnt_from, ni);
				ret = 3;
			}
		} else if (action == 'c') {
			if (rb[0] == '<' && cnt_from > 0) {
				lp = CURR_LINE;
				if (TEXT_LINE(lp)) {
					clr_opt_bookmark(lp);
					CURR_LINE = lll_rm(lp);		/* in reload_bydiff() */
					CURR_FILE.num_lines--;
				} else {
					PD_LOG(LOG_ERR, "invalid input, c, <, %d, %d", cnt_from, ni);
					ret = -4;
				}
				cnt_from--;
			} else if (rb[0] == '>' && cnt_to > 0 && ni >= 3) {
				changed = getxline_filter(&rb[2]);
				if ((lp = insert_line_before (CURR_LINE, &rb[2])) != NULL) {
					CURR_FILE.num_lines++;
					CURR_FILE.lineno++;
					lp->lflag &= ~LSTAT_CHANGE;
					if (changed) {
						lp->lflag |= LSTAT_CHANGE;
						changed_crlf++;
					}
				} else {
					PD_LOG(LOG_ERR, "invalid input, c, >, %d, %d", cnt_to, ni);
					ret = -5;
				}
				cnt_to--;
			} else if (rb[0] == '-' && cnt_from == 0) {
				; /*separator*/
			} else {
				PD_LOG(LOG_ERR, "invalid input, c (ni=%d, cnt to=%d from=%d)", ni, cnt_to, cnt_from);
				ret = 4;
			}
			if (cnt_from == 0 && cnt_to == 0) {
				action = '.';
			}
		}

	}
	CURR_FILE.fflag &= ~(FSTAT_EXTCH | FSTAT_SCRATCH | FSTAT_RO | FSTAT_CHANGE);
	if (orig_fixcr)
		cnf.gstat |= GSTAT_FIXCR;
	else
		cnf.gstat &= ~GSTAT_FIXCR;

	if (ret) {
		PD_LOG(LOG_ERR, "error during diff output processing, reset line position (%d)", ret);
		go_bottom();
		CURR_FILE.fflag |= (FSTAT_CHANGE);
	} else {
		go_top();
		lp = lll_goto_lineno (cnf.ring_curr, original_lineno);
		if (lp == NULL) {
			/* out of range, can happen */
			go_bottom();
		} else {
			CURR_LINE = lp;
			CURR_FILE.lineno = original_lineno;
		}
	}
	update_focus(CENTER_FOCUSLINE, cnf.ring_curr);

	/* selection handling */
	if (cnf.select_ri == cnf.ring_curr) {
		recover_selection();
	}

	wait4_bg(cnf.ring_curr);

	if (!ret) {
		if (actions_counter) {
			/* if line-end truncated then this must be visible */
			if (changed_crlf) {
				CURR_FILE.fflag |= FSTAT_CHANGE;
				tracemsg ("reload done -- some line-ends fixed");
			} else {
				tracemsg ("reload done");
			}
		} else {
			tracemsg ("identical");
		}
	} else {
		tracemsg ("reload failed");
	}

	return (ret);
}

/*
* clean_buffer - clean current file's data, make the text area empty
* but keep .search_expr and .stat
*/
int
clean_buffer (void)
{
	LINE *lp = NULL;
	int ret = 0;

	CURR_FILE.fflag = FSTAT_SCRATCH | FSTAT_CMD | FSTAT_OPEN | FSTAT_FMASK;
	CURR_FILE.num_lines = 0;
	CURR_FILE.lineno = 0;	/* top */
	CURR_FILE.lncol = 0;
	CURR_FILE.lnoff = 0;
	CURR_FILE.focus = 0;
	CURR_FILE.curpos = 0;
	CURR_FILE.curr_line = CURR_FILE.top;
	CURR_FILE.flevel = 1;
	/* do not change CURR_FILE.origin */

	/* reset selection, if it was here */
	if (cnf.select_ri != -1 && cnf.ring_curr == cnf.select_ri) {
		reset_select();			/* in: clean_buffer() */
	}

	/* remove text lines */
	lp = CURR_FILE.top->next;
	while (TEXT_LINE(lp))
		lp = lll_rm(lp);		/* in: clean_buffer() */

	/* free regexp */
	if (CURR_FILE.fflag & (FSTAT_TAG2 | FSTAT_TAG3)) {
		regfree(&CURR_FILE.search_reg);
	}
	if (CURR_FILE.fflag & FSTAT_TAG5) {
		regfree(&CURR_FILE.highlight_reg);
	}
	/* remove bookmarks, etc */
	clear_bookmarks(cnf.ring_curr);
	mhist_clear(cnf.ring_curr);

	return (ret);
}

/*
** drop_file - drop file immediately, even if changed
*/
int
drop_file (void)
{
	LINE *lp = NULL;
	int ring_i = -1;
	int ret = 0, origin = -1;

	ring_i = cnf.ring_curr;
	if (ring_i >= 0 && ring_i < RINGSIZE && (cnf.fdata[ring_i].fflag & FSTAT_OPEN)) {

		/* if there is bg proc running... */
		stop_bg_process();	/* drop_file() */

		/* reset selection, if it was here */
		if (cnf.select_ri != -1 && ring_i == cnf.select_ri) {
			reset_select();		/* in drop_file() */
		}

		/* remove all lines */
		lp = cnf.fdata[ring_i].curr_line;
		while (lp != NULL)
			lp = lll_rm(lp);		/* in: drop_file() */

		/* free regexp */
		if (cnf.fdata[ring_i].fflag & (FSTAT_TAG2 | FSTAT_TAG3)) {
			regfree(&cnf.fdata[ring_i].search_reg);
		}
		if (cnf.fdata[ring_i].fflag & FSTAT_TAG5) {
			regfree(&cnf.fdata[ring_i].highlight_reg);
		}

		origin = cnf.fdata[ring_i].origin;

		cnf.fdata[ring_i].origin = -1;
		cnf.fdata[ring_i].fflag = 0;		/* drop */
		cnf.fdata[ring_i].num_lines = 0;
		cnf.fdata[ring_i].lineno = 0;
		cnf.fdata[ring_i].lncol = 0;
		cnf.fdata[ring_i].lnoff = 0;
		cnf.fdata[ring_i].focus = 0;
		cnf.fdata[ring_i].curpos = 0;
		cnf.fdata[ring_i].curr_line = NULL;
		cnf.fdata[ring_i].top = NULL;
		cnf.fdata[ring_i].bottom = NULL;
		cnf.fdata[ring_i].flevel = 0;		/* drop */
		cnf.fdata[ring_i].fpath[0] = '\0';

		/* buffer already dropped, clear invalid things */
		clear_bookmarks(ring_i);
		mhist_clear(ring_i);

		/* change the ring size */
		--cnf.ring_size;
	} else {
		ERRLOG(0xE074); /* invalid ring index */
		ret = 1;
	}

	if (origin >= 0 && origin < RINGSIZE && (cnf.fdata[origin].fflag & FSTAT_OPEN)) {
		/* back to the origin, even if FSTAT_HIDDEN bit set */
		cnf.ring_curr = origin;
	} else {
		/* next, if any */
		next_file ();
	}

	return (ret);
}

/*
* drop_all - drop all buffers directly, free up memory
*/
int
drop_all (void)
{
	int ri;
	LINE *lp = NULL;

	for (ri=0; ri < RINGSIZE; ri++) {
		if ((cnf.fdata[ri].fflag & FSTAT_OPEN) == 0)
			continue;

		/* if there is bg proc running... set cnf.ring_curr is mandatory */
		cnf.ring_curr = ri;
		stop_bg_process();	/* drop_all() */

		/* remove all lines */
		lp = cnf.fdata[ri].curr_line;
		while (lp != NULL)
			lp = lll_rm(lp);	/* drop_all() */

		/* free regexp */
		if (cnf.fdata[ri].fflag & (FSTAT_TAG2 | FSTAT_TAG3)) {
			regfree(&cnf.fdata[ri].search_reg);
		}
		if (cnf.fdata[ri].fflag & FSTAT_TAG5) {
			regfree(&cnf.fdata[ri].highlight_reg);
		}

		cnf.fdata[ri].fflag = 0;
		cnf.fdata[ri].fpath[0] = '\0';
	}
	cnf.ring_curr = 0;
	cnf.ring_size = 0;

	return (0);
}

/*
** save_file - save current file to disk (overwrite if exists) with an intermediate backup,
**	the "save as" function does not overwrite an existing file,
**	running background process will be stopped
*/
int
save_file (const char *newfname)
{
	/* tracemsg at this level */
	FILE *fp = NULL;
	struct stat test;
	LINE *lp = NULL;
	char gname[FNAMESIZE];
	int ret = 0;
	int cnt;
	char forced_backup = 0;
	char save_as = (newfname[0] != '\0');
	char *fname_p = CURR_FILE.fpath;
	char backup_name[FNAMESIZE+10];
	memset(backup_name, 0, sizeof(backup_name));

	if (!save_as) {
		if (CURR_FILE.fflag & FSTAT_SPECW) {
			/* do not save temporary buffers */
			return (0);
		} else {
			if (!(CURR_FILE.fflag & FSTAT_CHANGE)) {
				tracemsg ("not changed, not saved.");
				return (0);
			}
		}
	} else {
		strncpy(gname, newfname, sizeof(gname));
		gname[sizeof(gname)-1] = '\0';
		if (glob_tilde_expansion(gname, sizeof(gname)))
			return (1);
		fname_p = gname;

		/* this mandatory check prevents overwriting another file */
		if (stat(fname_p, &test) == 0) {
			if (CURR_FILE.stat.st_ino != test.st_ino) {
				tracemsg ("file [%s] exist. choose another.", fname_p);
				return (0);
			}
		}

		/* if there is bg proc running... */
		stop_bg_process();	/* save_file() */
	}

	/* backup */
	ret = backup_file(fname_p, backup_name);
	if (ret != 0 && ret != 1) {
		tracemsg("Error: backup failed. save aborted.");
		return (4);
	}
	ret = 0;

	if ((cnf.gstat & GSTAT_SAV_INODE)==0) {
		/* unlink file before save to get an independent inode */
		if (unlink(fname_p)) {
			tracemsg("unlink [%s] failed before save (%s)", fname_p, strerror(errno));
		}
	}

	/* save */
	if ((fp = fopen(fname_p, "w")) == NULL) {
		tracemsg ("[%s]: %s.", fname_p, strerror(errno));
		CURR_FILE.fflag |= FSTAT_RO;
		if ((fp = fopen(CURR_FILE.fpath,"r")) == NULL) {
			CURR_FILE.fflag |= FSTAT_SCRATCH;
		} else {
			fclose(fp);
		}
		return (5);
	}
	lp = CURR_FILE.top->next;
	while (TEXT_LINE(lp))
	{
		if (lp->lflag & LSTAT_CHANGE) {
			lp->lflag |= LSTAT_ALTER;
			lp->lflag &= ~LSTAT_CHANGE;
		}
		cnt = fwrite (lp->buff, sizeof(char), (size_t)lp->llen, fp);
		if (cnt != lp->llen) {
			ERRLOG(0xE0AB);
			ret=1;
			break;
		}
		lp = lp->next;
	}
	if (!ret) {
		if (fstat(fileno(fp), &test) == 0) {
			CURR_FILE.stat = test;
		}
	}
	fclose(fp);

	if (!ret) {
		if ((cnf.gstat & GSTAT_NOKEEP) && !forced_backup) {
			unlink(backup_name);
		}
		/* update */
		CURR_FILE.fflag &= ~(FSTAT_CHANGE | FSTAT_CHMASK);
		CURR_FILE.fflag &= ~(FSTAT_SCRATCH | FSTAT_SPECW | FSTAT_RO | FSTAT_EXTCH);
		if (save_as) {
			strncpy (CURR_FILE.fpath, fname_p, FNAMESIZE);
			CURR_FILE.fpath[FNAMESIZE-1] = '\0';
			strncpy (CURR_FILE.fname, fname_p, FNAMESIZE);
			CURR_FILE.fname[FNAMESIZE-1] = '\0';
			abbrev_filename(CURR_FILE.fname);
			CURR_FILE.ftype = fname_ext(CURR_FILE.fname);
			/* ri not changed but the content, update titles and headers */
			cnf.gstat |= GSTAT_REDRAW;
		}
		/* ctime */
		tracemsg("file saved: %s", ctime(&CURR_FILE.stat.st_mtime));
	} else {
		ERRLOG(0xE073);
		tracemsg("Warning: save [%s] failed!", fname_p);
	}

	return (ret);
}

/*
* backup_file - create backup from disk file to disk (append '~' to fname)
* return: 0:ok, 1:file-not-exist, 2:creat-backup, 3:write, -1:malloc
*/
static int
backup_file (const char *fname, char *backup_name)
{
	int fd, bkp;
	char *data=NULL;	/* for I/O */
	int in, out;
	unsigned long blksize = 4096;

	strncpy(backup_name, fname, FNAMESIZE); /* call: FNAMESIZE+10, memset */
	strncat(backup_name, "~", 2);

	/* fd -- source file, maybe missing */
	if ((fd = open(fname, O_RDONLY|O_NONBLOCK)) == -1) {
		return (1);
	}

	/* bkp -- target file, must be writeable */
	if ((bkp = open(backup_name, O_CREAT | O_WRONLY | O_TRUNC | O_NONBLOCK, 0600)) == -1) {
		/* 2nd chance */
		strncpy(backup_name, "/tmp/", 6);
		mybasename(&backup_name[5], fname, FNAMESIZE);
		strncat(backup_name, "~", 2);
		if ((bkp = open(backup_name, O_CREAT | O_WRONLY | O_TRUNC | O_NONBLOCK, 0600)) == -1) {
			ERRLOG(0xE0AA); // open/w
			close(fd);
			return (2);
		}
	}

	if ((data = (char *) MALLOC(blksize)) == NULL) {
		ERRLOG(0xE03A);
		close(fd);
		close(bkp);
		return (3);
	}

	/* get-in put-out */
	while (1) {
		out = 0;
		in = read(fd, data, blksize);
		if (in > 0) {
			out = write(bkp, data, (size_t)in);
			if (out != in) {
				ERRLOG(0xE0AA); // write
				break;
			}
		} else if (in == 0) {
			break;	/* normal eof */
		} else { // (in == -1)
			ERRLOG(0xE0AA); // read
			break;	/* error */
		}
	}
	FREE(data); data = NULL;
	close(fd);
	close(bkp);

	if (in == out) {
		return (0);
	} else {
		return (-1);
	}
}

/*
* read_file_line - read the required line from that file
* return the dynamic buffer, or NULL on error
*/
char *
read_file_line (const char *fname, int lineno)
{
	FILE *fp=NULL;
	int lno=0;
	char *getbuff=NULL;

	if ((fp = fopen(fname,"r")) == NULL) {
		return (NULL);
	}
	if ((getbuff = (char *) MALLOC(LINESIZE_INIT)) == NULL) {
		ERRLOG(0xE039);
		fclose(fp);
		return (NULL);
	}
	while (lno<lineno)
	{
		if (fgets (getbuff, LINESIZE_INIT, fp) == NULL) {
			if (ferror(fp)) {
				ERRLOG(0xE098);
			}
			break;
		}
		lno++;
	}
	fclose(fp);

	if (lno != lineno) {
		FREE(getbuff); getbuff = NULL;
	}

	(void) getxline_filter(getbuff);

	return getbuff;
}

/* write out some characters
*/
int
write_out_chars (int fd, const char *buffer, int length)
{
	int out;
	if (length > 0) {
		out = write (fd, buffer, (size_t)length);
		if (out != length) {
			ERRLOG(0xE0A9);
			return 1;
		}
	} else {
		return 1;
	}
	return 0;
}

/* write out all visible lines from buffer
*/
int
write_out_all_visible (int fd, int ring_i, int with_shadow)
{
	LINE *lp=NULL;
	int count=0, out=0;
	int shadow=0, length=0;
	char mid_buff[100];

	if (ring_i>=0 && ring_i<RINGSIZE && (cnf.fdata[ring_i].fflag & FSTAT_OPEN)) {
		lp = cnf.fdata[ring_i].top;
		/* skip initial shadow lines */
		next_lp (ring_i, &lp, NULL);
		count = 0;
		while (TEXT_LINE(lp)) {
			if (!HIDDEN_LINE(ring_i,lp)) {
				/* shadow line, optional */
				if (shadow > 0) {
					if (shadow > 1) {
						snprintf(mid_buff, 30, "--- %d lines ---\n", shadow);
					} else {
						snprintf(mid_buff, 30, "--- 1 line ---\n");
					}
					length = strlen(mid_buff);
					out = write (fd, mid_buff, (size_t)length);
					if (out != length) {
						ERRLOG(0xE0A8);
						break;
					}
					count++;
				}
				/* line buffer, important */
				out = write (fd, lp->buff, (size_t)lp->llen);
				if (out != lp->llen) {
					ERRLOG(0xE0A7);
					break;
				}
				count++;
				shadow = 0;
			} else if (with_shadow && (cnf.gstat & GSTAT_SHADOW)) {
				shadow++;
			}
			lp = lp->next;
		}
	}

	return (count);
}

/* write out all lines from buffer
*/
int
write_out_all_lines (int fd, int ring_i)
{
	LINE *lp=NULL;
	int count=0, out=0;

	if (ring_i>=0 && ring_i<RINGSIZE && (cnf.fdata[ring_i].fflag & FSTAT_OPEN)) {
		lp = cnf.fdata[ring_i].top->next;
		while (TEXT_LINE(lp)) {
			/* out */
			out = write (fd, lp->buff, (size_t)lp->llen);
			if (out != lp->llen) {
				ERRLOG(0xE0A6);
				break;
			}
			count++;
			lp = lp->next;
		}
	}

	return (count);
}
