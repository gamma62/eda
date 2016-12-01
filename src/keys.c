/*
* keys.c
* keypress raw processing engine, uses external config data
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
#include <errno.h>
#include <stdlib.h>
#include <syslog.h>
#include "curses_ext.h"
#include "main.h"	/* includes <stdio.h> */
#include "proto.h"
#include "curses_ext.h"

#ifndef CONFPATH
#define CONFPATH	"/etc/eda"
#endif

/* global config */
extern CONFIG cnf;
extern KEYS keys[];
extern const int KLEN;
extern const int RES_KLEN;
/* extern int ESCDELAY; */

MEVENT pointer;

/* dynamic mapping */
int key_c_up    = 0;
int key_c_down  = 0;
int key_c_left  = 0;
int key_c_right = 0;
int key_c_ppage = 0;
int key_c_npage = 0;

/* local proto */
static int process_esc_seq (const char *str, NODE *tree);
static int test_key_handler (NODE *seq_tree);

/*
 * wrapper around wgetch() to add extended keypress recognition,
 * extra characters by sequence
 * args:
 *	seq_tree, has been filled up and every node->leaf is valid key_value
 * return:
 *	normal character,
 *	sequence tree leaf,
 *	KEY_RESIZE with some delay
 */
int
key_handler (WINDOW *wind, NODE *seq_tree)
{
	NODE *node = NULL;
	int ch = 0;
	char drop = 0;
	char resize = 0;
	int delay = 0;
	char ready = 0;
	int i;

	while (!ready) {
		ch = wgetch (wind);

		if (ch == KEY_RESIZE) {
			resize = 1;
			delay = 0;	/* reset */

		} else if (resize) {
			if (++delay >= RESIZE_DELAY) {
				ch = KEY_RESIZE;
				resize = 0;
				ready = 1;
			}

		} else if (ch == KEY_MOUSE) {
			if (getmouse(&pointer) == OK) {
				ready = 1;
			}

		/* dynamic mapping */
		} else if (ch == key_c_up) {
			ch = KEY_C_UP;
			ready = 1;
		} else if (ch == key_c_down) {
			ch = KEY_C_DOWN;
			ready = 1;
		} else if (ch == key_c_left) {
			ch = KEY_C_LEFT;
			ready = 1;
		} else if (ch == key_c_right) {
			ch = KEY_C_RIGHT;
			ready = 1;
		} else if (ch == key_c_ppage) {
			ch = KEY_C_PPAGE;
			ready = 1;
		} else if (ch == key_c_npage) {
			ch = KEY_C_NPAGE;
			ready = 1;

		} else if (ch != ERR) {

			if (ch == KEY_ESC) {
				node = seq_tree;

			} else if (node != NULL) {
				/* process sequence */
				for (i=0; i < node->bcount; i++) {
					if (node->branch[i]->ch == ch) {
						break;
					}
				}
				if (i < node->bcount) {
					node = node->branch[i];
					if (node->bcount == 0 && node->leaf != 0) {
						ch = node->leaf;
						ready = 1;
					}
					/* else: ok, continue processing */
				} else {
					/* not found, abort sequence */
					node = NULL;
					drop = 1;
				}

			} else if (!drop) {	/* node == NULL */
#ifdef META_SETS_HIGHBIT
				if ((ch >= 0xa0 && ch < 0xff)) {
					/* simulate KEY_ESC + simple character sequence */
					node = seq_tree;
					ungetch(ch & ~0x80);
				} else
#endif
				{	/* simple character */
					ready = 1;
				}
			}
			/* else: drop character */

		} else {	/* ch == ERR */
			if (node != NULL) {
				/* check sequence, must be a leaf */
				if (node->leaf != 0) {
					ch = node->leaf;
					ready = 1;
				}
				/* else: should not happen */
			} else {
				/* timeout */
				ready = 1;
			}
			node = NULL;
			drop = 0;
		}
	}/* while */

	return (ch);
}

/*
 * process the sequence file,
 * get the 'KEYNAME esc-sequence' lines and put into tree (cnf.seq_tree)
 * with calls to process_esc_seq()
 * return: 0 if everything is ok, or no file found
 */
int
process_seqfile (int noconfig)
{
	char seqfile[2][sizeof(cnf.myhome)+SHORTNAME];
	char *fname;
	int ret=0;
	FILE *fp;
	NODE *seq = NULL;
	char buff[CMDLINESIZE];
	int lno, len;

	seq = (NODE *) MALLOC(sizeof(NODE));
	if (seq == NULL) {
		return (1);
	}

	/* set ESC key even if sequence files do not exist */
	seq->ch = KEY_ESC;
	seq->leaf = KEY_ESC;
	seq->branch = NULL;
	seq->bcount = 0;

	if (noconfig) {
		cnf.seq_tree = seq;
		return 0;
	}

	/* read in first available only */
	strncpy(seqfile[0], cnf.myhome, sizeof(seqfile[0]));
	strncat(seqfile[0], "edaseq", SHORTNAME);
	strncpy(seqfile[1], CONFPATH "/edaseq", sizeof(seqfile[1]));

	/* read one sequence file only */
	fname = seqfile[0];
	if ((fp = fopen(fname, "r")) == NULL) {
		fname = seqfile[1];
		if ((fp = fopen(fname, "r")) == NULL) {
			cnf.seq_tree = seq;
			return (0);
		}
	}

	lno = 0;
	/* fprintf(stderr, "eda: processing seqfile [%s]\n", fname); */
	while (ret==0) {
		if (fgets (buff, CMDLINESIZE, fp) == NULL) {
			if (ferror(fp)) {
				ret = 2;
			}
			break;
		}
		++lno;
		len = strlen(buff);
		if (len > 0 && buff[len-1] == '\n')
			buff[--len] = '\0';
		ret = process_esc_seq(buff, seq);
	}
	fclose(fp);

	if (ret) {
		fprintf(stderr, "eda: processing [%s] failed (%d) line=%d\n", fname, ret, lno);
		FREE(seq);
		seq = NULL;
		free_seq_tree(cnf.seq_tree);
	} else {
		cnf.seq_tree = seq;
	}

	return (ret);
}

/*
 * process the input string
 * get values and interpret them
 * merge the whole set into the tree
 * return: 0 if everything is ok
 */
static int
process_esc_seq (const char *str, NODE *tree)
{
	const char *p;
	char *q;
	char key_name[30];
	int key_val=0;
	int leaves[20];
	NODE *node, *ptr;
	NODE **ptr2;
	unsigned lcount=0, i=0;
	int j=0, ki=0;

	p = str;

	/* drop comment line */
	if (*p == '#' || *p == '\n' || *p == '\0') {
		return (0);
	}

	/* copy the first WORD to key_name[], pattern: [A-Z0-9_]+ */
	for (i=0; i < sizeof(key_name); i++) {
		if ((*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || (*p == '_')) {
			key_name[i] = *p;
		} else {
			break;
		}
		p++;
	}
	if (i < sizeof(key_name)) {
		key_name[i] = '\0';
	} else {
		fprintf(stderr, "eda: process_esc_seq: key name should have less than %d characters [%s]\n", i, str);
		return (-1);
	}

	/* validate key_name against valid names in keys[] to get key_val */
	ki = index_key_string (key_name);
	if (ki >= KLEN) {
		fprintf(stderr, "eda: process_esc_seq: key %s is unknown\n", key_name);
		return (-2);
	} else if (ki < RES_KLEN) {
		fprintf(stderr, "eda: process_esc_seq: key %s is reserved\n", key_name);
		return (-3);
	}
	key_val = keys[ki].key_value;

	/* the rest is the escape sequence, convert hex chars to numbers and add to leaves[] */
	for (lcount=0;  lcount < (sizeof(leaves)/sizeof(int)) && *p != '\0'; lcount++) {
		leaves[lcount] = strtol(p, &q, 16);
		if (p == q) {
			fprintf(stderr, "eda: process_esc_seq: sequence should have only hexadecimal number values: [%s]\n", str);
			return (-4);
		}
		p = q;	/* skip to next */
	}
	if (lcount == 1 && leaves[0] != KEY_ESC) {
		/* dynamic mapping */
		if (strncmp(key_name, "KEY_C_UP", 8) == 0) {
			key_c_up = leaves[0];
			return (0);
		} else if (strncmp(key_name, "KEY_C_DOWN", 10) == 0) {
			key_c_down = leaves[0];
			return (0);
		} else if (strncmp(key_name, "KEY_C_LEFT", 10) == 0) {
			key_c_left = leaves[0];
			return (0);
		} else if (strncmp(key_name, "KEY_C_RIGHT", 11) == 0) {
			key_c_right = leaves[0];
			return (0);
		} else if (strncmp(key_name, "KEY_C_PPAGE", 11) == 0) {
			key_c_ppage = leaves[0];
			return (0);
		} else if (strncmp(key_name, "KEY_C_NPAGE", 11) == 0) {
			key_c_npage = leaves[0];
			return (0);
		}
	}
	if (leaves[0] != KEY_ESC) {
		fprintf(stderr, "eda: process_esc_seq: first leaf must be %x\n", KEY_ESC);
		return (-5);
	}
	if (lcount >= (sizeof(leaves)/sizeof(int))) {
		fprintf(stderr, "eda: process_esc_seq: key should have less than %d values [%s]\n", lcount, str);
		return (-6);
	}

	/*
	*	fprintf(stderr, "%s is 0x%04x: sequence of %d items: ", key_name, key_val, lcount);
	*	for (i=0;  i < lcount; i++) {
	*		fprintf(stderr, "%02x ", leaves[i]);
	*	}
	*	fprintf(stderr, "\n");
	*/

	/* merge leaves[] to tree, node is the running pointer */
	node = tree;
	for (i=1; i < lcount; i++) {
		/* search the next node->branch[] : j */
		j = node->bcount;
		if (node->branch != NULL) {
			for (j=0; j < node->bcount; j++) {
				if (node->branch[j]->ch == leaves[i])
					break;
			}
		}

		/* create 'branch' if necessary */
		if (j == node->bcount) {

			/* allocate space for bigger pointer array -- sizeof(NODE *) should be portable */
			ptr2 = (NODE **) REALLOC(node->branch, sizeof(NODE *) * (node->bcount+1));
			if (ptr2 == NULL) {
				return (-7);
			}
			node->branch = (NODE **)ptr2;
			node->branch[node->bcount] = NULL;	/* new array item */

			/* allocate space for the new pointer */
			ptr = (NODE *) MALLOC(sizeof(NODE));
			if (ptr == NULL) {
				return (-8);
			}

			/* set newbie */
			ptr->ch = leaves[i];
			ptr->leaf = 0;		/* leaf at the end, only */
			ptr->branch = NULL;	/* this array is empty */
			ptr->bcount = 0;	/* empty */
			node->branch[node->bcount] = ptr;

			/* increment at last */
			node->bcount++;
		}

		/* check the node pointers */
		if (node->branch == NULL || node->branch[j] == NULL) {
			fprintf(stderr, "internal error -- %s is 0x%04x (%d items)\n", key_name, key_val, lcount);
			return (-7);
		}
		/* skip down to next level */
		node = node->branch[j];
	}
	/* at last add key_val to leaf */
	node->leaf = key_val;

	return (0);
}

/*
 * free allocated memory:
 * this function will be called recursively
 */
void
free_seq_tree (NODE *node)
{
	int i;

	if (node != NULL) {
		if (node->branch != NULL) {
			for (i=0; i < node->bcount; i++) {
				free_seq_tree (node->branch[i]);
			}
			FREE(node->branch); node->branch = NULL;
		}
		FREE(node); node = NULL;
	}

	return;
}

/*
 * test version of event_handler()
 * keypress recognition test
 * based on library features and escape sequence tree,
 * read and processed from external file
 * ---
 * experimental mouse support, switch on/off by 'm'
 */
void
key_test (void)
{
	volatile int mouse_on=0;
	mmask_t mouse_events_mask = (BUTTON1_PRESSED | BUTTON1_RELEASED |
		BUTTON1_CLICKED | BUTTON1_DOUBLE_CLICKED |
		BUTTON1_TRIPLE_CLICKED |
		BUTTON2_CLICKED | BUTTON3_CLICKED);
	int ch=0, ki=0, maxx=0, maxy=0;

	initscr ();	/* Begin */
	cbreak ();
	noecho ();
	nonl ();
	clear ();
	scrollok(stdscr, TRUE);
	getmaxyx(stdscr, maxy, maxx);

	keypad (stdscr, TRUE);
	ESCDELAY = CUST_ESCDELAY;
	wtimeout (stdscr, CUST_WTIMEOUT);

	wprintw (stdscr, "eda: key_test\n");
	wprintw (stdscr, "escape delay=%d, wgetch timeout=%d maxx=%d maxy=%d\n", CUST_ESCDELAY, CUST_WTIMEOUT, maxx, maxy);
	wprintw (stdscr, "quit by Q\n");
	wsetscrreg(stdscr, 3, maxy-1);

	while (ch != 'q') {
		ch = test_key_handler (cnf.seq_tree);

		if (ch != ERR) {
			if (ch == KEY_RESIZE) {
				wprintw (stdscr, ">KEY_RESIZE\n");
				/* no RESIZE handling here */
			} else if (ch >= 0x20 && ch != 0x7F && ch <= 0xFF) {
				wprintw (stdscr, ">%02X=%c.", ch, ch);
			} else if (ch == '\r') {
				wprintw (stdscr, ">%02X\n", ch);
			} else if (ch == KEY_MOUSE) {
				wprintw (stdscr, ">KEY_MOUSE row=%d col=%d\n",
					pointer.y, pointer.x);
			} else {
				ki = index_key_value(ch);
				if (ki < KLEN) {
					wprintw (stdscr, ">0x%02X=%s\n", ch, keys[ki].key_string);
				} else {
					wprintw (stdscr, ">0x%02X=(unknown)\n", ch);
				}
			}
			/* put to screen */
			wnoutrefresh (stdscr);
		}

		/* the regular update */
		doupdate ();

		if (ch == 'm') {
			if (mouse_on) {
				(void) mousemask(0L, NULL);
			} else {
				(void) mousemask(mouse_events_mask, NULL);
			}
			mouse_on = !mouse_on;
		}
	}

	endwin ();	/* End */
}

/*
 * test version of key_handler()
 * inputs and returns are the same,
 * added trace: every element of sequence will be printed and
 * decisions also
 * ---
 * mouse events reported also (experimental)
 */
static int
test_key_handler (NODE *seq_tree)
{
	NODE *node = NULL;
	int ch = 0;
	char drop = 0;
	char resize = 0;
	int delay = 0;
	char ready = 0;
	int i;
	int mok=0;

	while (!ready) {
		ch = wgetch (stdscr);

		if (ch == KEY_RESIZE) {
			wprintw (stdscr, "*");
			resize = 1;
			delay = 0;	/* reset */

		} else if (resize) {
			if (++delay >=  RESIZE_DELAY) {
				ch = KEY_RESIZE;
				resize = 0;
				ready = 1;
			} else {
				wprintw (stdscr, ".");
			}

		} else if (ch == KEY_MOUSE) {
			mok = getmouse(&pointer);
			wprintw (stdscr, "(%d: y=%d x=%d state=0%o) ",
				mok, pointer.y, pointer.x, pointer.bstate);
			if (mok == OK) {
				ready = 1;
			}

		/* dynamic mapping */
		} else if (ch == key_c_up) {
			wprintw (stdscr, "0X%02X-", ch);
			ch = KEY_C_UP;
			ready = 1;
		} else if (ch == key_c_down) {
			wprintw (stdscr, "0X%02X-", ch);
			ch = KEY_C_DOWN;
			ready = 1;
		} else if (ch == key_c_left) {
			wprintw (stdscr, "0X%02X-", ch);
			ch = KEY_C_LEFT;
			ready = 1;
		} else if (ch == key_c_right) {
			wprintw (stdscr, "0X%02X-", ch);
			ch = KEY_C_RIGHT;
			ready = 1;
		} else if (ch == key_c_ppage) {
			wprintw (stdscr, "0X%02X-", ch);
			ch = KEY_C_PPAGE;
			ready = 1;
		} else if (ch == key_c_npage) {
			wprintw (stdscr, "0X%02X-", ch);
			ch = KEY_C_NPAGE;
			ready = 1;

		} else if (ch != ERR) {

			if (ch == KEY_ESC) {
				wprintw (stdscr, "%02X-", ch);
				node = seq_tree;

			} else if (node != NULL) {
				wprintw (stdscr, "%02X-", ch);
				/* process sequence */
				for (i=0; i < node->bcount; i++) {
					if (node->branch[i]->ch == ch) {
						break;
					}
				}
				if (i < node->bcount) {
					node = node->branch[i];
					if (node->bcount == 0 && node->leaf != 0) {
						ch = node->leaf;
						ready = 1;
					}
					/* else: ok, continue processing */
				} else {
					/* not found, abort sequence */
					wprintw (stdscr, "(no branch)-");
					node = NULL;
					drop = 1;
				}

			} else if (!drop) {	/* node == NULL */
#ifdef META_SETS_HIGHBIT
				if ((ch >= 0xa0 && ch < 0xff)) {
					/* simulate KEY_ESC + simple character sequence */
					wprintw (stdscr, "%02X=FAKE_META-", ch);
					node = seq_tree;
					ungetch(ch & ~0x80);
				} else
#endif
				{	/* simple character */
					ready = 1;
				}

			}
			/* else: drop character */
			else {
				wprintw (stdscr, "drop:%02X-", ch);
			}

		} else {	/* ch == ERR */
			if (node != NULL) {
				/* check sequence, must be a branch and leaf */
				if (node->leaf != 0) {
					ch = node->leaf;
					ready = 1;
				}
				/* else: should not happen */
				else {
					wprintw (stdscr, "(end w/o leaf!!!!)");
				}
			} else {
				/* timeout */
				ready = 1;
			}
			node = NULL;
			drop = 0;
		}

	}/* while */

	return (ch);
}
