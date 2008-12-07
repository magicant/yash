/* Yash: yet another shell */
/* terminfo.h: interface to terminfo and termios */
/* (C) 2007-2008 magicant */

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


#ifndef YASH_TERMINFO_H
#define YASH_TERMINFO_H


#define TI_ka1     "ka1"
#define TI_ka3     "ka3"
#define TI_kb2     "kb2"
#define TI_kbs     "kbs"
#define TI_kbeg    "kbeg"
#define TI_kcbt    "kcbt"
#define TI_kc1     "kc1"
#define TI_kc3     "kc3"
#define TI_kcan    "kcan"
#define TI_ktbc    "ktbc"
#define TI_kclr    "kclr"
#define TI_kclo    "kclo"
#define TI_kcmd    "kcmd"
#define TI_kcpy    "kcpy"
#define TI_kcrt    "kcrt"
#define TI_kctab   "kctab"
#define TI_kdch1   "kdch1"
#define TI_kdl1    "kdl1"
#define TI_kcud1   "kcud1"
#define TI_krmir   "krmir"
#define TI_kend    "kend"
#define TI_kent    "kent"
#define TI_kel     "kel"
#define TI_ked     "ked"
#define TI_kext    "kext"
#define TI_kf0     "kf0"
#define TI_kf1     "kf1"
#define TI_kf2     "kf2"
#define TI_kf3     "kf3"
#define TI_kf4     "kf4"
#define TI_kf5     "kf5"
#define TI_kf6     "kf6"
#define TI_kf7     "kf7"
#define TI_kf8     "kf8"
#define TI_kf9     "kf9"
#define TI_kf10    "kf10"
#define TI_kf11    "kf11"
#define TI_kf12    "kf12"
#define TI_kf13    "kf13"
#define TI_kf14    "kf14"
#define TI_kf15    "kf15"
#define TI_kf16    "kf16"
#define TI_kf17    "kf17"
#define TI_kf18    "kf18"
#define TI_kf19    "kf19"
#define TI_kf20    "kf20"
#define TI_kf21    "kf21"
#define TI_kf22    "kf22"
#define TI_kf23    "kf23"
#define TI_kf24    "kf24"
#define TI_kf25    "kf25"
#define TI_kf26    "kf26"
#define TI_kf27    "kf27"
#define TI_kf28    "kf28"
#define TI_kf29    "kf29"
#define TI_kf30    "kf30"
#define TI_kf31    "kf31"
#define TI_kf32    "kf32"
#define TI_kf33    "kf33"
#define TI_kf34    "kf34"
#define TI_kf35    "kf35"
#define TI_kf36    "kf36"
#define TI_kf37    "kf37"
#define TI_kf38    "kf38"
#define TI_kf39    "kf39"
#define TI_kf40    "kf40"
#define TI_kf41    "kf41"
#define TI_kf42    "kf42"
#define TI_kf43    "kf43"
#define TI_kf44    "kf44"
#define TI_kf45    "kf45"
#define TI_kf46    "kf46"
#define TI_kf47    "kf47"
#define TI_kf48    "kf48"
#define TI_kf49    "kf49"
#define TI_kf50    "kf50"
#define TI_kf51    "kf51"
#define TI_kf52    "kf52"
#define TI_kf53    "kf53"
#define TI_kf54    "kf54"
#define TI_kf55    "kf55"
#define TI_kf56    "kf56"
#define TI_kf57    "kf57"
#define TI_kf58    "kf58"
#define TI_kf59    "kf59"
#define TI_kf60    "kf60"
#define TI_kf61    "kf61"
#define TI_kf62    "kf62"
#define TI_kf63    "kf63"
#define TI_kfnd    "kfnd"
#define TI_khlp    "khlp"
#define TI_khome   "khome"
#define TI_kich1   "kich1"
#define TI_kil1    "kil1"
#define TI_kcub1   "kcub1"
#define TI_kll     "kll"
#define TI_kmrk    "kmrk"
#define TI_kmsg    "kmsg"
#define TI_kmous   "kmous"
#define TI_kmov    "kmov"
#define TI_knxt    "knxt"
#define TI_knp     "knp"
#define TI_kopn    "kopn"
#define TI_kopt    "kopt"
#define TI_kpp     "kpp"
#define TI_kprv    "kprv"
#define TI_kprt    "kprt"
#define TI_krdo    "krdo"
#define TI_kref    "kref"
#define TI_krfr    "krfr"
#define TI_krpl    "krpl"
#define TI_krst    "krst"
#define TI_kres    "kres"
#define TI_kcuf1   "kcuf1"
#define TI_ksav    "ksav"
#define TI_kslt    "kslt"
#define TI_kind    "kind"
#define TI_kri     "kri"
#define TI_khts    "khts"
#define TI_kspd    "kspd"
#define TI_kund    "kund"
#define TI_kcuu1   "kcuu1"
#define TI_kBEG    "kBEG"
#define TI_kCAN    "kCAN"
#define TI_kCMD    "kCMD"
#define TI_kCPY    "kCPY"
#define TI_kCRT    "kCRT"
#define TI_kDC     "kDC"
#define TI_kDL     "kDL"
#define TI_kEND    "kEND"
#define TI_kEOL    "kEOL"
#define TI_kEXT    "kEXT"
#define TI_kFND    "kFND"
#define TI_kHLP    "kHLP"
#define TI_kHOM    "kHOM"
#define TI_kIC     "kIC"
#define TI_kLFT    "kLFT"
#define TI_kMSG    "kMSG"
#define TI_kMOV    "kMOV"
#define TI_kNXT    "kNXT"
#define TI_kOPT    "kOPT"
#define TI_kPRV    "kPRV"
#define TI_kPRT    "kPRT"
#define TI_kRDO    "kRDO"
#define TI_kRPL    "kRPL"
#define TI_kRIT    "kRIT"
#define TI_kRES    "kRES"
#define TI_kSAV    "kSAV"
#define TI_kSPD    "kSPD"
#define TI_kUND    "kUND"
#define TI_cols    "cols"
#define TI_km      "km"
#define TI_lines   "lines"
#define TI_op      "op"
#define TI_setab   "setab"
#define TI_setaf   "setaf"
#define TI_setb    "setb"
#define TI_setf    "setf"
#define TI_sgr     "sgr"


extern int yle_lines, yle_columns;
extern _Bool yle_meta_bit8;
extern struct trienode_T /* trie_T */ *yle_keycodes;

extern _Bool yle_setupterm(void);

extern void yle_print_sgr(long standout, long underline, long reverse,
	long blink, long dim, long bold, long invisible);
extern void yle_print_op(void);
extern void yle_print_setfg(int color);
extern void yle_print_setbg(int color);
extern void yle_alert(void);


extern char yle_eof_char, yle_kill_char, yle_interrupt_char, yle_erase_char;

extern _Bool yle_set_terminal(void);
extern _Bool yle_restore_terminal(void);


#endif /* YASH_TERMINFO_H */


/* vim: set ts=8 sts=4 sw=4 noet: */
