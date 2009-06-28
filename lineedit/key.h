/* Yash: yet another shell */
/* key.h: definition of key codes */
/* (C) 2007-2009 magicant */

/* This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.  */


#ifndef YASH_KEY_H
#define YASH_KEY_H

#include <stddef.h>


/* Strings representing special keys */
#define Key_a1        L"\\a1"   // upper left of keypad
#define Key_a3        L"\\a3"   // upper right of keypad
#define Key_b2        L"\\b2"   // center of keypad
#define Key_backspace L"\\B"    // backspace
#define Key_beg       L"\\bg"   // beg
#define Key_btab      L"\\bt"   // back-tab
#define Key_c1        L"\\c1"   // lower left of keypad
#define Key_c3        L"\\c3"   // lower right of keypad
#define Key_cancel    L"\\cn"   // cancel key
#define Key_catab     L"\\ca"   // clear-all-tabs key
#define Key_clear     L"\\cs"   // clear-screen or erase key
#define Key_close     L"\\cl"   // close key
#define Key_command   L"\\co"   // command (cmd) key
#define Key_copy      L"\\cp"   // copy key
#define Key_create    L"\\cr"   // create key
#define Key_ctab      L"\\ct"   // clear-tab key
#define Key_delete    L"\\X"    // delete (delete-character) key
#define Key_dl        L"\\dl"   // delete-line key
#define Key_down      L"\\D"    // down-arrow key
#define Key_eic       L"\\ei"   // exit-insert-mode
#define Key_end       L"\\E"    // end key
#define Key_enter     L"\\et"   // enter/send key
#define Key_eol       L"\\el"   // clear-to-end-of-line key
#define Key_eos       L"\\es"   // clear-to-end-of-screen key
#define Key_exit      L"\\ex"   // exit key
#define Key_f00       L"\\F00"  // function key F0
#define Key_f01       L"\\F01"  // function key F1
#define Key_f02       L"\\F02"  // function key F2
#define Key_f03       L"\\F03"  // function key F3
#define Key_f04       L"\\F04"  // function key F4
#define Key_f05       L"\\F05"  // function key F5
#define Key_f06       L"\\F06"  // function key F6
#define Key_f07       L"\\F07"  // function key F7
#define Key_f08       L"\\F08"  // function key F8
#define Key_f09       L"\\F09"  // function key F9
#define Key_f10       L"\\F10"  // function key F10
#define Key_f11       L"\\F11"  // function key F11
#define Key_f12       L"\\F12"  // function key F12
#define Key_f13       L"\\F13"  // function key F13
#define Key_f14       L"\\F14"  // function key F14
#define Key_f15       L"\\F15"  // function key F15
#define Key_f16       L"\\F16"  // function key F16
#define Key_f17       L"\\F17"  // function key F17
#define Key_f18       L"\\F18"  // function key F18
#define Key_f19       L"\\F19"  // function key F19
#define Key_f20       L"\\F20"  // function key F20
#define Key_f21       L"\\F21"  // function key F21
#define Key_f22       L"\\F22"  // function key F22
#define Key_f23       L"\\F23"  // function key F23
#define Key_f24       L"\\F24"  // function key F24
#define Key_f25       L"\\F25"  // function key F25
#define Key_f26       L"\\F26"  // function key F26
#define Key_f27       L"\\F27"  // function key F27
#define Key_f28       L"\\F28"  // function key F28
#define Key_f29       L"\\F29"  // function key F29
#define Key_f30       L"\\F30"  // function key F30
#define Key_f31       L"\\F31"  // function key F31
#define Key_f32       L"\\F32"  // function key F32
#define Key_f33       L"\\F33"  // function key F33
#define Key_f34       L"\\F34"  // function key F34
#define Key_f35       L"\\F35"  // function key F35
#define Key_f36       L"\\F36"  // function key F36
#define Key_f37       L"\\F37"  // function key F37
#define Key_f38       L"\\F38"  // function key F38
#define Key_f39       L"\\F39"  // function key F39
#define Key_f40       L"\\F40"  // function key F40
#define Key_f41       L"\\F41"  // function key F41
#define Key_f42       L"\\F42"  // function key F42
#define Key_f43       L"\\F43"  // function key F43
#define Key_f44       L"\\F44"  // function key F44
#define Key_f45       L"\\F45"  // function key F45
#define Key_f46       L"\\F46"  // function key F46
#define Key_f47       L"\\F47"  // function key F47
#define Key_f48       L"\\F48"  // function key F48
#define Key_f49       L"\\F49"  // function key F49
#define Key_f50       L"\\F50"  // function key F50
#define Key_f51       L"\\F51"  // function key F51
#define Key_f52       L"\\F52"  // function key F52
#define Key_f53       L"\\F53"  // function key F53
#define Key_f54       L"\\F54"  // function key F54
#define Key_f55       L"\\F55"  // function key F55
#define Key_f56       L"\\F56"  // function key F56
#define Key_f57       L"\\F57"  // function key F57
#define Key_f58       L"\\F58"  // function key F58
#define Key_f59       L"\\F59"  // function key F59
#define Key_f60       L"\\F60"  // function key F60
#define Key_f61       L"\\F61"  // function key F61
#define Key_f62       L"\\F62"  // function key F62
#define Key_f63       L"\\F63"  // function key F63
#define Key_find      L"\\fd"   // find key
#define Key_help      L"\\hp"   // help key
#define Key_home      L"\\H"    // home key
#define Key_insert    L"\\I"    // insert (insert-char/enter-insert-mode)
#define Key_il        L"\\il"   // insert-line key
#define Key_left      L"\\L"    // left-arrow key
#define Key_ll        L"\\ll"   // home-down key
#define Key_mark      L"\\mk"   // mark key
#define Key_message   L"\\me"   // message key
#define Key_mouse     L"\\ms"   // mouse event
#define Key_move      L"\\mv"   // move key
#define Key_next      L"\\nx"   // next-object key
#define Key_pagedown  L"\\N"    // page-down (next-page) key
#define Key_open      L"\\on"   // open key
#define Key_options   L"\\op"   // options key
#define Key_pageup    L"\\P"    // page-up (previous-page) key
#define Key_previous  L"\\pv"   // previous-object key
#define Key_print     L"\\pr"   // print/copy key
#define Key_redo      L"\\rd"   // redo key
#define Key_reference L"\\rf"   // ref(erence) key
#define Key_refresh   L"\\rh"   // refresh key
#define Key_replace   L"\\rp"   // replace key
#define Key_restart   L"\\rs"   // restart key
#define Key_resume    L"\\re"   // resume key
#define Key_right     L"\\R"    // right-arrow key
#define Key_save      L"\\sv"   // save key
#define Key_select    L"\\sl"   // select key
#define Key_sf        L"\\sf"   // scroll-forward key
#define Key_sr        L"\\sr"   // scroll-backward key
#define Key_stab      L"\\st"   // set-tab key
#define Key_suspend   L"\\su"   // suspend key
#define Key_undo      L"\\ud"   // undo key
#define Key_up        L"\\U"    // up-arrow key
#define Key_s_beg     L"\\Sbg"  // shift+beginning
#define Key_s_cancel  L"\\Scn"  // shift+cancel
#define Key_s_command L"\\Sco"  // shift+command
#define Key_s_copy    L"\\Scp"  // shift+copy
#define Key_s_create  L"\\Scr"  // shift+create
#define Key_s_delete  L"\\SX"   // shift+delete
#define Key_s_dl      L"\\Sdl"  // shift+delete-line
#define Key_s_end     L"\\SE"   // shift+end
#define Key_s_eol     L"\\Sel"  // shift+end-of-line
#define Key_s_exit    L"\\Sex"  // shift+exit
#define Key_s_find    L"\\Sfd"  // shift+find
#define Key_s_help    L"\\Shp"  // shift+help
#define Key_s_home    L"\\SH"   // shift+home
#define Key_s_insert  L"\\SI"   // shift+insert
#define Key_s_left    L"\\SL"   // shift+left
#define Key_s_message L"\\Smg"  // shift+message
#define Key_s_move    L"\\Smv"  // shift+move
#define Key_s_next    L"\\Snx"  // shift+next
#define Key_s_options L"\\Sop"  // shift+options
#define Key_s_prev    L"\\Spv"  // shift+previous
#define Key_s_print   L"\\Spr"  // shift+print
#define Key_s_redo    L"\\Srd"  // shift+redo
#define Key_s_replace L"\\Srp"  // shift+replace
#define Key_s_right   L"\\SR"   // shift+right
#define Key_s_resume  L"\\Sre"  // shift+resume
#define Key_s_save    L"\\Ssv"  // shift+save
#define Key_s_suspend L"\\Ssu"  // shift+suspend
#define Key_s_undo    L"\\Sud"  // shift+undo
#define Key_c_at      L"\\^@"   // ctrl+@
#define Key_c_a       L"\\^A"   // ctrl+A
#define Key_c_b       L"\\^B"   // ctrl+B
#define Key_c_c       L"\\^C"   // ctrl+C
#define Key_c_d       L"\\^D"   // ctrl+D
#define Key_c_e       L"\\^E"   // ctrl+E
#define Key_c_f       L"\\^F"   // ctrl+F
#define Key_c_g       L"\\^G"   // ctrl+G
#define Key_c_h       L"\\^H"   // ctrl+H
#define Key_c_i       L"\\^I"   // ctrl+I
#define Key_c_j       L"\\^J"   // ctrl+J
#define Key_c_k       L"\\^K"   // ctrl+K
#define Key_c_l       L"\\^L"   // ctrl+L
#define Key_c_m       L"\\^M"   // ctrl+M
#define Key_c_n       L"\\^N"   // ctrl+N
#define Key_c_o       L"\\^O"   // ctrl+O
#define Key_c_p       L"\\^P"   // ctrl+P
#define Key_c_q       L"\\^Q"   // ctrl+Q
#define Key_c_r       L"\\^R"   // ctrl+R
#define Key_c_s       L"\\^S"   // ctrl+S
#define Key_c_t       L"\\^T"   // ctrl+T
#define Key_c_u       L"\\^U"   // ctrl+U
#define Key_c_v       L"\\^V"   // ctrl+V
#define Key_c_w       L"\\^W"   // ctrl+W
#define Key_c_x       L"\\^X"   // ctrl+X
#define Key_c_y       L"\\^Y"   // ctrl+Y
#define Key_c_z       L"\\^Z"   // ctrl+Z
#define Key_c_lb      L"\\^["   // ctrl+[
#define Key_c_bs      L"\\^\\"  // ctrl+\ //
#define Key_c_rb      L"\\^]"   // ctrl+]
#define Key_c_hat     L"\\^^"   // ctrl+^
#define Key_c_ul      L"\\^_"   // ctrl+_
#define Key_c_del     L"\\^?"   // delete
#define Key_interrupt L"\\!"    // INTR
#define Key_eof       L"\\#"    // EOF
#define Key_kill      L"\\$"    // KILL
#define Key_erase     L"\\?"    // ERASE
#define Key_tab       Key_c_i
#define Key_newline   Key_c_j
#define Key_cr        Key_c_m
#define Key_escape    Key_c_lb
#define Key_backslash L"\\\\"   // must end with '\\'

#define META_BIT    0x80
#define ESCAPE_CHAR '\33'

/* The type of functions that implement commands. */
typedef void le_command_func_T(wchar_t wc);


#endif /* YASH_KEY_H */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
