/*
* tags.c 
* support functions to use "tags" file from ctags, parse tags file, use saved information to jump to definition
*
* Copyright 2003-2014 Attila Gy. Molnar
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
#include <stdlib.h>	/* strtol */
#include <errno.h>
#include <syslog.h>
#include "main.h"
#include "proto.h"

/* global config */
extern CONFIG cnf;

/* local proto */
static TAG *tag_add (TAG *tp);
static TAG *tag_rm (TAG *tp);
static void get_tags_path_back (void);
static int tag_do (const char *arg_symbol, int flag);
static int tag_items (const char *symbol, TAGTYPE type, int flag);
static int tag_jump2_pattern (const char *fname, const char *pattern, int lineno);
static const char *tag_pattern_safe (const char *pattern);

/* regexp specials are ^.[$()|*+?{\\ and maybe }] */

/* local flag bits */
#define JUMP_TO		0x80
#define MEMBER_SEARCH	0x01
#define PARENT_SEARCH	0x02

/*
* add element after tp
* return with the new pointer next after tp, or NULL
*/
static TAG *
tag_add (TAG *tp)
{
	TAG *tx = NULL;

	if ((tx = (TAG *) MALLOC(sizeof(TAG))) == NULL) {
		return NULL;
	}
	if (tp == NULL) {
		/* root of chain */
		tx->next = NULL;
	} else {
		/* bind-in after tp */
		tx->next = tp->next;	/* save */
		tp->next = tx;
	}
	tx->symbol = NULL;
	tx->fname = NULL;
	tx->type = TAG_UNDEF;
	tx->lineno = 0;
	tx->pattern = NULL;

	return (tx);
} /* tag_add */

/*
* remove tp (if not NULL)
* return with the next element (or NULL)
*/
static TAG *
tag_rm (TAG *tp)
{
	TAG *tx = NULL;

	if (tp != NULL) {
		tx = tp->next;		/* save */
		FREE(tp->symbol); tp->symbol = NULL;
		FREE(tp->fname); tp->fname = NULL;
		if (tp->pattern != NULL) {
			FREE(tp->pattern); tp->pattern = NULL;
		}
		FREE(tp); tp = NULL;
	}

	return (tx);
} /* tag_rm */

/*
* save the relative path of current dir to "tags" file in cnf.tag_j2path[] and cnf.tag_j2len,
* based on cnf.tags_file and cnf._pwd --experimental--
*/
static void
get_tags_path_back (void)
{
	int i=0, j=0, k=0;

	cnf.tag_j2path[0] = '\0';
	cnf.tag_j2len = 0;

	if (cnf.tags_file[0] == '/') {
		strncpy(cnf.tag_j2path, cnf._pwd, sizeof(cnf.tag_j2path));
		cnf.tag_j2path[sizeof(cnf.tag_j2path)-1] = '\0';

		for (; cnf.tags_file[i] != '\0'; i++) {
			if (cnf.tags_file[i] == '/') {
				j = i;
			}
		}
		for (i=0; i < j && cnf.tag_j2path[i] != '\0'; i++) {
			if (cnf.tags_file[i] != cnf.tag_j2path[i]) {
				break;
			}
		}

		if (i == j) {
			/* count directory levels from tags file to curdir */
			k = 0;
			for (i=j+1; cnf.tag_j2path[i] != '\0'; i++) {
				if (cnf.tag_j2path[i] == '/') {
					k++;
				}
			}
			if (i > j+1 && cnf.tag_j2path[i-1] != '/') {
				k++;	/* the last dir */
			}
			if (k > 0 && 3*k < SHORTNAME-1) {
				cnf.tag_j2path[0] = '\0';
				cnf.tag_j2len = 0;
				while (--k >= 0) {
					strncat(cnf.tag_j2path, "../", 4);
					cnf.tag_j2len += 3;
				}
			}
		}
	} else {
		j = -1;
		i = k = 0;
		if (cnf.tags_file[0] == '.' && cnf.tags_file[1] == '/') {
			i = 2;
		}
		while (cnf.tags_file[i] != '\0' && k < (int)sizeof(cnf.tag_j2path)-1) {
			cnf.tag_j2path[k] = cnf.tags_file[i];
			if (cnf.tags_file[i] == '/') {
				j = k;
			}
			i++;
			k++;
		}
		cnf.tag_j2path[j+1] = '\0';
		cnf.tag_j2len = j+1;
	}

	TAGS_LOG(LOG_DEBUG, "tags file [%s] -> path to tags [%s] %d", cnf.tags_file, cnf.tag_j2path, cnf.tag_j2len);
}

/*
*
* global functions
*
*/

/*
** tag_load_file - load or reload the content of "tags" file as configured in "tags_file" resource
*/
int
tag_load_file (void)
{
	FILE *fp;
	TAG *tp = NULL;
	char *buff = NULL;
	char *pp=NULL;
	unsigned tlen, i, x, j;		/* i:pos x:length j:pos */
	int chunk=0, comments=0, line_format=0;
	int ret=0, err1=0, err2=0, err3=0;
	unsigned als=0;

	if (cnf.taglist != NULL) {
		tag_rm_all();
	}

	glob_name(cnf.tags_file, sizeof(cnf.tags_file));
	if ((fp = fopen(cnf.tags_file,"r")) == NULL) {
		tracemsg("cannot open tags file [%s]", cnf.tags_file);
		return (0);
	}
	get_tags_path_back();	/* experimental */

	buff = MALLOC(TAGLINE_SIZE);
	if (buff == NULL) {
		TAGS_LOG(LOG_ERR, "cannot allocate memory for reading tags file");
		err2 = 1;
		ret = 1;
	}

	while (ret==0 && err2==0)
	{
		if (fgets (buff, TAGLINE_SIZE, fp) == NULL) {
			if (ferror(fp)) {
				ret=2;
			}
			/* drop this line */
			break;	/* eof or error : end */
		}

		/* drop comment line and continue */
		if (buff[0] == '!') {
			comments++;
			continue;
		}

		/* get new pointer next after tp, or NULL */
		tp = tag_add(tp);
		if (tp == NULL) {
			TAGS_LOG(LOG_ERR, "failed (malloc)");
			ret=3;
			err2++;
			break;
		}

		/* save header once, later the tp pointers update the tail, where tp->next==NULL */
		if (cnf.taglist == NULL) {
			cnf.taglist = tp;
		}

		/*
		* symbol<TAB>fname<TAB>lineno;"<TAB>type
		* symbol<TAB>fname<TAB>lineno;"<TAB>type<TAB>extension_fields_optional
		* symbol<TAB>fname<TAB>pattern;"<TAB>type
		* symbol<TAB>fname<TAB>pattern;"<TAB>type<TAB>extension_fields_optional
		*
		* symbol<TAB>fname<TAB>lineno
		* symbol<TAB>fname<TAB>pattern
		*/

		tlen = strlen(buff);
		/* replace '\n' */
		tlen--;
		while (tlen > 0 && (buff[tlen-1]=='\n' || buff[tlen-1]=='\r')) {
			tlen--;
		}
		buff[tlen] = '\0';

		/* get symbol */
		i=0;
		pp = &buff[i];
		for (x=0; i<tlen && buff[i]!='\t'; i++, x++)
			;
		buff[i++] = '\0';
		als = ALLOCSIZE(x+1);
		if ((tp->symbol = (char *) MALLOC(als)) != NULL)
			strncpy(tp->symbol, pp, x+1);
		else
			err2++;		/* err2 : malloc error */

		/* get fname */
		pp = &buff[i];
		for (x=0; i<tlen && buff[i]!='\t'; i++, x++)
			;
		buff[i++] = '\0';
		als = ALLOCSIZE(x+1);
		if ((tp->fname = (char *) MALLOC(als)) != NULL)
			strncpy(tp->fname, pp, x+1);
		else
			err2++;		/* err2 : malloc error */

		/* get all the rest */
		pp = &buff[i];
		j = i;
		for (x=0; i<tlen && !(buff[i]==';' && buff[i+1]=='"'); i++, x++)
			;
		if (buff[i] == ';') {
			/* extended format */
			buff[i] = '\0';		/* close pp, lineno or pattern */
			i += 3;			
			line_format = 2;
		}
		else if (buff[j] >= '0' && buff[j] <= '9' && buff[i-1] >= '0' && buff[i-1] <= '9') {
			buff[i] = '\0';		/* simple lineno */
			line_format = 0;
		}
		else if (buff[j] == '/' && buff[i-1] == '/') {
			buff[i] = '\0';		/* simple pattern */
			line_format = 1;
		}
		else {
			TAGS_LOG(LOG_ERR, "(line-format) chunk=%d : symbol [%s] pp [%s] ",
				chunk+comments, tp->symbol, pp);
			err1++;		/* err1 : wrong line format */
			line_format = -1;
		}

		tp->pattern = NULL;
		if (line_format == 0) {
			tp->type = TAG_DEFINE;
			tp->lineno = (int) strtol(pp, NULL, 10);
		}
		else if (line_format == 1) {
			tp->type = TAG_FUNCTION;	/* or TAG_VARIABLE */
			tp->lineno = 0;
			als = ALLOCSIZE(x+1);
			if ((tp->pattern = (char *) MALLOC(als)) != NULL)
				strncpy(tp->pattern, pp, x+1);
			else
				err2++;		/* err2 : malloc error */
		}
		else if (line_format == 2) {	/* extended */
			/* get the type */
			tp->type = buff[i++];

			/*
			 * lineno or pattern --- mandatory
			 */
			if (tp->type == TAG_DEFINE) {
				tp->lineno = (int) strtol(pp, NULL, 10);
			} else if (tp->type != TAG_UNDEF) {
				tp->lineno = 0;
				als = ALLOCSIZE(x+1);
				if ((tp->pattern = (char *) MALLOC(als)) != NULL)
					strncpy(tp->pattern, pp, x+1);
				else
					err2++;		/* err2 : malloc error */
			} else {
				TAGS_LOG(LOG_ERR, "(unknown-tagtype) chunk=%d symbol [%s] type 0x%X",
					chunk+comments, tp->symbol, tp->type);
				err3++;		/* err3 : unknown tagtype */
				tp->type = TAG_UNDEF;
			}

		}/* line_format */

		chunk++;
	}/*while*/
	fclose(fp);

	cnf.trace=0;	/* drop previous msgs */
	if (ret || err2) {
		tracemsg ("tags load failed at %d", comments+chunk);
		TAGS_LOG(LOG_ERR, "chunks=%d ret=%d, (errors: format=%d malloc=%d tagtype=%d)",
			chunk+comments, ret, err1, err2, err3);
	} else {
		tracemsg ("%d symbols loaded: OK", chunk);
		TAGS_LOG(LOG_INFO, "chunks=%d comments=%d", chunk, comments);
	}

	FREE(buff);
	buff = NULL;
	return (ret);
} /* tag_load_file */

/*
* remove all tag items; free the tree
*/
int
tag_rm_all (void)
{
	TAG *tp = cnf.taglist;

	while (tp != NULL) {
		tp = tag_rm(tp);
	}
	cnf.taglist = NULL;

	return (0);
} /* tag_rm_all */

/*
** tag_view_info - view symbol definition in a notification, gained from "tags" file
*/
int
tag_view_info (const char *arg_symbol)
{
	int ret=0;

	ret = tag_do (arg_symbol, 0);

	return (ret==0);
} /* tag_view_info */

/*
** tag_jump_to - jump to the symbol definition, based on information gained from "tags" file
*/
int
tag_jump_to (const char *arg_symbol)
{
	int ret=0;

	/* arg_symbol is maybe ""
	 * possible call from main.c before cnf.bootup
	 */
	ret = tag_do (arg_symbol, JUMP_TO);

	return (ret!=1);
} /* tag_jump_to */

/* come back from the last tag jump (obsolete)
*/
int
tag_jump_back (void)
{
	/* return to the start position of tag_jump2_pattern() */
	return (mhist_pop());
} /* tag_jump_back */

/*
*
* other static functions
*
*/

/*
* start point for tag search
*/
static int
tag_do (const char *arg_symbol, int flag)
{
	const char *symbol=NULL;
	char *get_symbol=NULL;
	int ret=0;

	if (cnf.taglist == NULL) {
		tracemsg ("tags file [%s] not loaded", cnf.tags_file);
		TAGS_LOG(LOG_WARNING, "tags file [%s] not loaded", cnf.tags_file);
	} else {
		if (arg_symbol[0] == '\0') {
			get_symbol = select_word(CURR_LINE, CURR_FILE.lncol);
			symbol = get_symbol;
		} else {
			symbol = arg_symbol;
		}

		if (symbol != NULL && symbol[0] != '\0') {
			if (symbol[0] == '.' || symbol[0] == '>') {
				symbol++;
			}
			ret = tag_items (symbol, TAG_UNDEF, flag);	/* start to search... */

			/* failure messages here */
			if (flag & JUMP_TO) {
				if (ret == 0) {
					tracemsg ("cannot jump to symbol \"%s\".", symbol);
				} else if (ret > 1) {
					; /* multiple match -- ok */
				} else if (ret < 0) {
					tracemsg ("symbol \"%s\" search failed", symbol);
				}
			} else {
				if (ret == 0) {
					tracemsg ("cannot show symbol \"%s\".", symbol);
				}
			}
		} else {
			tracemsg ("no symbol given");
		}
		FREE(get_symbol); get_symbol = NULL;
	}

	return (ret);	/* symbols found */
} /* tag_do */

/*
* symbol<TAB>fname<TAB>lineno;"<TAB>type
* symbol<TAB>fname<TAB>lineno;"<TAB>type<TAB>extension_fields_optional
* symbol<TAB>fname<TAB>pattern;"<TAB>type
* symbol<TAB>fname<TAB>pattern;"<TAB>type<TAB>extension_fields_optional
*
* symbol<TAB>fname<TAB>lineno
* symbol<TAB>fname<TAB>pattern
*/

/*
* type: TAG_UNDEF or filter search
* flag: JUMP_TO
* return value: matching symbols found
*/
static int
tag_items (const char *symbol, TAGTYPE type, int flag)
{
	TAG *tp = cnf.taglist;
	int count = 0;
	const char *jump_fname = "ah";
	const char *jump_pattern = "oh";
	int jump_lineno = 0;
	static int saved_count = -100;
	static char saved_symbol[TAGSTR_SIZE] = "";

	if (saved_count == -100) {
		/* runtime init */
		memset(saved_symbol, 0, sizeof(saved_symbol));
		saved_count = -1;
	}

	if (flag & JUMP_TO) {
		if ((saved_count == -1) || (strncmp(saved_symbol, symbol, sizeof(saved_symbol)-1) != 0)) {
			memset(saved_symbol, 0, sizeof(saved_symbol));
			strncat(saved_symbol, symbol, sizeof(saved_symbol)-1);
			saved_count = 0;
		}
	}

	TAGS_LOG(LOG_DEBUG, "query symbol [%s] type %c flag 0x%X", symbol, (type==TAG_UNDEF ? '?' : type), flag);

	while (tp != NULL)
	{
		if ((strncmp (tp->symbol, symbol, TAGSTR_SIZE) == 0) &&
			((type == TAG_UNDEF) || (type == tp->type)))
		{
			TAGS_LOG(LOG_DEBUG, "match symbol [%s] fname [%s] type %c lineno %d pattern [%s]",
				tp->symbol, tp->fname, tp->type, tp->lineno, tp->pattern);

			if (flag & JUMP_TO) {
				if (tp->type == TAG_DEFINE) {
					if (count==0 || count==saved_count) {
						jump_fname = tp->fname;
						jump_pattern = NULL;
						jump_lineno = tp->lineno;
					}
					count++;
				} else if (tp->type != TAG_UNDEF) {
					if (count==0 || count==saved_count) {
						jump_fname = tp->fname;
						jump_pattern = tp->pattern;
						jump_lineno = -1;
					}
					count++;
				} else {
					tracemsg ("(type:%c) %s %s %s", tp->type, tp->symbol, tp->fname, tp->pattern);
				}
			} else {
				if (tp->type == TAG_DEFINE) {
					if (show_define(tp->fname, tp->lineno)) {
						/* failed */
						tracemsg ("?not found: [%s] %s :%d", tp->symbol, tp->fname, tp->lineno);
					}
					count++;
				} else {
					if (tp->type != TAG_UNDEF)
						count++;
					tracemsg ("(type:%c) %s %s %s", tp->type, tp->symbol, tp->fname, tp->pattern);
				}
			}
		}

		tp = tp->next;
	}

	if ((flag & JUMP_TO) && (count > 0)) {
		saved_count++;
		if (tag_jump2_pattern (jump_fname, jump_pattern, jump_lineno)) {
			/* failed */
			count = -count;
			saved_count = -1;
		} else {
			if (count > 1) {
				tracemsg ("symbol \"%s\" multiple match (%d of %d)", 
					symbol, saved_count, count);
			}
		}
		if (saved_count == count) {
			saved_count = -1;
		}
	}

	return (count);	/* number of symbols found */
} /* tag_items */

/*
* do the hard work in tag searching
*/
static int
tag_jump2_pattern (const char *fname, const char *pattern, int lineno)
{
	int ret=1;
	int from_ring, from_lineno;
	LINE *lx = NULL;
	const char *expr;
	int lineno2;
	const char *fnp=NULL;
	int rest = 0;

	from_ring = cnf.ring_curr;
	from_lineno = cnf.fdata[cnf.ring_curr].lineno;

	if (fname[0] == '/') {
		fnp = fname;
	} else {
		cnf.tag_j2path[cnf.tag_j2len] = '\0';
		rest = sizeof(cnf.tag_j2path) - cnf.tag_j2len;
		if (rest > 0) {
			strncat(&cnf.tag_j2path[cnf.tag_j2len], fname, rest);
			fnp = cnf.tag_j2path;
		} else {
			TAGS_LOG(LOG_ERR, "tag jump, cannot get path to %s from tags file", fname);
			ret = 1;
		}
	}

	/* open file or switch to
	 */
	if (fnp != NULL && add_file(fnp) == 0) {
		/* cnf.ring_curr already set by add_file() */

		if (CURR_FILE.fflag & FSTAT_SCRATCH) {
			TAGS_LOG(LOG_ERR, "file %s seems to be scratch", fnp);
			ret=2;

		} else if (lineno >= 1 && lineno <= CURR_FILE.num_lines) {
			TAGS_LOG(LOG_DEBUG, "ri=%d lineno %d", cnf.ring_curr, lineno);
			lx = lll_goto_lineno (cnf.ring_curr, lineno);
			if (TEXT_LINE(lx))
				ret = 0;

		} else if (pattern != NULL) {
			expr = tag_pattern_safe (pattern);
			TAGS_LOG(LOG_DEBUG, "ri=%d pattern-expr [%s]", cnf.ring_curr, expr);
			lx = search_goto_pattern (cnf.ring_curr, expr, &lineno2);
			lineno = lineno2;
			if (TEXT_LINE(lx))
				ret = 0;
		}
	}

	if (ret == 0 && lx != NULL) {
		if (cnf.bootup) {
			mhist_push (from_ring, from_lineno);
		}
		set_position (cnf.ring_curr, lineno, lx);
	} else {
		cnf.ring_curr = from_ring;
	}

	return (ret);
} /* tag_jump2_pattern */

/*
* copy characters until ';' and '"' or final '\0' comes
* remove the first and occasionally the last delimiter slash
*/
static const char *
tag_pattern_safe (const char *pattern)
{
	static char safe_pattern[TAGSTR_SIZE];
	int i=0, j=0, option=0;

	if (pattern[i] == '/') {
		/* skip this and remember to skip at the end */
		option |= 0x1;
		i++;
	}
	for (; pattern[i] != '\0' && j<TAGSTR_SIZE-2; i++) {
		if (pattern[i] == ';' && pattern[i+1] == '"')
			break;
		if (pattern[i] == '*' || pattern[i] == '[' || pattern[i] == ']') {
			safe_pattern[j++] = '\\';
		}
		safe_pattern[j++] = pattern[i];
	}
	if ((option & 0x1) && j>1 && safe_pattern[j-1] == '/') {
		/* drop final slash too (if any) */
		j--;
	}
	safe_pattern[j++] = '\0';

	return (safe_pattern);
} /* tag_pattern_safe */
