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

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include "main.h"
#include "proto.h"

/* global config */
extern CONFIG cnf;

/* local proto */
static int filter_func_eng_clang (int action, int fmask, char *symbol);
static int filter_func_eng_other (int action, int fmask, char *symbol);
static int filter_func_eng_easy (int action, int fmask, char *symbol);

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
		ERRLOG(0xE072); /* start line lx==NULL */
		return (1);
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
	return (0);
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
		ERRLOG(0xE071); /* start line lx==NULL */
		return (1);
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
	return (0);
} /* prev_lp  */

/* ------------------------------------------------------------------ */

/*
** filter_all - make all lines visible according to the parameter, those and only those;
**	special arguments are "alter" meaning altered lines, "selection",
**	"function" meaning functions and headers, and ":<linenumber>",
**	otherwise argument is handled as regexp
*/
int
filter_all (const char *expr)
{
	return (filter_base(FILTER_ALL, expr));
}

/*
** filter_more - make more lines visible according to the parameter;
**	special arguments are "alter" meaning altered lines, "selection",
**	"function" meaning functions and headers, and ":<linenumber>",
**	otherwise the argument is handled as regexp
*/
int
filter_more (const char *expr)
{
	return (filter_base(FILTER_MORE, expr));
}

/*
** filter_less - make less lines visible according to the parameter;
**	special arguments are "alter" meaning altered lines, "selection",
**	"function" meaning functions and headers, and ":<linenumber>",
**	otherwise the argument is handled as regexp
*/
int
filter_less (const char *expr)
{
	return (filter_base(FILTER_LESS, expr));
}

/*
** filter_m1 - make 1 line more visible around sequences of visible lines
**	(expand unhidden ranges)
*/
int
filter_m1 (void)
{
	int prev, masked;
	int fmask;
	LINE *lx;

	/* activate filter */
	CURR_FILE.fflag |= FMASK(CURR_FILE.flevel);
	fmask = FMASK(CURR_FILE.flevel);

	lx = CURR_FILE.top->next;
	prev = (lx->lflag & fmask);

	while (TEXT_LINE(lx)) {
		masked = (lx->lflag & fmask);
		if (prev != masked) {
			if (masked) {
				lx->lflag &= ~fmask;
			} else {
				lx->prev->lflag &= ~fmask;
			}
		}
		prev = masked;
		lx = lx->next;
	}

	return (0);
}

/* ------------------------------------------------------------------ */

/*
* filter engine for 'all', 'less' and 'more';
* "all" will show all lines, "less" will show no lines, "more" will do nothing;
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
		if (len >= 3 && strncmp(expr, "alter", len)==0) {
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

		} else if (len >= 3 && strncmp(expr, "selection", len)==0) {
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

		} else if (len >= 5 && strncmp(expr, "aliens", len)==0) {
			lx = CURR_FILE.top->next;
			lineno = 1;
			while (TEXT_LINE(lx)) {
				if (alien_count(lx) > 0) {
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

		} else if (len >= 4 && strncmp(expr, "function", len)==0) {
			ret = filter_func (action, fmask);

		} else if (len >= 2 && expr[0] == ':') {
			ret = 1;
			lineno = atoi(&expr[1]);
			if (lineno >= 1 && lineno <= CURR_FILE.num_lines) {
				if (action & FILTER_ALL) {
					lx = CURR_FILE.top->next;
					while (TEXT_LINE(lx)) {
						lx->lflag |= fmask;
						lx = lx->next;
					}
				}
				lx = lll_goto_lineno (cnf.ring_curr, lineno);
				if (TEXT_LINE(lx)) {
					CURR_LINE = lx;
					CURR_FILE.lineno = lineno;
					if (action & (FILTER_MORE | FILTER_ALL))
						lx->lflag &= ~fmask;
					else if (action & FILTER_LESS)
						lx->lflag |= fmask;
					ret = 0;
				}
			}

		} else if (len >= 3) {
			/* regcomp fail is possible */
			ret = filter_regex (action, fmask, expr);
		} else {
			/* the minimum length is required */
			return(0);
		}
	} /* len */

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

	/* temp view all */
	CURR_FILE.fflag &= ~fmask;
	if (CURR_FILE.ftype == C_FILETYPE) {
		ret = filter_func_eng_clang (action, fmask, NULL);
	} else if (CURR_FILE.ftype == PERL_FILETYPE || CURR_FILE.ftype == SHELL_FILETYPE) {
		ret = filter_func_eng_other (action, fmask, NULL);
	} else if (CURR_FILE.ftype == PYTHON_FILETYPE) {
		ret = filter_func_eng_easy (action, fmask, NULL);
	}
	/* temp view disabled */
	CURR_FILE.fflag |= fmask;

	return (ret);
}

/* copy the somewhat cleaner code from inbuff to outbuff without shifting bytes,
* just overwrite -- this is for C/C++ language sources;
* assumed the code is compilable, but multiline comments or strings can mislead;
* the preprocessor instructions can make confusion also
*/
void
purify_for_matching_clang (char *outb, const char *inbuff, int len)
{
	int offset, xch, i, missed=0;

	offset = 0;
	while (offset < len && inbuff[offset] != '\0') {
		outb[offset] = inbuff[offset];
		if (inbuff[offset] == '\'') {
			/* C literal with apostrophe 0x27 */
			offset++;
			if (offset+1 < len && inbuff[offset] != '\\' && inbuff[offset+1] == '\'') {
				/* C literal found */
				outb[offset] = ' ';
				outb[offset+1] = inbuff[offset+1];
				offset += 2;
			} else if (offset+2 < len && inbuff[offset] == '\\' && inbuff[offset+2] == '\'') {
				/* C literal found with char escape */
				outb[offset] = ' ';
				outb[offset+1] = ' ';
				outb[offset+2] = inbuff[offset+2];
				offset += 3;
			} else {
				missed++;
				FILT_LOG(LOG_NOTICE, "failed literal match offset=%d [%s]", offset, inbuff);
			}
		} else if (inbuff[offset] == '"') {
			/* one-line C string with double quotes 0x22 */
			xch = inbuff[offset];
			offset++;
			for (i = offset; i < len; i++) {
				if (inbuff[i-1] != '\\' && inbuff[i] == xch) {
					while (offset < i)
						outb[offset++] = ' ';
					outb[offset] = inbuff[offset];
					offset++;
					break;
				}
			}
			if (i > offset) {
				missed++;
				FILT_LOG(LOG_NOTICE, "failed string match offset=%d [%s]", offset, inbuff);
			}
		} else if (offset+1 < len && inbuff[offset] == '/' && inbuff[offset+1] == '/') {
			/* C++ comment found, clear to EoL */
			while (offset < len-1)
				outb[offset++] = ' ';
			outb[offset] = inbuff[offset];
			offset++;
			break;
		} else if (offset+1 < len && inbuff[offset] == '/' && inbuff[offset+1] == '*') {
			/* one-line C comments */
			offset++;
			outb[offset] = inbuff[offset];
			offset++;
			for (i = offset; i+1 < len; i++) {
				if (inbuff[i] == '*' && inbuff[i+1] == '/') {
					offset -= 2;
					while (offset <= i+1)
						outb[offset++] = ' ';
					break;
				}
			}
			if (i > offset) {
				missed++;
				FILT_LOG(LOG_NOTICE, "failed /**/ offset=%d", offset);
			}
		} else {
			offset++; /* skip and keep other chars */
		}
	}
	outb[offset] = '\0';

	if (missed + offset!=len) {
		;/* stateless function */
		FILT_LOG(LOG_NOTICE, "finished (missed=%d) fname=%s", missed + offset!=len, CURR_FILE.fname);
	}
	return;
}

/* copy the somewhat cleaner code from inbuff to outbuff without shifting bytes,
* just overwrite -- this is for Perl, Shell and similar language sources;
* multiline strings, here-documents and regex patterns can make confusion
*/
void
purify_for_matching_other (char *outb, const char *inbuff, int len)
{
	int offset, xch, i, missed=0;

	offset = 0;
	while (offset < len && inbuff[offset] != '\0') {
		outb[offset] = inbuff[offset];
		if (inbuff[offset] == '"' || inbuff[offset] == '\'') {
			/* one-line string with double quotes 0x22 or apostrophe 0x27 */
			xch = inbuff[offset];
			offset++;
			for (i = offset; i < len; i++) {
				if (inbuff[i-1] != '\\' && inbuff[i] == xch) {
					while (offset < i)
						outb[offset++] = ' ';
					outb[offset] = inbuff[offset];
					offset++;
					break;
				}
			}
			if (i > offset) {
				missed++;
				FILT_LOG(LOG_NOTICE, "failed string match offset=%d [%s]", offset, inbuff);
			}
		} else if ((offset==0 || inbuff[offset-1]==' ') && inbuff[offset] == '#') {
			/* comment found, clear to EoL --- think $#+ $#- $ARGV and ${#array} ${#@} $# etc */
			while (offset < len-1)
				outb[offset++] = ' ';
			outb[offset] = inbuff[offset];
			offset++;
			break;
		} else {
			offset++; /* skip and keep other chars */
		}
	}
	outb[offset] = '\0';

	if (missed + offset!=len) {
		;/* stateless function */
		FILT_LOG(LOG_NOTICE, "finished (missed=%d) fname=%s", missed + offset!=len, CURR_FILE.fname);
	}
	return;
}

/* function/block header patterns
*/
#define C_MATCHNAME		"([a-zA-Z_][a-zA-Z0-9:_]*)"
#define C_HEADER_ONE_PATTERN	C_MATCHNAME "[[:blank:]]*[(][^)]*[)]"
#define C_HEADER_TOP_PATTERN	C_MATCHNAME "[[:blank:]]*[(][,*a-zA-Z0-9:_[:blank:]]*$"
// on BoL but optionally with typedef
#define C_STRUCTURE_PATTERN	"(struct|enum|union)[[:blank:]]+" C_MATCHNAME "[^,()]*$"
// literal struct assignment
#define C_STRUCTURE_4PATTERN	"([^[:blank:]]+)[[:blank:]]*=[[:blank:]]*[{]"
// easy header patterns
#define PERL_MATCHNAME		"([.a-zA-Z0-9:_]+)"
#define PERL_HEADER_PATTERN	"^sub[[:blank:]]+" PERL_MATCHNAME
#define SHELL_MATCHNAME		"([a-zA-Z0-9_]+)"
#define SHELL_HEADER_PATTERN	"^function[[:blank:]]+" SHELL_MATCHNAME "|" "^" SHELL_MATCHNAME "[[:blank:]]*[(][)]"
#define PYTHON_MATCHNAME	"([a-zA-Z0-9_]+)"
#define PYTHON_HEADER_PATTERN	"^[[:blank:]]*(def|class)[[:blank:]]+" PYTHON_MATCHNAME ".*:" "|" "(__main__).?:"

/* headers can be very different, depending on indentation style,
* the closing curly brace seems to be a good startpoint to explore the hierarchy
*/
static int
filter_func_eng_clang (int action, int fmask, char *symbol)
{
	LINE *lx;
	regex_t reg1, reg2, reg3, reg4;
	const char *expr;
	regmatch_t pmatch[10];	/* match and sub match */
	int starting_lno;
	int lncol, lno, show_hide, searching_for_header;

	if (CURR_FILE.num_lines < 1)
		return 0;

	if (action & FILTER_ALL) {
		/* hide all lines */
		lx = CURR_FILE.top->next;
		while (TEXT_LINE(lx)) {
			lx->lflag |= fmask;
			lx = lx->next;
		}
	}

	starting_lno = CURR_FILE.lineno;
	if (action & FILTER_GET_SYMBOL) {
		/* get next block-end lno */
		lx = CURR_LINE;
		lno = CURR_FILE.lineno;
		while (TEXT_LINE(lx) && lx->buff[0] != '}') {
			lx = lx->next;
			lno++;
		}
	} else {
		/* get last block-end lno in the file */
		lx = CURR_FILE.bottom->prev;
		lno = CURR_FILE.num_lines;
		while (TEXT_LINE(lx) && lx->buff[0] != '}') {
			lx = lx->prev;
			lno--;
		}
	}
	if (!(TEXT_LINE(lx) && lx->buff[0] == '}'))
		return 0;

	expr = C_HEADER_ONE_PATTERN;
	if (regcomp (&reg1, expr, REGCOMP_OPTION)) {
		ERRLOG(0xE08B);
		return 1; /* internal regcomp failed */
	}
	expr = C_HEADER_TOP_PATTERN;
	if (regcomp (&reg2, expr, REGCOMP_OPTION)) {
		ERRLOG(0xE08A);
		regfree (&reg1);
		return 1; /* internal regcomp failed */
	}
	expr = C_STRUCTURE_PATTERN;
	if (regcomp (&reg3, expr, REGCOMP_OPTION)) {
		ERRLOG(0xE089);
		regfree (&reg1);
		regfree (&reg2);
		return 1; /* internal regcomp failed */
	}
	expr = C_STRUCTURE_4PATTERN;
	if (regcomp (&reg4, expr, REGCOMP_OPTION)) {
		ERRLOG(0xE088);
		regfree (&reg1);
		regfree (&reg2);
		regfree (&reg3);
		return 1; /* internal regcomp failed */
	}

	searching_for_header = 0;
	while (TEXT_LINE(lx)) {
		if (lx->buff[0] == '}') {
			if (action & (FILTER_MORE | FILTER_ALL))
				lx->lflag &= ~fmask;
			else if (action & FILTER_LESS)
				lx->lflag |= fmask;
			lncol = 0;
			lx = tomatch_eng (lx, &lno, &lncol, TOMATCH_DONT_SET_FOCUS | PURIFY_CLANG);
			if (!(TEXT_LINE(lx) && lx->buff[lncol] == '{'))
				break;
			searching_for_header = 1;

		} else {
			if (searching_for_header) {
				show_hide = 0;
				if (lx->buff[lncol] == '{') {
					show_hide = 1;
				}

				if ((lx->llen > 1) && ((lx->buff[0] == '*') || (lx->buff[0] == ' ' && lx->buff[1] == '*'))) {
					/* not a function header, maybe a comment ---> unhide, reset lncol, skip regex */
					show_hide = 0;
					lncol = 0;
				}
				else if (!regexec(&reg1, lx->buff, 10, pmatch, 0)) {
					show_hide = 1;
					searching_for_header = 0;
				}
				else if (!regexec(&reg2, lx->buff, 10, pmatch, 0)) {
					show_hide = 2;
					searching_for_header = 0;
				}
				else if ((lx->llen > 5) && (lx->buff[0]=='t' || lx->buff[0]=='s' || lx->buff[0]=='e' || lx->buff[0]=='u') &&
				!regexec(&reg3, lx->buff, 10, pmatch, 0)) {
					/* ^(?:typedef )?(struct|enum|union) */
					show_hide = 3;
					searching_for_header = 0;
				}
				else if (lncol > 5) {
					if ((lx->buff[lncol-1] == '=') || (lx->buff[lncol-2] == '=' && lx->buff[lncol-1] == ' '))
					{
						if (!regexec(&reg4, lx->buff, 10, pmatch, 0)) {
							show_hide = 4;
							searching_for_header = 0;
						}
					}
				}

				if (show_hide) {
					/* set flag for hide or clear to make visible */
					if (action & (FILTER_MORE | FILTER_ALL))
						lx->lflag &= ~fmask;
					else if (action & FILTER_LESS)
						lx->lflag |= fmask;
				}
				if (!searching_for_header) {
					if (action & FILTER_GET_SYMBOL) {
						if ((symbol != NULL) && (lno <= starting_lno)) {
							int ix, iy, nsub;
							// maybe 2 subpatterns
							nsub = (pmatch[2].rm_so >= 0 && pmatch[2].rm_so < pmatch[2].rm_eo) ? 2 : 1;
							// hack... sizeof(symbol) is TAGSTR_SIZE
							for (iy=0, ix = pmatch[nsub].rm_so; ix < pmatch[nsub].rm_eo && iy < TAGSTR_SIZE-1; ix++)
								symbol[iy++] = lx->buff[ix];
							symbol[iy] = '\0';
							FILT_LOG(LOG_NOTICE, "reg%d nsub %d -- %ld %ld symbol [%s]",
								searching_for_header, nsub, pmatch[nsub].rm_so, pmatch[nsub].rm_eo, symbol);
						}
						break;
					}
				}
			}

			lx = lx->prev;
			lno--;
		}
	}
	regfree (&reg1);
	regfree (&reg2);
	regfree (&reg3);
	regfree (&reg4);

	return 0;
}

/* the "main" is not necessarily a block, the source is often just flat,
* so the only reliable pattern is the header line, use the regexp match
*/
static int
filter_func_eng_other (int action, int fmask, char *symbol)
{
	LINE *lx;
	regex_t reg1;
	const char *expr;
	regmatch_t pmatch[10];	/* match and sub match */
	int lncol, lno, searching_for_brace;

	if (CURR_FILE.num_lines < 1)
		return 0;

	if (action & FILTER_ALL) {
		/* hide all lines */
		lx = CURR_FILE.top->next;
		while (TEXT_LINE(lx)) {
			lx->lflag |= fmask;
			lx = lx->next;
		}
	}

	if (CURR_FILE.ftype == PERL_FILETYPE) {
		expr = PERL_HEADER_PATTERN;
		if (regcomp (&reg1, expr, REGCOMP_OPTION)) {
			ERRLOG(0xE087);
			return 1; /* internal regcomp failed */
		}
	} else if (CURR_FILE.ftype == SHELL_FILETYPE) {
		expr = SHELL_HEADER_PATTERN;
		if (regcomp (&reg1, expr, REGCOMP_OPTION)) {
			ERRLOG(0xE086);
			return 1; /* internal regcomp failed */
		}
	} else {
		return 0;
	}

	if (action & FILTER_GET_SYMBOL) {
		lx = CURR_LINE;
		lno = CURR_FILE.lineno;
		while ((symbol != NULL) && TEXT_LINE(lx)) {
			if (!regexec(&reg1, lx->buff, 10, pmatch, 0)) {
				int ix, iy, nsub;
				// maybe 2 subpatterns
				nsub = (pmatch[2].rm_so >= 0 && pmatch[2].rm_so < pmatch[2].rm_eo) ? 2 : 1;
				// hack... sizeof(symbol) is TAGSTR_SIZE
				for (iy=0, ix = pmatch[nsub].rm_so; ix < pmatch[nsub].rm_eo && iy < TAGSTR_SIZE-1; ix++)
					symbol[iy++] = lx->buff[ix];
				symbol[iy] = '\0';
				FILT_LOG(LOG_NOTICE, "reg1 nsub %d -- %ld %ld symbol [%s]",
					nsub, pmatch[nsub].rm_so, pmatch[nsub].rm_eo, symbol);
				break;
			}

			lx = lx->prev;
			lno--;
		}
	} else {
		lx = CURR_FILE.top->next;
		lno = 1;
		searching_for_brace = 0;
		while (TEXT_LINE(lx)) {
			if (!regexec(&reg1, lx->buff, 10, pmatch, 0)) {
				if (action & (FILTER_MORE | FILTER_ALL))
					lx->lflag &= ~fmask;
				else if (action & FILTER_LESS)
					lx->lflag |= fmask;
				searching_for_brace = 1;
			}

			if (searching_for_brace) {
				/* opening brace on the header line or later */
				for (lncol = 0; lncol < lx->llen; lncol++) {
					if (lx->buff[lncol] == '{')
						break;
				}
				if (lx->buff[lncol] == '{') {
					if (action & (FILTER_MORE | FILTER_ALL))
						lx->lflag &= ~fmask;
					else if (action & FILTER_LESS)
						lx->lflag |= fmask;
					searching_for_brace = 0;

					lx = tomatch_eng (lx, &lno, &lncol, TOMATCH_DONT_SET_FOCUS | PURIFY_OTHER);
					if (!(TEXT_LINE(lx) && lx->buff[lncol] == '}'))
						break;

					if (action & (FILTER_MORE | FILTER_ALL))
						lx->lflag &= ~fmask;
					else if (action & FILTER_LESS)
						lx->lflag |= fmask;
				} else if (++searching_for_brace > 2) {
					/* checked the header and following line is enough */
					searching_for_brace = 0;
				}
			}

			lx = lx->next;
			lno++;
		}
	}
	regfree (&reg1);

	return 0;
}

/* no brace for hierarchy, use the regex pattern for match
*/
static int
filter_func_eng_easy (int action, int fmask, char *symbol)
{
	LINE *lx;
	regex_t reg1;
	const char *expr;
	regmatch_t pmatch[10];	/* match and sub match */

	if (CURR_FILE.num_lines < 1)
		return 0;

	if (CURR_FILE.ftype == PYTHON_FILETYPE) {
		expr = PYTHON_HEADER_PATTERN;
		if (regcomp (&reg1, expr, REGCOMP_OPTION)) {
			ERRLOG(0xE085);
			return 1; /* internal regcomp failed */
		}
	} else {
		return 0;
	}

	if (action & FILTER_GET_SYMBOL) {
		lx = CURR_LINE;
		while ((symbol != NULL) && TEXT_LINE(lx)) {
			if (!regexec(&reg1, lx->buff, 10, pmatch, 0)) {
				int ix, iy, nsub;
				// even 3 subpatterns
				nsub = (pmatch[3].rm_so >= 0 && pmatch[2].rm_so < pmatch[3].rm_eo) ? 3 :
					((pmatch[2].rm_so >= 0 && pmatch[2].rm_so < pmatch[2].rm_eo) ? 2 : 1);
				// hack... sizeof(symbol) is TAGSTR_SIZE
				for (iy=0, ix = pmatch[nsub].rm_so; ix < pmatch[nsub].rm_eo && iy < TAGSTR_SIZE-1; ix++)
					symbol[iy++] = lx->buff[ix];
				symbol[iy] = '\0';
				FILT_LOG(LOG_NOTICE, "reg1 nsub %d -- %ld %ld symbol [%s]",
					nsub, pmatch[nsub].rm_so, pmatch[nsub].rm_eo, symbol);
				break;
			}
			lx = lx->prev;
		}
	} else {
		lx = CURR_FILE.top->next;
		while (TEXT_LINE(lx)) {
			if (!regexec(&reg1, lx->buff, 10, pmatch, 0)) {
				if (action & (FILTER_MORE | FILTER_ALL))
					lx->lflag &= ~fmask;
				else if (action & FILTER_LESS)
					lx->lflag |= fmask;
			} else {
				if (action & FILTER_ALL)
					lx->lflag |= fmask;
			}
			lx = lx->next;
		}
	}
	regfree (&reg1);

	return 0;
}

/*
* return current block or function name
* (returned pointer maybe NULL, otherwise must be freed by caller)
*/
char *
block_name (int ri)
{
	char *symbol=NULL;
	int orig_ri, action, fmask, mask_active;

	if (ri < 0 || ri >= RINGSIZE || !(cnf.fdata[ri].fflag & FSTAT_OPEN))
		return (symbol);

	symbol = (char *) MALLOC(TAGSTR_SIZE);
	if (symbol == NULL) {
		ERRLOG(0xE021);
		return (symbol);
	}
	strncpy(symbol, "(unknown)", 10); /* TAGSTR_SIZE */

	/* CURR_FILE must be set for the engine */
	orig_ri = cnf.ring_curr;
	cnf.ring_curr = ri;

	fmask = FMASK(CURR_FILE.flevel);
	mask_active = (LMASK(cnf.ring_curr) != 0);

	/* temp view all */
	if (mask_active)
		CURR_FILE.fflag &= ~fmask;

	action = FILTER_GET_SYMBOL;
	if (CURR_FILE.ftype == C_FILETYPE) {
		filter_func_eng_clang (action, fmask, symbol);
		FILT_LOG(LOG_NOTICE, "symbol: clang [%s]", symbol); /* for testing */
	} else if (CURR_FILE.ftype == PERL_FILETYPE || CURR_FILE.ftype == SHELL_FILETYPE) {
		filter_func_eng_other (action, fmask, symbol);
		FILT_LOG(LOG_NOTICE, "symbol: other [%s]", symbol); /* for testing */
	} else if (CURR_FILE.ftype == PYTHON_FILETYPE) {
		filter_func_eng_easy (action, fmask, symbol);
		FILT_LOG(LOG_NOTICE, "symbol: easy [%s]", symbol); /* for testing */
	}

	/* restore temp view */
	if (mask_active)
		CURR_FILE.fflag |= fmask;

	/* restore CURR_FILE */
	cnf.ring_curr = orig_ri;

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

/* ------------------------------------------------------------------ */

/* re-allocate space for common tasks, never less
*/
int
common_space (int length)
{
	char *ptr;
	unsigned als = (length > 0) ? (unsigned)length : 1;

	if (ALLOCSIZE(als) > cnf.temp_als) {
		cnf.temp_als = ALLOCSIZE(als);
		ptr = (char *) REALLOC((void *)cnf.temp_buffer, cnf.temp_als);
		if (ptr == NULL) {
			ERRLOG(0xE001);
			cnf.temp_buffer = NULL;
			cnf.temp_als = 0;
		} else {
			cnf.temp_buffer = ptr;
		}
	}

	return (cnf.temp_buffer == NULL);
}

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
	if (!TEXT_LINE(lp))
		return (0); /* cannot start from top or bottom */

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
tomatch_eng (LINE *lp, int *io_lineno, int *io_lncol, int config_bits)
{
	LINE *ret_lp=NULL;
	int lineno, restore_focus, lncol;
	int fcnt, found;
	char chcur, tofind, dir, delim, ch;
	int fallback = !(config_bits & TOMATCH_WITH_PURIFY);
	char *tmpbuff;

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
			if (!fallback && !common_space(lp->llen)) {
				tmpbuff = cnf.temp_buffer;
				if (config_bits & PURIFY_CLANG)
					purify_for_matching_clang (tmpbuff, lp->buff, lp->llen);
				else
					purify_for_matching_other (tmpbuff, lp->buff, lp->llen);
			} else {
				tmpbuff = lp->buff; /* failure */
				fallback = 2;
			}

			/* search in this line (skip 0x0a) */
			while (lncol < lp->llen-1) {
				ch = tmpbuff[lncol];
				if (ch == chcur) {
					++delim;
				} else if (ch == tofind && --delim == 0) {
					found=1;
					break;
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
			if (!fallback && !common_space(lp->llen)) {
				tmpbuff = cnf.temp_buffer;
				if (config_bits & PURIFY_CLANG)
					purify_for_matching_clang (tmpbuff, lp->buff, lp->llen);
				else
					purify_for_matching_other (tmpbuff, lp->buff, lp->llen);
			} else {
				tmpbuff = lp->buff; /* failure */
				fallback = 2;
			}

			/* search in this line */
			while (lncol >= 0) {
				ch = tmpbuff[lncol];
				if (ch == chcur) {
					++delim;
				} else if (ch == tofind && --delim == 0) {
					found=1;
					break;
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
		if (config_bits & TOMATCH_SET_FOCUS) {
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
	if (!TEXT_LINE(lp))
		return (0); /* cannot start from top or bottom */

	lineno = CURR_FILE.lineno;
	lncol = CURR_FILE.lncol;
	fmask = FMASK(CURR_FILE.flevel);
	mask_active = (LMASK(cnf.ring_curr) != 0);

	if (mask_active)
		CURR_FILE.fflag &= ~fmask;

	if (CURR_FILE.ftype == C_FILETYPE) {
		lp = tomatch_eng (lp, &lineno, &lncol, TOMATCH_DONT_SET_FOCUS | PURIFY_CLANG);
	} else if (CURR_FILE.ftype == PERL_FILETYPE ||
		CURR_FILE.ftype == SHELL_FILETYPE ||
		CURR_FILE.ftype == PYTHON_FILETYPE) {
		lp = tomatch_eng (lp, &lineno, &lncol, TOMATCH_DONT_SET_FOCUS | PURIFY_OTHER);
	} else { /* TEXT_FILETYPE */
		lp = tomatch_eng (lp, &lineno, &lncol, TOMATCH_DONT_SET_FOCUS);
	}

	if (lp != NULL) {
		/* unhide */
		lp->lflag &= ~fmask;

		CURR_LINE = lp;
		CURR_FILE.lineno = lineno;
		CURR_FILE.lncol = lncol;
		update_curpos(cnf.ring_curr);
		update_focus(CENTER_FOCUSLINE, cnf.ring_curr);
		FILT_LOG(LOG_NOTICE, "success: lncol %d line [%s]", CURR_FILE.lncol, CURR_LINE->buff); /* for testing */
	}

	if (mask_active)
		CURR_FILE.fflag |= fmask;

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
	if (!TEXT_LINE(lp))
		return (0); /* cannot start from top or bottom */
	lineno = CURR_FILE.lineno;
	lncol = CURR_FILE.lncol;
	fmask = FMASK(CURR_FILE.flevel);
	mask_active = (LMASK(cnf.ring_curr) != 0);

	if (mask_active)
		CURR_FILE.fflag &= ~fmask;

	if (CURR_FILE.ftype == C_FILETYPE) {
		lp = tomatch_eng (lp, &lineno, &lncol, TOMATCH_DONT_SET_FOCUS | PURIFY_CLANG);
	} else if (CURR_FILE.ftype == PERL_FILETYPE ||
		CURR_FILE.ftype == SHELL_FILETYPE ||
		CURR_FILE.ftype == PYTHON_FILETYPE) {
		lp = tomatch_eng (lp, &lineno, &lncol, TOMATCH_DONT_SET_FOCUS | PURIFY_OTHER);
	} else { /* TEXT_FILETYPE */
		lp = tomatch_eng (lp, &lineno, &lncol, TOMATCH_DONT_SET_FOCUS);
	}
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

	if (mask_active)
		CURR_FILE.fflag |= fmask;

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
	int lineno, lncol;
	int fmask=0;
	int mask_active=0, do_unhide=0;
	int hidden_to_end=0, hidden_to_head=0;

	fmask = FMASK(CURR_FILE.flevel);
	mask_active = (LMASK(cnf.ring_curr) != 0);

	/* try to find block end... */
	lx = CURR_LINE;
	lineno = CURR_FILE.lineno;
	lncol = 0;
	while (TEXT_LINE(lx)) {
		if (lx->llen > 0 && lx->buff[lncol] == '}')
			break;
		lx = lx->next;
		lineno++;
		if (lx->lflag & fmask) hidden_to_end++;
	}
	if (!TEXT_LINE(lx))
		return (1);	/* block end not found */

	/* found, save block-end for the loop */
	lp_end = lx;

	if (mask_active)
		CURR_FILE.fflag &= ~fmask;

	if (CURR_FILE.ftype == C_FILETYPE) {
		lx = tomatch_eng (lp_end, &lineno, &lncol, TOMATCH_DONT_SET_FOCUS | PURIFY_CLANG);
	} else if (CURR_FILE.ftype == PERL_FILETYPE ||
		CURR_FILE.ftype == SHELL_FILETYPE ||
		CURR_FILE.ftype == PYTHON_FILETYPE) {
		lx = tomatch_eng (lp_end, &lineno, &lncol, TOMATCH_DONT_SET_FOCUS | PURIFY_OTHER);
	} else { /* TEXT_FILETYPE */
		lx = tomatch_eng (lp_end, &lineno, &lncol, TOMATCH_DONT_SET_FOCUS);
	}
	if (!(TEXT_LINE(lx) && lineno <= CURR_FILE.lineno+1)) {
		/* start lineno is not in the detected block, or block match failed */
		if (mask_active)
			CURR_FILE.fflag |= fmask;
		return (1);	/* no match */
	}

	/* found, save block-head for the loop */
	lp_head = lx;

	if (mask_active)
		CURR_FILE.fflag |= fmask;

	lx = lp_head;
	while (TEXT_LINE(lx)) {
		if (lx == CURR_LINE) {
			break;
		}
		lx = lx->next;
		if (lx->lflag & fmask) hidden_to_head++;
	}

	if (((lp_head->lflag & fmask)==0) && ((lp_end->lflag & fmask)==0)) {
		/* header and footer is visible, change internal lines */

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
