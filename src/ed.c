/*
* ed.c
* editor and main event handler, command parser and run, external macro run, command table tools
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
static int run_macro_command (int mi, char *cmdline_buffer);
static int run_command (int ti, const char *cmdline_buffer);
static void event_handler (void);
static int parse_cmdline (char *ibuff, int ilen);

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

	init_colors ();

	/* keys */
	keypad (cnf.wtext, TRUE);
	keypad (cnf.wbase, TRUE);
	/* timeouts */
	ESCDELAY = CUST_ESCDELAY;
	wtimeout (cnf.wtext, CUST_WTIMEOUT);
	wtimeout (cnf.wbase, CUST_WTIMEOUT);
	/*
	 * do not use signal handler on SIGWINCH because this inhibits KEY_RESIZE
	 * and cannot delay the action; workaround malloc problems in library
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
	getmaxyx (stdscr, cnf.maxy, cnf.maxx);

	setbegyx (cnf.wstatus, 0, 0);
	setbegyx (cnf.wtext, 1, 0);
	setbegyx (cnf.wbase, cnf.maxy - 1, 0);

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

static int
run_macro_command (int mi, char *cmdline_buffer)
{
	int ii=0, ix=0, iy=0, jj=0, kk=0, xx=0, args_cnt=0, exec=0;
	char dup_buffer[CMDLINESIZE];
	char *args[MAXARGS];
	char *mptr;
	char args_ready[CMDLINESIZE];

	if ( !(macros[mi].mflag & (CURR_FILE.fflag & FSTAT_CHMASK)) ) {
		strncpy(dup_buffer, cmdline_buffer, CMDLINESIZE);
		dup_buffer[CMDLINESIZE-1] = '\0';
		args_cnt = parse_args (dup_buffer, args);

		CMD_LOG(LOG_DEBUG, "macro: run mi=%d name=[%s] args=[%s] cnt=%d",
			mi, macros[mi].name, cmdline_buffer, args_cnt);

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
							// copy cmdline_buffer
							for (xx=0; cmdline_buffer[xx] != '\0' && kk < CMDLINESIZE-1; xx++) {
								args_ready[kk++] = cmdline_buffer[xx];
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
				CMD_LOG(LOG_WARNING, "macro: last (ti=%d) [%s] returned: %d",
					ix, table[ix].fullname, exec);
				break;	/* stop macro */
			}
		}/* macros[mi].maclist[] */
		cnf.gstat &= ~GSTAT_SILENT;

		/* force screen update */
		cnf.gstat &= ~(GSTAT_UPDNONE | GSTAT_UPDFOCUS);
	}/* change allowed */

	return (exec);
}

static int
run_command (int ti, const char *cmdline_buffer)
{
	int exec = 0;

	if ( !(table[ti].tflag & (CURR_FILE.fflag & FSTAT_CHMASK)) ) {
		if (table[ti].tflag & TSTAT_ARGS) {
			REC_LOG(LOG_DEBUG, "command %d [%s] [%s]", ti, table[ti].fullname, cmdline_buffer);
			if (cnf.gstat & GSTAT_RECORD)
				record(table[ti].fullname, cmdline_buffer);
			exec = (table[ti].funcptr)(cmdline_buffer);
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

	wclear (cnf.wstatus);
	wclear (cnf.wbase);
	wclear (cnf.wtext);

	clhistory_push("not used", 8);
	load_clhistory ();
	reset_cmdline();

	/* optional automacro run */
	if (cnf.ring_size > 0 && cnf.automacro[0] != '\0') {
		int autolength;
		autolength = strlen(cnf.automacro);
		ti = parse_cmdline (cnf.automacro, autolength);
		if (ti >= TLEN && ti < TLEN+MLEN) {
			mi = ti - TLEN;
			run_macro_command (mi, cnf.automacro);
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

		/*
		 * regular screen update (REFRESH_EVENT used to force update)
		 */
		if (ch != ERR) {
			upd_statusline ();
			if (cnf.trace > 0) {
				MAIN_LOG(LOG_DEBUG, "full update with trace in this cycle");
				if (CURR_FILE.focus < cnf.trace)
					CURR_FILE.focus = cnf.trace;
				upd_text_area (0);
				upd_trace ();
				clear_trace_next_time = 1;
			} else if (clear_trace_next_time) {
				MAIN_LOG(LOG_DEBUG, "full update now, trace in previous cycle");
				upd_text_area (0);
				clear_trace_next_time = 0;
			} else {
				if (!(cnf.gstat & GSTAT_UPDNONE)) {
					if (cnf.gstat & GSTAT_UPDFOCUS) {
						MAIN_LOG(LOG_DEBUG, "update focus line only");
					} else {
						MAIN_LOG(LOG_DEBUG, "update full page");
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
					/* parse command line and exec command if found
					* and buffer is not read/only
					*/
					ti = parse_cmdline (cnf.cmdline_buff, cnf.cmdline_len);
					if (ti >= TLEN && ti < TLEN+MLEN) {
						mi = ti - TLEN;
						run_macro_command (mi, cnf.cmdline_buff);
					} else if (ti >= 0 && ti < TLEN) {
						run_command (ti, cnf.cmdline_buff);
					} else {
						tracemsg("unknown command");
					}
				}

				/* reset reused workbench after parse and run */
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

			/* warning, if the key was not handled */
			if (ret == 0) {
				tracemsg("unhandled key 0x%02X", ch);
			}
		}

	} /* while */
	save_clhistory ();

	clhistory_cleanup(0);
	FREE(cnf.clhistory);
	cnf.clhistory = NULL;

} /* event_handler() */

#define EXTSEP(sep)	((sep)==' ' || (sep)=='\t' || (sep)=='/' || (sep)=='\'' || (sep)=='\"')

/*
* cmdline parser
* cmd[\x20\x09/'"!]+args
* cmd is mandatory, args is optional,
* argument separators: space, tab, '/', '\'', '\"'
* return: ti (table index) and the arguments in ibuff[]
* ibuff[] is maybe empty string but never NULL
*/
static int
parse_cmdline (char *ibuff, int ilen)
{
	char *cmd, *args, delim[3], ext;
	int i, j, ti = (-1), clen = 0;

	/* separate command and args list */
	delim[0] = ibuff[0];
	delim[1] = '\0';
	cmd = NULL;
	args = NULL;
	clen = 0;

	if (delim[0] == ':' && ibuff[1] < 'A') {
		/* 0x3a goto_line() */
		cmd = delim;
		args = ibuff+1;	/* good for both ":12" and ": 12" */
		clen = 1;
	} else if (delim[0] == '|' && ibuff[1] == '|') {
		/* 0x7c filter_shadow_cmd() */
		delim[1] = ibuff[1];
		delim[2] = '\0';
		cmd = delim;
		args = ibuff+2;	/* good for both "||a2ps -1 -f8" and "|| a2ps -1 -f8" */
		clen = 2;
	} else if (delim[0] == '|') {
		/* 0x7c filter_cmd() */
		cmd = delim;
		args = ibuff+1;	/* good for both "|sort" and "| wc -l" */
		clen = 1;
	} else if (delim[0] == '/') {
		/* 0x2f search() */
		/* '/' is the command and (maybe) the first argument also */
		cmd = delim;
		args = ibuff;	/* reuse ibuff as args list */
		clen = 1;
	} else {
		/* strip_blanks(3,...) already done
		* we need whitechar(s) after the command,
		* the rest of characters will construct the args list
		*/
		cmd = ibuff;
		args = ibuff;
		/* command in cmd[]: close with '\0' */
		for (i=0; i<ilen; i++) {
			if (EXTSEP(ibuff[i]))
				break;
		}
		clen = i;
		ext = ibuff[i];
		ibuff[i++] = '\0';
		/* args list in args[]: start after whitechar(s) or no args */
		args = &ibuff[i];
		if (i<ilen && (EXTSEP(ext))) {
			/* shift out bytes and restore ext */
			for (j=ilen-1; j>=i; j--) {
				ibuff[j+1] = ibuff[j];
			}
			ibuff[++ilen] = '\0';
			ibuff[i] = ext;
		}
		for (; i<ilen && (ibuff[i]==' ' || ibuff[i]=='\t'); i++, args++)
			;
		if (i>=ilen || *args == '\0')
			args = NULL;
	}

	/* search down command name in macros[].name and table[].name, ...
	* not a HASH function "name -> index" but simple and pretty good
	*/
	for (ti=0; ti < MLEN; ti++)
	{
		if (macros[ti].name[0] != '\0') {
			for (i=0; i < clen; i++) {
				if (macros[ti].name[i] != cmd[i])
					break;
			}
			if (macros[ti].name[i] == '\0' && cmd[i] == '\0')
				break;
		}
	}
	if (ti < MLEN) {
		ti += TLEN;
	} else {
		for (ti=0; ti < TLEN; ti++)
		{
			if (table[ti].minlen >= 1 && table[ti].minlen <= clen) {
				for (i=0; i < clen; i++) {
					if (table[ti].name[i] != cmd[i])
						break;
				}
				if (i == clen)	/* match: ti is the index */
					break;
			}
		}
		if (ti == TLEN) {
			ti += MLEN;
		}
	}

	/* arguments */
	if (ti == TLEN+MLEN) {
		ibuff[0] = '\0';
	} else if (args == NULL) {
		ibuff[0] = '\0';
	} else {
		CMD_LOG(LOG_INFO, "parser: cmd [%s] args [%s]", cmd, args);
		/* copy args list back */
		if (args > ibuff) {
			for (i=0; i<ilen && args[i]!='\0'; i++) {
				ibuff[i] = args[i];
			}
			ibuff[i] = '\0';
		}
		/* else: args reused ibuff */
	}

	return (ti);
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
