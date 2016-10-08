/*
* filter.c
* base and advanced functions for filtered view and edit (like a builtin egrep)
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
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <features.h>

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include "main.h"
#include "proto.h"

/* global config */
extern CONFIG cnf;

/* local proto */
static int filter_func_bottom_up (int action, int fmask);
static int filter_func_top_down (int action, int fmask);

/*
* base movement: move to next line point,
* return 1 if not possible (NULL), 0 else (ok)
*/
int
next_lp (int ri, LINE **linep_p, int *count)
{
	LINE *lx;
	int cnt=0, fmask=0;

	/* fmask for the lines */
	fmask = LMASK(ri);

	cnt = 0;
	lx = *linep_p;
	if (lx == NULL) {
		FILT_LOG(LOG_CRIT, "assert, start line lx==NULL");
		return (0);
	}
	while ( (lx->next != NULL) && !(lx->lflag & LSTAT_BOTTOM) ) {
		lx = lx->next;
		cnt++;
		/* take it, if not masked */
		if ( !(lx->lflag & fmask) )
			break;
	}

	*linep_p = lx;
	if (count != NULL)
		*count = cnt;
	return (1);
} /* next_lp */

/*
* base movement: move to prev line point,
* return 1 if not possible (NULL), 0 else (ok)
*/
int
prev_lp (int ri, LINE **linep_p, int *count)
{
	LINE *lx;
	int cnt=0, fmask=0;

	/* fmask for the lines */
	fmask = LMASK(ri);

	cnt = 0;
	lx = *linep_p;
	if (lx == NULL) {
		FILT_LOG(LOG_CRIT, "assert, start line lx==NULL");
		return (0);
	}
	while ( (lx->prev != NULL) && !(lx->lflag & LSTAT_TOP) ) {
		lx = lx->prev;
		cnt++;
		/* take it, if not masked */
		if ( !(lx->lflag & fmask) )
			break;
	}

	*linep_p = lx;
	if (count != NULL)
		*count = cnt;
	return (1);
} /* prev_lp  */

/* ------------------------------------------------------------------ */

/*
** filter_all - make all lines visible according to the parameter, those and only those;
**	special arguments are "alter" mentioning altered lines, "selection",
**	"function" mentioning functions and headers, and ":<linenumber>",
**	otherwise argument is handled as regexp
*/
int
filter_all (const char *expr)
{
	return (filter_base(FILTER_ALL, expr));
} /* filter_all */

/*
** filter_more - make more lines visible according to the parameter;
**	special arguments are "alter" mentioning altered lines, "selection",
**	"function" mentioning functions and headers, and ":<linenumber>",
**	otherwise the argument is handled as regexp
*/
int
filter_more (const char *expr)
{
	return (filter_base(FILTER_MORE, expr));
} /* filter_more */

/*
** filter_less - make less lines visible according to the parameter;
**	special arguments are "alter" mentioning altered lines, "selection",
**	"function" mentioning functions and headers, and ":<linenumber>",
**	otherwise the argument is handled as regexp
*/
int
filter_less (const char *expr)
{
	return (filter_base(FILTER_LESS, expr));
} /* filter_less */

/* ------------------------------------------------------------------ */

/*
* filter engine for 'all', 'less' and 'more';
* "all //" will show all lines, "less //" will show no lines, "more //" does nothing;
* special arguments are "alter", "selection", "function" and ":<linenumber>",
* otherwise argument is handled as regexp
*/
int
filter_base (int action, const char *expr)
{
	unsigned len;
	int fmask, lineno;
	int cnt=0, ret=0;
	LINE *lx;

	/* activate filter */
	CURR_FILE.fflag |= FMASK(CURR_FILE.flevel);
	fmask = FMASK(CURR_FILE.flevel);
	len = strlen(expr);

	if (len==0) {
		if (action == FILTER_ALL) {
			/* view all lines */
			lx = CURR_FILE.top->next;
			while (TEXT_LINE(lx)) {
				lx->lflag &= ~fmask;
				lx = lx->next;
			}
			ret = 0;
		} else if (action == FILTER_LESS) {
			/* hide all lines */
			lx = CURR_FILE.top->next;
			while (TEXT_LINE(lx)) {
				lx->lflag |= fmask;
				lx = lx->next;
			}
			ret = 0;
		} else {
			/* nothing to do for more */
			ret = 0;
		}

	} else {
		/* altered lines */
		if (strncmp(expr, "alter", len)==0) {
			lx = CURR_FILE.top->next;
			while (TEXT_LINE(lx)) {
				if (lx->lflag & (LSTAT_ALTER | LSTAT_CHANGE)) {
					if (action & (FILTER_MORE | FILTER_ALL))
						lx->lflag &= ~fmask;
					else if (action & FILTER_LESS)
						lx->lflag |= fmask;
				} else if (action & FILTER_ALL) {
					lx->lflag |= fmask;
				}
				lx = lx->next;
			}
			ret = 0;

		} else if (strncmp(expr, "selection", len)==0) {
			lx = CURR_FILE.top->next;
			lineno = 1;
			while (TEXT_LINE(lx)) {
				if (lx->lflag & LSTAT_SELECT) {
					if (action & (FILTER_MORE | FILTER_ALL))
						lx->lflag &= ~fmask;
					else if (action & FILTER_LESS)
						lx->lflag |= fmask;
				} else if (action & FILTER_ALL) {
					lx->lflag |= fmask;
				}
				lx = lx->next;
				lineno++;
			}
			ret = 0;

		} else if (strncmp(expr, "function", len)==0) {
			ret = filter_func (action, fmask);

		} else if (expr[0] == ':') {
			ret = 1;
			lineno = atoi(&expr[1]);
			if (lineno >= 1 && lineno <= CURR_FILE.num_lines) {
				lx = lll_goto_lineno (cnf.ring_curr, lineno);
				if (TEXT_LINE(lx)) {
					CURR_LINE = lx;
					CURR_FILE.lineno = lineno;
					ret = 0;
				}
			}
			if (ret == 0) {
				if (action & (FILTER_MORE | FILTER_ALL))
					lx->lflag &= ~fmask;
				else if (action & FILTER_LESS)
					lx->lflag |= fmask;
			}/* no else branch here! */

		} else {
			/* regcomp fail is possible */
			ret = filter_regex (action, fmask, expr);
		}
	}/* len */

	/* skip to next if line not visible, and
	 * pull focus line to middle
	 */
	if (HIDDEN_LINE(cnf.ring_curr,CURR_LINE)) {
		lx = CURR_LINE;
		next_lp (cnf.ring_curr, &lx, &cnt);
		if (TEXT_LINE(lx)) {
			CURR_LINE = lx;
			CURR_FILE.lineno += cnt;
		} else {
			lx = CURR_LINE;
			prev_lp (cnf.ring_curr, &lx, &cnt);
			CURR_LINE = lx;
			CURR_FILE.lineno -= cnt;
		}
	}
	update_focus(FOCUS_AVOID_BORDER, cnf.ring_curr);

	/* calculate lncol after skip down */
	CURR_FILE.lncol = get_col(CURR_LINE, CURR_FILE.curpos);

	return (ret);
} /* filter_base */

/*
* view or hide function headers: more or less
*/
int
filter_func (int action, int fmask)
{
	int ret=0;

	if (CURR_FILE.ftype == C_FILETYPE) {
		ret = filter_func_bottom_up (action, fmask);
	} else if (CURR_FILE.ftype == PERL_FILETYPE ||
		CURR_FILE.ftype == TCL_FILETYPE ||
		CURR_FILE.ftype == SHELL_FILETYPE ||
		CURR_FILE.ftype == PYTHON_FILETYPE ||
		CURR_FILE.ftype == TEXT_FILETYPE)
	{
		ret = filter_func_top_down (action, fmask);
	} else {
		ret = 0;
	}

	return (ret);
}

/*
* view or hide C/C++ function headers: more or less, this algorithm is running from bottom to top
*/
static int
filter_func_bottom_up (int action, int fmask)
{
	LINE *lx=NULL, *lp=NULL;
	int level=IL_NONE;
	regex_t reg1, reg2, reg3;
	const char *expr;
	regmatch_t pmatch[10];	/* match and sub match */

	if (CURR_FILE.ftype == C_FILETYPE) {
		expr = C_HEADER_PATTERN;
		if (regcomp (&reg1, expr, REGCOMP_OPTION)) {
			return (1); /* internal regcomp failed */
		}
		expr = C_STRUCTURE_PATTERN;
		if (regcomp (&reg2, expr, REGCOMP_OPTION)) {
			regfree (&reg1);
			return (1); /* internal regcomp failed */
		}
	} else {
		/* unknown */
		return (0);
	}

	expr = HEADER_PATTERN_END;
	if (regcomp (&reg3, expr, REGCOMP_OPTION)) {
		regfree (&reg1);
		regfree (&reg2);
		return (1); /* internal regcomp failed */
	}

	lx = CURR_FILE.bottom->prev;
	level = IL_NONE;
	while (TEXT_LINE(lx)) {
		if (lx->llen < 1) {
			; /* error */

		} else if (lx->buff[0] == CLOSE_CURBRAC) {
			level = IL_END;
		} else if (lx->buff[0] == OPEN_CURBRAC) {
			level = IL_BEGIN;

		} else if (level == IL_END) {
			/* simple extra check, if block is empty */
			if (lx->llen > 3 && strchr(&lx->buff[0], OPEN_CURBRAC) != NULL) {
				/* safe guess */
				level = IL_BEGIN;
			} else {
				level = IL_INTERN;
			}
		} else if (level == IL_HEADER) {
			level = IL_NONE;

		} else if (CURR_FILE.ftype == C_FILETYPE) {
			if (level == IL_INTERN) {
				if (!regexec(&reg2, lx->buff, 10, pmatch, 0)) {
					level = IL_HEADER;
				}
				else if (!regexec(&reg3, lx->buff, 10, pmatch, 0)) {
					lp = TEXT_LINE(lx->prev) ? lx->prev : NULL;
					if (lx->llen > 3 && lx->buff[0] != ' ' && lx->buff[0] != '\t' &&
					!regexec(&reg1, lx->buff, 10, pmatch, 0))
					{
						level = IL_HEADER;
					}
					else if (lp && lp->llen > 3 && lp->buff[0] != ' ' && lp->buff[0] != '\t' &&
					!regexec(&reg1, lp->buff, 10, pmatch, 0))
					{
						/* lp is a valid header, lx is a good begin */
						level = IL_BEGIN;
					}
				}

			} else if (level == IL_BEGIN) {
				lp = TEXT_LINE(lx->prev) ? lx->prev : NULL;
				if (!regexec(&reg2, lx->buff, 10, pmatch, 0)) {
					level = IL_HEADER;
				}
				else if (lx->llen > 3 && lx->buff[0] != ' ' && lx->buff[0] != '\t' &&
				!regexec(&reg1, lx->buff, 10, pmatch, 0))
				{
					level = IL_HEADER;
				}
				else if (lp && lp->llen > 3 && lp->buff[0] != ' ' && lp->buff[0] != '\t' &&
				!regexec(&reg1, lp->buff, 10, pmatch, 0))
				{
					/* lp is a valid header, lx is still a good begin */
					level = IL_BEGIN;
				} else {
					/* match failed */
					level = IL_NONE;
				}
			}
		}

		/* set flag for hide or clear to make visible */
		if (level & (IL_HEADER | IL_BEGIN | IL_END)) {
			if (action & (FILTER_MORE | FILTER_ALL))
				lx->lflag &= ~fmask;
			else if (action & FILTER_LESS)
				lx->lflag |= fmask;
		} else if (action & FILTER_ALL) {
			lx->lflag |= fmask;
		}

		lx = lx->prev;
	}

	if (CURR_FILE.ftype == C_FILETYPE) {
		regfree (&reg1);
		regfree (&reg2);
	}
	regfree (&reg3);

	return (0);
} /* filter_func_bottom_up */

/*
* view or hide function block headers: more or less, this algorithm is running from top to bottom
*/
static int
filter_func_top_down (int action, int fmask)
{
	LINE *lx=NULL;
	int level=IL_NONE;
	regex_t reg1, reg2;
	const char *expr;
	regmatch_t pmatch[10];	/* match and sub match */

	if (CURR_FILE.ftype == PERL_FILETYPE) {
		expr = PERL_HEADER_PATTERN;
		if (regcomp (&reg1, expr, REGCOMP_OPTION)) {
			return (1); /* internal regcomp failed */
		}
	} else if (CURR_FILE.ftype == TCL_FILETYPE) {
		expr = TCL_HEADER_PATTERN;
		if (regcomp (&reg1, expr, REGCOMP_OPTION)) {
			return (1); /* internal regcomp failed */
		}
	} else if (CURR_FILE.ftype == SHELL_FILETYPE) {
		expr = SHELL_HEADER_PATTERN;
		if (regcomp (&reg1, expr, REGCOMP_OPTION)) {
			return (1); /* internal regcomp failed */
		}
	} else if (CURR_FILE.ftype == PYTHON_FILETYPE) {
		expr = PYTHON_HEADER_PATTERN;
		if (regcomp (&reg1, expr, REGCOMP_OPTION)) {
			return (1); /* internal regcomp failed */
		}
	} else if (CURR_FILE.ftype == TEXT_FILETYPE) {
		expr = TEXT_HEADER_PATTERN;
		if (regcomp (&reg1, expr, REGCOMP_OPTION)) {
			return (1); /* internal regcomp failed */
		}
	} else {
		/* unknown */
		return (0);
	}

	expr = HEADER_PATTERN_END2;
	if (regcomp (&reg2, expr, REGCOMP_OPTION)) {
		regfree (&reg1);
		return (1); /* internal regcomp failed */
	}

	lx = CURR_FILE.top->next;
	level = IL_NONE;
	while (TEXT_LINE(lx)) {
		if (lx->llen < 1) {
			; /* error */

		} else if (CURR_FILE.ftype == PYTHON_FILETYPE) {
			/* simple check, header or not */
			if (!regexec(&reg1, lx->buff, 10, pmatch, 0)) {
				level = IL_HEADER;
			} else {
				level = IL_NONE;
			}
		} else if (CURR_FILE.ftype == TEXT_FILETYPE) {
			/* simple check, header or not */
			if (!regexec(&reg1, lx->buff, 10, pmatch, 0)) {
				level = IL_HEADER;
			} else {
				level = IL_NONE;
			}

		} else if (level == IL_NONE) {
			/* header check -> IL_HEADER or IL_BEGIN */
			if (!regexec(&reg1, lx->buff, 10, pmatch, 0)) {
				if (!regexec(&reg2, lx->buff, 10, pmatch, 0)) {
					/* double match, jump over IL_HEADER */
					level = IL_BEGIN;
				} else {
					/* next line should be IL_BEGIN */
					level = IL_HEADER;
				}
			}

		} else if (level == IL_HEADER) {
			if (lx->buff[0] == OPEN_CURBRAC) {
				level = IL_BEGIN;
			} else {
				/* this is somehow an error */
				level = IL_NONE;
			}

		} else if (level == IL_BEGIN) {
			if (lx->buff[0] == CLOSE_CURBRAC) {
				/* empty block */
				level = IL_END;
			} else {
				level = IL_INTERN;
			}

		} else if (level == IL_INTERN) {
			if (lx->buff[0] == CLOSE_CURBRAC) {
				level = IL_END;
			}

		} else if (level == IL_END) {
			level = IL_NONE;
		}

		/* set flag for hide or clear to make visible */
		if (level & (IL_HEADER | IL_BEGIN | IL_END)) {
			if (action & (FILTER_MORE | FILTER_ALL))
				lx->lflag &= ~fmask;
			else if (action & FILTER_LESS)
				lx->lflag |= fmask;
		} else if (action & FILTER_ALL) {
			lx->lflag |= fmask;
		}

		lx = lx->next;
	}

	regfree (&reg1);
	regfree (&reg2);

	return (0);
} /* filter_func_top_down */

/*
* return current block or function name
* (returned pointer maybe NULL, otherwise must be freed by caller)
*/
char *
block_name (int ri)
{
	char *symbol=NULL;
	LINE *lx=NULL, *lp_end=NULL, *lp=NULL;
	int lineno=0, lncol=0;
	int fmask=0, mask_active=0;

	symbol = (char *) MALLOC(TAGSTR_SIZE);
	if (symbol == NULL) {
		return symbol;
	}
	symbol[0] = '\0';

	if (ri < 0 || ri >= RINGSIZE || !(cnf.fdata[ri].fflag & FSTAT_OPEN)) {
		return (symbol);
	}

	/* general fallback */
	strncpy(symbol, "(unknown)", 10);

	if (CURR_FILE.ftype != C_FILETYPE &&
	CURR_FILE.ftype != PERL_FILETYPE &&
	CURR_FILE.ftype != TCL_FILETYPE &&
	CURR_FILE.ftype != SHELL_FILETYPE)
	{
		return (symbol);
	}

	/* try to find block end... */
	lx = CURR_LINE;
	lineno = CURR_FILE.lineno;
	while (TEXT_LINE(lx)) {
		if (lx->llen > 0 && lx->buff[lncol] == CLOSE_CURBRAC) {
			break;
		}
		lx = lx->next;
		lineno++;
	}

	if (!TEXT_LINE(lx)) {
		FILT_LOG(LOG_DEBUG, "block end not found");
		return (symbol);
	}

	/* block-end found, save it */
	lp_end = lx;

	fmask = FMASK(CURR_FILE.flevel);
	mask_active = (LMASK(cnf.ring_curr) != 0);

	/* try to find block begin... */
	if (mask_active)
		CURR_FILE.fflag &= ~fmask;
	lx = tomatch_eng (lp_end, &lineno, &lncol, TOMATCH_DONT_SET_FOCUS);
	if (mask_active)
		CURR_FILE.fflag |= fmask;

	if (!TEXT_LINE(lx) || lineno > CURR_FILE.lineno+1) {
		FILT_LOG(LOG_DEBUG, "block begin not found");
		return (symbol);
	}
	if (lx->buff[0] == OPEN_CURBRAC) {
		lx = lx->prev;
		if (!TEXT_LINE(lx)) {
			FILT_LOG(LOG_DEBUG, "block header missing");
			return (symbol);
		}
	}

	/* new fallback */
	strncpy(symbol, "(file level)", 13);

	/* on the header line, try to match block name... */
	if (CURR_FILE.ftype == C_FILETYPE) {
		if (lx->llen > 10 && strncmp(&lx->buff[0], "typedef", 7) == 0) {
			regexp_match(&lx->buff[0], C_STRUCTURE_PATTERN, 4, symbol);
			FILT_LOG(LOG_DEBUG, "C typedef ... [%s]", symbol);
		} else {
			if (!regexp_match(&lx->buff[0], C_STRUCTURE_PATTERN, 2, symbol)) {
				; /* ok, match */
				FILT_LOG(LOG_DEBUG, "C structure ... [%s]", symbol);
			} else {
				lp = TEXT_LINE(lx->prev) ? lx->prev : NULL;
				if (lx->llen > 3 && lx->buff[0] != ' ' && lx->buff[0] != '\t' &&
				!regexp_match(&lx->buff[0], C_HEADER_PATTERN, 1, symbol))
				{
					; /* ok, match */
					FILT_LOG(LOG_DEBUG, "C header ... [%s]", symbol);
				}
				else if (lp && lp->llen > 3 && lp->buff[0] != ' ' && lp->buff[0] != '\t' &&
				!regexp_match(&lp->buff[0], C_HEADER_PATTERN, 1, symbol))
				{
					; /* ok, match */
					FILT_LOG(LOG_DEBUG, "C header (prev) ... [%s]", symbol);
				}
			}
		}
	} else if (CURR_FILE.ftype == PERL_FILETYPE) {
		regexp_match(&lx->buff[0], PERL_HEADER_PATTERN, 1, symbol);
		FILT_LOG(LOG_DEBUG, "Perl ... [%s]", symbol);
	} else if (CURR_FILE.ftype == TCL_FILETYPE) {
		regexp_match(&lx->buff[0], TCL_HEADER_PATTERN, 1, symbol);
		FILT_LOG(LOG_DEBUG, "Tcl ... [%s]", symbol);
	} else if (CURR_FILE.ftype == SHELL_FILETYPE) {
		if (lx->llen > 10 && strncmp(&lx->buff[0], "function", 8) == 0) {
			regexp_match(&lx->buff[0], SHELL_HEADER_PATTERN, 1, symbol);
		} else {
			regexp_match(&lx->buff[0], SHELL_HEADER_PATTERN, 2, symbol);
		}
		FILT_LOG(LOG_DEBUG, "Shell ... [%s]", symbol);
	}

	FILT_LOG(LOG_DEBUG, "symbol [%s]", symbol);
	return (symbol);
} /* block_name */

/* ------------------------------------------------------------------ */

/*
** filter_tmp_all - switch between filtered view and full view, showing all lines
*/
int
filter_tmp_all (void)
{
	int cnt=0;

	if (LMASK(cnf.ring_curr)) {
		/* temp view all */
		CURR_FILE.fflag &= ~FMASK(CURR_FILE.flevel);
	} else {
		/* restore filter bit */
		CURR_FILE.fflag |= FMASK(CURR_FILE.flevel);
		if (HIDDEN_LINE(cnf.ring_curr,CURR_LINE)) {
			next_lp (cnf.ring_curr, &(CURR_LINE), &cnt);
			CURR_FILE.lineno += cnt;
			CURR_FILE.lncol = get_col(CURR_LINE, CURR_FILE.curpos);
		}
		update_focus(FOCUS_AVOID_BORDER, cnf.ring_curr);
	}

	return (0);
} /* filter_tmp_all */

/*
** filter_expand_up - expand the range of visible lines upwards
*/
int
filter_expand_up (void)
{
	if (CURR_LINE->lflag & LSTAT_TOP) {
		return (0);
	}

	if ( HIDDEN_LINE(cnf.ring_curr, CURR_LINE->prev) ) {
		/* flevel bit is set, clean it */
		(CURR_LINE->prev)->lflag &= ~LMASK(cnf.ring_curr);
	}
	/* prev line visible, go up */
	CURR_LINE = CURR_LINE->prev;
	CURR_FILE.lineno--;
	CURR_FILE.lncol = get_col(CURR_LINE, CURR_FILE.curpos);

	update_focus(FOCUS_AWAY_TOP, cnf.ring_curr);

	return (0);
} /* filter_expand_up */

/*
** filter_expand_down - expand the range of visible lines downwards
*/
int
filter_expand_down (void)
{
	if (CURR_LINE->lflag & LSTAT_BOTTOM) {
		return (0);
	}

	if ( HIDDEN_LINE(cnf.ring_curr, CURR_LINE->next) ) {
		/* flevel bit is set, clean it */
		(CURR_LINE->next)->lflag &= ~LMASK(cnf.ring_curr);
	}
	/* next line visible, go down */
	CURR_LINE = CURR_LINE->next;
	CURR_FILE.lineno++;
	CURR_FILE.lncol = get_col(CURR_LINE, CURR_FILE.curpos);

	update_focus(FOCUS_AWAY_BOTTOM, cnf.ring_curr);

	return (0);
} /* filter_expand_down */

/*
** filter_restrict - restrict the range of visible lines by the current line
*/
int
filter_restrict (void)
{
	int cnt=0;

	if (!TEXT_LINE(CURR_LINE)) {
		return (0);
	}

	/* set hide bit */
	CURR_LINE->lflag |= LMASK(cnf.ring_curr);

	/* skip to next, always */
	next_lp (cnf.ring_curr, &(CURR_LINE), &cnt);
	CURR_FILE.lineno += cnt;

	CURR_FILE.lncol = get_col(CURR_LINE, CURR_FILE.curpos);

	if (CURR_FILE.focus < TEXTROWS/2)
		update_focus(INCR_FOCUS, cnf.ring_curr);

	return (0);
} /* filter_restrict */

/* ------------------------------------------------------------------ */

/*
** incr_filter_level - increment filter level of this buffer
*/
int
incr_filter_level (void)
{
	int cnt;

	CURR_FILE.flevel++;
	if (FMASK(CURR_FILE.flevel) == 0) {
		/* out, already highest level */
		CURR_FILE.flevel--;
		return (1);
	} else {
		if (HIDDEN_LINE(cnf.ring_curr,CURR_LINE)) {
			next_lp (cnf.ring_curr, &(CURR_LINE), &cnt);
			CURR_FILE.lineno += cnt;
			CURR_FILE.lncol = get_col(CURR_LINE, CURR_FILE.curpos);
		}
		update_focus(FOCUS_AVOID_BORDER, cnf.ring_curr);
	}

	return (0);
} /* incr_filter_level */

/*
** incr2_filter_level - increment filter level and duplicate filter bits also
*/
int
incr2_filter_level (void)
{
	LINE *lx = NULL;
	int fmask0, fmask1;
	fmask0 = FMASK(CURR_FILE.flevel);

	CURR_FILE.flevel++;
	if (FMASK(CURR_FILE.flevel) == 0) {
		/* out, already highest level */
		CURR_FILE.flevel--;
		return (1);
	} else {
		/* copy temp-view-all status bit */
		fmask1 = FMASK(CURR_FILE.flevel);
		if (CURR_FILE.fflag & fmask0) {
			CURR_FILE.fflag |= fmask1;
		} else {
			CURR_FILE.fflag &= ~fmask1;
		}
		lx = CURR_FILE.top->next;
		while (TEXT_LINE(lx)) {
			/* copy hide-line status bit */
			if (lx->lflag & fmask0) {
				lx->lflag |= fmask1;
			} else {
				lx->lflag &= ~fmask1;
			}
			lx = lx->next;
		}
		/* keep focus */
		tracemsg("filter level increased, filter bits copied");
	}

	return (0);
} /* incr2_filter_level */

/*
** decr_filter_level - decrement filter level of this buffer
*/
int
decr_filter_level (void)
{
	int cnt;

	CURR_FILE.flevel--;
	if (CURR_FILE.flevel <= 0) {
		/* out, already lowest level */
		CURR_FILE.flevel++;
		return (1);
	} else {
		if (HIDDEN_LINE(cnf.ring_curr,CURR_LINE)) {
			next_lp (cnf.ring_curr, &(CURR_LINE), &cnt);
			CURR_FILE.lineno += cnt;
			CURR_FILE.lncol = get_col(CURR_LINE, CURR_FILE.curpos);
		}
		update_focus(FOCUS_AVOID_BORDER, cnf.ring_curr);
	}

	return (0);
} /* decr_filter_level */

/*
** decr2_filter_level - decrement filter level and duplicate filter bits also
*/
int
decr2_filter_level (void)
{
	LINE *lx = NULL;
	int fmask0, fmask1;
	fmask0 = FMASK(CURR_FILE.flevel);

	CURR_FILE.flevel--;
	if (CURR_FILE.flevel <= 0) {
		/* out, already lowest level */
		CURR_FILE.flevel++;
		return (1);
	} else {
		/* copy temp-view-all status bit */
		fmask1 = FMASK(CURR_FILE.flevel);
		if (CURR_FILE.fflag & fmask0) {
			CURR_FILE.fflag |= fmask1;
		} else {
			CURR_FILE.fflag &= ~fmask1;
		}
		lx = CURR_FILE.top->next;
		while (TEXT_LINE(lx)) {
			/* copy hide-line status bit */
			if (lx->lflag & fmask0) {
				lx->lflag |= fmask1;
			} else {
				lx->lflag &= ~fmask1;
			}
			lx = lx->next;
		}
		/* keep focus */
		tracemsg("filter level decreased, filter bits copied");
	}

	return (0);
} /* decr2_filter_level */

/*
** tomatch - go to the matching block character;
**	pairs of Parenthesis "()", Square bracket "[]", Curly bracket "{}" and Angle bracket "<>" are searched
*/
int
tomatch (void)
{
	LINE *lp=NULL;
	int lineno=0, lncol=0;
	int o_lineno=0, o_lnoff=0, o_focus=0;

	lp = CURR_LINE;
	if (!TEXT_LINE(lp)) {
		return (0);
	}

	lineno = o_lineno = CURR_FILE.lineno;
	o_focus = CURR_FILE.focus;
	lncol = CURR_FILE.lncol;
	o_lnoff = CURR_FILE.lnoff;

	lp = tomatch_eng (lp, &lineno, &lncol, TOMATCH_SET_FOCUS);
	if (lp != NULL) {
		CURR_LINE = lp;
		CURR_FILE.lineno = lineno;
		CURR_FILE.lncol = lncol;
		update_curpos(cnf.ring_curr);
		if (o_lineno == CURR_FILE.lineno && o_lnoff == CURR_FILE.lnoff && o_focus == CURR_FILE.focus)
			cnf.gstat |= GSTAT_UPDNONE;
	}

	return (0);
} /* tomatch */

/*
* tomatch_eng - (engine) go to the matching block character, skip char constants
*	return the new line pointer (or NULL if anything failed) and set lineno, lncol
*/
LINE *
tomatch_eng (LINE *lp, int *io_lineno, int *io_lncol, int set_focus)
{
	LINE *ret_lp=NULL;
	int lineno, restore_focus, lncol;
	int fcnt, found;
	char chcur, tofind, dir, delim, ch;

	/* initializing */
	lineno = *io_lineno;
	restore_focus = CURR_FILE.focus;
	lncol = *io_lncol;
	fcnt = 0;
	chcur = lp->buff[lncol];
	delim = 0;	/* delimiter count, this what we will get back */
	ch = 0;		/* out of range */
	found = 0;	/* end-condition */

	/* fillup */
	switch (chcur)
	{
	case '(':	tofind = ')';	dir = 1;	break;
	case ')':	tofind = '(';	dir = -1;	break;
	case '[':	tofind = ']';	dir = 1;	break;
	case ']':	tofind = '[';	dir = -1;	break;
	case '{':	tofind = '}';	dir = 1;	break;
	case '}':	tofind = '{';	dir = -1;	break;
	case '<':	tofind = '>';	dir = 1;	break;
	case '>':	tofind = '<';	dir = -1;	break;
	default:
		return (NULL);
	}

	/* run to match */
	if (dir == 1)
	{
		lncol++;	/* initial skip */
		delim++;
		while (TEXT_LINE(lp)) {

			/* search in this line (skip 0x0a) */
			while (lncol < lp->llen-1) {
				ch = lp->buff[lncol];
				/* in-line character constant check */
				if ((ch == '\'') && (lncol+2 < lp->llen-1) &&
				(lp->buff[lncol+2] == '\'')) {
					lncol += 2;
				} else if ((ch == '\'') && (lncol+3 < lp->llen-1) &&
				(lp->buff[lncol+1] == '\\') && (lp->buff[lncol+3] == '\'')) {
					lncol += 3;
				} else {
					if (ch == chcur) {
						++delim;
					} else if (ch == tofind && --delim == 0) {
						found=1;
						break;
					}
				}
				lncol++;
			}
			if (found)
				break;

			/* skip to next line */
			next_lp (cnf.ring_curr, &lp, &fcnt);
			lineno += fcnt;
			if ((cnf.gstat & GSTAT_SHADOW) && (fcnt > 1))
				update_focus(INCR_FOCUS_SHADOW, cnf.ring_curr);
			else
				update_focus(INCR_FOCUS, cnf.ring_curr);
			lncol = 0;
		}/*while*/

	} else {
		lncol--;	/* initial skip */
		delim++;
		while (TEXT_LINE(lp)) {

			/* search in this line */
			while (lncol >= 0) {
				ch = lp->buff[lncol];
				/* in-line character constant check */
				if ((ch == '\'') && (lncol-2 >= 0) &&
				(lp->buff[lncol-2] == '\'')) {
					lncol -= 2;
				} else if ((ch == '\'') && (lncol-3 >= 0) &&
				(lp->buff[lncol-2] == '\\') && (lp->buff[lncol-3] == '\'')) {
					lncol -= 3;
				} else {
					if (ch == chcur) {
						++delim;
					} else if (ch == tofind && --delim == 0) {
						found=1;
						break;
					}
				}
				lncol--;
			}
			if (found)
				break;

			/* skip to prev line */
			prev_lp (cnf.ring_curr, &lp, &fcnt);
			lineno -= fcnt;
			if ((cnf.gstat & GSTAT_SHADOW) && (fcnt > 1))
				update_focus(DECR_FOCUS_SHADOW, cnf.ring_curr);
			else
				update_focus(DECR_FOCUS, cnf.ring_curr);
			lncol = lp->llen-1;
		}/*while*/

	}/*directions*/

	if (found) {
		ret_lp = lp;
		*io_lineno = lineno;
		if (set_focus == TOMATCH_SET_FOCUS) {
			update_focus(FOCUS_AVOID_BORDER, cnf.ring_curr);
		} else {
			CURR_FILE.focus = restore_focus;
		}
		*io_lncol = lncol;
	} else {
		CURR_FILE.focus = restore_focus;
	}

	return (ret_lp);
} /* tomatch_eng */

/*
** forcematch - go to the matching block character like tomatch but searching invisible lines also,
**	the result will be made visible
*/
int
forcematch (void)
{
	LINE *lp=NULL;
	int lineno=0, lncol=0;
	int fmask=0;
	char mask_active=0;

	lp = CURR_LINE;
	if (!TEXT_LINE(lp)) {
		return (0);
	}

	lineno = CURR_FILE.lineno;
	lncol = CURR_FILE.lncol;
	fmask = FMASK(CURR_FILE.flevel);
	mask_active = (LMASK(cnf.ring_curr) != 0);

	if (mask_active) {
		CURR_FILE.fflag &= ~fmask;
	}

	lp = tomatch_eng (lp, &lineno, &lncol, TOMATCH_DONT_SET_FOCUS);
	if (lp != NULL) {
		/* unhide */
		lp->lflag &= ~fmask;

		CURR_LINE = lp;
		CURR_FILE.lineno = lineno;
		CURR_FILE.lncol = lncol;
		update_curpos(cnf.ring_curr);
		update_focus(CENTER_FOCUSLINE, cnf.ring_curr);
	}

	if (mask_active) {
		CURR_FILE.fflag |= fmask;
	}

	return (0);
} /* forcematch */

/*
** fold_block - folding block lines manually, hide/unhide block lines upto the matching block character
*/
int
fold_block (void)
{
	LINE *lp=NULL;
	int lineno=0, lncol=0;
	int fmask=0;
	char mask_active=0, do_unhide=0;

	lp = CURR_LINE;
	if (!TEXT_LINE(lp)) {
		return (0);
	}
	lineno = CURR_FILE.lineno;
	lncol = CURR_FILE.lncol;
	fmask = FMASK(CURR_FILE.flevel);
	mask_active = (LMASK(cnf.ring_curr) != 0);

	if (mask_active) {
		CURR_FILE.fflag &= ~fmask;
	}

	lp = tomatch_eng (lp, &lineno, &lncol, TOMATCH_DONT_SET_FOCUS);
	if (lp != NULL) {

		/* hide or unhide other lines back towards current */
		if (lineno < CURR_FILE.lineno) {
			/* increase to come back */

			/* unhide if one of the lines is hidden: first, first/next, current/prev */
			if ( (lp->lflag & fmask) ||
			((lp->next) && ((lp->next)->lflag & fmask)) ||
			((CURR_LINE->prev)->lflag & fmask) )
				do_unhide = 1;
			lp->lflag &= ~fmask;	/* unhide target, unconditionally */

			lp = lp->next;
			lineno++;
			while (TEXT_LINE(lp) && lineno < CURR_FILE.lineno) {
				lp->lflag = (do_unhide) ? (lp->lflag & ~fmask) : (lp->lflag | fmask);
				lp = lp->next;
				lineno++;
			}
		} else if (lineno > CURR_FILE.lineno) {
			/* decrease to come back */

			/* unhide if one of the lines is hidden: last, last/prev, current/next */
			if ( (lp->lflag & fmask) ||
			((lp->prev) && ((lp->prev)->lflag & fmask)) ||
			((CURR_LINE->next)->lflag & fmask) )
				do_unhide = 1;
			lp->lflag &= ~fmask;	/* unhide target, unconditionally */

			lp = lp->prev;
			lineno--;
			while (TEXT_LINE(lp) && lineno > CURR_FILE.lineno) {
				lp->lflag = (do_unhide) ? (lp->lflag & ~fmask) : (lp->lflag | fmask);
				lp = lp->prev;
				lineno--;
			}
		}

		/* current lineno/focus/lncol does not change */
	}

	if (mask_active) {
		CURR_FILE.fflag |= fmask;
	}

	return (0);
} /* fold_block */

/*
** fold_thisfunc - fold block content around the focus line, hide/unhide block lines;
**	show header and footer lines first, change content visibility othervise
*/
int
fold_thisfunc (void)
{
	LINE *lp_end=NULL;
	LINE *lp_head=NULL;
	LINE *lx=NULL;
	int lineno=0, lncol=0;
	int fmask=0;
	int mask_active=0, do_unhide=0;
	int hidden_to_end=0, hidden_to_head=0;

	fmask = FMASK(CURR_FILE.flevel);
	mask_active = (LMASK(cnf.ring_curr) != 0);

	/* try to find block end... */
	lx = CURR_LINE;
	lineno = CURR_FILE.lineno;
	while (TEXT_LINE(lx)) {
		if (lx->llen > 0 && lx->buff[lncol] == CLOSE_CURBRAC) {
			break;
		}
		lx = lx->next;
		lineno++;
		if (lx->lflag & fmask) hidden_to_end++;
	}

	if (!TEXT_LINE(lx)) {
		FILT_LOG(LOG_DEBUG, "block end not found");
		return (1);	/* end not found */
	}

	/* found, save block-end for the loop */
	lp_end = lx;
	FILT_LOG(LOG_DEBUG, "hidden lines to end %d, end hidden: %d",
		hidden_to_end, ((lp_end->lflag & fmask) != 0));

	if (mask_active) {
		CURR_FILE.fflag &= ~fmask;
	}

	lx = tomatch_eng (lp_end, &lineno, &lncol, TOMATCH_DONT_SET_FOCUS);
	if (!TEXT_LINE(lx)) {
		FILT_LOG(LOG_DEBUG, "block begin not found (tomatch)");
		if (mask_active) {
			CURR_FILE.fflag |= fmask;
		}
		return (1);	/* begin not found */
	} else if (lineno > CURR_FILE.lineno+1) {
		FILT_LOG(LOG_DEBUG, "block begin not found, current (%d) is before block (%d)", CURR_FILE.lineno, lineno);
		if (mask_active) {
			CURR_FILE.fflag |= fmask;
		}
		return (1);	/* begin not found */
	}

	/* found, save block-head for the loop */
	lp_head = lx;

	if (mask_active) {
		CURR_FILE.fflag |= fmask;
	}

	lx = lp_head;
	while (TEXT_LINE(lx)) {
		if (lx == CURR_LINE) {
			break;
		}
		lx = lx->next;
		if (lx->lflag & fmask) hidden_to_head++;
	}

	FILT_LOG(LOG_DEBUG, "hidden lines to head %d, head hidden: %d",
		hidden_to_head, ((lp_head->lflag & fmask) != 0));

	if (((lp_head->lflag & fmask)==0) || ((lp_end->lflag & fmask)==0)) {
		/* header and footer both visible, change internal lines */

		do_unhide = (hidden_to_end + hidden_to_head > 0);

		lx = lp_head->next;
		while (TEXT_LINE(lx) && lx != lp_end) {
			lx->lflag = (do_unhide) ?
				(lx->lflag & ~fmask) : (lx->lflag | fmask);
			lx = lx->next;
		}
	}

	lp_head->lflag &= ~fmask;
	if (lp_head->prev)
		(lp_head->prev)->lflag &= ~fmask;

	lp_end->lflag &= ~fmask;

	CURR_LINE->lflag &= ~fmask;

	/* current lineno/focus/lncol does not change */
	return (0);
} /* fold_thisfunc */
