/*
* search.c
* search and replace/change functions, filtering, line tag and word highlight, internal tool for search
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
#include <string.h>
#include <stdlib.h>	/* atoi */
#include <syslog.h>
#include "main.h"
#include "proto.h"

#ifndef KEY_ESC
#define KEY_ESC  0x1b
#endif

/* global config */
extern CONFIG cnf;

/* local proto */
static int repeat_search_eng (void);
static int search_for_replace (CHDATA *chp);
static int accum_replacement (CHDATA *chp);
static int do_replacement (CHDATA *chp);

/* regexp specials are ^.[$()|*+?{\\ and maybe }] */
/* regexp shorthands (extensions like in other regex tools) are \w \W \s \S \d \D and \t */
static int repeat_search_initial_call=0;

/*
 * filtering with regular expression
 */
int
filter_regex (int action, int fmask, const char *expr)
{
	int ret=0;
	regex_t reg;
	char errbuff[ERRBUFF_SIZE];
	LINE *lx;
	regmatch_t pmatch;
	char expr_tmp[XPATTERN_SIZE];
	char expr_new[XPATTERN_SIZE];

	cut_delimiters (expr, expr_tmp, sizeof(expr_tmp));
	regexp_shorthands (expr_tmp, expr_new, sizeof(expr_new));
	memset (errbuff, 0, ERRBUFF_SIZE);
	ret = regcomp (&reg, expr_new, REGCOMP_OPTION);
	if (ret) {
		regerror(ret, &reg, errbuff, ERRBUFF_SIZE);
		REPL_LOG(LOG_ERR, "pattern [%s]: regcomp failed (%d): %s", expr_new, ret, errbuff);
		tracemsg("%s", errbuff);
	} else {
		lx = CURR_FILE.top->next;
		while (TEXT_LINE(lx)) {
			ret = regexec(&reg, lx->buff, 1, &pmatch, 0);
			if (ret == 0 && pmatch.rm_so >= 0 &&
				(pmatch.rm_eo == 0 || pmatch.rm_so < pmatch.rm_eo))
			{
				if (action & (FILTER_MORE | FILTER_ALL))
					lx->lflag &= ~fmask;
				else if (action & FILTER_LESS)
					lx->lflag |= fmask;
			} else if (action & FILTER_ALL) {
				lx->lflag |= fmask;
			}
			lx = lx->next;
		}
	}
	regfree (&reg);

	return (0);
}

/*
 * internal use only : match return(0), no match return(1), error return(-1).
 * if match is not NULL, match is _filled up_ with the Nth sub
 * if nsub is from [0,10) -- the length of match is limited to TAGSTR_SIZE chars
 */
int
regexp_match (const char *buff, const char *expr, int nsub, char *match)
{
	int ret = -1;
	regex_t reg;
	char errbuff[ERRBUFF_SIZE];
	regmatch_t pmatch[10];	/* match and sub match */
	int iy=0, ix=0;
	char expr_tmp[XPATTERN_SIZE];
	char expr_new[XPATTERN_SIZE];

	cut_delimiters (expr, expr_tmp, sizeof(expr_tmp));
	regexp_shorthands (expr_tmp, expr_new, sizeof(expr_new));
	memset (errbuff, 0, ERRBUFF_SIZE);
	ret = regcomp (&reg, expr_new, REGCOMP_OPTION);
	if (ret) {
		regerror(ret, &reg, errbuff, ERRBUFF_SIZE);
		REPL_LOG(LOG_CRIT, "internal pattern [%s]: regcomp failed (%d): %s", expr_new, ret, errbuff);
		ret = -1;
	} else {
		ret = regexec(&reg, buff, 10, pmatch, 0);
		if (ret == 0 && pmatch[0].rm_so >= 0 &&
			(pmatch[0].rm_eo == 0 || pmatch[0].rm_so < pmatch[0].rm_eo))
		{
			ret = 0;	/* ok, match */
		} else {
			ret = 1;	/* fail, no match */
		}
	}

	/* fill back matching if required and if possible */
	if (ret==0 && 0<=nsub && nsub<10 && match != NULL) {
		iy=0;
		if (pmatch[nsub].rm_so >= 0) {
			for (ix = pmatch[nsub].rm_so; ix < pmatch[nsub].rm_eo; ix++) {
				match[iy++] = buff[ix];
				if (iy >= TAGSTR_SIZE-1) {
					break;	/* it's enough for a match */
				}
			}
		}
		match[iy] = '\0';
	}

	regfree (&reg);
	return (ret);
}

/*
 * internal search engine, "locate", scans all regular buffers
 */
int
internal_search (const char *pattern)
{
	int ret=0, rret=0;
	regex_t reg;
	char errbuff[ERRBUFF_SIZE];
	regmatch_t pmatch;
	int ri, ri_lineno;
	LINE *lp=NULL, *lx=NULL, *ri_lp=NULL;
	char one_line[1024];

	memset (errbuff, 0, ERRBUFF_SIZE);
	ret = regcomp (&reg, pattern, REGCOMP_OPTION);
	if (ret) {
		regerror(ret, &reg, errbuff, ERRBUFF_SIZE);
		REPL_LOG(LOG_ERR, "pattern [%s]: regcomp failed (%d): %s", pattern, ret, errbuff);
		tracemsg("%s", errbuff);
		return (1);
	}

	/* header
	*/
	memset (one_line, 0, sizeof(one_line));
	snprintf(one_line, sizeof(one_line)-1, "%s\n", pattern);
	if ((lp = insert_line_before (CURR_FILE.bottom, one_line)) != NULL) {
		CURR_FILE.num_lines++;
		if (milbuff (lp, 0, 0, "locate ", 7)) {
			ret = 2;
		}
	} else {
		ret = 2;
	}

	/* scan all lines of regular files in the ring
	*/
	lp = CURR_LINE = CURR_FILE.bottom->prev;
	for (ri=0; ret==0 && ri<RINGSIZE; ri++) {
		if ((cnf.fdata[ri].fflag & FSTAT_OPEN) && !(cnf.fdata[ri].fflag & FSTAT_SPECW))
		{
			ri_lp = cnf.fdata[ri].top->next;
			ri_lineno = 1;
			while (ret==0 && TEXT_LINE(ri_lp)) {
				rret = regexec(&reg, ri_lp->buff, 1, &pmatch, 0);
				if (rret == 0 && pmatch.rm_so >= 0 &&
					(pmatch.rm_eo == 0 || pmatch.rm_so < pmatch.rm_eo))
				{
					snprintf(one_line, sizeof(one_line)-1, "%s:%d:\n",  cnf.fdata[ri].fname, ri_lineno);
					if ((lx = append_line (lp, one_line)) == NULL) {
						ret = 2;
						break;
					} else {
						CURR_FILE.num_lines++;
						lp = lx;
						if (milbuff (lp, lp->llen-1, 0, ri_lp->buff, ri_lp->llen-1)) {
							ret = 2;
							break;
						}
					}
				}
				ri_lineno++;
				ri_lp = ri_lp->next;
			}
		}
	}

	regfree (&reg);

	/* footer
	*/
	if (ret==0 && append_line (lp, "\n") != NULL) {
		CURR_FILE.num_lines++;
	}

	/* pull and update
	*/
	CURR_LINE = CURR_FILE.bottom->prev;
	CURR_FILE.lineno = CURR_FILE.num_lines;
	update_focus(FOCUS_ON_LASTBUT1_LINE, cnf.ring_curr);

	return (ret);
}

/*
 * search line by pattern
 * return LINE pointer and the lineno also
 * pattern should be prepared ready for regexp-search
 */
LINE *
search_goto_pattern (int ri, const char *pattern, int *new_lineno)
{
	LINE *lx;
	int ret=1;
	regex_t reg;
	char expr_tmp[XPATTERN_SIZE];
	char expr_new[XPATTERN_SIZE];

	*new_lineno = 0;
	if (ri < 0 || ri >= RINGSIZE || !(cnf.fdata[ri].fflag & FSTAT_OPEN))
		return NULL;
	if (pattern == NULL)
		return NULL;

	/* no delimiters, just copy */
	strncpy (expr_tmp, pattern, sizeof(expr_tmp));
	expr_tmp[sizeof(expr_tmp)-1] = '\0';

	regexp_shorthands (expr_tmp, expr_new, sizeof(expr_new));
	ret = regcomp (&reg, expr_new, REG_NOSUB | REG_NEWLINE);
	if (ret) {
		/* search regexp by ctags */
		REPL_LOG(LOG_ERR, "(ctags) regcomp failed [%s]: %d", expr_new, ret);
		lx = NULL;
	} else {
		lx = cnf.fdata[ri].top->next;
		*new_lineno = 1;
		while (TEXT_LINE(lx)) {
			ret = regexec(&reg, lx->buff, 0, NULL, 0);
			if (ret == 0) {
				break;
			}
			lx = lx->next;
			(*new_lineno)++;
		}
	}
	regfree (&reg);
	/* search finish */

	return (lx);	/* *new_lineno also */
}

/*
** color_tag - mark lines with color in different ways, if argument is missing the focus word is used;
**	with arguments "alter", "selection", ":<lineno>" the mentioned lines are tagged,
**	while with regexp argument only the matchhing lines will have color mark;
**	with empty pattern all marks are removed (like "tag //")
*/
int
color_tag (const char *expr)
{
	unsigned len;
	int fmask, lineno, cnt, ret=0;
	LINE *lx=NULL;
	regex_t reg;
	char errbuff[ERRBUFF_SIZE];
	regmatch_t pmatch;
	char expr_tmp[XPATTERN_SIZE];
	char expr_new[XPATTERN_SIZE];

	fmask = LSTAT_TAG1;
	len = strlen(expr);

	/* fast reset, but only for in-view lines
	 * and return
	 */
	if (len==0) {
		lx = CURR_FILE.top;
		next_lp (cnf.ring_curr, &lx, NULL);
		while (TEXT_LINE(lx)) {
			lx->lflag &= ~fmask;	/* reset-off */
			next_lp (cnf.ring_curr, &lx, NULL);
		}
		return (ret);
	}
	/* expr is not empty, some simple cases (do _additional_ marking only)
	 * and return
	 */
	else {
		/* altered lines -- additional marking */
		if (strncmp(expr, "alter", len)==0) {
			lx = CURR_FILE.top;
			next_lp (cnf.ring_curr, &lx, NULL);
			while (TEXT_LINE(lx)) {
				if (lx->lflag & (LSTAT_ALTER | LSTAT_CHANGE)) {
					lx->lflag |= fmask;	/* add-on */
				}
				next_lp (cnf.ring_curr, &lx, NULL);
			}
			return (ret);

		/* selection lines -- additional marking */
		} else if (strncmp(expr, "selection", len)==0) {
			lx = CURR_FILE.top;
			next_lp (cnf.ring_curr, &lx, &cnt);
			lineno = cnt;
			while (TEXT_LINE(lx)) {
				if (lx->lflag & LSTAT_SELECT) {
					lx->lflag |= fmask;	/* add-on */
				}
				next_lp (cnf.ring_curr, &lx, &cnt);
				lineno += cnt;
			}
			return (ret);

		/* one line -- additional marking */
		} else if (expr[0] == ':') {
			ret = 1;
			lineno = atoi(&expr[1]);
			if (lineno >= 1 && lineno <= CURR_FILE.num_lines) {
				lx = lll_goto_lineno (cnf.ring_curr, lineno);
				if (lx != NULL && !HIDDEN_LINE(cnf.ring_curr,lx)) {
					lx->lflag |= fmask;	/* add-on */
					ret = 0;
				}
			}
			return (ret);
		}
	}/* len */

	/* the rest is for regular expression based line coloring (in-view only)
	 * pattern match => view, else => don't
	 */
	cut_delimiters (expr, expr_tmp, sizeof(expr_tmp));

	regexp_shorthands (expr_tmp, expr_new, sizeof(expr_new));
	memset (errbuff, 0, ERRBUFF_SIZE);
	ret = regcomp (&reg, expr_new, REGCOMP_OPTION);
	if (ret) {
		regerror(ret, &reg, errbuff, ERRBUFF_SIZE);
		REPL_LOG(LOG_ERR, "pattern [%s]: regcomp failed (%d): %s", expr_new, ret, errbuff);
		tracemsg("%s", errbuff);
	} else {
		/* tag coloring
		 * only for in-view lines
		 */
		lx = CURR_FILE.top;
		next_lp (cnf.ring_curr, &lx, NULL);
		while (TEXT_LINE(lx)) {
			ret = regexec(&reg, lx->buff, 1, &pmatch, 0);
			if (ret == 0 && pmatch.rm_so >= 0 &&
				(pmatch.rm_eo == 0 || pmatch.rm_so < pmatch.rm_eo))
			{
				lx->lflag |= fmask;	/* set on */
			} else {
				lx->lflag &= ~fmask;	/* set off */
			}
			next_lp (cnf.ring_curr, &lx, NULL);
		}
	}
	regfree (&reg);

	return (0);
}

/*
** highlight_word - highlight matching bytes in the line according to regexp or the focus word;
**	reset highlighting if cursor is not in a word or pattern is explicit empty (like "high //")
*/
int
highlight_word (const char *expr)
{
	/* do regcomp() here the rest is in upd_text_area() */

	char *word=NULL;
	int ret=0;
	char errbuff[ERRBUFF_SIZE];
	char expr_tmp[XPATTERN_SIZE];
	char expr_new[XPATTERN_SIZE];

	/* do reset */
	if (CURR_FILE.fflag & FSTAT_TAG5) {
		regfree(&(CURR_FILE.highlight_reg));
		CURR_FILE.fflag &= ~FSTAT_TAG5;
	}

	if (expr[0] == '\0') {
		word = select_word(CURR_LINE, CURR_FILE.lncol);
		if (word == NULL || word[0] == '\0') {
			FREE(word); word = NULL;
			return (0);
		}
		if (word[0] == '.' || word[0] == '>') {
			strncpy (expr_new, word+1, sizeof(expr_new));
		} else {
			strncpy (expr_new, word, sizeof(expr_new));
		}
		expr_new[sizeof(expr_new)-1] = '\0';
		FREE(word); word = NULL;
	} else {
		cut_delimiters (expr, expr_tmp, sizeof(expr_tmp));
		regexp_shorthands (expr_tmp, expr_new, sizeof(expr_new));
	}

	memset (errbuff, 0, ERRBUFF_SIZE);
	ret = regcomp (&(CURR_FILE.highlight_reg), expr_new, REGCOMP_OPTION);
	if (ret) {
		regerror(ret, &(CURR_FILE.highlight_reg), errbuff, ERRBUFF_SIZE);
		REPL_LOG(LOG_ERR, "pattern [%s]: regcomp failed (%d): %s", expr_new, ret, errbuff);
		tracemsg("%s", errbuff);
		/* FSTAT_TAG5 not set */
		regfree(&(CURR_FILE.highlight_reg));
	} else {
		CURR_FILE.fflag |= FSTAT_TAG5;
		/* check if pattern has BoL or EoL anchor */
		if (expr_new[0] == '^' || expr_new[0] == '$') {
			CURR_FILE.fflag |= FSTAT_TAG6;
		} else {
			CURR_FILE.fflag &= ~FSTAT_TAG6;
		}
	}

	return (0);
}

/*
* copy characters from expr[] to expr_new[] and cut delimiters (in order: / '' "" !)
*/
void
cut_delimiters (const char *expr, char *expr_new, int length)
{
	int i=0, j=0;
	char beg = expr[0];

	if (beg == 0x2f || beg == 0x27 || beg == 0x22 || beg == 0x21) {
		i++;
		while (expr[i] != '\0' && j < length-1) {
			expr_new[j++] = expr[i++];
		}
		if (j > 0 && expr_new[j-1] == beg) {
			--j;
		}
	} else {
		/* just copy */
		while (expr[i] != '\0' && j < length-1) {
			expr_new[j++] = expr[i++];
		}
	}

	expr_new[j++] = '\0';
}

/*
 * regexp shorthands: \d \D and \t
 * the \s \S \w \W are handled by regcomp/glibc
 */
void
regexp_shorthands (const char *pattern, char *ext_pattern, int length)
{
	int i=0, j=0, opt=0;

	for (i=0; pattern[i] != '\0' && j < length-7; i++) {
		if (opt) {
			/* the previous character was backslash */
			if (pattern[i] == 'd') {
				ext_pattern[j++] = '[';		/* [[:digit:]] */
				ext_pattern[j++] = '0';
				ext_pattern[j++] = '-';
				ext_pattern[j++] = '9';
				ext_pattern[j++] = ']';
			} else if (pattern[i] == 'D') { /* 6 chars, the max */
				ext_pattern[j++] = '[';		/* [^[:digit:]] */
				ext_pattern[j++] = '^';
				ext_pattern[j++] = '0';
				ext_pattern[j++] = '-';
				ext_pattern[j++] = '9';
				ext_pattern[j++] = ']';
			} else if (pattern[i] == 't') {
				ext_pattern[j++] = '\t';
			} else {
				ext_pattern[j++] = '\\';
				ext_pattern[j++] = pattern[i];
			}
			opt = 0;
		} else {
			/* prev char was ordinary */
			if (pattern[i] == '\\') {
				opt = 1;
			} else {
				ext_pattern[j++] = pattern[i];
			}
		}
	}
	ext_pattern[j++] = '\0';

	return;
}

/*
** tag_focusline - toggle the color mark of the focus line
*/
int
tag_focusline (void)
{
	if (TEXT_LINE(CURR_LINE)) {
		/* toggle */
		if (CURR_LINE->lflag & LSTAT_TAG1)
			CURR_LINE->lflag &= ~LSTAT_TAG1;	/* off */
		else
			CURR_LINE->lflag |= LSTAT_TAG1;		/* on */
		return (0);
	} else {
		return (1);
	}
}

/*
** search - start forward search with given regular expression (like "/reset" or "/\<ret\>/");
**	reset search immediately if match not found;
**	submatch referencies '\\1'...'\\9' can be used
*/
int
search (const char *expr)
{
	/* ready for continue in repeat_search() and in text_line()  */

	int ret=0;
	char errbuff[ERRBUFF_SIZE];
	char expr_tmp[XPATTERN_SIZE];
	char expr_new[XPATTERN_SIZE];

	/* reset regexp */
	reset_search();

	/* leave if no args */
	if (strlen(expr) == 0) {
		return (0);
	}

	/* cut delimiter 0x2f */
	cut_delimiters (expr, expr_tmp, sizeof(expr_tmp));

	/* leave if search pattern is empty */
	if (strlen(expr_tmp) == 0) {
		return (0);
	}

	regexp_shorthands (expr_tmp, expr_new, sizeof(expr_new));
	memset (errbuff, 0, ERRBUFF_SIZE);
	ret = regcomp (&(CURR_FILE.search_reg), expr_new, REGCOMP_OPTION);
	if (ret) {
		regerror(ret, &(CURR_FILE.search_reg), errbuff, ERRBUFF_SIZE);
		REPL_LOG(LOG_ERR, "pattern [%s]: regcomp failed (%d): %s", expr_new, ret, errbuff);
		tracemsg("%s", errbuff);
		/* FSTAT_TAG2 not set, do not regfree(&(CURR_FILE.search_reg)); */

	} else {
		CURR_FILE.fflag |= FSTAT_TAG2;
		if (expr_new[0] == '^' || expr_new[0] == '$') {
			CURR_FILE.fflag |= FSTAT_TAG4;
		} else {
			CURR_FILE.fflag &= ~FSTAT_TAG4;
		}
		repeat_search_initial_call = 1;
		if (repeat_search() != 0) {
			reset_search();		/* drop search */
		} else {
			/* store the original expr */
			strncpy (CURR_FILE.search_expr, expr, SEARCHSTR_SIZE);
			CURR_FILE.search_expr[SEARCHSTR_SIZE-1] = '\0';
		}
	}

	return (0);
} /* search */

/*
 * reset search; returns 0
 */
int
reset_search (void)
{
	if (CURR_FILE.fflag & (FSTAT_TAG2 | FSTAT_TAG3)) {
		regfree(&(CURR_FILE.search_reg));
		CURR_FILE.fflag &= ~(FSTAT_TAG2 | FSTAT_TAG3 | FSTAT_TAG4);
	}
	return (0);
}

/*
 * internal subroutine for repeat_search, phase one and two; return 0 on success
 */
static int
repeat_search_eng (void)
{
	int ret = 1;
	LINE *lx = CURR_LINE;
	int lineno = CURR_FILE.lineno;
	int xcol = CURR_FILE.lncol;
	regmatch_t pmatch;
	int cnt = 0;
	int search_rflag = 0;

	while (!(lx->lflag & LSTAT_BOTTOM)) {
		if (xcol < lx->llen) {
			search_rflag = (xcol>0) && (CURR_FILE.fflag & FSTAT_TAG4) ? REG_NOTBOL : 0;
			ret = regexec(&(CURR_FILE.search_reg), lx->buff+xcol, 1, &pmatch, search_rflag);
			if (ret == 0 && pmatch.rm_so >= 0) {
				if ((CURR_FILE.fflag & FSTAT_TAG4) && pmatch.rm_so == pmatch.rm_eo)
				{
					if (xcol+pmatch.rm_so == 0) {
						break;
					} else if (xcol+pmatch.rm_so == lx->llen-1) {
						break;
					}
				} else if (pmatch.rm_so < pmatch.rm_eo) {
					break;
				}
			}
		}
		next_lp (cnf.ring_curr, &lx, &cnt);
		lineno += cnt;
		xcol = 0;
		/* partially useful */
		if ((cnf.gstat & GSTAT_SHADOW) && (cnt > 1))
			update_focus(INCR_FOCUS_SHADOW, cnf.ring_curr);
		else
			update_focus(INCR_FOCUS, cnf.ring_curr);
	}

	if (ret == 0) {
		/* found */
		CURR_LINE = lx;
		CURR_FILE.lineno = lineno;
		CURR_FILE.lncol = xcol + pmatch.rm_eo;
		/* focus updated */
	}

	return (ret);
}

/*
 * search the next matching line,
 * adjust CURR_LINE, CURR_FILE.lineno and focus, lncol, curpos, lnoff
 * if found: return 0
 * if not: return 1 (except for the initial call)
 * else: -1 (error)
 */

/*
** repeat_search - search next occurence; reset search if not found
*/
int
repeat_search (void)
{
	int ret = 1;
	LINE *restore_lx = CURR_LINE;
	int restore_lineno = CURR_FILE.lineno;
	int restore_lncol = CURR_FILE.lncol;
	int restore_focus = CURR_FILE.focus;

	if ( !(CURR_FILE.fflag & FSTAT_TAG2)) {
		return (0);
	}

	/* start search */
	if (CURR_LINE->lflag & LSTAT_TOP) {
		CURR_LINE = CURR_LINE->next;
		CURR_FILE.lineno++;
	}

	/* initial shift in the line */
	if (!repeat_search_initial_call && (CURR_FILE.fflag & FSTAT_TAG4)) {
		/* special skip before repeated search, zero length BoL or EoL anchor */
		CURR_FILE.lncol++;
	}

	/* phase one -- search only the first match */
	ret = repeat_search_eng();
	if (ret == 0) {
		/* found -- match position set by engine */
		update_focus(FOCUS_AVOID_BORDER, cnf.ring_curr);
		update_curpos(cnf.ring_curr);
		/* text area */
		CURR_FILE.fflag &= ~FSTAT_CMD;
	} else {
		if (repeat_search_initial_call && (CURR_FILE.focus > 0) && (UNLIKE_TOP(CURR_LINE))) {
			/* phase two -- search only to show earlier matches */
			go_first_screen_line ();
			CURR_FILE.lncol = CURR_FILE.curpos = 0;
			ret = repeat_search_eng();
		}

		/* restore original position anyway */
		CURR_LINE = restore_lx;
		CURR_FILE.lineno = restore_lineno;
		CURR_FILE.focus = restore_focus;
		CURR_FILE.lncol = restore_lncol;
		update_curpos(cnf.ring_curr);

		if (ret == 0) {
			/* found, but before the focus -- position reverted */
			/* text area */
			CURR_FILE.fflag &= ~FSTAT_CMD;
		} else {
			/* not found -- position reverted */
			tracemsg ("search: no match");
			reset_search();
			ret = (repeat_search_initial_call) ? 1 : 0;	/* 'no match' is not an error */
		}
	}

	repeat_search_initial_call = 0;
	return (ret);
} /* repeat_search */

/*
** change - start search and replace with given regular expressions (like "ch /from/to/"),
**	possible delimiters are slash, single quote, double quote, exclamation mark;
**	submatch referencies '\\1'...'\\9' and '&' can be used
*/
int
change (const char *argz)
{
	int ret=1;
	char errbuff[ERRBUFF_SIZE];
	char *expr=NULL, *repl_expr=NULL;
	char beg, *r=NULL;
	char expr_tmp[XPATTERN_SIZE];
	char expr_new[XPATTERN_SIZE], repl_expr_new[XPATTERN_SIZE];
	int global_opt = 0;
	LINE *g_lx=NULL;
	int g_lineno=0, g_focus=0, g_lncol=0;

	/* reset regexp */
	reset_search();

	/* leave if no arguments */
	if (strlen(argz) == 0) {
		return (0);
	}

	strncpy(expr_tmp, argz, sizeof(expr_tmp));
	expr_tmp[sizeof(expr_tmp)-1] = '\0';

	/* parse arguments into expressions */
	beg = expr_tmp[0];
	if (beg==0x2f || beg==0x27 || beg==0x22 || beg==0x21) {
		expr = r = expr_tmp+1;
		for (; *r != '\0' && *r != beg; r++)
			;
		if (*r == beg) {
			*r = '\0';
			repl_expr = ++r;
			for (; *r != '\0' && *r != beg; r++)
				;
			if (*r == beg) {
				global_opt = (*(r+1) == 'g');
			}
			*r = '\0';
		}
	}
	if (expr == NULL || repl_expr == NULL) {
		tracemsg("failure: missing pattern delimiters");
		return (1);
	}

	/* leave if search pattern is empty */
	if (strlen(expr) == 0) {
		return (0);
	}

	regexp_shorthands (expr, expr_new, sizeof(expr_new));
	regexp_shorthands (repl_expr, repl_expr_new, sizeof(repl_expr_new));
	/* keep repl_expr unchanged */

	memset (errbuff, 0, ERRBUFF_SIZE);
	ret = regcomp (&(CURR_FILE.search_reg), expr_new, REGCOMP_OPTION);
	if (ret) {
		regerror(ret, &(CURR_FILE.search_reg), errbuff, ERRBUFF_SIZE);
		REPL_LOG(LOG_ERR, "pattern [%s]: regcomp failed (%d): %s", expr_new, ret, errbuff);
		tracemsg("%s", errbuff);
		/* FSTAT_TAG{2|3} not set, do not regfree(&(CURR_FILE.search_reg)); */

	} else {
		CURR_FILE.fflag |= (FSTAT_TAG2 | FSTAT_TAG3);
		/* check if pattern has BoL or EoL anchor */
		if (expr_new[0] == '^' || expr_new[0] == '$') {
			CURR_FILE.fflag |= FSTAT_TAG4;
		} else {
			CURR_FILE.fflag &= ~FSTAT_TAG4;
		}
		if (global_opt) {
			/* to save */
			g_lx = CURR_LINE;
			g_lineno = CURR_FILE.lineno;
			g_focus = CURR_FILE.focus;
			g_lncol = CURR_FILE.lncol;
		}
		if (repeat_change(0xff) != 0) {
			reset_search();		/* drop search */
		} else {
			/* store the original expr */
			strncpy (CURR_FILE.search_expr, expr, SEARCHSTR_SIZE);
			CURR_FILE.search_expr[SEARCHSTR_SIZE-1] = '\0';
			/* .replace_expr will be used in accum_replacement() */
			strncpy (CURR_FILE.replace_expr, repl_expr_new, SEARCHSTR_SIZE);
			CURR_FILE.replace_expr[SEARCHSTR_SIZE-1] = '\0';
			/**/
			/* select text area */
			CURR_FILE.fflag &= ~FSTAT_CMD;
			/**/
			if (global_opt) {
				REPL_LOG(LOG_DEBUG, "(global) quiet search & replace, no question");
				cnf.trace = 0;	/* drop previous msgs */
				ret = repeat_change('r');
				/* and to restore */
				CURR_LINE = g_lx;
				CURR_FILE.lineno = g_lineno;
				CURR_FILE.focus = g_focus;
				CURR_FILE.lncol = g_lncol;
				update_curpos(cnf.ring_curr);
			}
		}
	}

	return (ret);
} /* change */

/*
 * the change repeater...
 * return 0 on success, 1 if no more found, other values for errors
 */
int
repeat_change (int ch)
{
	int ret = 0;
	char *s;
	unsigned als;
	static CHDATA *chp = NULL;
	int cnt=0;
	int restore_focus = CURR_FILE.focus;

	if (ch == 0xff) {
		s = (char *) MALLOC(sizeof(CHDATA));
		if (s == NULL) {
			chp = NULL;
			ret = 4 | 8;
		} else {
			chp = (CHDATA *)s;

			chp->change_count = 0;
			chp->lx = CURR_LINE;
			chp->lineno = CURR_FILE.lineno;
			chp->lncol = CURR_FILE.lncol;
			if (CURR_LINE->lflag & LSTAT_TOP) {
				next_lp (cnf.ring_curr, &(chp->lx), &cnt);
				chp->lineno += cnt;
				chp->lncol = 0;
			}

			/* initial allocation for rep_buff[]; macro will reserve enough space */
			als = REP_ASIZE(0);
			s = (char *) MALLOC(als);
			if (s == NULL) {
				FREE(chp);
				chp = NULL;
				ret = 4 | 8;
			} else {
				chp->rep_buff = s;
				chp->rep_length = 0;
				chp->rflag = 0xff;	/* initially: rep_buff isn't const */
				/**/
				ret = search_for_replace (chp);
			}
		}

	} else if (ch=='y' || ch=='Y') {
		if (chp->rflag)
			accum_replacement (chp);
		if (do_replacement (chp)) {
			ret = 4;	/* malloc error */
		} else {
			chp->change_count++;
			chp->lncol += chp->pmatch[0].rm_so + chp->rep_length + ((CURR_FILE.fflag & FSTAT_TAG4) ? 1 : 0);
			CURR_FILE.lncol = chp->lncol;
			update_curpos(cnf.ring_curr);
			/**/
			ret = search_for_replace (chp);
		}

	} else if (ch=='n' || ch=='N') {
		chp->lncol += chp->pmatch[0].rm_eo + ((CURR_FILE.fflag & FSTAT_TAG4) ? 1 : 0);
		CURR_FILE.lncol = chp->lncol;
		update_curpos(cnf.ring_curr);
		/**/
		ret = search_for_replace (chp);

	} else if (ch=='r' || ch=='R') {
		while ((ret == 0) && TEXT_LINE(chp->lx)) {
			if (chp->rflag)
				accum_replacement (chp);
			if (do_replacement (chp)) {
				ret = 4;	/* malloc error */
			} else {
				chp->change_count++;
				chp->lncol += chp->pmatch[0].rm_so + chp->rep_length + ((CURR_FILE.fflag & FSTAT_TAG4) ? 1 : 0);
				/**/
				ret = search_for_replace (chp);
			}
		}
		/* original lncol should be updated */
		CURR_FILE.lncol = get_col(CURR_LINE, CURR_FILE.curpos);
		ret |= 1;

	} else if (ch=='q' || ch=='Q' || ch==KEY_ESC) {
		ret = 3;
	} else {
		ret = -1;	/* no search, no update!!! */
	}

	if (ret > 0) {
		reset_search();
		CURR_FILE.focus = restore_focus;
		if (ret & 4) {
			tracemsg ("change aborted due to malloc error");
		} else {
			if (ch == 0xff) {
				tracemsg ("change: no match");
			} else {
				tracemsg ("change count %d", chp->change_count);
			}
			REPL_LOG(LOG_DEBUG, "(end) ch 0x%x: line %d change_count %d", ch, chp->lineno, chp->change_count);
		}
		if (chp != NULL) {
			FREE(chp->rep_buff);
			chp->rep_buff = NULL;
			FREE(chp);
			chp = NULL;
		}
	} else if (ret == 0) {
		/* found */
		CURR_LINE = chp->lx;
		CURR_FILE.lineno = chp->lineno;
		update_focus(FOCUS_AVOID_BORDER, cnf.ring_curr);
		CURR_FILE.lncol = chp->lncol + chp->pmatch[0].rm_eo;
		update_curpos(cnf.ring_curr);
		/* chp->lncol must be saved as is! */
		REPL_LOG(LOG_DEBUG, "(found) ch 0x%x: line %d lncol %d -> %d", ch, chp->lineno, chp->lncol, CURR_FILE.lncol);
		tracemsg (REPLACE_QUEST);
	} else {
		REPL_LOG(LOG_DEBUG, "(exception) ch 0x%x: line %d", ch, chp->lineno);
		tracemsg (REPLACE_QUEST);
	}

	return (ret);
} /* repeat_change */

/*
 * search the next matching expression: strictly for change
 * return 0 if found, 1 if not
 */
static int
search_for_replace (CHDATA *chp)
{
	int ret = 1;
	int cnt;
	char search_rflag=0;

	/* start search */
	while (!(chp->lx->lflag & LSTAT_BOTTOM)) {
		if (chp->lncol < chp->lx->llen) {
			search_rflag = (chp->lncol>0) && (CURR_FILE.fflag & FSTAT_TAG4) ? REG_NOTBOL : 0;
			ret = regexec(&(CURR_FILE.search_reg), chp->lx->buff + chp->lncol, 10, chp->pmatch, search_rflag);
			if (ret == 0 && chp->pmatch[0].rm_so >= 0)
			{
				if ((CURR_FILE.fflag & FSTAT_TAG4) && chp->pmatch[0].rm_so == chp->pmatch[0].rm_eo)
				{
					if (chp->lncol+chp->pmatch[0].rm_so == 0) {
						break;
					} else if (chp->lncol+chp->pmatch[0].rm_eo == chp->lx->llen-1) {
						break;
					}
				} else if (chp->pmatch[0].rm_so < chp->pmatch[0].rm_eo) {
					break;
				}
			}
		}
		/* not found, yet */
		next_lp (cnf.ring_curr, &(chp->lx), &cnt);
		chp->lineno += cnt;
		chp->lncol = 0;		/* lncol for the next line */
		if ((cnf.gstat & GSTAT_SHADOW) && (cnt > 1))
			update_focus(INCR_FOCUS_SHADOW, cnf.ring_curr);
		else
			update_focus(INCR_FOCUS, cnf.ring_curr);
	}

	/* finished */
	if ((ret == 0) && TEXT_LINE(chp->lx)) {
		return (0); /* found */
	} else {
		return (1); /* not found */
	}
} /* search_for_replace */

/* shorthands */
#define PMLEN(ii)	(chp->pmatch[(ii)].rm_eo - chp->pmatch[(ii)].rm_so)
#define BEG_SRC(ii)	(chp->lx->buff + chp->lncol + chp->pmatch[(ii)].rm_so)

/*
 * accumulate the replacement string from replace expr "pattern" and LINE buffer with pmatch[]
 * value access thru chp
 *
 * return value is the length of the accumulated string or -1 on error
 *
 * the replace "pattern" is const, so if no backref found on first call
 * we can eliminate further calls
 */
static int
accum_replacement (CHDATA *chp)
{
	char *rp;	/* ptr on (const) CURR_FILE.replace_expr */
	char *srcp;	/* source lx->buff ptr on matching expr */
	int idx=0;	/* index on chp->rep_buff[] */
	int nsub=0;	/* sub expression number */
	int sub_expression_used = 0;
	int jj=0;
	int sublen=0;	/* matching subexpr length */
	unsigned als=0;	/* allocation */
	char *s;

	/* remains zero if const replacement */
	chp->rflag = 0;
	srcp = NULL;

	/*
	 * copy char by char from the zero terminated const repl "pattern" string
	 * and handle back references \0, \1, ... \9 and & (alias \0)
	 * but \\0, \\1, ... \\9, \& and \\ literally as \0, \1, ... \9, & and \
	 * --- thanks for this idea/feature to who made it ---
	 */
	for (rp = CURR_FILE.replace_expr; *rp != '\0'; rp++) {
		srcp = NULL;
		sublen = 0;
		sub_expression_used = 0;
		if (*rp == '\\') {
			rp++;	/* extra incr, skip the backslash */
			if (*rp >= '0' && *rp <= '9') {
				/* replace \N with Nth submatch */
				nsub = *rp - '0';
				srcp = BEG_SRC(nsub);
				sublen = PMLEN(nsub);
				sub_expression_used = 1;
			} else if (*rp == '&' || *rp == '\\') {
				/* replace with literal */
				chp->rep_buff[idx++] = *rp;
			} else {
				/* all other cases */
				chp->rep_buff[idx++] = '\\';
				chp->rep_buff[idx++] = *rp;	/* maybe zero */
				if (*rp == '\0') {
					idx--;
					break;		/* because the extra incr */
				}
			}
		} else if (*rp == '&') {
			srcp = BEG_SRC(nsub);
			sublen = PMLEN(nsub);
			sub_expression_used = 1;
		} else {
			chp->rep_buff[idx++] = *rp;
		}

		if (sub_expression_used && srcp != NULL && sublen > 0) {
			/*
			 * realloc for chp->rep_buff with REP_ASIZE(idx+sublen)
			 */
			als = REP_ASIZE(idx+sublen);
			s = (char *) REALLOC((void *)chp->rep_buff, als);
			if (s == NULL) {
				return (-1);
			}
			chp->rep_buff = s;

			/*
			 * copy bytes from search "pattern" to chp->rep_buff
			 */
			for (jj=0; jj < sublen; jj++) {
				if (srcp[jj] != '\n') {
					/* fixit --- remove embedded newline */
					chp->rep_buff[idx] = srcp[jj];
					idx++;
				}
			}

			chp->rflag |= (nsub>0 ? 1 : 2);
		}

	}/* for */

	chp->rep_buff[idx] = '\0';
	chp->rep_length = idx;
	/* allocated space: REP_ASIZE(idx) */

	REPL_LOG(LOG_DEBUG, "line %d --- rep_length %d rep_buff [%s] rflag 0x%x",
		chp->lineno, chp->rep_length, chp->rep_buff, chp->rflag);

	return (0);
} /* accum_replacement */

/*
 * do the replacement in the LINE buffer with the accumulated replacement string
 * value access thru chp
 *
 * return 0 if ok, -1 on error
 */
static int
do_replacement (CHDATA *chp)
{
	int ret = 0;

	REPL_LOG(LOG_DEBUG, "line %d --- from %d length %d -- replace length %d",
		chp->lineno,
		chp->lncol + chp->pmatch[0].rm_so,
		chp->pmatch[0].rm_eo - chp->pmatch[0].rm_so,
		chp->rep_length);

	ret = milbuff (chp->lx,
		chp->lncol + chp->pmatch[0].rm_so,
		chp->pmatch[0].rm_eo - chp->pmatch[0].rm_so,
		chp->rep_buff,
		chp->rep_length);

	/* update */
	chp->lx->lflag |= LSTAT_CHANGE;
	CURR_FILE.fflag |= FSTAT_CHANGE;

	return (ret);
} /* do_replacement */
