/*
* util.c
* various general tools, globbing, file/dir name manipulation
*
* Copyright 2003-2015 Attila Gy. Molnar
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
 * get last word from the line
 */
char *
get_last_word (const char *dataline, int len)
{
	char *word = NULL, *s = NULL;
	int i=0, j=0;
	int size = len; /* allocate as much size as the input */
	unsigned als=0;

	/* init
	*/
	als = ALLOCSIZE(size);
	word = (char *) MALLOC(als);
	if (word == NULL) {
		return (NULL);
	}

	/* get last word */
	while (j<len && dataline[j] != '\n' && dataline[j] != '\r') {
		if (dataline[j] == ' ' || dataline[j] == '\t')
			i = 0;
		else
			word[i++] = dataline[j];
		j++;
	}
	word[i] = '\0';

	/* realloc
	*/
	als = ALLOCSIZE(i);
	if (als < ALLOCSIZE(size)) {
		s = (char *) REALLOC((void *)word, als);
		if (s != NULL) {
			word = s;
		}
		/* no problem */
	}

	return word;
}

/*
 * get rest of line, optionally strip newline and symlink part
 */
int
get_rest_of_line (char **buffer, int *bufsize, const char *dataline, int from, int dlen)
{
	char *word = *buffer;
	int wl = *bufsize;
	char *next=NULL;
	int offset=0;

	if (dataline == NULL || dlen < 0 || dlen <= from) {
		word = NULL;
		return (-1);
	}

	csere(&word, &wl, 0, 0, dataline+from, dlen-from);
	if (word == NULL) {
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
		csere(&word, &wl, 0, offset, "", 0);
	}

	/* give pointers back */
	*buffer = word;
	*bufsize = wl;

	return (0);
}

/*
 * utility to get filenames by the TAB, as usual...
 * allocate space for choices; MUST BE free'd by caller
 */
int
get_fname (char *path, unsigned maxsize, char **choices)
{
	char prop[CMDLINESIZE];
	glob_t globbuf;
	int r=0, i=0, j=0, len=0, new=0, s=0;
	unsigned request=0, als=0;
	char *qq;

	if (path == NULL || maxsize <= 1) {
		return -1;
	}

	*choices = NULL;
	globbuf.gl_offs = 0;
	prop[0] = '\0';
	strncat(prop, path, sizeof(prop)-2);
	strncat(prop, "*", 2);

	/* file or dir */
	r = glob(prop, GLOB_ERR | GLOB_TILDE_CHECK | GLOB_MARK, NULL, &globbuf);

	if (r==0) {		/* succesful completion */

		r = globbuf.gl_pathc;
		if (r==1) {
			strncpy(path, globbuf.gl_pathv[0], maxsize-1);
			path[maxsize-1] = '\0';

		} else /* r>1 */ {
			/* guess the common prefix */
			len = strlen(path);	/* original length */
			new = maxsize-1;
			strncpy(path, globbuf.gl_pathv[0], new);
			for (i=1; i<r; i++) {
				for (j=len; j<new; j++) {
					if (path[j] == '\0' ||
					globbuf.gl_pathv[i][j] == '\0' ||
					path[j] != globbuf.gl_pathv[i][j]) 
					{
						new = j;
						break;
					}
				}
			}
			path[new] = '\0';

			/* return choices, but limit number of bytes, 500? */
			request = 1;
			i = 0;
			while (i<r) {
				s = strlen(globbuf.gl_pathv[i]);
				if (request+s+1 < 500) {
					request += s+1;
					i++;
				} else {
					r = i;
					break;
				}
			}

			als = ALLOCSIZE(request+2);
			qq = (char *) MALLOC(als);
			if (qq != NULL) {
				*choices = qq;	/* save for return, to be freed by caller */
				qq[0]='\0';
				for (i=0; i<r; i++) {
					strncat(qq, globbuf.gl_pathv[i], request);
					strncat(qq, " ", 2);
				}
			} else {
				r = -r;
				/* beep(); beep(); */
			}
		}

	/* failed... */
	} else {
		r = -r;
		/* beep(); beep(); */
	}
	globfree(&globbuf);

	return r;
} /* get_fname */

/*
 * internal utility to glob/expand filename
 */
int
glob_name (char *fname, unsigned maxsize)
{
	int ret=1;
	glob_t globbuf;
	char probe[FNAMESIZE*2];
	unsigned i=0, len=0, sl=0;

	if (fname == NULL || maxsize <= 1) {
		return -1;
	}

	maxsize = MIN(maxsize, FNAMESIZE);
	fname[maxsize-1] = '\0';

	glob(fname, GLOB_ERR | GLOB_TILDE_CHECK, NULL, &globbuf);
	if (globbuf.gl_pathc == 1) {
		/* ok */
		fname[0] = '\0';
		strncat(fname, globbuf.gl_pathv[0], maxsize-1);
		ret=0;
	} else {
		/* resolve dirname, maybe the file doesn't exist */
		len = strlen(fname);
		len = MIN(len, maxsize-1);
		if (len>2 && fname[0]=='~') {
			globfree(&globbuf);
			for (i=0, sl=1; i <= len; i++) {
				probe[i] = fname[i];
				if (fname[i]=='/')
					sl = i;
			}
			probe[sl] = '\0';	/* sl>=1 && sl<len */
			/* again */
			glob(probe, GLOB_ERR | GLOB_TILDE_CHECK | GLOB_MARK, NULL, &globbuf);
			if (globbuf.gl_pathc == 1) {
				/* path resolved */
				probe[0] = '\0';
				strncat(probe, globbuf.gl_pathv[0], maxsize-1);
#ifndef NO_GLOB_WORKAROUND
				/* workaround
				* for "~/no-more-slash" names, to avoid /home/whatever//no-more-slash
				*/
				len = strlen(probe);
				if (len>2 && probe[len-1]=='/' && fname[sl]=='/') {
					probe[--len] = '\0';
				}
#endif
				strncat(probe, &fname[sl], maxsize-1);
				/* and copy back to fname */
				fname[0] = '\0';
				strncat(fname, probe, maxsize-1);
				ret=0;
			}
		}
	}
	globfree(&globbuf);

	return (ret);
} /* glob_name */

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
	int slen = strlen(str);

	switch (slen)
	{
	case 1:
		if (str[0]=='1' || str[0]=='y')
			ret=1;
		else if (str[0]=='0' || str[0]=='n')
			ret=0;
		break;

	case 2:
		if (strncmp(str, "on", 2)==0)
			ret=1;
		else if (strncmp(str, "no", 2)==0)
			ret=0;
		break;

	case 3:
		if (strncmp(str, "yes", 3)==0)
			ret=1;
		if (strncmp(str, "off", 3)==0 || strncmp(str, "not", 3)==0)
			ret=0;
		break;

	default:
		ret = (-1);
		break;
	}

	return (ret);
} /* yesno */

/*
 * return the filetype (by file extension)
 */
int
fname_ext (const char *cfname)
{
	int i=0, j = -1;
	char ext[20];
	int fxtype = UNKNOWN_FILETYPE;

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
	} else if (j==2 && ext[0]=='t' && ext[1]=='k') {
		fxtype = TCL_FILETYPE;
	} else if (j==3 && ext[0]=='t' && ext[1]=='c' && ext[2]=='l') {
		fxtype = TCL_FILETYPE;
	} else if (j==2 && ext[0]=='p' && ext[1]=='y') {
		fxtype = PYTHON_FILETYPE;
	} else if (j==2 && ext[0]=='s' && ext[1]=='h') {
		fxtype = SHELL_FILETYPE;
	} else {
		/* hope the best */
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
	int i=0, idx=0;

	if (from > strsize || from < 0)
		return (-1);

	if (reverse) {
		idx = strsize+1;
		for (i = from; i >= 0; i--) {
			if (string[i] == '/') {
				idx = i;
				if (get_first) break;
			}
		}
	} else {
		idx = -1;
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
 * 	length can be 0 or strlen of input if no delimiters found
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
 */
void
mybasename (char *outpath, const char *inpath, int outbuffsize)
{
	int ilen=0, last=0;

	if (outbuffsize < 1) {
		return;
	} else if (inpath == NULL || inpath[0] == '\0') {
		outpath[0] = '\0';
	} else {
		ilen = strlen(inpath);
		ilen = MIN(ilen, outbuffsize-1);
		while (ilen > 1 && inpath[ilen-1] == '/') {
			ilen--;
		}
		last = slash_index (inpath, ilen, 0, SLASH_INDEX_FWD, SLASH_INDEX_GET_LAST);
		if (last == -1) {
			strncpy(outpath, &inpath[0], ilen);
			outpath[ilen] = '\0';
		} else {
			strncpy(outpath, &inpath[last+1], ilen-last-1);
			outpath[ilen-last-1] = '\0';
		}
	}

	return;
}

/*
 * return the dir part of filename path, before the last slash
 * but keep the "/" string
 * and return "." instead of empty string
 */
void
mydirname (char *outpath, const char *inpath, int outbuffsize)
{
	int ilen=0, last=0;

	if (outbuffsize < 2) {
		return;
	} else if (inpath == NULL || inpath[0] == '\0') {
		outpath[0] = '.';
		outpath[1] = '\0';
	} else {
		ilen = strlen(inpath);
		ilen = MIN(ilen, outbuffsize-1);
		while (ilen > 1 && inpath[ilen-1] == '/') {
			ilen--;
		}
		last = slash_index (inpath, ilen, 0, SLASH_INDEX_FWD, SLASH_INDEX_GET_LAST);
		if (last == -1) {
			outpath[0] = '.';
			outpath[1] = '\0';
		} else if (last == 0) {
			outpath[0] = '/';
			outpath[1] = '\0';
		} else {
			strncpy(outpath, &inpath[0], last);
			outpath[last] = '\0';
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
	csere (&dir, &size, 0, 0, &path[0], ilen);
	if (dir == NULL) {
		return NULL;
	}

	/* add curpath if not absolute path */
	if (dir[0] != '/') {
		/* insert PWD into the dir */
		csere (&dir, &size, 0, 0, "/", 1);
		csere (&dir, &size, 0, 0, cnf._pwd, cnf.l1_pwd);
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
				FREE(dir);
				dir = NULL;
				break; /* invalid path */
			}
			prev = slash_index (dir, size, runner-1, SLASH_INDEX_REVERSE, SLASH_INDEX_GET_FIRST);
			if (prev == -1 || prev > size) {
				FREE(dir);
				dir = NULL;
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

	return (dir);
}

/*
 * break down input[] into array of strings, each in args[] is a pointer
 * into the original input string; '\0' replaces delimiter
 * return number of args and last element is NULL
 * special: keep 'word ...' patterns as is together in one string
 */
int
parse_args (char *input, char **args)
{
#define NONE_WORD	0
#define BASE_WORD	1
#define SPEC_WORD	2

	int pos=0, nb_args=0, mode=NONE_WORD;

	if (input == NULL) {
		args[0] = NULL;
		return (0);
	}

	while (input[pos] != '\0' && nb_args < MAXARGS-1) {
		switch (mode) {
		case NONE_WORD:
			if (input[pos] == ' ' || input[pos] == '\t') {
				pos++;
			} else if (input[pos] == '\'') {
				args[nb_args++] = &input[++pos];
				mode = SPEC_WORD;
			} else {
				args[nb_args++] = &input[pos];
				mode = BASE_WORD;
			}
			break;
		case BASE_WORD:
			if (input[pos] == ' ' || input[pos] == '\t') {
				input[pos++] = '\0';
				mode = NONE_WORD;
			} else {
				pos++;	/* in word */
			}
			break;
		case SPEC_WORD:
			if (input[pos] == '\'') {
				input[pos++] = '\0';
				mode = NONE_WORD;
			} else {
				pos++;	/* in word */
			}
			break;
		default:
			break;
		}
	}/* while */

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
	int pn_length = 0, entry_length = 0, offset = 0;
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
				offset = entry_length - pn_length;
				if (offset == 0) {
					if (strncmp(progname, entry, pn_length) == 0) {
						retval = pid;
					}
				} else if (offset > 0 && entry[offset-1] == '/') {
					if (strncmp(progname, &entry[offset], pn_length) == 0) {
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
