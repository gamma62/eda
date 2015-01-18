/*
* main.h
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

#ifndef _MAIN_H_
#define _MAIN_H_

#include <config.h>
#include <stdio.h>
#include <curses.h>	/* includes stdarg.h */
#include <sys/types.h>
#include <sys/stat.h>
#include <regex.h>

#define MALLOC		malloc
#define REALLOC		realloc
#define FREE(p)		free((p))

#define FNAMESIZE	256		/* size for filename (like d_name in dirent structure) */
#define RINGSIZE	37		/* file ring (array dimension) for buffers */
#define CLHISTSIZE	200		/* command line history depth */
#define CMDLINESIZE	256		/* max command line length, max screen row length, max regexp length; multiple of 16 */
#define PREFIXSIZE	8		/* prefix length */
#define TRACESIZE	12		/* max number of rows in the trace/message */
#define ERRBUFF_SIZE	512		/* for regcomp() failure messages */
#define TAGLINE_SIZE	0x800		/* 2k */
#define TAGSTR_SIZE	160		/* fields in 'tags file', each; multiple of 16 */
#define SEARCHSTR_SIZE	200		/* size of storage for search expression */
#define MAXARGS		32		/* arg count max for args[] -- tokenization, read_pipe() */
#define SHORTNAME	80		/* logfile, *_path, *_opts, rcfile, keyfile, bookmark sample */
#define XPATTERN_SIZE	1024		/* for regexp pattern, after shorthand replacement */
#define PALETTE_MAX	1

#define LINESIZE_INIT	0x1000		/* text line, initial memory allocation ==4096 */
#define LINESIZE_MIN	0x001f		/* (2^5-1) incr/decr step for realloc() ==31 */
/* with space for text lines '\0' */
#define ALLOCSIZE(len)	((size_t) (((len) | LINESIZE_MIN) + 1))

/* bigger allocation steps for change() -- required: 0xff >= CMDLINESIZE ....... miert is? ez a feltetel nem teljesul */
/* 0xff space reserved after allocation */
#define REP_ASIZE(len)	((size_t) (((len) | 0x1f) + 0xff + 1))

#ifdef DEVELOPMENT_VERSION
#define MAIN_LOG(prio, fmt, args...)	if (cnf.log[0]>0 && cnf.log[0]>=prio) syslog(prio, "MAIN: " fmt, ##args)
#define FH_LOG(prio, fmt, args...)	if (cnf.log[1]>0 && cnf.log[1]>=prio) syslog(prio, "FH:%s: " fmt, __FUNCTION__, ##args)
#define CMD_LOG(prio, fmt, args...)	if (cnf.log[2]>0 && cnf.log[2]>=prio) syslog(prio, "CMD:%s: " fmt, __FUNCTION__, ##args)
#define HIST_LOG(prio, fmt, args...)	if (cnf.log[3]>0 && cnf.log[3]>=prio) syslog(prio, "HIST:%s: " fmt, __FUNCTION__, ##args)
#define REPL_LOG(prio, fmt, args...)	if (cnf.log[4]>0 && cnf.log[4]>=prio) syslog(prio, "REPL:%s: " fmt, __FUNCTION__, ##args)
#define TAGS_LOG(prio, fmt, args...)	if (cnf.log[5]>0 && cnf.log[5]>=prio) syslog(prio, "TAGS:%s: " fmt, __FUNCTION__, ##args)
#define SELE_LOG(prio, fmt, args...)	if (cnf.log[6]>0 && cnf.log[6]>=prio) syslog(prio, "SELE:%s: " fmt, __FUNCTION__, ##args)
#define FILT_LOG(prio, fmt, args...)	if (cnf.log[7]>0 && cnf.log[7]>=prio) syslog(prio, "FILT:%s: " fmt, __FUNCTION__, ##args)
#define PIPE_LOG(prio, fmt, args...)	if (cnf.log[8]>0 && cnf.log[8]>=prio) syslog(prio, "PIPE:%s: " fmt, __FUNCTION__, ##args)
#define PD_LOG(prio, fmt, args...)	if (cnf.log[9]>0 && cnf.log[9]>=prio) syslog(prio, "PD:%s: " fmt, __FUNCTION__, ##args)
#define REC_LOG(prio, fmt, args...)	if (cnf.log[10]>0 && cnf.log[10]>=prio) syslog(prio, "REC: " fmt, ##args)
#define UPD_LOG(prio, fmt, args...)	if (cnf.log[11]>0 && cnf.log[11]>=prio) syslog(prio, "UPD: " fmt, ##args)
#else
#define MAIN_LOG(prio, fmt, args...)	;
#define FH_LOG(prio, fmt, args...)	;
#define CMD_LOG(prio, fmt, args...)	;
#define HIST_LOG(prio, fmt, args...)	;
#define REPL_LOG(prio, fmt, args...)	;
#define TAGS_LOG(prio, fmt, args...)	;
#define SELE_LOG(prio, fmt, args...)	;
#define FILT_LOG(prio, fmt, args...)	;
#define PIPE_LOG(prio, fmt, args...)	;
#define PD_LOG(prio, fmt, args...)	;
#define REC_LOG(prio, fmt, args...)	;
#define UPD_LOG(prio, fmt, args...)	;
#endif

/* for wgetch() and timers */
#define CUST_ESCDELAY	20		/* to set global variable ESCDELAY: expire time (original 1000ms) */
#define CUST_WTIMEOUT	50		/* wgetch timeout (miliseconds) */
#define RESIZE_DELAY	3		/* internal counter for wgetch timeouts before reporting KEY_RESIZE */
#define FILE_CHDELAY	(1000/CUST_WTIMEOUT*5)	/* file re-stat timing, 5 seconds */
#define ZOMBIE_DELAY	(1000/CUST_WTIMEOUT*1)	/* check process alive, 1 second */
#define REFRESH_EVENT	(-2)		/* force display refresh */

/* bit masks for global flags */
#define GSTAT_PREFIX	0x00000001	/* view prefix area */
#define GSTAT_SHADOW	0x00000002	/* view filter shadow lines */
#define GSTAT_SMARTIND	0x00000004	/* smart indent for new lines */
#define GSTAT_MOUSE	0x00000008	/* mouse support */
#define GSTAT_MOVES	0x00000010	/* selection move clears selection bit */
#define GSTAT_CASES	0x00000020	/* case sensitive filter/tag/search */
#define GSTAT_INDENT	0x00000040	/* tab indent (or space indent) */
#define GSTAT_NOKEEP	0x00000080	/* backup nokeep (drop backup file after succesfull save) */
#define GSTAT_SILENT	0x00000100	/* silent mode, non-interactive */
#define GSTAT_UPDNONE	0x00000200	/* no screen update required */
#define GSTAT_UPDFOCUS	0x00000400	/* only focus line update required */
#define GSTAT_AUTOTITLE 0x00000800	/* automatic set of xterm title */
#define GSTAT_CLOSE_OVER 0x00001000	/* close the shell buffer after "over" command */
#define GSTAT_SAVE_INODE 0x00002000	/* save file with original inode, replace content; transparent for hardlink/symlink */
#define GSTAT_LOCATE	0x00004000	/* use external or internal search method, default 0, external find/egrep */
#define GSTAT_RECORD	0x00008000	/* macro recording flag */
/*			0x00010000	*/

#define TILDE		"~"
#define TOP_MARK	"<<top>>\n"		/* pass LINESIZE_MIN */
#define BOTTOM_MARK	"<<eof>>\n"		/* pass LINESIZE_MIN */
#define REPLACE_QUEST	"replace: Yes/No/Rest/Quit ?"
#define PROJECT_HEADER	"### project settings ###"	/* project file header (line prefix, mandatory) */
#define PROJECT_CHDIR	"### chdir "			/* project's home dir */
#define PROJECT_FILES	"### project files ###"		/* files section (line prefix, mandatory) */

/* options for directory listing */
#define LSDIR_L		0x0001
#define LSDIR_A		0x0002
#define LSDIR_OPTS	(LSDIR_L | LSDIR_A)

/* bit masks for file flags */
#define FSTAT_CMD	0x00000001	/* focus: command line OR text area */
#define FSTAT_NOEDIT	0x00000002	/* no inline edit */
#define FSTAT_NOADDLIN	0x00000004	/* no line adding */
#define FSTAT_NODELIN	0x00000008	/* no line deleting */
#define FSTAT_CHMASK	(FSTAT_NOEDIT | FSTAT_NOADDLIN | FSTAT_NODELIN)
#define FSTAT_OPEN	0x00000010	/* ring index used (file opened) */
#define FSTAT_RO	0x00000020	/* read-only */
#define FSTAT_CHANGE	0x00000040	/* memory buffer changed */
#define FSTAT_SCRATCH	0x00000080	/* no file, only memory buffer */
#define FSTAT_FMASK	0x00007f00	/* filter mask-bits (7 levels), placeholder for the active filter bit per file */
#define FSTAT_TAG2	0x00008000	/* flag for active search regexp */
#define FSTAT_TAG3	0x00010000	/* replace active (change) */
#define FSTAT_SPECW	0x00020000	/* special buffer (maybe special parser/handler) */
#define FSTAT_TAG4	0x00040000	/* flag for anchored search regexp */
#define FSTAT_TAG5	0x00080000	/* word highlight regexp */
#define FSTAT_TAG6	0x00100000	/* flag for anchored highlight regexp */
#define FSTAT_EXTCH	0x00200000	/* external change (file newer on disk) */
#define FSTAT_HIDDEN	0x00400000	/* partially hide regular file in the ring */
#define FSTAT_INTERACT	0x00800000	/* interactive buffer (needs FSTAT_SPECW also) */
/*			0x01000000 */

/* bit masks for line flags */
#define LSTAT_TRUNC	0x00000001	/* line truncated (on read) */
#define LSTAT_CHANGE	0x00000002	/* line changed: since last save */
#define LSTAT_ALTER	0x00000004	/* line altered: since open */
#define LSTAT_TAG1	0x00000008	/* by line tag */
#define LSTAT_SELECT	0x00000010	/* selection bit */
#define LSTAT_TOP	0x00000020	/* top mark bit */
#define LSTAT_BOTTOM	0x00000040	/* bottom mark bit */
/*			0x00000080 */
#define LSTAT_FMASK	FSTAT_FMASK	/* filter mask-bits (for hide), placeholder for the hide bits per line */
/*			0x00008000 */
#define LSTAT_BM_BITS	0x000f0000	/* mask for bookmark index, placeholder for 15, we use 9 only (see BM_BIT_SHIFT) */
/* bookmarks */
#define BM_BIT_SHIFT	(4*4)		/* bit shift count, to convert index<-->mask, bm_i <--> bm_bits (see LSTAT_BM_BITS) */

/* bit masks for command (table) flags */
#define TSTAT_ARGS	0x0001		/* cmd requires argument */
#define TSTAT_EDIT	FSTAT_NOEDIT	/* cmd can edit the line buffer */
#define TSTAT_ADDLIN	FSTAT_NOADDLIN	/* cmd can add line */
#define TSTAT_DELIN	FSTAT_NODELIN	/* cmd can delete line */
#define TSTAT_OPTARG	0x0010		/* commane line parameter is optional */
/* #define TSTAT_	0x0020 */

/* macro definitions */
#define MAX(x,y)	((x) > (y) ? (x) : (y))
#define MIN(x,y)	((x) < (y) ? (x) : (y))
/* macros (leftvalues) */
#define CURR_FILE	(cnf.fdata[cnf.ring_curr])
#define CURR_LINE	(CURR_FILE.curr_line)
#define SELECT_FI	(cnf.fdata[cnf.select_ri])

/* text-line query */
#define TEXT_LINE(lp)		( (lp) && !((lp)->lflag & (LSTAT_TOP | LSTAT_BOTTOM)) )
#define UNLIKE_TOP(lp)		( (lp) && !((lp)->lflag & LSTAT_TOP) )
#define UNLIKE_BOTTOM(lp)	( (lp) && !((lp)->lflag & LSTAT_BOTTOM) )
/* default mask for fflag depending on flevel */
#define FMASK(level)		( (0x80 << (level)) & FSTAT_FMASK )
/* effective mask for lines (.lflag) depending on .fflag AND .flevel -- bit in .fflag for temporary switch */
#define LMASK(ri)		( cnf.fdata[(ri)].fflag & (FMASK(cnf.fdata[(ri)].flevel)) )
/* invisible line query */
#define HIDDEN_LINE(ri,lp)	( (lp)->lflag & LMASK(ri) )

/* regcomp options */
#define REGCOMP_OPTION	(REG_EXTENDED | REG_NEWLINE | ((cnf.gstat & GSTAT_CASES) ? 0 : REG_ICASE))
/* for variable/function name selection */
#define IS_ID(ch)	( ((ch) >= 'a' && (ch) <= 'z') || ((ch) >= 'A' && (ch) <= 'Z') || \
			((ch) >= '0' && (ch) <= '9') || ((ch) == '_') )
#define IS_BLANK(ch)	((ch) == ' ' || (ch) == '\t')
#define TOHEX(ch)	(((ch) >= 0 && (ch) < 10) ? ('0'+(ch)) : ('A'+(ch)-10))

/* text area size -- only after bootup */
#define TEXTROWS	(cnf.maxy-2)		/* screen rows in text area */
#define TEXTCOLS	(cnf.maxx-cnf.pref)	/* screen columns for text in text area */

/* filter targets */
#define FILTER_ALL	0x1
#define FILTER_MORE	0x2
#define FILTER_LESS	0x4

/* function or block levels */
#define OPEN_CURBRAC	0x7B
#define CLOSE_CURBRAC	0x7D
#define IL_NONE		0
#define IL_HEADER	1
#define IL_BEGIN	2
#define IL_INTERN	4
#define IL_END		8

/* some function and block header patterns for various file types
 */
#define C_HEADER_PATTERN	"([:a-zA-Z0-9_]+)\\s*[(]"
#define C_STRUCTURE_PATTERN	"^(struct|enum|union|class|namespace)\\s+(\\w*)|^typedef\\s+(struct|enum|union)\\s+(\\w*)"
#define HEADER_PATTERN_END	"[)]\\s*[{]\\s*$"
/**/
#define PERL_HEADER_PATTERN	"^sub\\s+([:.a-zA-Z0-9_]+)"
#define TCL_HEADER_PATTERN	"^proc\\s+([:.a-zA-Z0-9_]+)"
#define SHELL_HEADER_PATTERN	"^function\\s+([a-zA-Z0-9_]+)|^([a-zA-Z0-9_]+)\\s*[(][)]"
#define HEADER_PATTERN_END2	"\\s*[{]\\s*(#.*)?$"
/**/
#define PYTHON_HEADER_PATTERN	"^\\s*def\\s+(\\w+)"
#define TEXT_HEADER_PATTERN	"^\\S+"

/* options for pipe i/o processing
*/
/* common */
#define OPT_IN_OUT		0x0010	/* feed some input line(s) for bg process (default: SELECTION only) */
#define OPT_IN_OUT_FOCUS	0x0011	/* single line only (focus) */
#define OPT_IN_OUT_VIS_ALL	0x0012	/* all visible lines */
#define OPT_IN_OUT_REAL_ALL	0x0014	/* really all lines */
#define OPT_IN_OUT_SH_MARK	0x0020	/* flag, lines with shadow markers */
#define OPT_IN_OUT_MASK		0x003f
#define OPT_REDIR_ERR		0x0040	/* stderr redirected to stdout */
#define OPT_COMMON_MASK		0x00ff
/* base */
#define OPT_BASE_MASK	0x0f00
#define OPT_STANDARD	0x0000	/* the standard processing into scratch buffer, fg or bg */
#define OPT_NOSCRATCH	0x0100	/* no scratch buffer -- custom processing */
/* standard processing only: */
#define OPT_NOBG	0x1000	/* keep processing in foreground */
#define OPT_SILENT	0x2000	/* no header/footer lines */
#define OPT_NOAPP	0x4000	/* do not append to buffer, wipe out */
#define OPT_INTERACT	0x8000	/* interactive bg process */

typedef int (*FUNCPTR) (const char *);
typedef int (*FUNCP0) (void);
typedef struct cmdline_tag CMDLINE;
typedef struct line_tag LINE;
typedef struct fdata_tag FDATA;
typedef struct config_tag CONFIG;
typedef struct node_tag NODE;
typedef struct change_data_tag CHDATA;
typedef struct table_tag TABLE;
typedef struct key_string_tag KEYS;
typedef struct macros_tag MACROS;
typedef struct macroitems_tag MACROITEMS;
typedef struct tagstru_tag TAG;
typedef struct bookmark_tag BOOKMARK;
typedef struct motion_history_tag MHIST;

/* the command line */
struct cmdline_tag
{
	CMDLINE *prev;
	CMDLINE *next;
	char buff[CMDLINESIZE];
	int len;
};

/* for the line chain (memory buffer) */
struct line_tag
{
	LINE *prev;		/* NULL if first line */
	LINE *next;		/* NULL if last line */
	char *buff;		/* malloc() and free() */
	int llen;		/* line length, characters in the line */
	int lflag;		/* LSTAT_ (various flags w/ filter mask and bookmark bits) */
};

typedef enum filetype_enum
{
	UNKNOWN_FILETYPE = -1,

	C_FILETYPE = 0,
	PERL_FILETYPE,
	TCL_FILETYPE,
	PYTHON_FILETYPE,
	SHELL_FILETYPE,

	TEXT_FILETYPE
} FXTYPE;

/* for the file ring */
struct fdata_tag
{
	char fname[FNAMESIZE];	/* filename, relative to cwd, or absolute path */
	char basename[FNAMESIZE];
	char dirname[FNAMESIZE];
	struct stat stat;

	/* ri allocation here (FSTAT_OPEN bit) */
	int fflag;		/* FSTAT_ (various attribute flags w/ filter mask bits) */
	int origin;		/* used by SCRATCH buffers */

	int num_lines;
	int lineno;		/* line number of current, focus line in the file(buffer) */
	int lncol;		/* character position in the current line, lncol > llen temporary allowed! */
	int lnoff;		/* offset of the lines on the screen */

	int focus;		/* row of focus line in the window */
	int curpos;		/* abs. cursor position in current, focus row */

	/* the buffer */
	LINE *curr_line;	/* linked list of lines */

	LINE *top;		/* fix part of linked list: top and bottom marker */
	LINE *bottom;		/* theese two are used for the filtering */
	int flevel;		/* filter level */
	FXTYPE ftype;		/* file type by extension */

	regex_t search_reg;	/* search regexp (with FSTAT_TAG{2|3}) */
	char search_expr[SEARCHSTR_SIZE];	/* the last search expression */
	char replace_expr[SEARCHSTR_SIZE];	/* the last replace expression */
	regex_t highlight_reg;	/* regexp for word highlighting (with FSTAT_TAG5) */

	int	pipe_opts;	/* options for pipe in/out processing */
	int	chrw;		/* child pid r/w */
	int	pipe_output;	/* child output -- fd for pipe read (0 if closed) */
	int	pipe_input;	/* child input, optional -- fd for pipe write (0 if closed) */
	char	*readbuff;	/* for reads from pipe, (NULL if free'd)  */
	int	rb_nexti;	/* next index in readbuff */
	int 	last_input_length;	/* saved in type_text() for prompt recognition, interactive shells */
};

struct bookmark_tag
{
	int ring;		/* ring index, or -1 */
	char sample[SHORTNAME];	/* sample part of the line-buffer */
};

/* key sequence tree */
struct node_tag {
	int ch;			/* element of sequence		*/
	int leaf;		/* key_value if this is a LEAF	*/
	NODE **branch;		/* array of branches or NULL	*/
	int bcount;		/* branch array size or 0	*/
};

/* main context */
struct config_tag
{
	int maxx, maxy;		/* terminal dimensions */
	WINDOW *wstatus;	/* the first line of the screen, the status line */
	WINDOW *wtext;		/* middle of the screen, text area */
	WINDOW *wbase;		/* the last line of the screen, the command line */

	uid_t uid, euid;		/* user */
	gid_t gid, egid, groups[50];	/* group */
	pid_t pid, ppid, sid, pgid;	/* process */
	time_t starttime;

	int gstat;		/* GSTAT_ */
	int pref;		/* prefix length */
	int indentsize;		/* 1 (tab), or 4 (sp) */
	int tabsize;		/* 8 */
	int trace;		/* rows in trace/message */
	char tracerow[TRACESIZE][CMDLINESIZE];

	int palette;		/* color setting, 0...PALETTE_MAX */
	int bootup;
	int noconfig;
	int lsdir_opts;		/* options for directory listing */

	/* file ring */
	int ring_curr;		/* 0 ... ring_size-1 */
	int ring_size;		/* 0 if nothing, else 1 ... RINGSIZE */
	FDATA fdata[RINGSIZE];

	/* selection */
	int select_ri;		/* selection made at ring index (or -1) */
	int select_w;		/* watch */

	/* command line history */
	int clpos;		/* abs. position on command line */
	int cloff;		/* offset of command line on the screen */
	char cmdline_buff[CMDLINESIZE];
	int cmdline_len;
	CMDLINE *clhistory;	/* command line history */
	int clhist_size;
	int reset_clhistory;	/* request, to do this later */

	char find_path[SHORTNAME];
	char find_opts[FNAMESIZE];
	char tags_file[SHORTNAME];
	char vcs_tool[10][SHORTNAME];		/* vcs tools, extension */
	char vcs_path[10][SHORTNAME];		/* vcs tools, extension */
	char make_path[SHORTNAME];
	char make_opts[SHORTNAME];
	char sh_path[SHORTNAME];
	char diff_path[SHORTNAME];
	char ssh_path[SHORTNAME];
	char project[SHORTNAME];

	char _home[FNAMESIZE];
	char _pwd[FNAMESIZE];
	char myhome[FNAMESIZE];
	int l1_home;
	int l1_pwd;
	int l2_myhome;

	BOOKMARK bookmark[10];	/* set bookmarks by back reference from the LINE */

	NODE *seq_tree;		/* key sequence tree */

	char tag_j2path[FNAMESIZE];
	int tag_j2len;
	TAG *taglist;		/* 'tags' info tree */

	MHIST *mhistory;	/* push/pop list */

	char automacro[CMDLINESIZE];	/* testing */

	char log[12];		/* logging, flags by modules */
};

/* search&replace */
struct change_data_tag
{
	int change_count;	/* counter for "Rest" */
	LINE *lx;		/* temp. for change */
	int lineno;		/* temp. for change */
	int lncol;		/* temp. in-line column index */
	regmatch_t pmatch[10];	/* match and sub match */
	int rflag;		/* 1=backref, 0=no-backref */
	char *rep_buff;		/* replacement buffer (malloc/free) */
	int rep_length;		/* actual size of rep_buff[] */
};

/* for the key and command handler */
struct table_tag
{
	const char *name;	/* command name, optional */
	int fkey;		/* key value, optional, changeable */
	int minlen;		/* required min.length of name to identify */
	FUNCPTR funcptr;	/* function pointer */
	const char *fullname;	/* functions exact name */
	int tflag;		/* TSTAT_ values */
};

struct key_string_tag
{
	int key_value;
	const char *key_string;
};

struct macroitems_tag
{
	int m_index;		/* index in the table[] */
	char m_args[128];	/* function argument, optional */
};

struct macros_tag
{
	int fkey;		/* key value, mandatory */
	char name[32];		/* command name, optional */
	int mflag;		/* items' ORed TSTAT_ values */
	int items;		/* items count in the list */
	MACROITEMS *maclist;	/* list of macro items */
};

typedef enum tagtype_enum
{
	TAG_FUNCTION	= 'f',
	TAG_DEFINE	= 'd',
	TAG_VARIABLE	= 'v',
	TAG_TYPEDEF	= 't',
	TAG_STRUCTURE	= 's',
	TAG_UNION	= 'u',
	TAG_MEMBER	= 'm',
	TAG_ENUM	= 'e',
	TAG_GLOB	= 'g',
	TAG_CLASS	= 'c',
	TAG_LINK	= 'l',
	TAG_UNDEF	= '?'
} TAGTYPE;

struct tagstru_tag
{
	TAG *next;
	char *symbol;
	char *fname;
	TAGTYPE type;
	int lineno;
	char *pattern;
};

struct motion_history_tag
{
	MHIST *prev;		/* NULL, at first */
	int ring;		/* */
	int lineno;		/* */
};

/* motion types (ops) for focus change
*/
typedef enum motion_type_enum
{
	FOCUS_ON_1ST_LINE,
	FOCUS_ON_2ND_LINE,
	FOCUS_ON_LASTBUT1_LINE,
	FOCUS_ON_LAST_LINE,
	INCR_FOCUS,
	DECR_FOCUS,
	INCR_FOCUS_SHADOW,
	DECR_FOCUS_SHADOW,
	CENTER_FOCUSLINE,
	FOCUS_AWAY_TOP,
	FOCUS_AWAY_BOTTOM,
	FOCUS_AVOID_BORDER,
	FOCUS_NOCHANGE
} MOTION_TYPE;

typedef enum test_access_type_enum
{
	TEST_ACCESS_RW_OK = 6,
	TEST_ACCESS_R_OK  = 4,
	TEST_ACCESS_W_OK  = 2,
	TEST_ACCESS_NONE  = 0
} TEST_ACCESS_TYPE;

#endif
