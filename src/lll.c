/*
* lll.c
* linked list library :-)
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
#include <stdlib.h>	/* malloc, realloc, free */
#include "main.h"
#include "proto.h"

/* global config */
extern CONFIG cnf;

/*
 * add new element (after line_p if not NULL)
 * return with the pointer to this element
 * or NULL if error
 */
LINE *
lll_add (LINE *line_p)
{
	LINE *line_next;

	if ((line_next = (LINE *) MALLOC(sizeof(LINE))) == NULL) {
		ERRLOG(0xE02C);
		return NULL;
	}
	if (line_p == NULL) {
		/* root of chain */
		line_next->next = NULL;
		line_next->prev = NULL;
		line_next->buff = NULL;
		line_next->llen = 0;
		line_next->lflag = 0;
	} else {
		/* bind-in after line_p */
		line_next->next = line_p->next;	/*save*/
		line_p->next = line_next;
		line_next->prev = line_p;
		if (line_next->next != NULL)
			(line_next->next)->prev = line_next;
		line_next->buff = NULL;
		line_next->llen = 0;
		line_next->lflag = 0;
	}

	return line_next;
}

/*
 * add new element before line_p
 * return with the pointer to this element
 * or NULL if error
 */
LINE *
lll_add_before (LINE *line_p)
{
	LINE *line_prev;

	if (line_p == NULL) {
		line_prev = lll_add(line_p);
	} else {
		if ((line_prev = (LINE *) MALLOC(sizeof(LINE))) == NULL) {
			ERRLOG(0xE02B);
			return NULL;
		}

		/* bind-in before line_p */
		line_prev->prev = line_p->prev;	/*save*/
		line_p->prev = line_prev;
		line_prev->next = line_p;
		if (line_prev->prev != NULL)
			(line_prev->prev)->next = line_prev;
		line_prev->buff = NULL;
		line_prev->llen = 0;
		line_prev->lflag = 0;
	}

	return line_prev;
}

/*
 * remove line_p (if not NULL)
 * return with the next element (or, if this is NULL, with the prev one)
 */
LINE *
lll_rm (LINE *line_p)
{
	LINE *line_x;

	if (line_p == NULL) {
		return (NULL);

	} else if (line_p->next != NULL) {
		line_x = line_p->next;		/* save to return */
		line_x->prev = line_p->prev;
		if (line_x->prev != NULL)
			(line_x->prev)->next = line_x;
		FREE(line_p->buff);
		line_p->buff = NULL;
		FREE(line_p);
		line_p = NULL;

	} else {
		/* line_p->next == NULL */
		line_x = line_p->prev;		/* save to return */
		if (line_x != NULL) {
			line_x->next = NULL;
		}
		FREE(line_p->buff);
		line_p->buff = NULL;
		FREE(line_p);
		line_p = NULL;
	}

	return (line_x);
}

/*
 * move lp_src after lp_trg
 * return with the pointer to this element (lp_src becomes lp_trg->next)
 * or NULL on error
 */
LINE *
lll_mv (LINE *lp_src, LINE *lp_trg)
{
	LINE *line_x;

	if (lp_src == NULL || lp_trg == NULL)
		return (NULL);

	/* link-out element */
	if (lp_src->next != NULL) {
		line_x = lp_src->next;	/*save*/
		line_x->prev = lp_src->prev;
		if (line_x->prev != NULL)
			(line_x->prev)->next = line_x;
	} else {
		/* lp_src->next == NULL */
		line_x = lp_src->prev;
		if (line_x != NULL) {
			line_x->next = NULL;
		}
	}

	/* bind-in lp_src after lp_trg */
	lp_src->next = lp_trg->next;
	lp_trg->next = lp_src;
	lp_src->prev = lp_trg;
	if (lp_src->next != NULL)
		(lp_src->next)->prev = lp_src;

	return (lp_src);
}

/*
 * move lp_src before lp_trg
 * return with the pointer to this element (lp_src becomes lp_trg->prev)
 * or NULL on error
 */
LINE *
lll_mv_before (LINE *lp_src, LINE *lp_trg)
{
	LINE *line_x;

	if (lp_src == NULL || lp_trg == NULL)
		return (NULL);

	/* link-out element */
	if (lp_src->next != NULL) {
		line_x = lp_src->next;	/*save*/
		line_x->prev = lp_src->prev;
		if (line_x->prev != NULL)
			(line_x->prev)->next = line_x;
	} else {
		/* lp_src->next == NULL */
		line_x = lp_src->prev;
		if (line_x != NULL) {
			line_x->next = NULL;
		}
	}

	/* bind-in lp_src before lp_trg */
	lp_src->prev = lp_trg->prev;
	lp_trg->prev = lp_src;
	lp_src->next = lp_trg;
	if (lp_src->prev != NULL)
		(lp_src->prev)->next = lp_src;

	return (lp_src);
}

/*
 * go to absolute line number (counter start with 1, TOP is 0, BOTTOM is num_lines+1)
 * return with the pointer to this element or NULL if out of range
 * (optimized for speed)
 */
LINE *
lll_goto_lineno (int ri, int lineno)
{
	int cnt=0;
	LINE *lp=NULL;

	if (ri < 0 || ri >= RINGSIZE || !(cnf.fdata[ri].fflag & FSTAT_OPEN))
		return NULL;

	if (lineno < 0 || lineno > cnf.fdata[ri].num_lines+1)
		return NULL;

	/* special cases here: top, bottom and curr_line also
	*/
	if (lineno == 0) {
		return (cnf.fdata[ri].top);
	} else if (lineno == cnf.fdata[ri].num_lines+1) {
		return (cnf.fdata[ri].bottom);
	} else if (lineno == cnf.fdata[ri].lineno) {
		return (cnf.fdata[ri].curr_line);
	}

	/* loop stops if lp is top or bottom
	*/
	if (lineno < cnf.fdata[ri].lineno) {
		/* target between top and curr_line */
		if (lineno < cnf.fdata[ri].lineno - lineno) {
			/* top: increment */
			lp = cnf.fdata[ri].top->next;
			cnt = 1;
			for (; cnt<lineno && TEXT_LINE(lp); cnt++) {
				lp = lp->next;
			}
		} else {
			/* curr_line: decrement */
			lp = cnf.fdata[ri].curr_line->prev;
			cnt = cnf.fdata[ri].lineno-1;
			for (; cnt>lineno && TEXT_LINE(lp); cnt--) {
				lp = lp->prev;
			}
		}
	} else if (lineno > cnf.fdata[ri].lineno) {
		/* target between curr_line and bottom */
		if (cnf.fdata[ri].num_lines - lineno < lineno - cnf.fdata[ri].lineno) {
			/* bottom: decrement */
			lp = cnf.fdata[ri].bottom->prev;
			cnt = cnf.fdata[ri].num_lines;
			for (; cnt>lineno && TEXT_LINE(lp); cnt--) {
				lp = lp->prev;
			}
		} else {
			/* curr_line: increment */
			lp = cnf.fdata[ri].curr_line->next;
			cnt = cnf.fdata[ri].lineno+1;
			for (; cnt<lineno && TEXT_LINE(lp); cnt++) {
				lp = lp->next;
			}
		}
	}

	return (lp);
}
