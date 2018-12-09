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

#include <config.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <syslog.h>
#include "curses_ext.h"
#include "main.h"	/* includes <stdio.h> */
#include "proto.h"

#ifndef CONFPATH
#define CONFPATH	"/etc/eda"
#endif

/* global config */
extern CONFIG cnf;

extern TABLE table[];
extern KEYS keys[];
extern const int TLEN, KLEN, RES_KLEN;
extern MACROS *macros;
extern int MLEN;
extern MEVENT mouse_event_pointer;

/* dynamic mapping */
static int key_c_up    = 0;
static int key_c_down  = 0;
static int key_c_left  = 0;
static int key_c_right = 0;
static int key_c_ppage = 0;
static int key_c_npage = 0;

/* local proto */
static int process_esc_seq (const char *str, NODE *tree);

/*
 * wrapper around wgetch() to add extended keypress recognition,
 * extra characters by sequence
 * args:
 *	seq_tree, has been filled up and every node->leaf is valid key_value
 * return:
 *	normal character,
 *	sequence tree leaf,
 *	KEY_RESIZE no delay
 */
int
key_handler (NODE *seq_tree, int testing)
{
	NODE *node = NULL;
	int ch=0, ready=0, mok=0, meta_key=0, node_seq_items=0;
	unsigned i=0;

	while (!ready) {
		ch = wgetch (stdscr);

		if (ch == KEY_RESIZE) {
			ready = 1;
			if (testing) wprintw (stdscr, "resize ");

		} else if (ch == KEY_MOUSE) {
			mok = getmouse(&mouse_event_pointer);
			if (testing) {
				wprintw (stdscr, "(%d: y=%d x=%d bstate=0%o) ",
					mok, mouse_event_pointer.y, mouse_event_pointer.x, mouse_event_pointer.bstate);
			}
			if (mok == OK) {
				ready = 1;
			}

		/* dynamic mapping */
		} else if (ch == key_c_up) {
			if (testing) wprintw (stdscr, "%02X ", ch);
			ch = KEY_C_UP;
			ready = 2;
		} else if (ch == key_c_down) {
			if (testing) wprintw (stdscr, "%02X ", ch);
			ch = KEY_C_DOWN;
			ready = 2;
		} else if (ch == key_c_left) {
			if (testing) wprintw (stdscr, "%02X ", ch);
			ch = KEY_C_LEFT;
			ready = 2;
		} else if (ch == key_c_right) {
			if (testing) wprintw (stdscr, "%02X ", ch);
			ch = KEY_C_RIGHT;
			ready = 2;
		} else if (ch == key_c_ppage) {
			if (testing) wprintw (stdscr, "%02X ", ch);
			ch = KEY_C_PPAGE;
			ready = 2;
		} else if (ch == key_c_npage) {
			if (testing) wprintw (stdscr, "%02X ", ch);
			ch = KEY_C_NPAGE;
			ready = 2;

		} else if (ch != ERR) {

			if (ch == KEY_ESC) {
				if (testing) wprintw (stdscr, "%02X ", ch);
				node = seq_tree;
				node_seq_items = 1;

			} else if (node != NULL) {
				if (testing) wprintw (stdscr, "%02X ", ch);
				++node_seq_items;

				/* maybe META character sequence */
				if (node_seq_items == 2 && ch >= 0x20 && ch < 0x7f) {
					switch (ch) {
					case ' ': meta_key=KEY_M_SPACE; break;

					case '!': meta_key=KEY_M_EXCLAM; break;
					case '\"': meta_key=KEY_M_DQUOTE; break;
					case '#': meta_key=KEY_M_HASH; break;
					case '$': meta_key=KEY_M_DOLLAR; break;
					case '%': meta_key=KEY_M_PERCENT; break;
					case '&': meta_key=KEY_M_AMPERSAND; break;
					case '\'': meta_key=KEY_M_SQUOTE; break;
					case '(': meta_key=KEY_M_LPAREN; break;
					case ')': meta_key=KEY_M_RPAREN; break;
					case '*': meta_key=KEY_M_STAR; break;
					case '+': meta_key=KEY_M_PLUS; break;
					case ',': meta_key=KEY_M_COMMA; break;
					case '-': meta_key=KEY_M_MINUS; break;
					case '.': meta_key=KEY_M_DOT; break;
					case '/': meta_key=KEY_M_SLASH; break;
					case '0': meta_key=KEY_M_ZERO; break;
					case '1': meta_key=KEY_M_ONE; break;
					case '2': meta_key=KEY_M_TWO; break;
					case '3': meta_key=KEY_M_THREE; break;
					case '4': meta_key=KEY_M_FOUR; break;
					case '5': meta_key=KEY_M_FIVE; break;
					case '6': meta_key=KEY_M_SIX; break;
					case '7': meta_key=KEY_M_SEVEN; break;
					case '8': meta_key=KEY_M_EIGHT; break;
					case '9': meta_key=KEY_M_NINE; break;
					case ':': meta_key=KEY_M_COLON; break;
					case ';': meta_key=KEY_M_SEMICOLON; break;
					case '<': meta_key=KEY_M_LESSTHAN; break;
					case '=': meta_key=KEY_M_EQUAL; break;
					case '>': meta_key=KEY_M_GREATHAN; break;
					case '?': meta_key=KEY_M_QMARK; break;

					case '@': meta_key=KEY_M_AT; break;
					case 'A': meta_key=KEY_S_M_A; break;
					case 'B': meta_key=KEY_S_M_B; break;
					case 'C': meta_key=KEY_S_M_C; break;
					case 'D': meta_key=KEY_S_M_D; break;
					case 'E': meta_key=KEY_S_M_E; break;
					case 'F': meta_key=KEY_S_M_F; break;
					case 'G': meta_key=KEY_S_M_G; break;
					case 'H': meta_key=KEY_S_M_H; break;
					case 'I': meta_key=KEY_S_M_I; break;
					case 'J': meta_key=KEY_S_M_J; break;
					case 'K': meta_key=KEY_S_M_K; break;
					case 'L': meta_key=KEY_S_M_L; break;
					case 'M': meta_key=KEY_S_M_M; break;
					case 'N': meta_key=KEY_S_M_N; break;
					case 'O': meta_key=KEY_S_M_O; break; /* 0x1b 0x4f is special */
					case 'P': meta_key=KEY_S_M_P; break;
					case 'Q': meta_key=KEY_S_M_Q; break;
					case 'R': meta_key=KEY_S_M_R; break;
					case 'S': meta_key=KEY_S_M_S; break;
					case 'T': meta_key=KEY_S_M_T; break;
					case 'U': meta_key=KEY_S_M_U; break;
					case 'V': meta_key=KEY_S_M_V; break;
					case 'W': meta_key=KEY_S_M_W; break;
					case 'X': meta_key=KEY_S_M_X; break;
					case 'Y': meta_key=KEY_S_M_Y; break;
					case 'Z': meta_key=KEY_S_M_Z; break;
					case '[': meta_key=KEY_M_LSQBRAC; break; /* 0x1b 0x5b is special */
					case '\\': meta_key=KEY_M_BACKSLASH; break;
					case ']': meta_key=KEY_M_RSQBRAC; break;
					case '^': meta_key=KEY_M_CARET; break;
					case '_': meta_key=KEY_M_UNDERLINE; break;

					case '`': meta_key=KEY_M_GRAVEACC; break;
					case 'a': meta_key=KEY_M_A; break;
					case 'b': meta_key=KEY_M_B; break;
					case 'c': meta_key=KEY_M_C; break;
					case 'd': meta_key=KEY_M_D; break;
					case 'e': meta_key=KEY_M_E; break;
					case 'f': meta_key=KEY_M_F; break;
					case 'g': meta_key=KEY_M_G; break;
					case 'h': meta_key=KEY_M_H; break;
					case 'i': meta_key=KEY_M_I; break;
					case 'j': meta_key=KEY_M_J; break;
					case 'k': meta_key=KEY_M_K; break;
					case 'l': meta_key=KEY_M_L; break;
					case 'm': meta_key=KEY_M_M; break;
					case 'n': meta_key=KEY_M_N; break;
					case 'o': meta_key=KEY_M_O; break;
					case 'p': meta_key=KEY_M_P; break;
					case 'q': meta_key=KEY_M_Q; break;
					case 'r': meta_key=KEY_M_R; break;
					case 's': meta_key=KEY_M_S; break;
					case 't': meta_key=KEY_M_T; break;
					case 'u': meta_key=KEY_M_U; break;
					case 'v': meta_key=KEY_M_V; break;
					case 'w': meta_key=KEY_M_W; break;
					case 'x': meta_key=KEY_M_X; break;
					case 'y': meta_key=KEY_M_Y; break;
					case 'z': meta_key=KEY_M_Z; break;
					case '{': meta_key=KEY_M_LCURBRAC; break;
					case '|': meta_key=KEY_M_BAR; break;
					case '}': meta_key=KEY_M_RCURBRAC; break;
					case '~': meta_key=KEY_M_TILDE; break;
					default:
						meta_key=0;
						break;
					}
					if (meta_key != 0 && ch != 0x5b && ch != 0x4f) {
						/* early break, quick response to repeated Alt-keys */
						ch = meta_key;
						ready = 1;
					}
				} else {
					meta_key=0;
				}

				/* move node pointer forward, maybe the input sequence is
				 * a real seq, like: 1B 5B 31 3B 32 50 for KEY_S_F1
				 * or a meta char, like: 1B 5B for KEY_M_LSQBRAC
				 */
				for (i=0; i < node->bcount; i++) {
					if (node->branch[i]->ch == ch) {
						break;
					}
				}
				if (i < node->bcount) {
					/* has a branch, still in sequence */
					node = node->branch[i];
				} else {
					/* sequence lost, must be an undefined sequence */
					node = NULL;
				}

			} else {	/* node == NULL */
				if (node_seq_items) {
					if (testing) wprintw (stdscr, "%02X ", ch);
					++node_seq_items;
				} else {
					/* 'ch' must be simple character */
					ready = 1;
				}
			}

		} else {	/* ch == ERR */
			if (node_seq_items) {
				if (node != NULL && node->leaf != 0) {
					ch = node->leaf;
				} else if (node_seq_items == 2 && meta_key != 0) {
					ch = meta_key;
				} else {
					if (testing) wprintw (stdscr, "=> undefined sequence\n");
				}
				ready = 1;
			} else {
				/* timeout */
				ready = 9;
			}
			node = NULL;
		}
	}/* while */

	/* postprocessing */
	switch (ch)
	{
	case KEY_BACKSPACE:
		break;
	case KEY_C_H:
		if (testing) wprintw (stdscr, "((map KEY_C_H 0x%02X to KEY_BACKSPACE))\n", ch);
		ch = KEY_BACKSPACE;
		break;
	case KEY_ASCII_DEL:
		if (testing) wprintw (stdscr, "((map KEY_ASCII_DEL 0x%02X to KEY_BACKSPACE))\n", ch);
		ch = KEY_BACKSPACE;
		break;
	default:
		break;
	}

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
		ERRLOG(0xE038);
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
				ERRLOG(0xE097);
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
		free_seq_tree (cnf.seq_tree);
		seq = NULL;
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
	unsigned lcount=0, i=0, j=0;
	int ki=0;

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
	for (lcount=0; lcount < (sizeof(leaves)/sizeof(int)) && *p != '\0'; lcount++) {
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
		/* search for matching sequence item */
		j = node->bcount;
		if (node->branch != NULL) {
			for (j=0; j < node->bcount; j++) {
				if (node->branch[j]->ch == leaves[i]) /* found */
					break;
			}
		}

		/* add new item to node->branch[] if there is no match */
		if (j == node->bcount) {

			/* allocate space for bigger pointer array -- sizeof(NODE *) should be portable */
			ptr2 = (NODE **) REALLOC(node->branch, sizeof(NODE *) * (node->bcount+1));
			if (ptr2 == NULL) {
				ERRLOG(0xE00C);
				return (-7);
			}
			node->branch = (NODE **)ptr2;
			node->branch[node->bcount] = NULL;	/* new array item */

			/* allocate space for the new pointer */
			ptr = (NODE *) MALLOC(sizeof(NODE));
			if (ptr == NULL) {
				ERRLOG(0xE037);
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

	/* at last add key_val to leaf -- the final result of esc key sequence */
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
	unsigned i=0;

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
	int ch=0, ki=0, maxx=0, maxy=0, ti=0;

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

	wprintw (stdscr, "eda: key_test -- escape sequences ((ESCDELAY=%d WTIMEOUT=%d))\n", ESCDELAY, CUST_WTIMEOUT);
	wprintw (stdscr, "     format: escape key sequence => key code ... [function name]\n");
	wprintw (stdscr, "     quit with Q or q\n");
	wprintw (stdscr, "TERM=%s %dx%d -- %s\n", getenv("TERM"), maxy, maxx, curses_version());
	wprintw (stdscr, "# 00000000 8x'0' (REP ECMA-048)\n");
	wsetscrreg(stdscr, 5, maxy-1);

	while (ch != 'q') {
#ifdef RAW
		ch = wgetch(stdscr);
		if (ch != ERR) {
			if (ch == '\r' || ch == '\n')
				wprintw (stdscr, "\n");
			else
				wprintw (stdscr, "%02X ", ch);
		}
#else
		ch = key_handler (cnf.seq_tree, 2);

		if (ch != ERR) {
			if (ch == KEY_RESIZE) {
				wprintw (stdscr, "=> KEY_RESIZE\n");
				/* no RESIZE handling here */
			} else if (ch >= 0x20 && ch != 0x7F && ch <= 0xFF) {
				wprintw (stdscr, "%c", ch);
			} else if (ch == '\r') {
				wprintw (stdscr, "\n");
			} else if (ch == KEY_MOUSE) {
				wprintw (stdscr, "=> KEY_MOUSE row=%d col=%d\n",
					mouse_event_pointer.y, mouse_event_pointer.x);
			} else if (ch == KEY_NONE) {
				wprintw (stdscr, "%02X => ignored\n", ch);
			} else {
				ki = index_key_value(ch);
				if (ki >= 0 && ki < KLEN) {
					wprintw (stdscr, "=> %02X=%s", ch, keys[ki].key_string);
					ti = index_macros_fkey(ch);
					if (ti >= 0 && ti < MLEN) {
						wprintw (stdscr, " ... %s (macro)", macros[ti].name);
					} else {
						ti = keys[ki].table_index;
						if (ti >= 0 && ti < TLEN) {
							wprintw (stdscr, " ... %s", table[ti].fullname);
						}
					}
				} else {
					wprintw (stdscr, "%02X => unbound key", ch);
				}
				wprintw (stdscr, "\n");
			}
		}

		if (ch == 'm') {
			if (mouse_on) {
				(void) mousemask(0L, NULL);
			} else {
				(void) mousemask(mouse_events_mask, NULL);
			}
			mouse_on = !mouse_on;
		}
#endif
	}

	endwin ();	/* End */
}
