/*
* ed.c
* editor and main event handler, command parser and run, external macro run, command table tools
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
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>	/* abort() */
#include <syslog.h>
#include "curses_ext.h"
#include "main.h"
#include "proto.h"

/* global config */
extern CONFIG cnf;

extern TABLE table[];
extern KEYS keys[];
#include "command.h"
extern const int TLEN, KLEN, RES_KLEN;
const int TLEN = sizeof(table)/sizeof(TABLE);
const int KLEN = sizeof(keys)/sizeof(KEYS);
const int RES_KLEN = RESERVED_KEYS;
extern MACROS *macros;
MACROS *macros;
extern int MLEN;
int MLEN;
extern MEVENT mouse_event_pointer;
MEVENT mouse_event_pointer;

/* local proto */
static int getmaxyx_and_offset_sanity (void);
static void event_handler (void);
static int parse_cmdline (char *ibuff, int ilen, char *args);
static void errdump(void);

/*
* editor
*/
void
editor (void)
{
	initscr ();	/* Begin */
	cbreak ();
	noecho ();
	nonl ();
	typeahead (-1);

	if (getmaxyx_and_offset_sanity()) {
		fprintf(stderr, "cannot get terminal window size\n");
		return;
	}
	if (!has_colors()) {
		endwin();
		fprintf(stderr, "this terminal has no colors\n");
		return;
	}
	start_color();
	init_colors_and_cpal();

	/* function keys */
	keypad (stdscr, TRUE);
	/* timeouts */
	ESCDELAY = CUST_ESCDELAY;
	wtimeout (stdscr, CUST_WTIMEOUT);
	/*
	 * do not use signal handler on SIGWINCH because this inhibits KEY_RESIZE
	 * and cannot delay the action; the library does malloc operations
	 */

	/* ready */
	mouse_support();
	cnf.bootup = 1;

	event_handler ();

	if (cnf.gstat & GSTAT_AUTOTITLE) /* try to restore title */
		xterm_title ("xterm");
	if (cnf.automacro[0] == '\0') {
		/* not an engine test (automacro) */
		clear();
		refresh();
	}

	endwin ();	/* End */
	cnf.bootup = 0;

	MAIN_LOG(LOG_NOTICE, "<<< pid=%d", getpid());
} /* editor() */

/*
* handle resize event (KEY_RESIZE), sanity for focus and curpos/lnoff
*/
static int
getmaxyx_and_offset_sanity (void)
{
	int ri;

	/* get max values of stdscr */
	getmaxyx (stdscr, cnf.maxy, cnf.maxx);
	if (cnf.maxy < 1 || cnf.maxx < 1)
		return (1);

	/* fix focus and curpos/lnoff in each buffer */
	for (ri=0; ri<RINGSIZE; ri++) {
		if (cnf.fdata[ri].fflag & FSTAT_OPEN) {
			if (cnf.fdata[ri].focus > TEXTROWS-1)
				cnf.fdata[ri].focus = TEXTROWS-1;
			update_curpos(ri);
		}
	}

	/* reset cmdline position */
	cnf.clpos = 0;
	cnf.cloff = 0;

	return (0);
}

/* for update optimization */
static char upd_funcname[40];
static int upd_event;

int
run_macro_command (int mi, char *args_inbuff)
{
	int ix=0, iy=0, jj=0, kk=0, xx=0, args_cnt=0, exec=0;
	unsigned ii=0;
	char dup_buffer[CMDLINESIZE];
	char dup_fname[FNAMESIZE];
	char *args[MAXARGS+1];
	char *mptr;
	char args_ready[CMDLINESIZE];

	if ( !(macros[mi].mflag & (CURR_FILE.fflag & FSTAT_CHMASK)) ) {
		if (UPD_LOG_AVAIL(LOG_NOTICE)) {
			upd_funcname[0] = 'm';
			upd_funcname[1] = ':';
			strncpy(&upd_funcname[2], macros[mi].name, sizeof(upd_funcname)-3);
			upd_funcname[sizeof(upd_funcname)-1] = '\0';
		}

		strncpy(dup_fname, CURR_FILE.fname, FNAMESIZE);
		dup_fname[FNAMESIZE-1] = '\0';
		strncpy(dup_buffer, args_inbuff, CMDLINESIZE);
		dup_buffer[CMDLINESIZE-1] = '\0';
		args[0] = args_inbuff;
		args_cnt = parse_args (dup_buffer, args+1);

		CMD_LOG(LOG_NOTICE, "macro: run mi=%d name=[%s] key=0x%02x args=[%s] cnt=%d",
			mi, macros[mi].name, macros[mi].fkey, args_inbuff, args_cnt);

		cnf.gstat |= (GSTAT_SILENCE | GSTAT_MACRO_FG); // macro flags
		for (ii=0; ii < macros[mi].items; ii++) {
			ix = macros[mi].maclist[ii].m_index;
			if (table[ix].tflag & TSTAT_ARGS) {
				mptr = macros[mi].maclist[ii].m_args;
				/*--- compile actual args list from macro args string and commandline */
				jj = kk = 0;
				while (mptr[jj] != '\0' && kk+1 < CMDLINESIZE-1) {
					if (mptr[jj] == '\\' && mptr[jj+1] != '\0') {
						/* escaped, copy args literally */
						args_ready[kk++] = mptr[jj];
						args_ready[kk++] = mptr[jj+1];
						jj += 2;
					} else if (mptr[jj] == '$' && mptr[jj+1] >= '0' && mptr[jj+1] <= '9') {
						iy = mptr[jj+1] - '0';
						if (iy >= 0 && iy <= args_cnt) {
							/* copy args[iy] */
							for (xx=0; args[iy][xx] != '\0' && kk < CMDLINESIZE-1; xx++) {
								args_ready[kk++] = args[iy][xx];
							}
						} else {
							/* fail, copy args literally */
							args_ready[kk++] = mptr[jj];
							args_ready[kk++] = mptr[jj+1];
						}
						jj += 2;
					} else if (mptr[jj] == '^' && mptr[jj+1] == 'G') {
						/* copy (short) filename */
						for (xx=0; dup_fname[xx] != '\0' && kk < CMDLINESIZE-1; xx++) {
							args_ready[kk++] = dup_fname[xx];
						}
						jj += 2;
					} else {
						args_ready[kk++] = mptr[jj++];
					}
				}
				args_ready[kk] = '\0';
				/*--- ready */
				if (cnf.gstat & GSTAT_RECORD)
					record(table[ix].fullname, args_ready);
				exec = (table[ix].funcptr)(args_ready);
			} else {
				if (cnf.gstat & GSTAT_RECORD)
					record(table[ix].fullname, "");
				exec = ((FUNCP0) table[ix].funcptr)();
			}
			if (exec) {
				CMD_LOG(LOG_ERR, "macro: (mi=%d name=[%s] key=0x%02x) last command (ti=%d) [%s] returned: %d",
					mi, macros[mi].name, macros[mi].fkey, ix, table[ix].fullname, exec);
				break;	/* stop macro */
			}
		}/* macros[mi].maclist[] */
		cnf.gstat &= ~(GSTAT_SILENCE | GSTAT_MACRO_FG);

		/* force screen update */
		cnf.gstat &= ~(GSTAT_UPDNONE | GSTAT_UPDFOCUS);
	}/* change allowed */

	return (exec);
}

int
run_command (int ti, const char *args_inbuff, int fkey)
{
	int exec = 0;

	if ( !(table[ti].tflag & (CURR_FILE.fflag & FSTAT_CHMASK)) ) {
		if (UPD_LOG_AVAIL(LOG_NOTICE)) {
			strncpy(upd_funcname, table[ti].fullname, sizeof(upd_funcname)-1);
			upd_funcname[sizeof(upd_funcname)-1] = '\0';
			if (fkey != KEY_NONE) {
				CMD_LOG(LOG_NOTICE, "command: run ti=%d name=[%s] key=0x%02x args=[%s]",
					ti, table[ti].name, fkey, args_inbuff);
			} else {
				CMD_LOG(LOG_NOTICE, "command: run ti=%d name=[%s] args=[%s]",
					ti, table[ti].name, args_inbuff);
			}
		}

		if (table[ti].tflag & TSTAT_ARGS) {
			if (cnf.gstat & GSTAT_RECORD)
				record(table[ti].fullname, args_inbuff);
			exec = (table[ti].funcptr)(args_inbuff);
		} else {
			if (cnf.gstat & GSTAT_RECORD)
				record(table[ti].fullname, "");
			exec = ((FUNCP0) table[ti].funcptr)();
		}
		if (exec) {
			CMD_LOG(LOG_ERR, "command: last (ti=%d) [%s] returned: %d",
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
	int last_ri = -1, ring_siz = 0;

	wclear (stdscr);

	clhistory_push("not used", 8);
	if (!cnf.noconfig) {
		load_clhistory ();
	}
	reset_cmdline();
	memset(args_buff, 0, CMDLINESIZE);

	/* optional automacro run */
	if (cnf.ring_size > 0 && cnf.automacro[0] != '\0') {
		int autolength;
		autolength = strlen(cnf.automacro);
		ti = parse_cmdline (cnf.automacro, autolength, args_buff);
		if (ti >= TLEN && ti < TLEN+MLEN) {
			mi = ti - TLEN;
			run_macro_command (mi, args_buff);
		} else {
			// macro, stored in cnf.automacro, does not exist
			drop_all();
		}
		if (cnf.ring_size > 0) {
			cnf.automacro[0] = '\0';
		} /* else: engine test */
	}

	upd_event = 0;
	upd_funcname[0] = '\0';
	while (cnf.ring_size > 0)
	{

		/*
		 * regular screen update (REFRESH_EVENT and GSTAT_REDRAW used to force update)
		 */
		if (ch != ERR) {
			if (ch == REFRESH_EVENT) //local trigger
				cnf.gstat |= GSTAT_REDRAW; //external trigger
			if (cnf.gstat & GSTAT_TYPING) {
				upd_status_typing_tutor ();
			} else {
				upd_statusline (); //force update with GSTAT_REDRAW flag
			}
			/* update terminal title and/or tab header only if necessary */
			if (last_ri != cnf.ring_curr || ring_siz != cnf.ring_size || ch == REFRESH_EVENT || (cnf.gstat & GSTAT_REDRAW)) {
				last_ri = cnf.ring_curr;
				if (cnf.gstat & GSTAT_AUTOTITLE) {
					upd_termtitle();
				}
				if (cnf.gstat & GSTAT_TABHEAD) {
					show_tabheader();
				}
				ring_siz = cnf.ring_size;
			}
			if (cnf.trace > 0) {
				UPD_LOG(LOG_INFO, "page with trace [%d %s]", upd_event, upd_funcname);
				if (CURR_FILE.focus < cnf.trace+1)
					CURR_FILE.focus = cnf.trace+1;
				upd_text_area (0);
				upd_trace ();
				clear_trace_next_time = 1;
			} else if (clear_trace_next_time) {
				UPD_LOG(LOG_INFO, "page after trace [%d %s]", upd_event, upd_funcname);
				upd_text_area (0);
				clear_trace_next_time = 0;
			} else {
				if (!(cnf.gstat & GSTAT_UPDNONE)) {
					if (UPD_LOG_AVAIL(LOG_NOTICE)) {
						if (cnf.gstat & GSTAT_UPDFOCUS) {
							UPD_LOG(LOG_DEBUG, "focus [%d %s]", upd_event, upd_funcname);
						} else if (upd_event < 16) {
							UPD_LOG(LOG_NOTICE, "page [%d %s]", upd_event, upd_funcname);
						} else {
							UPD_LOG(LOG_NOTICE, "page [%d]", upd_event);
						}
					}
					upd_text_area (cnf.gstat & GSTAT_UPDFOCUS);
				}
			}
			cnf.gstat &= ~(GSTAT_UPDNONE | GSTAT_UPDFOCUS);
			cnf.gstat &= ~GSTAT_REDRAW;
			upd_cmdline ();

			doupdate ();
			delay_cnt_4stat = 0;
		}

		/*
		 * wait for input, do cursor reposition
		 */
		if (CURR_FILE.fflag & FSTAT_CMD) {
			wmove (stdscr, cnf.maxy-1, cnf.clpos-cnf.cloff);
		} else {
			wmove (stdscr, cnf.head + CURR_FILE.focus, cnf.pref + CURR_FILE.curpos-CURR_FILE.lnoff);
		}
		ch = key_handler (cnf.seq_tree, 0);
		upd_event = 0;
		upd_funcname[0] = '\0';

		/* window resize */
		if (ch == KEY_RESIZE) {
			(void) getmaxyx_and_offset_sanity();
			ch = REFRESH_EVENT;
			upd_event = 32; //resize
			continue;
		}

		/* optional mouse positioning support, loop up */
		if (ch == KEY_MOUSE) {
			if (!set_position_by_pointer(mouse_event_pointer)) {
				cnf.gstat |= GSTAT_UPDFOCUS;
				CURR_FILE.fflag &= ~FSTAT_CMD;
			} else {
				ch = ERR;
			}
			upd_event = 64; //mouse
			continue;
		}

		/* idle time slots, used for background processes */
		if (ch == ERR) {
			delay_cnt_4stat++;
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
			upd_event = 128; //background tasks
			continue;
		}
		ret = 0;

		/*
		 * input processing
		 */
		if (CURR_FILE.fflag & FSTAT_CMD) {
			/* cmdline intern editing, (ret 0/1) */
			ret = ed_cmdline (ch);		/* 0/1 */
			upd_event = ret;

			if (ret && ch == KEY_RETURN) {
				if (cnf.cmdline_len == 7 && !strncmp(cnf.cmdline_buff, "errdump", 7)) {
					errdump(); /* no history */
					reset_cmdline();
					continue;
				}
				/* save buffer to cmdline history */
				cmdline_to_clhistory (cnf.cmdline_buff, cnf.cmdline_len);
				//fix: strip_blanks
				cnf.cmdline_len = strlen(cnf.cmdline_buff);

				if (cnf.cmdline_len > 0) {
					ti = parse_cmdline (cnf.cmdline_buff, cnf.cmdline_len, args_buff);
					if (ti >= TLEN && ti < TLEN+MLEN) {
						mi = ti - TLEN;
						run_macro_command (mi, args_buff);
					} else if (ti >= 0 && ti < TLEN) {
						run_command (ti, args_buff, KEY_NONE);
					} else {
						/* warning */
						cnf.gstat |= GSTAT_UPDNONE;
						tracemsg("unknown command [%s]", cnf.cmdline_buff);
					}
				} else {
					cnf.gstat |= GSTAT_UPDNONE;
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
			upd_event = ret;

		} else if (cnf.gstat & GSTAT_TYPING) {
			typing_tutor_handler(ch);
			ret = 16;	/* key handled */
			upd_event = ret;

		} else {
			/* handle text-special cases here (ret 0/2) */
			ret = ed_text (ch);		/* 0/2 */
			upd_event = ret;
			if (UPD_LOG_AVAIL(LOG_NOTICE)) {
				if (upd_event && (upd_funcname[0] == '\0')) {
					int ki;
					ki = index_key_value(ch);
					if (ki >= 0 && ki < KLEN)
						snprintf (upd_funcname, sizeof(upd_funcname)-1,
							"0x%04x %s", ch, keys[ki].key_string);
				}
			}
		}

		if (!ret && (cnf.ring_size > 0)) {
			/* the common key-handlings (ret 0/4) */
			ret = ed_common (ch);		/* 0/4 */
			upd_event = ret;

			/* warning */
			if (ret == 0) {
				cnf.gstat |= GSTAT_UPDNONE;
				if ((ch & KEY_META) && (ch & ~KEY_META) > 0x20 && (ch & ~KEY_META) < 0x7f) {
					tracemsg("unbound key (Alt-%c)", (ch & ~KEY_META));
				} else {
					tracemsg("unbound key 0x%02X", ch);
				}
			}
			if (UPD_LOG_AVAIL(LOG_NOTICE)) {
				if (upd_event && (upd_funcname[0] == '\0')) {
					int ki;
					ki = index_key_value(ch);
					if (ki >= 0 && ki < KLEN)
						snprintf (upd_funcname, sizeof(upd_funcname)-1,
							"0x%04x %s", ch, keys[ki].key_string);
				}
			}
		}

	} /* while */
	if (!cnf.noconfig) {
		save_clhistory ();
	}

	return;
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
		abeg = 1;
	} else if (ibuff[0] == '/') {
		/* 0x2f search() */
		clen = 1; /* command and the first argument also, do not skip! */
		abeg = 0;
	} else {
		for (i=0; i<ilen; i++) {
			if (ibuff[i] == ' ' || ibuff[i] == '\t')
				break;
		}
		clen = i;
		for (i=clen; i<ilen; i++) {
			if (ibuff[i] != ' ' && ibuff[i] != '\t')
				break;
		}
		abeg = i;
	}

	/* search down command name in macros[].name and table[].name
	*/
	for (xi=0; xi < MLEN; xi++)
	{
		if (macros[xi].name[0] != '\0') {
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
	}

	return (xi);
} /* parse_cmdline() */

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

/*
* search down fkey in macros[].fkey, return index
*/
int
index_macros_fkey (int fkey)
{
	int mi;
	for (mi=0; mi < MLEN; mi++) {
		if (macros[mi].fkey == fkey) {
			break;
		}
	}
	return (mi);
}

static void errdump(void)
{
	unsigned i, j;
	if (cnf.ie > 0) {
#ifndef DEVELOPMENT_VERSION
	openlog("eda", LOG_PID, LOG_USER);
#endif
		for(i=0; i < cnf.errsiz; i++) {
			j = (i+cnf.ie) % cnf.errsiz;
			if (cnf.errlog[j])
				syslog(LOG_ERR, "0x%04X", cnf.errlog[j]);
			cnf.errlog[j] = 0;
		}
		cnf.ie = 0;
#ifndef DEVELOPMENT_VERSION
	closelog();
#endif
	}
}
