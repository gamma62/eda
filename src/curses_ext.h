/*
* curses_ext.h
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

#ifndef _CURSES_EXT_H_
#define _CURSES_EXT_H_

#include <curses.h>

/* all KEY_*** defines are going into keys[] 
 * except "reserved: internal use only" (see also headers.sh)
 * reserved keys could be appended to keys[]
 */

#define KEY_RETURN	0x0d	/* reserved */
#define KEY_TAB		0x09	/* reserved */
#define KEY_ESC		0x1b	/* reserved */
#define KEY_C_RSQBRAC	0x1d
#define KEY_META	0x2000	/* reserved: internal use only! */
			/* Meta/Alt character flag */
#define KEY_CTRL	0x4000	/* reserved: internal use only! */
			/* special characters with Ctrl- or Shift- */
#define KEY_NONE	(0)	/* default, no-op value */

#ifndef KEY_BACKSPACE
#define KEY_BACKSPACE	0x10F	/* reserved (maybe 0x107 aka 0407) */
#endif
#define KEY_ASCII_DEL	0x7F	/* reserved, ascii DEL */

/* (no space before define!)
*define KEY_IC		reserved
*define KEY_DC		reserved
*define KEY_HOME	reserved
*define KEY_END		reserved
*define KEY_PPAGE
*define KEY_NPAGE
*define KEY_UP		reserved
*define KEY_DOWN	reserved
*define KEY_LEFT	reserved
*define KEY_RIGHT	reserved
*/

/* KEY_F0 is 0x108 (0410)
* space for 64 keys from 0x109 (0410) upto 0x148 (0510):
* FUNC 109-114
* SHIFT_FUNC 115-120
* CONTROL_FUNC 121-12c
* META_FUNC 12d-138 (reserved by X)
* 139-144 (unknown keys)
* 145-148 (unknown keys)
*/

/* FUNC 109-114
*/
#define KEY_F1		(KEY_F0+1)	/* 109 */
#define KEY_F2		(KEY_F0+2)	/* 10a */
#define KEY_F3		(KEY_F0+3)	/* 10b */
#define KEY_F4		(KEY_F0+4)	/* 10c */
#define KEY_F5		(KEY_F0+5)	/* 10d */
#define KEY_F6		(KEY_F0+6)	/* 10e */
#define KEY_F7		(KEY_F0+7)	/* 10f */
#define KEY_F8		(KEY_F0+8)	/* 110 */
#define KEY_F9		(KEY_F0+9)	/* 111 */
#define KEY_F10		(KEY_F0+10)	/* 112 */
#define KEY_F11		(KEY_F0+11)	/* 113 */
#define KEY_F12		(KEY_F0+12)	/* 114 reserved for text-area cmdline switch */

/* SHIFT_FUNC 115-120
*/
#define KEY_S_F1	(KEY_F0+13)	/* 115 */
#define KEY_S_F2	(KEY_F0+14)	/* 116 */
#define KEY_S_F3	(KEY_F0+15)	/* 117 */
#define KEY_S_F4	(KEY_F0+16)	/* 118 */
#define KEY_S_F5	(KEY_F0+17)	/* 119 */
#define KEY_S_F6	(KEY_F0+18)	/* 11a */
#define KEY_S_F7	(KEY_F0+19)	/* 11b */
#define KEY_S_F8	(KEY_F0+20)	/* 11c */
#define KEY_S_F9	(KEY_F0+21)	/* 11d */
#define KEY_S_F10	(KEY_F0+22)	/* 11e */
#define KEY_S_F11	(KEY_F0+23)	/* 11f */
#define KEY_S_F12	(KEY_F0+24)	/* 120 */

/* CONTROL_FUNC 121-12c
*/
#define KEY_C_F1	(KEY_F0+25)	/* 121 */
#define KEY_C_F2	(KEY_F0+26)	/* 122 */
#define KEY_C_F3	(KEY_F0+27)	/* 123 */
#define KEY_C_F4	(KEY_F0+28)	/* 124 */
#define KEY_C_F5	(KEY_F0+29)	/* 125 */
#define KEY_C_F6	(KEY_F0+30)	/* 126 */
#define KEY_C_F7	(KEY_F0+31)	/* 127 */
#define KEY_C_F8	(KEY_F0+32)	/* 128 */
#define KEY_C_F9	(KEY_F0+33)	/* 129 */
#define KEY_C_F10	(KEY_F0+34)	/* 12a */
#define KEY_C_F11	(KEY_F0+35)	/* 12b */
#define KEY_C_F12	(KEY_F0+36)	/* 12c */

/* usual control-key characters
* mapped into ASCII 01 - 1a
* ------------------
*/
#define KEY_C_A		0x01
#define KEY_C_B		0x02
#define KEY_C_C		0x03	/* reserved: ^C */
#define KEY_C_D		0x04	/* (EOT) */
#define KEY_C_E		0x05
#define KEY_C_F		0x06
#define KEY_C_G		0x07
#define KEY_C_H		0x08	/* reserved: ^H (ascii BS) see also BACKSP */
#define KEY_C_I		0x09	/* reserved: ^I or KEY_TAB */
#define KEY_C_J		0x0a
#define KEY_C_K		0x0b
#define KEY_C_L		0x0c
#define KEY_C_M		0x0d	/* reserved: ^M or KEY_RETURN */
#define KEY_C_N		0x0e
#define KEY_C_O		0x0f	/* (history in MC) */
#define KEY_C_P		0x10	/* (history in MC) */
#define KEY_C_Q		0x11	/* reserved: ^Q */
#define KEY_C_R		0x12
#define KEY_C_S		0x13	/* reserved: ^S */
#define KEY_C_T		0x14
#define KEY_C_U		0x15
#define KEY_C_V		0x16	/* wait for special char? */
#define KEY_C_W		0x17
#define KEY_C_X		0x18
#define KEY_C_Y		0x19
#define KEY_C_Z		0x1a	/* reserved: ^Z */

/* Meta characters:
* ------------------
*/
#define KEY_M_RETURN	(KEY_META | KEY_RETURN)

#define KEY_M_SPACE	(KEY_META | ' ')
#define KEY_M_EXCLAM	(KEY_META | '!')
#define KEY_M_DQUOTE	(KEY_META | '\"')
#define KEY_M_HASH	(KEY_META | '#')
#define KEY_M_DOLLAR	(KEY_META | '$')
#define KEY_M_PERCENT	(KEY_META | '%')
#define KEY_M_AMPERSAND	(KEY_META | '&')
#define KEY_M_SQUOTE	(KEY_META | '\'')
#define KEY_M_LPAREN	(KEY_META | '(')
#define KEY_M_RPAREN	(KEY_META | ')')
#define KEY_M_STAR	(KEY_META | '*')
#define KEY_M_PLUS	(KEY_META | '+')
#define KEY_M_COMMA	(KEY_META | ',')
#define KEY_M_MINUS	(KEY_META | '-')
#define KEY_M_DOT	(KEY_META | '.')
#define KEY_M_SLASH	(KEY_META | '/')
#define KEY_M_ZERO	(KEY_META | '0')
#define KEY_M_ONE	(KEY_META | '1')
#define KEY_M_TWO	(KEY_META | '2')
#define KEY_M_THREE	(KEY_META | '3')
#define KEY_M_FOUR	(KEY_META | '4')
#define KEY_M_FIVE	(KEY_META | '5')
#define KEY_M_SIX	(KEY_META | '6')
#define KEY_M_SEVEN	(KEY_META | '7')
#define KEY_M_EIGHT	(KEY_META | '8')
#define KEY_M_NINE	(KEY_META | '9')
#define KEY_M_COLON	(KEY_META | ':')
#define KEY_M_SEMICOLON	(KEY_META | ';')
#define KEY_M_LESSTHAN	(KEY_META | '<')
#define KEY_M_EQUAL	(KEY_META | '=')
#define KEY_M_GREATHAN	(KEY_META | '>')
#define KEY_M_QMARK	(KEY_META | '?')

#define KEY_M_AT	(KEY_META | '@')
#define KEY_S_M_A	(KEY_META | 'A')
#define KEY_S_M_B	(KEY_META | 'B')
#define KEY_S_M_C	(KEY_META | 'C')
#define KEY_S_M_D	(KEY_META | 'D')
#define KEY_S_M_E	(KEY_META | 'E')
#define KEY_S_M_F	(KEY_META | 'F')
#define KEY_S_M_G	(KEY_META | 'G')
#define KEY_S_M_H	(KEY_META | 'H')
#define KEY_S_M_I	(KEY_META | 'I')
#define KEY_S_M_J	(KEY_META | 'J')
#define KEY_S_M_K	(KEY_META | 'K')
#define KEY_S_M_L	(KEY_META | 'L')
#define KEY_S_M_M	(KEY_META | 'M')
#define KEY_S_M_N	(KEY_META | 'N')
#define KEY_S_M_O	(KEY_META | 'O')
#define KEY_S_M_P	(KEY_META | 'P')
#define KEY_S_M_Q	(KEY_META | 'Q')
#define KEY_S_M_R	(KEY_META | 'R')
#define KEY_S_M_S	(KEY_META | 'S')
#define KEY_S_M_T	(KEY_META | 'T')
#define KEY_S_M_U	(KEY_META | 'U')
#define KEY_S_M_V	(KEY_META | 'V')
#define KEY_S_M_W	(KEY_META | 'W')
#define KEY_S_M_X	(KEY_META | 'X')
#define KEY_S_M_Y	(KEY_META | 'Y')
#define KEY_S_M_Z	(KEY_META | 'Z')
#define KEY_M_LSQBRAC	(KEY_META | '[')
#define KEY_M_BACKSLASH	(KEY_META | '\\')
#define KEY_M_RSQBRAC	(KEY_META | ']')
#define KEY_M_CARET	(KEY_META | '^')
#define KEY_M_UNDERLINE	(KEY_META | '_')

#define KEY_M_GRAVEACC	(KEY_META | '`')
#define KEY_M_A		(KEY_META | 'a')
#define KEY_M_B		(KEY_META | 'b')
#define KEY_M_C		(KEY_META | 'c')
#define KEY_M_D		(KEY_META | 'd')
#define KEY_M_E		(KEY_META | 'e')
#define KEY_M_F		(KEY_META | 'f')
#define KEY_M_G		(KEY_META | 'g')
#define KEY_M_H		(KEY_META | 'h')
#define KEY_M_I		(KEY_META | 'i')
#define KEY_M_J		(KEY_META | 'j')
#define KEY_M_K		(KEY_META | 'k')
#define KEY_M_L		(KEY_META | 'l')
#define KEY_M_M		(KEY_META | 'm')
#define KEY_M_N		(KEY_META | 'n')
#define KEY_M_O		(KEY_META | 'o')
#define KEY_M_P		(KEY_META | 'p')
#define KEY_M_Q		(KEY_META | 'q')
#define KEY_M_R		(KEY_META | 'r')
#define KEY_M_S		(KEY_META | 's')
#define KEY_M_T		(KEY_META | 't')
#define KEY_M_U		(KEY_META | 'u')
#define KEY_M_V		(KEY_META | 'v')
#define KEY_M_W		(KEY_META | 'w')
#define KEY_M_X		(KEY_META | 'x')
#define KEY_M_Y		(KEY_META | 'y')
#define KEY_M_Z		(KEY_META | 'z')
#define KEY_M_LCURBRAC	(KEY_META | '{')
#define KEY_M_BAR	(KEY_META | '|')
#define KEY_M_RCURBRAC	(KEY_META | '}')
#define KEY_M_TILDE	(KEY_META | '~')

/* direct mapping for these, optionally
* ---------------------------------------------
*/
#define KEY_C_UP	(KEY_CTRL | 1)
#define KEY_C_DOWN	(KEY_CTRL | 2)
#define KEY_C_LEFT	(KEY_CTRL | 3)
#define KEY_C_RIGHT	(KEY_CTRL | 4)
#define KEY_C_PPAGE	(KEY_CTRL | 5)
#define KEY_C_NPAGE	(KEY_CTRL | 6)

#endif
