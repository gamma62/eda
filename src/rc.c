/*
* rc.c
* tools for resource handling, load and set resources, key trees, macros, projects
* and the "cmds" table
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
#include <stdlib.h>	/* atoi, strtol */
#include <unistd.h>	/* access */
#include <errno.h>
#include <syslog.h>
#include "main.h"
#include "proto.h"

#ifndef CONFPATH
#define CONFPATH	"/etc/eda"
#endif
#ifndef PROGHOME
#define PROGHOME	"/usr/local/share/eda"
#endif

/* global config */
extern CONFIG cnf;

extern TABLE table[];
extern KEYS keys[];
extern const int TLEN, KLEN, RES_KLEN;
extern MACROS *macros;
extern int MLEN;

/* local proto */
static void set_usage (int show_what);
static int fkey_table_relation (char *args);
static int pretty_print_key_names(int ki, char *key_buff, int size);

#define SHOW_CURRENT_VALUES 1
#define SHOW_USAGE 0

/*
* the usage for set()
*/
static void
set_usage (int show_what)
{
	if (show_what == SHOW_CURRENT_VALUES) {
		tracemsg ("prefix %d tabhead %d shadow %d smartind %d move_reset %d case_sensitive %d",
			(cnf.gstat & GSTAT_PREFIX) ? 1 : 0,
			(cnf.gstat & GSTAT_TABHEAD) ? 1 : 0,
			(cnf.gstat & GSTAT_SHADOW) ? 1 : 0,
			(cnf.gstat & GSTAT_SMARTIND) ? 1 : 0,
			(cnf.gstat & GSTAT_MOVES) ? 1 : 0,
			(cnf.gstat & GSTAT_CASES) ? 1 : 0);
		tracemsg ("autotitle %d backup_nokeep %d close_over %d save_inode %d",
			(cnf.gstat & GSTAT_AUTOTITLE) ? 1 : 0,
			(cnf.gstat & GSTAT_NOKEEP) ? 1 : 0,
			(cnf.gstat & GSTAT_CLOS_OVER) ? 1 : 0,
			(cnf.gstat & GSTAT_SAV_INODE) ? 1 : 0);
		tracemsg ("indent %s %d  tabsize %d",
			(cnf.gstat & GSTAT_INDENT) ? "tab" : "space",
			cnf.indentsize,
			cnf.tabsize);
		tracemsg ("find path [%s]  find opts [%s]",
			cnf.find_path, cnf.find_opts);
		tracemsg ("make path %s make opts [%s]",
			cnf.make_path, cnf.make_opts);
		tracemsg ("sh path %s diff path [%s]",
			cnf.sh_path, cnf.diff_path);
		tracemsg ("tags file [%s] lsdirsort %d",
			cnf.tags_file, cnf.lsdirsort); 
		tracemsg ("...see other settings in rcfile");
	} else if (show_what == SHOW_USAGE) {
		tracemsg ("set {prefix | tabhead | shadow | smartindent | move_reset | case_sensitive} {on|off}");
		tracemsg ("set {tabsize COUNT} | {indent {tab|space} COUNT}");
		tracemsg ("set {autotitle | backup_nokeep | close_over | save_inode } {yes|no}");
		tracemsg ("set {find_opts OPTIONS}");
		tracemsg ("set {make_opts OPTS}");
		tracemsg ("set {tags_file FILE}");
		tracemsg ("...other settings in rcfile");
	}
}

/******************************************************************************
rc_c_local_macros() {
*/

#define SET_CHECK(dest) \
	if (sublen > 0) { \
		if (sublen < (int)sizeof(dest)) { \
			strncpy((dest), subtoken, sizeof(dest)); \
			(dest)[sizeof(dest)-1] = '\0'; \
		} else { \
			if (cnf.bootup) tracemsg ("name very long [%s]", subtoken); \
			ret = 2; \
		} \
	} \
	if (cnf.bootup) tracemsg ("[%s]", (dest));

#define SET_CHECK_X(dest, testlen, testpath) \
	if ((testlen) > 0) { \
		if ((testlen) < (int)sizeof(dest)) { \
			if (access((testpath), R_OK | X_OK) == 0) { \
				strncpy((dest), (testpath), sizeof(dest)); \
				(dest)[sizeof(dest)-1] = '\0'; \
			} else { \
				if (cnf.bootup) tracemsg ("invalid path [%s]", (testpath)); \
				ret = 3; \
			} \
		} else { \
			if (cnf.bootup) tracemsg ("name very long [%s]", (testpath)); \
			ret = 2; \
		} \
	} \
	if (cnf.bootup) tracemsg ("[%s]", (dest));

#define SET_CHECK_B(mask) \
	if (sublen > 0) { \
		y = yesno(subtoken); \
		if (y == 1) { \
			cnf.gstat |= (mask); \
		} else if (y == 0) { \
			cnf.gstat &= ~(mask); \
		} else { \
			ret = 4; \
		} \
	}

/*
}
******************************************************************************/

/*
** set - set resource values or print as notification, usage: "set [resource [value(s)]]",
**	get help with "set help"
*/
int
set (const char *argz)
{
	char args[FNAMESIZE];
	char *token=NULL, *subtoken=NULL, *subsubtoken=NULL;
	int slen=0, restidx=-1, sublen=-1, subsublen=-1;
	int x=0, y=0, ri=0, ret=0;

	memset(args, 0, sizeof(args));
	strncpy(args, argz, sizeof(args)-2); /* reserve two zero bytes, we may overwrite one */

	slen = parse_token(args, "\x09 ", &restidx);

	/* special case separately: empty line */
	if (slen == 0) {
		if (cnf.bootup) set_usage(SHOW_CURRENT_VALUES);
		return (0);
	}

	args[slen] = '\0';
	token = &args[0];
	if (restidx > 0) {
		subtoken = &args[restidx];
		sublen = parse_token(subtoken, "\x09 ", &restidx);
		subtoken[sublen] = '\0'; /* restore delimiter if required */
		if (restidx > 0) {
			subsubtoken = &subtoken[restidx];
			subsublen = strlen(subsubtoken);
		}
	}

	if (token[0] == '?') {
		if (cnf.bootup) set_usage(SHOW_USAGE);
		return (0);
	} else if (slen < 3 || token[0] == '#') {
		/* 1 or 2 char resource names and comments will be ignored */
		return (0);
	}

	/*
	 * test each setting one by one, (slen >= 3) now
	 */
	if (strncmp(token, "prefix", 6)==0) {
		SET_CHECK_B( GSTAT_PREFIX );
		if (ret==0) {
			cnf.pref = (y==1) ? PREFIXSIZE : 0;
		}
		/* update! */
		for (ri=0; ri<RINGSIZE; ri++) {
			if (cnf.fdata[ri].fflag & FSTAT_OPEN)
				update_curpos(ri);
		}
		//if (cnf.bootup) tracemsg ("prefix %d", (cnf.gstat & GSTAT_PREFIX) ? 1 : 0);

	} else if (strncmp(token, "tabhead", 7)==0) {
		SET_CHECK_B( GSTAT_TABHEAD );
		if (ret==0) {
			cnf.head = (y==1) ? 2 : 1;
		}
		/* update! */
		for (ri=0; ri<RINGSIZE; ri++) {
			if (cnf.fdata[ri].focus > TEXTROWS-1)
				cnf.fdata[ri].focus = TEXTROWS;
		}
		//if (cnf.bootup) tracemsg ("tabhead %d", (cnf.gstat & GSTAT_TABHEAD) ? 1 : 0);

	} else if (strncmp(token, "shadow", 6)==0) {
		SET_CHECK_B( GSTAT_SHADOW );
		if (cnf.bootup) tracemsg ("shadow %d", (cnf.gstat & GSTAT_SHADOW) ? 1 : 0);

	} else if (strncmp(token, "smartindent", 5)==0) {
		SET_CHECK_B( GSTAT_SMARTIND );
		if (cnf.bootup) tracemsg ("smartind %d", (cnf.gstat & GSTAT_SMARTIND) ? 1 : 0);

	} else if (strncmp(token, "move_reset", 10)==0) {
		SET_CHECK_B( GSTAT_MOVES );
		if (cnf.bootup) tracemsg ("move_reset %d", (cnf.gstat & GSTAT_MOVES) ? 1 : 0);

	} else if (strncmp(token, "case_sensitive", 4)==0) {
		SET_CHECK_B( GSTAT_CASES );
		if (cnf.bootup) tracemsg ("case_sensitive %d", (cnf.gstat & GSTAT_CASES) ? 1 : 0);

	} else if (strncmp(token, "autotitle", 9)==0) {
		SET_CHECK_B( GSTAT_AUTOTITLE );
		if (cnf.bootup) tracemsg ("autotitle %d", (cnf.gstat & GSTAT_AUTOTITLE) ? 1 : 0);

	} else if (strncmp(token, "backup_nokeep", 13)==0) {
		SET_CHECK_B( GSTAT_NOKEEP );
		if (cnf.bootup) tracemsg ("backup_nokeep %d", (cnf.gstat & GSTAT_NOKEEP) ? 1 : 0);

	} else if (strncmp(token, "close_over", 10)==0) {
		SET_CHECK_B( GSTAT_CLOS_OVER );
		if (cnf.bootup) tracemsg ("close_over %d", (cnf.gstat & GSTAT_CLOS_OVER) ? 1 : 0);

	} else if (strncmp(token, "save_inode", 10)==0) {
		SET_CHECK_B( GSTAT_SAV_INODE );
		if (cnf.bootup) tracemsg ("save_inode %d", (cnf.gstat & GSTAT_SAV_INODE) ? 1 : 0);

	} else if (strncmp(token, "tabsize", 4)==0) {
		/* decimal */
		if (sublen > 0) {
			x = strtol(subtoken, NULL, 10);
			if (x < 2 || x > 16) {
				ret = 1;
			} else if (cnf.tabsize != x) {
				cnf.tabsize = x;
				if (cnf.bootup) {
					/* update! */
					for (ri=0; ri<RINGSIZE; ri++) {
						if (cnf.fdata[ri].fflag & FSTAT_OPEN)
							update_curpos(ri);
					}
				}
			}
		}
		if (cnf.bootup) tracemsg ("tabsize %d", cnf.tabsize);

	} else if (strncmp(token, "indent", 6)==0) {
		if (sublen > 0) {
			ret = 1;
			x = 0;
			if (strncmp(subtoken, "tab", 3)==0) {
				cnf.gstat |= GSTAT_INDENT;
				x = 1;
			} else if (strncmp(subtoken, "space", 5)==0) {
				cnf.gstat &= ~GSTAT_INDENT;
				x = 2;
			}
			if (x) {
				if (subsublen > 0) {
					x = strtol(subsubtoken, NULL, 10);
					if (x > 7) {
						cnf.indentsize = 8;
					} else if (x < 2) {
						cnf.indentsize = 1;
					} else {
						cnf.indentsize = x;
					}
					ret = 0;
				}
			}
		}
		if (cnf.bootup) tracemsg ("indent %s %d", ((cnf.gstat & GSTAT_INDENT) ? "tab" : "space"), cnf.indentsize);

	} else if (strncmp(token, "tags_file", 4)==0) {
		SET_CHECK( cnf.tags_file );

	} else if (strncmp(token, "find_opts", 9)==0) {
		if (sublen > 0) {
			subtoken[sublen] = ' '; /* overwrite zero, one multiword argument required */
			sublen = strlen(subtoken);
			SET_CHECK( cnf.find_opts );
		} else {
			if (cnf.bootup) tracemsg ("find_opts %s", cnf.find_opts);
		}
	} else if (!cnf.bootup && strncmp(token, "find_path", 9)==0) {
		SET_CHECK_X( cnf.find_path, sublen, subtoken );

	} else if (strncmp(token, "make_opts", 9)==0) {
		if (sublen > 0) {
			subtoken[sublen] = ' '; /* overwrite zero, one multiword argument required */
			sublen = strlen(subtoken);
			SET_CHECK( cnf.make_opts );
		} else {
			if (cnf.bootup) tracemsg ("make_opts %s", cnf.make_opts);
		}
	} else if (!cnf.bootup && strncmp(token, "make_path", 9)==0) {
		SET_CHECK_X( cnf.make_path, sublen, subtoken );

	} else if (!cnf.bootup && strncmp(token, "sh_path", 9)==0) {
		SET_CHECK_X( cnf.sh_path, sublen, subtoken );
	} else if (!cnf.bootup && strncmp(token, "diff_path", 9)==0) {
		SET_CHECK_X( cnf.diff_path, sublen, subtoken );

	/* vcs tool settings */
	} else if (!cnf.bootup && strncmp(token, "vcstool", 7)==0) {
		if (sublen > 0) {
			ret = 1;
			for(x=0; x < 10; x++) {
				y = strlen(cnf.vcs_tool[x]);
				if (y==0 || !strncmp(subtoken, cnf.vcs_tool[x], (size_t)y)) {
					break;
				}
			}
			if (x < 10 && subsublen > 0) {
				y = sizeof(cnf.vcs_tool[x]);
				strncpy(cnf.vcs_tool[x], subtoken, (size_t)y);
				cnf.vcs_tool[x][y-1] = '\0';
				ret = 0;
				SET_CHECK_X( cnf.vcs_path[x], subsublen, subsubtoken );
				if (ret != 0) {
					cnf.vcs_tool[x][0] = '\0';
					cnf.vcs_path[x][0] = '\0';
				}
			}
		}
	}

	/* terminal color settings, color palette string parser -- shared prefix "palette"
	*/
	else if (!cnf.bootup && strncmp(token, "palette", 7)==0) {
		if (sublen > 0) {
			if ((subtoken[x] >= '0') && (subtoken[x] <= '9')) {
				x = strtol(subtoken, NULL, 0);
				if (x >= 0 && x < cnf.palette_count)
					cnf.palette = x;
			} else {
				/* pass input string to parser */
				subtoken[sublen] = ' '; /* overwrite zero, one multiword argument required */
				ret = color_palette_parser(subtoken);
			}
		}
	}

	/* lsdir sort settings */
	else if (!strncmp(token, "sort", 4) || !strncmp(token, "lsdirsort", 9)) {
		if (sublen > 0) {
			x = strtol(subtoken, NULL, 0);
			if (x >= 0 && x <= 2) {
				if (x != cnf.lsdirsort) {
					cnf.lsdirsort = x;
				}
			}
		}
		if (cnf.bootup) tracemsg ("lsdirsort %d", cnf.lsdirsort);
	}

	/* syslog log levels by module */
	else if (strncmp(token, "log", 3)==0) {
		int size2=0;
		char mystring[100];
		size2 = sizeof(cnf.log) / sizeof(cnf.log[0]);
		if (sublen > 0) {
			subtoken[sublen] = ' '; /* overwrite zero, one multiword argument required */
			sublen = strlen(subtoken);
			for(x=0; x < size2; x++) {
				cnf.log[x] = 0;
			}
			for(x=0,y=0; subtoken[x] != '\0' && y < size2; x++) {
				/* skip not-a-number */
				if ((subtoken[x] >= '0') && (subtoken[x] <= '7')) {
					cnf.log[y] = (subtoken[x] - '0');
					y++;
				}
			}
		}
		if (cnf.bootup && size2 > 11) {
			snprintf(mystring, sizeof(mystring),
			"MAIN%d FH%d CMD%d HIST%d REPL%d TAGS%d SELE%d FILT%d PIPE%d PD%d REC%d UPD%d",
				cnf.log[0],cnf.log[1],cnf.log[2],cnf.log[3],cnf.log[4],cnf.log[5],
				cnf.log[6],cnf.log[7],cnf.log[8],cnf.log[9],cnf.log[10],cnf.log[11]);
			tracemsg ("log %s", mystring);
		}
	}

	else {
		if (cnf.bootup) {
			set_usage(SHOW_USAGE);
		} else {
			fprintf(stderr, "eda: rc [%s] [%s] ... bad token\n", token, subtoken);
			ret = 2;
		}
	}

	return (ret);
}/* set */

/*
* process rc files, before cnf.bootup
*/
int
process_rcfile (int noconfig)
{
	char rcfile[2][sizeof(cnf.myhome)+SHORTNAME];
	int j=0, rcline=0, len=0, ret=0;
	FILE *fp;
	char str[CMDLINESIZE];

	if (noconfig) {
		return 0;
	}

	/* read in first available only */
	strncpy(rcfile[0], cnf.myhome, sizeof(rcfile[0]));
	strncat(rcfile[0], "edarc", SHORTNAME);
	strncpy(rcfile[1], CONFPATH "/edarc", sizeof(rcfile[1]));

	for (j=0; j<2; j++)
	{
		rcline = 0;
		if ((fp = fopen(rcfile[j], "r")) != NULL)
		{
			while (ret==0) {
				if (fgets (str, CMDLINESIZE, fp) == NULL) {
					if (ferror(fp)) {
						ERRLOG(0xE096);
						ret = 8;
					}
					break;
				}
				++rcline;
				len = strlen(str);
				if (len > 0 && str[len-1] == '\n')
					str[--len] = '\0';
				strip_blanks (STRIP_BLANKS_FROM_END|STRIP_BLANKS_FROM_BEGIN, str, &len);
				/**/
				if ((ret = set (str)) != 0) {
					fprintf(stderr, "eda: resource failure %s:%d\n", rcfile[j], rcline);
					// error in rcfile
				}
			}
			fclose(fp);
		}
		if (ret) {
			fprintf(stderr, "eda: processing [%s] failed (%d), line=%d\n", rcfile[j], ret, rcline);
			break;
		}
		if (rcline > 0) {
			break;
		}
	}

	return (ret);
}/* process_rcfile */

/*
* write table index to keys[]
* internal function for process_keyfile()
*/
static int
fkey_table_relation (char *args)
{
	int ti=0, ki=0;
	char *s_func=NULL, *s_key=NULL;
	int slen=0, restidx=-1, sublen=-1;
	int ret = 1;

	slen = parse_token(args, "\x09= ", &restidx);

	/* special case separately: empty line */
	if (slen == 0) {
		return (0);
	}

	args[slen] = '\0';
	s_key = &args[0];
	if (restidx > 0) {
		s_func = &args[restidx];
		sublen = parse_token(s_func, "\x09 ", &restidx);
		s_func[sublen] = '\0';
	}

	if (slen < 3 || sublen < 1 || s_key[0] == '#') {
		/* comment lines covered also */
		return (0);
	}

	/* find ti and ki */
	ret=1;
	ti = index_func_fullname(s_func);
	if (ti < TLEN && table[ti].fkey != -1) {
		ki = index_key_string(s_key);
		if (ki < KLEN && ki >= RES_KLEN) {
			keys[ki].table_index = ti;
			ret = 0;
		} else {
			if (ki < RES_KLEN) {
				fprintf(stderr, "eda: key %s is reserved\n", s_key);
			} else {
				fprintf(stderr, "eda: key %s is unknown\n", s_key);
			}
		}
	} else {
		fprintf(stderr, "eda: function %s is unknown or cannot bind fkey\n", s_func);
	}

	return (ret);
}/* fkey_table_relation */

/*
* process keys files, before cnf.bootup
*/
int
process_keyfile (int noconfig)
{
	char keyfile[2][sizeof(cnf.myhome)+SHORTNAME];
	int j=0, kline=0, len=0, ret=0, ki;
	FILE *fp;
	char str[CMDLINESIZE];

	/* must be cleaned */
	for (ki=0; ki < KLEN; ki++) {
		keys[ki].table_index = -1;
	}

	if (noconfig) {
		return 0;
	}

	/* read in first available only */
	strncpy(keyfile[0], cnf.myhome, sizeof(keyfile[0]));
	strncat(keyfile[0], "edakeys", SHORTNAME);
	strncpy(keyfile[1], CONFPATH "/edakeys", sizeof(keyfile[1]));

	for (j=0; j<2; j++)
	{
		kline = 0;
		if ((fp = fopen(keyfile[j], "r")) != NULL)
		{
			while (ret==0) {
				if (fgets (str, CMDLINESIZE, fp) == NULL) {
					if (ferror(fp)) {
						ERRLOG(0xE095);
						ret = 8;
					}
					break;
				}
				++kline;
				len = strlen(str);
				if (len > 0 && str[len-1] == '\n')
					str[--len] = '\0';
				strip_blanks (STRIP_BLANKS_FROM_END|STRIP_BLANKS_FROM_BEGIN, str, &len);
				/**/
				ret = fkey_table_relation (str);
			}
			fclose(fp);
		}
		if (ret) {
			fprintf(stderr, "eda: processing [%s] failed (%d), line=%d\n", keyfile[j], ret, kline);
			break;
		}
		if (kline > 0) {
			break;
		}
	}

	return (ret);
}/* process_keyfile */

/*
* process macro files, before and after cnf.bootup
*/
int
process_macrofile (int noconfig)
{
	char macfile[2][sizeof(cnf.myhome)+SHORTNAME];
	int j=0, mline=0, len=0, ret=0, iy=0, ix=0;
	FILE *fp;
	char inputline[CMDLINESIZE];
	char name[20];
	int fkey, ti, ki;
	void *ptr=NULL;
	MACROS *mac=NULL;
	char pattern_header[SEARCHSTR_SIZE];
	char pattern_item[SEARCHSTR_SIZE];
	regex_t regexp_header, regexp_item;
	regmatch_t pmatch[3];

	macros = NULL;
	MLEN = 0;

	if (noconfig) {
		return 0;
	}

	strncpy(pattern_header, "^(KEY_[A-Z0-9_]+)[[:blank:]]*(.*)", 100);
	strncpy(pattern_item, "^\t([a-zA-Z0-9_]+)[[:blank:]]*(.*)", 100);

	/* read in first available only */
	strncpy(macfile[0], cnf.myhome, sizeof(macfile[0]));
	strncat(macfile[0], "edamacro", SHORTNAME);
	strncpy(macfile[1], PROGHOME "/edamacro", sizeof(macfile[1]));

	if (regcomp (&regexp_header, pattern_header, REG_EXTENDED | REG_NEWLINE)) {
		ERRLOG(0xE08D);
		return (1);
	}
	if (regcomp (&regexp_item, pattern_item, REG_EXTENDED | REG_NEWLINE)) {
		ERRLOG(0xE08C);
		regfree (&regexp_header);
		return (1);
	}

	for (j=0; j<2; j++)
	{
		mline = 0;
		if ((fp = fopen(macfile[j], "r")) != NULL)
		{
			while (ret==0)
			{
				if (fgets (inputline, CMDLINESIZE, fp) == NULL) {
					if (ferror(fp)) {
						ERRLOG(0xE094);
						ret = 160; /*ferror*/
					}
					break;
				}
				++mline;
				len = strlen(inputline);
				if (len > 0 && inputline[len-1] == '\n')
					inputline[--len] = '\0';
				strip_blanks (STRIP_BLANKS_FROM_END, inputline, &len);

				if (len < 1 || inputline[0] == '#' || len < 5)
					continue;

				if (regexec(&regexp_header, inputline, 3, pmatch, 0) == 0) {
					iy = (pmatch[1].rm_so >= 0) ? (pmatch[1].rm_eo - pmatch[1].rm_so) : 0;
					if (iy <= 0 || iy >= (int)sizeof(name)) {
						ret = 161; /*string for key name very long, cannot be zerolength*/
						break;
					}
					strncpy(name, &inputline[pmatch[1].rm_so], (size_t)iy);
					name[iy] = '\0';

					if (iy > 0 && (ki = index_key_string(name)) < KLEN) {
						fkey = keys[ki].key_value;
						ptr = REALLOC((void *)macros, sizeof(MACROS) * (size_t)(MLEN+1));
						if (ptr == NULL) {
							ERRLOG(0xE003);
							ret = 162; /*realloc*/
							break;
						}
						macros = (MACROS *) ptr;
						macros[MLEN].fkey = fkey;
						macros[MLEN].name[0] = '\0';

						if (pmatch[2].rm_so >= 0) {
							ix = sizeof(macros[MLEN].name)-1;
							if (ix > pmatch[2].rm_eo - pmatch[2].rm_so)
								ix = pmatch[2].rm_eo - pmatch[2].rm_so;
							if (ix > 0) {
								strncpy(macros[MLEN].name, &inputline[pmatch[2].rm_so], (size_t)ix);
								macros[MLEN].name[ix] = '\0';
							} else {
								macros[MLEN].name[0] = '\0';
							}
						}

						macros[MLEN].mflag = 0;
						macros[MLEN].items = 0;
						macros[MLEN].maclist = NULL;
						mac = &macros[MLEN];	/* workpointer for the list */
						MLEN++;
					} else {
						ret = 163; /*key name not found*/
						break;
					}

				} else if ((mac != NULL) && (regexec(&regexp_item, inputline, 3, pmatch, 0) == 0)) {
					iy = (pmatch[1].rm_so >= 0) ? (pmatch[1].rm_eo - pmatch[1].rm_so) : 0;
					if (iy <= 0 || iy >= (int)sizeof(name)) {
						ret = 164; /*string for function name very long, cannot be zerolength*/
						break;
					}
					strncpy(name, &inputline[pmatch[1].rm_so], (size_t)iy);
					name[iy] = '\0';

					if (iy > 0 && (ti = index_func_fullname(name)) < TLEN) {
						ptr = REALLOC((void *)mac->maclist, sizeof(MACROITEMS)*(mac->items+1));
						if (ptr == NULL) {
							ERRLOG(0xE002);
							ret = 165; /*realloc*/
							break;
						}
						mac->maclist = (MACROITEMS *) ptr;
						mac->maclist[mac->items].m_index = ti;
						mac->maclist[mac->items].m_args[0] = '\0';

						if (pmatch[2].rm_so >= 0) {
							ix = sizeof(mac->maclist[mac->items].m_args)-1;
							if (ix > pmatch[2].rm_eo - pmatch[2].rm_so)
								ix = pmatch[2].rm_eo - pmatch[2].rm_so;
							if (ix > 0) {
								strncpy(mac->maclist[mac->items].m_args, &inputline[pmatch[2].rm_so], (size_t)ix);
								mac->maclist[mac->items].m_args[ix] = '\0';
							} else {
								mac->maclist[mac->items].m_args[0] = '\0';
							}
						}

						mac->mflag |= table[ti].tflag;
						mac->items++;
					} else {
						ret = 166; /*function name not found*/
						break;
					}

				} else {
					ret = 167; /*pattern match failure*/
					break;
				}
			}
			fclose(fp);
		}
		if (ret) {
			if (cnf.bootup) {
				tracemsg("processing [%s] failed (%d), line=%d", macfile[j], ret, mline);
				break;
			}
			fprintf(stderr, "eda: processing [%s] failed (%d), line=%d", macfile[j], ret, mline);
			if (ret==161) {
				fprintf(stderr, " : string for key name very long, max %u\n", (unsigned)sizeof(name));
			} else if (ret==163) {
				fprintf(stderr, " : key name not found\n");
			} else if (ret==164) {
				fprintf(stderr, " : string for function name very long, max %u\n", (unsigned)sizeof(name));
			} else if (ret==166) {
				fprintf(stderr, " : function name not found\n");
			} else if (ret==167) {
				fprintf(stderr, " : macrofile syntax failure\n");
			} else {
				fprintf(stderr, "\n");
			}
			break;
		}
		if (mline > 0) {
			break;
		}
	}

	regfree (&regexp_header);
	regfree (&regexp_item);

	return (ret);
}/* process_macrofile */

/*
* free up memory of macros
*/
void
drop_macros (void)
{
	int mi;
	if (macros) {
		for (mi=0; mi<MLEN; mi++) {
			if (macros[mi].maclist) {
				FREE(macros[mi].maclist);
				macros[mi].maclist = NULL;
			}
		}
		FREE(macros);
		macros = NULL;
	}
	MLEN = 0;
}

/*
** reload_macros - drop all macros and process macrofile again
*/
int
reload_macros (void)
{
	int ret=0;

	drop_macros();

	ret = process_macrofile(0);
	if (ret) {
		tracemsg ("loading macros failed (%d)", ret);
	} else {
		tracemsg ("%d macros loaded", MLEN);
	}

	return ret;
}

/*
* process project file
*/
int
process_project (int noconfig)
{
	char projfile[sizeof(cnf.myhome)+SHORTNAME];
	int pline=0, ret=0;
	FILE *fp;
	char str[CMDLINESIZE];
	char *ptr;
	int section = 0;	/* 1 for project config, 2 for project files */
	int len, length[3];	/* prefix patterns */
	int focus;

	if (noconfig) {
		return 0;
	}

	strncpy(projfile, cnf.myhome, sizeof(projfile));
	strncat(projfile, cnf.project, SHORTNAME-6);
	strncat(projfile, ".proj", 6);

	length[0] = strlen(PROJECT_HEADER);
	length[1] = strlen(PROJECT_FILES);
	length[2] = strlen(PROJECT_CHDIR);

	if ((fp = fopen(projfile, "r")) == NULL) {
		fprintf(stderr, "eda: cannot open project file [%s]\n", projfile);
		return 1;
	}

	pline = 0;
	while (ret==0)
	{
		if (fgets (str, CMDLINESIZE, fp) == NULL) {
			if (ferror(fp)) {
				ERRLOG(0xE093);
				ret = 8;
			}
			break;
		}
		++pline;
		len = strlen(str);
		if (len > 0 && str[len-1] == '\n')
			str[--len] = '\0';
		strip_blanks (STRIP_BLANKS_FROM_END|STRIP_BLANKS_FROM_BEGIN, str, &len);

		if (section == 0) {
			if ((len >= length[0]) && (strncmp(str, PROJECT_HEADER, (size_t)length[0]) == 0)) {
				section = 1;
				continue;
			}
		} else if (section == 1) {
			if ((len >= length[1]) && (strncmp(str, PROJECT_FILES, (size_t)length[1]) == 0)) {
				section = 2;
				continue;
			} else if ((len > length[2]) && (strncmp(str, PROJECT_CHDIR, (size_t)length[2]) == 0)) {
				ptr = str+length[2];
				len -= length[2];
				if (len > 1)
					strip_blanks (STRIP_BLANKS_FROM_BEGIN, ptr, &len);
				if (len > 1) {
					if (chdir(ptr)) {
						ret = 2;
						break;
					}
				} else {
					ret = 1;
					break;
				}
				/* after chdir(ptr) ... save current workdir */
				if (getcwd(cnf._pwd, sizeof(cnf._pwd)-1) == NULL) {
					/* fallback */
					strncpy(cnf._pwd, ptr, sizeof(cnf._pwd));
					cnf._pwd[sizeof(cnf._pwd)-1] = '\0';
				}
				cnf.l1_pwd = strlen(cnf._pwd);
				/**/
				if (read_extcmd_line ("pwd", 1, cnf._altpwd, sizeof(cnf._altpwd))) {
					/* fallback */
					cnf._altpwd[0] = '\0';
					cnf.l2_altpwd = 0;
				} else {
					cnf.l2_altpwd = strlen(cnf._altpwd);
					if (cnf.l1_pwd == cnf.l2_altpwd && strncmp(cnf._pwd, cnf._altpwd, cnf.l1_pwd) == 0) {
						cnf._altpwd[0] = '\0';
						cnf.l2_altpwd = 0;
					}
				}
				/* do not call set() */
				continue;
			}
		}
		if (len == 0 || str[0] == '#') {
			continue;
		}

		if (section == 1) {
			ret = set (str);
			if (ret) ret += 100;
		} else if (section == 2) {
			/* settings after a filename: focus */
			if (strncmp(str, "focus=", 6) == 0) {
				if (cnf.ring_size > 0 && CURR_FILE.fflag & FSTAT_OPEN) {
					focus = strtol(str+6, NULL, 10);
					if (focus > 0) /* cnf.maxy is undefined at this point! */
						CURR_FILE.focus = focus;
				}
				continue;
			}
			/**/
			ret = simple_parser(str, SIMPLE_PARSER_JUMP);
			if (ret) ret += 200;
		}
	}
	fclose(fp);

	if (ret) {
		fprintf(stderr, "eda: processing [%s] failed (ret=%d), line=%d\n", projfile, ret, pline);
	}
	return (ret);
}/* process_project */

/*
** save_project - save project file in the ~/.eda/ directory,
**	last used project name can be omitted
*/
int
save_project (const char *projectname)
{
	int ret=0, ri=0, section=0;
	char projfile[sizeof(cnf.myhome)+SHORTNAME];
	FILE *fp=NULL;
	char *data=NULL;
	unsigned datasize=0, dlen=0, als=0;
	char str[CMDLINESIZE];
	unsigned len=0, length[3];

	if (projectname[0] == '\0' && cnf.project[0] == '\0') {
		tracemsg("no project name defined");
		return (1);
	}

	/* access ~/.eda checked in main.c */

	strncpy(projfile, cnf.myhome, sizeof(projfile));
	if (projectname[0] != '\0') {
		strncat(projfile, projectname, SHORTNAME-6);
	} else {
		strncat(projfile, cnf.project, SHORTNAME-6);
	}
	strncat(projfile, ".proj", 6);

	length[0] = strlen(PROJECT_HEADER);
	length[1] = strlen(PROJECT_FILES);
	length[2] = strlen(PROJECT_CHDIR);

	if ((fp = fopen(projfile, "r")) != NULL) {
		fseek(fp, 0, SEEK_END);
		datasize = ftell(fp);
		als = ALLOCSIZE(datasize+1);
		if ((data = (char *) MALLOC(als)) == NULL) {
			ERRLOG(0xE02A);
			tracemsg ("failed to save project");
			fclose(fp);
			return (-1);
		}
		fseek(fp, 0, SEEK_SET);
		memset(str, 0, sizeof(str));
		while (ret==0) {
			if (fgets (str, sizeof(str)-1, fp) == NULL) {
				if (ferror(fp)) {
					ERRLOG(0xE092);
					ret = 8;
				}
				break;
			}
			len = strlen(str);
			if (section == 0) {
				if (strncmp(str, PROJECT_HEADER, length[0]) == 0) {
					section = 1;
					continue;
				}
			} else if (section == 1) {
				if (strncmp(str, PROJECT_FILES, length[1]) == 0) {
					section = 2;
					break; /* finished */
				} else if (strncmp(str, PROJECT_CHDIR, length[2]) == 0) {
					continue;
				}
			}
			if (section && len > 1) {
				strncpy(data+dlen, str, len+1);
				dlen += len;
			}
		}
		*(data+dlen) = '\0';
		fclose(fp);
	}

	if ((fp = fopen(projfile, "w")) == NULL) {
		tracemsg ("cannot write %s project file", projfile);
		ret = -1;
	} else {
		fprintf(fp, "%s\n%s %s\n", PROJECT_HEADER, PROJECT_CHDIR, cnf._pwd);
		if (dlen > 0) {
			if (fwrite (data, sizeof(char), dlen, fp) != dlen) {
				ERRLOG(0xE0A5);
				ret = 2;
			}
		}
		if (!ret) {
			fprintf(fp, "\n%s\n", PROJECT_FILES);
		}
		memset(str, 0, sizeof(str));
		ri = cnf.ring_curr;
		do {
			if ((cnf.fdata[ri].fflag & FSTAT_OPEN) &&
			((cnf.fdata[ri].fflag & (FSTAT_SPECW | FSTAT_SCRATCH)) == 0))
			{
				snprintf(str, sizeof(str)-1, "%s:%d\nfocus=%d\n",
					cnf.fdata[ri].fname, cnf.fdata[ri].lineno, cnf.fdata[ri].focus);
				len = strlen(str);
				if (fwrite (str, sizeof(char), len, fp) != len) {
					ERRLOG(0xE0A4);
					ret = 2;
					break;
				}
			}
			ri = (ri<RINGSIZE-1) ?  ri+1 : 0;
		} while (ri != cnf.ring_curr);
		fclose(fp);
	}
	FREE(data);
	data = NULL;

	if (ret==0) {
		if (projectname[0] != '\0') {
			strncpy(cnf.project, projectname, sizeof(cnf.project));
			cnf.project[sizeof(cnf.project)-1] = '\0';
		}
		tracemsg ("project %s saved", cnf.project);
	}

	return (ret);
}/* save_project */

/*
** load_rcfile - open ~/.eda/edarc resource file,
**	changes in the file are activated at next run
*/
int
load_rcfile (void)
{
	char rcfile[sizeof(cnf.myhome)+SHORTNAME];
	int ret=0;

	strncpy(rcfile, cnf.myhome, sizeof(rcfile));
	strncat(rcfile, "edarc", SHORTNAME);
	ret = add_file(rcfile);

	return (ret);
}

/*
** load_keyfile - open ~/.eda/edakeys file with user defined symbolic key names,
**	changes in the file are activated at next run
*/
int
load_keyfile (void)
{
	char keyfile[sizeof(cnf.myhome)+SHORTNAME];
	int ret=0;

	strncpy(keyfile, cnf.myhome, sizeof(keyfile));
	strncat(keyfile, "edakeys", SHORTNAME);
	ret = add_file(keyfile);

	return (ret);
}

/*
** load_macrofile - load_macrofile - open ~/.eda/edamacro file with user defined macros,
**	changes in the file are activated at next run
*/
int
load_macrofile (void)
{
	char macfile[sizeof(cnf.myhome)+SHORTNAME];
	int ret=0;

	strncpy(macfile, cnf.myhome, sizeof(macfile));
	strncat(macfile, "edamacro", SHORTNAME);
	ret = add_file(macfile);

	return (ret);
}

/* pretty print key names into given char array
*/
int
pretty_print_key_names(int ki, char *buff, int size)
{
	int len;
	len = strlen(buff);

	if (len+35 > size) {
		strncat(buff, "...", 10);
		return 1;
	}

	if ((strncmp(keys[ki].key_string, "KEY_F", 5) == 0) && 
	(keys[ki].key_string[5] >= '0' && keys[ki].key_string[5] <= '9')) {
		snprintf(&buff[len], 30, "%s ", keys[ki].key_string + 4);
	} else if (strncmp(keys[ki].key_string, "KEY_C_", 6) == 0) {
		snprintf(&buff[len], 30, "Ctrl-%s ", keys[ki].key_string + 6);
	} else if (strncmp(keys[ki].key_string, "KEY_M_", 6) == 0) {
		snprintf(&buff[len], 30, "Alt-%s ", keys[ki].key_string + 6);
	} else if (strncmp(keys[ki].key_string, "KEY_S_M_", 8) == 0) {
		snprintf(&buff[len], 30, "Alt-Shift-%s ", keys[ki].key_string + 8);
	} else if (strncmp(keys[ki].key_string, "KEY_S_", 6) == 0) {
		snprintf(&buff[len], 30, "Shift-%s ", keys[ki].key_string + 6);
	} else {
		snprintf(&buff[len], 30, "%s ", keys[ki].key_string);
	}

	return 0;
}

/*
** show_commands - show table of commands with keyboard shortcuts
*/
int
show_commands (void)
{
	int ret=1;
	LINE *lp=NULL;
	char circle_line[1024];
	char key_buff[90];
	char name_buff[30];
	int ti, ki, i, j, mi;

	/* open or reopen? */
	ret = scratch_buffer("*cmds*");
	if (ret) {
		return (ret);
	}
	/* cnf.ring_curr is set now -- CURR_FILE and CURR_LINE alive */
	if (CURR_FILE.num_lines > 0) {
		clean_buffer();
	}
	CURR_FILE.fflag |= FSTAT_SPECW;
	CURR_FILE.fflag &= ~FSTAT_NOEDIT;	/* temporary */

	ret = type_text("\n\
command name          function name         keyboard shortcut\n\
--------------------  --------------------  -------------------------\n");

	if (!ret) {

		CURR_LINE = CURR_FILE.bottom->prev;
		memset(circle_line, '\0', sizeof(circle_line));
		memset(name_buff, '\0', sizeof(name_buff));
		memset(key_buff, '\0', sizeof(key_buff));

		for (ti=0; ret == 0 && ti < TLEN; ti++) {

			if (table[ti].minlen == -1 && table[ti].fkey == -1) {
				/* only for macros */
				continue;
			}

			/* command name with mandatory or optional arguments */
			name_buff[0] = '\0';
			if (table[ti].minlen >= 1) {
				i = 0;
				for (j=0; i < 20 && table[ti].name[j] != '\0'; j++) {
					if (i == table[ti].minlen)
						name_buff[i++] = '.';
					name_buff[i++] = table[ti].name[j];
				}
				name_buff[i++] = '\0';
				if (table[ti].tflag & TSTAT_ARGS) {
					strncat(name_buff, ((table[ti].tflag & TSTAT_OPTARG) ? " [<arg>]" : " <arg>"), 10);
				}
			} else {
				strncpy(name_buff, "n/a", 10);
			}

			/* list of keys, pretty printing, space limitation */
			key_buff[0] = '\0';
			if (table[ti].fkey == -1) {
				strncpy(key_buff, "n/a", 10);
			} else {
				for (ki=0; ki < KLEN; ki++) {
					if (keys[ki].table_index == ti) {
						if (pretty_print_key_names(ki, key_buff, sizeof(key_buff)))
							break;
					}
				}
				if (key_buff[0] == '\0') {
					strncat(key_buff, "none", 10);
				}
			}

			snprintf(circle_line, sizeof(circle_line)-1, "%-20s  %-20s  %s\n",
				name_buff, table[ti].fullname, key_buff);

			ret = type_text(circle_line);
		}
	}

	if (!ret && macros) {

		ret = type_text("\n\
macro key             macro name\n\
--------------------  ----------------------------------------\n");

		for (mi=0; ret == 0 && mi < MLEN; mi++) {
			key_buff[0] = '\0';
			ki = index_key_value( macros[mi].fkey );
			if (ki >= 0 && ki < KLEN && ki != RES_KLEN) { // KEY_NONE index is RES_KLEN
				pretty_print_key_names(ki, key_buff, sizeof(key_buff));
			} else {
				strncpy(key_buff, "none", 10);
			}

			name_buff[0] = '\0';
			if (macros[mi].name[0] == '\0') {
				strncpy(name_buff, "n/a", 10);
			} else {
				strncpy(name_buff, macros[mi].name, 20);
				if (macros[mi].mflag & TSTAT_ARGS) {
					strncat(name_buff, ((macros[mi].mflag & TSTAT_OPTARG) ? " [<arg>]" : " <arg>"), 10);
				}
			}

			snprintf(circle_line, sizeof(circle_line)-1, "%-20s  %s\n",
				key_buff, name_buff);

			ret = type_text(circle_line);
		}
	}

	if (ret==0) {
		CURR_LINE = CURR_FILE.top->next;
		/* after type_text/insert_chars clean LSTAT_CHANGE */
		for (lp=CURR_LINE; TEXT_LINE(lp); lp=lp->next) {
			lp->lflag &= ~LSTAT_CHANGE;
		}
		CURR_FILE.lineno = 1;
		update_focus(FOCUS_ON_2ND_LINE, cnf.ring_curr);
		go_home();
		CURR_FILE.fflag &= ~FSTAT_CHANGE;
		/* disable inline editing, adding or removing lines */
		CURR_FILE.fflag |= (FSTAT_NOEDIT | FSTAT_NOADDLIN | FSTAT_NODELIN);
	} else {
		ret |= drop_file();
	}

	return (ret);
}

/*
* save_clhistory - save command line history (latest first in the file)
*/
void
save_clhistory (void)
{
	char histfile[sizeof(cnf.myhome)+SHORTNAME];
	FILE *fp=NULL;
	CMDLINE *runner;
	char str[1024];
	unsigned len=0;
	int items=0;

	runner = cnf.clhistory;
	if (runner == NULL)
		return;

	strncpy(histfile, cnf.myhome, sizeof(cnf.myhome));
	strncat(histfile, "history", SHORTNAME);

	if ((fp = fopen(histfile, "w")) != NULL) {
		while (runner->prev != NULL) {
			runner = runner->prev;
		}
		memset(str, 0, sizeof(str));
		/* the most recent item is useless */
		while (runner->next != NULL) {
			if (runner->len > 0) {
				snprintf(str, (size_t)runner->len+2, "%s\n", runner->buff);
				len = strlen(str);
				if (fwrite (str, sizeof(char), len, fp) != len) {
					ERRLOG(0xE0A3);
					break;
				}
				items++;
			}
			runner = runner->next;
		}
		fclose(fp);
	}

	return;
}/* save_clhistory */

/*
* load_clhistory - load command line history (push back them in the list)
*/
void
load_clhistory (void)
{
	char histfile[sizeof(cnf.myhome)+SHORTNAME];
	FILE *fp=NULL;
	char str[1024];
	int len=0;
	int items=0;

	memset(histfile, 0, sizeof(histfile));
	strncat(histfile, cnf.myhome, sizeof(cnf.myhome));
	strncat(histfile, "history", SHORTNAME);

	if ((fp = fopen(histfile, "r")) != NULL) {
		memset(str, 0, sizeof(str));
		while (fgets (str, CMDLINESIZE-1, fp) != NULL) {
			len = strlen(str);
			if (len > 1) {
				if (str[len-1] == '\n')
					str[--len] = '\0';
				/* not empty */
				clhistory_push(str, len);
				items++;
			}
		}
		fclose(fp);
	}

	return;
}/* load_clhistory */
