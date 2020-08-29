/*
* cmd.c
* basic editing command set, command history, event handler functios: ed_cmdline, ed_text, ed_common
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
#include <time.h>
#include <syslog.h>
#include "curses_ext.h"
#include "main.h"
#include "proto.h"

/* global config */
extern CONFIG cnf;

/* from ed.c */
extern TABLE table[];
extern const int TLEN;
extern MACROS *macros;
extern int MLEN;
extern KEYS keys[];
extern const int KLEN;

/* local proto */
static void reset_clhistory(void);
static CMDLINE *clhistory_unlink (CMDLINE *runner);
static void clhistory_rm_olddup (char *buff, int len, int depth);
static void go2end_cmdline(void);

/* come back to the most recent item
*/
static void
reset_clhistory(void)
{
	while (cnf.clhistory != NULL && cnf.clhistory->next != NULL)
		cnf.clhistory = cnf.clhistory->next;
	cnf.reset_clhistory = 0;
}

/*
** switch_text_cmd - switch between text area and command line
*/
int
switch_text_cmd (void)
{
	CURR_FILE.fflag ^= FSTAT_CMD;
	cnf.gstat |= GSTAT_UPDNONE;
	return 0;
}

/*
** go_text - move cursor to the text area, useful for for macros
*/
int
go_text (void)
{
	CURR_FILE.fflag &= ~FSTAT_CMD;
	cnf.gstat |= GSTAT_UPDNONE;
	return (0);
}

/*
** message - display a string as "message", mostly for macros
*/
int
message (const char *str)
{
	int silent;
	silent = (cnf.gstat & GSTAT_SILENCE);

	if (str && strlen(str) > 0) {
		/* macros should be silent, but this call is intentional */
		if (silent)
			cnf.gstat &= ~GSTAT_SILENCE;

		tracemsg("%s", str);

		if (silent)
			cnf.gstat |= GSTAT_SILENCE;
	}

	return 0;
}

/*
** msg_from_text - display text from the current line as message, mostly for macros
*/
int
msg_from_text (void)
{
	char row[CMDLINESIZE];
	int i;

	for (i=0; i < CURR_LINE->llen-1 && i < CMDLINESIZE-1; i++) {
		row[i] = CURR_LINE->buff[i];
	}
	row[i] = '\0';

	message (row);

	return 0;
}

/* push string down in command line history
*/
int
clhistory_push (const char *buff, int len)
{
	CMDLINE *ptr=NULL;

	if (len < 1) {
		return 1;
	}
	ptr = (CMDLINE *) MALLOC(sizeof(CMDLINE));
	if (ptr == NULL) {
		ERRLOG(0xE036);
		return 1;
	}

	cnf.clhist_size++;
	if (cnf.clhistory == NULL) {
		ptr->prev = NULL;
		ptr->next = NULL;
		ptr->len = 0;
		ptr->buff[ptr->len] = '\0';
		cnf.clhistory = ptr;
		// list initiated

	} else {
		/* linkage */
		ptr->prev = cnf.clhistory->prev;
		cnf.clhistory->prev = ptr;
		ptr->next = cnf.clhistory;
		if (ptr->prev != NULL)
			(ptr->prev)->next = ptr;

		/* copy */
		ptr->len = MIN(CMDLINESIZE-1, len);
		strncpy(ptr->buff, buff, (size_t)ptr->len);
		ptr->buff[ptr->len] = '\0';

		// command added
	}

	return 0;
}

/* remove one item from the command line history
*/
static CMDLINE *
clhistory_unlink (CMDLINE *runner)
{
	CMDLINE *xbase;

	if (runner == NULL) {
		return NULL;

	} else if (runner->next != NULL) {
		xbase = runner->next;
		xbase->prev = runner->prev;
		if (xbase->prev != NULL)
			(xbase->prev)->next = xbase;

	} else if (runner->prev != NULL) {
		/* up, since cannot go down */
		xbase = runner->prev;
		if (xbase != NULL) {
			xbase->next = NULL;
		}

	} else {
		/* nothing to unlink */
		xbase = NULL;
	}

	FREE(runner);
	runner = NULL;
	if (cnf.clhist_size > 0) cnf.clhist_size--;

	return (xbase);
}

/* remove old items from command line history, keep most recent,
* but do not destroy the linked list, keep=0 will keep one
*/
int
clhistory_cleanup (int keep)
{
	CMDLINE *runner, *prev;
	int item=0;

	reset_clhistory();

	runner = cnf.clhistory;
	while (++item < keep && runner != NULL && runner->prev != NULL) {
		runner = runner->prev;
	}

	while (runner != NULL) {
		if (runner->prev == NULL) {
			break;
		} else {
			/* unlink nodes from linked list, but only prev */
			prev = runner->prev;
			clhistory_unlink(prev);
		}
	}

	if (runner != NULL) {
		return (0);
	} else {
		return (1);
	}
}

/* check current item with some olders, and remove duplicates
*/
static void
clhistory_rm_olddup (char *buff, int len, int depth)
{
	CMDLINE *runner;
	int item=0;

	if (len > 0 && cnf.clhistory != NULL) {
		runner = cnf.clhistory->prev;
		while (runner != NULL && item < depth) {
			if (runner->len == len) {
				/* zero-length items should not be here */
				if (strncmp(runner->buff, buff, (size_t)len) == 0) {
					clhistory_unlink(runner);
					break;
				}
			}
			runner = runner->prev;
			item++;
		}
	}
	return;
}

/*
** clhistory_prev - copy the previous item from history to command line,
**	with prefix filtering, prefix of the original line
*/
int
clhistory_prev (void)
{
	int prefixlength = 0;
	CMDLINE *try = NULL;

	CURR_FILE.fflag |= FSTAT_CMD;
	cnf.gstat |= GSTAT_UPDNONE;

	if (cnf.clhistory == NULL || cnf.clhistory->prev == NULL) {
		return 0;
	}

	if (cnf.reset_clhistory) {
		reset_clhistory();
	}

	if (cnf.clhistory->next == NULL) {
		/* something special for prefix filter? */
		;;;

		/* copy to "workbuffer"
		*/
		if (cnf.cmdline_len > 0) {
			cnf.clhistory->len = MIN(cnf.cmdline_len, CMDLINESIZE-1);
			strncpy(cnf.clhistory->buff, cnf.cmdline_buff, (size_t)cnf.clhistory->len);
			cnf.clhistory->buff[cnf.clhistory->len] = '\0';
		} else {
			cnf.clhistory->len = 0;
			cnf.clhistory->buff[cnf.clhistory->len] = '\0';
		}
	}

	/* skip to prev... with prefix filtering
	*/
	if (cnf.clhistory->len > 0 && cnf.clpos > 0 && cnf.cmdline_len > 0) {
		prefixlength = MIN(cnf.clpos,cnf.cmdline_len);
		prefixlength = MIN(prefixlength,7);
		try = cnf.clhistory->prev;
		while (prefixlength >= 0 && try != NULL) {
			if ((try->len >= prefixlength) &&
			    (strncmp(try->buff, cnf.cmdline_buff, (size_t)prefixlength) == 0))
			{
				cnf.clhistory = try;
				break;
			}
			try = try->prev;	/* maybe NULL now */
		}
	} else {
		/* no filter */
		if (cnf.clhistory->prev != NULL) {
			cnf.clhistory = cnf.clhistory->prev;
		}
	}

	if (cnf.clhistory->len > 0) {
		cnf.cmdline_len = MIN(cnf.clhistory->len, CMDLINESIZE-1);
		strncpy(cnf.cmdline_buff, cnf.clhistory->buff, (size_t)cnf.cmdline_len);
		cnf.cmdline_buff[cnf.cmdline_len] = '\0';
	} else {
		cnf.cmdline_len = 0;
		cnf.cmdline_buff[cnf.cmdline_len] = '\0';
	}

	cnf.clpos = MIN(cnf.clpos, cnf.cmdline_len);
	if (cnf.clpos - cnf.cloff > cnf.maxx-1)
		cnf.cloff = cnf.clpos - cnf.maxx + 1;
	else if (cnf.cloff > cnf.clpos)
		cnf.cloff = cnf.clpos + 1;

	HIST_LOG(LOG_NOTICE, "---done, cmdline buffer [%s]", cnf.cmdline_buff); /* testing */
	return 0;
}

/*
** clhistory_next - copy the next item (towards present) from history to command line,
**	with prefix filtering, prefix of the original line
*/
int
clhistory_next (void)
{
	int prefixlength = 0;
	CMDLINE *try = NULL;

	CURR_FILE.fflag |= FSTAT_CMD;
	cnf.gstat |= GSTAT_UPDNONE;

	if (cnf.clhistory == NULL || cnf.clhistory->next == NULL) {
		return 0;
	}

	if (cnf.reset_clhistory) {
		reset_clhistory();
	}

	/* skip to next... with prefix filtering
	*/
	if (cnf.clhistory->len > 0 && cnf.clpos > 0 && cnf.cmdline_len > 0) {
		prefixlength = MIN(cnf.clpos,cnf.cmdline_len);
		prefixlength = MIN(prefixlength,7);
		try = cnf.clhistory->next;
		while (prefixlength >= 0 && try != NULL) {
			if ((try->len >= prefixlength) &&
			    (strncmp(try->buff, cnf.cmdline_buff, (size_t)prefixlength) == 0))
			{
				cnf.clhistory = try;
				break;
			} else if (try->next == NULL) {
				cnf.clhistory = try;	/* leave, this is the "workbuffer" */
				break;
			}
			try = try->next;
		}
	} else {
		/* no filter -- do not segfault */
		if (cnf.clhistory->next != NULL) {
			cnf.clhistory = cnf.clhistory->next;
		}
	}

	if (cnf.clhistory->len > 0) {
		cnf.cmdline_len = MIN(cnf.clhistory->len, CMDLINESIZE-1);
		strncpy(cnf.cmdline_buff, cnf.clhistory->buff, (size_t)cnf.cmdline_len);
		cnf.cmdline_buff[cnf.cmdline_len] = '\0';
	} else {
		cnf.cmdline_len = 0;
		cnf.cmdline_buff[cnf.cmdline_len] = '\0';
	}

	cnf.clpos = MIN(cnf.clpos, cnf.cmdline_len);
	if (cnf.clpos - cnf.cloff > cnf.maxx-1)
		cnf.cloff = cnf.clpos - cnf.maxx + 1;
	else if (cnf.cloff > cnf.clpos)
		cnf.cloff = cnf.clpos + 1;

	HIST_LOG(LOG_NOTICE, "+++done, cmdline buffer [%s]", cnf.cmdline_buff); /* testing */
	return 0;
}

/* set position to length and set the correct offset when the position is after the end
*/
static void
go2end_cmdline(void)
{
	cnf.clpos = cnf.cmdline_len;	/* after last */
	if (cnf.clpos > cnf.maxx-1)
		cnf.cloff = cnf.clpos - cnf.maxx + 1;
	else
		cnf.cloff = 0;
}

/* clear command line and set first position
*/
void
reset_cmdline()
{
	cnf.clpos = 0;
	cnf.cloff = 0;
	cnf.cmdline_buff[cnf.clpos] = '\0';
	cnf.cmdline_len = cnf.clpos;
}

/*
* ed_cmdline -- edit command line; base inline editing here and
*	jump to current text area, filename globbing, clhistory browse with prefix filter
*	the cmdline history is saved outside (after RETURN)
* return:
*	0: key not relevant here (to be processed in ed_common)
*	1: action done
*/
int
ed_cmdline (int ch)
{
	int ret=1;
	int ii, ix;
	char *choices = NULL;
	char insert[20]; /* put stack protector to silence */

	switch (ch)
	{
	case KEY_C_UP:
		clhistory_prev();
		break;
	case KEY_C_DOWN:
		clhistory_next();
		break;

	/* switch to text area */
	case KEY_DOWN:
		go_down();
		break;
	case KEY_UP:
		go_up();
		break;

	default:
		/* command line editing */

		/* optimize text area refresh */
		cnf.gstat |= GSTAT_UPDNONE;

		switch (ch)
		{
		case KEY_BACKSPACE:
			if (cnf.clpos > 0) {
				if (cnf.clpos > cnf.cmdline_len) {
					go2end_cmdline();
				} else {
					for (ii = cnf.clpos-1; ii+1 <= cnf.cmdline_len; ii++)
						cnf.cmdline_buff[ii] = cnf.cmdline_buff[ii+1];
					cnf.clpos--;
					cnf.cmdline_len--;
					cnf.cmdline_buff[cnf.cmdline_len] = '\0';
					/* skip to left (stepwise) */
					if (cnf.cloff > cnf.clpos)
						cnf.cloff = (cnf.clpos >= 4) ? (cnf.clpos - 4) : 0;
				}
				cnf.reset_clhistory = 1;
			}
			break;
		case KEY_DC:
			if (cnf.clpos < cnf.cmdline_len) {
				for (ii = cnf.clpos; ii+1 <= cnf.cmdline_len; ii++)
					cnf.cmdline_buff[ii] = cnf.cmdline_buff[ii+1];
				cnf.cmdline_len--;
				cnf.cmdline_buff[cnf.cmdline_len] = '\0';
				cnf.reset_clhistory = 1;
			}
			break;
		case KEY_F6:
			if (cnf.clpos < cnf.cmdline_len) {
				cnf.cmdline_buff[cnf.clpos] = '\0';
				cnf.cmdline_len = cnf.clpos;
				cnf.reset_clhistory = 1;
			}
			break;

		/* moving around */
		case KEY_HOME:
			cnf.clpos = 0;
			cnf.cloff = 0;
			break;
		case KEY_END:
			go2end_cmdline();
			break;
		case KEY_LEFT:
			cnf.clpos = (cnf.clpos > 0) ? cnf.clpos-1 : 0;
			/* skip to left (stepwise) */
			if (cnf.cloff > cnf.clpos)
				cnf.cloff = (cnf.clpos >= 4) ? (cnf.clpos - 4) : 0;
			break;
		case KEY_RIGHT:
			/* do not move over the end */
			if (cnf.clpos < cnf.cmdline_len) {
				cnf.clpos = (cnf.clpos < CMDLINESIZE-1) ? cnf.clpos+1 : CMDLINESIZE-1;
				if (cnf.clpos - cnf.cloff > cnf.maxx-1)
					cnf.cloff = cnf.clpos - cnf.maxx + 1;
			}
			break;

		/* filename globbing */
		case KEY_TAB:
			if ( !(cnf.cmdline_len > 0 && cnf.clpos > 0) ) {
				break;
			}

			cnf.reset_clhistory = 1;

			/* filename globbing
			 * always, except ... where we can use regexp
			 */
			ix = 2;
			if ((strncmp(cnf.cmdline_buff, "all ", 4) == 0) ||
			    (strncmp(cnf.cmdline_buff, "less ", 5) == 0) ||
			    (strncmp(cnf.cmdline_buff, "more ", 5) == 0) ||
			    (strncmp(cnf.cmdline_buff, "tag ", 4) == 0) ||
			    (strncmp(cnf.cmdline_buff, "/", 1) == 0) ||
			    (strncmp(cnf.cmdline_buff, "ch ", 3) == 0))
			{
				ix = 0;
			} else if (cnf.cmdline_buff[0] == '|' && cnf.cmdline_buff[1] == '|') {
				ix = 2;
			} else if (cnf.cmdline_buff[0] == '|') {
				ix = 1;
			}

			if ((ix > 0) &&
			    (cnf.cmdline_buff[cnf.clpos-1] != ' ') &&
			    (cnf.cmdline_buff[cnf.clpos] == ' ' || cnf.cmdline_buff[cnf.clpos] == '\0'))
			{
				/* ix points last word */
				for (ii = cnf.clpos-1; ii >= ix; ii--) {
					if (cnf.cmdline_buff[ii] == ' ') {
						ix = ii+1;
						break;
					}
				}

				/* characters after cursor will be lost */
				choices = NULL;
				if (CMDLINESIZE > ix) {
					if (!glob_tab_expansion(cnf.cmdline_buff+ix, (unsigned)(CMDLINESIZE-ix), &choices)) {
						cnf.cmdline_len = strlen(cnf.cmdline_buff);
						cnf.clpos = cnf.cmdline_len;	/* after last */
						if (cnf.clpos > cnf.maxx - 1)
							cnf.cloff = cnf.clpos - cnf.maxx + 10;
						if (choices != NULL) {
							tracemsg("%s", choices);	/* limited lines */
						}
					}
				}
				FREE(choices); choices = NULL;
			}
			/* insert TAB like normal character */
			else {
				insert[0] = '\t';
				insert[1] = '\0';
				if (type_cmd(insert))
					ret = 0;
			}
			break;

		case KEY_RETURN:
			/* parser ready */
			break;

		/*
		 * printable characters (int ch)
		 */
		default:
			if (ch >= 0x20 && ch != 0x7F && ch <= 0xFF) {
				insert[0] = (char) (ch & 0xff);
				insert[1] = '\0';
				if (type_cmd(insert))
					ret = 0;
			} else {
				ret=0;	/* key not handled here */
			}
			break;

		} /* switch */

		if (ret==0 || (ch==KEY_RETURN)) {
			/* refresh is important if the key is not handled here or command start required */
			cnf.gstat &= ~(GSTAT_UPDNONE | GSTAT_UPDFOCUS);
		}
	} /* switch */

	if (ch == KEY_RETURN) {
		CMD_LOG(LOG_NOTICE, "return: buff [%s] length (%d)", cnf.cmdline_buff, cnf.cmdline_len);
	}

	return (ret);
} /* ed_cmdline() */

/* save command line buffer to command line history, after strip and duplicate check
*/
void
cmdline_to_clhistory (char *buff, int length)
{
	int xlength = length;

	reset_clhistory();

	/* strip all blanks, even if this can cut ending of regexp pattern
	 */
	strip_blanks (STRIP_BLANKS_FROM_END|STRIP_BLANKS_FROM_BEGIN, buff, &xlength);

	if (xlength > 0) {
		clhistory_rm_olddup (buff, xlength, 5);
		clhistory_push (buff, xlength);
	}
}

/*
* ed_text - edit line in text area; base inline editing here and
*	with screen update optimization
* return:
*	0: not relevant (key not handled here)
*	2: action done
*/
int
ed_text (int ch)
{
	int ret=2;
	int o_lnoff = CURR_FILE.lnoff;
	int o_lncol = CURR_FILE.lncol;
	int o_llen  = CURR_LINE->llen;
	char insert[20]; /* put stack protector to silence */

	switch (ch)
	{

	/* moving around */
	case KEY_UP:
		go_up();
		break;
	case KEY_DOWN:
		go_down();
		break;
	case KEY_HOME:
		go_smarthome();
		if (o_lnoff == CURR_FILE.lnoff)
			cnf.gstat |= GSTAT_UPDNONE;
		break;
	case KEY_END:
		go_end();
		if (o_lnoff == CURR_FILE.lnoff)
			cnf.gstat |= GSTAT_UPDNONE;
		break;
	case KEY_LEFT:
		go_left();
		if (o_lnoff == CURR_FILE.lnoff)
			cnf.gstat |= GSTAT_UPDNONE;
		break;
	case KEY_RIGHT:
		go_right();
		if (o_lnoff == CURR_FILE.lnoff)
			cnf.gstat |= GSTAT_UPDNONE;
		break;

	/* delete, split/join, add new */
	case KEY_BACKSPACE:
		delback_char();
		if (o_lncol > 0 && o_lncol < o_llen && o_lnoff == CURR_FILE.lnoff)
			cnf.gstat |= GSTAT_UPDFOCUS;
		break;
	case KEY_DC:
		delete_char();
		if (o_lncol < o_llen-1 && o_lnoff == CURR_FILE.lnoff)
			cnf.gstat |= GSTAT_UPDFOCUS;
		break;
	case KEY_RETURN:
		keypress_enter();
		break;

	case KEY_TAB:
		if ( !(CURR_FILE.fflag & FSTAT_NOEDIT) ) {
			insert[0] = '\t';
			insert[1] = '\0';
			if (insert_chars (insert, 1)) {
				ret = 0;
			} else if (o_lnoff == CURR_FILE.lnoff) {
				cnf.gstat |= GSTAT_UPDFOCUS;
			}
		} else {
			ret=0;
		}
		break;
	/*
	 * printable characters (int ch)
	 */
	default:
		if (ch >= 0x20 && ch != 0x7F && ch <= 0xFF) {
			if (!TEXT_LINE(CURR_LINE)) { /* top or bottom */
				cnf.gstat |= GSTAT_UPDNONE;
				/* push character to comandline */
				CURR_FILE.fflag |= FSTAT_CMD;
				reset_cmdline();
				ret = (ed_cmdline(ch) == 0) ? 0 : 2;
			} else if ( !(CURR_FILE.fflag & FSTAT_NOEDIT) ) {
				insert[0] = (char) (ch & 0xff);
				insert[1] = '\0';
				if (insert_chars (insert, 1)) {
					ret = 0;
				} else if (o_lnoff == CURR_FILE.lnoff) {
					cnf.gstat |= GSTAT_UPDFOCUS;
				}
			} else if (CURR_FILE.fflag & FSTAT_SPECW) { /* even if specw is always noedit */
				cnf.gstat |= GSTAT_UPDNONE;
				/* push character to comandline */
				CURR_FILE.fflag |= FSTAT_CMD;
				reset_cmdline();
				ret = (ed_cmdline(ch) == 0) ? 0 : 2;
			} else {
				ret = 0;
			}
		} else {
			ret=0;	/* key not handled here */
		}
		break;
	} /* switch */

	return (ret);
} /* ed_text() */

/*
** typing_tutor - start typing tutor on current file,
**	the file shall be prepared, no tabs except indentation,
**	line ends without space and text reformatted between margins
*/
int
typing_tutor(void)
{
	if ((CURR_FILE.fflag & FSTAT_SPECW)) {
		tracemsg("cannot start typing tutor -- special buffer");
		return (1);
	}

	CURR_FILE.fflag &= ~FSTAT_CMD; /* force text area */
	if (CURR_LINE->lflag & LSTAT_TOP) {
		go_home();
		go_down();
	}
	if (!TEXT_LINE(CURR_LINE)) {
		tracemsg("cannot start typing tutor -- no text lines");
		return (1);
	}

	center_focusline();
	cnf.tutor_pass = cnf.tutor_fail = 0;
	cnf.tutor_start = time(NULL);
	cnf.gstat |= GSTAT_TYPING;
	tracemsg("let's start");

	return (0);
}

/*
* typing_tutor_handler - typing tutor input controller
*/
int
typing_tutor_handler(int ch)
{
	int ret=16;
	int all, etime;
	float r, f, g;
	char score[100];
	int linecnt=0, wordcnt=0, charcnt=0;
	int fch = CURR_LINE->buff[CURR_FILE.lncol];

	if (KEY_ESC == ch || KEY_F4 == ch || KEY_F8 == ch) {
		/* leave */
		cnf.gstat &= ~GSTAT_TYPING;
	} else if (CURR_FILE.lncol >= CURR_LINE->llen-1) {
		if (KEY_RETURN != ch) {
			beep();
			cnf.tutor_fail++;
		} else {
			cnf.tutor_pass++;
		}
		go_right();
	} else if (fch >= 0x20 && fch < 0x7f) {
		if (ch != fch) {
			beep();
			cnf.tutor_fail++;
			//tracemsg(" [%c] is wrong, type [%c]", ch, fch);
		} else {
			cnf.tutor_pass++;
			go_right();
		}
		cnf.gstat |= GSTAT_UPDFOCUS;
	} else {
		// fch is non-ascii -- do not count, just move
		go_right();
		cnf.gstat |= GSTAT_UPDFOCUS;
	}

	if (CURR_FILE.lncol > CURR_LINE->llen-1) {
		go_home();
		go_down();
		go_smarthome();
		center_focusline();
		cnf.gstat &= ~(GSTAT_UPDNONE | GSTAT_UPDFOCUS);
	}

	if (!TEXT_LINE(CURR_LINE))
		cnf.gstat &= ~GSTAT_TYPING;

	if (!(cnf.gstat & GSTAT_TYPING)) {
		if (KEY_F4 == ch) {
			quit_file ();
		} else if (KEY_F8 == ch) {
			next_file ();
		} else if (KEY_ESC == ch) {
			all = cnf.tutor_pass + cnf.tutor_fail;
			r = (all > 0) ? 100.0 * cnf.tutor_pass / all : 0.0;
			tracemsg("passed %d of %d (rate %.2f)", cnf.tutor_pass, all, r);
			tracemsg("typo %d", cnf.tutor_fail);
		} else {
			tracemsg("--- stats --------");
			all = cnf.tutor_pass + cnf.tutor_fail;
			r = (all > 0) ? 100.0 * cnf.tutor_pass / all : 0.0;
			tracemsg("%d chars, rate %.2f (typo %d)", cnf.tutor_pass, r, cnf.tutor_fail);
			etime = time(NULL) - cnf.tutor_start;
			f = (etime > 0) ? 1.0 * all / etime : 0.0;
			wc(&linecnt, &wordcnt, &charcnt);
			g = (etime > 0) ? 60.0 * wordcnt / etime : 0.0;
			tracemsg("elapsed %d s, speed %.2f char/s, %.2f word/min", etime, f, g);
			/**/
			snprintf(score, sizeof(score), "%d chars, rate %.2f, speed %.2f c/s %.2f w/m\n",
				cnf.tutor_pass, r, f, g);
			put_string_to_file("tutor.log", score);
		}
		cnf.gstat |= GSTAT_REDRAW; //for upd_statusline
	}

	return (ret);
}

/*
* movements in the text area; interface functions, handle current file's lncol/curpos/lnoff,
* and engine functions, work with parameters
*/

/*
** keypress_enter - handle the Enter keypress in text area,
**	regular file buffers, special window
*/
int
keypress_enter (void)
{
	if ( !(CURR_FILE.fflag & FSTAT_NOEDIT) ) {
		split_line();
	} else if (CURR_FILE.fflag & FSTAT_SPECW) {
		general_parser();
	}

	return (0);
}

/*
** go_home - move cursor to 1st position in focus line
*/
int
go_home (void)
{
	CURR_FILE.lncol = 0;
	CURR_FILE.curpos = 0;
	CURR_FILE.lnoff = 0;
	return (0);
}

/*
** go_smarthome - move cursor to 1st position in focus line or 1st non-blank
*/
int
go_smarthome (void)
{
	int col=0;

	/* skip prefix blanks */
	for (col=0; col < CURR_LINE->llen-1 && IS_BLANK(CURR_LINE->buff[col]); col++)
		;

	if (CURR_FILE.lncol != 0) {
		/* real home */
		CURR_FILE.lncol = 0;
		CURR_FILE.curpos = 0;
		CURR_FILE.lnoff = 0;
	} else {
		/* first non-blank */
		CURR_FILE.lncol = col;
		CURR_FILE.curpos = get_pos(CURR_LINE, CURR_FILE.lncol);
		if (CURR_FILE.curpos - CURR_FILE.lnoff > TEXTCOLS-1)
			CURR_FILE.lnoff = CURR_FILE.curpos - TEXTCOLS + 1;
	}

	return (0);
}

/*
** go_end - move cursor to last position in focus line
*/
int
go_end (void)
{
	CURR_FILE.lncol = CURR_LINE->llen-1;	/* at last (NL) */
	update_curpos(cnf.ring_curr);
	return (0);
}

/*
* select_word - return word under/around text-area cursor (utility function)
* (returned pointer maybe NULL, otherwise must be freed by caller)
*/
char *
select_word (const LINE *lp, int lncol)
{
	char *symbol=NULL;
	int beg=0, end=0;
	int len=0;

	if ( !TEXT_LINE(lp) || lncol >= lp->llen-1 || !IS_ID(lp->buff[lncol]) ) {
		return symbol;
	}

	symbol = (char *) MALLOC(TAGSTR_SIZE);
	if (symbol == NULL) {
		ERRLOG(0xE035);
		return symbol;
	}
	symbol[0]='\0';

	/*
	* identify text-area position: not over last char (NL),
	* go left to select the first non-blank char
	* go right to select the last non-blank char
	* got a valid selection?
	* copy to symbol[] for return
	*
	*/
	for (beg = lncol; beg >= 0 && IS_ID(lp->buff[beg]); beg--)
		;
	++beg;
	for (end = lncol; end < lp->llen-1 && IS_ID(lp->buff[end]); end++)
		;
	--end;
	if (beg <= end)
	{
		if (beg>0 && (lp->buff[beg-1]=='.' || lp->buff[beg-1]=='>')) {
			beg--;
		}
		len = MIN(end-beg+1, TAGSTR_SIZE-1);
		strncpy (symbol, &lp->buff[beg], (size_t)len);
		symbol[len] = '\0';
	}

	return (symbol);
}

/*
** prev_nonblank - move cursor to the previous word's begin character on the left
*/
int
prev_nonblank (void)
{
	int col=0;
	int o_lnoff = CURR_FILE.lnoff;

	if (CURR_LINE->llen == 1)
		return(0);
	col = MIN(CURR_FILE.lncol, CURR_LINE->llen-1);

	if (col > 0) {
		col--;
		if (IS_BLANK(CURR_LINE->buff[col])) {
			/* select prev words begin */
			for ( ; col >= 0 && IS_BLANK(CURR_LINE->buff[col]); col--)
				;
			for ( ; col >= 0 && !IS_BLANK(CURR_LINE->buff[col]); col--)
				;
			++col;
		} else {
			/* select this words begin */
			for ( ; col >= 0 && !IS_BLANK(CURR_LINE->buff[col]); col--)
				;
			++col;
		}

		/* recalc */
		CURR_FILE.lncol = col;
		CURR_FILE.curpos = get_pos(CURR_LINE, CURR_FILE.lncol);
		if (CURR_FILE.lnoff > CURR_FILE.curpos)
			CURR_FILE.lnoff = CURR_FILE.curpos;
	}
	if (o_lnoff == CURR_FILE.lnoff)
		cnf.gstat |= GSTAT_UPDNONE;

	return(0);
}

/*
** next_nonblank - move cursor to the next word's begin character on the right
*/
int
next_nonblank (void)
{
	int col=0;
	int o_lnoff = CURR_FILE.lnoff;

	if (CURR_LINE->llen == 1)
		return(0);
	col = CURR_FILE.lncol;

	if (col < CURR_LINE->llen-1) {
		/* go right from this word */
		for ( ; col < CURR_LINE->llen-1 && !IS_BLANK(CURR_LINE->buff[col]); col++)
			;

		/* skip blanks */
		for ( ; col < CURR_LINE->llen-1 && IS_BLANK(CURR_LINE->buff[col]); col++)
			;

		/* begin of word: here we are */

		/* recalc */
		CURR_FILE.lncol = col;
		CURR_FILE.curpos = get_pos(CURR_LINE, CURR_FILE.lncol);
		if (CURR_FILE.curpos - CURR_FILE.lnoff > TEXTCOLS-1)
			CURR_FILE.lnoff = CURR_FILE.curpos - TEXTCOLS + 1;
	}
	if (o_lnoff == CURR_FILE.lnoff)
		cnf.gstat |= GSTAT_UPDNONE;

	return(0);
}

/* wc - count chars, words, lines like wc(1) does it in the base system
* do this on the lines of current buffer
*/
void
wc (int *linecnt, int *wordcnt, int *charcnt)
{
	int len, inword;
	char *p;
	LINE *lp;

	lp = CURR_FILE.top;
	next_lp (cnf.ring_curr, &lp, NULL);

	while (TEXT_LINE(lp)) {
		*linecnt += 1;
		*charcnt += lp->llen;

		len = lp->llen;
		p = lp->buff;

		inword = 0;
		while (len > 0) {
			if (IS_BLANK(*p)) {
				inword = 0;
			} else if (!inword) {
				inword = 1;
				*wordcnt += 1;
			}
			len--;
			p++;
		}

		next_lp (cnf.ring_curr, &lp, NULL);
	}

	return;
}

/* set new ring/lp/lineno position and unhide that line (internal function without any checks here)
*/
void
set_position (int ri, int lineno, LINE *lp)
{
	cnf.ring_curr = ri;
	CURR_LINE = lp;
	CURR_FILE.lineno = lineno;
	CURR_FILE.fflag &= ~FSTAT_CMD;
	/* must work even if !cnf.bootup */
	update_focus(CENTER_FOCUSLINE, ri);
	go_home();
	if (HIDDEN_LINE(cnf.ring_curr, CURR_LINE)) {
		CURR_LINE->lflag &= ~LMASK(cnf.ring_curr);
	}
}

/*
** go_left - move cursor to left by 1 character in the text area
*/
int
go_left (void)
{
	int fix_it=0, fixed=0;

	/* jump to line's end if over the end */
	if (CURR_FILE.lncol > CURR_LINE->llen-1) {
		CURR_FILE.lncol = CURR_LINE->llen-1;
		CURR_FILE.curpos = get_pos(CURR_LINE, CURR_FILE.lncol);

	} else if (CURR_FILE.lncol < 1) {
		CURR_FILE.lncol = 0;
		CURR_FILE.curpos = 0;

	/* backtab */
	} else {
		if (CURR_LINE->buff[CURR_FILE.lncol] == '\t') {
			fix_it = get_pos(CURR_LINE, CURR_FILE.lncol);
			if (CURR_FILE.curpos > fix_it) {
				CURR_FILE.curpos = fix_it;
				fixed = 1;
			}
		}
		if (!fixed) {
			CURR_FILE.lncol--;
			if (CURR_LINE->buff[CURR_FILE.lncol] == '\t') {
				CURR_FILE.curpos = get_pos(CURR_LINE, CURR_FILE.lncol);
			} else {
				CURR_FILE.curpos--;
			}
		}
	}

	if (CURR_FILE.lnoff > CURR_FILE.curpos)
		CURR_FILE.lnoff = CURR_FILE.curpos;

	return (0);
}

/*
** go_right - move cursor to right by 1 character in the text area
*/
int
go_right (void)
{
	/* limitation over line's end */
	if (CURR_FILE.lncol > CURR_LINE->llen-1) {
		if (CURR_FILE.lncol < LINESIZE_INIT) {
			CURR_FILE.lncol++;
			CURR_FILE.curpos++;
		}

	} else if (CURR_LINE->buff[CURR_FILE.lncol] == '\t') {
		CURR_FILE.lncol++;
		CURR_FILE.curpos += cnf.tabsize - (CURR_FILE.curpos % cnf.tabsize);

	} else {
		CURR_FILE.lncol++;
		CURR_FILE.curpos++;
	}

	if (CURR_FILE.curpos - CURR_FILE.lnoff > TEXTCOLS-1)
		CURR_FILE.lnoff = CURR_FILE.curpos - TEXTCOLS + 1;

	return (0);
}

/*
** scroll_1line_up - scroll the text area up with one line, keep focus
*/
int
scroll_1line_up (void)
{
	LINE *lp=CURR_LINE;
	int fcnt=0;	/* file lines */

	if (UNLIKE_TOP(lp)) {
		prev_lp (cnf.ring_curr, &lp, &fcnt);
		CURR_LINE = lp;
		CURR_FILE.lineno -= fcnt;
		CURR_FILE.lncol = get_col(CURR_LINE, CURR_FILE.curpos);
	}

	return (0);
}

/*
** go_up - go up one line in the text area, or leave command line
*/
int
go_up (void)
{
	LINE *lp=CURR_LINE;
	int fcnt=0;

	cnf.gstat |= GSTAT_UPDFOCUS;
	if (CURR_FILE.fflag & FSTAT_CMD) {
		CURR_FILE.fflag &= ~FSTAT_CMD;
	} else {
		if (UNLIKE_TOP(lp)) {
			prev_lp (cnf.ring_curr, &lp, &fcnt);
			CURR_LINE = lp;
			CURR_FILE.lineno -= fcnt;
			if (CURR_FILE.focus > 0) {
				if ((cnf.gstat & GSTAT_SHADOW) && (fcnt > 1)) {
					if (CURR_FILE.focus == 1)
						cnf.gstat &= ~(GSTAT_UPDNONE | GSTAT_UPDFOCUS);
					update_focus(DECR_FOCUS_SHADOW, cnf.ring_curr);
				} else {
					update_focus(DECR_FOCUS, cnf.ring_curr);
				}
			} else {
				cnf.gstat &= ~(GSTAT_UPDNONE | GSTAT_UPDFOCUS);
			}
		}
		CURR_FILE.lncol = get_col(CURR_LINE, CURR_FILE.curpos);
	}

	return (0);
}

/*
** scroll_1line_down - scroll the text area down with one line, keep focus
*/
int
scroll_1line_down (void)
{
	LINE *lp=CURR_LINE;
	int fcnt=0;	/* file lines */

	if (UNLIKE_BOTTOM(lp)) {
		next_lp (cnf.ring_curr, &lp, &fcnt);
		CURR_LINE = lp;
		CURR_FILE.lineno += fcnt;
		CURR_FILE.lncol = get_col(CURR_LINE, CURR_FILE.curpos);
	}

	return (0);
}

/*
** go_down - go down one line in the text area, or leave command line
*/
int
go_down (void)
{
	LINE *lp=CURR_LINE;
	int fcnt=0;

	cnf.gstat |= GSTAT_UPDFOCUS;
	if (CURR_FILE.fflag & FSTAT_CMD) {
		CURR_FILE.fflag &= ~FSTAT_CMD;
	} else {
		if (UNLIKE_BOTTOM(lp)) {
			next_lp (cnf.ring_curr, &lp, &fcnt);
			CURR_LINE = lp;
			CURR_FILE.lineno += fcnt;
			if (CURR_FILE.focus < TEXTROWS-1) {
				if ((cnf.gstat & GSTAT_SHADOW) && (fcnt > 1)) {
					if (CURR_FILE.focus == TEXTROWS-2)
						cnf.gstat &= ~(GSTAT_UPDNONE | GSTAT_UPDFOCUS);
					update_focus(INCR_FOCUS_SHADOW, cnf.ring_curr);
				} else {
					update_focus(INCR_FOCUS, cnf.ring_curr);
				}
			} else {
				cnf.gstat &= ~(GSTAT_UPDNONE | GSTAT_UPDFOCUS);
			}
		}
		CURR_FILE.lncol = get_col(CURR_LINE, CURR_FILE.curpos);
	}

	return (0);
}

/*
** go_page_up - move focus one screen upwards, or jump to screen's first line first
*/
int
go_page_up (void)
{
	if (CURR_FILE.focus > 0 || CURR_FILE.fflag & FSTAT_CMD) {
		go_first_screen_line ();
	} else {
		scroll_screen_up ();
	}
	return (0);
}

/* move focus up to the first line on the screen
*/
int
go_first_screen_line (void)
{
	LINE *lp=CURR_LINE;
	int fcnt=0;	/* file lines */

	/* switch to the text area */
	CURR_FILE.fflag &= ~FSTAT_CMD;

	/* upto the first line
	 */
	while (CURR_FILE.focus > 0) {
		prev_lp (cnf.ring_curr, &lp, &fcnt);
		if (UNLIKE_TOP(lp)) {
			CURR_LINE = lp;
			CURR_FILE.lineno -= fcnt;
			if ((cnf.gstat & GSTAT_SHADOW) && (fcnt > 1))
				update_focus(DECR_FOCUS_SHADOW, cnf.ring_curr);
			else
				update_focus(DECR_FOCUS, cnf.ring_curr);
		} else {
			break;
		}
	}
	/* set 1st, even if it was larger */
	update_focus(FOCUS_ON_1ST_LINE, cnf.ring_curr);

	CURR_FILE.lncol = get_col(CURR_LINE, CURR_FILE.curpos);
	return (0);
}

/*
** scroll_screen_up - scroll the text area up with at most one screen
*/
int
scroll_screen_up (void)
{
	LINE *lp=CURR_LINE;
	int cnt=0;	/* screen lines (shadow) */
	int fcnt=0;	/* file lines */

	/* scroll up one page -- focus remains */
	for (cnt=0; cnt < TEXTROWS; cnt++) {
		prev_lp (cnf.ring_curr, &lp, &fcnt);
		if (UNLIKE_TOP(lp)) {
			CURR_LINE = lp;
			CURR_FILE.lineno -= fcnt;
			if ((cnf.gstat & GSTAT_SHADOW) && (fcnt > 1)) {
				cnt++;	/* for the shadow line */
			}
		} else {
			break;
		}
	}

	CURR_FILE.lncol = get_col(CURR_LINE, CURR_FILE.curpos);
	return (0);
}

/*
** go_page_down - move focus one screen downwards, or jump to screen's last line first
*/
int
go_page_down (void)
{
	if (CURR_FILE.focus < TEXTROWS-1 || CURR_FILE.fflag & FSTAT_CMD) {
		go_last_screen_line ();
	} else {
		scroll_screen_down ();
	}
	return (0);
}

/* move focus up to the last line on the screen
*/
int
go_last_screen_line (void)
{
	LINE *lp=CURR_LINE;
	int fcnt=0;	/* file lines */

	/* switch to the text area */
	CURR_FILE.fflag &= ~FSTAT_CMD;

	/* upto the last line
	 */
	while (CURR_FILE.focus < TEXTROWS-1) {
		next_lp (cnf.ring_curr, &lp, &fcnt);
		if (UNLIKE_BOTTOM(lp)) {
			CURR_LINE = lp;
			CURR_FILE.lineno += fcnt;
			if ((cnf.gstat & GSTAT_SHADOW) && (fcnt > 1))
				update_focus(INCR_FOCUS_SHADOW, cnf.ring_curr);
			else
				update_focus(INCR_FOCUS, cnf.ring_curr);
		} else {
			break;
		}
	}
	/* set last, even if it was lower */
	update_focus(FOCUS_ON_LAST_LINE, cnf.ring_curr);

	CURR_FILE.lncol = get_col(CURR_LINE, CURR_FILE.curpos);
	return (0);
}

/*
** scroll_screen_down - scroll the text area down with at most one screen
*/
int
scroll_screen_down (void)
{
	LINE *lp=CURR_LINE;
	int cnt=0;	/* screen lines (shadow) */
	int fcnt=0;	/* file lines */

	/* scroll down one page -- focus remains */
	for (cnt=0; cnt < TEXTROWS; cnt++) {
		next_lp (cnf.ring_curr, &lp, &fcnt);
		if (UNLIKE_BOTTOM(lp)) {
			CURR_LINE = lp;
			CURR_FILE.lineno += fcnt;
			if ((cnf.gstat & GSTAT_SHADOW) && (fcnt > 1)) {
				cnt++;	/* for the shadow line */
			}
		} else {
			break;
		}
	}

	CURR_FILE.lncol = get_col(CURR_LINE, CURR_FILE.curpos);
	return (0);
}

/*
** go_top - move focus to the files first visible text line, or th etop marker
*/
int
go_top (void)
{
	CURR_LINE = CURR_FILE.top->next;
	if (TEXT_LINE(CURR_LINE) && !HIDDEN_LINE(cnf.ring_curr,CURR_LINE)) {
		CURR_FILE.lineno = 1;
	} else {
		CURR_LINE = CURR_FILE.top;
		CURR_FILE.lineno = 0;
	}
	CURR_FILE.fflag &= ~FSTAT_CMD;
	update_focus(FOCUS_ON_1ST_LINE, cnf.ring_curr);
	return (0);
}

/*
** go_bottom - move focus to the files last visible text line, or the bottom marker
*/
int
go_bottom (void)
{
	CURR_LINE = CURR_FILE.bottom->prev;
	if (TEXT_LINE(CURR_LINE) && !HIDDEN_LINE(cnf.ring_curr,CURR_LINE)) {
		CURR_FILE.lineno = CURR_FILE.num_lines;
	} else {
		CURR_LINE = CURR_FILE.bottom;
		CURR_FILE.lineno = CURR_FILE.num_lines+1;
	}
	CURR_FILE.fflag &= ~FSTAT_CMD;
	update_focus(FOCUS_ON_LAST_LINE, cnf.ring_curr);
	return (0);
}

/*
** goto_line - move focus to the line with given number, like ":1"
*/
int
goto_line (const char *args)
{
	int lineno;
	LINE *lp;

	if (args[0] == '+' || args[0] == ':') {
		args++;
	}
	lineno = atoi(&args[0]);

	/* users count lines from 1, do we so...
	* top is 0, bottom is num_lines+1
	*/
	lp = lll_goto_lineno (cnf.ring_curr, lineno);
	if (lp == NULL) {
		tracemsg("line is out of range");
		return (1);
	} else {
		set_position (cnf.ring_curr, lineno, lp);
		return (0);
	}
}

/*
** goto_pos - move cursor to the given screen position in the line
*/
int
goto_pos (const char *args)
{
	int curpos;

	cnf.gstat |= GSTAT_UPDNONE;
	if (args[0] == '\0') {
		tracemsg("character position %d (window offset %d), byte index %d, line length %d",
			CURR_FILE.curpos, CURR_FILE.lnoff, CURR_FILE.lncol, CURR_LINE->llen);
	} else {
		curpos = atoi(&args[0]);
		if (curpos >= 0) {
			CURR_FILE.curpos = curpos;
			CURR_FILE.lncol = get_col(CURR_LINE, CURR_FILE.curpos);
			if (CURR_FILE.lnoff > CURR_FILE.curpos) {
				CURR_FILE.lnoff = CURR_FILE.curpos;
				cnf.gstat &= ~(GSTAT_UPDNONE | GSTAT_UPDFOCUS);
			} else if (CURR_FILE.curpos - CURR_FILE.lnoff > TEXTCOLS-1) {
				CURR_FILE.lnoff = CURR_FILE.curpos - TEXTCOLS + 1;
				cnf.gstat &= ~(GSTAT_UPDNONE | GSTAT_UPDFOCUS);
			}
			CURR_FILE.fflag &= ~FSTAT_CMD;
		}
	}

	return (0);
}

/*
** center_focusline - scroll the screen that focus line will be vertically centered
*/
int
center_focusline (void)
{
	update_focus(CENTER_FOCUSLINE, cnf.ring_curr);
	return (0);
}

/* insert one string (no newline) into command line from current position
*/
int
type_cmd (const char *str)
{
	int slen=0, i=0;

	slen = strlen(str);
	if (slen > 0 && cnf.cmdline_len + slen < CMDLINESIZE-1) {
		cnf.reset_clhistory = 1;

		if (cnf.clpos < cnf.cmdline_len) {
			for (i = cnf.cmdline_len-1; i >= cnf.clpos; i--) {
				cnf.cmdline_buff[i+slen] = cnf.cmdline_buff[i];
			}
		} else {
			cnf.clpos = cnf.cmdline_len;
		}

		for (i = 0; i < slen && cnf.clpos+i < CMDLINESIZE-1; i++)
			cnf.cmdline_buff[cnf.clpos+i] = str[i];
		cnf.cmdline_len += i;
		cnf.cmdline_buff[cnf.cmdline_len] = '\0';

		cnf.clpos += i;
		if (cnf.clpos - cnf.cloff > cnf.maxx-1)
			cnf.cloff = cnf.clpos - cnf.maxx + 1;
	}
	cnf.gstat |= GSTAT_UPDNONE;

	return (0);
}

/*
** cp_text2cmd - copy current line from text area to the command line, overwrite rest of line
*/
int
cp_text2cmd (void)
{
	int space;

	cnf.reset_clhistory = 1;

	/* keep clpos and cloff -- copy as much as target space */
	cnf.cmdline_buff[cnf.clpos] = '\0';
	space = MIN(CMDLINESIZE-cnf.clpos-1, CURR_LINE->llen-1); // the rest, without LF
	if (space > 0)
		strncat(&cnf.cmdline_buff[cnf.clpos], CURR_LINE->buff, (size_t)space);
	cnf.cmdline_len = strlen(cnf.cmdline_buff);

	CURR_FILE.fflag |= FSTAT_CMD;
	cnf.gstat |= GSTAT_UPDNONE;

	return (0);
}

/*
** cp_name2open - copy nonspace characters around cursor to command line, assuming that is a filename
*/
int
cp_name2open (void)
{
	int beg, end, len;

#define FN(ch)	( ((ch) >= 'a' && (ch) <= 'z') || ((ch) >= 'A' && (ch) <= 'Z') || ((ch) == '_') || \
		((ch) >= '0' && (ch) <= '9') || ((ch) == '.') || ((ch) == '+') || ((ch) == '-') || ((ch) == '/') )

	if ( !TEXT_LINE(CURR_LINE) || CURR_FILE.lncol >= CURR_LINE->llen-1 || !FN(CURR_LINE->buff[CURR_FILE.lncol]) ) {
		return (0);
	}

	for (beg = CURR_FILE.lncol; beg >= 0 && FN(CURR_LINE->buff[beg]); beg--)
		;
	++beg;
	for (end = CURR_FILE.lncol; end < CURR_LINE->llen-1 && FN(CURR_LINE->buff[end]); end++)
		;
	--end;
	len = end-beg+1;

#undef FN

	cnf.reset_clhistory = 1;

	if (len > 0 && len < CMDLINESIZE-2) {
		/* reset clpos and cloff -- copy as much as target space */
		cnf.clpos = cnf.cloff = 0;
		strncpy(cnf.cmdline_buff, "e ", 3);
		strncat(cnf.cmdline_buff, &CURR_LINE->buff[beg], (size_t)len); // enough space; no '\0' in source
		cnf.cmdline_len = strlen(cnf.cmdline_buff);
	}

	CURR_FILE.fflag |= FSTAT_CMD;
	cnf.gstat |= GSTAT_UPDNONE;

	return (0);
}

/* csere - replace region in string with replacement,
* reallocate space in blocks, als>=32 even if bufsize is zero,
* return error if realloc failed
*/
int
csere (char **buffer, int *bufsize, int from, int length, const char *replacement, int rl)
{
	char *buf = *buffer;
	int size = *bufsize;
	int lo=0, up=0, rr=0;
	int n=0;
	char *s=NULL;
	unsigned als=0, allocated=0;

	if (size < 1) size=0;
	if (from < 1) from=0;
	if (length < 1) length=0;
	if (from > size) from=size;
	if (length > size-from) length=size-from;
	if (replacement == NULL || rl < 0) { /* abort replacement */
		return (-2);
	}

	allocated = ALLOCSIZE(size);
	if (size == 0 || buf == NULL) {
		size = length = from = 0;
		allocated = ALLOCSIZE(rl);
		s = (char *) REALLOC((void *)buf, allocated);
		if (s == NULL) {
			ERRLOG(0xE00B);
			return (-1);
		}
		buf = s;
	}

	if (rl > length) {
		/* the replacement is longer */

		/* realloc more space
		*/
		n = size + (rl - length);
		als = ALLOCSIZE(n);
		if (als > allocated) {
			s = (char *) REALLOC((void *)buf, als);
			if (s == NULL) {
				ERRLOG(0xE00A);
				return (-1);
			}
			buf = s;
		}
		size = n;

		/* push the rest away
		*/
		lo=size - (rl - length);
		up=size;
		while (lo >= from+length) {
			buf[up--] = buf[lo];
			buf[lo--] = '\0';
		}

	} else if (rl < length) {
		/* the replacement is shorter */

		/* pull the rest back
		*/
		lo=from+rl;
		up=from+length;
		while (up <= size) {
			buf[lo++] = buf[up++];
		}

		/* realloc less space
		*/
		n = size + (rl - length);
		als = ALLOCSIZE(n);
		if (als < allocated) {
			s = (char *) REALLOC((void *)buf, als);
			if (s == NULL) {
				ERRLOG(0xE009); /* no pb */
			} else {
				buf = s;
			}
		}
		size = n;
	}

	/* copy replacement in place
	*/
	lo=from;
	rr=0;
	if (replacement != NULL) {
		while (rr < rl && replacement[rr] != '\0') {
			buf[lo++] = replacement[rr++];
		}
	}

	/* safety */
	buf[size] = '\0';

	/* give pointers back */
	*buffer = buf;
	*bufsize = size;

	return (0);
}

///* like csere, but without bufsize parameter
//*/
//int
//csere0 (char **buffer, int from, int length, const char *replacement, int rl)
//{
//	int bufsize = 0;
//	int ret = 0;
//
//	if (*buffer != NULL)
//		bufsize = strlen(*buffer);
//	if (from >= 0) {
//		ret = csere (buffer, &bufsize, from, length, replacement, rl);
//	} else {
//		/* append */
//		ret = csere (buffer, &bufsize, bufsize, length, replacement, rl);
//	}
//
//	return (ret);
//}

/* milbuff - the internal line buff manager,
*
* change line buff content, reallocate space, save LF at the end,
* this is a low-level function, does not update GUI, for that
* call insert_chars() or delete_char() instead
* return error if realloc failed
*/
int
milbuff (LINE *lp, int from, int length, const char *replacement, int rl)
{
	char *s=NULL;
	unsigned als=0;

	/* it is the callers responsibility, if LF is in the replacement
	*/
	if (csere (&lp->buff, &lp->llen, from, length, replacement, rl)) {
		return (-1);
	}

	if (lp->buff[lp->llen-1] != '\n') {
		/* fixit
		*/
		als = ALLOCSIZE(lp->llen+1);
		if (als > ALLOCSIZE(lp->llen)) {
			s = (char *) REALLOC((void *)lp->buff, als);
			if (s == NULL) {
				ERRLOG(0xE008);
				return (-1); /* lost of resource */
			}
			lp->buff = s;
		}

		lp->buff[lp->llen++] = '\n';
		lp->buff[lp->llen] = '\0';
	}

	return (0);
}

/*
** type_text - insert stream data (multiline) into text buffer from cursor position,
**	smartindent is off while this action;
*/
int
type_text (const char *str)
{
	int slen=0, begin=0, last=0, ret=0, smartind;

	if (str[0] == '\0')
		return(0);

	if (CURR_FILE.fflag & FSTAT_NOEDIT) {
		return(1);
	}

	smartind = (cnf.gstat & GSTAT_SMARTIND);
	if (smartind)
		cnf.gstat &= ~GSTAT_SMARTIND;

	slen = strlen(str);
	begin = 0;
	last = 0;

	while (begin < slen) {
		while (last < slen && str[last] != '\n') {
			last++;
		}
		if (begin < last) {
			ret = insert_chars (&str[begin], last-begin);
		}
		if (ret) {
			break;
		}
		if (str[last] == '\n') {
			split_line();
			last++;
		}
		begin = last;
	}

	if (smartind)
		cnf.gstat |= GSTAT_SMARTIND;

	return (ret);
}

/*
* base edit functions in the text area
*/

/*
* insert_chars - insert characters (string w/o newline) into current line
* (only for text area)
*/
int
insert_chars (const char *input, int ilen)
{
	char simple_recalc = 0;

	if (!TEXT_LINE(CURR_LINE))
		return (0);
	if (ilen <= 0)
		return (0);

	/* pre-update */
	if (CURR_FILE.lncol > CURR_LINE->llen-1) {
		go_end();
	} else {
		if (ilen==1 && CURR_LINE->buff[CURR_FILE.lncol] != '\t' && input[0] != '\t')
			simple_recalc = 1;
	}

	/* insert */
	if (milbuff (CURR_LINE, CURR_FILE.lncol, 0, input, ilen)) {
		/* failed, but the buffer remains */
		return(1);
	}
	CURR_FILE.lncol += ilen;

	/* update */
	CURR_LINE->lflag |= LSTAT_CHANGE;

	/* recalc curpos and lnoff */
	if (simple_recalc) {
		CURR_FILE.curpos++;
	} else {
		CURR_FILE.curpos = get_pos(CURR_LINE, CURR_FILE.lncol);
	}
	if (CURR_FILE.curpos - CURR_FILE.lnoff > TEXTCOLS-1)
		CURR_FILE.lnoff = CURR_FILE.curpos - TEXTCOLS + 1;

	/* update */
	CURR_FILE.fflag |= FSTAT_CHANGE;

	return (0);
}

/*
** delete_char - delete one character under cursor in current line,
**	this action may delete the line itself or join the next line
*/
int
delete_char (void)
{
	char deleted_char;

	if (CURR_FILE.fflag & FSTAT_NOEDIT)
		return (0);
	if (!TEXT_LINE(CURR_LINE))
		return (0);

	/* at line end or over */
	if (CURR_FILE.lncol >= CURR_LINE->llen-1) {
		if (CURR_FILE.lncol == 0) {
			delline();
		} else {
			join_line();
		}

	/* in the middle of the line */
	} else {
		deleted_char = CURR_LINE->buff[CURR_FILE.lncol];

		if (CURR_FILE.lncol < CURR_LINE->llen-1) {
			/* realloc */
			(void) milbuff (CURR_LINE, CURR_FILE.lncol, 1, "", 0);

			/* update */
			CURR_LINE->lflag |= LSTAT_CHANGE;
		}

		if (deleted_char == '\t') {
			/* recalculate, it was a TAB */
			CURR_FILE.curpos = get_pos(CURR_LINE, CURR_FILE.lncol);
			if (CURR_FILE.lnoff > CURR_FILE.curpos)
				CURR_FILE.lnoff = CURR_FILE.curpos;
		}

		/* update */
		CURR_FILE.fflag |= FSTAT_CHANGE;
	}

	return (0);
}

/*
** delback_char - go left and delete, or join current line to the previous one
*/
int
delback_char (void)
{
	LINE *lp;

	if (CURR_FILE.fflag & FSTAT_NOEDIT)
		return (0);
	if (!TEXT_LINE(CURR_LINE))
		return (0);

	if (CURR_FILE.lncol > 0) {
		go_left();
		if (CURR_FILE.lncol < CURR_LINE->llen - 1) {
			delete_char();
		}
	} else {
		lp = CURR_LINE->prev;
		if (lp->lflag & LSTAT_TOP) {
			; /* silent no */
		} else if (HIDDEN_LINE(cnf.ring_curr,lp)) {
			tracemsg("the previous line is not in-view");
		} else {
			go_up();
			cnf.gstat &= ~GSTAT_UPDFOCUS;
			go_end();
			delete_char();
		}
	}

	return (0);
}

/*
** deleol - delete focus line characters from cursor position upto the end of line,
**	this action may join the next line
*/
int
deleol (void)
{
	int deleted_char;
	int o_lnoff = CURR_FILE.lnoff;

	if (CURR_FILE.fflag & FSTAT_CMD)
		return (0);
	if (!TEXT_LINE(CURR_LINE))
		return (0);

	/* at line end or over */
	if (CURR_FILE.lncol >= CURR_LINE->llen-1) {
		if (CURR_FILE.lncol == 0) {
			delline();
		} else {
			join_line();
		}

	/* in the middle of the line */
	} else {
		deleted_char = CURR_LINE->buff[CURR_FILE.lncol];
		(void) milbuff (CURR_LINE, CURR_FILE.lncol, CURR_LINE->llen, "\n", 1);

		/* update! */
		if (deleted_char == '\t') {
			/* recalculate, it was a TAB */
			CURR_FILE.curpos = get_pos(CURR_LINE, CURR_FILE.lncol);
			if (CURR_FILE.lnoff > CURR_FILE.curpos)
				CURR_FILE.lnoff = CURR_FILE.curpos;
		}
		CURR_FILE.fflag |= FSTAT_CHANGE;
		CURR_LINE->lflag |= LSTAT_CHANGE;

		if (o_lnoff == CURR_FILE.lnoff)
			cnf.gstat |= GSTAT_UPDFOCUS;
	}

	return (0);
}

/*
** del2bol - delete focus line characters from cursor position toward begin of line
*/
int
del2bol (void)
{
	if (CURR_FILE.fflag & FSTAT_CMD)
		return (0);
	if (!TEXT_LINE(CURR_LINE))
		return (0);

	/* at line end or over */
	if (CURR_FILE.lncol > CURR_LINE->llen-1)
		CURR_FILE.lncol = CURR_LINE->llen-1;

	if (CURR_FILE.lncol > 0) {
		(void) milbuff (CURR_LINE, 0, CURR_FILE.lncol, "", 0);

		/* update! */
		CURR_FILE.fflag |= FSTAT_CHANGE;
		CURR_LINE->lflag |= LSTAT_CHANGE;

		if (CURR_FILE.lnoff == 0)
			cnf.gstat |= GSTAT_UPDFOCUS;
	}
	CURR_FILE.curpos = CURR_FILE.lncol = CURR_FILE.lnoff = 0;

	return (0);
}

/*
** delline - delete current (focus) line in the text area
*/
int
delline (void)
{
	LINE *lp;
	int cnt=0;

	if (CURR_FILE.fflag & FSTAT_CMD)
		return (0);

	if (CURR_FILE.fflag & FSTAT_NODELIN)
		return (0);

	if (!TEXT_LINE(CURR_LINE))
		return (0);

	/* before remove */
	clr_opt_bookmark(CURR_LINE);	/* in delline() */

	/* save the next */
	lp = CURR_LINE;
	next_lp (cnf.ring_curr, &lp, &cnt);

	/* remove */
	lll_rm(CURR_LINE);	/* in delline() */
	CURR_FILE.num_lines--;
	CURR_FILE.fflag |= FSTAT_CHANGE;

	/* skip */
	CURR_LINE = lp;
	CURR_FILE.lineno += cnt-1;
	CURR_FILE.lncol = get_col(CURR_LINE, CURR_FILE.curpos);

	return (0);
}

/*
** duplicate - duplicate current (focus) line in text area and move cursor down
*/
int
duplicate (void)
{
	LINE *lx=NULL;
	int ret=0;

	if (!TEXT_LINE(CURR_LINE))
		return (0);

	if ((lx = append_line (CURR_LINE, CURR_LINE->buff)) == NULL) {
		ret=2;
	} else {
		lx->lflag = CURR_LINE->lflag & ~LSTAT_BM_BITS;
		CURR_LINE = lx;

		/* skip focus (no shadow line) */
		update_focus(INCR_FOCUS, cnf.ring_curr);

		/* update */
		CURR_FILE.lineno++;
		CURR_FILE.num_lines++;
		CURR_FILE.fflag |= FSTAT_CHANGE;
		CURR_LINE->lflag |= LSTAT_CHANGE;
	}

	return (ret);
}

/*
** split_line - split current line in two parts at current position
*/
int
split_line (void)
{
	LINE *lx=NULL;
	int blanks=0;

	/* insert empty line before bottom
	*/
	if (CURR_LINE->lflag & LSTAT_BOTTOM) {
		if ((lx = insert_line_before (CURR_LINE, "\n")) == NULL) {
			return(2);
		}
		lx->lflag |= LSTAT_CHANGE;
		CURR_LINE = lx;
		/* noupdate: focus/lineno value doesn't change */

	/* append empty line after top
	*/
	} else if (CURR_LINE->lflag & LSTAT_TOP) {
		if ((lx = append_line (CURR_LINE, "\n")) == NULL) {
			return(2);
		}
		lx->lflag |= LSTAT_CHANGE;
		CURR_LINE = lx;

		/* update */
		update_focus(INCR_FOCUS, cnf.ring_curr);
		CURR_FILE.lineno++;

	/* append empty line if cursor is at end or over the end
	*/
	} else if (CURR_FILE.lncol >= CURR_LINE->llen-1) {
		if (cnf.gstat & GSTAT_SMARTIND) {
			blanks = count_prefix_blanks (CURR_LINE->buff, CURR_LINE->llen);
		}

		if ((lx = append_line (CURR_LINE, "\n")) == NULL) {
			return(2);
		}
		lx->lflag |= LSTAT_CHANGE;
		lx->lflag |= (CURR_LINE->lflag & LSTAT_SELECT);
		if (cnf.gstat & GSTAT_SMARTIND) {
			if (milbuff (lx, 0, blanks, CURR_LINE->buff, blanks)) {
				return (2);
			}
		}
		CURR_LINE = lx;

		/* update */
		update_focus(INCR_FOCUS, cnf.ring_curr);
		CURR_FILE.lineno++;

	/* cursor is at start of line (the line is not empty), insert line before
	*/
	} else if (CURR_FILE.lncol == 0) {
		if ((lx = insert_line_before (CURR_LINE, "\n")) == NULL) {
			return(2);
		}
		lx->lflag |= LSTAT_CHANGE;
		lx->lflag |= (CURR_LINE->lflag & LSTAT_SELECT);
		/* CURR_LINE remains */

		/* update */
		update_focus(INCR_FOCUS, cnf.ring_curr);
		CURR_FILE.lineno++;

	/* real split */
	} else {
		if (cnf.gstat & GSTAT_SMARTIND) {
			blanks = count_prefix_blanks (CURR_LINE->buff, CURR_LINE->llen);
		}

		/* breakpoint for the new line */
		if ((lx = append_line (CURR_LINE, &CURR_LINE->buff[CURR_FILE.lncol])) == NULL) {
			return(2);
		}
		if (cnf.gstat & GSTAT_SMARTIND) {
			if (milbuff (lx, 0, 0, CURR_LINE->buff, blanks)) {
				return (2);
			}
		}

		/* realloc old focus line */
		(void) milbuff (CURR_LINE, CURR_FILE.lncol, CURR_LINE->llen, "\n", 1);

		/* bits for both lines */
		CURR_LINE->lflag |= LSTAT_CHANGE;
		lx->lflag |= LSTAT_CHANGE;
		lx->lflag |= (CURR_LINE->lflag & (LSTAT_TAG1 | LSTAT_SELECT));
		CURR_LINE = lx;

		/* update */
		update_focus(INCR_FOCUS, cnf.ring_curr);
		CURR_FILE.lineno++;

	}

	CURR_FILE.lncol = blanks;
	update_curpos(cnf.ring_curr);

	CURR_FILE.num_lines++;
	CURR_FILE.fflag |= FSTAT_CHANGE;

	return (0);
}

/*
** join_line - join current line with next visible text line
*/
int
join_line (void)
{
	LINE *lp_next = NULL;
	int next_is_empty = 0;

	if (!TEXT_LINE(CURR_LINE))
		return (0);

	lp_next = CURR_LINE->next;
	next_is_empty = (lp_next->llen <= 1);

	if (!TEXT_LINE(lp_next)) {
		return (0);
	} else if (HIDDEN_LINE(cnf.ring_curr,lp_next)) {
		tracemsg("the next line is not in-view");
		return (0);
	}

	/* pre-update */
	if (CURR_FILE.lncol > CURR_LINE->llen-1)
		go_end();

	/* join CURR_LINE->buff with lp_next->buff */
	if (milbuff (CURR_LINE, CURR_LINE->llen-1, 1, lp_next->buff, lp_next->llen)) {
		return (2);
	}

	/* remove */
	clr_opt_bookmark(lp_next);
	lll_rm(lp_next);		/* in join_line() */

	/* update */
	CURR_FILE.num_lines--;
	CURR_FILE.fflag |= FSTAT_CHANGE;
	if (!next_is_empty)
		CURR_LINE->lflag |= LSTAT_CHANGE;
	update_curpos(cnf.ring_curr);

	return (0);
}

/*
* ed_common - editor commands for command line and text area,
* get values from table[] and macros[]
*
* return:
* 0: not relevant (key not handled here)
* 4: action done
*/
int
ed_common (int ch)
{
	int ret=4;
	int ti=0;
	char args_buff[CMDLINESIZE];

	switch (ch)
	{
	case KEY_ESC:	/* switch between command and text area */
		switch_text_cmd();
		break;
	case KEY_C_UP:		/* cmdline, get prev from the history */
		clhistory_prev();
		break;
	case KEY_C_DOWN:	/* cmdline, get next from the history */
		clhistory_next();
		break;
	case KEY_NONE:		/* ignore */
		break;

	default:
		/* search for fkey in macros[].fkey and table[].fkey
		*/
		memset(args_buff, 0, sizeof(args_buff));

		ti = hash_fkey(ch);
		if (ti >= 0) {
			if (ti < TLEN) {
				run_command (ti, args_buff, ch);
				break;	/* switch */
			} else {
				run_macro_command (ti-TLEN, args_buff);
				break;	/* switch */
			}
		}

		/* warning in event_handler */

		ret = 0;
		break;
	} /* switch */

	return (ret);
} /* ed_common() */
