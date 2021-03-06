/* function prototypes
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

/*
* public functions must in table[] available user calls directly or in macros
* the macro flag is just a remark, that these are shorter ones, calling an engine or so
*/
#ifndef _PROTOTYPES_H_
#define _PROTOTYPES_H_

/* cmd.c */
extern int switch_text_cmd (void);			/* public */
extern int go_text (void);				/* public */
extern int message (const char *str);			/* public */
extern int msg_from_text (void);			/* public */
extern int clhistory_push (const char *buff, int len);
extern int clhistory_cleanup (int keep);
extern int clhistory_prev (void);			/* public */
extern int clhistory_next (void);			/* public */
extern void reset_cmdline (void);
extern int ed_cmdline (int ch);
extern void cmdline_to_clhistory (char *buff, int len);
extern int ed_text (int ch);
extern int typing_tutor (void);				/* public */
extern int typing_tutor_handler (int ch);
extern int keypress_enter (void);			/* public */
extern int go_home (void);				/* public */
extern int go_smarthome (void);				/* public */
extern int go_end (void);				/* public */
extern char *select_word (const LINE *lp, int lncol);
extern int prev_nonblank (void);			/* public */
extern int next_nonblank (void);			/* public */
extern void wc (int *lin, int *w, int *ch);
extern void set_position (int ri, int lineno, LINE *lp);
extern int go_left (void);				/* public */
extern int go_right (void);				/* public */
extern int scroll_1line_up (void);			/* public */
extern int go_up (void);				/* public */
extern int scroll_1line_down (void);			/* public */
extern int go_down (void);				/* public */
extern int go_page_up (void);				/* public */
extern int go_first_screen_line (void);
extern int scroll_screen_up (void);			/* public */
extern int go_page_down (void);				/* public */
extern int go_last_screen_line (void);
extern int scroll_screen_down (void);			/* public */
extern int go_top (void);				/* public */
extern int go_bottom (void);				/* public */
extern int goto_line (const char *args);		/* public */
extern int goto_pos (const char *args);			/* public */
extern int center_focusline (void);			/* public */
extern int type_cmd (const char *str);
extern int cp_text2cmd (void);				/* public */
extern int cp_name2open (void);				/* public */
extern int csere (char **, int *, int, int, const char *, int);
//extern int csere0 (char **, int, int, const char *, int);
extern int milbuff (LINE *, int, int, const char *, int);
extern int type_text (const char *str);			/* public */
extern int insert_chars (const char *input, int ilen);
extern int delete_char (void);				/* public */
extern int delback_char (void);				/* public */
extern int deleol (void);				/* public */
extern int del2bol (void);				/* public */
extern int delline (void);				/* public */
extern int duplicate (void);				/* public */
extern int split_line (void);				/* public */
extern int join_line (void);				/* public */
extern int ed_common (int ch);

/* cmdlib.c */
extern int nop (void);					/* public */
extern int version (void);				/* public */
extern int pwd (void);					/* public */
extern int uptime (void);				/* public */
extern int delete_lines (const char *args);		/* public */
extern int strip_lines (const char *args);		/* public */
extern int ringlist_parser (const char *dataline);
extern char *dirlist_parser (const char *dataline);
extern int lsdir (const char *dirpath);
#define SIMPLE_PARSER_JUMP 0
#define SIMPLE_PARSER_WINKIN 1
extern int simple_parser (const char *dline, int jump_mode);
extern int python_parser (const char *dline);
extern int diff_parser (const char *dataline);
extern void general_parser (void);
extern int is_special (const char *special);		/* public */
extern int search_word (void);				/* public, macro */
extern int tag_line_byword (void);			/* public, macro */
extern int prefix_macro (void);				/* public, macro */
extern int tabhead_macro (void);			/* public, macro */
extern int smartind_macro (void);			/* public, macro */
extern int shadow_macro (void);				/* public, macro */
extern int incr_filter_cycle (void);			/* public, macro */
extern int fw_option_switch (void);			/* public, macro */
extern int fsearch_path_macro (const char *fpath);	/* public, macro */
extern int fsearch_args_macro (const char *fparams);	/* public, macro */
extern int mouse_support (void);			/* public, macro */
extern int locate_find_switch (void);			/* public, macro */
extern int recording_switch (void);			/* public, macro */
extern int multisearch_cmd (void);			/* public, macro */
extern int find_window_switch (void);			/* public, macro */
extern int word_case (void);				/* public */
extern int ins_varname (void);				/* public */
extern int ins_bname (void);				/* public */
extern int view_bname (void);				/* public */
extern int ins_filename (void);				/* public */
extern int xterm_title (const char *xtitle);		/* public */
extern int rotate_palette (void);			/* public */
extern int bm_set (const char *args);			/* public */
extern int bm_clear (const char *args);			/* public */
extern int bm_jump1 (void);				/* public, macro */
extern int bm_jump2 (void);				/* public, macro */
extern int bm_jump3 (void);				/* public, macro */
extern int bm_jump4 (void);				/* public, macro */
extern int bm_jump5 (void);				/* public, macro */
extern int bm_jump6 (void);				/* public, macro */
extern int bm_jump7 (void);				/* public, macro */
extern int bm_jump8 (void);				/* public, macro */
extern int bm_jump9 (void);				/* public, macro */
extern int process_diff (void);				/* public */
extern int hgdiff_eng (const char *, const char *, const char *);
extern int internal_hgdiff (const char *);		/* public, macro */
extern int internal_gitdiff (const char *);		/* public, macro */

/* disp.c */
extern void upd_statusline (void);
extern void upd_status_typing_tutor (void);
extern void upd_termtitle (void);
extern void show_tabheader (void);
extern void upd_cmdline (void);
extern void upd_text_area (int focus_line_only);
extern int alien_count (LINE *lp);
extern int get_pos (LINE *lp, int lncol);
extern int get_col (LINE *lp, int curpos);
extern int set_position_by_pointer (MEVENT pointer);
extern int first_screen_row (void);
extern int last_screen_row (void);
extern void update_curpos (int ri);
extern void update_focus (MOTION_TYPE motion_type, int ri);
extern void upd_trace (void);
extern int init_colors_and_cpal (void);
extern int color_palette_parser (const char *input);
extern void color_test (void);

/* ed.c */
extern void editor (void);
extern int run_macro_command (int mi, char *cmdline_buffer);
extern int run_command (int ti, const char *cmdline_buffer, int fkey);
extern int index_func_fullname (const char *fullname);
extern int index_key_string (const char *key_string);
extern int index_key_value (int key_value);
extern int index_macros_fkey (int fkey);
extern int init_hashtables(void);
extern int hash_fkey (int ch);
extern int hash_name (const char *ibuff, int clen);

/* keys.c */
extern int key_handler (NODE *seq_tree, int testing);
extern int process_seqfile (int noconfig);
extern void free_seq_tree (NODE *node);
extern void key_test (void);

/* fh.c */
extern int next_file (void);				/* public */
extern int prev_file (void);				/* public */
extern int query_inode (ino_t inode);
extern int try_add_file (const char *testfname);
extern int add_file (const char *fname);		/* public */
extern int quit_file (void);				/* public */
extern int quit_others (void);				/* public */
extern int file_file (void);				/* public */
extern int quit_all (void);				/* public */
extern int file_all (void);				/* public */
extern int save_all (void);				/* public */
extern int hide_file (void);				/* public */
extern LINE *append_line (LINE *lp, const char *extbuff);
extern LINE *insert_line_before (LINE *lp, const char *extbuff);
extern int check_files (void);
extern int restat_file (int ring_i);
extern TEST_ACCESS_TYPE testaccess (struct stat *test);
extern int read_lines (FILE *fp, LINE **linep, int *lineno, int *fflag);
extern int read_file (const char *fname, const struct stat *test);
extern int query_scratch_fname (const char *fname);
extern int scratch_buffer (const char *fname);
extern int reload_file (void);				/* public */
extern int show_diff (const char *diff_opts);		/* public */
extern int reload_bydiff (void);			/* public */
extern int clean_buffer (void);
extern int drop_file (void);				/* public */
extern int drop_all (void);
extern int save_file (const char *newfname);		/* public */
extern char *read_file_line (const char *fname, int lineno);
extern int write_out_chars (int fd, const char *buffer, int length);
extern int write_out_all_visible (int fd, int ring_i, int with_shadow);
extern int write_out_all_lines (int fd, int ring_i);

/* filter.c */
extern int next_lp (int ri, LINE **linep_p, int *count);
extern int prev_lp (int ri, LINE **linep_p, int *count);
extern int filter_all (const char *expr);		/* public, macro */
extern int filter_more (const char *expr);		/* public, macro */
extern int filter_less (const char *expr);		/* public, macro */
extern int filter_m1 (void);				/* public */
extern int filter_base (int action, const char *expr);
#define PURIFY_CLANG 2
#define PURIFY_OTHER 4
extern int filter_func (int action, int fmask);
extern void purify_for_matching_clang (char *outb, const char *inbuff, int len);
extern void purify_for_matching_other (char *outb, const char *inbuff, int len);
extern char *block_name (int ri);
extern int filter_tmp_all (void);			/* public */
extern int filter_expand_up (void);			/* public */
extern int filter_expand_down (void);			/* public */
extern int filter_restrict (void);			/* public */
extern int incr_filter_level (void);			/* public */
extern int incr2_filter_level (void);			/* public */
extern int decr_filter_level (void);			/* public */
extern int decr2_filter_level (void);			/* public */
extern int common_space (int length);
extern int tomatch (void);				/* public */
#define TOMATCH_DONT_SET_FOCUS 0
#define TOMATCH_SET_FOCUS 1
#define TOMATCH_WITH_PURIFY (PURIFY_CLANG | PURIFY_OTHER)
extern LINE *tomatch_eng (LINE *, int *, int *, int config_bits);
extern int forcematch (void);				/* public */
extern int fold_block (void);				/* public */
extern int fold_thisfunc (void);			/* public */

/* lll.c */
extern LINE *lll_add (LINE *line_p);
extern LINE *lll_add_before (LINE *line_p);
extern LINE *lll_rm (LINE *line_p);
extern LINE *lll_mv (LINE *lp_src, LINE *lp_trg);
extern LINE *lll_mv_before (LINE *lp_src, LINE *lp_trg);
extern LINE *lll_goto_lineno (int ri, int lineno);

/* main.c */
extern void tracemsg (const char *format, ...);
extern void record (const char *func, const char *param);
extern void put_string_to_file (const char *fn, const char *s);

/* pipe.c */
extern int shell_cmd (const char *ext_cmd);		/* public */
extern int make_cmd (const char *opts);			/* public */
extern int vcstool (const char *ext_cmd);		/* public */
extern int find_cmd (const char *ext_cmd);		/* public */
extern int locate_cmd (const char *expr);		/* public */
extern int filter_cmd (const char *ext_cmd);		/* public */
extern int filter_shadow_cmd (const char *ext_cmd);	/* public */
extern int lsdir_cmd (const char *ext_cmd);		/* public */
extern int show_define (const char *fname, int lineno);
extern int read_extcmd_line (const char *ext_cmd, int lineno, char *buff, int siz);
extern int read_stdin (void);
extern int read_pipe (const char *sbufname, const char *ext_cmd, const char *ext_argstr, int opts);
extern int readout_pipe (int ring_i);
extern int background_pipes (void);
extern int wait4_bg (int ring_i);
extern int stop_bg_process (void);			/* public */

/* rc.c */
extern int set (const char *argz);			/* public */
extern int process_rcfile (int noconfig);
extern int process_keyfile (int noconfig);
extern int process_macrofile (int noconfig);
extern void drop_macros (void);
extern int reload_macros (void);			/* public */
extern int process_project (int noconfig);
extern int save_project (const char *projectname);	/* public */
extern int load_rcfile (void);				/* public */
extern int load_keyfile (void);				/* public */
extern int load_macrofile (void);			/* public */
extern int show_commands (void);			/* public */
extern void save_clhistory (void);
extern void load_clhistory (void);

/* ring.c */
extern int list_buffers (void);				/* public */
extern void set_bookmark (int bm_i);
extern void clr_bookmark (int bm_i);
extern void clr_opt_bookmark (LINE *lp);
extern int jump2_bookmark (int bm_i);
extern void clear_bookmarks (int ring_i);
extern int show_bookmarks (void);			/* public */
extern int mhist_push (int ring_i, int lineno);
extern int mhist_pop (void);
extern void mhist_clear (int ring_i);

/* search.c */
extern int filter_regex (int action, int fmask, const char *expr);
extern int regexp_match (const char *buff, const char *expr, int nsub, char *match);
extern int internal_search (const char *pattern);
extern LINE *search_goto_pattern (int ri, const char *pattern, int *new_lineno);
extern int color_tag (const char *expr);		/* public */
extern int highlight_word (const char *expr);		/* public */
extern void cut_delimiters (const char *expr, char *expr_new, int length);
extern void regexp_shorthands (const char *pattern, char *new_pattern, int length);
extern int tag_focusline (void);			/* public */
extern int search (const char *expr);			/* public */
extern int reset_search (void);
extern int repeat_search (void);			/* public */
extern int change (const char *argz);			/* public */
extern int repeat_change (int ch);

/* select.c */
extern int line_select (void);				/* public */
extern int reset_select (void);				/* public */
extern int select_all (void);				/* public */
extern int go_select_first (void);			/* public */
extern LINE *selection_first_line (int *lineno);
extern int go_select_last (void);			/* public */
extern LINE *selection_last_line (int *lineno);
extern int recover_selection (void);
extern int cp_select (void);				/* public */
extern int cp_select_eng (LINE *lp_src, LINE *lp_target);
extern int rm_select (void);				/* public */
extern int rm_select_eng (LINE *lp_last);
extern int mv_select (void);				/* public */
extern int mv_select_eng (LINE *lp_src, LINE *lp_target);
extern int wr_select (int fd, int with_shadow);
extern int over_select (void);				/* public */
extern int unindent_left (void);			/* public, macro */
extern int indent_right (void);				/* public, macro */
extern int shift_left (void);				/* public, macro */
extern int shift_right (void);				/* public, macro */
extern int uncomment (void);				/* public, macro */
extern int comment (void);				/* public, macro */
extern int pad_block (const char *curpos);		/* public */
extern int pad_line (LINE *lp, int padsize);
extern int cut_block (const char *curpos);		/* public, macro */
extern int left_cut_block (const char *curpos);		/* public, macro */
extern int split_block (const char *curpos);		/* public */
extern int join_block (const char *separator);		/* public */

/* tags.c */
extern int tag_load_file (const char *tags_file);	/* public */
extern int tag_rm_all (void);
extern int tag_view_info (const char *arg_symbol);	/* public */
extern int tag_jump_to (const char *arg_symbol);	/* public */
extern int tag_jump_back (void);			/* public */

/* util.c */
extern int get_rest_of_line (char **, int *, const char *, int, int);
extern int glob_tab_expansion (char *path, unsigned maxsize, char **choices);
extern int glob_tilde_expansion (char *path, unsigned maxsize);
#define STRIP_BLANKS_FROM_END    0x01
#define STRIP_BLANKS_FROM_BEGIN  0x02
#define STRIP_BLANKS_SQUEEZE     0x04
extern int strip_blanks (int operation, char *str, int *length);
extern int count_prefix_blanks (const char *buff, int llen);
extern int yesno (const char *str);
extern FXTYPE fname_ext (const char *cfname);
#define SLASH_INDEX_REVERSE   1
#define SLASH_INDEX_FWD       0
#define SLASH_INDEX_GET_FIRST 1
#define SLASH_INDEX_GET_LAST  0
extern int slash_index (const char *string, int strsize, int from, int direction, int first_last);
extern int parse_token (const char *inputstr, const char *delim, int *index_rest);
extern void mybasename (char *outpath, const char *inpath, int outbuffsize);
extern void mydirname (char *outpath, const char *inpath, int outbuffsize);
extern char *canonicalpath (const char *path);
extern int parse_args (char *input, char **args);
extern int pidof (const char *progname);
extern int is_process_alive (int pid);

#endif
