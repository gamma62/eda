/*
* ring.c 
* ring-list, bookmark handling, motion history functions
*
* Copyright 2003-2011 Attila Gy. Molnar
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
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <features.h>

#include <config.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <syslog.h>
#include "main.h"
#include "proto.h"

/* global config */
extern CONFIG cnf;

/* local proto */

/*
** list_buffers - open a special buffer with a list of open files and bookmarks
*/
int
list_buffers (void)
{
	int ri, lno_read;
	LINE *lp=NULL, *lx=NULL;
	char one_line[CMDLINESIZE*2];
	struct tm *tm_p=NULL;
	char mtime_buff[40];
	int ret=1, bm_i, cnt;
	int origin = cnf.ring_curr;

	/* open or reopen? */
	ret = scratch_buffer("*ring*");
	if (ret==0 && CURR_FILE.num_lines > 0) {
		ret = clean_buffer();
	}
	if (ret) {
		return (ret);
	}
	/* cnf.ring_curr is set now -- CURR_FILE and CURR_LINE alive */
	CURR_FILE.num_lines = 0;
	CURR_FILE.fflag |= (FSTAT_SPECW);

	/* fill with data from ring
	 */
	CURR_FILE.num_lines += cnf.ring_size;
	lp = CURR_FILE.bottom->prev;
	lno_read = 0;
	memset(one_line, 0, sizeof(one_line));
	for (ri=0; ret==0 && ri<RINGSIZE; ri++) {
		if (cnf.fdata[ri].fflag & FSTAT_OPEN)
		{
			/* base data
			*/
			if (cnf.fdata[ri].fflag & (FSTAT_SPECW | FSTAT_SCRATCH)) {
				snprintf(one_line, sizeof(one_line)-1, "r=%d %s %s%s%s #lines %d\n",
					ri, cnf.fdata[ri].fname,
					(cnf.fdata[ri].fflag & FSTAT_SPECW) ? "special " : "",
					(cnf.fdata[ri].fflag & FSTAT_SCRATCH) ? "scratch " : "",
					(cnf.fdata[ri].fflag & FSTAT_INTERACT) ? "interactive " : "",
					cnf.fdata[ri].num_lines);
			} else {
				tm_p = localtime(&cnf.fdata[ri].stat.st_mtime);
				strftime(mtime_buff, 39, "%Y-%m-%d %H:%M:%S", tm_p);
				mtime_buff[39] = '\0';
				snprintf(one_line, sizeof(one_line)/2, "r=%d %s %s %s%s #lines %d ",
					ri, cnf.fdata[ri].fname,
					(cnf.fdata[ri].fflag & FSTAT_RO) ? "R/O" : "R/W",
					(cnf.fdata[ri].fflag & FSTAT_CHANGE) ? "Mod " : "",
					(cnf.fdata[ri].fflag & FSTAT_HIDDEN) ? "HIDDEN " : "",
					cnf.fdata[ri].num_lines);
				cnt = strlen(one_line);
				/**/
				snprintf(one_line+cnt, sizeof(one_line)/2, "line:%d [%s/%s]%s %s\n",
					cnf.fdata[ri].lineno,
					cnf.fdata[ri].dirname, cnf.fdata[ri].basename,
					(cnf.fdata[ri].fflag & FSTAT_EXTCH) ? " !!changed on disk!!" : "",
					mtime_buff);
			}

			if ((lx = append_line (lp, one_line)) != NULL) {
				lno_read++;
				lp=lx;
			} else {
				ret = 2;
				break;
			}

			/* optional: bookmarks
			*/
			for (bm_i=1; bm_i < 10; bm_i++) {
				if (ri == cnf.bookmark[bm_i].ring) {
					snprintf(one_line, sizeof(one_line)-1, "\tbookmark %d: %s\n",
						bm_i, cnf.bookmark[bm_i].sample);
					if ((lx = append_line (lp, one_line)) != NULL) {
						lno_read++;
						lp=lx;
					} else {
						ret = 2;
						break;
					}
				}
			}

		}/* if fdata... */
	}/* for ri... */

	if (ret==0) {
		CURR_FILE.num_lines = lno_read;
		CURR_LINE = CURR_FILE.top->next;
		CURR_FILE.lineno = 1;
		if (origin != cnf.ring_curr) {
			CURR_FILE.origin = origin;
		}
		update_focus(FOCUS_SET_INIT, cnf.ring_curr, 1, NULL);
		go_home();
		CURR_FILE.fflag &= ~FSTAT_CHANGE;
		/* disable inline editing and adding lines */
		CURR_FILE.fflag |= (FSTAT_NOEDIT | FSTAT_NOADDLIN);
	} else {
		ret |= drop_file();
	}

	return (ret);
}

/* internal function for bookmark set, known limitation: one line -- one bookmark
*/
void
set_bookmark (int bm_i)
{
	char *bn=NULL;
	char *sample=NULL;
	int slen=0, blen=0;

	if ((bm_i > 0 && bm_i < 10) && (TEXT_LINE(CURR_LINE))) {

		/* check line's bookmark bits clean
		 * (known limitation: one line -- one bookmark)
		 */
		if (CURR_LINE->lflag & LSTAT_BM_BITS) {
			tracemsg("this line has already bookmark %d set",
				(CURR_LINE->lflag & LSTAT_BM_BITS) >> BM_BIT_SHIFT);
			return;
		}

		/* allowed to overwrite other setting of the same bm_i (clear and set)
		*/
		if (cnf.bookmark[bm_i].ring == cnf.ring_curr)
			clr_bookmark (bm_i);
		cnf.bookmark[bm_i].ring = cnf.ring_curr;
		CURR_LINE->lflag |= (bm_i << BM_BIT_SHIFT) & LSTAT_BM_BITS;
		HIST_LOG(LOG_INFO, "bookmark %d, bit set, ring %d (original lineno %d))",
			bm_i, cnf.bookmark[bm_i].ring, CURR_FILE.lineno);

		/* prepare sample of [block-name: ]sample-string-from-the-line
		*/
		bn = block_name(cnf.ring_curr);
		if (bn != NULL && bn[0] != '\0') {
			blen = strlen(bn);
			csere(&sample, &slen, 0, slen, bn, blen);
			csere(&sample, &slen, slen, 0, ": ", 2);
			FREE(bn); bn = NULL;
		}
		csere(&sample, &slen, slen, 0, CURR_LINE->buff, CURR_LINE->llen);
		if (sample) {
			if (slen>0 && sample[slen-1]=='\n') {
				sample[--slen] = '\0';	/* remove trailing newline */
			}
			strip_blanks (0x7, sample, &slen);	/* strip and squeeze (tracemsg doesn't like tabs) */
			strncpy(cnf.bookmark[bm_i].sample, sample, sizeof(cnf.bookmark[bm_i].sample)-1);
			cnf.bookmark[bm_i].sample[sizeof(cnf.bookmark[bm_i].sample)-1] = '\0';
			FREE(sample); sample = NULL;
		}

		tracemsg("bookmark %d set", bm_i);
	}

	return;
}

/* internal function for bookmark clear
*/
void
clr_bookmark (int bm_i)
{
	int ri=0;
	int bm_bits;
	LINE *lx = NULL;

	if (bm_i > 0 && bm_i < 10) {
		ri = cnf.bookmark[bm_i].ring;
		if ((ri >= 0) && (ri < RINGSIZE) && (cnf.fdata[ri].fflag & FSTAT_OPEN)) {

			/* clear bookmark bits if exact match
			 * (known limitation: one line -- one bookmark)
			 */
			bm_bits = (bm_i << BM_BIT_SHIFT) & LSTAT_BM_BITS;
			lx = cnf.fdata[ri].top->next;
			while (TEXT_LINE(lx)) {
				if ((lx->lflag & LSTAT_BM_BITS) == bm_bits) {
					lx->lflag &= ~LSTAT_BM_BITS;
					HIST_LOG(LOG_INFO, "bookmark %d, bit found and cleared", bm_i);
				}
				lx = lx->next;
			}
		}
		HIST_LOG(LOG_INFO, "bookmark %d, cleared, ring %d", bm_i, ri);
		cnf.bookmark[bm_i].ring = -1;
		cnf.bookmark[bm_i].sample[0] = '\0';
	}
	return;
}

/* internal function : clear bookmark before line remove
*/
void
clr_opt_bookmark (void)
{
	int bm_i;
	if (CURR_LINE->lflag & LSTAT_BM_BITS) {
		bm_i = (CURR_LINE->lflag & LSTAT_BM_BITS) >> BM_BIT_SHIFT;
		CURR_LINE->lflag &= ~LSTAT_BM_BITS;
		if (bm_i > 0 && bm_i < 10) {
			cnf.bookmark[bm_i].ring = -1;
			cnf.bookmark[bm_i].sample[0] = '\0';
		}
	}
	return;
}

/* internal function for jump to bookmark, interactive mode if !force
*/
int
jump2_bookmark (int bm_i, int force)
{
	static int bm_flag = -1;
	int ri=0;
	int bm_bits=0, lineno=0;
	LINE *lx = NULL;
	int ret=1;

	if (bm_i > 0 && bm_i < 10) {
		ri = cnf.bookmark[bm_i].ring;
		if (ri < 0 || ri >= RINGSIZE) {
			if (!force) tracemsg("bookmark %d not set", bm_i);
		} else if (cnf.fdata[ri].fflag & FSTAT_OPEN) {
			bm_bits = (bm_i << BM_BIT_SHIFT) & LSTAT_BM_BITS;
			if (!force && bm_flag != bm_i) {
				/* highlight first the sample text -- interactive mode
				*/
				tracemsg("bookmark %d: %s", bm_i, cnf.bookmark[bm_i].sample);
				bm_flag = bm_i;
				ret = 0;
			} else {
				/* go to the reference (if not yet there)
				*/
				if ((CURR_LINE->lflag & LSTAT_BM_BITS) != bm_bits) {
					lx = cnf.fdata[ri].top->next;
					lineno = 1;
					while (TEXT_LINE(lx)) {
						if ((lx->lflag & LSTAT_BM_BITS) == bm_bits)
							break;
						lx = lx->next;
						lineno++;
					}
					if (TEXT_LINE(lx)) {
						HIST_LOG(LOG_INFO, "bookmark %d, reached, ring %d lineno %d",
							bm_i, ri, lineno);
						set_position (ri, lineno, lx);
						ret = 0;
					} else {
						HIST_LOG(LOG_INFO, "bookmark %d, not found -- clearing", bm_i);
						cnf.bookmark[bm_i].ring = -1;
						cnf.bookmark[bm_i].sample[0] = '\0';
					}
				} else {
					/* do not run if not necessary
					 */
					ret = 0;
				}
			}/* bm_flag */
		}/* ri and OPEN */
	}/* bm_i */

	if (ret)
		bm_flag = -1;
	return (ret);
}

/*
* internal function for clear bookmarks and references;
* if ring_i is (-1) then from _all_ files, otherwise only from the one
*/
void
clear_all_bookmarks (int ring_i)
{
	int ri, bm_i;
	LINE *lx = NULL;

	/* clear all ref's from OPEN files
	*/
	for (ri=0; ri < RINGSIZE; ri++) {
		if (ring_i == -1 || ring_i == ri) {
			/*
			 * remove all reference(s) from file "cnf.fdata[ri]"
			 */
			if (cnf.fdata[ri].fflag & FSTAT_OPEN) {
				lx = cnf.fdata[ri].top->next;
				while (TEXT_LINE(lx)) {
					lx->lflag &= ~LSTAT_BM_BITS;
					lx = lx->next;
				}
			}

			/*
			 * remove ring "ri" from bookmark table
			 */
			for (bm_i=0; bm_i < 10; bm_i++) {
				if (cnf.bookmark[bm_i].ring == ri || cnf.bookmark[bm_i].ring < 0) {
					cnf.bookmark[bm_i].ring = -1;
					cnf.bookmark[bm_i].sample[0] = '\0';
				}
			}
		}/* selected ri */
	}

	return;
}

/* push current position to motion history chain
*/
int
mhist_push (int ring_i, int lineno)
{
	MHIST *mhp = NULL;

	if ((mhp = (MHIST *) MALLOC(sizeof(MHIST))) == NULL) {
		return (1);
	}

	if (cnf.mhist_curr == NULL) {
		mhp->prev = NULL;
	} else {
		mhp->prev = cnf.mhist_curr;
	}
	mhp->ring = ring_i;
	mhp->lineno = lineno;

	/* current is the last node, the first to go back */
	cnf.mhist_curr = mhp;
	HIST_LOG(LOG_INFO, "mhist push: ri %d lineno %d", mhp->ring, mhp->lineno);

	return (0);
}

/*
** mhist_pop - skip back one step in motion history, highly experimental
*/
int
mhist_pop (void)
{
	MHIST *mhp = NULL;
	LINE *lp = NULL;
	int ready = 0;

	while (cnf.mhist_curr != NULL && ready==0) {
		mhp = cnf.mhist_curr;
		if ((mhp->ring >= 0) && (mhp->ring < RINGSIZE) &&
		    (cnf.fdata[mhp->ring].fflag & FSTAT_OPEN))
		{
			lp = lll_goto_lineno (mhp->ring, mhp->lineno);
			if (lp != NULL) {
				HIST_LOG(LOG_INFO, "mhist pop: success, ri %d lineno %d", mhp->ring, mhp->lineno);
				set_position (mhp->ring, mhp->lineno, lp);
				ready = 1;
			}
		}
		cnf.mhist_curr = mhp->prev;
		FREE(mhp);
		mhp = NULL;
	}

	if (ready == 0) {
		tracemsg("motion history is empty");
	}

	return (0);
}

/* clear motion history
*/
void
mhist_clear (void)
{
	MHIST *mhp = NULL;

	while (cnf.mhist_curr != NULL) {
		mhp = cnf.mhist_curr;
		cnf.mhist_curr = mhp->prev;
		FREE(mhp);
		mhp = NULL;
	}

	return;
}
