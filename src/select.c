/*
* select.c
* select/unselect lines, cp/rm/mv lines, indent/unindent tools, block based join/split/cut functions
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
#include <unistd.h>	/* write */
#include <errno.h>
#include <syslog.h>
#include "main.h"
#include "proto.h"

/* global config */
extern CONFIG cnf;

/* local proto */
static int shift_engine (int type);
static int lcut_block_engine (int curpos, int left);

/*
** line_select - select line, set border line of range of selected lines,
**	any further calls extend or shrink this range
*/
int
line_select (void)
{
	LINE *lp_next=NULL, *lp_prev=NULL;
	int found_selection=0;

	if (!TEXT_LINE(CURR_LINE)) {
		return (0);
	}

	CURR_FILE.fflag &= ~FSTAT_CMD;

	if (cnf.select_ri != cnf.ring_curr) {
		reset_select();
		cnf.select_ri = cnf.ring_curr;
		cnf.select_w = CURR_FILE.lineno;
		CURR_LINE->lflag |= LSTAT_SELECT;
		return (0);
	}
	/* CURR_FILE/CURR_LINE is ok now */

	/* modify selection */
	if (CURR_LINE->lflag & LSTAT_SELECT) {
		/* remove selection bit from some lines, towards the nearer border */

		/* select the direction to the nearer border */
		lp_prev = lp_next = CURR_LINE;
		while (TEXT_LINE(lp_prev) && (lp_prev->lflag & LSTAT_SELECT) &&
			TEXT_LINE(lp_next) && (lp_next->lflag & LSTAT_SELECT))
		{
			lp_prev = lp_prev->prev;
			lp_next = lp_next->next;
		}

		/* remove the less selected */
		if (TEXT_LINE(lp_prev) && (lp_prev->lflag & LSTAT_SELECT)) {
			lp_next = CURR_LINE->next;
			while (TEXT_LINE(lp_next) && (lp_next->lflag & LSTAT_SELECT)) {
				lp_next->lflag &= ~LSTAT_SELECT;
				lp_next = lp_next->next;
			}
		} else {
			lp_prev = CURR_LINE->prev;
			while (TEXT_LINE(lp_prev) && (lp_prev->lflag & LSTAT_SELECT)) {
				lp_prev->lflag &= ~LSTAT_SELECT;
				lp_prev = lp_prev->prev;
			}
		}

	} else {
		/* extend selection towards the last selection (guess) */

		if (cnf.select_w < CURR_FILE.lineno) {
			lp_prev = CURR_LINE->prev;
			while (TEXT_LINE(lp_prev) && !(lp_prev->lflag & LSTAT_SELECT)) {
				lp_prev = lp_prev->prev;
			}
			if (TEXT_LINE(lp_prev) && (lp_prev->lflag & LSTAT_SELECT)) {
				found_selection = -1;
			} else {
				/* other direction */
				lp_next = CURR_LINE->next;
				while (TEXT_LINE(lp_next) && !(lp_next->lflag & LSTAT_SELECT)) {
					lp_next = lp_next->next;
				}
				if (TEXT_LINE(lp_next) && (lp_next->lflag & LSTAT_SELECT)) {
					found_selection = 1;
				}
			}
		} else {
			lp_next = CURR_LINE->next;
			while (TEXT_LINE(lp_next) && !(lp_next->lflag & LSTAT_SELECT)) {
				lp_next = lp_next->next;
			}
			if (TEXT_LINE(lp_next) && (lp_next->lflag & LSTAT_SELECT)) {
				found_selection = 1;
			} else {
				/* other direction */
				lp_prev = CURR_LINE->prev;
				while (TEXT_LINE(lp_prev) && !(lp_prev->lflag & LSTAT_SELECT)) {
					lp_prev = lp_prev->prev;
				}
				if (TEXT_LINE(lp_prev) && (lp_prev->lflag & LSTAT_SELECT)) {
					found_selection = -1;
				}
			}
		}/*watch*/

		if (found_selection == -1) {
			lp_prev = CURR_LINE->prev;
			while (TEXT_LINE(lp_prev) && !(lp_prev->lflag & LSTAT_SELECT)) {
				lp_prev->lflag |= LSTAT_SELECT;
				lp_prev = lp_prev->prev;
			}
		} else if (found_selection == 1) {
			lp_next = CURR_LINE->next;
			while (TEXT_LINE(lp_next) && !(lp_next->lflag & LSTAT_SELECT)) {
				lp_next->lflag |= LSTAT_SELECT;
				lp_next = lp_next->next;
			}
		}/*found?*/

	}/*modify*/

	CURR_LINE->lflag |= LSTAT_SELECT;
	cnf.select_w = CURR_FILE.lineno;

	return (0);
}

/*
** reset_select - reset line selection
*/
int
reset_select (void)
{
	LINE *lp_next=NULL;

	if (cnf.select_ri == -1) {
		return (0);
	}

	/* top down */
	lp_next = SELECT_FI.top->next;
	while (TEXT_LINE(lp_next)) {
		lp_next->lflag &= ~LSTAT_SELECT;
		lp_next = lp_next->next;
	}
	cnf.select_ri = -1;
	cnf.select_w = 0;

	return (0);
}

/*
** select_all - select all visible lines in current buffer without changing filter bits
*/
int
select_all (void)
{
	int ret=0, count=0;
	LINE *lp=NULL;

	reset_select();

	if (CURR_FILE.num_lines > 0) {
		cnf.select_w = 1;
		cnf.select_ri = cnf.ring_curr;

		lp = CURR_FILE.top->next;
		while (TEXT_LINE(lp)) {
			lp->lflag |= LSTAT_SELECT;
			if (!HIDDEN_LINE(cnf.select_ri,lp))
				count++;
			lp = lp->next;
		}

		if (count==0)
			tracemsg ("file is not empty, but selected lines are not visible");
	}

	return (ret);
}

/*
** go_select_first - move the focus to the first visible line of selection
*/
int
go_select_first (void)
{
	LINE *lp_next=NULL;
	int lineno=0, cnt=0;

	if (cnf.select_ri == -1) {
		tracemsg("no selection");
		return (0);
	}

	lp_next = selection_first_line (&lineno);
	if (TEXT_LINE(lp_next)) {
		if (HIDDEN_LINE(cnf.select_ri,lp_next)) {
			next_lp (cnf.select_ri, &lp_next, &cnt);
			lineno += cnt;
		}
		if (TEXT_LINE(lp_next) && (lp_next->lflag & LSTAT_SELECT)) {
			set_position (cnf.select_ri, lineno, lp_next);
		} else {
			tracemsg ("selection is not visible");
		}
	} else {
		tracemsg("no selection");
		cnf.select_ri = -1;
	}

	return (0);
} /* go_select_first */

/*
* selection_first_line - return the selections first line pointer (or NULL)
*/
LINE *
selection_first_line (int *lineno)
{
	LINE *lp=NULL;

	lp = SELECT_FI.curr_line;
	(*lineno) = SELECT_FI.lineno;
	if (!TEXT_LINE(lp) || !(lp->lflag & LSTAT_SELECT)) {
		if (cnf.select_w < SELECT_FI.lineno) {
			lp = SELECT_FI.curr_line->prev;
			(*lineno) = SELECT_FI.lineno -1;
			while (TEXT_LINE(lp) && !(lp->lflag & LSTAT_SELECT)) {
				lp = lp->prev;
				(*lineno)--;
			}
			if (!TEXT_LINE(lp)) {
				/* other direction */
				lp = SELECT_FI.curr_line->next;
				(*lineno) = SELECT_FI.lineno +1;
				while (TEXT_LINE(lp) && !(lp->lflag & LSTAT_SELECT)) {
					lp = lp->next;
					(*lineno)++;
				}
			}
		} else {
			lp = SELECT_FI.curr_line->next;
			(*lineno) = SELECT_FI.lineno +1;
			while (TEXT_LINE(lp) && !(lp->lflag & LSTAT_SELECT)) {
				lp = lp->next;
				(*lineno)++;
			}
			if (!TEXT_LINE(lp)) {
				/* other direction */
				lp = SELECT_FI.curr_line->prev;
				(*lineno) = SELECT_FI.lineno -1;
				while (TEXT_LINE(lp) && !(lp->lflag & LSTAT_SELECT)) {
					lp = lp->prev;
					(*lineno)--;
				}
			}
		}/*watch*/
	}
	if (TEXT_LINE(lp) && (lp->lflag & LSTAT_SELECT)) {
		while (TEXT_LINE(lp->prev) && ((lp->prev)->lflag & LSTAT_SELECT)) {
			lp = lp->prev;
			(*lineno)--;
		}
	}

	return (lp);
}

/*
** go_select_last - move the focus to the last visible line of selection
*/
int
go_select_last (void)
{
	LINE *lp_prev=NULL;
	int lineno=0, cnt=0;

	if (cnf.select_ri == -1) {
		tracemsg("no selection");
		return (0);
	}

	lp_prev = selection_last_line (&lineno);
	if (TEXT_LINE(lp_prev)) {
		if (HIDDEN_LINE(cnf.select_ri,lp_prev)) {
			prev_lp (cnf.select_ri, &lp_prev, &cnt);
			lineno -= cnt;
		}
		if (TEXT_LINE(lp_prev) && (lp_prev->lflag & LSTAT_SELECT)) {
			set_position (cnf.select_ri, lineno, lp_prev);
		} else {
			tracemsg ("selection is not visible");
		}
	} else {
		tracemsg("no selection");
		cnf.select_ri = -1;
	}

	return (0);
} /* go_select_last */

/*
* selection_last_line - return the selections last line pointer (or NULL)
*/
LINE *
selection_last_line (int *lineno)
{
	LINE *lp=NULL;

	lp = SELECT_FI.bottom->prev;
	*lineno = SELECT_FI.num_lines;
	while (TEXT_LINE(lp)) {
		if (lp->lflag & LSTAT_SELECT)
			break;
		lp = lp->prev;
		(*lineno)--;
	}

	return (lp);
}

/*
* recover_selection - make the selection continuous again
*/
int
recover_selection (void)
{
	LINE *lp=NULL;
	int lineno = 0, first = -1, last = -1;

	/* go forward, search for the first selection line */
	lp = SELECT_FI.top->next;
	lineno = 1;
	while (TEXT_LINE(lp)) {
		if (lp->lflag & LSTAT_SELECT) {
			first = lineno;
			break;
		}
		lp = lp->next;
		lineno++;
	}

	if (!TEXT_LINE(lp)) {
		/* nothing found, no selection */
		cnf.select_w = 0;
		cnf.select_ri = -1;
		return (0);
	}

	/* go back, search for the last selection line */
	lp = SELECT_FI.bottom->prev;
	lineno = SELECT_FI.num_lines;
	while (TEXT_LINE(lp)) {
		if (lp->lflag & LSTAT_SELECT) {
			last = lineno;
			break;
		}
		lp = lp->prev;
		lineno--;
	}

	/* all these lines belong to selection */
	while (TEXT_LINE(lp) && lineno > first) {
		lp->lflag |= LSTAT_SELECT;
		lp = lp->prev;
		lineno--;
	}

	/* check the watch */
	if (cnf.select_w < first)
		cnf.select_w = first;
	else if (cnf.select_w > last)
		cnf.select_w = last;

	return (0);
}

/*
** cp_select - copy visible selection lines to current file after focus line,
**	newly added lines will be the new selection
*/
int
cp_select (void)
{
	LINE *lp_src=NULL, *lp_target=NULL;
	int lineno=0, count=0;

	if (cnf.select_ri == -1) {
		tracemsg ("no selection");
		return (0);
	}
	if (CURR_FILE.fflag & FSTAT_NOADDLIN) {
		tracemsg ("no line addition in this buffer");
		return (0);
	}

	lp_src = selection_first_line (&lineno);
	lp_target = CURR_LINE;
	if ((lp_target == CURR_FILE.bottom) ||
	((lp_target->lflag & LSTAT_SELECT) && (lp_target->next->lflag & LSTAT_SELECT))) {
		tracemsg ("selection copy: target conflict");
		return (0);
	}

	count = cp_select_eng (lp_src, lp_target);
	if (count < 0) {
		tracemsg ("error: selection copy failed (malloc)");
		return (1);
	}

	/* update */
	CURR_FILE.num_lines += count;
	if (count > 0) {
		CURR_FILE.fflag |= FSTAT_CHANGE;
	}

	/* at last */
	cnf.select_ri = cnf.ring_curr;

	return (0);
} /* cp_select */

/*
* cp_select_eng - (engine) copy lines from lp_src to lp_target while the source has select bit,
*	hidden lines are not copied and not counted but the selection bit will be removed
*	return the count, negative value signals malloc error
*/
int
cp_select_eng (LINE *lp_src, LINE *lp_target)
{
	LINE *lx=NULL, *lp_stop_loop=NULL;
	int count=0;

	/* we need endless loop management if src and target is not separated
	 */
	lp_stop_loop = lp_target;

	while (TEXT_LINE(lp_src) && (lp_src->lflag & LSTAT_SELECT)) {
		if (!HIDDEN_LINE(cnf.select_ri,lp_src)) {
			lx = append_line (lp_target, lp_src->buff);
			if (lx == NULL) {
				count = -1;
				break;
			}
			lp_target = lx;

			lp_target->lflag = (lp_src->lflag & ~LSTAT_BM_BITS);
			lp_target->lflag &= ~LSTAT_FMASK;
			lp_target->lflag |= LSTAT_CHANGE;

			count++;
		}
		lp_src->lflag &= ~LSTAT_SELECT;
		if (lp_stop_loop == lp_src) {
			break;
		}
		lp_src = lp_src->next;
	}

	return (count);
}

/*
** rm_select - remove visible selection lines and reset selection
*/
int
rm_select (void)
{
	LINE *lp_first=NULL;
	int lno_first=0, count=0, cnt=0;

	if (cnf.select_ri == -1) {
		tracemsg ("no selection");
		return (0);
	}
	if (SELECT_FI.fflag & FSTAT_NODELIN) {
		tracemsg ("no line delete in this buffer");
		return (0);
	}

	lp_first = selection_first_line (&lno_first);

	/* relocate current? */
	if (SELECT_FI.curr_line->lflag & LSTAT_SELECT) {
		SELECT_FI.curr_line = lp_first;
		prev_lp (cnf.select_ri, &(SELECT_FI.curr_line), &cnt);
		SELECT_FI.lineno = lno_first - cnt;
	}

	count = rm_select_eng (lp_first);

	/* update */
	if (lno_first < SELECT_FI.lineno)
		SELECT_FI.lineno -= count;
	SELECT_FI.num_lines -= count;
	if (count > 0) {
		SELECT_FI.fflag |= FSTAT_CHANGE;
	}

	/* at last */
	cnf.select_ri = -1;
	cnf.select_w = 0;

	return (0);
} /* rm_select */

/*
* rm_select_eng - (engine) remove selection lines top down
*	hidden lines are not removed but selection bit will be cleared,
*	return the removed lines count
*/
int
rm_select_eng (LINE *lp_rm)
{
	int count=0;

	while (TEXT_LINE(lp_rm) && (lp_rm->lflag & LSTAT_SELECT)) {
		if (HIDDEN_LINE(cnf.select_ri,lp_rm)) {
			lp_rm->lflag &= ~LSTAT_SELECT;
			lp_rm = lp_rm->next;
		} else {
			clr_opt_bookmark (lp_rm);
			/* skip to next, if TEXT_LINE */
			lp_rm = lll_rm (lp_rm);	/* in rm_select_eng() */
			count++;
		}
	}

	return (count);
}

/*
** mv_select - move visible selection lines to current file after focuse line,
**	and reset selection if move-reset resource is ON,
**	otherwise newly added lines will be the new selection
*/
int
mv_select (void)
{
	LINE *lp_src=NULL, *lp_target=NULL;
	int lno_src=0, count=0, cnt=0;

	if (cnf.select_ri == -1) {
		tracemsg ("no selection");
		return (0);
	}
	if (CURR_FILE.fflag & FSTAT_NOADDLIN) {
		tracemsg ("no line addition in this buffer");
		return (0);
	}
	if (SELECT_FI.fflag & FSTAT_NODELIN) {
		tracemsg ("no line delete in this buffer");
		return (0);
	}

	lp_src = selection_first_line (&lno_src);
	lp_target = CURR_LINE;
	if (lp_target->lflag & LSTAT_SELECT) {
		tracemsg ("selection move conflict: target line in selection");
		return (0);
	}

	if (cnf.select_ri != cnf.ring_curr) {
		/* relocate current? */
		if (SELECT_FI.curr_line->lflag & LSTAT_SELECT) {
			SELECT_FI.curr_line = lp_src;
			prev_lp (cnf.select_ri, &(SELECT_FI.curr_line), &cnt);
			SELECT_FI.lineno = lno_src - cnt;
		}
	}

	count = mv_select_eng (lp_src, lp_target);

	/* update */
	if (lno_src < SELECT_FI.lineno)
		SELECT_FI.lineno -= count;
	SELECT_FI.num_lines -= count;
	CURR_FILE.num_lines += count;
	if (count > 0) {
		SELECT_FI.fflag |= FSTAT_CHANGE;
		CURR_FILE.fflag |= FSTAT_CHANGE;
	}

	/* at last */
	if (cnf.gstat & GSTAT_MOVES) {
		cnf.select_ri = -1;
		cnf.select_w = 0;
	} else {
		cnf.select_ri = cnf.ring_curr;
		cnf.select_w = lno_src+1;
	}

	return (0);
} /* mv_select */

/*
* mv_select_eng - (engine) move lines from lp_src to lp_target while the source has select bit,
*	hidden lines are not moved and not counted but the selection bit will be removed
*	return the count; no malloc here, only unlink/link in node chains
*/
int
mv_select_eng (LINE *lp_src, LINE *lp_target)
{
	LINE *lp_next_src=NULL;
	int count=0;

	while (TEXT_LINE(lp_src) && (lp_src->lflag & LSTAT_SELECT)) {
		lp_next_src = lp_src->next;
		if (cnf.gstat & GSTAT_MOVES)
			lp_src->lflag &= ~LSTAT_SELECT;
		if (!HIDDEN_LINE(cnf.select_ri,lp_src)) {
			lp_target = lll_mv(lp_src, lp_target);

			lp_target->lflag &= ~(LSTAT_BM_BITS | LSTAT_FMASK);
			lp_target->lflag |= LSTAT_CHANGE;

			count++;
		}
		lp_src = lp_next_src;
	}

	return (count);
}

/*
* wr_select - (engine) write out selection (only visible lines) to file,
* the selection will remain untouched
*/
int
wr_select (int fd, int with_shadow)
{
	LINE *lp_src=NULL;
	int count=0, out=0;
	int shadow=0, length=0;
	char mid_buff[100];

	if (cnf.select_ri == -1) {
		return (-1);
	}

	lp_src = selection_first_line (&count);
	if (TEXT_LINE(lp_src)) {
		if (HIDDEN_LINE(cnf.select_ri,lp_src)) {
			/* skip initial shadow lines */
			next_lp(cnf.select_ri, &lp_src, NULL);
		}
	}
	count = 0;
	while (TEXT_LINE(lp_src) && (lp_src->lflag & LSTAT_SELECT)) {
		if (!HIDDEN_LINE(cnf.select_ri,lp_src)) {
			/* shadow line, optionally */
			if (shadow > 0) {
				if (shadow > 1) {
					snprintf(mid_buff, 30, "--- %d lines ---\n", shadow);
				} else {
					snprintf(mid_buff, 30, "--- 1 line ---\n");
				}
				length = strlen(mid_buff);
				out = write (fd, mid_buff, length);
				if (out != length) {
					SELE_LOG(LOG_ERR, "write (to fd=%d) failed (%d!=%d) (%s)",
						fd, out, length, strerror(errno));
					break;
				}
				count++;
			}
			/* line buffer, important */
			out = write (fd, lp_src->buff, lp_src->llen);
			if (out != lp_src->llen) {
				SELE_LOG(LOG_ERR, "write (to fd=%d) failed (%d!=%d) (%s)",
					fd, out, lp_src->llen, strerror(errno));
				break;
			}
			count++;
			shadow = 0;
		} else if (with_shadow && (cnf.gstat & GSTAT_SHADOW)) {
			shadow++;
		}
		lp_src = lp_src->next;
	}

	return (count);
} /* wr_select */

/*
 * over_select_eng - overwrite visible lines of ('selection') with lines from source,
 *	the more sources, append copies to target,
 *	the more in 'selection', delete rest
 *	relocate 'selection' curr_line upwards if it would be removed otherwise
*/
int
over_select_eng (int src_ri, LINE *lp_src, LINE *lp_src_end)
{
	int target_ri;
	LINE *lp_target=NULL, *lp_target_end=NULL;
	LINE *lp=NULL;
	int src_ready=0, target_ready=0, over=0, insert=0, delete=0, ans=0;
	int lno_first, lno_last, cnt;

	/* the target */
	target_ri = cnf.select_ri;
	lp_target = selection_first_line (&lno_first);
	if (TEXT_LINE(lp_target)) {
		if (HIDDEN_LINE(target_ri, lp_target)) {
			next_lp (target_ri, &lp_target, &cnt);
			lno_first += cnt;
		}
	}
	lp_target_end = selection_last_line (&lno_last);
	if (TEXT_LINE(lp_target_end)) {
		if (HIDDEN_LINE(target_ri, lp_target_end)) {
			prev_lp (target_ri, &lp_target_end, &cnt);
			lno_last -= cnt;
		}
	}
	if (!TEXT_LINE(lp_target) || !TEXT_LINE(lp_target_end)) {
		return -2;
	}
	reset_select();
	SELE_LOG(LOG_DEBUG, "target: lno_first %d [%s] lno_last %d [%s]",
		lno_first, lp_target->buff, lno_last, lp_target_end->buff);

	while (!src_ready && TEXT_LINE(lp_src) && !target_ready && TEXT_LINE(lp_target)) {
		/* in the range of original selection lines,
		 * overwrite the buffer
		 */
		SELE_LOG(LOG_DEBUG, "overwrite: [%s] with [%s]", lp_target->buff, lp_src->buff);
		if (milbuff (lp_target, 0, lp_target->llen, lp_src->buff, lp_src->llen)) {
			ans = -1;
			break;
		}
		lp_target->lflag |= LSTAT_CHANGE;
		over++;

		if (lp_src == lp_src_end)
			src_ready = 1;
		next_lp (src_ri, &lp_src, NULL);

		if (lp_target == lp_target_end)
			target_ready = 1;
		next_lp (target_ri, &lp_target, &cnt);
		lno_first += cnt;
	}
	if (over > 0)
		cnf.fdata[target_ri].fflag |= FSTAT_CHANGE;
	if (ans)
		return ans;
	SELE_LOG(LOG_DEBUG, "after %d overwrite: lno_first %d curr lineno %d", over, lno_first, cnf.fdata[target_ri].lineno);

	if (!src_ready && TEXT_LINE(lp_src)) {
		/* append the rest of source */
		while (!src_ready && TEXT_LINE(lp_src)) {
			if (lp_src == lp_src_end)
				src_ready = 1;

			SELE_LOG(LOG_DEBUG, "insert: [%s]", lp_src->buff);
			lp = insert_line_before (lp_target, lp_src->buff);
			if (lp == NULL) {
				ans = -1;
				break;
			}
			lp->lflag |= LSTAT_CHANGE;
			insert++;

			next_lp (src_ri, &lp_src, NULL);
		}

		/* update */
		if (lno_first <= cnf.fdata[target_ri].lineno)
			cnf.fdata[target_ri].lineno += insert;
		cnf.fdata[target_ri].num_lines += insert;
		if (insert > 0)
			cnf.fdata[target_ri].fflag |= FSTAT_CHANGE;
		SELE_LOG(LOG_DEBUG, "after %d insert: curr lineno %d", insert, cnf.fdata[target_ri].lineno);

	} else if (!target_ready && TEXT_LINE(lp_target)) {
		/* remove the rest of 'selection' */

		/* relocate current (up) if current with lineno would be removed */
		if (lno_first <= cnf.fdata[target_ri].lineno && cnf.fdata[target_ri].lineno <= lno_last) {
			cnf.fdata[target_ri].curr_line = lp_target;
			prev_lp (target_ri, &(cnf.fdata[target_ri].curr_line), &cnt);
			cnf.fdata[target_ri].lineno = lno_first - cnt;
			SELE_LOG(LOG_DEBUG, "after relocate (to first %d - cnt %d): curr lineno %d", lno_first, cnt, cnf.fdata[target_ri].lineno);
		}

		while (!target_ready && TEXT_LINE(lp_target)) {
			if (lp_target == lp_target_end)
				target_ready = 1;

			if (HIDDEN_LINE(target_ri, lp_target)) {
				lp_target = lp_target->next;
			} else {
				SELE_LOG(LOG_DEBUG, "delete: [%s]", lp_target->buff);
				clr_opt_bookmark(lp_target);
				/* skip to next, if TEXT_LINE */
				lp_target = lll_rm (lp_target);	/* in over_select_eng() */
				delete++;
			}
		}
		SELE_LOG(LOG_DEBUG, "after %d delete: curr lineno %d", delete, cnf.fdata[target_ri].lineno);

		/* update */
		if (lno_first < cnf.fdata[target_ri].lineno)
			cnf.fdata[target_ri].lineno -= delete;
		cnf.fdata[target_ri].num_lines -= delete;
		if (delete > 0)
			cnf.fdata[target_ri].fflag |= FSTAT_CHANGE;

	}

	return (ans);
}

/*
** over_select - overwrite visible selection lines with the ones from "*sh*" buffer,
**	command must be launched from the "*sh*" buffer
*/
int
over_select (void)
{
	int src_ri=0, target_ri=0;
	LINE *lp_src=NULL, *lp_src_end=NULL;
	int ans=0;

	/* the source */
	if (strncmp(CURR_FILE.fname, "*sh*", 4) != 0) {
		tracemsg ("selection overwrite only from *sh* buffer");
		return (0);
	}
	src_ri = cnf.ring_curr;			/* buffer will be dropped -- depending on configuration */

	/* the target */
	if (cnf.select_ri == -1) {
		tracemsg ("no selection target");
		return (0);
	}
	target_ri = cnf.select_ri;		/* selection will be reset by engine */
	if (cnf.fdata[target_ri].fflag & FSTAT_CHMASK) {
		tracemsg ("selection is in read/only buffer");
		return (0);
	}
	if (target_ri == src_ri) {
		tracemsg ("selection target and source must be in different buffers");
		return (0);
	}

	/* from the source, copy the whole buffer, all visible lines */
	lp_src = cnf.fdata[src_ri].top;
	next_lp (src_ri, &lp_src, NULL);
	lp_src_end = cnf.fdata[src_ri].bottom;

	/* the engine */
	ans = over_select_eng (src_ri, lp_src, lp_src_end);
	if (ans == -2) {
		tracemsg ("selection has no visible line(s)");
		return (0);
	} else if (ans < 0) {
		tracemsg ("error: selection copy failed (malloc)");
		return (1);
	}

	/* drop source buffer */
	if (cnf.gstat & GSTAT_CLOSE_OVER) {
		cnf.ring_curr = src_ri;
		drop_file();
	}

	/* set focus, content of curr_line and lineno maybe changed */
	cnf.ring_curr = target_ri;
	go_home();
	update_focus(CENTER_FOCUSLINE, cnf.ring_curr);

	return (0);
} /* over_select */

/**************************************** the shift engine **********************************/

#define UNINDENT_LEFT	1
#define INDENT_RIGHT	2
#define SHIFT_LEFT	4
#define SHIFT_RIGHT	8

/*
** unindent_left - shift characters of (visible) selection lines to the left,
**	as long as the first character is whitechar
*/
int
unindent_left (void)
{
	return (shift_engine (UNINDENT_LEFT));
}

/*
** indent_right - shift characters of (visible) selection lines to the right,
**	insert first character(s) according to the indent resource
*/
int
indent_right (void)
{
	return (shift_engine (INDENT_RIGHT));
}

/*
** shift_left - shift characters of (visible) selection lines to the left,
**	until the line is not empty
*/
int
shift_left (void)
{
	return (shift_engine (SHIFT_LEFT));
}

/*
** shift_right - shift characters of (visible) selection lines to the right,
**	the first character will be duplicated if the line is not empty
*/
int
shift_right (void)
{
	return (shift_engine (SHIFT_RIGHT));
}

/*
* shift characters to left or right ... the engine
* send a warning tracemsg() if nothing done
* focus remains, current line/curpos/lncol also
*/
static int
shift_engine (int type)
{
	LINE *lp=NULL;
	int lineno=0, i=0;
	char *first_chars=NULL;
	int prefix=0;
	long mod=0;

	if (cnf.select_ri != cnf.ring_curr) {
		return (0);
	}

	lp = selection_first_line (&lineno);
	if (TEXT_LINE(lp)) {
		if (HIDDEN_LINE(cnf.select_ri,lp)) {
			next_lp (cnf.select_ri, &lp, NULL);
		}
	}
	if (!TEXT_LINE(lp) || !(lp->lflag & LSTAT_SELECT)) {
		tracemsg ("selection not visible");
		return (0);
	}

	if (type == INDENT_RIGHT) {
		for (i=0; i<cnf.indentsize; i++) {
			csere (&first_chars, &prefix, 0, 0, ((cnf.gstat & GSTAT_INDENT) ? "\t" : " "), 1);
		}
	} else if (type == SHIFT_RIGHT) {
		csere (&first_chars, &prefix, 0, 0, " ", 1);
	}

	/* do not change empty lines
	*/
	while (lp->lflag & LSTAT_SELECT) {
		if (lp->llen > 1) {
			switch (type) {
			case UNINDENT_LEFT:
				if (IS_BLANK(lp->buff[0])) {
					(void) milbuff (lp, 0, 1, "", 0);
					mod++;
					lp->lflag |= LSTAT_CHANGE;
				}
				break;
			case INDENT_RIGHT:
				if (milbuff (lp, 0, 0, first_chars, prefix)) {
					break;
				}
				lp->lflag |= LSTAT_CHANGE;
				mod++;
				break;
			case SHIFT_LEFT:
				(void) milbuff (lp, 0, 1, "", 0);
				mod++;
				lp->lflag |= LSTAT_CHANGE;
				break;
			case SHIFT_RIGHT:
				first_chars[0] = lp->buff[0];
				if (milbuff (lp, 0, 0, first_chars, 1)) {
					break;
				}
				mod++;
				lp->lflag |= LSTAT_CHANGE;
				break;
			default:
				break;
			}
		}
		next_lp (cnf.select_ri, &lp, NULL);
	}

	if (first_chars != NULL) {
		FREE(first_chars);
		first_chars = NULL;
	}

	if (mod==0) {
		tracemsg ("nothing shifted");
	} else {
		if (SELECT_FI.curr_line->lflag & LSTAT_SELECT) {
			SELECT_FI.lncol = get_col(SELECT_FI.curr_line, SELECT_FI.curpos);
		}
		SELECT_FI.fflag |= FSTAT_CHANGE;
	}

	return (0);
}

/**************************************** block manipulation tools **********************************/

/*
** pad_block - pad the selection lines one-by-one, fill with space character
**	upto given position or current cursor position
*/
int
pad_block (const char *opt_curpos)
{
	int curpos=0, lineno=0;
	LINE *lp=NULL;
	int ret=0, mod=0;

	if (cnf.select_ri != cnf.ring_curr) {
		return (0);
	}

	if (opt_curpos[0] == '\0') {
		curpos = SELECT_FI.curpos;
	} else {
		curpos = atoi(&opt_curpos[0]);
	}
	if (curpos < 0)
		curpos = 0;

	lp = selection_first_line (&lineno);
	if (TEXT_LINE(lp)) {
		if (HIDDEN_LINE(cnf.select_ri,lp)) {
			next_lp (cnf.select_ri, &lp, NULL);
		}
	}
	if (!TEXT_LINE(lp) || !(lp->lflag & LSTAT_SELECT)) {
		tracemsg ("selection not visible");
		return (0);
	}

	while (lp->lflag & LSTAT_SELECT) {
		ret = pad_line(lp, curpos);
		if (ret < 0) {
			ret = 1;
			break;
		} else if (ret == 0) {
			lp->lflag |= LSTAT_CHANGE;
			SELECT_FI.fflag |= FSTAT_CHANGE;
			mod++;
		}
		ret = 0;
		next_lp (cnf.select_ri, &lp, NULL);
	}

	if (ret) {
		SELE_LOG(LOG_ERR, "selection padding failed: ri=%d ret=%d", cnf.select_ri, ret);
		tracemsg ("padding failed");
	} else if (mod==0) {
		tracemsg ("nothing changed");
	}

	return (0);
}

/*
* pad the LINE upto padsize with space characters
* return: 0 -- ok, 1 -- no change, -1 -- malloc error
*/
int
pad_line (LINE *lp, int padsize)
{
	int lsize=0, start=0, i=0;

	lsize = get_pos(lp, lp->llen-1);
	if (padsize > lsize) {
		start = lp->llen;
		if (milbuff (lp, lp->llen-1, 0, " ", padsize - lsize)) {
			return (-1);
		}
		for (i=start; i < lp->llen-1; i++) {
			lp->buff[i] = ' ';	/* fill with space */
		}
	} else {
		return (1);
	}

	return (0);
}

/*
** cut_block - cut the selection lines one-by-one at given position
**	or at current cursor position
*/
int
cut_block (const char *opt_curpos)
{
	int curpos=0;

	if (cnf.select_ri != cnf.ring_curr) {
		return (0);
	}

	if (opt_curpos[0] == '\0') {
		curpos = SELECT_FI.curpos;
	} else {
		curpos = atoi(&opt_curpos[0]);
	}
	if (curpos < 0)
		curpos = 0;

	/* update! */
	SELECT_FI.curpos = get_pos(SELECT_FI.curr_line, SELECT_FI.lncol);
	if (SELECT_FI.lnoff > SELECT_FI.curpos)
		SELECT_FI.lnoff = SELECT_FI.curpos;

	return (lcut_block_engine (curpos, 0));
}

/*
** left_cut_block - cut the selection lines to the left one-by-one at given position
**	or at current cursor position
*/
int
left_cut_block (const char *opt_curpos)
{
	int curpos=0;

	if (cnf.select_ri != cnf.ring_curr) {
		return (0);
	}

	if (opt_curpos[0] == '\0') {
		curpos = SELECT_FI.curpos;
	} else {
		curpos = atoi(&opt_curpos[0]);
	}
	if (curpos < 0)
		curpos = 0;

	/* update! */
	SELECT_FI.curpos = SELECT_FI.lncol = SELECT_FI.lnoff = 0;

	return (lcut_block_engine (curpos, 1));
}

/*
* left/cut selection engine, to cut characters from curpos to left or to the lineend,
* each line of selection; must be the current file
*/
static int
lcut_block_engine (int curpos, int left)
{
	int lncol=0, lineno=0;
	LINE *lp=NULL;
	int mod=0;

	lp = selection_first_line (&lineno);
	if (TEXT_LINE(lp)) {
		if (HIDDEN_LINE(cnf.select_ri,lp)) {
			next_lp (cnf.select_ri, &lp, NULL);
		}
	}
	if (!TEXT_LINE(lp) || !(lp->lflag & LSTAT_SELECT)) {
		tracemsg ("selection not visible");
		return (0);
	}

	/* like deleol/del2bol, but never remove the line
	*/
	while (lp->lflag & LSTAT_SELECT) {
		lncol = get_col(lp, curpos);
		if (left) {
			if (lncol > 0) {
				(void) milbuff (lp, 0, lncol, "", 0);
				mod++;
				lp->lflag |= LSTAT_CHANGE;
				SELECT_FI.fflag |= FSTAT_CHANGE;
			}
		} else {
			if (lncol < lp->llen-1) {
				(void) milbuff (lp, lncol, lp->llen, "\n", 1);
				mod++;
				lp->lflag |= LSTAT_CHANGE;
				SELECT_FI.fflag |= FSTAT_CHANGE;
			}
		}
		next_lp (cnf.select_ri, &lp, NULL);
	}

	if (mod==0) {
		tracemsg ("nothing changed");
	}

	return (0);
}

/*
** split_block - split selected lines in two separate lines, one-by-one at given position
**	or at current cursor position
*/
int
split_block (const char *opt_curpos)
{
	int curpos=0, lineno=0, lncol=0;
	int ret=0, mod=0;
	LINE *lp_target=NULL, *lx=NULL, *lp_source=NULL;

	if (cnf.select_ri != cnf.ring_curr) {
		return (0);
	}

	if (opt_curpos[0] == '\0') {
		curpos = SELECT_FI.curpos;
	} else {
		curpos = atoi(&opt_curpos[0]);
	}
	if (curpos < 0)
		curpos = 0;

	lp_source = selection_first_line (&lineno);
	if (TEXT_LINE(lp_source)) {
		if (HIDDEN_LINE(cnf.select_ri,lp_source)) {
			next_lp (cnf.select_ri, &lp_source, NULL);
		}
	}
	if (!TEXT_LINE(lp_source) || !(lp_source->lflag & LSTAT_SELECT)) {
		tracemsg ("selection not visible");
		return (0);
	}

	lp_target = selection_last_line (&lineno);
	if (TEXT_LINE(lp_target)) {
		next_lp (cnf.select_ri, &lp_target, NULL); /* first visible line after selection */
	} else {
		tracemsg ("selection not visible");
		return (0);
	}

	SELE_LOG(LOG_DEBUG, "curpos value %d", curpos);
	while (lp_source->lflag & LSTAT_SELECT)
	{
		lncol = get_col(lp_source, curpos);

		/* insert empty line to TARGET
		*/
		lx = insert_line_before (lp_target, "\n");
		if (lx != NULL) {
			mod++;
			lx->lflag |= LSTAT_CHANGE;
			SELECT_FI.fflag |= FSTAT_CHANGE;
			SELECT_FI.num_lines++;
		} else {
			ret = 1;
			break;
		}

		if (lncol < lp_source->llen-1) {
			SELE_LOG(LOG_DEBUG, "lncol %d < lp->llen-1 %d (insert %d bytes)",
				lncol, lp_source->llen, lp_source->llen-1-lncol);
			/* copy data from SOURCE to TARGET, bytes from lncol
			*/
			if (milbuff (lx, 0, 0, &lp_source->buff[lncol], lp_source->llen-1-lncol)) {
				ret = 2;
				break;
			}
			/* realloc SOURCE, cut bytes from lncol to line end
			*/
			SELE_LOG(LOG_DEBUG, "(source) strip bytes from lncol to line end");
			(void) milbuff (lp_source, lncol, lp_source->llen, "\n", 1);
			lp_source->lflag |= LSTAT_CHANGE;
		} else {
			SELE_LOG(LOG_DEBUG, "lncol %d >= lp->llen-1 %d (do nothing)",
				lncol, lp_source->llen-1);
		}

		next_lp (cnf.select_ri, &lp_source, NULL);
	}

	if (ret) {
		SELE_LOG(LOG_ERR, "failed: ri=%d ret=%d", cnf.select_ri, ret);
		tracemsg ("split operation failed");
	} else if (mod==0) {
		tracemsg ("nothing changed");
	}

	return (ret);
}

/*
** join_block - join two blocks of selection lines one-by-one, the separator line is either
**	chosen by the given regex pattern or the first empty line, if nothing passed
*/
int
join_block (const char *separator)
{
	int ret=0;
	regex_t reg;
	char expr_tmp[XPATTERN_SIZE];
	char expr_new[XPATTERN_SIZE];
	char errbuff[ERRBUFF_SIZE];
	regmatch_t pmatch;
	LINE *lp_target=NULL, *lx=NULL, *lp_source=NULL;
	int lineno=0, cnt=0, count=0, mod=0;
	int padsize=0, lsize=0;

	/* selection ring index must be the current */
	if (cnf.select_ri != cnf.ring_curr) {
		return (0);
	}

	memset (errbuff, 0, ERRBUFF_SIZE);
	if (separator[0] == '\0') {
		strncpy (expr_tmp, "^$", 20);
	} else if (separator[0] == '^') {
		strncpy (expr_tmp, separator, sizeof(expr_tmp));
		expr_tmp[sizeof(expr_tmp)-1] = '\0';
	} else {
		expr_tmp[0] = '^';
		strncpy (expr_tmp+1, separator, sizeof(expr_tmp)-1);
		expr_tmp[sizeof(expr_tmp)-1] = '\0';
	}
	regexp_shorthands (expr_tmp, expr_new, sizeof(expr_new));
	SELE_LOG(LOG_DEBUG, "separator expression [%s]", expr_new);

	ret = regcomp (&reg, expr_new, REGCOMP_OPTION);
	if (ret) {
		regerror(ret, &reg, errbuff, ERRBUFF_SIZE);
		SELE_LOG(LOG_ERR, "pattern [%s]: regcomp failed (%d): %s", expr_new, ret, errbuff);
		tracemsg("%s", errbuff);
		return (1);
	}

	/* upper part of selection starts with lp_target
	*/
	lp_target = selection_first_line (&lineno);
	if (TEXT_LINE(lp_target)) {
		if (HIDDEN_LINE(cnf.ring_curr,lp_target)) {
			next_lp (cnf.ring_curr, &lp_target, &cnt);
			lineno += cnt;
		}
	}
	if (!TEXT_LINE(lp_target) || !(lp_target->lflag & LSTAT_SELECT)) {
		SELE_LOG(LOG_DEBUG, "selection not visible");
		tracemsg ("selection not visible");
		return (0);
	}
	SELE_LOG(LOG_DEBUG, "target lineno %d", lineno);

	/* count down upto separator
	*/
	count = 0;
	lx = lp_target;
	while (lx->lflag & LSTAT_SELECT)
	{
		if (!regexec(&reg, lx->buff, 1, &pmatch, 0)) {
			SELE_LOG(LOG_DEBUG, "match? lineno %d -- so:%ld eo:%ld", lineno, (long)pmatch.rm_so, (long)pmatch.rm_eo);
			if (pmatch.rm_so >= 0 && (pmatch.rm_eo == 0 || pmatch.rm_so < pmatch.rm_eo)) {
				break;	/* ok, match */
			}
		}
		lsize = get_pos(lx, lx->llen-1);
		if (padsize < lsize) {
			padsize = lsize;
		}
		count++;
		next_lp (cnf.ring_curr, &lx, &cnt);
		lineno += cnt;
	}
	regfree (&reg);

	if (!(TEXT_LINE(lx)) || !(lx->lflag & LSTAT_SELECT)) {
		SELE_LOG(LOG_DEBUG, "separator line not found");
		tracemsg ("separator line not found");
		return (0);
	}
	SELE_LOG(LOG_DEBUG, "separator reached, lineno %d (old watch %d) padsize %d", lineno, cnf.select_w, padsize);
	cnf.select_w = lineno;

	if (CURR_FILE.lineno > cnf.select_w) {
		SELE_LOG(LOG_DEBUG, "change current line from %d to %d", CURR_FILE.lineno, cnf.select_w);
		CURR_LINE = lx;
		CURR_FILE.lineno = cnf.select_w;
	}

	/* the next visible after separator is lp_source
	*/
	lp_source = lx;
	next_lp (cnf.ring_curr, &lp_source, &cnt);
	lineno += cnt;
	SELE_LOG(LOG_DEBUG, "source lineno %d (target lines count %d)", lineno, count);

	/* start joining: lp_target + lp_source
	*/
	while (count > 0 && (lp_source->lflag & LSTAT_SELECT))
	{
		count--;
		if (milbuff (lp_target, lp_target->llen-1, 1, lp_source->buff, lp_source->llen)) {
			ret = 2;
			break;
		}
		if (lp_source->llen > 1)
			lp_target->lflag |= LSTAT_CHANGE;

		/* remove */
		clr_opt_bookmark(lp_source);
		lp_source = lll_rm(lp_source);		/* in join_block() */
		if (HIDDEN_LINE(cnf.ring_curr,lp_source)) {
			next_lp (cnf.ring_curr, &lp_source, NULL);
		}

		/* update */
		SELECT_FI.num_lines--;
		SELECT_FI.fflag |= FSTAT_CHANGE;

		mod++;
		next_lp (cnf.ring_curr, &lp_target, &cnt);
		lineno += cnt;
	}
	SELE_LOG(LOG_DEBUG, "one-to-one session finished (current %d) mod=%d", lineno, mod);

	/* lp_target is the separator, insert rest of block before this line
	*/
	if (ret == 0 && (TEXT_LINE(lp_source)) && (lp_source->lflag & LSTAT_SELECT)) {
		while (lp_source->lflag & LSTAT_SELECT)
		{
			lx = insert_line_before (lp_target, "\n");
			if (lx == NULL) {
				ret = 3;
				break;
			}
			pad_line(lx, padsize);
			if (milbuff (lx, lx->llen-1, 1, lp_source->buff, lp_source->llen)) {
				ret = 3;
				break;
			}
			lineno++;

			/* update */
			lx->lflag |= LSTAT_SELECT;
			lx->lflag |= LSTAT_CHANGE;
			SELECT_FI.fflag |= FSTAT_CHANGE;

			/* remove */
			clr_opt_bookmark(lp_source);
			lp_source = lll_rm(lp_source);		/* in join_block() */
			if (HIDDEN_LINE(cnf.ring_curr,lp_source)) {
				next_lp (cnf.ring_curr, &lp_source, NULL);
			}

			mod++;
		}
		SELE_LOG(LOG_DEBUG, "inserting rest of sources finished (current %d) mod=%d", lineno, mod);
	}

	if (ret) {
		SELE_LOG(LOG_ERR, "failed: ri=%d ret=%d", cnf.ring_curr, ret);
		tracemsg ("join operation failed");
	} else if (mod==0) {
		tracemsg ("nothing changed");
	} else {
		/* update CURR_FILE lncol and curpos */
		if (CURR_FILE.lncol >= CURR_LINE->llen-1)
			go_end();
		update_curpos(cnf.ring_curr);
	}

	return (ret);
}
