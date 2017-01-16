/* 
* command.h
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

/* Pointer and Name */
#define PN(ptr)		(FUNCPTR) ptr, #ptr

/* this "db" contains the names of command line commands and related attributes,
 * only these functions have external visibility, all they have return type int,
 * this "db" is almost immutable, that means
 * changeable is _ONLY_ the fkey value by the edakeys file
 *
 * command line commands : (minlen > 0)
 * 	table[] is used to check commands in parse_cmdline()
 * 	min_len (-1) means: command line use has no sense
 * keypresses (fkey >= KEY_NONE)
 * 	table[] is used to check keypresses in ed_common()
 * 	fkey (-1) means: key-binding not possible or meaningless
 */
TABLE table[] = {
	/* name		fkey, minlen,		funcptr,fullname	tflag(TSTAT_) */
	/* -----------	----------------------	----------------------	------------- */

	/* base editor functions: moving around in text area */
	{ "",		KEY_PPAGE, -1,		PN(go_page_up),		0x00},
	{ "",		KEY_NPAGE, -1,		PN(go_page_down),	0x00},
	{ "",		KEY_C_LEFT, -1,		PN(prev_nonblank),	0x00},
	{ "",		KEY_C_RIGHT, -1,	PN(next_nonblank),	0x00},
	{ "top",	KEY_C_PPAGE, 3,		PN(go_top),		0x00},
	{ "bottom",	KEY_C_NPAGE, 3,		PN(go_bottom),		0x00},
	{ ":",		-1, 1,			PN(goto_line),		0x01},
	{ "pos",	-1, 3,			PN(goto_pos),		0x01},
	{ "center",	KEY_F5, 2,		PN(center_focusline),	0x00},
	{ "",		KEY_M_LESSTHAN, -1,	PN(clhistory_prev),	0x00},
	{ "", 		KEY_M_GREATHAN, -1,	PN(clhistory_next),	0x00},

	/* file I/O and switching buffers */
	{ "ed",		-1, 1,			PN(add_file),		0x11},
	{ "re!",	KEY_NONE, 3,		PN(reload_file),	0x00},
	{ "diff",	KEY_NONE, 4,		PN(show_diff),		0x11},
	{ "reload",	KEY_NONE, 2,		PN(reload_bydiff),	0x00},
	{ "prev",	KEY_S_F8, 4,		PN(prev_file),		0x00},
	{ "next",	KEY_F8, 4,		PN(next_file),		0x00},
	{ "save",	KEY_F2, 2,		PN(save_file),		0x11},
	{ "file",	KEY_F3, 2,		PN(file_file),		0x00},
	{ "quit",	KEY_F4, 1,		PN(quit_file),		0x00},
	{ "qquit",	KEY_NONE, 2,		PN(drop_file),		0x00},
	{ "qoth",	KEY_NONE, 2,		PN(quit_others),	0x00},
	{ "qall",	KEY_S_F4, 2,		PN(quit_all),		0x00},
	{ "fall",	KEY_S_F3, 2,		PN(file_all),		0x00},
	{ "sall",	KEY_S_F2, 2,		PN(save_all),		0x00},
	{ "hide",	KEY_NONE, 4,		PN(hide_file),		0x00},

	/* in-line edit */
	{ "deleol",	KEY_F6, 6,		PN(deleol),		0x0a},
	{ "del2bol",	KEY_S_F6, 7,		PN(del2bol),		0x0a},
	{ "delline",	KEY_M_D, 4,		PN(delline),		0x08},
	{ "dup",	KEY_F7, 3,		PN(duplicate),		0x04},
	{ "delete",	-1, 4,			PN(delete_lines),	0x09},
	{ "strip",	-1, 5,			PN(strip_lines),	0x03},

	/* line selection based functions */
	{ "lisel",	KEY_M_L, 5,		PN(line_select),	0x00},
	{ "resel",	KEY_M_U, 5,		PN(reset_select),	0x00},
	{ "cpsel",	KEY_M_C, 5,		PN(cp_select),		0x00},
	{ "mvsel",	KEY_M_M, 5,		PN(mv_select),		0x00},
	{ "rmsel",	KEY_M_G, 5,		PN(rm_select),		0x00},
	{ "selfirst",	KEY_M_A, 5,		PN(go_select_first),	0x00},
	{ "sellast",	KEY_M_E, 5,		PN(go_select_last),	0x00},
	{ "seall",	KEY_C_A, 3,		PN(select_all),		0x00},
	{ "over",	KEY_NONE, 4,		PN(over_select),	0x00},
	{ "unindent",	KEY_M_LSQBRAC, 5,	PN(unindent_left),	0x02},
	{ "indent",	KEY_M_RSQBRAC, 3,	PN(indent_right),	0x02},
	{ "shleft",	KEY_M_LCURBRAC, 3,	PN(shift_left),		0x02},
	{ "shright",	KEY_M_RCURBRAC, 3,	PN(shift_right),	0x02},

	/* multiline selection operations */
	{ "padb",	KEY_NONE, 3,		PN(pad_block),		0x03},
	{ "cutb",	KEY_NONE, 3,		PN(cut_block),		0x03},
	{ "lcutb",	KEY_NONE, 4,		PN(left_cut_block),	0x03},
	{ "splitb",	KEY_NONE, 5,		PN(split_block),	0x07},
	{ "joinb",	KEY_NONE, 4,		PN(join_block),		0x0b},

	/* filtering while editing at different levels */
	{ "all",	-1, 3,			PN(filter_all),		0x11},
	{ "less",	-1, 4,			PN(filter_less),	0x11},
	{ "more",	-1, 4,			PN(filter_more),	0x01},
	{ "ftmp",	KEY_M_EQUAL, 4,		PN(filter_tmp_all),	0x00},
	{ "fxup",	KEY_C_U, 4,		PN(filter_expand_up),	0x00},
	{ "fxdn",	KEY_C_N, 4,		PN(filter_expand_down),	0x00},
	{ "fres",	KEY_C_E, 4,		PN(filter_restrict),	0x00},
	{ "ilevel",	KEY_NONE, 2,		PN(incr_filter_level),	0x00},
	{ "dlevel",	KEY_NONE, 2,		PN(decr_filter_level),	0x00},
	{ "il2",	KEY_NONE, 3,		PN(incr2_filter_level),	0x00},
	{ "dl2",	KEY_NONE, 3,		PN(decr2_filter_level),	0x00},
	{ "",		KEY_M_BACKSLASH, -1,	PN(incr_filter_cycle),	0x00},
	{ "m1",		-1, 2,			PN(filter_m1),		0x00},

	/* search, change, highlight, regexp tools */
	{ "/",		-1, 1,			PN(search),		0x01},
	{ "",		KEY_C_L, -1,		PN(repeat_search),	0x00},
	{ "change",	-1, 2,			PN(change),		0x03},
	/* repeat_change() -- only from change() and event_handler() */
	{ "tag",	-1, 3,			PN(color_tag),		0x11},
	{ "tf",		KEY_M_T, 2,		PN(tag_focusline),	0x00},
	{ "",		KEY_C_F, -1,		PN(search_word),	0x00},
	/* control-H reserved */
	{ "high",	KEY_C_J, 2,		PN(highlight_word),	0x11},
	{ "",		KEY_C_K, -1,		PN(tag_line_byword),	0x00},

	/* multifile search tools */
	{ "find",	KEY_NONE, 4,		PN(find_cmd),		0x11},
	{ "locate",	KEY_NONE, 3,		PN(locate_cmd),		0x11},
	{ "locswitch",	KEY_NONE, 5,		PN(locate_find_switch),	0x00},
	{ "",		KEY_M_Q, -1,		PN(multisearch_cmd),	0x00},
	{ "",		KEY_M_W, -1,		PN(find_window_switch),	0x00},
	{ "fword",	KEY_NONE, 2,		PN(fw_option_switch),	0x00},
	{ "fspath",	KEY_NONE, 3,		PN(fsearch_path_macro),	0x01},
	{ "fseargs",	KEY_NONE, 4,		PN(fsearch_args_macro),	0x01},

	/* brace match and folding */
	{ "match",	KEY_F9, 3,		PN(tomatch),		0x00},
	{ "fmatch",	KEY_S_F9, 2,		PN(forcematch),		0x00},
	{ "fblock",	KEY_S_F10, 2,		PN(fold_block),		0x00},
	{ "thisf",	KEY_S_F11, 4,		PN(fold_thisfunc),	0x00},

	/* using ctags */
	{ "lt",		-1, 2,			PN(tag_load_file),	0x00},
	{ "symbol",	KEY_F10, 3,		PN(tag_view_info),	0x11},
	{ "jump",	KEY_F11, 1,		PN(tag_jump_to),	0x11},
	{ "jback",	KEY_C_T, 2,		PN(tag_jump_back),	0x00},

	/* external tools, filter pipes, VCS calls and unified diffs */
	{ "sh",		-1, 2,			PN(shell_cmd),		0x01},
	{ "ish",	-1, 3,			PN(ishell_cmd),		0x11},
	{ "make",	-1, 4,			PN(make_cmd),		0x11},
	{ "|",		-1, 1,			PN(filter_cmd),		0x01},
	{ "||",		-1, 2,			PN(filter_shadow_cmd),	0x01},
	{ "vcstool",	-1, 7,			PN(vcstool),		0x01},
	{ "pdiff",	KEY_NONE, 2,		PN(process_diff),	0x00},

	/* resources, keys, macros, projects, changing buffer type */
	{ "set",	-1, 3,			PN(set),		0x01},
	{ "rc",		-1, 2,			PN(load_rcfile),	0x00},
	{ "keys",	-1, 3,			PN(load_keyfile),	0x00},
	{ "macros",	-1, 5,			PN(load_macrofile),	0x00},
	{ "rem",	KEY_NONE, 3,		PN(reload_macros),	0x00},
	{ "sp",		KEY_NONE, 2,		PN(save_project),	0x11},
	{ "is",		KEY_NONE, 2,		PN(is_special),		0x11},

	/* buffer views and UI changes, ringlist, lsdir */
	{ "palette",	KEY_M_QMARK, 3,		PN(rotate_palette),	0x00},
	{ "pref",	KEY_M_AT, 4,		PN(prefix_macro),	0x00},
	{ "smart",	KEY_M_MINUS, 3,		PN(smartind_macro),	0x00},
	{ "shadow",	KEY_M_HASH, 3,		PN(shadow_macro),	0x00},
	{ "ring",	KEY_M_R, 2,		PN(list_buffers),	0x00},
	{ "lsdir",	-1, 2,			PN(lsdir_cmd),		0x11},
	{ "redraw",	KEY_C_R, 6,		PN(force_redraw),	0x00},

	/* bookmarks */
	{ "bms",	KEY_M_ZERO, 3,		PN(bm_set),		0x11},
	{ "bmc",	KEY_NONE, 3,		PN(bm_clear),		0x01},
	{ "b1",		KEY_M_ONE, 2,		PN(bm_jump1),		0x00},
	{ "b2",		KEY_M_TWO, 2,		PN(bm_jump2),		0x00},
	{ "b3",		KEY_M_THREE, 2,		PN(bm_jump3),		0x00},
	{ "b4",		KEY_M_FOUR, 2,		PN(bm_jump4),		0x00},
	{ "b5",		KEY_M_FIVE, 2,		PN(bm_jump5),		0x00},
	{ "b6",		KEY_M_SIX, 2,		PN(bm_jump6),		0x00},
	{ "b7",		KEY_M_SEVEN, 2,		PN(bm_jump7),		0x00},
	{ "b8",		KEY_M_EIGHT, 2,		PN(bm_jump8),		0x00},
	{ "b9",		KEY_M_NINE, 2,		PN(bm_jump9),		0x00},

	/* export/insert, view block name and anything else */
	{ "",		KEY_C_B, -1,		PN(ins_bname),		0x00},
	{ "",		KEY_C_V, -1,		PN(ins_varname),	0x00},
	{ "",		KEY_C_G, -1,		PN(ins_filename),	0x00},
	{ "",		KEY_C_X, -1,		PN(cp_text2cmd),	0x00},
	{ "vbn",	KEY_M_SLASH, 3,		PN(view_bname),		0x00},
	{ "wcase",	KEY_C_W, 2,		PN(word_case),		0x02},
	{ "cmds",	KEY_NONE, 3,		PN(show_commands),	0x00},
	{ "version",	KEY_NONE, 7,		PN(version),		0x00},
	{ "mouse",	KEY_NONE, 5,		PN(mouse_support),	0x00},
	{ "record",	KEY_NONE, 6,		PN(recording_switch),	0x00},

	/* hardcoded keys and functions for macros (the function pointers must be listed) */

	{ "stop",	KEY_NONE, 4,	PN(stop_bg_process),		0x00},
	{ "",		-1, -1,		PN(finish_in_fg),		0x00},
	/* switch between command line and text area (ESC or F12) */
	{ "",		-1, -1,		PN(switch_text_cmd),		0x00},
	/* clhistory (previous in history: Ctrl-UP or Ctrl-O) */
	{ "",		-1, -1,		PN(clhistory_prev),		0x00},
	/* clhistory (next, back, in history: Ctrl-DOWN or Ctrl-P) */
	{ "",		-1, -1,		PN(clhistory_next),		0x00},
	/* moving around in text area and command line */
	{ "",		-1, -1,		PN(go_up),			0x00},
	{ "",		-1, -1,		PN(go_down),			0x00},
	{ "",		-1, -1,		PN(go_smarthome),		0x00},
	{ "",		-1, -1,		PN(go_home),			0x00},
	{ "",		-1, -1,		PN(go_end),			0x00},
	{ "",		-1, -1,		PN(go_left),			0x00},
	{ "",		-1, -1,		PN(go_right),			0x00},
	/* inline editing in text area and command line */
	{ "",		-1, -1,		PN(delback_char),		0x0a},
	{ "",		-1, -1,		PN(delete_char),		0x0a},
	/* launching command or start parser in text area */
	{ "",		-1, -1,		PN(keypress_enter),		0x06},
	/* special text area functions: */
	{ "",		-1, -1,		PN(type_text),			0x07},
	{ "",		-1, -1,		PN(split_line),			0x06},
	{ "",		-1, -1,		PN(join_line),			0x0a},

	/* the end */
	{ "nop",	-1, 3,		PN(nop),			0x00},
};
#undef PN

/* Key and Name */
#define KN(value)	value, #value

/* values and names of keyboard shortcuts */
KEYS keys[] = {
	/* key_value	key_string */
	/* ------------ ---------- */
#include "rkeys.h"	/* reserved keys : generated file! */
#include "keys.h"	/* keys list : generated file! */
};
#undef KN
