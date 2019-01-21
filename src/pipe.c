/*
* pipe.c
* tools for fork/execvp processes, external programs and filters, make, find, vcs tools, shell,
* utilities for background processing
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>		/* getpid, getuid, pipe, fork, close, execvp, fcntl */
#include <fcntl.h>
#include <signal.h>		/* signal, kill */
#include <sys/wait.h>		/* waitpid */
#include <errno.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <glob.h>		/* glob, globfree */
#include "main.h"
#include "proto.h"

/* global config */
extern CONFIG cnf;

#define XREAD	0
#define XWRITE	1

/* local proto */
static int filter_cmd_eng (const char *ext_cmd, int opts);
static int fork_exec (const char *ext_cmd, const char *ext_argstr, int *in_pipe, int *out_pipe, int opts);
static int finish_in_fg (void);
static int getxline (char *buff, int *count, int max, int fd);

/*
** shell_cmd - launch shell to run given command with the optional arguments and catch output to buffer
*/
int
shell_cmd (const char *ext_cmd)
{
	int ret=0;
	char ext_argstr[CMDLINESIZE];
	memset(ext_argstr, 0, sizeof(ext_argstr));

	/* command is mandatory */
	if (ext_cmd[0] == '\0') {
		return (0);
	}

	if (ext_cmd[0] == 0x27) {
		snprintf(ext_argstr, sizeof(ext_argstr)-1, "sh -c %s", ext_cmd);
	} else {
		snprintf(ext_argstr, sizeof(ext_argstr)-1, "sh -c '%s'", ext_cmd);
	}
	ret = read_pipe ("*sh*", cnf.sh_path, ext_argstr, OPT_REDIR_ERR | OPT_TTY);

	return (ret);
}

/*
** make_cmd - start make with optional arguments and catch output,
**	set arguments on commandline and options in make_opts resource
*/
int
make_cmd (const char *ext_cmd)
{
	int ret=0;
	char ext_argstr[CMDLINESIZE];
	memset(ext_argstr, 0, sizeof(ext_argstr));

	if (cnf.make_path[0] == '\0') {
		tracemsg("make path not configured");
		return (1);
	}

	/* make w/ or w/o options */
	snprintf(ext_argstr, sizeof(ext_argstr)-1, "%s %s %s", cnf.make_path, cnf.make_opts, ext_cmd);

	ret = read_pipe ("*make*", cnf.make_path, ext_argstr, OPT_REDIR_ERR | OPT_TTY);

	return (ret);
}

/*
** vcstool - start VCS command with arguments (does not fork to background),
**	the first argument must be the name of the vcs tool itself,
**	the rest are the optional parameters
*/
int
vcstool (const char *ext_cmd)
{
	int ret=0;
	char tname[20];
	int i;
	unsigned slen;

	/* toolname is mandatory */
	if (ext_cmd[0] == '\0') {
		return (0);
	}

	for (i=0; i<10; i++) {
		slen = strlen(cnf.vcs_tool[i]);
		if (slen>0 && !strncmp(ext_cmd, cnf.vcs_tool[i], slen)) {
			break;
		}
	}

	if (i == 10 || ext_cmd[0] == '\0' || cnf.vcs_path[i][0] == '\0') {
		tracemsg("vcs tool not configured");
		return (1);
	}

	memset(tname, 0, sizeof(tname));
	snprintf(tname, sizeof(tname)-1, "*%s*", cnf.vcs_tool[i]);

	/* OPT_NOAPP breaks macros with multiple commands */
	ret = read_pipe (tname, cnf.vcs_path[i], ext_cmd, OPT_NOBG | OPT_REDIR_ERR);

	return (ret);
}

/*
** find_cmd - start find/egrep process with given pattern and catch output,
**	set arguments on commandline and options in find_opts resource
*/
int
find_cmd (const char *ext_cmd)
{
	int ret=0;
	char ext_argstr[CMDLINESIZE];
	char temp[CMDLINESIZE];
	char *word=NULL;
	char delim;

	if (cnf.find_path[0] == '\0') {
		tracemsg("find path not configured");
		return (1);
	}

	memset(ext_argstr, 0, sizeof(ext_argstr));
	if (ext_cmd[0] == '\0') {
		/* try variable name first */
		word = select_word(CURR_LINE, CURR_FILE.lncol);
		if (word == NULL || word[0] == '\0') {
			FREE(word); word = NULL;
			/* try function/block name */
			word = block_name(cnf.ring_curr);
			if (word == NULL || word[0] == '\0') {
				FREE(word); word = NULL;
				return (1);
			}
		}
		snprintf(ext_argstr, sizeof(ext_argstr)-1, "find %s %s '%s' {} ;",
			cnf.find_opts, ((cnf.gstat & GSTAT_CASES) ? "" : "-i"),
			((word[0] == '.' || word[0] == '>') ? word+1 : word));
		FREE(word); word = NULL;
	} else {
		/* arg. from commandline */
		delim = ext_cmd[0];
		cut_delimiters (ext_cmd, temp, sizeof(temp));
		delim = (delim == 0x22) ? 0x22 : 0x27;
		snprintf(ext_argstr, sizeof(ext_argstr)-1, "find %s %s %c%s%c {} ;",
			cnf.find_opts, ((cnf.gstat & GSTAT_CASES) ? "" : "-i"), delim, temp, delim);
	}

	ret = read_pipe ("*find*", cnf.find_path, ext_argstr, OPT_REDIR_ERR);

	return (ret);
}

/*
** locate_cmd - start internal search with given pattern,
**	search in open regular buffers only
*/
int
locate_cmd (const char *expr)
{
	int ret=0;
	char temp[CMDLINESIZE];
	char pattern[CMDLINESIZE];
	char *word=NULL;
	int ring_i = cnf.ring_curr;

	if (expr[0] == '\0') {
		/* try variable name first */
		word = select_word(CURR_LINE, CURR_FILE.lncol);
		if (word == NULL || word[0] == '\0') {
			FREE(word); word = NULL;
			/* try function/block name */
			word = block_name(cnf.ring_curr);
			if (word == NULL || word[0] == '\0') {
				FREE(word); word = NULL;
				return (1);
			}
		}
		if (word[0] == '.' || word[0] == '>') {
			strncpy (pattern, word+1, sizeof(pattern));
		} else {
			strncpy (pattern, word, sizeof(pattern));
		}
		pattern[sizeof(pattern)-1] = '\0';
		FREE(word); word = NULL;
	} else {
		cut_delimiters (expr, temp, sizeof(temp));
		regexp_shorthands (temp, pattern, sizeof(pattern));
	}

	/* open or switch to */
	if ((ret = scratch_buffer("*find*")) != 0) {
		return (ret);
	}
	/* cnf.ring_curr is set now */
	if (CURR_FILE.pipe_output != 0) {
		tracemsg("background process still running!");
		cnf.ring_curr = ring_i;
		return (0);
	}
	/* additional flags */
	CURR_FILE.fflag |= FSTAT_SPECW;
	/* disable inline editing, adding lines */
	CURR_FILE.fflag |= (FSTAT_NOEDIT | FSTAT_NOADDLIN);

	/* (re)set origin */
	if (ring_i != cnf.ring_curr) {
		CURR_FILE.origin = ring_i;
	}

	/* start the engine
	*/
	ret = internal_search (pattern);

	if (ret) {
		ret |= drop_file();
	}

	return (ret);
}

/*
** filter_cmd - start filter command in shell and feed selection lines into child process and catch output,
**	set child process arguments on commandline, for example "|sort -k2"
*/
int
filter_cmd (const char *ext_cmd)
{
	int ret=0, opts=0;

	if (ext_cmd[0] == '\0') {
		tracemsg("no filter command");
		return (0);
	}

	if (cnf.select_ri == -1) {
		tracemsg ("no selection for filter command");
		return (1);
	}

	opts = OPT_IN_OUT | OPT_REDIR_ERR | OPT_SILENT;
	ret = filter_cmd_eng(ext_cmd, opts);

	return (ret);
}

/*
** filter_shadow_cmd - start filter command in shell and feed lines into child process and catch output,
**	push out shadow line markers together with selection lines or all lines if no selection here,
**	set child process arguments on commandline, for example "|| a2ps -1 -f8"
*/
int
filter_shadow_cmd (const char *ext_cmd)
{
	int ret=0, opts=0;

	if (ext_cmd[0] == '\0') {
		tracemsg("no filter command");
		return (0);
	}

	opts = OPT_IN_OUT | OPT_REDIR_ERR | OPT_SILENT;

	/* default: (visible) SELECTION lines */
	if (cnf.select_ri != cnf.ring_curr)
		opts |= OPT_IN_OUT_VIS_ALL;	/* ALL (visible) lines */

	opts |= OPT_IN_OUT_SH_MARK;		/* with shadow line mark */

	ret = filter_cmd_eng(ext_cmd, opts);

	return (ret);
}

/*
* filter_cmd_eng - start filter command in shell to feed lines into this process and catch output,
*	set arguments on commandline, option flags from caller
*/
static int
filter_cmd_eng (const char *ext_cmd, int opts)
{
	int ret=0;
	char cmd[FNAMESIZE], args[FNAMESIZE];
	unsigned i=0, j=0;

	if (ext_cmd[0] == '\0') {
		tracemsg("no filter command");
		return (0);
	}

	i = 0;
	/* skip initial whitechars */
	while (ext_cmd[i] == ' ' || ext_cmd[i] == '\t') {
		i++;
	}
	j = 0;
	/* get the command */
	for (; ext_cmd[i] != '\0' && ext_cmd[i] != ' ' && ext_cmd[i] != '\t'; i++) {
		if (j < sizeof(cmd)-1) {
			cmd[j] = ext_cmd[i];
			args[j] = ext_cmd[i];
			j++;
		}
	}
	cmd[j] = '\0';
	/* append the rest to the arguments */
	for (; ext_cmd[i] != '\0'; i++) {
		if (j < sizeof(args)-1) {
			args[j++] = ext_cmd[i];
		}
	}
	args[j] = '\0';

	ret = read_pipe ("*sh*", cmd, args, opts);

	return (ret);
}

/*
** lsdir_cmd - create directory listing in special buffer for easy navigation and opening,
**	the one optional argument must be a directory or a shell glob
*/
int
lsdir_cmd (const char *ext_cmd)
{
	int ret=0;
	char dirpath[FNAMESIZE];
	int xlen=0, empty_pattern=0;
	struct stat test;
	int origin = cnf.ring_curr;

	cnf.lsdir_opts = LSDIR_OPTS;	/* defaults */

	/* the rest must be the dirpath
	*/
	strncpy(dirpath, ext_cmd, sizeof(dirpath)-2);
	dirpath[sizeof(dirpath)-2] = '\0';
	xlen = strlen(dirpath);
	strip_blanks (STRIP_BLANKS_FROM_END|STRIP_BLANKS_FROM_BEGIN, dirpath, &xlen);	/* do not squeeze */
	if (xlen < 1) {
		empty_pattern = 1;
		strncpy(dirpath, "./", 3);
		xlen = 2;
	} else {
		if (glob_tilde_expansion(dirpath, sizeof(dirpath)))
			return (1); /* useless */
		xlen = strlen(dirpath);

		/* if this is a filename, get the dirname for ls */
		if (xlen>0 && stat(dirpath, &test)==0 && (S_ISREG(test.st_mode))) {
			while (xlen>0 && dirpath[xlen-1] != '/')
				xlen--;
			if (xlen>0 && dirpath[xlen-1] == '/')
				dirpath[xlen] = '\0';
			else {
				strncpy(dirpath, "./", 3);
				xlen=2;
			}
		}
		/* dirpath: directory name or regexp pattern */
	}

	/* open or switch to */
	ret = scratch_buffer("*ls*");
	if (ret==0) {
		/* switch to */
		if (origin != cnf.ring_curr && empty_pattern && CURR_FILE.num_lines > 0)
			return (0);
		/* generate or re-generate */
		if (CURR_FILE.num_lines > 0)
			ret = clean_buffer();
	}
	if (ret) {
		return (ret);
	}

	/* start the engine
	*/
	ret = lsdir (dirpath);
	if (ret) {
		ret |= drop_file();
	}

	return (ret);
}

/*
* show_define - grab one line from the given file and show value (of #define)
*/
int
show_define (const char *fname, int lineno)
{
	int ret=1;
	char *iobuffer=NULL;
	char *next=NULL;
	int xlen=0;

	iobuffer = read_file_line (fname, lineno);
	if (iobuffer != NULL) {
		/* remove prefix */
		next = strstr(iobuffer, "define");
		if (next == NULL) {
			next = iobuffer;
		} else {
			next = next + 6;
		}

		xlen = strlen(next);
		strip_blanks (STRIP_BLANKS_FROM_END|STRIP_BLANKS_FROM_BEGIN|STRIP_BLANKS_SQUEEZE, next, &xlen);
		if (xlen > TEXTCOLS-1)
			next[TEXTCOLS-1] = '\0';

		tracemsg("%s", next);
		FREE(iobuffer); iobuffer = NULL;
		ret = 0;
	}

	return (ret);
}

/*
 * read_extcmd_line - given an external command to be passed to sh -c '...'
 * and return the specified lineno from the output
*/
int
read_extcmd_line (const char *ext_cmd, int lineno, char *buff, int siz)
{
	FILE *pipe_fp;
	int lno=0, slen=0;
	char *p, cache[1024];

	if (buff == NULL || siz < 1) {
		return (1);
	}

	pipe_fp = popen(ext_cmd, "r");
	if (pipe_fp == NULL) {
		return (1);
	}

	memset(cache, '\0', 1024);
	while ((p = fgets(cache, 1023, pipe_fp)) != NULL) {
		if (++lno == lineno) {
			strncpy(buff, cache, (size_t)siz);
			buff[siz-1] = '\0';

			slen = strlen(buff);
			if (slen > 0 && buff[slen-1] == '\n')
				slen--;
			buff[slen] = '\0';
		}
	}
	pclose(pipe_fp);

	return (0);
}

/* does the common work starting background process
* returns child PID if ok, otherwise -1
* pipes:
*               in              |
*   parent -->  |               |
*    feed       | --> child --> |
*               |    process    | --> parent
*               |               |    read out
*               |              out
*
*/
static int
fork_exec (const char *ext_cmd, const char *ext_argstr,
	int *in_pipe, int *out_pipe, int opts)
{
	char token_str[CMDLINESIZE];
	char *args[MAXARGS], cmd[FNAMESIZE];
	int xx=0;
	int chrw=-1;

	/* copy and parse args[] */
	strncpy(token_str, ext_argstr, sizeof(token_str));
	token_str[sizeof(token_str)-1] = '\0';
	if ((xx = parse_args (token_str, args)) < 1) {
		// failed
		return (-1);
	}

	/* check command */
	if (ext_cmd[0] == '\0') {
		// no command to launch
		return (-1);
	}
	strncpy(cmd, ext_cmd, sizeof(cmd));
	cmd[sizeof(cmd)-1] = '\0';

	if (pipe(out_pipe) || pipe(in_pipe)) {
		PIPE_LOG(LOG_ERR, "pipe() call failed (%s)", strerror(errno));
		return (-1);
	}
	// in pipe -- in_pipe[XWRITE], in_pipe[XREAD]
	// out pipe -- out_pipe[XWRITE], out_pipe[XREAD]

	if ((chrw = fork()) == -1) {
		PIPE_LOG(LOG_ERR, "fork() [r/w] failed (%s)", strerror(errno));
		close(in_pipe[XWRITE]);
		if (out_pipe[XREAD] != in_pipe[XWRITE])
			close(out_pipe[XREAD]);
		close(out_pipe[XWRITE]);
		if (in_pipe[XREAD] != out_pipe[XWRITE])
			close(in_pipe[XREAD]);
		return (-1);
	}

	if (chrw == 0) {
		/* child process
		*/
		struct sigaction sigAct;
		memset(&sigAct, 0, sizeof(sigAct));

		/* close parent sides */
		close(in_pipe[XWRITE]);
		if (out_pipe[XREAD] != in_pipe[XWRITE])
			close(out_pipe[XREAD]);

		dup2(out_pipe[XWRITE], 1);		/* stdout */
		if (opts & OPT_REDIR_ERR) {
			dup2(out_pipe[XWRITE], 2);	/* redir stderr */
		} else {
			close(2);			/* drop stderr */
		}
		if (opts & (OPT_IN_OUT | OPT_TTY)) {
			dup2(in_pipe[XREAD], 0);	/* stdin from pipe */
		} else {
			close(0);			/* no stdin */
		}

		/* close after dup */
		close(out_pipe[XWRITE]);
		if (in_pipe[XREAD] != out_pipe[XWRITE])
			close(in_pipe[XREAD]);
		/* become session leader */
		if (setsid() == -1)
			perror("setsid");

		/* set big terminal size for the child process */
		setenv("COLUMNS", "512", 1);

		/* reset signal handler for child
		*/
		sigAct.sa_handler = SIG_DFL;
		sigaction(SIGHUP, &sigAct, NULL);
		sigaction(SIGINT, &sigAct, NULL);
		sigaction(SIGQUIT, &sigAct, NULL);
		sigaction(SIGTERM, &sigAct, NULL);
		sigaction(SIGPIPE, &sigAct, NULL);
		sigaction(SIGUSR1, &sigAct, NULL);
		sigaction(SIGUSR2, &sigAct, NULL);

		(void)execvp (cmd, args);
		PIPE_LOG(LOG_ERR, "child: execvp() [%s] failed (%s)", cmd, strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* parent process
	*/
	close(out_pipe[XWRITE]);
	if (in_pipe[XREAD] != out_pipe[XWRITE])
		close(in_pipe[XREAD]);

	return chrw;
}

/*
* read pipe of external process until finished
*/
int
finish_in_fg (void)
{
	int ret = 0;

	CURR_FILE.pipe_opts |= OPT_NOBG;

	/* loop stops if pipe_output closed
	*/
	while (ret==0 && CURR_FILE.pipe_output) {
		ret = readout_pipe (cnf.ring_curr);
		if (ret==1)
			ret=0; /* that was an empty read */
	}

	return ret;
}

/*
* this function reads the stdin, moves that to pipe_output and reopen /dev/tty,
* finish reading in the background as "*sh*" buffer without a process
* returns 0 if ok, otherwise error...
*/
int
read_stdin (void)
{
	int ring_i=0, ret=0;
	int pipeFD, newSTDIN;

	ring_i = cnf.ring_curr;

	if ( isatty(0) ) {
		/* normal tty, not stdin pipe */
		return 1;
	}

	pipeFD = dup(0);
	if (pipeFD == -1) {
		perror("dup 0 failed");
		return 2;
	}
	close(0);

	newSTDIN = open("/dev/tty", O_RDONLY);
	if (newSTDIN == -1) {
		perror("open /dev/tty failed");
		close(pipeFD);
		return 3;
	}
	if (newSTDIN != 0) {
		/* this should not happen, but call dup2 */
		dup2(newSTDIN, 0);
		close(newSTDIN);
	}
	if ( !isatty(0) ) {
		/* control, this should not happen */
		close(pipeFD);
		return 4;
	}

	/* the rest is standard input processing */

	if ((ret = scratch_buffer("*sh*")) != 0) {
		return (ret);
	}
	/* cnf.ring_curr is set now */

	CURR_FILE.chrw = -1;
	CURR_FILE.pipe_opts = OPT_SILENT;
	if (cnf.gstat & GSTAT_MACRO_FG)
		CURR_FILE.pipe_opts |= OPT_NOBG; // ext.process in macro should remain in fg
	CURR_FILE.pipe_input = 0;
	CURR_FILE.pipe_output = pipeFD;

	CURR_FILE.fflag |= FSTAT_SPECW;
	CURR_FILE.fflag |= (FSTAT_NOEDIT | FSTAT_NOADDLIN);

	if (ring_i != cnf.ring_curr) {
		CURR_FILE.origin = ring_i;
	}

	CURR_FILE.curr_line = CURR_FILE.top;
	CURR_FILE.lineno = 0;

	if (CURR_FILE.pipe_opts & OPT_NOBG) {
		/* finish in foreground -- loop stops if pipe_output closed
		*/
		PIPE_LOG(LOG_NOTICE, "ri=%d stdin-pipe -- continue in FOREground", cnf.ring_curr);
		ret = finish_in_fg();
		/* finished */
	} else {
		/* continue in background
		*/
		if (fcntl(CURR_FILE.pipe_output, F_SETFL, O_NONBLOCK) == -1) {
			ERRLOG(0xE0B2);
		}
		PIPE_LOG(LOG_NOTICE, "ri=%d stdin-pipe -- continue in BACKground", cnf.ring_curr);
	}

	return (ret);
}

/*
* this function starts external command like "$ext_cmd $ext_argstr"
* in the background and is going to process the outcome to memory buffer
* (ring index selected by sbufname) with some options in opts
* returns 0 if ok, otherwise error...
*/
int
read_pipe (const char *sbufname, const char *ext_cmd, const char *ext_argstr, int opts)
{
	int in_pipe[2];		/* childs input pipe (feed by parent process) */
	int out_pipe[2];	/* childs output pipe, read back by the parent process */
	int chrw = -1;
	int lno_write=0, ring_i=0, ret = 0;
	ring_i = cnf.ring_curr;

	/* sanity check */
	// bad opts: no-scratch only with OPT_BASE_MASK | OPT_COMMON_MASK bits

	if ((opts & OPT_BASE_MASK) == OPT_STANDARD) {
		/* open or switch to */
		if ((ret = scratch_buffer(sbufname)) != 0) {
			return (ret);
		}
	}
	/* cnf.ring_curr is set now */
	if (CURR_FILE.pipe_output != 0) {
		tracemsg("cannot start, background process is running here!");
		cnf.ring_curr = ring_i;
		return (0);
	}

	if ((opts & OPT_BASE_MASK) == OPT_STANDARD) {
		if (opts & OPT_NOAPP) {
			if (CURR_FILE.num_lines > 0) {
				clean_buffer();
			}
		}
	}

	/* common */
	chrw = fork_exec (ext_cmd, ext_argstr, in_pipe, out_pipe, opts);
	if (chrw == -1) {
		tracemsg("failed to start external command"); /* remain in this buffer */
		return (2);
	}
	CURR_FILE.chrw = chrw;

	PIPE_LOG(LOG_NOTICE, "fork/parent -- ri:%d, new ri:%d -- child:%d",
		ring_i, cnf.ring_curr, chrw); // in_pipe[XWRITE], out_pipe[XREAD]
	// "pid:%d (ppid:%d) sid:%d pgid:%d", cnf.pid, cnf.ppid, cnf.sid, cnf.pgid);
	// "uid:%d gid:%d euid:%d egid:%d", cnf.uid, cnf.gid, cnf.euid, cnf.egid);

	/* common */
	if (opts & OPT_IN_OUT) {
		/* feed external r/w process
		*/
		if ((opts & OPT_IN_OUT_FOCUS) == OPT_IN_OUT_FOCUS) {
			/* focus line from the original buffer */
			lno_write = write_out_chars(in_pipe[XWRITE],
				cnf.fdata[ring_i].curr_line->buff,
				cnf.fdata[ring_i].curr_line->llen);
		} else if ((opts & OPT_IN_OUT_REAL_ALL) == OPT_IN_OUT_REAL_ALL) {
			/* all lines from the original buffer */
			lno_write = write_out_all_lines(in_pipe[XWRITE],
				ring_i);
		} else if ((opts & OPT_IN_OUT_VIS_ALL) == OPT_IN_OUT_VIS_ALL) {
			/* all visible lines, with or without shadow mark */
			lno_write = write_out_all_visible(in_pipe[XWRITE],
				ring_i,
				((LMASK(cnf.ring_curr)) ? (opts & OPT_IN_OUT_SH_MARK) : 0));
		} else {
			/* selection lines, with or without shadow mark */
			lno_write = wr_select(in_pipe[XWRITE],
				((LMASK(cnf.ring_curr)) ? (opts & OPT_IN_OUT_SH_MARK) : 0));
		}
		if (lno_write < 1)
			ret = 100;
		PIPE_LOG(LOG_NOTICE, "fed child:%d (opts:0x%x) with %d line(s)",
			chrw, (opts & OPT_IN_OUT_MASK), lno_write); // in_pipe[XWRITE]
	}

	/* common */
	if ((opts & OPT_TTY) == 0) {
		close(in_pipe[XWRITE]);
		in_pipe[XWRITE] = 0;
	}

	if ((opts & OPT_BASE_MASK) == OPT_STANDARD) {
		/* additional flags */
		CURR_FILE.fflag |= FSTAT_SPECW;
		/* disable inline editing, adding lines */
		CURR_FILE.fflag |= (FSTAT_NOEDIT | FSTAT_NOADDLIN);

		if ((opts & OPT_SILENT) == 0) {
			/* first line: header
			*/
			char header_str[CMDLINESIZE];
			strncpy(header_str, (cnf.uid ? "$ " : "# "), 3);
			strncat(header_str, ext_argstr, sizeof(header_str)-5);
			strncat(header_str, "\n", 2);
			if (insert_line_before (CURR_FILE.bottom, header_str) != NULL) {
				CURR_FILE.num_lines++;
			} else {
				ret = 200;
			}
		}
	}

	/* common */
	if (cnf.gstat & GSTAT_MACRO_FG)
		opts |= OPT_NOBG; // ext.process in macro should remain in fg
	CURR_FILE.pipe_opts = opts;
	CURR_FILE.pipe_input = in_pipe[XWRITE];
	CURR_FILE.pipe_output = out_pipe[XREAD];

	if ((opts & OPT_BASE_MASK) == OPT_STANDARD) {
		if (CURR_FILE.lineno >= CURR_FILE.num_lines - ((opts & OPT_SILENT) ? 0 : 1)) {
			/* append goes after bottom->prev, so force pull up current */
			CURR_FILE.curr_line = CURR_FILE.bottom->prev;
			CURR_FILE.lineno = CURR_FILE.num_lines;
			CURR_LINE->lflag &= ~LMASK(cnf.ring_curr);	/* unhide */
			update_focus(INCR_FOCUS, cnf.ring_curr);
		}

		/* (re)set origin */
		if (ring_i != cnf.ring_curr) {
			CURR_FILE.origin = ring_i;
		}

		if (opts & OPT_NOBG) {
			/* finish in foreground -- loop stops if pipe_output closed
			*/
			PIPE_LOG(LOG_NOTICE, "ri=%d [%s] -- continue in FOREground", cnf.ring_curr, sbufname);
			ret = finish_in_fg();
			/* finished */
		} else {
			/* continue in background
			*/
			if (fcntl(CURR_FILE.pipe_output, F_SETFL, O_NONBLOCK) == -1) {
				ERRLOG(0xE0B1);
			}
			PIPE_LOG(LOG_NOTICE, "ri=%d [%s] -- continue in BACKground", cnf.ring_curr, sbufname);
		}
	} else {
		PIPE_LOG(LOG_NOTICE, "ri=%d -- external processing", cnf.ring_curr);
		ret = 0;
	}

	return (ret);
} /* read_pipe */

/*
* read lines from pipe into memory buffer (ring_i),
* stop processing on eof/error
* return: 1:ok/nothing, 0:ok/done, -1:error
* finished if cnf.fdata[ring_i].pipe_output is 0
*/
int
readout_pipe (int ring_i)
{
	char *rb=NULL;
	int ret=0;
	int ni, total=0, finish=0, pull, got, count, childpid=0, exitstatus=0;
	int ring_orig = cnf.ring_curr;

	/* init */
	if (cnf.fdata[ring_i].readbuff == NULL) {
		cnf.fdata[ring_i].rb_nexti = 0;
		cnf.fdata[ring_i].readbuff = (char *) MALLOC(LINESIZE_INIT+1);
		if (cnf.fdata[ring_i].readbuff == NULL) {
			ERRLOG(0xE029);
			cnf.ring_curr = ring_i;
			stop_bg_process();	/* init failed */
			cnf.ring_curr = ring_orig;
			return (-1);
		}
	}

	rb = cnf.fdata[ring_i].readbuff;
	ni = cnf.fdata[ring_i].rb_nexti;

	if ((cnf.fdata[ring_i].pipe_opts & OPT_BASE_MASK) == OPT_NOSCRATCH) {
		/* custom processing -- for reload_bydiff() */

		ni = 0;
		while (ret==0 && !finish && ni < LINESIZE_INIT) {
			got = getxline ((rb+ni), &count, LINESIZE_INIT - ni, cnf.fdata[ring_i].pipe_output);
			finish = (got == 0 || got == -1);
			ni += count;
			if ((ni > 0) && (finish || rb[ni-1] == '\n' || ni > LINESIZE_INIT-10)) {
				break;
			}
		}
		cnf.fdata[ring_i].rb_nexti = ni;
		ret = (finish) ? 1 : 0;

	} else {
		/* standard processing */

		LINE *lp=NULL, *lx=NULL;
		lp = cnf.fdata[ring_i].bottom->prev;

		pull = (cnf.fdata[ring_i].lineno >= cnf.fdata[ring_i].num_lines);

		while (ret==0 && !finish && total < LINESIZE_INIT) {
			got = getxline ((rb+ni), &count, LINESIZE_INIT - ni, cnf.fdata[ring_i].pipe_output);
			finish = (got == 0 || got == -1);
			ni += count;
			if ((ni > 0) && (finish || rb[ni-1] == '\n' || ni > LINESIZE_INIT-10)) {
				if ((lx = append_line (lp, rb)) != NULL) {
					lp = lx;
					cnf.fdata[ring_i].num_lines++;
				} else {
					ret = -1;
				}
				total += ni;
				ni = 0;
			} else {
				/* nothing todo, break loop */
				ret = (finish || total) ? 0 : 1;
				break;
			}
		}
		cnf.fdata[ring_i].rb_nexti = ni;
		got = 0;

		if (finish) {
			/* pipe_output must be closed -- that is the flag
			*/
			childpid = cnf.fdata[ring_i].chrw;
			exitstatus = wait4_bg(ring_i);	/* finished */

			if ((cnf.fdata[ring_i].pipe_opts & OPT_SILENT) == 0) {
				/* last line: footer
				*/
				if (insert_line_before (cnf.fdata[ring_i].bottom, "\n") != NULL) {
					cnf.fdata[ring_i].num_lines++;
				} else {
					ret = -1;
				}
			}

			if (cnf.fdata[ring_i].pipe_opts & OPT_NOBG) {
				PIPE_LOG(ret ? LOG_ERR : LOG_NOTICE, "-- ri=%d [%s] ret=%d FOREground task finished (pid %d, exit %d)",
					ring_i, cnf.fdata[ring_i].fname, ret, childpid, exitstatus);
			} else {
				PIPE_LOG(ret ? LOG_ERR : LOG_NOTICE, "-- ri=%d [%s] ret=%d BACKground task finished (pid %d, exit %d)",
					ring_i, cnf.fdata[ring_i].fname, ret, childpid, exitstatus);
			}
		}

		/* pull current line and focus */
		if (ret==0 && pull) {
			cnf.fdata[ring_i].curr_line = cnf.fdata[ring_i].bottom->prev;
			cnf.fdata[ring_i].lineno = cnf.fdata[ring_i].num_lines;
			cnf.fdata[ring_i].curr_line->lflag &= ~LMASK(cnf.ring_curr);
			update_focus(FOCUS_ON_LASTBUT1_LINE, ring_i);
		}
	}

	return (ret);
} /* readout_pipe */

/*
* handle all background pipes with readout_pipe() calls
* return 0 if nothing happened in current buffer
* return 1 after changes to current buffer (cnf.ring_curr)
* return -1 on error
*/
int
background_pipes (void)
{
	int bits = (FSTAT_OPEN | FSTAT_SPECW | FSTAT_SCRATCH);
	int ri=0, err=0;
	int ret = 0;	/* no change */

	for (ri=0; ri<RINGSIZE; ri++) {
		if (((cnf.fdata[ri].fflag & bits) == bits) && (cnf.fdata[ri].pipe_output != 0)) {
			err = readout_pipe (ri);
			if (err < 0) {
				// pipe read failure
				ret = -1;	/* error */
			} else if (err == 0) {
				/* change happened */
				if ((ri == cnf.ring_curr) && (ret == 0)) {
					ret = 1;
				}
			}
		}
	}

	return (ret);
}

/*
* close pipe, free up memory buffer and
* waitpid for the background process (if not yet closed)
* return status of child process (-1 on error, 0 if already closed)
*/
int
wait4_bg (int ri)
{
	int status=0;

	if (0 <= ri && ri < RINGSIZE) {
		if (cnf.fdata[ri].pipe_input != 0) {
			close(cnf.fdata[ri].pipe_input);
			cnf.fdata[ri].pipe_input = 0;
		}
		if (cnf.fdata[ri].pipe_output != 0) {
			close(cnf.fdata[ri].pipe_output);
			cnf.fdata[ri].pipe_output = 0;
		}
		if (cnf.fdata[ri].readbuff != NULL) {
			FREE(cnf.fdata[ri].readbuff);
			cnf.fdata[ri].readbuff = NULL;
		}
		if (cnf.fdata[ri].chrw > 0) {
			if (waitpid(cnf.fdata[ri].chrw, &status, WNOHANG) == -1) {
				// failed
				kill(cnf.fdata[ri].chrw, SIGHUP);
				status = -SIGHUP;
			} else {
				if (WIFEXITED(status)) {
					status = WEXITSTATUS(status);
				} else {
					kill(cnf.fdata[ri].chrw, SIGKILL);
					status = -SIGKILL;
				}
			}
			cnf.fdata[ri].chrw = -1;
		}
	}

	return status;
}

/*
** stop_bg_process - stop running background process of current buffer
*/
int
stop_bg_process (void)
{
	int status=0;

	if (CURR_FILE.fflag & FSTAT_OPEN) {
		if (CURR_FILE.pipe_input != 0) {
			close(CURR_FILE.pipe_input);
			CURR_FILE.pipe_input = 0;
		}
		if (CURR_FILE.pipe_output != 0) {
			close(CURR_FILE.pipe_output);
			CURR_FILE.pipe_output = 0;
		}
		if (CURR_FILE.readbuff != NULL) {
			FREE(CURR_FILE.readbuff);
			CURR_FILE.readbuff = NULL;
		}
		if (CURR_FILE.chrw > 0) {
			status = kill(CURR_FILE.chrw, SIGKILL);
			waitpid(CURR_FILE.chrw, &status, WNOHANG);
			CURR_FILE.chrw = -1;
		}
	}

	return (0);
}

/*
* get a line like fgets() and filter like getxline_filter(),
* returns 0=eof, 1=ok, 2=EAGAIN, -1=error
*/
static int
getxline (char *buff, int *count, int max, int fd)
{
	char ch=0;
	int cnt=0, ret=0;

	while (cnt < max-2) {
		ch=0;
		ret = read (fd, &ch, 1); /* read bytes one-by-one */

		if (ret == 0) {
			/* eof */
			break;
		} else if (ret == -1) {
			if (errno == EAGAIN) {
				ret = 2;
			} else {
				// read from pipe failed
				ret = -1;
			}
			break;
		}

		ret = 1;
		if (ch == '\n') {
			buff[cnt++] = '\n';
			break;
		} else if (ch == '\r') {
			if ((cnf.gstat & GSTAT_FIXCR) == 0)
				buff[cnt++] = ch;
		} else if (ch == 0x09) {
			/* tab */
			buff[cnt++] = ch;
		} else if ((unsigned char)ch >= 0x20 && (unsigned char)ch != 0x7f) {
			/* printable */
			buff[cnt++] = ch;
		} else if (ch == 0x08) {
			/* backspace */
			if (cnt > 0)
				buff[--cnt] = '\0';
		}
	}

	buff[cnt] = '\0';
	*count = cnt;

	return ret;
}
