/*
* disp.c
* all the original character based display implementation
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
#include "main.h"
#include "proto.h"

/* global config */
extern CONFIG cnf;

typedef enum color_attribute_type_tag
{
	COLOR_INIT = 0,

	COLOR_NORMAL_TEXT = 1,
	COLOR_TAGGED_TEXT = 2,
	COLOR_HIGH_TEXT   = 3,
	COLOR_SEARCH_TEXT = 4,

	COLOR_FOCUS_FLAG = 4,	/* 5 6 7 8 */
	COLOR_SELECT_FLAG = 8,	/* 9 10 11 12  13 14 15 16 */

	COLOR_TRACEMSG_TEXT   = 17,
	COLOR_STATUSLINE_TEXT = 18,
	COLOR_CMDLINE_TEXT    = 19,
	COLOR_SHADOW_TEXT   = 20,

	COLOR_COUNT
} color_attribute_type;

/* local proto */
static int add_to_obuff(const char *disp_opts, char *obuff, int *bufflen);
static attr_t set_attr (color_attribute_type base, int lflag, int focus_flag);
static void map_match_to_out (const char *ibuff, int imatch_so, int imatch_eo, int *pos, int padsize, int focus, const char *obuff);
static void text_line (LINE *lp, int lineno, int focus, int focus_flag);
static const char *center_fname (const char *ibuff, const char *catbuff, int padsize);
static void shadow_line (int count, int focus);
static void empty_line (int focus);

/* local variables */
static attr_t at[COLOR_COUNT];

static int
add_to_obuff(const char *disp_opts, char *obuff, int *bufflen)
{
	int ix=0, len=0;
	len = *bufflen;

	while (disp_opts[ix] == '%') {
		ix++; /* skip '%' */
		if (disp_opts[ix] == 'i') {
			/* ring index/size */
			snprintf (obuff + len, 15, "(%d/%d) ", cnf.ring_curr+1, cnf.ring_size);
			len += strlen(obuff+len);
			ix++;
		} else if (disp_opts[ix] == 'f') {
			snprintf (obuff + len, 15, "Filter %c%d ",
				(LMASK(cnf.ring_curr) ? '=' : '*'), CURR_FILE.flevel);
			len += strlen(obuff+len);
			ix++;
		} else if (disp_opts[ix] == 'l') {
			snprintf (obuff + len, 15, "Lines %d ", CURR_FILE.num_lines);
			len += strlen(obuff+len);
			ix++;
		} else if (disp_opts[ix] == 'p') {
			/* relative position in the window */
			snprintf (obuff + len, 20, "(%d,%d) ", CURR_FILE.focus, (CURR_FILE.curpos-CURR_FILE.lnoff));
			len += strlen(obuff+len);
			ix++;
		} else if (disp_opts[ix] == 'r') {
			snprintf(obuff + len, 20, "Row %d Col %d ", CURR_FILE.lineno, CURR_FILE.curpos);
			len += strlen(obuff+len);
			ix++;
		} else if (disp_opts[ix] == 'x') {
			/* hex character display */
			if (CURR_FILE.lncol < CURR_LINE->llen) {
				snprintf (obuff + len, 20, "(0x%02X) ", (unsigned char)CURR_LINE->buff[CURR_FILE.lncol]);
			} else {
				snprintf (obuff + len, 20, "(0x  ) ");
			}
			len += strlen(obuff+len);
			ix++;
		} else {
			break;
		}
	}

	*bufflen = len;
	return ix;
}

/*
* update status line
*/
void
upd_statusline (void)
{
	static char obuff_left[CMDLINESIZE+1];
	static char obuff_right[CMDLINESIZE+1];
	static char obuff_rest[20];
	static int last_ri = -1;
	unsigned ix=0;
	int lenleft=0, lenright=0;

	obuff_left[0] = '\0';
	obuff_right[0] = '\0';
	obuff_rest[0] = '\0';

	strncpy(obuff_left, " eda ", 6);
	lenleft = strlen(obuff_left);

	ix += add_to_obuff(cnf.disp_opts+ix, obuff_left, &lenleft);

	/* skip separator */
	while (cnf.disp_opts[ix] != '\0') {
		if (cnf.disp_opts[ix] == ' ') {
			ix++;
			break;
		} else {
			ix++;
		}
	}

	ix += add_to_obuff(cnf.disp_opts+ix, obuff_right, &lenright);

	/* file attributes */
	if (CURR_FILE.fflag & FSTAT_SPECW) {
		if (CURR_FILE.fflag & FSTAT_CHMASK)
			strncat (obuff_rest, " r/o", 4);
		if (CURR_FILE.fflag & FSTAT_INTERACT)
			strncat (obuff_rest, " int", 4);
	} else {
		if (CURR_FILE.fflag & FSTAT_SCRATCH)
			strncat (obuff_rest, " S", 2);
		if (CURR_FILE.fflag & FSTAT_RO)
			strncat (obuff_rest, " R/O", 4);
		if (CURR_FILE.fflag & FSTAT_CHANGE)
			strncat (obuff_rest, " *", 2);
		if (CURR_FILE.fflag & FSTAT_EXTCH)
			strncat (obuff_rest, " !!", 3);
		if (CURR_FILE.fflag & FSTAT_HIDDEN)
			strncat (obuff_rest, " H", 2);
	}

	mvwprintw (cnf.wstatus, 0, 0, "%s ", obuff_left);
	if (cnf.gstat & GSTAT_AUTOTITLE) {
		if (last_ri != cnf.ring_curr) {
			last_ri = cnf.ring_curr;
			xterm_title("");
		}
	}
	if ((cnf.gstat & GSTAT_AUTOTITLE) && (CURR_FILE.basename[0])) {
		if (CURR_FILE.fname[0] != '/') {
			/* relative path is important extra information */
			mvwprintw (cnf.wstatus, 0, lenleft, " %s ",
				center_fname ((CURR_FILE.fname), obuff_rest, (cnf.maxx-lenleft-lenright-2)));
		} else {
			/* absolute path is already shown in terminal title */
			mvwprintw (cnf.wstatus, 0, lenleft, " %s ",
				center_fname ((CURR_FILE.basename), obuff_rest, (cnf.maxx-lenleft-lenright-2)));
		}
	} else {
		/* no autotitle or bsasename is NULL */
		mvwprintw (cnf.wstatus, 0, lenleft, " %s ",
			center_fname ((CURR_FILE.fname), obuff_rest, (cnf.maxx-lenleft-lenright-2)));
	}
	mvwprintw (cnf.wstatus, 0, cnf.maxx-lenright, "%s", obuff_right);

	/* refresh */
	wnoutrefresh (cnf.wstatus);

	return;
} /* upd_statusline() */

/*
* update command line
*/
void
upd_cmdline (void)
{
	static char obuff[CMDLINESIZE+1];
	int i, j;

	for (i=cnf.cloff, j=0; j < CMDLINESIZE && cnf.cmdline_buff[i] != '\0' && i<cnf.cmdline_len && j<cnf.maxx; i++, j++) {
		if (cnf.cmdline_buff[i] < ' ') {
			/* hide TAB character */
			obuff[j] = 0x20;
		} else {
			obuff[j] = cnf.cmdline_buff[i];
		}
	}

	for (; j < CMDLINESIZE-1 && j <= cnf.maxx-1; )
		obuff[j++] = 0x20;
	obuff[j] = '\0';

	/* let's see */
	mvwaddnstr (cnf.wbase, 0, 0, obuff, cnf.maxx);

	/* refresh */
	wnoutrefresh (cnf.wbase);

	return;
} /* upd_cmdline() */

static attr_t
set_attr (color_attribute_type base, int lflag, int focus_flag)
{
	color_attribute_type idx = COLOR_NORMAL_TEXT;

	if (base == COLOR_INIT) {
		if (lflag & LSTAT_TAG1) {
			idx = COLOR_TAGGED_TEXT;
		} else {
			idx = COLOR_NORMAL_TEXT;
		}
	} else {
		/* search and highlight has difficult conditions, must be set directly
		*/
		idx = base;
	}

	if (focus_flag) {
		idx += COLOR_FOCUS_FLAG;
	}

	if (lflag & LSTAT_SELECT) {
		idx += COLOR_SELECT_FLAG;
	}

	return (at[idx]);
}

/*
* update text area, around the focus line
*/
void
upd_text_area (int focus_line_only)
{
	LINE *lp;
	int cnt=0;
	int focus=0, lineno=0;
	static int old_focus=0;

	/* update the focus line */
	lp = CURR_LINE;
	lineno = CURR_FILE.lineno;
	/* if (CURR_FILE.focus > TEXTROWS-1) CURR_FILE.focus = TEXTROWS-1; */
	focus = CURR_FILE.focus;
	text_line (lp, lineno, focus, 1);

	if (focus_line_only)
	{
		if (old_focus < CURR_FILE.focus) {
			lp = CURR_LINE;
			lineno = CURR_FILE.lineno;
			focus = CURR_FILE.focus;
			while (focus > 0) {
				prev_lp (cnf.ring_curr, &lp, &cnt);
				lineno -= cnt;
				if ((cnf.gstat & GSTAT_SHADOW) && (cnt > 1)) {
					focus--;
					if (focus==0)
						break;
				}
				if (lp->lflag & LSTAT_TOP)
					break;
				focus--;
				if (focus == old_focus) {
					text_line (lp, lineno, focus, 0);
					break;
				}
			}
			if ((lp->lflag & LSTAT_TOP) && (focus > 0)) {
				focus--;
				empty_line (focus);
			}
		} else if (old_focus > CURR_FILE.focus) {
			lp = CURR_LINE;
			lineno = CURR_FILE.lineno;
			focus = CURR_FILE.focus;
			while (focus < TEXTROWS-1) {
				next_lp (cnf.ring_curr, &lp, &cnt);
				lineno += cnt;
				if ((cnf.gstat & GSTAT_SHADOW) && (cnt > 1)) {
					focus++;
					if (focus==TEXTROWS-1)
						break;
				}
				if (lp->lflag & LSTAT_BOTTOM)
					break;
				focus++;
				if (focus == old_focus) {
					text_line (lp, lineno, focus, 0);
					break;
				}
			}
			if ((lp->lflag & LSTAT_BOTTOM) && (focus < TEXTROWS-1)) {
				focus++;
				empty_line (focus);
			}
		}
	}
	old_focus = CURR_FILE.focus;

	if (!focus_line_only)
	{
		/* update the lines above focus, upto top
		*/
		lp = CURR_LINE;
		lineno = CURR_FILE.lineno;
		focus = CURR_FILE.focus;
		while (focus > 0) {
			prev_lp (cnf.ring_curr, &lp, &cnt);
			lineno -= cnt;
			if ((cnf.gstat & GSTAT_SHADOW) && (cnt > 1)) {
				focus--;
				shadow_line (cnt-1, focus);
				if (focus==0)
					break;
			}
			/* hide TOP mark */
			if (lp->lflag & LSTAT_TOP) {
				break;
			}
			focus--;
			text_line (lp, lineno, focus, 0);
		}
		while (focus > 0) {	/* empty lines up */
			focus--;
			empty_line (focus);
		}

		/* update the lines below focus, upto bottom
		*/
		lp = CURR_LINE;
		lineno = CURR_FILE.lineno;
		focus = CURR_FILE.focus;
		while (focus < TEXTROWS-1) {
			next_lp (cnf.ring_curr, &lp, &cnt);
			lineno += cnt;
			if ((cnf.gstat & GSTAT_SHADOW) && (cnt > 1)) {
				focus++;
				shadow_line (cnt-1, focus);
				if (focus==TEXTROWS-1)
					break;
			}
			/* hide EOF mark */
			if (lp->lflag & LSTAT_BOTTOM) {
				break;
			}
			focus++;
			text_line (lp, lineno, focus, 0);
		}
		while (focus < TEXTROWS-1) {	/* empty lines down */
			focus++;
			empty_line (focus);
		}
	}

	/* refresh */
	wnoutrefresh (cnf.wtext);

	return;
} /* upd_text_area() */

/* tool for text_line
*/
static void
map_match_to_out (const char *ibuff, int imatch_so, int imatch_eo,
	int *pos, int padsize, int focus, const char *obuff)
{
	int i=0;
	int so=0, eo=0, begin=0, end=0, length=0;

	/* map input so and eo to the output so and eo */
	so = *pos;
	for (i=0; i < imatch_eo; i++)
	{
		if (i == imatch_so)
			so = *pos;
		if (ibuff[i] == '\t')
			*pos += cnf.tabsize - (*pos % cnf.tabsize);
		else
			(*pos)++;
	}
	eo = *pos;

	/* check and print out */
	begin = so - CURR_FILE.lnoff;
	end = eo - CURR_FILE.lnoff;
	if (end > 0 && begin < padsize) {
		length = end-begin;
		if (begin < 0) {
			length += begin;
			begin = 0;
		}
		if (end >= padsize) {
			length -= end-padsize;
		}
		mvwaddnstr (cnf.wtext, focus, cnf.pref+begin, &obuff[begin], length);
	}

	return;
}

/*
* text line output
*/
static void
text_line (LINE *lp, int lineno, int focus, int focus_flag)
{
	const char *ibuff = lp->buff;
	int llen = lp->llen;
	int flag = lp->lflag;
	attr_t attr;

	static regmatch_t pmatch;
	static char obuff[CMDLINESIZE+1];
	int i;			/* real, valid index of ibuff */
	int pos;		/* virtual index of rendered output */
	int j;			/* valid index in obuff */
	int padsize;		/* depends on .pref and .lnoff */
	char ch=0;		/* character in the line, rendering */
	int cnt=0;		/* tabstop counter, rendering */
	int bm_char = ' ';	/* bookmark indicator in prefix */
	int alt_char = ' ';	/* alter/change indicator in prefix */
	char disp_rflag = 0;

	attr = set_attr (COLOR_INIT, flag, focus_flag);
	wattron (cnf.wtext, attr);

	/* prefix --> obuff[] */
	if (cnf.gstat & GSTAT_PREFIX)
	{
		if (cnf.pref < 3 || lineno < 0 ) {
			for (i=0; i<cnf.pref; i++) {
				obuff[i] = ' ';
			}
		} else {
			if (flag & LSTAT_BM_BITS) {
				bm_char =  ((flag & LSTAT_BM_BITS) >> BM_BIT_SHIFT) + '0';
			}
			if (flag & LSTAT_CHANGE) {
				alt_char = '*';
			} else if (flag & LSTAT_ALTER) {
				alt_char = '.';
			}
			obuff[0] = (char)bm_char;
			obuff[1] = (char)alt_char;
			/* show only the (PREFIXSIZE-3) lower decimal characters --> 00001...99999 */
			for (i=cnf.pref-2; i > 1; i--) {
				obuff[i] = lineno%10 + '0';
				lineno /= 10;
			}
			obuff[cnf.pref-1] = (flag & LSTAT_TRUNC) ? 'x' : ' ';
			obuff[cnf.pref] = '\0';
		}
		mvwaddnstr (cnf.wtext, focus, 0, obuff, cnf.pref);
	}

	/* wclrtoeol? -- does not work well, selection lines are shown upto lineend only */
	padsize = MIN(CMDLINESIZE-1, cnf.maxx-cnf.pref);
	memset(obuff, ' ', padsize);
	obuff[padsize] = '\0';

	/* text: i:ibuff[] --> j:obuff[]
	 * pos is the virtual output index, for .tabsize and .lnoff
	*/
	i = j = pos = 0;
	while (i<llen && ibuff[i] != '\0')
	{
		/* get ch from in-buffer */
		if (ibuff[i] == '\t') {
			/* insert spaces upto next tab stop */
			if (cnt == 0)
				cnt = cnf.tabsize - (pos % cnf.tabsize);
			if (cnt-- > 0)
				ch = ' ';
			if (cnt == 0)
				i++;
		} else if (ibuff[i] == '\r' || ibuff[i] == '\n') {
			break;
		/* hide this if we have wide char support... */
		} else if (ibuff[i] < 0 || ibuff[i] >= 0x7f) {
			ch = '?';
			i++;
		} else {
			ch = ibuff[i++];
		}

		/* put ch to out-buffer */
		if (j >= padsize)
			break;
		else if (pos++ >= CURR_FILE.lnoff)
			obuff[j++] = ch;
	}

	/* show the line */
	mvwaddnstr (cnf.wtext, focus, cnf.pref, obuff, padsize);
	wattroff (cnf.wtext, attr);

	/*
	 * highlight
	 */
	if (CURR_FILE.fflag & FSTAT_TAG5)
	{
		attr = set_attr (COLOR_HIGH_TEXT, flag, focus_flag);
		wattron (cnf.wtext, attr);
		i = pos = disp_rflag = 0;
		while ((pos - CURR_FILE.lnoff < padsize) &&
			!regexec(&(CURR_FILE.highlight_reg), &ibuff[i], 1, &pmatch, disp_rflag))
		{
			if (pmatch.rm_so < pmatch.rm_eo) {
				map_match_to_out(&ibuff[i], pmatch.rm_so, pmatch.rm_eo, 
					&pos, padsize, focus, obuff);
				i += pmatch.rm_eo;
			} else {
				i++;
				pos++;
			}
			disp_rflag = (CURR_FILE.fflag & FSTAT_TAG6) ? REG_NOTBOL : 0;
		}
		wattroff (cnf.wtext, attr);
	}

	/*
	 * search (or change)
	 * change: only focus and below
	 */
	if (((CURR_FILE.fflag & (FSTAT_TAG2 | FSTAT_TAG3)) == FSTAT_TAG2) ||
	    ((CURR_FILE.fflag & FSTAT_TAG3) && (focus >= CURR_FILE.focus)))
	{
		attr = set_attr (COLOR_SEARCH_TEXT, flag, focus_flag);
		wattron (cnf.wtext, attr);
		i = pos = disp_rflag = 0;
		while ((pos - CURR_FILE.lnoff < padsize) &&
			!regexec(&(CURR_FILE.search_reg), &ibuff[i], 1, &pmatch, disp_rflag))
		{
			if (pmatch.rm_so < pmatch.rm_eo) {
				map_match_to_out(&ibuff[i], pmatch.rm_so, pmatch.rm_eo, 
					&pos, padsize, focus, obuff);
				i += pmatch.rm_eo;
			} else {
				i++;
				pos++;
			}
			disp_rflag = (CURR_FILE.fflag & FSTAT_TAG4) ? REG_NOTBOL : 0;
		}
		wattroff (cnf.wtext, attr);
	}

	return;
} /* text_line() */

/*
* get current cursor position (absolute, independent from offset)
* lncol may be over the line's end
*/
int
get_pos(LINE *lp, int lncol)
{
	int i=0;	/* char index (in line) */
	int pos=0;	/* position in screen row (abs) */

	for (i=0; i<lncol; i++) {
		if (i > lp->llen-1)
			pos++;
		else if (lp->buff[i] == '\t')
			pos += (cnf.tabsize - (pos % cnf.tabsize));
		else
			pos++;
	} /* for */

	return (pos);
} /* get_pos() */

/*
* get current column in the line from absolute position
* return may be over the line's end
*/
int
get_col(LINE *lp, int curpos)
{
	int i=0;	/* char index (in line) */
	int pos=0;	/* position counter */

	for (i=0; pos<=curpos; i++) {
		if (i > lp->llen-1)
			pos++;
		else if (lp->buff[i] == '\t')
			pos += (cnf.tabsize - (pos % cnf.tabsize));
		else
			pos++;
	} /* for */

	return (i-1);
} /* get_col() */

/*
* update_curpos - update curpos and lnoff in current file/line
* ri: file ring index
* this function is part of the uniform output rendering
*/
void
update_curpos (int ri)
{
	int save_ri = cnf.ring_curr;
	cnf.ring_curr = ri;

	CURR_FILE.curpos = get_pos(CURR_LINE, CURR_FILE.lncol);
	/* line offset */
	if (CURR_FILE.curpos < TEXTCOLS)
		CURR_FILE.lnoff = 0;
	else if (CURR_FILE.curpos - CURR_FILE.lnoff > TEXTCOLS - 1)
		CURR_FILE.lnoff = CURR_FILE.curpos - TEXTCOLS + 1;
	else if (CURR_FILE.lnoff > CURR_FILE.curpos)
		CURR_FILE.lnoff = CURR_FILE.curpos;

	cnf.ring_curr = save_ri;
	return;
}

/*
* set_position_by_pointer - move the cursor to new position by pointer
*/
int
set_position_by_pointer (MEVENT pointer)
{
	int focus = CURR_FILE.focus;
	LINE *lp = CURR_LINE;
	int fcnt = 0;

	if ((pointer.y < 1) || (pointer.y > cnf.maxy-1) ||
		(pointer.x < cnf.pref) || (pointer.x > cnf.maxx-1))
	{
		return 1;
	}
	pointer.y -= 1;
	pointer.x -= cnf.pref;

	if (focus > pointer.y) {
		while (focus > pointer.y) {
			prev_lp (cnf.ring_curr, &lp, &fcnt);
			if (UNLIKE_TOP(lp)) {
				if ((cnf.gstat & GSTAT_SHADOW) && (fcnt > 1)) {
					if (focus > pointer.y+1) {
						CURR_LINE = lp;
						CURR_FILE.lineno -= fcnt;
						focus -= 2;
					} else {
						break;
					}
				} else {
					CURR_LINE = lp;
					CURR_FILE.lineno -= fcnt;
					focus -= 1;
				}
			} else {
				break;
			}
		}

	} else if (focus < pointer.y) {
		while (focus < pointer.y) {
			next_lp (cnf.ring_curr, &lp, &fcnt);
			if (UNLIKE_BOTTOM(lp)) {
				if ((cnf.gstat & GSTAT_SHADOW) && (fcnt > 1)) {
					if (focus < pointer.y-1) {
						CURR_LINE = lp;
						CURR_FILE.lineno += fcnt;
						focus += 2;
					} else {
						break;
					}
				} else {
					CURR_LINE = lp;
					CURR_FILE.lineno += fcnt;
					focus += 1;
				}
			} else {
				break;
			}
		}

	}
	CURR_FILE.focus = focus;

	CURR_FILE.curpos = pointer.x + CURR_FILE.lnoff;
	CURR_FILE.lncol = get_col(CURR_LINE, CURR_FILE.curpos);

	return 0;
}

/*
* first_screen_row - returns first screen-row number
*/
int
first_screen_row (void)
{
	LINE *lp = CURR_LINE;
	int lineno = CURR_FILE.lineno;
	int focus = CURR_FILE.focus;
	int fcnt = 0;	/* file lines */

	/* upto the first line on the screen
	 */
	while ((focus > 0) && UNLIKE_TOP(lp)) {
		prev_lp (cnf.ring_curr, &lp, &fcnt);
		lineno -= fcnt;
		/* see PRESET_FOCUS_MOVE_DECR */
		if (focus > 0) {
			if ((cnf.gstat & GSTAT_SHADOW) && (fcnt > 1) && (focus > 1))
				focus -= 2;
			else
				focus -= 1;
		}
	}

	return (focus);
}

/*
* last_screen_row - returns last screen-row number
*/
int
last_screen_row (void)
{
	LINE *lp = CURR_LINE;
	int lineno = CURR_FILE.lineno;
	int focus = CURR_FILE.focus;
	int fcnt = 0;	/* file lines */

	/* upto the last line on the screen
	 */
	while ((focus < TEXTROWS-1) && UNLIKE_BOTTOM(lp)) {
		next_lp (cnf.ring_curr, &lp, &fcnt);
		lineno += fcnt;
		/* see PRESET_FOCUS_MOVE_INCR */
		if (focus < TEXTROWS-1) {
			if ((cnf.gstat & GSTAT_SHADOW) && (fcnt > 1) && (focus < TEXTROWS-2))
				focus += 2;
			else
				focus += 1;
		}
	}

	return (focus);
}

/*
* update_focus - update vertical focus position of indexed file
* value: file ring index -- or something else
* data_ptr: the focus preset
* this function is part of the uniform output rendering
*/
void
update_focus (MOTION_TYPE motion_type, int ri, int value, int *data_ptr)
{
	int first, last;

	switch(motion_type)
	{
	case FOCUS_ON_FIRST_LINE:
		/* move focus to the first line of the file
		*/
		cnf.fdata[ri].focus = 1;
		break;

	case FOCUS_ON_LAST_LINE:
		/* move focus to the last line of the file
		*/
		cnf.fdata[ri].focus = MIN(cnf.fdata[ri].num_lines, TEXTROWS-2);
		break;

	case INCR_FOCUS_ONCE:
		/* direct skip, forward
		*/
		if (cnf.fdata[ri].focus < TEXTROWS-1)
			cnf.fdata[ri].focus++;
		break;

	case DECR_FOCUS_ONCE:
		/* direct skip, backward
		*/
		if (cnf.fdata[ri].focus > 0)
			cnf.fdata[ri].focus--;
		break;

	case CENTER_FOCUSLINE:
		if (TEXTROWS > 0)
			cnf.fdata[ri].focus = TEXTROWS/2;	/* screen's middle line */
		else
			cnf.fdata[ri].focus = TRACESIZE;	/* sane, if we have not yet TEXTROWS */
		break;

	case FOCUS_SET_INIT:
	case FOCUS_AFTER_PRESET:
		cnf.fdata[ri].focus = value;		/* new focus */
		break;

	case PRESET_FOCUS_MOVE_INCR:
		/* move focus forward with two or one, if possible
		* value: skipped lines count
		* data_ptr: the focus preset
		*/
		if (*data_ptr < TEXTROWS-1) {
			if ((cnf.gstat & GSTAT_SHADOW) && (value > 1) && (*data_ptr < TEXTROWS-2))
				*data_ptr += 2;
			else
				*data_ptr += 1;
		}
		break;

	case PRESET_FOCUS_MOVE_DECR:
		/* move focus backward with two or one, if possible
		* value: skipped lines count
		* data_ptr: the focus preset
		*/
		if (*data_ptr > 0) {
			if ((cnf.gstat & GSTAT_SHADOW) && (value > 1) && (*data_ptr > 1))
				*data_ptr -= 2;
			else
				*data_ptr -= 1;
		}
		break;

	case CENTER_FOCUS_ON_BIG_LEAP:
		/* center if count of skipped lines is big
		* value: the preset focus
		*/
		if (value > TEXTROWS/3)
			cnf.fdata[ri].focus = TEXTROWS/2;
		break;

	case KEEP_FOCUS_UPPER_3RD:
		/* keep focus in the middle 3rd, away from top
		*/
		if (cnf.fdata[ri].focus < TEXTROWS/3)
			cnf.fdata[ri].focus = TEXTROWS/3;
		else if (cnf.fdata[ri].focus > TEXTROWS/3)
			cnf.fdata[ri].focus--;
		break;

	case KEEP_FOCUS_LOWER_3RD:
		/* keep focus in the middle 3rd, away from bottom
		*/
		if (cnf.fdata[ri].focus > TEXTROWS*2/3)
			cnf.fdata[ri].focus = TEXTROWS*2/3;
		else if (cnf.fdata[ri].focus < TEXTROWS*2/3)
			cnf.fdata[ri].focus++;
		break;

	case KEEP_FOCUS_CENTERED:
		/* in upper half: move focus down,
		*  in lower half: scroll window up
		*/
		if (cnf.fdata[ri].focus < TEXTROWS/2)
			cnf.fdata[ri].focus++;
		break;

	case FOCUS_AVOID_BORDER:
		/* avoid the borders, pull back focus to the limit (1/5 instead of 1/8)
		* value: the direction of previous motion
		* data_ptr: the focus preset
		*/
		if (value < 0 && *data_ptr < TEXTROWS/5) {
			*data_ptr = TEXTROWS/5;
		} else if (value > 0 && *data_ptr > TEXTROWS*4/5) {
			*data_ptr = TEXTROWS*4/5;
		}
		break;

	case FOCUS_OPT_PULL_MIDDLE:
		/* if new focus differs : set and pull to middle
		* value: the focus preset
		*/
		if (cnf.fdata[ri].focus == value) {
			break;			/* leave */
		}
		cnf.fdata[ri].focus = value;		/* set it */
		/* overflow */
	case FOCUS_PULL_MIDDLE:
		first = first_screen_row();
		last = last_screen_row();
		if (last - first < TEXTROWS/2) {
			/* almost empty, centralise */
			if (cnf.fdata[ri].focus < TEXTROWS/3) {
				cnf.fdata[ri].focus = TEXTROWS/3;
			} else if (cnf.fdata[ri].focus > TEXTROWS*2/3) {
				cnf.fdata[ri].focus = TEXTROWS*2/3;
			}
		} else {
			/* more lines, avoid the borders (1/5 instead of 1/8) */
			if (cnf.fdata[ri].focus < TEXTROWS/5) {
				cnf.fdata[ri].focus = TEXTROWS/5;
			} else if (cnf.fdata[ri].focus > TEXTROWS*4/5) {
				cnf.fdata[ri].focus = TEXTROWS*4/5;
			}
		}
		break;

	case NOTHING_TODO:
		break;

	default:
		break;
	}
}

/*
* update trace message window
*/
void
upd_trace (void)
{
	int f, i;
	attr_t attr = at[COLOR_TRACEMSG_TEXT];

	/* trace message lines (only once) */
	if (cnf.trace > 0) {
		wattron (cnf.wtext, attr);
		for (f=0; f<cnf.trace && f<TEXTROWS-1; f++) {
			wmove (cnf.wtext, f, 0);
			wclrtoeol(cnf.wtext);
			/**/
			for (i=0; i<cnf.maxx ; i++) {
				if (cnf.tracerow[f][i] == '\0' || cnf.tracerow[f][i] == '\n')
					break;
			}
			if (i < cnf.maxx)
				cnf.tracerow[f][i++] = ' ';
			cnf.tracerow[f][i] = '\0';
			mvwprintw (cnf.wtext, f, 0, "%s", cnf.tracerow[f]);
			/**/
			cnf.tracerow[f][0] = '\0';
		}
		wattroff (cnf.wtext, attr);
		wnoutrefresh (cnf.wtext);
	}
	cnf.trace=0;	/* this output finished */

	return;
} /* upd_trace() */

/*
* space padded or cutted line output (return str pointer)
* center text
*/
static const char *
center_fname (const char *ibuff, const char *catbuff, int padsize)
{
	static char obuff[CMDLINESIZE+1];
	int i, j, k, ilen=0, catlen=0;

	for (i=0, ilen=0, j=0; ibuff[i] != '\0'; i++) {
		if (ibuff[i] == '/')
			j = i;
		ilen++;
	}

	catlen = strlen(catbuff);
	k = ilen+catlen;
	padsize = MIN(padsize, CMDLINESIZE);	/* obuff[] */
	if (k > padsize) {
		/* try to get more space */
		if (j > 0 && j+1 < ilen) {
			/* truncate ibuff */
			ibuff += j+1;
			ilen -= j+1;
		}
		k = ilen+catlen;
		padsize = MIN(padsize, CMDLINESIZE);	/* obuff[] */
	}

	if (k <= padsize) {
		/* center line */
		for (j=0; j < (padsize-k)/2; j++)
			obuff[j] = ' ';
		for (i=0; i<ilen; i++, j++)
			obuff[j] = ibuff[i];
		for (i=0; i<catlen; i++, j++)
			obuff[j] = catbuff[i];
		for (; j<padsize; j++)
			obuff[j] = ' ';
		obuff[j] = '\0';

	} else { /* ilen+catlen > padsize */
		/* center line: cut prefix */
		j=0;
		obuff[j++] = '<';
		for (i=k-padsize+1; i<ilen; i++)
			obuff[j++] = ibuff[i];
		for (i=0; i<catlen; i++)
				obuff[j++] = catbuff[i];
		obuff[j] = '\0';
	}

	return (obuff);
}

/*
* shadow line
*/
static void
shadow_line (int count, int focus)
{
	static char mid_buff[100];
	attr_t attr = at[COLOR_SHADOW_TEXT];

	if (count > 1) {
		snprintf(mid_buff, 30, "--- %d lines ---", count);
	} else {
		snprintf(mid_buff, 30, "--- 1 line ---");
	}
	wmove (cnf.wtext, focus, 0);
	wclrtoeol (cnf.wtext);
	wattron (cnf.wtext, attr);
	mvwaddnstr (cnf.wtext, focus, cnf.pref, mid_buff, cnf.maxx);
	wattroff (cnf.wtext, attr);

	return;
}

/*
* empty line
*/
static void
empty_line (int focus)
{
	wmove (cnf.wtext, focus, 0);
	wclrtoeol (cnf.wtext);
	mvwaddnstr (cnf.wtext, focus, 0, TILDE, cnf.maxx);
	return;
}

void
init_colors (void)
{
	int i;

	if (cnf.palette == 1) {		/* light background palette */

		i = COLOR_NORMAL_TEXT;
		init_pair (i,	COLOR_BLACK,	COLOR_WHITE);
		at[i] = COLOR_PAIR(i);
		i = COLOR_TAGGED_TEXT;
		init_pair (i,	COLOR_BLUE,	COLOR_WHITE);
		at[i] = COLOR_PAIR(i);
		i = COLOR_HIGH_TEXT;
		init_pair (i,	COLOR_YELLOW,	COLOR_BLACK);
		at[i] = COLOR_PAIR(i) | A_BOLD | A_REVERSE;
		i = COLOR_SEARCH_TEXT;
		init_pair (i,	COLOR_RED,	COLOR_WHITE);
		at[i] = COLOR_PAIR(i) | A_BOLD | A_REVERSE;

		i = COLOR_FOCUS_FLAG + COLOR_NORMAL_TEXT;
		init_pair (i,	COLOR_WHITE,	COLOR_CYAN);
		at[i] = COLOR_PAIR(i);
		i = COLOR_FOCUS_FLAG + COLOR_TAGGED_TEXT;
		init_pair (i,	COLOR_BLUE,	COLOR_CYAN);
		at[i] = COLOR_PAIR(i);
		i = COLOR_FOCUS_FLAG + COLOR_HIGH_TEXT;
		init_pair (i,	COLOR_YELLOW,	COLOR_BLACK);
		at[i] = COLOR_PAIR(i) | A_BOLD | A_REVERSE;
		i = COLOR_FOCUS_FLAG + COLOR_SEARCH_TEXT;
		init_pair (i,	COLOR_RED,	COLOR_WHITE);
		at[i] = COLOR_PAIR(i) | A_BOLD | A_REVERSE;

		i = COLOR_SELECT_FLAG + COLOR_NORMAL_TEXT;
		init_pair (i,	COLOR_BLACK,	COLOR_GREEN);
		at[i] = COLOR_PAIR(i);
		i = COLOR_SELECT_FLAG + COLOR_TAGGED_TEXT;
		init_pair (i,	COLOR_BLUE,	COLOR_GREEN);
		at[i] = COLOR_PAIR(i);
		i = COLOR_SELECT_FLAG + COLOR_HIGH_TEXT;
		init_pair (i,	COLOR_YELLOW,	COLOR_BLACK);
		at[i] = COLOR_PAIR(i) | A_BOLD | A_REVERSE;
		i = COLOR_SELECT_FLAG + COLOR_SEARCH_TEXT;
		init_pair (i,	COLOR_RED,	COLOR_WHITE);
		at[i] = COLOR_PAIR(i) | A_BOLD | A_REVERSE;

		i = COLOR_SELECT_FLAG + COLOR_FOCUS_FLAG + COLOR_NORMAL_TEXT;
		init_pair (i,	COLOR_BLACK,	COLOR_CYAN);
		at[i] = COLOR_PAIR(i);
		i = COLOR_SELECT_FLAG + COLOR_FOCUS_FLAG + COLOR_TAGGED_TEXT;
		init_pair (i,	COLOR_BLUE,	COLOR_CYAN);
		at[i] = COLOR_PAIR(i);
		i = COLOR_SELECT_FLAG + COLOR_FOCUS_FLAG + COLOR_HIGH_TEXT;
		init_pair (i,	COLOR_YELLOW,	COLOR_BLACK);
		at[i] = COLOR_PAIR(i) | A_BOLD | A_REVERSE;
		i = COLOR_SELECT_FLAG + COLOR_FOCUS_FLAG + COLOR_SEARCH_TEXT;
		init_pair (i,	COLOR_RED,	COLOR_WHITE);
		at[i] = COLOR_PAIR(i) | A_BOLD | A_REVERSE;

		i = COLOR_TRACEMSG_TEXT;
		init_pair (i,	COLOR_BLACK,	COLOR_CYAN);
		at[i] = COLOR_PAIR(i);
		i = COLOR_STATUSLINE_TEXT;
		init_pair (i,	COLOR_YELLOW,	COLOR_BLUE);
		at[i] = COLOR_PAIR(i) | A_BOLD;
		i = COLOR_CMDLINE_TEXT;
		init_pair (i,	COLOR_YELLOW,	COLOR_BLUE);
		at[i] = COLOR_PAIR(i) | A_BOLD;
		i = COLOR_SHADOW_TEXT;
		init_pair (i,	COLOR_CYAN,	COLOR_WHITE);
		at[i] = COLOR_PAIR(i);

	} else {			/* dark background palette */

		i = COLOR_NORMAL_TEXT;
		init_pair (i,	COLOR_GREEN,	COLOR_BLACK);
		at[i] = COLOR_PAIR(i);
		i = COLOR_TAGGED_TEXT;
		init_pair (i,	COLOR_BLUE,	COLOR_BLACK);
		at[i] = COLOR_PAIR(i);
		i = COLOR_HIGH_TEXT;
		init_pair (i,	COLOR_CYAN,	COLOR_BLACK);
		at[i] = COLOR_PAIR(i);
		i = COLOR_SEARCH_TEXT;
		init_pair (i,	COLOR_RED,	COLOR_BLACK);
		at[i] = COLOR_PAIR(i);

		i = COLOR_FOCUS_FLAG + COLOR_NORMAL_TEXT;
		init_pair (i,	COLOR_YELLOW,	COLOR_BLACK);
		at[i] = COLOR_PAIR(i) | A_BOLD;
		i = COLOR_FOCUS_FLAG + COLOR_TAGGED_TEXT;
		init_pair (i,	COLOR_BLUE,	COLOR_BLACK);
		at[i] = COLOR_PAIR(i) | A_BOLD;
		i = COLOR_FOCUS_FLAG + COLOR_HIGH_TEXT;
		init_pair (i,	COLOR_CYAN,	COLOR_BLACK);
		at[i] = COLOR_PAIR(i) | A_BOLD;
		i = COLOR_FOCUS_FLAG + COLOR_SEARCH_TEXT;
		init_pair (i,	COLOR_RED,	COLOR_BLACK);
		at[i] = COLOR_PAIR(i) | A_BOLD;

		i = COLOR_SELECT_FLAG + COLOR_NORMAL_TEXT;
		init_pair (i,	COLOR_GREEN,	COLOR_BLACK);
		at[i] = COLOR_PAIR(i) | A_REVERSE;
		i = COLOR_SELECT_FLAG + COLOR_TAGGED_TEXT;
		init_pair (i,	COLOR_BLUE,	COLOR_BLACK);
		at[i] = COLOR_PAIR(i) | A_REVERSE;
		i = COLOR_SELECT_FLAG + COLOR_HIGH_TEXT;
		init_pair (i,	COLOR_CYAN,	COLOR_BLACK);
		at[i] = COLOR_PAIR(i) | A_REVERSE;
		i = COLOR_SELECT_FLAG + COLOR_SEARCH_TEXT;
		init_pair (i,	COLOR_RED,	COLOR_BLACK);
		at[i] = COLOR_PAIR(i) | A_REVERSE;

		i = COLOR_SELECT_FLAG + COLOR_FOCUS_FLAG + COLOR_NORMAL_TEXT;
		init_pair (i,	COLOR_YELLOW,	COLOR_BLACK);
		at[i] = COLOR_PAIR(i) | A_BOLD | A_REVERSE;
		i = COLOR_SELECT_FLAG + COLOR_FOCUS_FLAG + COLOR_TAGGED_TEXT;
		init_pair (i,	COLOR_BLUE,	COLOR_BLACK);
		at[i] = COLOR_PAIR(i) | A_BOLD | A_REVERSE;
		i = COLOR_SELECT_FLAG + COLOR_FOCUS_FLAG + COLOR_HIGH_TEXT;
		init_pair (i,	COLOR_CYAN,	COLOR_BLACK);
		at[i] = COLOR_PAIR(i) | A_BOLD | A_REVERSE;
		i = COLOR_SELECT_FLAG + COLOR_FOCUS_FLAG + COLOR_SEARCH_TEXT;
		init_pair (i,	COLOR_RED,	COLOR_BLACK);
		at[i] = COLOR_PAIR(i) | A_BOLD | A_REVERSE;

		i = COLOR_TRACEMSG_TEXT;
		init_pair (i,	COLOR_BLUE,	COLOR_CYAN);
		at[i] = COLOR_PAIR(i);
		i = COLOR_STATUSLINE_TEXT;
		init_pair (i,	COLOR_YELLOW,	COLOR_BLUE);
		at[i] = COLOR_PAIR(i) | A_BOLD;
		i = COLOR_CMDLINE_TEXT;
		init_pair (i,	COLOR_YELLOW,	COLOR_BLUE);
		at[i] = COLOR_PAIR(i) | A_BOLD;
		i = COLOR_SHADOW_TEXT;
		init_pair (i,	COLOR_GREEN,	COLOR_BLACK);
		at[i] = COLOR_PAIR(i);

	}

	//wattrset (cnf.wstatus,	at[COLOR_STATUSLINE_TEXT]);
	//wattrset (cnf.wbase,	at[COLOR_CMDLINE_TEXT]);
	//wattrset (cnf.wtext,	at[COLOR_NORMAL_TEXT]);

	wbkgd (cnf.wstatus,	' ' | at[COLOR_STATUSLINE_TEXT]);
	wbkgd (cnf.wbase,	' ' | at[COLOR_CMDLINE_TEXT]);
	wbkgd (cnf.wtext,	' ' | at[COLOR_NORMAL_TEXT]);
}
