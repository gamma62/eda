/*
* ed.c
* editor and main event handler, command parser and run, external macro run, command table tools
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
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>	/* abort() */
#include <syslog.h>
#include "curses_ext.h"
#include "main.h"
#include "proto.h"

/* global config */
extern CONFIG cnf;
/* extern int ESCDELAY; */
extern MEVENT pointer;

#include "command.h"
const int TLEN = sizeof(table)/sizeof(TABLE);
const int KLEN = sizeof(keys)/sizeof(KEYS);
const int RES_KLEN = RESERVED_KEYS;
MACROS *macros;
int MLEN;

/* local proto */
static void event_handler (void);
static int parse_cmdline (char *ibuff, int ilen, char *args);

/*
* editor
*/
void
editor (void)
{
	initscr ();	/* Begin */
	if (!has_colors()) {
		endwin ();
		fprintf(stderr, "terminal has no colors\n");
		return;
	} else {
		start_color();
	}
	cbreak ();
	noecho ();
	nonl ();
	clear ();
	typeahead (0); /* check stdin for typeahead, we used initscr() */

	getmaxyx (stdscr, cnf.maxy, cnf.maxx);
	if (cnf.maxy < 1 || cnf.maxx < 1) {
		fprintf(stderr, "cannot get terminal window size\n");
		return;
	}
	cnf.wstatus = newwin (1, cnf.maxx, 0, 0);
	cnf.wtext = newwin (cnf.maxy - 2, cnf.maxx, 1, 0);
	cnf.wbase = newwin (1, cnf.maxx, cnf.maxy - 1, 0);

	init_colors (cnf.palette);

	/* keys */
	keypad (cnf.wtext, TRUE);
	keypad (cnf.wbase, TRUE);
	/* timeouts */
	ESCDELAY = CUST_ESCDELAY;
	wtimeout (cnf.wtext, CUST_WTIMEOUT);
	wtimeout (cnf.wbase, CUST_WTIMEOUT);
	/*
	 * do not use signal handler on SIGWINCH because this inhibits KEY_RESIZE
	 * and cannot delay the action; the library does malloc operations
	 */

	/* ready */
	mouse_support();
	cnf.bootup = 1;

	event_handler ();

	clear ();
	refresh ();
	endwin ();	/* End */
	cnf.bootup = 0;

	MAIN_LOG(LOG_NOTICE, "<<< pid=%d", getpid());
} /* editor() */

/*
* handle resize event (KEY_RESIZE)
*/
void
app_resize (void)
{
	/* get max values of stdscr */
	getmaxyx (stdscr, cnf.maxy, cnf.maxx);
	if (cnf.maxy < 1 || cnf.maxx < 1) {
		return;
	}

	/* reset begin screen values */
	if ((cnf.wstatus == NULL) || (cnf.wtext == NULL) || (cnf.wbase == NULL)) {
		return;
	}
	(cnf.wstatus)->_begy = 0;
	(cnf.wstatus)->_begx = 0;
	(cnf.wtext)->_begy = 1;
	(cnf.wtext)->_begx = 0;
	(cnf.wbase)->_begy = cnf.maxy - 1;
	(cnf.wbase)->_begx = 0;

	wresize (cnf.wstatus, 1, cnf.maxx);
	wresize (cnf.wtext, cnf.maxy - 2, cnf.maxx);
	wresize (cnf.wbase, 1, cnf.maxx);

	if (CURR_FILE.focus > TEXTROWS-1) {
		CURR_FILE.focus = TEXTROWS-1;
	}
	if (CURR_FILE.curpos - CURR_FILE.lnoff > TEXTCOLS-1) {
		CURR_FILE.curpos = 0;
		CURR_FILE.lnoff = 0;
		CURR_FILE.lncol = 0;
	}
	if (cnf.clpos - cnf.cloff > cnf.maxx-1) {
		cnf.clpos = 0;
		cnf.cloff = 0;
	}
}

int
run_macro_command (int mi, char *args_inbuff)
{
	int ii=0, ix=0, iy=0, jj=0, kk=0, xx=0, args_cnt=0, exec=0;
	char dup_buffer[CMDLINESIZE];
	char *args[MAXARGS];
	char *mptr;
	char args_ready[CMDLINESIZE];

	if ( !(macros[mi].mflag & (CURR_FILE.fflag & FSTAT_CHMASK)) ) {
		strncpy(dup_buffer, args_inbuff, CMDLINESIZE);
		dup_buffer[CMDLINESIZE-1] = '\0';
		args_cnt = parse_args (dup_buffer, args);

		CMD_LOG(LOG_DEBUG, "macro: run mi=%d name=[%s] key=0x%02x args=[%s] cnt=%d",
			mi, macros[mi].name, macros[mi].fkey, args_inbuff, args_cnt);

		cnf.gstat |= GSTAT_SILENT;
		for (ii=0; ii < macros[mi].items; ii++) {
			ix = macros[mi].maclist[ii].m_index;
			if (table[ix].tflag & TSTAT_ARGS) {
				mptr = macros[mi].maclist[ii].m_args;
				//--- compile actual args list from macro args string and commandline
				jj = kk = 0;
				while (mptr[jj] != '\0' && kk+1 < CMDLINESIZE-1) {
					if (mptr[jj] == '\\' && mptr[jj+1] != '\0') {
						// escaped, copy args literally
						args_ready[kk++] = mptr[jj];
						args_ready[kk++] = mptr[jj+1];
						jj += 2;
					} else if (mptr[jj] == '$' && mptr[jj+1] >= '0' && mptr[jj+1] <= '9') {
						iy = mptr[jj+1] - '0';
						if (iy > 0 && iy <= args_cnt) {
							// copy args[iy-1]
							for (xx=0; args[iy-1][xx] != '\0' && kk < CMDLINESIZE-1; xx++) {
								args_ready[kk++] = args[iy-1][xx];
							}
						} else if (iy == 0) {
							// copy whole args_inbuff
							for (xx=0; args_inbuff[xx] != '\0' && kk < CMDLINESIZE-1; xx++) {
								args_ready[kk++] = args_inbuff[xx];
							}
						} else {
							// fail, copy args literally
							args_ready[kk++] = mptr[jj];
							args_ready[kk++] = mptr[jj+1];
						}
						jj += 2;
					} else if (mptr[jj] == '^' && mptr[jj+1] == 'G') {
						// copy (short) filename
						for (xx=0; CURR_FILE.fname[xx] != '\0' && kk < CMDLINESIZE-1; xx++) {
							args_ready[kk++] = CURR_FILE.fname[xx];
						}
						jj += 2;
					} else {
						args_ready[kk++] = mptr[jj++];
					}
				}
				args_ready[kk] = '\0';
				//--- ready
				REC_LOG(LOG_DEBUG, "trace macro %d [%s] [%s]", ix, table[ix].fullname, args_ready);
				if (cnf.gstat & GSTAT_RECORD)
					record(table[ix].fullname, args_ready);
				exec = (table[ix].funcptr)(args_ready);
			} else {
				REC_LOG(LOG_DEBUG, "trace macro %d [%s]", ix, table[ix].fullname);
				if (cnf.gstat & GSTAT_RECORD)
					record(table[ix].fullname, "");
				exec = ((FUNCP0) table[ix].funcptr)();
			}
			if (exec) {
				CMD_LOG(LOG_WARNING, "macro: (mi=%d name=[%s] key=0x%02x) last command (ti=%d) [%s] returned: %d",
					mi, macros[mi].name, macros[mi].fkey, ix, table[ix].fullname, exec);
				break;	/* stop macro */
			}
		}/* macros[mi].maclist[] */
		cnf.gstat &= ~GSTAT_SILENT;

		/* force screen update */
		cnf.gstat &= ~(GSTAT_UPDNONE | GSTAT_UPDFOCUS);
	}/* change allowed */

	return (exec);
}

int
run_command (int ti, const char *args_inbuff)
{
	int exec = 0;

	if ( !(table[ti].tflag & (CURR_FILE.fflag & FSTAT_CHMASK)) ) {

		CMD_LOG(LOG_DEBUG, "command: run ti=%d name=[%s] key=0x%02x args=[%s]",
			ti, table[ti].name, table[ti].fkey, args_inbuff);

		if (table[ti].tflag & TSTAT_ARGS) {
			REC_LOG(LOG_DEBUG, "command %d [%s] [%s]", ti, table[ti].fullname, args_inbuff);
			if (cnf.gstat & GSTAT_RECORD)
				record(table[ti].fullname, args_inbuff);
			exec = (table[ti].funcptr)(args_inbuff);
		} else {
			REC_LOG(LOG_DEBUG, "command %d [%s]", ti, table[ti].fullname);
			if (cnf.gstat & GSTAT_RECORD)
				record(table[ti].fullname, "");
			exec = ((FUNCP0) table[ti].funcptr)();
		}
		if (exec) {
			CMD_LOG(LOG_WARNING, "command: last (ti=%d) [%s] returned: %d",
				ti, table[ti].fullname, exec);
		}
	}/* change allowed */

	return (exec);
}

/*
* editor's main event handler
*/
static void
event_handler (void)
{
	int ch = 0, ret = 0;
	int ti = -1, mi = -1;
	unsigned delay_cnt_4stat = 0;
	int clear_trace_next_time = 0;
	char args_buff[CMDLINESIZE];

	wclear (cnf.wstatus);
	wclear (cnf.wbase);
	wclear (cnf.wtext);

	clhistory_push("not used", 8);
	if (!cnf.noconfig) {
		load_clhistory ();
	}
	reset_cmdline();
	memset(args_buff, 0, CMDLINESIZE);

	/* optional automacro run */
	if (cnf.ring_size > 0 && cnf.automacro[0] != '\0' && (cnf.noconfig == 0)) {
		int autolength;
		autolength = strlen(cnf.automacro);
		ti = parse_cmdline (cnf.automacro, autolength, args_buff);
		if (ti >= TLEN && ti < TLEN+MLEN) {
			mi = ti - TLEN;
			run_macro_command (mi, args_buff);
		} else {
			CMD_LOG(LOG_WARNING, "macro [%s] does not exist", cnf.automacro);
		}
	}
	while (cnf.ring_size > 0)
	{
		/*
		 * critical error checks
		 */
		if (CURR_LINE == NULL) {
			MAIN_LOG(LOG_CRIT, "assert, CURR_LINE==NULL (ri=%d) -- abort", cnf.ring_curr);
			abort();
			break;
		}
		if (CURR_LINE->buff == NULL) {
			MAIN_LOG(LOG_CRIT, "assert, CURR_LINE->buff==NULL (ri=%d) -- abort", cnf.ring_curr);
			abort();
			break;
		}
		if ((CURR_LINE->llen < 1) || (CURR_FILE.lncol < 0)) {
			MAIN_LOG(LOG_CRIT, "assert, llen %d lncol %d lineno %d -- abort",
				CURR_LINE->llen, CURR_FILE.lncol, CURR_FILE.lineno);
			abort();
			break;
		}

		if (cnf.cmdline_len >= CMDLINESIZE || cnf.cmdline_buff[cnf.cmdline_len] != '\0') {
			MAIN_LOG(LOG_ERR, "cmdline buffer not terminated [%c%c%c...] (len=%d) -- fixing",
				cnf.cmdline_buff[0], cnf.cmdline_buff[1], cnf.cmdline_buff[2], cnf.cmdline_len);
			reset_cmdline();
		}
		if (CURR_LINE->buff[CURR_LINE->llen-1] != '\n') {
			MAIN_LOG(LOG_ERR, "line %d: NL missing! -- fixing", CURR_FILE.lineno);
			CURR_LINE->buff[CURR_LINE->llen-1] = '\n';
		}
		if (CURR_LINE->buff[CURR_LINE->llen] != '\0') {
			MAIN_LOG(LOG_ERR, "line %d: final zero missing! -- fixing", CURR_FILE.lineno);
			CURR_LINE->buff[CURR_LINE->llen] = '\0';
		}
		if (TEXTROWS < TRACESIZE+1) {
			MAIN_LOG(LOG_ERR, "text window has very few lines (%d)", TEXTROWS);
		}

		/*
		 * regular screen update (REFRESH_EVENT used to force update)
		 */
		if (ch != ERR) {
			upd_statusline ();
			upd_termtitle ();
			if (cnf.trace > 0) {
				UPD_LOG(LOG_DEBUG, "full update with trace in this cycle");
				if (CURR_FILE.focus < cnf.trace+1)
					CURR_FILE.focus = cnf.trace+1;
				upd_text_area (0);
				upd_trace ();
				clear_trace_next_time = 1;
			} else if (clear_trace_next_time) {
				UPD_LOG(LOG_DEBUG, "full update now, trace in previous cycle");
				upd_text_area (0);
				clear_trace_next_time = 0;
			} else {
				if (!(cnf.gstat & GSTAT_UPDNONE)) {
					if (cnf.gstat & GSTAT_UPDFOCUS) {
						UPD_LOG(LOG_DEBUG, "update focus line only");
					} else {
						UPD_LOG(LOG_DEBUG, "update full page");
					}
					upd_text_area (cnf.gstat & GSTAT_UPDFOCUS);
				}
			}
			cnf.gstat &= ~(GSTAT_UPDNONE | GSTAT_UPDFOCUS);
			upd_cmdline ();
		}
		doupdate ();

		/*
		 * wait for input, do cursor reposition
		 */
		if (CURR_FILE.fflag & FSTAT_CMD) {
			if (cnf.clpos-cnf.cloff > TEXTCOLS-1 || cnf.clpos-cnf.cloff < 0) {
				CMD_LOG(LOG_ERR, "bad command line cursor position : clpos=%d cloff=%d -- fixing",
					cnf.clpos, cnf.cloff);
				if (cnf.clpos-cnf.cloff > TEXTCOLS-1)
					cnf.clpos = cnf.cloff + TEXTCOLS-1;
				else if (cnf.clpos-cnf.cloff < 0)
					cnf.clpos = cnf.cloff;
			}
			wmove (cnf.wbase, 0, cnf.clpos-cnf.cloff);
			ch = key_handler (cnf.wbase, cnf.seq_tree);
		} else {
			if (CURR_FILE.curpos-CURR_FILE.lnoff > TEXTCOLS-1 || CURR_FILE.curpos-CURR_FILE.lnoff < 0) {
				CMD_LOG(LOG_ERR, "bad text cursor position : curpos=%d lnoff=%d -- fixing",
					CURR_FILE.curpos, CURR_FILE.lnoff);
				if (CURR_FILE.curpos-CURR_FILE.lnoff > TEXTCOLS-1)
					CURR_FILE.curpos = CURR_FILE.lnoff + TEXTCOLS-1;
				else if (CURR_FILE.curpos-CURR_FILE.lnoff < 0)
					CURR_FILE.curpos = CURR_FILE.lnoff;
			}
			wmove (cnf.wtext, CURR_FILE.focus, cnf.pref+CURR_FILE.curpos-CURR_FILE.lnoff);
			ch = key_handler (cnf.wtext, cnf.seq_tree);
		}
		delay_cnt_4stat++;

		/* delayed window resize, loop up */
		if (ch == KEY_RESIZE) {
			app_resize();
			force_redraw();
			continue;

		/* optional mouse positioning support, loop up */
		} else if (ch == KEY_MOUSE) {
			if (!set_position_by_pointer(pointer)) {
				cnf.gstat |= GSTAT_UPDFOCUS;
				CURR_FILE.fflag &= ~FSTAT_CMD;
			} else {
				ch = ERR;
			}
			continue;

		/* bg processing in time slots, loop up */
		} else if (ch == ERR) {
			if (delay_cnt_4stat > FILE_CHDELAY) {
				/* rare slots: stat disk-files
				*/
				if (check_files()) {
					ch = REFRESH_EVENT;
				}
				delay_cnt_4stat = 0;
			} else {
				/* most free slots:
				* check by non-blocking read if background processes have update
				*/
				ret = background_pipes();
				if (ret) {
					ch = REFRESH_EVENT;
				}
			}
			continue;
		}
		ret = 0;

		/*
		 * input processing
		 */
		if (CURR_FILE.fflag & FSTAT_CMD) {
			/* cmdline intern editing, (ret 0/1) */
			ret = ed_cmdline (ch);		/* 0/1 */

			if (ret && ch == KEY_RETURN) {
				/* save buffer to cmdline history */
				cmdline_to_clhistory (cnf.cmdline_buff, cnf.cmdline_len);

				if (cnf.cmdline_len > 0) {
					ti = parse_cmdline (cnf.cmdline_buff, cnf.cmdline_len, args_buff);
					if (ti >= TLEN && ti < TLEN+MLEN) {
						mi = ti - TLEN;
						run_macro_command (mi, args_buff);
					} else if (ti >= 0 && ti < TLEN) {
						run_command (ti, args_buff);
					} else {
						/* warning */
						tracemsg("unknown command [%s]", cnf.cmdline_buff);
					}
				}

				/* reset workbench after parse and run */
				reset_cmdline();
				/* do the history cleanup now */
				if (cnf.clhist_size > CLHISTSIZE + 50) {
					clhistory_cleanup(CLHISTSIZE);
				}
			}

		} else if (CURR_FILE.fflag & FSTAT_TAG3) {
			/* change in progress (ret 8) */
			repeat_change(ch);
			ret = 8;	/* key handled */

		} else {
			/* handle text-special cases here (ret 0/2) */
			ret = ed_text (ch);		/* 0/2 */
		}

		if (!ret && (cnf.ring_size > 0)) {
			/* the common key-handlings (ret 0/4) */
			ret = ed_common (ch);		/* 0/4 */

			/* warning */
			if (ret == 0) {
				tracemsg("unknown key 0x%02X", ch);
			}
		}

	} /* while */
	if (!cnf.noconfig) {
		save_clhistory ();
	}

	clhistory_cleanup(0);
	FREE(cnf.clhistory);
	cnf.clhistory = NULL;

} /* event_handler() */

/*
* parse the input buffer, separate command name and search in table[] and macros[]
* return: index to command, mi+TLEN or ti or failure, MLEN+TLEN
* ibuff[] -- zero inserted after possible command name
* args[] -- copy of arguments
*/
static int
parse_cmdline (char *ibuff, int ilen, char *args)
{
	int xi = (-1); /* index of command or macro */
	int clen = 0; /* length of command word in ibuff[] */
	int abeg = 0; /* begin of arguments in ibuff[] */
	int i=0;

	if (ibuff[0] == ':' && ibuff[1] >= '0' && ibuff[1] <= '9') {
		/* 0x3a goto_line() */
		clen = 1; /* only ":12" -- allow vi like macros to live */
		abeg = 1;
	} else if (ibuff[0] == '|' && ibuff[1] == '|') {
		/* 0x7c filter_shadow_cmd() */
		clen = 2; /* good for both "||a2ps -1 -f8" and "|| a2ps -1 -f8" */
		abeg = 2;
	} else if (ibuff[0] == '|') {
		/* 0x7c filter_cmd() */
		clen = 1; /* good for both "|sort" and "| wc -l" */
		abeg = 0;
	} else if (ibuff[0] == '/') {
		/* 0x2f search() */
		clen = 1; /* command and the first argument also, do not skip! */
		abeg = 0;
	} else {
		for (i=0; i<ilen; i++) {
			if (ibuff[i] ==' ' || ibuff[i] =='\t')
				break;
		}
		clen = i;
		for (i=clen; i<ilen; i++) {
			if (ibuff[i] !=' ' && ibuff[i] !='\t')
				break;
		}
		abeg = i;
	}
	//CMD_LOG(LOG_DEBUG, "clen %d abeg %d", clen, abeg);

	/* search down command name in macros[].name and table[].name
	*/
	for (xi=0; xi < MLEN; xi++)
	{
		if (macros[xi].name[0] != '\0') {
			//CMD_LOG(LOG_DEBUG, "try macro [%s]", macros[xi].name);
			for (i=0; i < clen; i++) {
				if (macros[xi].name[i] != ibuff[i])
					break;
			}
			if (i == clen && macros[xi].name[i] == '\0')
				break; /* match: xi is the macro index */
		}
	}
	if (xi < MLEN) {
		xi += TLEN; /* match, shift up */
	} else {
		for (xi=0; xi < TLEN; xi++)
		{
			if (table[xi].minlen >= 1 && table[xi].minlen <= clen) {
				for (i=0; i < clen; i++) {
					if (table[xi].name[i] != ibuff[i])
						break;
				}
				if (i == clen)	/* match: xi is the table index */
					break;
			}
		}
		if (xi == TLEN) {
			xi += MLEN; /* no match, shift up */
		}
	}

	if (xi == TLEN+MLEN) {
		/* not found */
		args[0] = '\0';
		ibuff[clen] = '\0';
	} else {
		/* prepare the argument list */
		for (i=abeg; i<ilen; i++) {
			args[i-abeg] = ibuff[i];
		}
		args[i-abeg] = '\0';
		ibuff[clen] = '\0';
		CMD_LOG(LOG_DEBUG, "command [%s] arguments [%s]", ibuff, args);
	}

	return (xi);
} /* parse_cmdline() */

/*
** force_redraw - force the screen redraw of current buffer
*/
int
force_redraw (void)
{
	cnf.gstat &= ~(GSTAT_UPDNONE | GSTAT_UPDFOCUS);
	wclear (cnf.wstatus);
	wclear (cnf.wbase);
	wclear (cnf.wtext);
	return (0);
}

/*
* search down fullname in table[].fullname, return index
*/
int
index_func_fullname (const char *fullname)
{
	int ti;
	for (ti=0; ti < TLEN; ti++) {
		if (strncmp(table[ti].fullname, fullname, 20) == 0) {
			break;
		}
	}
	return (ti);
}

/*
* search down key_string in keys[].key_string, return index
*/
int
index_key_string (const char *key_string)
{
	int ki;
	for (ki=0; ki < KLEN; ki++) {
		if (strncmp(keys[ki].key_string, key_string, 20) == 0) {
			break;
		}
	}
	return (ki);
}

/*
* search down key_value in keys[].key_value, return index
*/
int
index_key_value (int key_value)
{
	int ki;
	for (ki=0; ki < KLEN; ki++) {
		if (keys[ki].key_value == key_value) {
			break;
		}
	}
	return (ki);
}
