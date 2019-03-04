/*
* util.c
* various general tools, globbing, file/dir name manipulation
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
#include <syslog.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glob.h>		/* glob, globfree */
#include <fcntl.h>
#include <errno.h>
#include "main.h"
#include "proto.h"

/* global config */
extern CONFIG cnf;

/*
 * get rest of line, optionally strip newline and symlink part
 */
int
get_rest_of_line (char **buffer, int *bufsize, const char *dataline, int from, int dlen)
{
	char *word = *buffer;
	int wl = *bufsize;
	char *next=NULL;
	int offset=0, ret=0;

	if (dataline == NULL || dlen < 0 || dlen <= from) {
		word = NULL;
		return (-1);
	}

	ret = csere (&word, &wl, 0, 0, dataline+from, dlen-from);
	if (ret || word == NULL) {
		return (-1);
	}

	/* strip newline */
	if (wl > 0 && word[wl-1] == '\n') {
		word[--wl] = '\0';
	}

	/* strip symlink, return real name */
	next = strstr(word, " -> ");
	if (next != NULL) {
		offset = next - word + 4;
		(void) csere (&word, &wl, 0, offset, "", 0);
	}

	/* give pointers back */
	*buffer = word;
	*bufsize = wl;

	return (0);
}

/*
 * internal utility for TILDE expansion and validating the directory part of path
 */
int
glob_tilde_expansion (char *path, unsigned maxsize)
{
	int ret=1;
	glob_t globbuf;
	char fname[FNAMESIZE];
	char dirname[FNAMESIZE];
	unsigned len, flen;

	if (path == NULL || maxsize < 10)
		return -1;

	mybasename(fname, path, FNAMESIZE);
	flen = strlen(fname);
	mydirname(dirname, path, FNAMESIZE);

	ret = glob(dirname, GLOB_TILDE | GLOB_MARK, NULL, &globbuf);
	if (ret == 0 && globbuf.gl_pathc == 1) {
		len = strlen(globbuf.gl_pathv[0]);
		if (maxsize > len + flen) {
			//
			// remove boring "./" prefix from relative path, except empty fname
			//
			memset(path, '\0', maxsize);
			if (globbuf.gl_pathv[0][0] == '.' && globbuf.gl_pathv[0][1] == '/' && fname[0] != '\0') {
				strncpy(path, globbuf.gl_pathv[0]+2, len);
			} else {
				strncpy(path, globbuf.gl_pathv[0], len);
			}
			strncat(path, fname, flen);
			ret=0;
		} else {
			/* too long */
			ret=3;
		}
	} else {
		/* glob failed */
		ret=2;
	}
	globfree(&globbuf);

	return (ret);
}

/*
 * internal utility to get names from filesysten matching pattern,
 * write back common characters and if there is no exact match, return the choices
 */
int
glob_tab_expansion (char *path, unsigned maxsize, char **choices)
{
	int ret=1;
	glob_t globbuf;
	unsigned len, i, j, end, request, items, als;
	int copy_back_common_bytes=0;
	char *qq;

	if (path == NULL || maxsize < 10)
		return -1;

	*choices = NULL;
	if ((strchr(path, '*') == NULL) && (strchr(path, '?') == NULL)) {
		strncat(path, "*", 2);
		copy_back_common_bytes = 1;
	}

	ret = glob(path, GLOB_TILDE | GLOB_MARK, NULL, &globbuf);
	if (ret == 0 && globbuf.gl_pathc >= 1) {
		ret=0;
		if (globbuf.gl_pathc == 1) {
			/* exact match */
			len = strlen(globbuf.gl_pathv[0]);
			if (len < maxsize) {
				strncpy(path, globbuf.gl_pathv[0], maxsize-1);
				path[len] = '\0';
			} else {
				/* too long */
				ret=3;
			}
		} else {
			/* partial match */
			if (copy_back_common_bytes) {
				end = 0;
				for (j=0; j < maxsize && !end; j++) {
					path[j] = globbuf.gl_pathv[0][j];
					for (i=0; i < globbuf.gl_pathc; i++) {
						if (globbuf.gl_pathv[i][j] == '\0' ||
						    globbuf.gl_pathv[i][j] != path[j]) {
						        end = j;
							break;
						}
					}
				}
				path[end++] = '\0'; /* end < maxsize */
			}
			/* create list of choices (1000++ bytes), items count */
			request = 1;
			for (i=0; i < globbuf.gl_pathc && request < 1000; i++)
				request += strlen(globbuf.gl_pathv[i]) +1;
			items = i;
			als = ALLOCSIZE(request+4); /* for optional 3+1 */
			qq = (char *) MALLOC(als);
			if (qq == NULL) {
				ERRLOG(0xE031);
				ret = 4;
			} else {
				*choices = qq;	/* save for return, to be freed by caller */
				qq[0]='\0';
				for (i=0; i < items; i++) {
					strncat(qq, globbuf.gl_pathv[i], request);
					strncat(qq, " ", 2);
				}
				if (items < globbuf.gl_pathc)
					strncat(qq, "...", 3); /* optional 3+1 bytes */
			}
		}
	} else {
		// if (globbuf.gl_pathc >= 1) /* glob failed */
		ret=2;
	}
	globfree(&globbuf);

	return (ret);
}

/* ---------------------------------------------------------- */

/*
 * strip blanks (space, tab) from strings:
 * operation bits: strip from end, strip from begin, squeeze internal blanks to space
 */
int
strip_blanks (int operation, char *str, int *length)
{
	int ret=0;
	int i,j;

	if (str == NULL || *length <= 1) {
		return -1;
	}

	if (operation & STRIP_BLANKS_FROM_END) {
		i = *length;
		/* strip blanks, end */
		while (i > 0 && (str[i-1]==' ' || str[i-1]=='\t')) {
			str[--i] = '\0';
		}
		*length = i;
	}
	if (operation & STRIP_BLANKS_FROM_BEGIN) {
		i = 0;
		/* strip blanks, begin */
		while (i < *length && (str[i]==' ' || str[i]=='\t')) {
			i++;
		}
		if (i > 0) {
			for (j=0; j+i <= *length; j++) {
				str[j] = str[j+i];
			}
			*length -= i;
		}
	}
	if (operation & STRIP_BLANKS_SQUEEZE) {
		/* squeeze internal blanks */
		for (i=0; i < *length; i++) {
			if (str[i]=='\t')
				str[i] = ' ';
		}
		j=0;
		if (*length > 0) {
			for (i=j=1; str[i] != '\0'; i++) {
				if (str[i] == ' ') {
					if (str[j-1] != ' ') {
						/* copy first blank */
						str[j++] = str[i];
					}
				} else {
					/* copy non-blank */
					str[j++] = str[i];
				}
			}
		}
		str[j] = '\0';
		*length = j;
	}

	return (ret);
} /* strip_blanks */

/*
 * count prefix blanks in buffer
 */
int
count_prefix_blanks (const char *buff, int llen)
{
	int blanks=0;

	while ((blanks < llen) && (buff[blanks] == ' ' || buff[blanks] == '\t')) {
		blanks++;
	}

	/* reset, if only white chars in the line */
	if (!(buff[blanks] > ' ')) {
		blanks = 0;
	}

	return (blanks);
} /* count_prefix_blanks */

/*
 * yes/no tool
 */
int
yesno (const char *str)
{
	int ret = (-1);

	if (str[1] == '\0') {
		if (str[0]=='1' || str[0]=='y')
			ret=1;
		else if (str[0]=='0' || str[0]=='n')
			ret=0;

	} else if (str[2] == '\0') {
		if (str[0]=='o' && str[1]=='n')
			ret=1;
		else if (str[0]=='n' && str[1]=='o')
			ret=0;

	} else if (str[3] == '\0') {
		if (str[0]=='y' && str[1]=='e' && str[2]=='s')
			ret=1;
		else if (str[0]=='o' && str[1]=='f' && str[2]=='f')
			ret=0;
	}

	return (ret);
} /* yesno */

/*
 * return the filetype (by file extension)
 */
FXTYPE
fname_ext (const char *cfname)
{
	int i=0, j = -1;
	char ext[20];
	FXTYPE fxtype = TEXT_FILETYPE;

	for (i=0; i < FNAMESIZE-1 && cfname[i] != '\0'; i++) {
		if (cfname[i] == '.') {
			j = 0;
		} else if (j>=0 && j<10) {
			if (cfname[i] >= 'A' && cfname[i] <= 'Z') {
				ext[j++] = cfname[i] - 'A' +'a';
			} else {
				ext[j++] = cfname[i];
			}
		}
	}
	if (j<0) j=0;
	ext[j] = '\0';

	if (j==1 && (ext[0]=='c' || ext[0]=='h')) {
		fxtype = C_FILETYPE;
	} else if (j==3 && ext[0]=='c' && ext[1]=='p' && ext[2]=='p') {
		fxtype = C_FILETYPE;
	} else if (j==2 && ext[0]=='p' && ext[1]=='l') {
		fxtype = PERL_FILETYPE;
	} else if (j==2 && ext[0]=='p' && ext[1]=='y') {
		fxtype = PYTHON_FILETYPE;
	} else if (j==2 && ext[0]=='s' && ext[1]=='h') {
		fxtype = SHELL_FILETYPE;
	} else {
		fxtype = TEXT_FILETYPE;
	}

	return (fxtype);
} /* fname_ext */

/*
 * helper function to get index of slash, reverse or direct, first or last
 */
int
slash_index (const char *string, int strsize, int from, int reverse, int get_first)
{
	int i=0, idx = -1;

	if (from > strsize || from < 0)
		return (-1);

	if (reverse) {
		for (i = from; i >= 0; i--) {
			if (string[i] == '/') {
				idx = i;
				if (get_first) break;
			}
		}
	} else {
		for (i = from; string[i] != '\0' && i < strsize; i++) {
			if (string[i] == '/') {
				idx = i;
				if (get_first) break;
			}
		}
	}

	return (idx);
}

/*
 * helper function to get token in given string by delimiter(s),
 * return length of token, but
 * length can be 0 if no delimiters found
 * index_rest is the next index after delimiters, and maybe -1
 */
int
parse_token (const char *inputstr, const char *delim, int *index_rest)
{
	int length=0;
	int i=0, j=0, k=0;

	*index_rest = -1;

	for (i=0; inputstr[i] != '\0'; i++) {
		for (j=0; delim[j] != '\0'; j++) {
			if (inputstr[i] == delim[j]) {
				*index_rest = i;
				break; /* one delimiter found */
			}
		}
		if (delim[j] != '\0') {
			break;
		}
	}
	length = i;

	if (delim[j] != '\0') {
		/* squeeze delimiters */
		for (k=length; inputstr[k] != '\0'; k++) {
			for (j=0; delim[j] != '\0'; j++) {
				if (inputstr[k] == delim[j]) {
					*index_rest = k;
					break; /* another delimiter found */
				}
			}
			if (delim[j] == '\0') {
				/* not a delimiter */
				break;
			}
		}
		if (inputstr[k] == '\0') {
			*index_rest = -1;
		} else {
			*index_rest = k;
		}
	}

	return (length);
}

/*
 * return the base part of filename path (word after last slash)
 * tilde belongs to directory part and simple "." and ".." is dirname
 * do not stat the result here, simple "foo" is filename, caller must check it
 */
void
mybasename (char *outpath, const char *inpath, int outbuffsize)
{
	int ilen=0, last=0;

	if (outbuffsize < 1) {
		return;
	}
	outpath[0] = '\0';
	if (inpath != NULL && inpath[0] != '\0') {
		ilen = strlen(inpath);
		ilen = MIN(ilen, outbuffsize-1);
		if (ilen >= 2 && inpath[ilen-2] == '/' && inpath[ilen-1] == '.') {
			/* slash-dot at the end */
			return;
		} else if (ilen > 1 && inpath[ilen-1] == '/') {
			/* slash at the end */
			return;
		}
		last = slash_index (inpath, ilen, 0, SLASH_INDEX_FWD, SLASH_INDEX_GET_LAST);
		if (last == -1) {
			if (inpath[0] == '~') {
				outpath[0] = '\0';
			} else if (ilen==1 && inpath[0] == '.') {
				outpath[0] = '\0';
			} else if (ilen==2 && inpath[0] == '.' && inpath[1] == '.') {
				outpath[0] = '\0';
			} else {
				strncpy(outpath, &inpath[0], (size_t)ilen);
				outpath[ilen] = '\0';
			}
		} else if (ilen-last > 1) {
			/* not empty */
			strncpy(outpath, &inpath[last+1], (size_t)(ilen-last-1));
			outpath[ilen-last-1] = '\0';
		}
	}

	return;
}

/*
 * return the dir part of filename path, before the last slash
 * but keep the "/" string
 * and return "." instead of empty string
 * tilde belongs to directory part and simple "." and ".." is dirname
 * do not stat the result here, simple "foo" is filename, caller must check it
 */
void
mydirname (char *outpath, const char *inpath, int outbuffsize)
{
	int ilen=0, last=0;

	if (outbuffsize < 2) {
		return;
	}
	outpath[0] = '.';
	outpath[1] = '\0';
	if (inpath != NULL && inpath[0] != '\0') {
		ilen = strlen(inpath);
		ilen = MIN(ilen, outbuffsize-1);
		last = slash_index (inpath, ilen, 0, SLASH_INDEX_FWD, SLASH_INDEX_GET_LAST);
		if (last == -1) {
			if (inpath[0] == '~') {
				strncpy(outpath, &inpath[0], (size_t)ilen);
				outpath[ilen] = '\0';
			} else if (ilen==1 && inpath[0] == '.') {
				outpath[0] = '.';
				outpath[1] = '\0';
			} else if (ilen==2 && inpath[0] == '.' && inpath[1] == '.') {
				outpath[0] = '.';
				outpath[1] = '.';
				outpath[2] = '\0';
			} else {
				/* do not return empty outpath */
				outpath[0] = '.';
				outpath[1] = '\0';
			}
		} else {
			while (last > 1 && inpath[last-1] == '/') {
				last--;
			}
			if (last == 0) {
				/* keep special outpath */
				outpath[0] = '/';
				outpath[1] = '\0';
			} else if (last+2 == ilen && inpath[last+1] == '.') {
				/* slash-dot at the end */
				strncpy(outpath, &inpath[0], (size_t)ilen);
				outpath[ilen] = '\0';
			} else if (last > 0) {
				/* not empty */
				strncpy(outpath, &inpath[0], (size_t)last);
				outpath[last] = '\0';
			}
		}
	}

	return;
}

/*
 * return the canonical (absolute) path
 */
char *
canonicalpath (const char *path)
{
	char *dir = NULL;
	int ilen=0, size=0;
	int runner = 0, prev = 0;

	if (path == NULL) {
		return NULL;
	}

	/* dup */
	ilen = strlen(path);
	if (csere (&dir, &size, 0, 0, &path[0], ilen)) {
		FREE(dir); dir = NULL;
		return NULL;
	}

	/* replace tilde or insert pwd if not absolute path */
	if (ilen == 0) {
		/* replace empty dir with PWD */
		if (csere (&dir, &size, 0, 0, cnf._pwd, (int)cnf.l1_pwd)) {
			FREE(dir); dir = NULL;
			return NULL;
		}
	} else if (dir[0] == '~') {
		if (ilen == 1 || (ilen > 1 && dir[1] == '/')) {
			/* replace tilde with HOME */
			if (csere (&dir, &size, 0, 1, cnf._home, (int)cnf.l1_home)) {
				FREE(dir); dir = NULL;
				return NULL;
			}
		}
	} else if (dir[0] != '/') {
		/* insert PWD+"/" into the dir */
		if (csere (&dir, &size, 0, 0, "/", 1) || csere (&dir, &size, 0, 0, cnf._pwd, (int)cnf.l1_pwd)) {
			FREE(dir); dir = NULL;
			return NULL;
		}
	}

	/* simple replacement (dot-slash) and (slash-slash) */
	runner = 0;
	while (runner < size) {
		if (runner+1 < size && dir[runner+1] == '.' && dir[runner+2] == '/') {
			(void) csere (&dir, &size, runner, 2, "", 0);
		} else if (dir[runner] == '/' && dir[runner+1] == '/') {
			(void) csere (&dir, &size, runner, 1, "", 0);
		} else {
			runner = slash_index (dir, size, runner+1, SLASH_INDEX_FWD, SLASH_INDEX_GET_FIRST);
			if (runner < 0) {
				break;
			}
		}
	}

	/* advanced replacement: "/word/.." -> "/" */
	runner = 0;
	while (runner+2 < size) {
		if (dir[runner] == '/' && dir[runner+1] == '.' && dir[runner+2] == '.' &&
			(dir[runner+3] == '\0' || dir[runner+3] == '/'))
		{
			if (runner == 0) {
				FREE(dir); dir = NULL;
				size = 0;
				break; /* invalid path */
			}
			prev = slash_index (dir, size, runner-1, SLASH_INDEX_REVERSE, SLASH_INDEX_GET_FIRST);
			if (prev == -1) {
				FREE(dir); dir = NULL;
				size = 0;
				break; /* invalid path */
			}
			if (prev == 0 && dir[runner+3] == '\0') {
				(void) csere (&dir, &size, prev+1, runner+2-prev, "", 0);
			} else {
				(void) csere (&dir, &size, prev, runner+3-prev, "", 0);
			}
			runner = prev;
		} else {
			runner = slash_index (dir, size, runner+1, SLASH_INDEX_FWD, SLASH_INDEX_GET_FIRST);
			if (runner < 0) break;
		}
	}

	/* finish */
	if (size >= 2 && dir[size-2] == '/' && dir[size-1] == '.') {
		if (size == 2) {
			/* keep the only slash */
			dir[--size] = '\0';
		} else {
			/* slash-dot */
			size -= 2;
			dir[size] = '\0';
		}
	}
	if (size > 1 && dir[size-1] == '/') {
		dir[--size] = '\0';
	}

	return (dir);
}

/*
 * break down input[] into array of strings (words without whitechars),
 * each element in args[] is a pointer into the original input string;
 * '\0' replaces first whitechar delimiter;
 * return number of args (limited by MAXARGS) and the last element is NULL;
 * keep '...' and "..." patterns in the string as is, the first rules
 * no nesting of same type, remove separator
 * backspace handling only for: backspace, quote, space, tab
 */
int
parse_args (char *input, char **args)
{
#define NONE_WORD	0
#define BASE_WORD	1
#define WORD_IN_SQUOTE	2
#define WORD_IN_DQUOTE	4

	int nb_args=0, mode=NONE_WORD;
	char *tp, *sp, bsp;

	if (input == NULL) {
		args[0] = NULL;
		return (0);
	}
	tp = sp = &input[0];
	bsp = 0;

	while (*sp != '\0' && nb_args < MAXARGS-1) {
		if ((bsp == 1) && (*sp == '\\' || *sp == '\'' || *sp == '\"' || *sp == ' ' || *sp == '\t')) {
			if (mode == NONE_WORD) {
				args[nb_args++] = tp;
				mode = BASE_WORD;
			}
			*tp++ = *sp;
		} else {
			if (*sp == '\\') {
				;// ignore

			} else if (*sp == '\'') {
				if (mode == WORD_IN_DQUOTE) {
					;// keep mode
				} else if (mode == NONE_WORD) {
					args[nb_args++] = tp;
					mode = WORD_IN_SQUOTE;
				} else if (mode == BASE_WORD) {
					mode = WORD_IN_SQUOTE;
				} else {
					mode = BASE_WORD;
				}

			} else if (*sp == '\"') {
				if (mode == WORD_IN_SQUOTE) {
					;// keep mode
				} else if (mode == NONE_WORD) {
					args[nb_args++] = tp;
					mode = WORD_IN_DQUOTE;
				} else if (mode == BASE_WORD) {
					mode = WORD_IN_DQUOTE;
				} else {
					mode = BASE_WORD;
				}

			} else if (*sp == ' ' || *sp == '\t') {
				if (mode == WORD_IN_SQUOTE || mode == WORD_IN_DQUOTE) {
					*tp++ = *sp;
				} else if (mode == BASE_WORD) {
					mode = NONE_WORD;
					*tp++ = '\0';
				}
			} else { // regular chars except backspace, apostrophe, quote, space, tab
				if (mode == NONE_WORD) {
					args[nb_args++] = tp;
					mode = BASE_WORD;
				}
				*tp++ = *sp;
			}
		}
		bsp = (*sp == '\\') ? !bsp : 0;
		sp++;
	}

	*tp++ = '\0';
	args[nb_args] = NULL;
	return (nb_args);
} /* parse_args */

/* check if progname is running, returns PID or -1
* uses the /proc interface
*/
int
pidof (const char *progname)
{
	const char *procdir = "/proc/";
	DIR *dir = NULL;
	struct dirent *de = NULL;
	char fullname[FNAMESIZE];
	int fd = 0;
	char entry[FNAMESIZE+1];
	int in;
	unsigned long blksize = FNAMESIZE;
	unsigned pn_length = 0, entry_length = 0;
	int retval = -1, pid = 0;

	dir = opendir (procdir);
	if (dir == NULL) {
		return retval;
	}
	pn_length = strlen(progname);

	while ((de = readdir (dir)) != NULL) {
		pid = atoi(de->d_name);
		if (pid < 100) {
			continue;
		}

		/* check /proc/$PID/cmdline file
		*/
		strncpy(fullname, procdir, 10);
		strncat(fullname, de->d_name, 10);
		strncat(fullname, "/cmdline", 10);

		fd = open(fullname, O_RDONLY | O_NONBLOCK);
		if (fd != -1) {
			in = read(fd, entry, blksize);
			if (in > 0) {
				entry[in] = '\0';
				entry_length = strlen(entry);
				if (entry_length == pn_length) {
					if (strncmp(progname, entry, pn_length) == 0) {
						retval = pid;
					}
				} else if (entry_length > pn_length && entry[entry_length-pn_length-1] == '/') {
					if (strncmp(progname, &entry[entry_length-pn_length], pn_length) == 0) {
						retval = pid;
					}
				}
			}
			close(fd);
		}

		if (retval > 0) {
			break;
		}
	}
	closedir (dir);

	return retval;
}

/* check if pid is alive
* uses the /proc interface
*/
int
is_process_alive (int pid)
{
	char fullname[FNAMESIZE];
	struct stat test;
	int retval = 0;

	if (pid > 0) {
		memset(fullname, 0, sizeof(fullname));
		snprintf(fullname, sizeof(fullname)-1, "/proc/%d/cmdline", pid);
		if (stat (fullname, &test) == 0) {
			if (S_ISREG(test.st_mode)) {
				retval = 1;
			}
		}
	}

	return retval;
}
