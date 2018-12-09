/*
* disp.c
* all the original character based display implementation
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
#include "main.h"
#include "proto.h"

/* global config */
extern CONFIG cnf;

/* local proto */
static void set_color_attribute (WINDOW *win, int idx);
static void map_match_to_out (const char *ibuff, int imatch_so, int imatch_eo, int *pos, int padsize, int focus, const char *obuff);
static void text_line (LINE *lp, int lineno, int focus, int focus_flag);
static const char *center_fname (const char *ibuff, const char *catbuff, int catlen, int padsize);
static void shadow_empty_line (int focus, int count);
#define empty_line(focus)	shadow_empty_line((focus), -1)

static void
set_color_attribute (WINDOW *win, int idx)
{
	int bits;
	bits = cnf.cpal.bits[ idx ];
	wattron (win, cnf.cpairs[bits & 0x3f]);
	if (bits & 0x40)
		wattron (win, A_BOLD);
	if (bits & 0x80)
		wattron (win, A_REVERSE);
}

#define WATCH_FLAGS	(FSTAT_SPECW | FSTAT_CHMASK | FSTAT_INTERACT | FSTAT_SCRATCH | FSTAT_RO | FSTAT_CHANGE | FSTAT_EXTCH | FSTAT_HIDDEN)

/* update status line
*/
void
upd_statusline (void)
{
	char obuff_left[CMDLINESIZE+1];
	char obuff_right[CMDLINESIZE+1];
	char obuff_attr[20];
	unsigned hexa=0;
	int lenleft=0, lenright=0, lenattr=0, length_avail=0;
	static int orig_ri = -1;
	static int orig_flags = 0;
	static int orig_pipout = 0;

	obuff_left[0] = '\0';
	obuff_right[0] = '\0';
	obuff_attr[0] = '\0';

	/* left wing */
	snprintf(obuff_left, sizeof(obuff_left)-1, " eda (%2d,%-2d) F:%c%c L:%-5d ",
		cnf.ring_curr+1, cnf.ring_size,
		(CURR_FILE.flevel+'0'), (LMASK(cnf.ring_curr) ? '=' : '*'),
		CURR_FILE.num_lines);
	lenleft = strlen(obuff_left);

	/* right wing */
	hexa = (CURR_FILE.lncol < CURR_LINE->llen) ? (unsigned)CURR_LINE->buff[CURR_FILE.lncol] : 0;
	if (cnf.ie > 0) {
		snprintf(obuff_right, sizeof(obuff_right)-1, " Err:%u %5d,%-3d (0x%02X) ",
			cnf.ie, CURR_FILE.lineno, CURR_FILE.curpos, hexa);
	} else {
		snprintf(obuff_right, sizeof(obuff_right)-1, " %5d,%-3d (0x%02X) ",
			CURR_FILE.lineno, CURR_FILE.curpos, hexa);
	}
	lenright = strlen(obuff_right);

	if ((cnf.gstat & GSTAT_REDRAW) || orig_ri != cnf.ring_curr
	|| orig_flags != (CURR_FILE.fflag & WATCH_FLAGS) || orig_pipout != CURR_FILE.pipe_output)
	{
		/* middle, attributes after filename */
		if (CURR_FILE.fflag & FSTAT_SPECW) {
			if (CURR_FILE.fflag & FSTAT_CHMASK) {
				obuff_attr[lenattr++] = ' ';
				obuff_attr[lenattr++] = 'r';
				obuff_attr[lenattr++] = '/';
				obuff_attr[lenattr++] = 'o';
			}
			if (CURR_FILE.fflag & FSTAT_INTERACT) {
				obuff_attr[lenattr++] = ' ';
				obuff_attr[lenattr++] = 'i';
				obuff_attr[lenattr++] = 'n';
				obuff_attr[lenattr++] = 't';
			}
			if (CURR_FILE.pipe_output != 0) {
				obuff_attr[lenattr++] = ' ';
				obuff_attr[lenattr++] = 'r';
				obuff_attr[lenattr++] = 'u';
				obuff_attr[lenattr++] = 'n';
			}
		} else {
			if (CURR_FILE.fflag & FSTAT_SCRATCH) {
				obuff_attr[lenattr++] = ' ';
				obuff_attr[lenattr++] = 'S';
			}
			if (CURR_FILE.fflag & FSTAT_RO) {
				obuff_attr[lenattr++] = ' ';
				obuff_attr[lenattr++] = 'R';
				obuff_attr[lenattr++] = '/';
				obuff_attr[lenattr++] = 'O';
			}
			if (CURR_FILE.fflag & FSTAT_CHANGE) {
				obuff_attr[lenattr++] = ' ';
				obuff_attr[lenattr++] = '*';
			}
			if (CURR_FILE.fflag & FSTAT_EXTCH) {
				obuff_attr[lenattr++] = ' ';
				obuff_attr[lenattr++] = '!';
				obuff_attr[lenattr++] = '!';
			}
			if (CURR_FILE.fflag & FSTAT_HIDDEN) {
				obuff_attr[lenattr++] = ' ';
				obuff_attr[lenattr++] = 'H';
			}
		}
		obuff_attr[lenattr] = '\0';
		length_avail = cnf.maxx-lenleft-lenright;
	}

	set_color_attribute (stdscr, COLOR_STATUSLINE_TEXT);

	mvwaddnstr (stdscr, 0, 0, obuff_left, lenleft);
	mvwaddnstr (stdscr, 0, cnf.maxx-lenright, obuff_right, lenright);

	/* middle, filename before attributes, 10 bytes minimum */
	if (length_avail-lenattr > 10) {
		mvwaddnstr (stdscr, 0, lenleft,
			center_fname ((CURR_FILE.fname), obuff_attr, lenattr, length_avail),
			length_avail);
	}

	wattrset (stdscr, A_NORMAL);
	wnoutrefresh (stdscr);

	orig_ri = cnf.ring_curr;
	orig_flags = CURR_FILE.fflag & WATCH_FLAGS;
	orig_pipout = CURR_FILE.pipe_output;
	return;
}

/* update status line -- for the typing tutor
*/
void
upd_status_typing_tutor (void)
{
	char obuff_left[CMDLINESIZE+1];
	char obuff_right[CMDLINESIZE+1];
	unsigned hexa=0;
	int lenleft=0, lenright=0, length_avail=0;
	static int orig_ri = -1;
	static int orig_flags = 0;

	obuff_left[0] = '\0';
	obuff_right[0] = '\0';

	set_color_attribute (stdscr, COLOR_STATUSLINE_TEXT);

	/* left wing */
	if ((cnf.gstat & GSTAT_REDRAW) || orig_ri != cnf.ring_curr || orig_flags != (CURR_FILE.fflag & WATCH_FLAGS)) {
		snprintf(obuff_left, sizeof(obuff_left)-1, " eda (typing tutor) ");
		lenleft = strlen(obuff_left);
		mvwaddnstr (stdscr, 0, 0, obuff_left, lenleft);
	}

	/* right wing */
	hexa = (CURR_FILE.lncol < CURR_LINE->llen) ? (unsigned)CURR_LINE->buff[CURR_FILE.lncol] : 0;
	snprintf(obuff_right, sizeof(obuff_right)-1, " pass %d, fail %d (0x%02X) ",
		cnf.tutor_pass, cnf.tutor_fail, hexa);
	lenright = strlen(obuff_right);
	mvwaddnstr (stdscr, 0, cnf.maxx-lenright, obuff_right, lenright);

	/* middle, filename before attributes, 10 bytes minimum */
	if ((cnf.gstat & GSTAT_REDRAW) || orig_ri != cnf.ring_curr || orig_flags != (CURR_FILE.fflag & WATCH_FLAGS)) {
		length_avail = cnf.maxx-lenleft-lenright;
		if (length_avail > 10) {
			mvwaddnstr (stdscr, 0, lenleft,
				center_fname ((CURR_FILE.fname), "", 0, length_avail),
				length_avail);
		}
	}

	wattrset (stdscr, A_NORMAL);
	wnoutrefresh (stdscr);

	orig_ri = cnf.ring_curr;
	orig_flags = CURR_FILE.fflag & WATCH_FLAGS;
	return;
}

/* update terminal title, depending on setting and window change
*/
void
upd_termtitle (void)
{
	char xtbuff[FNAMESIZE+10];
	char *fullpath=NULL;
	char dirname[FNAMESIZE];
	char basename[FNAMESIZE];

	if (CURR_FILE.fflag & FSTAT_SPECW) {
		if (xterm_title("eda"))
			cnf.gstat &= ~GSTAT_AUTOTITLE;
	} else {
		mybasename(basename, CURR_FILE.fpath, FNAMESIZE);
		fullpath = canonicalpath(CURR_FILE.fpath);
		mydirname(dirname, fullpath, FNAMESIZE);
		FREE(fullpath); fullpath = NULL;
		snprintf(xtbuff, sizeof(xtbuff), "%s (%s) - eda",
			basename, dirname);
		xtbuff[sizeof(xtbuff)-1] = '\0';
		if (xterm_title("xtbuff"))
			cnf.gstat &= ~GSTAT_AUTOTITLE;
	}

	return;
}

/* show buffer names in a sub-header line
*/
void
show_tabheader (void)
{
	int ri, len=0, plen=0, fnl, off;
	char tabheader[RINGSIZE*20];
	int space, after, before, ri0;

	/* fill tabheader[] */
	memset(tabheader, '\0', sizeof(tabheader));
	ri0 = RINGSIZE;
	ri = cnf.ring_curr;
	do {
		if (cnf.fdata[ri].fflag & FSTAT_OPEN) {
			if (ri < ri0) {
				before = len;
				ri0 = ri;
			}
			fnl = strlen(cnf.fdata[ri].fname);
			off = (fnl > 16) ? fnl-16 : 0;
			if (ri == cnf.ring_curr)
				plen = strlen(&cnf.fdata[ri].fname[off]) + 3;
			if (fnl > 16)
				sprintf(&tabheader[len], "<%s | ", &cnf.fdata[ri].fname[off+1]);
			else
				sprintf(&tabheader[len], "%s | ", &cnf.fdata[ri].fname[off]);
			len += strlen(&tabheader[len]);
		}

		ri = (ri<RINGSIZE-1) ?  ri+1 : 0;
	} while (ri != cnf.ring_curr);

	/* output cannot be more than cnf.maxx, 4 bytes reserved */
	space = MIN(cnf.maxx-4, len);

	if (len-3 > space) {
		/* second part to show, starting with current buffer, 'after' is the length following current buffer name */
		after = (space - plen)/2;
		while (plen+after < space) {
			if (tabheader[plen+after] == '|')
				break;
			after++;
		}
		if (plen+after < space)
			after--; /* skip back from '|' */
		else
			after = -3; /* no match, for example only one buffer */

		/* first part to show, the rest of tabheader[], show before the current buffer, length is 'before' */
		before = space - (plen+after) +2;
		while (before > 0) {
			if (tabheader[len-before] == '|')
				break;
			--before;
		}
		if (before > 2 && tabheader[len-before] == '|')
			before -= 2; /* found, get back the 2 bytes */
		else
			before = 0; /* just a fallback */
	}

	/* colorize */
	set_color_attribute (stdscr, COLOR_TRACEMSG_TEXT);
	wmove (stdscr, 1, 0);
	wclrtoeol(stdscr);
	if (len-3 > space) {
		tabheader[plen+after] = '\0';
		mvwprintw (stdscr, 1, 0, "< %s%s >", &tabheader[len-before], &tabheader[0]);
		set_color_attribute (stdscr, COLOR_SELECT_FLAG+COLOR_HIGH_TEXT);
		mvwaddnstr (stdscr, 1, before+2, &tabheader[0], plen-3);
	} else {
		if (before >  3) {
			tabheader[before-3] = '\0';
			mvwprintw (stdscr, 1, 0, "[ %s%s ]", &tabheader[before], &tabheader[0]);
			set_color_attribute (stdscr, COLOR_SELECT_FLAG+COLOR_HIGH_TEXT);
			mvwaddnstr (stdscr, 1, len-before+2, &tabheader[0], plen-3);
		} else {
			tabheader[len-3] = '\0';
			mvwprintw (stdscr, 1, 0, "[ %s ]", &tabheader[0]);
			set_color_attribute (stdscr, COLOR_SELECT_FLAG+COLOR_HIGH_TEXT);
			mvwaddnstr (stdscr, 1, 2, &tabheader[0], plen-3);
		}
	}
	wattrset (stdscr, A_NORMAL);
	wnoutrefresh (stdscr);

	return;
}

/* update command line
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

	set_color_attribute (stdscr, COLOR_CMDLINE_TEXT);
	mvwaddnstr (stdscr, cnf.maxy-1, 0, obuff, cnf.maxx);
	wattrset (stdscr, A_NORMAL);
	wnoutrefresh (stdscr);

	return;
}

/* update text area, around the focus line
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
					text_line (lp, lineno, old_focus, 0);
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
					text_line (lp, lineno, old_focus, 0);
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
		/* update the lines above focus, upto top (bottom->up)
		*/
		lp = CURR_LINE;
		lineno = CURR_FILE.lineno;
		focus = CURR_FILE.focus;
		while (focus > 0) {
			prev_lp (cnf.ring_curr, &lp, &cnt);
			lineno -= cnt;
			if ((cnf.gstat & GSTAT_SHADOW) && (cnt > 1)) {
				focus--;
				shadow_empty_line (focus, cnt-1);
				if (focus==0)
					break;
			}
			focus--;
			if (lp->lflag & LSTAT_TOP) {
				/* hide TOP mark */
				empty_line (focus);
				break;
			} else {
				text_line (lp, lineno, focus, 0);
			}
		}
		while (focus > 0) {	/* empty lines up */
			focus--;
			empty_line (focus);
		}

		/* update the lines below focus, upto bottom (top->down)
		*/
		lp = CURR_LINE;
		lineno = CURR_FILE.lineno;
		focus = CURR_FILE.focus;
		while (focus < TEXTROWS-1) {
			next_lp (cnf.ring_curr, &lp, &cnt);
			lineno += cnt;
			if ((cnf.gstat & GSTAT_SHADOW) && (cnt > 1)) {
				focus++;
				shadow_empty_line (focus, cnt-1);
				if (focus==TEXTROWS-1)
					break;
			}
			focus++;
			if (lp->lflag & LSTAT_BOTTOM) {
				/* hide EOF mark */
				empty_line (focus);
				break;
			} else {
				text_line (lp, lineno, focus, 0);
			}
		}
		while (focus < TEXTROWS-1) {	/* empty lines down */
			focus++;
			empty_line (focus);
		}
	}

	wnoutrefresh (stdscr);

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
		mvwaddnstr (stdscr, cnf.head+focus, cnf.pref+begin, &obuff[begin], length);
	}

	return;
}

/* text line output formatter
*/
static void
text_line (LINE *lp, int lineno, int focus, int focus_flag)
{
	const char *ibuff = lp->buff;
	int llen = lp->llen;
	int lflag = lp->lflag;
	int idx;

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

	if (lflag & LSTAT_TAG1)
		idx = COLOR_TAGGED_TEXT;
	else
		idx = COLOR_NORMAL_TEXT;
	if (focus_flag)
		idx += COLOR_FOCUS_FLAG;
	if (lflag & LSTAT_SELECT)
		idx += COLOR_SELECT_FLAG;
	set_color_attribute (stdscr, idx);

	/* prefix --> obuff[] */
	if (cnf.gstat & GSTAT_PREFIX)
	{
		if (cnf.pref < 3 || lineno < 0 ) {
			for (i=0; i<cnf.pref; i++) {
				obuff[i] = ' ';
			}
		} else {
			if (lflag & LSTAT_BM_BITS) {
				bm_char =  ((lflag & LSTAT_BM_BITS) >> BM_BIT_SHIFT) + '0';
			}
			if (lflag & LSTAT_CHANGE) {
				alt_char = '*';
			} else if (lflag & LSTAT_ALTER) {
				alt_char = '.';
			}
			obuff[0] = (char)bm_char;
			obuff[1] = (char)alt_char;
			/* show only the (PREFIXSIZE-3) lower decimal characters --> 00001...99999 */
			for (i=cnf.pref-2; i > 1; i--) {
				obuff[i] = lineno%10 + '0';
				lineno /= 10;
			}
			obuff[cnf.pref-1] = (lflag & LSTAT_TRUNC) ? 'x' : ' ';
			obuff[cnf.pref] = '\0';
		}
		mvwaddnstr (stdscr, cnf.head+focus, 0, obuff, cnf.pref);
	}

	/* selection lines must be printed after line end */
	padsize = MIN(CMDLINESIZE-1, cnf.maxx-cnf.pref);
	memset(obuff, ' ', (size_t)padsize);
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
	mvwaddnstr (stdscr, cnf.head+focus, cnf.pref, obuff, padsize);
	wattrset (stdscr, A_NORMAL);

	/*
	 * highlight
	 */
	if (CURR_FILE.fflag & FSTAT_TAG5)
	{
		idx = COLOR_HIGH_TEXT;
		if (focus_flag)
			idx += COLOR_FOCUS_FLAG;
		if (lflag & LSTAT_SELECT)
			idx += COLOR_SELECT_FLAG;
		set_color_attribute (stdscr, idx);

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
		wattrset (stdscr, A_NORMAL);
	}

	/*
	 * search (or change)
	 * change: only focus and below
	 */
	if (((CURR_FILE.fflag & (FSTAT_TAG2 | FSTAT_TAG3)) == FSTAT_TAG2) ||
	    ((CURR_FILE.fflag & FSTAT_TAG3) && (focus >= CURR_FILE.focus)))
	{
		idx = COLOR_SEARCH_TEXT;
		if (focus_flag)
			idx += COLOR_FOCUS_FLAG;
		if (lflag & LSTAT_SELECT)
			idx += COLOR_SELECT_FLAG;
		set_color_attribute (stdscr, idx);

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
		wattrset (stdscr, A_NORMAL);
	}

	return;
} /* text_line() */

/* get the count of alien characters in the line
*/
int
alien_count (LINE *lp)
{
	const char *ibuff = lp->buff;
	int llen = lp->llen;
	int i, j, pos;
	int count = 0;

	i = j = pos = 0;
	while (i<llen && ibuff[i] != '\0') {
		/* until we have wide char support... */
		if (ibuff[i] < 0 || ibuff[i] >= 0x7f) {
			count++;
		}
		i++;
	}

	return count;
}

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
}

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
}

/* update curpos and lnoff in current file/line; uniform output rendering
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

/* move the cursor to new position by pointer
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

/* update vertical focus position; uniform output rendering
*/
void
update_focus (MOTION_TYPE motion_type, int ri)
{
	int away_top, away_bottom;

	switch(motion_type)
	{
	case FOCUS_ON_1ST_LINE:
		cnf.fdata[ri].focus = 0;
		break;

	case FOCUS_ON_2ND_LINE:
		cnf.fdata[ri].focus = 1;
		break;

	case FOCUS_ON_LASTBUT1_LINE:
		cnf.fdata[ri].focus = MIN(cnf.fdata[ri].num_lines, TEXTROWS-2);
		break;

	case FOCUS_ON_LAST_LINE:
		cnf.fdata[ri].focus = TEXTROWS-1;
		break;

	case INCR_FOCUS:
		if (cnf.fdata[ri].focus < TEXTROWS-1)
			cnf.fdata[ri].focus++;
		break;

	case DECR_FOCUS:
		if (cnf.fdata[ri].focus > 0)
			cnf.fdata[ri].focus--;
		break;

	case INCR_FOCUS_SHADOW:
		if (cnf.fdata[ri].focus < TEXTROWS-2)
			cnf.fdata[ri].focus += 2;
		else if (cnf.fdata[ri].focus < TEXTROWS-1)
			cnf.fdata[ri].focus += 1;
		break;

	case DECR_FOCUS_SHADOW:
		if (cnf.fdata[ri].focus > 1)
			cnf.fdata[ri].focus -= 2;
		else if (cnf.fdata[ri].focus > 0)
			cnf.fdata[ri].focus -= 1;
		break;

	case CENTER_FOCUSLINE:
		if (TEXTROWS > 0)
			cnf.fdata[ri].focus = TEXTROWS/2;
		else
			cnf.fdata[ri].focus = TRACESIZE;
		break;

	case FOCUS_AWAY_TOP:
		away_top = TEXTROWS/5;
		if (cnf.fdata[ri].focus < away_top)
			cnf.fdata[ri].focus = away_top;
		else if (cnf.fdata[ri].focus > away_top)
			cnf.fdata[ri].focus--;
		break;

	case FOCUS_AWAY_BOTTOM:
		away_bottom = TEXTROWS - TEXTROWS/5;
		if (cnf.fdata[ri].focus > away_bottom)
			cnf.fdata[ri].focus = away_bottom;
		else if (cnf.fdata[ri].focus < away_bottom)
			cnf.fdata[ri].focus++;
		break;

	case FOCUS_AVOID_BORDER:
		away_top = TEXTROWS/9;
		if (away_top < 3) away_top = 3;
		away_bottom = TEXTROWS - away_top;
		if (cnf.fdata[ri].focus < away_top)
			cnf.fdata[ri].focus = away_top;
		else if (cnf.fdata[ri].focus > away_bottom)
			cnf.fdata[ri].focus = away_bottom;
		break;

	default:
		break;
	}
} /* update_focus */

/* update trace message window
*/
void
upd_trace (void)
{
	int f, i;

	/* trace message lines (only once) */
	if (cnf.trace > 0) {
		set_color_attribute (stdscr, COLOR_TRACEMSG_TEXT);
		for (f=0; f<cnf.trace && f<TEXTROWS-1; f++) {
			wmove (stdscr, cnf.head+f, 0);
			wclrtoeol(stdscr);
			/**/
			for (i=0; i<cnf.maxx ; i++) {
				if (cnf.tracerow[f][i] == '\0' || cnf.tracerow[f][i] == '\n')
					break;
			}
			if (i < cnf.maxx)
				cnf.tracerow[f][i++] = ' ';
			cnf.tracerow[f][i] = '\0';
			mvwaddnstr (stdscr, cnf.head+f, 0, cnf.tracerow[f], cnf.maxx);
			/**/
			cnf.tracerow[f][0] = '\0';
		}
		wattrset (stdscr, A_NORMAL);
		wnoutrefresh (stdscr);
	}
	cnf.trace=0;	/* this output finished */

	return;
}

/* space padded or cutted line output (return static string)
*/
static const char *
center_fname (const char *ibuff, const char *catbuff, int catlen, int padsize)
{
	static char obuff[CMDLINESIZE+1];
	int i, j, k, ilen=0;

	ilen = strlen(ibuff);
	padsize = MIN(padsize, CMDLINESIZE);	/* save obuff[] */

	k = ilen+catlen;
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
	} else {
		/* cut prefix */
		j=0;
		obuff[j++] = '<';
		for (i=k-padsize+1; i<ilen; i++)
			obuff[j++] = ibuff[i];
		for (i=0; i<catlen; i++, j++)
			obuff[j] = catbuff[i];
		obuff[j] = '\0';
	}

	return (obuff);
}

/* other simple outputs, shadow line, empty line
*/
static void
shadow_empty_line (int focus, int count)
{
	wmove (stdscr, cnf.head+focus, 0);
	set_color_attribute (stdscr, COLOR_SHADOW_TEXT);
	wclrtoeol (stdscr);
	if (count > 1) {
		mvwprintw (stdscr, cnf.head+focus, cnf.pref, "--- %d lines ---", count);
	} else if (count == 1) {
		mvwprintw (stdscr, cnf.head+focus, cnf.pref, "--- 1 line ---");
	} else {
		mvwaddnstr (stdscr, cnf.head+focus, 0, "~", 1);
	}
	wattrset (stdscr, A_NORMAL);
	return;
}

/* color configuration after nc colors set
*/
int
init_colors_and_cpal (void)
{
	short pair, fg, bg;
	chtype bkgd;

	if (!cnf.bootup) {
		/* init cnf.cpairs -- only once, after start_color() */
		pair=1; /* skip pair 0 anyway */
		for (bg=0; bg < COLORS; bg++) {
			for (fg=0; fg < COLORS; fg++) {
				if (init_pair(pair, fg, bg) != ERR) {
					cnf.cpairs[bg*COLORS+fg] = COLOR_PAIR(pair);
				}
				pair++;
			}
		}
	}

	/* copy configured palette to cnf.cpal */
	if (cnf.palette >= 0 && cnf.palette < cnf.palette_count) {
		cnf.cpal = cnf.palette_array[ cnf.palette ];
	}

	/* after each palette change */
	bkgd = (chtype)' ' | cnf.cpairs[cnf.cpal.bits[COLOR_NORMAL_TEXT] & 0x3f];
	wbkgdset (stdscr, bkgd);

	return 0;
}

static const char *color_names[] = {
	"black",
	"red",
	"green",
	"yellow",
	"blue",
	"magenta",
	"cyan",
	"white"
};

/* parse color palette config string, not using nc color macros and defines
*/
int
color_palette_parser(const char *input)
{
	int len, i, j, fg, bg, bold, reverse, ret=1;
	CPAL cpal, *ptr;
	memset(cpal.name, '\0', sizeof(cpal.name));

	len = strlen(input);

	for (j=0; j < len && input[j] != ' '; j++) {
		if (j < (int)sizeof(cpal.name)-1)
			cpal.name[j] = input[j];
	}

	for (i=0; i < CPAL_BITS && j < len; i++) {
		if (input[j] == ' ')
			j++;
		if (j+3 >= len)
			break;
		fg = (input[j] >= '0' && input[j] <= '7') ? input[j]-'0' : 7;
		j++;
		bg = (input[j] >= '0' && input[j] <= '7') ? input[j]-'0' : 0;
		j++;
		bold = (input[j] == 'b');
		j++;
		reverse = (input[j] == 'r');
		j++;
		cpal.bits[i] = ((8*bg+fg) & 0x3f) | bold*0x40 | reverse*0x80;
	}
	if (i == CPAL_BITS) {
		if (cnf.palette_array == NULL) {
			ptr = (CPAL *)MALLOC(sizeof(CPAL) * 2);
			if (ptr == NULL) {
				ERRLOG(0xE034);
				ret = 1;
			} else {
				cnf.palette_array = ptr;
				cnf.palette_array[ 0 ] = cnf.cpal;
				cnf.palette_array[ 1 ] = cpal;
				cnf.palette_count = 2;
				ret = 0;
			}
		} else {
			ptr = (CPAL *)REALLOC((void *)cnf.palette_array, sizeof(CPAL) * (size_t)(cnf.palette_count + 1));
			if (ptr == NULL) {
				ERRLOG(0xE007);
				ret = 1;
			} else {
				cnf.palette_array = ptr;
				cnf.palette_array[ cnf.palette_count ] = cpal;
				cnf.palette_count++;
				ret = 0;
			}
		}
	}

	return ret;
}

void
color_test(void)
{
	short pair, fg, bg;
	int opt_bold, opt_reverse, i;
	const int width=8; // the text width of matrix entries
	char buff[8+1];
	memset(buff, '\0', sizeof(buff));

	initscr ();	/* Begin */
	cbreak ();
	noecho ();
	nonl ();
	clear ();
	if (!has_colors()) {
		endwin();
		fprintf(stderr, "color test: this terminal has no colors\n");
		return;
	}
	start_color();

	// min 39x82
	wprintw (stdscr, "eda: color test -- colors %d pairs %d -- lines %d cols %d\n",
		COLORS, COLOR_PAIRS, LINES, COLS);
	wprintw (stdscr, "\n");

	/* init pairs -- skip pair 0 anyway */
	pair=1;
	for (bg=0; bg < COLORS; bg++) {
		for (fg=0; fg < COLORS; fg++) {
			if (init_pair(pair, fg, bg) != ERR) {
				cnf.cpairs[bg*COLORS+fg] = COLOR_PAIR(pair);
			}
			pair++;
		}
	}

	// print header
	wprintw (stdscr, "%-*s ", width, " ");
	for (fg=0; fg < COLORS; fg++) {
		wprintw (stdscr, "%-*s ", width, color_names[fg]);
	}
	wprintw (stdscr, "\n");

	opt_bold = opt_reverse = 0;
	for (i=0; i < 4; i++) {
		// 8x8 color matrix with normal, bold, reverse, reverse+bold options
		opt_bold = i & 1;
		opt_reverse = i & 2;

		for (bg=0; bg < COLORS; bg++) {
			wprintw (stdscr, "%-*s ", width, color_names[bg]);
			for (fg=0; fg < COLORS; fg++) {
				sprintf(buff, "{%d%d%c%c}", fg, bg, (opt_bold ? 'b' : '_'), (opt_reverse ? 'r' : '_'));
				//sprintf(buff, "{%02x}", bg*8+fg + (opt_bold ? 0x40 : 0) + (opt_reverse ? 0x80 : 0));
				attron( cnf.cpairs[bg*COLORS+fg] );
				if (opt_bold)
					attron(A_BOLD);
				if (opt_reverse)
					attron(A_REVERSE);
				wprintw (stdscr, "%-*s", width, buff);
				attrset(A_NORMAL);
				wprintw (stdscr, " ");
			}
			wprintw (stdscr, "\n");
		}
		wprintw (stdscr, "\n");
	}
	refresh ();

	endwin ();	/* End */
	return;
}
