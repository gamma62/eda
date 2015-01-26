/*
* main.c
* main function, init, defaults, signal handler and log tools
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
#include <stdlib.h>		/* malloc, realloc, free */
#include <signal.h>
#include <sys/types.h>		/* getpid */
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <syslog.h>
#include <locale.h>
#include <errno.h>
#include <stdarg.h>
#include "main.h"
#include "proto.h"

/* global config */
CONFIG cnf;

#ifdef DEVELOPMENT_VERSION
const char long_version_string[] = "eda v." VERSION "-rev." REV;
#else
const char long_version_string[] = "eda v." VERSION;
#endif

/* local proto */
static void sigh (int sig);
static void leave (const char *reason);
static void set_defaults (void);

/*
* Set defaults, get args, set sighandler, start monitor.
*/
int
main (int argc, char *argv[])
{
	int opt;
	char find_symbol[TAGSTR_SIZE];
	int loadtags = 0, keytest = 0;
	int which_next = 0;
	struct sigaction sigAct;

	memset(find_symbol, 0, sizeof(find_symbol));
	set_defaults();

	if (mkdir(cnf.myhome, 0755) == -1) {
		if (errno != EEXIST) {
			leave("cannot create ~/.eda directory");
		}
	}

	/*
	* argument processing
	*/
	while ((opt = getopt(argc, argv, "hVkp:tj:a:n")) != -1)
	{
		switch (opt) {
		case 'p':
			strncpy(cnf.project, optarg, sizeof(cnf.project));
			cnf.project[sizeof(cnf.project)-1] = '\0';
			break;
		case 't':
			loadtags = 1;
			break;
		case 'j':
			loadtags = 1;
			strncpy(find_symbol, optarg, sizeof(find_symbol));
			find_symbol[sizeof(find_symbol)-1] = '\0';
			break;
		case 'a':
			strncpy(cnf.automacro, optarg, sizeof(cnf.automacro));
			cnf.automacro[sizeof(cnf.automacro)-1] = '\0';
			break;
		case 'k':
			keytest = 1;
			break;
		case 'n':
			cnf.noconfig = 1;
			break;
		case 'V':
			printf ("%s\n", long_version_string);
			printf ("\n\
Copyright 2003-2015 Attila Gy. Molnar.\n\
\n\
Eda is free software: you can redistribute it and/or modify\n\
it under the terms of the GNU General Public License as published by\n\
the Free Software Foundation, either version 3 of the License, or\n\
(at your option) any later version.\n\
\n\
Eda is distributed in the hope that it will be useful,\n\
but WITHOUT ANY WARRANTY; without even the implied warranty of\n\
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n\
GNU General Public License for more details.\n\
\n\
You should have received a copy of the GNU General Public License\n\
along with Eda.  If not, see <http://www.gnu.org/licenses/>.\n\
\n");
			leave("");	/* version */

		case 'h':
		case ':':	/* missing parameter for one of the options */
		case '?':	/* unknown option character */
		default:
			printf ("Usage:\n\
	eda -h\n\
		this help\n\
	eda -V\n\
		print version and exit\n\
	eda -k\n\
		run as key tester, to help create sequence file\n\
	eda [options] filename [+line] [filename [+line] ...]\n\
		options: \n\
		-p project	load project with settings and file list\n\
		-t 		force tags file load on startup\n\
		-j symbol	jump to symbol after startup\n\
		-a macro	macro name to start automatically after startup\n\
		-n 		no configuration file processing\n\
\n");
			leave("");	/* help */
		}
	}
	/* args post-processing later */

	/* init point for memory management control */

	/* setup syslog */
#ifdef DEVELOPMENT_VERSION
	openlog("eda", LOG_PID, LOG_LOCAL0);
	if (cnf.noconfig) {
		setlogmask( LOG_MASK(LOG_EMERG) );
	}
#endif

	if (process_rcfile(cnf.noconfig) || process_keyfile(cnf.noconfig) || process_seqfile(cnf.noconfig)) {
		leave("resource processing failed");
	}
	if (process_macrofile(cnf.noconfig)) {
		leave("macro processing failed");
	}

	/* set signal handler */
	memset(&sigAct, 0, sizeof (sigAct));
	sigemptyset(&sigAct.sa_mask);
	sigAct.sa_handler = sigh;
	sigaction(SIGHUP, &sigAct, NULL);	/* Hangup || control proc died */
	sigaction(SIGINT, &sigAct, NULL);	/* interrupt from keyb (Ctrl-C) */
	sigaction(SIGQUIT, &sigAct, NULL);	/* quit from keyb (Ctrl-\) */
	sigaction(SIGPIPE, &sigAct, NULL);	/* catch and ignore */
	sigaction(SIGTERM, &sigAct, NULL);	/* (first) termination signal */
	/* signal(SIGCHLD, SIG_DFL); --- do not catch it; needed for waitpid() and Unix98 pty */
	sigAct.sa_handler = SIG_IGN;
	sigaction(SIGUSR1, &sigAct, NULL);
	sigaction(SIGUSR2, &sigAct, NULL);

	if (keytest) {
		if (isatty(0) && isatty(1))
			key_test();
		leave("keytest finished");
	}

	/* load project, settings and files --- before log start */
	if (cnf.project[0] != '\0') {
		if (process_project(cnf.noconfig)) {
			leave("project processing failed");
		}
	}

	MAIN_LOG(LOG_NOTICE, ">>> pid=%d %s", getpid(), long_version_string);

	/* parameters */
	while ((optind < argc) && cnf.ring_size < RINGSIZE) {
		if (optind+1 < argc && (argv[optind+1][0] == '+' || argv[optind+1][0] == ':')) {
			/* <filename> +<linenumber>
			* <filename> :<linenumber>
			*/
			if (add_file(argv[optind]) == 0) {
				/* current file */
				goto_line(argv[optind+1]);
			}
			optind += 2;
		} else if (optind+1 < argc && (argv[optind][0] == '+')) {
			/* +<linenumber> <filename>
			*/
			if (add_file(argv[optind+1]) == 0) {
				/* current file */
				goto_line(argv[optind]);
			}
			optind += 2;
		} else {
			if (strchr(argv[optind], ':') != NULL) {
				/* <filename>:<linenumber>
				*/
				simple_parser(argv[optind], SIMPLE_PARSER_JUMP);
			} else {
				/* <filename>
				*/
				add_file(argv[optind]);
			}
			optind++;
		}
	}
	which_next = -cnf.ring_size;

	/* tags load and jump to */
	if (loadtags) {
		if (!tag_load_file()) {
			if (find_symbol[0] != '\0') {
				which_next = tag_jump_to(find_symbol); /* 0 or 1 */
				/* 0 - cannot open tags file --> empty ring if no other files
				*  0 - one match (success)
				*  1 - multiple matches, jump to the first
				*  1 - no match --> empty ring if other files
				*/
			}
		}
	}

#ifndef NO_STDIN_PIPE
	/* stdin pipe --- without other options */
	if ((cnf.ring_size == 0) && ( !isatty(0) )) {
		read_stdin();
	}
#endif

	/* nothing else? */
	if (cnf.ring_size == 0) {
#ifdef OPEN_NONAME
		if (scratch_buffer("*noname*") != 0) {
			MAIN_LOG(LOG_NOTICE, "<<< opening scratch file failed");
			leave("opening scratch file failed");
		}
#else
		MAIN_LOG(LOG_NOTICE, "<<< empty ring");
		leave("empty ring");
#endif
	} else {
		/* select the first file in the ring -- except jump required */
		if (which_next < -1) {
			/* ring_size > 1 and tag_load_file failed or not required */
			cnf.ring_curr = cnf.ring_size;
			next_file();
		}
	}

	if (!isatty(0) || !isatty(1)) {
		MAIN_LOG(LOG_NOTICE, "<<< stdin or stdout is not tty");
		leave("no tty");	/* isatty */
	}

	editor();
	leave("");	/* minden jo, ha vege jo */

	return (0);
}

/*
* Signal handler
*/
static void
sigh (int sig)
{
	static time_t last=0;
	time_t now;

	if (sig == SIGPIPE) {
		return;		/* ignored */
	}

	/* handle accidental keyb interrupt (^C) */
	if (sig == SIGINT) {
		now = time(NULL);
		/* 5 seconds */
		if (now > last+5) {
			last = now;
			return;
		}
	}

	MAIN_LOG(LOG_ERR, "signal %d", sig);

	if (cnf.bootup) {
		/* this is signal handler, do not call
		* clear (); refresh ();
		*/
		endwin();
	}

	exit(EXIT_SUCCESS);
}

/*
* Exit from the program, regular retire, not an emergency
* clean up everything, to check we can do it
*/
static void
leave (const char *reason)
{
	/* free up buffers, stop bg proc, etc */
	drop_all();

	/* sequence tree */
	free_seq_tree(cnf.seq_tree);

	/* tags */
	tag_rm_all();

	/* motion history -- useless */
	mhist_clear(-1);

	/* keys and macros */
	drop_macros();

	if (reason && reason[0] != '\0') {
		fprintf(stderr, "%s\n", reason);
	}
#ifdef DEVELOPMENT_VERSION
	closelog();
#endif

	/* finish point for memory management control */
	exit(EXIT_SUCCESS);
}

/*
* config defaults
*/
static void
set_defaults(void)
{
	unsigned i=0, groupsize=0;
	char *ptr=NULL;

	memset(&cnf, 0, sizeof(cnf));
	cnf.bootup = 0;
	cnf.noconfig = 0;

	/* to avoid special characters in pipe inputs */
	setenv("LANG", "C", 1);
	setlocale(LC_ALL, "C"); /* this may fail */

	/* user, group */
	cnf.uid = getuid();
	cnf.gid = getgid();
	cnf.euid = geteuid();
	cnf.egid = getegid();
	groupsize = (sizeof(cnf.groups) / sizeof(cnf.groups[0]));
	for (i=0; i<groupsize; i++)
		cnf.groups[i] = 0;
	if (getgroups(groupsize-1, cnf.groups) < 0) {
		cnf.groups[0] = cnf.egid;
	}

	/* process */
	cnf.pid = getpid();
	cnf.ppid = getppid();
	cnf.sid = getsid(0);
	cnf.pgid = getpgid(0);
	/*
	*	char *cterminal;
	*	cterminal = ctermid(NULL);
	*	ctermfd = open(cterminal, O_NOCTTY, O_RDONLY);
	*	if (ctermfd > 0) {
	*		//session id from controlling tty -- tcgetsid(ctermfd)
	*		//terminal foreground process group from controlling tty -- tcgetpgrp(ctermfd)
	*		close(ctermfd);
	*	}
	*/

	cnf.starttime = time(NULL);

	/* global settings */
	cnf.gstat = 0;
	cnf.gstat |= (GSTAT_SHADOW | GSTAT_SMARTIND | GSTAT_MOVES | GSTAT_CASES);
	cnf.gstat |= (GSTAT_AUTOTITLE | GSTAT_INDENT | GSTAT_NOKEEP);
	cnf.gstat |= (GSTAT_CLOSE_OVER | GSTAT_SAVE_INODE);
	cnf.gstat &= ~(GSTAT_MOUSE);	/* explicit off */
	cnf.tabsize = 8;
	cnf.indentsize = (cnf.gstat & GSTAT_INDENT) ? 1 : 4;	/* 1 tab or 4 spaces */
	cnf.pref = (cnf.gstat & GSTAT_PREFIX) ? PREFIXSIZE : 0;

	cnf.palette = 0;
	cnf.trace = 0;		/* count of tracemsg[] lines */

	/* wgetch engine and terminal resize */
	cnf.seq_tree = NULL;

	/* file ring, first file */
	cnf.ring_curr = 0;
	cnf.ring_size = 0;
	for(i=0; i < RINGSIZE; i++) {
		cnf.fdata[i].basename[0] = '\0';
		cnf.fdata[i].dirname[0] = '\0';
		cnf.fdata[i].flevel = 1;
		cnf.fdata[i].fflag = 0;
		cnf.fdata[i].curr_line = NULL;
		cnf.fdata[i].top = NULL;
		cnf.fdata[i].bottom = NULL;
		cnf.fdata[i].pipe_output = 0;
		cnf.fdata[i].pipe_input = 0;
		cnf.fdata[i].readbuff = NULL;
		cnf.fdata[i].rb_nexti = 0;
	}

	/* selection */
	cnf.select_ri = -1;
	cnf.select_w = 0;

	/* command line history */
	cnf.clhistory = NULL;
	cnf.clhist_size = 0;
	cnf.cmdline_buff[0] = '\0';
	cnf.cmdline_len = 0;
	cnf.clpos = 0;
	cnf.cloff = 0;

	/* tags info tree */
	cnf.taglist = NULL;

	/* bookmarking */
	for(i=0; i < 10; i++)
		cnf.bookmark[i].ring = -1;

	/* motion history */
	cnf.mhistory = NULL;

	/*
	* external tools and settings
	* (overwrite from rcfile)
	*/
	strncpy(cnf.find_path,	"/usr/bin/find",	sizeof(cnf.find_path));
	strncpy(cnf.find_opts,	". -type f -name '*.[ch]' -exec egrep -nH -w", sizeof(cnf.find_opts));
	strncpy(cnf.tags_file,	"./tags",		sizeof(cnf.tags_file));
	strncpy(cnf.make_path,	"/usr/bin/make",	sizeof(cnf.make_path));
	strncpy(cnf.make_opts,	"-f Makefile",		sizeof(cnf.make_opts));
	strncpy(cnf.sh_path,	"/bin/bash",		sizeof(cnf.sh_path));
	strncpy(cnf.diff_path,	"/usr/bin/diff",	sizeof(cnf.diff_path));
	strncpy(cnf.ssh_path,	"/usr/bin/ssh",		sizeof(cnf.ssh_path));
	for(i=0; i < 10; i++) {
		cnf.vcs_tool[i][0] = '\0';
		cnf.vcs_path[i][0] = '\0';
	}


	ptr = getenv("HOME");
	if (ptr != NULL) {
		strncpy(cnf._home, ptr, sizeof(cnf._home));
		cnf._home[sizeof(cnf._home)-1] = '\0';
	} else {
		cnf._home[0] = '~';
		cnf._home[1] = '/';
		cnf._home[2] = '\0';
		if (glob_name(cnf._home, sizeof(cnf._home))) {
			cnf._home[0] = '\0';
			fprintf(stderr, "get home dir failed (%s)\n", strerror(errno));
		}
	}
	/* donot trust on (nonstatic) environment variables, like PWD
	*/
	if (getcwd(cnf._pwd, sizeof(cnf._pwd)-1) == NULL) {
		cnf._pwd[0] = '\0';
		fprintf(stderr, "getcwd failed (%s)\n", strerror(errno));
	}

	strncpy(cnf.myhome, cnf._home, sizeof(cnf.myhome));
	cnf.myhome[sizeof(cnf.myhome)-10] = '\0';
	strncat(cnf.myhome, "/.eda/", 10);
	cnf.l1_home = strlen(cnf._home);
	cnf.l1_pwd = strlen(cnf._pwd);
	cnf.l2_myhome = strlen(cnf.myhome);

	for(i=0; i < sizeof(cnf.log) / sizeof(cnf.log[0]); i++) {
		cnf.log[i] = 0;
	}

	/* testing */
	memset(cnf.automacro, 0, sizeof(cnf.automacro));

	return;
}

/*
* trace messages on the display (text area rows)
* with some word-wrapping
*/
void
tracemsg(const char *format, ...)
{
	char temp[1024];	/* instead of TRACESIZE*CMDLINESIZE */
	int i=0, j=0, k=0;
	va_list args;

	/* do nothing if cnf.maxx is 0,
	 * like before cnf.bootup
	 */
	if (!cnf.bootup || (cnf.gstat & GSTAT_SILENT) || cnf.maxx < 20)
		return;
	if (cnf.trace >= TRACESIZE)
		return;

	va_start (args, format);
	vsnprintf(temp, sizeof(temp)-1, format, args);
	va_end (args);
	temp[sizeof(temp)-1] = '\0'; /* safe */

	while (cnf.trace < TRACESIZE) {
		for (j=0, k=0; j < cnf.maxx-1 && temp[i] != '\0'; j++, i++) {
			cnf.tracerow[cnf.trace][j] = temp[i];
			if ((k == 0) || (j < cnf.maxx - 20)) {
				if (cnf.tracerow[cnf.trace][j] == ' ')
					k = j;
			}
		}

		/* word wrap at last boundary */
		if (j > cnf.maxx - 2) {
			i -= j-k-1;	/* previous word begin */
			j = k;		/* overwrite last space */
		}
		cnf.tracerow[cnf.trace][j] = '\0';

		cnf.trace++;
		if (temp[i] == '\0') {
			break;
		}
	}

	return;
}

/*
* record commands and keystrokes into macro.log, to help creating macros
*/
void
record (const char *funcname, const char *parameter)
{
	FILE *fp = NULL;
	char logfile[sizeof(cnf.myhome)+SHORTNAME];

	strncpy(logfile, cnf.myhome, sizeof(logfile));
	strncat(logfile, "macro.log", SHORTNAME);

	if (funcname != NULL) {
		fp = fopen(logfile, "a");
		if (fp != NULL) {
			if (parameter != NULL && parameter[0] != '\0') {
				fprintf(fp, "\t%s\t%s\n", funcname, parameter);
			} else if (strncmp(funcname, "recording_switch", 12) == 0) {
				fprintf(fp, ".\n");
			} else {
				fprintf(fp, "\t%s\n", funcname);
			}
			fflush(fp);
			fclose(fp);
		}
	} else {
		/* initialize recording */
		fp = fopen(logfile, "w");
		if (fp != NULL) {
			fprintf(fp, "KEY_NONE	sample_macro\n#Description: \n");
			fflush(fp);
			fclose(fp);
		}
	}

	return;
}
