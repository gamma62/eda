/*
* pipe.c 
* tools for fork/execve processes, external programs and filters, make, find, vcs tools, sh/ssh,
* utilities for background processing
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>		/* getuid, pipe, fork, close, execvp, fcntl */
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>		/* signal, kill */
#include <sys/wait.h>		/* waitpid */
#include <errno.h>
#include <syslog.h>
#include "main.h"
#include "proto.h"

/* global config */
extern CONFIG cnf;

#define XREAD	0
#define XWRITE	1

/* local proto */
static int filter_cmd_eng (const char *ext_cmd, int opts);
static int fork_exec (const char *ext_cmd, const char *ext_argstr, int *in_pipe, int *out_pipe, int opts);
static int check_zombie (int ri);
static int getxline (char *buff, int *count, int max, int fd);
static void filter_esc_seq (char *buff, int *count);

/*
** shell_cmd - launch shell to run given command with the optional arguments and catch output to buffer
*/
int
shell_cmd (const char *ext_cmd)
{
	int ret=0;
	char ext_argstr[CMDLINESIZE];
	memset(ext_argstr, 0, sizeof(ext_argstr));

	/* command is mandatory because this is not interactive */
	if (ext_cmd[0] == '\0') {
		return (0);
	}

	if (ext_cmd[0] == 0x27) {
		snprintf(ext_argstr, sizeof(ext_argstr)-1, "sh -c %s", ext_cmd);
	} else {
		snprintf(ext_argstr, sizeof(ext_argstr)-1, "sh -c '%s'", ext_cmd);
	}
	ret = read_pipe ("*sh*", cnf.sh_path, ext_argstr, OPT_REDIR_ERR);

	return (ret);
}

/*
** ishell_cmd - launch interactive shell or given command with the optional arguments,
**	catch output and show in buffer, read input from the last line;
**	the Apostrophe character maybe used to enclose parameters
*/
int
ishell_cmd (const char *ext_cmd)
{
	int ret=0;
	char ext_argstr[CMDLINESIZE];
	unsigned i=0, j=0;
	char head[50];
	memset(ext_argstr, 0, sizeof(ext_argstr));
	memset(head, 0, sizeof(head));

	/* command is optional */
	if (ext_cmd[0] == '\0') {
		snprintf(ext_argstr, sizeof(ext_argstr)-1, "i-sh");
	} else {
		if (ext_cmd[0] == 0x27) {
			j = 1;
			snprintf(ext_argstr, sizeof(ext_argstr)-1, "sh -c %s", ext_cmd);
		} else {
			snprintf(ext_argstr, sizeof(ext_argstr)-1, "sh -c '%s'", ext_cmd);
		}
	}

	head[i++] = '*';
	while (i < sizeof(head)-2 && ext_cmd[j] != '\0') {
		if ((ext_cmd[j] == ' ') || (ext_cmd[j] == 0x27))
			break;
		head[i++] = ext_cmd[j++];
	}
	head[i++] = '*';
	head[i++] = '\0';

	ret = read_pipe (head, cnf.sh_path, ext_argstr, OPT_REDIR_ERR | OPT_SILENT | OPT_INTERACT);

	return (ret);
}

/*
** sshell_cmd - launch secure shell with arguments starting with <user>@<host>,
**	catch output and show in buffer, read input from the last line;
*/
int
sshell_cmd (const char *ext_cmd)
{
	int ret=0;
	char ext_argstr[CMDLINESIZE];
	unsigned i=0, j=0, found=0;
	char head[50];
	memset(ext_argstr, 0, sizeof(ext_argstr));
	memset(head, 0, sizeof(head));

	/* arguments are mandatory, best practice is to start with user@host */
	if (ext_cmd[0] == '\0') {
		return (0);
	}

	/* get user@host */
	head[i++] = '*';
	while (i < sizeof(head)-2 && ext_cmd[j] != '\0') {
		if (ext_cmd[j] == ' ') {
			if (found)
				break;
			else
				i = 1;
		} else {
			head[i++] = ext_cmd[j];
			if (ext_cmd[j] == '@')
				found = 1;
		}
		j++;
	}
	head[i++] = '*';
	head[i++] = '\0';

	snprintf(ext_argstr, sizeof(ext_argstr)-1, "ssh %s", ext_cmd);
	ret = read_pipe (head, cnf.ssh_path, ext_argstr, OPT_REDIR_ERR | OPT_SILENT | OPT_INTERACT);

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
	snprintf(ext_argstr, sizeof(ext_argstr)-1, "make %s %s", cnf.make_opts, ext_cmd);

	ret = read_pipe ("*make*", cnf.make_path, ext_argstr, OPT_REDIR_ERR);

	return (ret);
}

/*
** vcstool - start VCS command with arguments, the first one must be the name of the vcs tool itself,
**	the rest are the real parameters, macros maybe used for abbreviation
*/
int
vcstool (const char *ext_cmd)
{
	int ret=0;
	char tname[20];
	int i, slen;

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

	ret = read_pipe (tname, cnf.vcs_path[i], ext_cmd, OPT_NOAPP | OPT_NOBG | OPT_REDIR_ERR);

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

	/* remember start point */
	mhist_push (cnf.ring_curr, CURR_FILE.lineno);

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

	/* remember start point */
	mhist_push (cnf.ring_curr, CURR_FILE.lineno);

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
	int ret=0;

	if (ext_cmd[0] == '\0') {
		tracemsg("no filter command");
		return (0);
	}

	ret = filter_cmd_eng(ext_cmd, OPT_IN_OUT | OPT_REDIR_ERR | OPT_SILENT);

	return (ret);
}

/*
** filter_shadow_cmd - start filter command in shell and feed lines into child process and catch output,
**	push out shadow line markers together with selection lines, or all lines if no selection,
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
		if (j < sizeof(cmd)) {
			cmd[j] = ext_cmd[i];
			args[j] = ext_cmd[i];
			j++;
		}
	}
	cmd[j] = '\0';
	/* append the rest to the arguments */
	for (; ext_cmd[i] != '\0'; i++) {
		if (j < sizeof(args)) {
			args[j++] = ext_cmd[i];
		}
	}
	args[j] = '\0';

	ret = read_pipe ("*sh*", cmd, args, opts);

	return (ret);
}

/*
** lsdir_cmd - create directory listing in special buffer for easy navigation and opening, argument must be a directory
*/
int
lsdir_cmd (const char *ext_cmd)
{
	int ret=0;
	char dirpath[FNAMESIZE];
	int xlen=0;
	struct stat test;

	cnf.lsdir_opts = LSDIR_OPTS;	/* defaults */

	/* the rest must be the dirpath
	*/
	strncpy(dirpath, ext_cmd, sizeof(dirpath)-2);
	dirpath[sizeof(dirpath)-2] = '\0';
	xlen = strlen(dirpath);
	strip_blanks (0x3, dirpath, &xlen);	/* strip only, do not squeeze */
	if (xlen < 1) {
		strncpy(dirpath, "./", 3);
		xlen=2;
	} else {

		/* tilde expansion */
		glob_name(dirpath, sizeof(dirpath));
		xlen = strlen(dirpath);

		if (stat(dirpath, &test)==0 && (S_ISDIR(test.st_mode))) {
			if (dirpath[xlen-1] != '/') {
				dirpath[xlen++] = '/';
				dirpath[xlen] = '\0';
			}
		} else {
			tracemsg("%s is not a directory name", dirpath);
			return 1;
		}
	}

	/* open or switch to */
	if ((ret = scratch_buffer("*ls*")) != 0) {
		return (ret);
	}
	/* cnf.ring_curr is set now */

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

		/* squeeze */
		xlen = strlen(next);
		strip_blanks (0x7, next, &xlen);
		if (xlen > TEXTCOLS-1)
			next[TEXTCOLS-1] = '\0';

		tracemsg("%s", next);
		FREE(iobuffer); iobuffer = NULL;
		ret = 0;
	}

	return (ret);
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
	int ptm, pts;
	char slave[FNAMESIZE], *sp;

	/* copy and parse args[] */
	strncpy(token_str, ext_argstr, sizeof(token_str)-1);
	token_str[sizeof(token_str)-1] = '\0';
	if ((xx = parse_args (token_str, args)) < 1) {
		PIPE_LOG(LOG_ERR, "parse_args() failed");
		return (-1);
	}

	/* check command */
	if (ext_cmd[0] == '\0') {
		PIPE_LOG(LOG_ERR, "no command to launch");
		return (-1);
	}
	strncpy(cmd, ext_cmd, sizeof(cmd)-1);
	cmd[sizeof(cmd)-1] = '\0';

#ifdef DEVELOPMENT_VERSION
	{
		int i;
		char temp[1024];
		memset(temp, 0, sizeof(temp));
		for (i=0; i<xx; i++) {
			strncat(temp, " ", 1);
			strncat(temp, args[i], 100);
		}
		PIPE_LOG(LOG_DEBUG, "(development) cmd: %s args:%s", cmd, temp);
	}
#endif

	if (opts & OPT_INTERACT) {
		if ((ptm = open("/dev/ptmx", O_RDWR)) < 0) {
			PIPE_LOG(LOG_ERR, "open /dev/ptmx failed (%s)", strerror(errno));
			return (-1);
		}
		if (grantpt(ptm) || unlockpt(ptm)) {
			PIPE_LOG(LOG_ERR, "grantpt or unlockpt failed (%s)", strerror(errno));
			return (-1);
		}
		if ((sp = ptsname(ptm)) == NULL) {
			PIPE_LOG(LOG_ERR, "ptsname failed (%s)", strerror(errno));
			return (-1);
		}
		strncpy(slave, sp, sizeof(slave));
		slave[sizeof(slave)-1] = '\0';
		if ((pts = open(slave, O_RDWR)) < 0) {
			PIPE_LOG(LOG_ERR, "open slave [%s] failed (%s)", slave, strerror(errno));
			return (-1);
		}
		PIPE_LOG(LOG_DEBUG, "slave pty [%s] ptm %d pts %d", slave, ptm, pts);
		/* parent */
		out_pipe[XREAD] = ptm;
		in_pipe[XWRITE] = ptm;
		/* child */
		out_pipe[XWRITE] = pts;
		in_pipe[XREAD] = pts;
	} else {
		if ((pipe(out_pipe) != 0) || (pipe(in_pipe) != 0)) {
			PIPE_LOG(LOG_ERR, "pipe() failed (%s)", strerror(errno));
			return (-1);
		}
	}
	if ((chrw = fork()) == -1) {
		PIPE_LOG(LOG_ERR, "fork() [r/w] failed (%s)", strerror(errno));
		close(out_pipe[XREAD]);
		close(out_pipe[XWRITE]);
		close(in_pipe[XREAD]);
		close(in_pipe[XWRITE]);
		return (-1);
	}

	if (chrw == 0) {
		/* child process
		*/
		struct sigaction sigAct;
		memset(&sigAct, 0, sizeof (sigAct));

		if (opts & OPT_INTERACT) {
			dup2(out_pipe[XWRITE], 1);		/* stdout */
			if (opts & OPT_REDIR_ERR) {
				dup2(out_pipe[XWRITE], 2);	/* redir stderr */
			}
			if (opts & (OPT_IN_OUT | OPT_INTERACT)) {
				dup2(in_pipe[XREAD], 0);	/* stdin from pipe */
			}
		} else {
			dup2(out_pipe[XWRITE], 1);		/* stdout */
			if (opts & OPT_REDIR_ERR) {
				dup2(out_pipe[XWRITE], 2);	/* redir stderr */
			} else {
				close(2);			/* drop stderr */
			}
			close(out_pipe[XREAD]);
			close(out_pipe[XWRITE]);
			if (opts & OPT_IN_OUT) {
				dup2(in_pipe[XREAD], 0);	/* stdin from pipe */
			} else {
				close(0);			/* no stdin */
			}
			close(in_pipe[XREAD]);
			close(in_pipe[XWRITE]);
		}

		/* reset signal handler for child
		*/
		sigAct.sa_handler = SIG_DFL;
		sigaction(SIGHUP, &sigAct, NULL);
		sigaction(SIGINT, &sigAct, NULL);
		sigaction(SIGQUIT, &sigAct, NULL);
		sigaction(SIGTERM, &sigAct, NULL);
		sigaction(SIGCHLD, &sigAct, NULL);
		sigaction(SIGPIPE, &sigAct, NULL);
		sigaction(SIGUSR1, &sigAct, NULL);
		sigaction(SIGUSR2, &sigAct, NULL);

		if (execvp (cmd, args) == -1) {
			PIPE_LOG(LOG_ERR, "child %d: execvp() [%s] failed (%s)", getpid(), cmd, strerror(errno));
		}
		exit(EXIT_SUCCESS);
	}

	/* parent process
	*/
	close(out_pipe[XWRITE]);
	close(in_pipe[XREAD]);

	return chrw;
}

/*
** finish_in_fg - finish reading output of external process in foreground,
**	read from pipe until process finished
*/
int
finish_in_fg (void)
{
	int ret = 0;

	CURR_FILE.pipe_opts |= OPT_NOBG;
	PIPE_LOG(LOG_DEBUG, "ri=%d", cnf.ring_curr);

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
	if ((opts & OPT_BASE_MASK) == OPT_STANDARD) {
		if ((opts & (OPT_NOBG | OPT_INTERACT)) == (OPT_NOBG | OPT_INTERACT)) {
			/* code error */
			PIPE_LOG(LOG_CRIT, "wrong options 0x%x for %s, interactive must be background process!", opts, sbufname);
			return (1);
		}
	} else {
		if ((opts & ~(OPT_BASE_MASK | OPT_COMMON_MASK)) != 0) {
			/* code error */
			PIPE_LOG(LOG_CRIT, "wrong (non standard) options 0x%x for custom processing", opts);
			return (1);
		}
	}

	if ((opts & OPT_BASE_MASK) == OPT_STANDARD) {
		/* open or switch to */
		if ((ret = scratch_buffer(sbufname)) != 0) {
			return (ret);
		}
	}
	/* cnf.ring_curr is set now */
	if (CURR_FILE.pipe_output != 0) {
		tracemsg("running background process!");
		PIPE_LOG(LOG_DEBUG, "background process (%s) running! ring:%d pipe:%d", sbufname, cnf.ring_curr, CURR_FILE.pipe_output);
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
		tracemsg("failed to start external tool"); /* remain in this buffer */
		return (2);
	}
	CURR_FILE.chrw = chrw;

	PIPE_LOG(LOG_DEBUG, "fork -- ring:%d new:%d child pid:%d child pipes: in=%d out=%d",
		ring_i, cnf.ring_curr, chrw, in_pipe[XWRITE], out_pipe[XREAD]);

	/* common */
	if (opts & OPT_IN_OUT) {
		/* feed external r/w process
		*/
		if ((opts & OPT_IN_OUT_FOCUS) == OPT_IN_OUT_FOCUS) {
			/* focus line from the original buffer */
			lno_write = write_out_chars(in_pipe[XWRITE], 
				cnf.fdata[ring_i].curr_line->buff, cnf.fdata[ring_i].curr_line->llen);
		} else if ((opts & OPT_IN_OUT_REAL_ALL) == OPT_IN_OUT_REAL_ALL) {
			/* all lines from the original buffer */
			lno_write = write_out_all_lines(in_pipe[XWRITE],
				ring_i);
		} else if ((opts & OPT_IN_OUT_VIS_ALL) == OPT_IN_OUT_VIS_ALL) {
			/* all visible lines, with or without shadow mark */
			lno_write = write_out_all_visible(in_pipe[XWRITE],
				ring_i, (opts & OPT_IN_OUT_SH_MARK));
		} else {
			/* selection lines, with or without shadow mark */
			lno_write = wr_select(in_pipe[XWRITE],
				(opts & OPT_IN_OUT_SH_MARK));
		}
		PIPE_LOG(LOG_DEBUG, "child input: pipe=%d (ring %d) -- wrote %d line(s)", in_pipe[XWRITE], ring_i, lno_write);
	}

	/* common */
	if ((opts & OPT_INTERACT) == 0) {
		close(in_pipe[XWRITE]);
		in_pipe[XWRITE] = 0;
	}

	if ((opts & OPT_BASE_MASK) == OPT_STANDARD) {
		/* additional flags */
		CURR_FILE.fflag |= FSTAT_SPECW;
		if (opts & OPT_INTERACT) {
			CURR_FILE.fflag |= FSTAT_INTERACT;
			/* for prompt recognition */
			CURR_FILE.last_input_length = 0;
		} else {
			/* disable inline editing, adding lines */
			CURR_FILE.fflag |= (FSTAT_NOEDIT | FSTAT_NOADDLIN);
		}

		if ((opts & OPT_SILENT) == 0) {
			/* first line: header
			*/
			char header_str[CMDLINESIZE];
			strncpy(header_str, (getuid() ? "$ " : "# "), 3);
			strncat(header_str, ext_argstr, sizeof(header_str)-5);
			strncat(header_str, "\n", 2);
			if (insert_line_before (CURR_FILE.bottom, header_str) != NULL) {
				CURR_FILE.num_lines++;
			} else {
				ret = 2;
			}
		}
		if (opts & OPT_INTERACT) {
			split_line();
			/* switch to text area */
			CURR_FILE.fflag &= ~FSTAT_CMD;
		}
	}

	/* common */
	CURR_FILE.pipe_opts = opts;
	CURR_FILE.pipe_input = in_pipe[XWRITE];
	CURR_FILE.pipe_output = out_pipe[XREAD];

	if ((opts & OPT_BASE_MASK) == OPT_STANDARD) {
		if (CURR_FILE.lineno >= CURR_FILE.num_lines - ((opts & OPT_SILENT) ? 0 : 1)) {
			/* append goes after bottom->prev, so force pull up current */
			CURR_FILE.curr_line = CURR_FILE.bottom->prev;
			CURR_FILE.lineno = CURR_FILE.num_lines;
			CURR_LINE->lflag &= ~LMASK(cnf.ring_curr);	/* unhide */
			update_focus(INCR_FOCUS_ONCE, cnf.ring_curr, 0, NULL);
		}

		/* (re)set origin */
		if (ring_i != cnf.ring_curr) {
			CURR_FILE.origin = ring_i;
		}

		if (opts & OPT_NOBG) {
			/* finish in foreground -- loop stops if pipe_output closed
			*/
			PIPE_LOG(LOG_DEBUG, "ri=%d [%s] -- finish in foreground", cnf.ring_curr, sbufname);
			ret = finish_in_fg();
			/* finished */
		} else {
			/* continue in background
			*/
			if (fcntl(CURR_FILE.pipe_output, F_SETFL, O_NONBLOCK) == -1) {
				PIPE_LOG(LOG_ERR, "read pipe, fcntl (pipe %d) failed (%s)", CURR_FILE.pipe_output, strerror(errno));
			}
			PIPE_LOG(LOG_DEBUG, "ri=%d [%s] ret=%d -- continue in background", cnf.ring_curr, sbufname, ret);
		}
	} else {
		PIPE_LOG(LOG_DEBUG, "ri=%d -- external processing", cnf.ring_curr);
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
	int ni, total=0, finish=0, pull, got, count;
	int ring_orig = cnf.ring_curr;
	int status=0;
	static int zombie = 0;

	/* init */
	if (cnf.fdata[ring_i].readbuff == NULL) {
		cnf.fdata[ring_i].readbuff = (char *) MALLOC(LINESIZE_INIT+1);
		cnf.fdata[ring_i].rb_nexti = 0;
		if (cnf.fdata[ring_i].readbuff == NULL) {
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
			got = getxline ((rb+ni), &count, 
				LINESIZE_INIT - ni, cnf.fdata[ring_i].pipe_output);
			finish = (got == 0 || got == -1);
			ni += count;
			if ((ni > 0) && (finish || rb[ni-1] == '\n' || ni > LINESIZE_INIT-10)) {
				break;
			}
		}
		cnf.fdata[ring_i].rb_nexti = ni;
		ret = (finish) ? 1 : 0;
		PIPE_LOG(LOG_DEBUG, "-- ri=%d count=%d finish=%d (ni=%d ret=%d)", ring_i, count, finish, ni, ret);

	} else if (cnf.fdata[ring_i].fflag & FSTAT_INTERACT) {
		/* interactive bg process */

		cnf.ring_curr = ring_i;
		got = read (cnf.fdata[ring_i].pipe_output, rb, LINESIZE_INIT);
		if (got == -1) {
			if (++zombie == ZOMBIE_DELAY) {
				if (check_zombie(ring_i) == -1) {
					stop_bg_process();	/* defunct */
				}
				zombie = 0;
			}
		} else if (got > 0) {
			rb[got] = '\0';
			/* no real terminal handling here, just filter out ESC sequencies */
			filter_esc_seq(rb, &got);
			ret = type_text(rb);	/* add/append to text buffer */
			zombie = 0;
		}
		cnf.ring_curr = ring_orig;

	} else {
		/* standard processing */

		LINE *lp=NULL, *lx=NULL;
		lp = cnf.fdata[ring_i].bottom->prev;

		pull = (cnf.fdata[ring_i].lineno >= cnf.fdata[ring_i].num_lines);

		while (ret==0 && !finish && total < LINESIZE_INIT) {
			got = getxline ((rb+ni), &count, 
				LINESIZE_INIT - ni, cnf.fdata[ring_i].pipe_output);
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

		if (finish) {
			/* pipe_output must be closed -- that is the flag
			*/
			PIPE_LOG(LOG_DEBUG, "-- ri=%d, pipe=%d finished --- wait4", ring_i, cnf.fdata[ring_i].pipe_output);
			status = wait4_bg(ring_i);	/* finished */

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
				PIPE_LOG(LOG_DEBUG, "-- ri=%d [%s] foreground task finished (status=%d, ret=%d)",
					ring_i, cnf.fdata[ring_i].fname, status, ret);
			} else {
				PIPE_LOG(LOG_DEBUG, "-- ri=%d [%s] background task finished (status=%d, ret=%d)",
					ring_i, cnf.fdata[ring_i].fname, status, ret);
			}
		}

		/* pull current line and focus */
		if (ret==0 && pull) {
			cnf.fdata[ring_i].curr_line = cnf.fdata[ring_i].bottom->prev;
			cnf.fdata[ring_i].lineno = cnf.fdata[ring_i].num_lines;
			cnf.fdata[ring_i].curr_line->lflag &= ~LMASK(cnf.ring_curr);
			update_focus(FOCUS_ON_LAST_LINE, ring_i, 0, NULL);
		}
	}

	return (ret);
} /* readout_pipe */

/*
* handle all background pipes with readout_pipe() calls
* return 0 if nothing happened in current buffer
* return 1 after changes to current buffer
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
				PIPE_LOG(LOG_ERR, "readout_pipe() failed, ri=%d -- error %d", ri, err);
				ret = -1;	/* error */
			} else if (err == 0) {
				/* change happened */
				ret = (ri == cnf.ring_curr) ? (ret | 1) : ret;
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
			if (waitpid(cnf.fdata[ri].chrw, &status, 0) == -1) {
				PIPE_LOG(LOG_ERR, "waitpid() pid=%d failed: (%s)", cnf.fdata[ri].chrw, strerror(errno));
				kill(cnf.fdata[ri].chrw, SIGHUP);
				status = -1;
			} else {
				if (WIFEXITED(status)) {
					status = WEXITSTATUS(status);
					PIPE_LOG(LOG_DEBUG, "--- ri=%d chrw=%d status=%d", ri, cnf.fdata[ri].chrw, status);
				} else {
					PIPE_LOG(LOG_DEBUG, "--- ri=%d chrw=%d unknown status 0x%X", ri, cnf.fdata[ri].chrw, status);
					kill(cnf.fdata[ri].chrw, SIGKILL);
					status = -1;
				}
			}
			cnf.fdata[ri].chrw = -1;
		}
	}

	return status;
}

/*
* check bg process if alive or defunct
*/
static int
check_zombie (int ri)
{
	int status=0;

	if (0 <= ri && ri < RINGSIZE) {
		if (cnf.fdata[ri].chrw > 0) {
			if (waitpid(cnf.fdata[ri].chrw, &status, WNOHANG) == -1) {
				/* defunct -> gone */
				PIPE_LOG(LOG_DEBUG, "waitpid pid=%d (%s)", cnf.fdata[ri].chrw, strerror(errno));
				cnf.fdata[ri].chrw = -1;
				status = -1;
			} else {
				status = 0;
			}
		} else {
			status = -1;
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
			PIPE_LOG(LOG_DEBUG, "kill pid=%d status=%d", CURR_FILE.chrw, status);
			waitpid(CURR_FILE.chrw, &status, WNOHANG);
			PIPE_LOG(LOG_DEBUG, "waitpid pid=%d status=0x%X", CURR_FILE.chrw, status);
			CURR_FILE.chrw = -1;
		}
		if (CURR_FILE.fflag & FSTAT_INTERACT) {
			CURR_FILE.fflag &= ~FSTAT_INTERACT;
		}
	}

	return (0);
}

/*
* get a line like fgets() with getxline_filter(),
* fix inline CR, let pass CR/LF through and drop control chars
* returns 0=eof, 1=ok, 2=EAGAIN, -1=error
*/
static int
getxline (char *buff, int *count, int max, int fd)
{
	int ch, ch_prev=0;
	int cnt=0, ret=0;

	while (cnt < max-2) {
		ch=0;
		ret = read(fd, &ch, 1);
		if (ret == 0) {
			/* eof */
			break;
		} else if (ret == -1) {
			if (errno == EAGAIN) {
				ret = 2;
				break;
			} else {
				PIPE_LOG(LOG_ERR, "getxline, read (from pipe) failed (%s)", strerror(errno));
				ret = -1;
				break;
			}
		} else {
			ret = 1;
			if (ch == '\n') {
				if (ch_prev == '\r')
					buff[cnt++] = '\r';
				buff[cnt++] = '\n';
				break;
			} else if (ch == 0x09) {
				/* tab */
				buff[cnt++] = ch;
			} else if (ch >= 0x20 && ch != 0x7f) {
				/* printable */
				buff[cnt++] = ch;
			} else if (ch == 0x08) {
				/* backspace */
				if (cnt > 0)
					buff[--cnt] = '\0';
			}
			ch_prev = ch;
		}
	}

	buff[cnt] = '\0';
	*count = cnt;

	return ret;
}

/*
* filter the input buffer to remove control characters, ESC sequencies, etc
*/
static void
filter_esc_seq (char *buff, int *count)
{
	int i=0, j=0;
	int seq = 0;

	while (j < *count) {
		if (j+1 < *count && buff[j] == '\r' && buff[j+1] == '\n') {
			buff[i++] = '\n';
			j += 2;
		} else if (buff[j] == '\n' || buff[j] == '\r') {
			buff[i++] = '\n';
			j++;
		} else if ((buff[j] == 0x09) || (buff[j] >= 0x20 && buff[j] != 0x7f)) {
			buff[i++] = buff[j];
			j++;
		} else if (buff[j] == 0x08) {
			/* backspace */
			if (i > 0)
				--i;
			j++;
		} else if (buff[j] == 0x1B) {
			/* filter out terminal ESC sequencies (like 1B 5D 30 3B .... 07),
			* interactive sessions, like ssh, can push into the pipe
			* ---- real terminal handling is not possible here
			*/
			seq = 0;
			if (j+3 < *count && buff[j+1] == 0x5D && buff[j+2] == 0x30 && buff[j+3] == 0x3B) {
				while (j+seq < *count && seq < 100) {
					if (buff[j+seq] == 0x07) {
						seq++;
						break;
					} else {
						seq++;
					}
				}
				if (j+seq < *count && seq < 100) {
					j += seq;
				} else {
					j += 4;
				}
			} else {
				j++;
			}
		} else {
			j++;
		}
	}

	buff[i] = '\0';
	*count = i;

	return;
}
