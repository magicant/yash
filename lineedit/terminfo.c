/* Yash: yet another shell */
/* terminfo.c: interface to terminfo and termios */
/* (C) 2007-2012 magicant */

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


#include "../common.h"
#include "terminfo.h"
#include <assert.h>
#include <ctype.h>
#if HAVE_CURSES_H
# include <curses.h>
#elif HAVE_NCURSES_H
# include <ncurses.h>
#elif HAVE_NCURSES_NCURSES_H
# include <ncurses/ncurses.h>
#elif HAVE_NCURSESW_NCURSES_H
# include <ncursesw/ncurses.h>
#endif
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#if HAVE_TIOCGWINSZ
# include <sys/ioctl.h>
#endif
#if HAVE_TERM_H
# include <term.h>
#elif HAVE_NCURSES_TERM_H
# include <ncurses/term.h>
#elif HAVE_NCURSESW_TERM_H
# include <ncursesw/term.h>
#endif
#include <termios.h>
#include <unistd.h>
#include "../option.h"
#include "../sig.h"
#include "../util.h"
#include "../variable.h"
#include "display.h"
#include "key.h"
#include "trie.h"


/********** TERMINFO **********/


/* terminfo capabilities */
#define TI_am      "am"
#define TI_bel     "bel"
#define TI_blink   "blink"
#define TI_bold    "bold"
#define TI_clear   "clear"
#define TI_colors  "colors"
#define TI_cols    "cols"
#define TI_cr      "cr"
#define TI_cub     "cub"
#define TI_cub1    "cub1"
#define TI_cud     "cud"
#define TI_cud1    "cud1"
#define TI_cuf     "cuf"
#define TI_cuf1    "cuf1"
#define TI_cuu     "cuu"
#define TI_cuu1    "cuu1"
#define TI_dim     "dim"
#define TI_ed      "ed"
#define TI_el      "el"
#define TI_flash   "flash"
#define TI_invis   "invis"
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
#define TI_kMOV    "kMOV"
#define TI_kMSG    "kMSG"
#define TI_kNXT    "kNXT"
#define TI_kOPT    "kOPT"
#define TI_kPRT    "kPRT"
#define TI_kPRV    "kPRV"
#define TI_kRDO    "kRDO"
#define TI_kRES    "kRES"
#define TI_kRIT    "kRIT"
#define TI_kRPL    "kRPL"
#define TI_kSAV    "kSAV"
#define TI_kSPD    "kSPD"
#define TI_kUND    "kUND"
#define TI_ka1     "ka1"
#define TI_ka3     "ka3"
#define TI_kb2     "kb2"
#define TI_kbeg    "kbeg"
#define TI_kbs     "kbs"
#define TI_kc1     "kc1"
#define TI_kc3     "kc3"
#define TI_kcan    "kcan"
#define TI_kcbt    "kcbt"
#define TI_kclo    "kclo"
#define TI_kclr    "kclr"
#define TI_kcmd    "kcmd"
#define TI_kcpy    "kcpy"
#define TI_kcrt    "kcrt"
#define TI_kctab   "kctab"
#define TI_kcub1   "kcub1"
#define TI_kcud1   "kcud1"
#define TI_kcuf1   "kcuf1"
#define TI_kcuu1   "kcuu1"
#define TI_kdch1   "kdch1"
#define TI_kdl1    "kdl1"
#define TI_ked     "ked"
#define TI_kel     "kel"
#define TI_kend    "kend"
#define TI_kent    "kent"
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
#define TI_khts    "khts"
#define TI_kich1   "kich1"
#define TI_kil1    "kil1"
#define TI_kind    "kind"
#define TI_kll     "kll"
#define TI_km      "km"
#define TI_kmous   "kmous"
#define TI_kmov    "kmov"
#define TI_kmrk    "kmrk"
#define TI_kmsg    "kmsg"
#define TI_knp     "knp"
#define TI_knxt    "knxt"
#define TI_kopn    "kopn"
#define TI_kopt    "kopt"
#define TI_kpp     "kpp"
#define TI_kprt    "kprt"
#define TI_kprv    "kprv"
#define TI_krdo    "krdo"
#define TI_kref    "kref"
#define TI_kres    "kres"
#define TI_krfr    "krfr"
#define TI_kri     "kri"
#define TI_krmir   "krmir"
#define TI_krpl    "krpl"
#define TI_krst    "krst"
#define TI_ksav    "ksav"
#define TI_kslt    "kslt"
#define TI_kspd    "kspd"
#define TI_ktbc    "ktbc"
#define TI_kund    "kund"
#define TI_lines   "lines"
#define TI_msgr    "msgr"
#define TI_nel     "nel"
#define TI_op      "op"
#define TI_rev     "rev"
#define TI_rmkx    "rmkx"
#define TI_setab   "setab"
#define TI_setaf   "setaf"
#define TI_setb    "setb"
#define TI_setf    "setf"
#define TI_sgr0    "sgr0"
#define TI_smkx    "smkx"
#define TI_smso    "smso"
#define TI_smul    "smul"
#define TI_xenl    "xenl"
#define TI_xmc     "xmc"


/* This flag is set to true when the terminfo database needs to be refreshed
 * because the $TERM variable has been changed. */
_Bool le_need_term_update = 1;

/* Number of lines, columns and colors available in the current terminal. */
/* Initialized in `le_setupterm'. */
int le_lines, le_columns, le_colors;

/* The value of the "xmc" capability. */
int le_ti_xmc;

/* Whether the terminal has the "xenl" and "msgr" flags set. */
_Bool le_ti_xenl, le_ti_msgr;

/* True if the meta key inputs character whose 8th bit is set. */
/* Used only if the `shopt_le_convmeta' option is "auto". */
_Bool le_meta_bit8;

/* Strings sent by terminal when special key is pressed.
 * The values of entries are `keyseq'. */
trie_T *le_keycodes = NULL;

/* True if the terminal is set to the keyboard-transmit mode. */
static _Bool transmit_mode = 0;


static inline int is_strcap_valid(const char *s)
    __attribute__((const));
static void set_up_keycodes(void);
static _Bool try_print_cap(const char *capname)
    __attribute__((nonnull));
static void move_cursor(char *capone, char *capmul, long count, int affcnt)
    __attribute__((nonnull));
static _Bool move_cursor_1(char *capone, long count)
    __attribute__((nonnull));
static _Bool move_cursor_mul(char *capmul, long count, int affcnt)
    __attribute__((nonnull));
static void print_color_code(long color, char *seta, char *set)
    __attribute__((nonnull));
static void print_smkx(void);
static void print_rmkx(void);
static int putchar_stderr(int c);


/* Checks if the result of `tigetstr' is valid. */
int is_strcap_valid(const char *s)
{
    return s != NULL && s != (const char *) -1;
}

/* Calls `setupterm' and checks if terminfo data is available.
 * If `bypass' is true and `le_need_term_update' is false, the terminfo data
 * are not refreshed and only the terminal size (`le_lines' and `le_columns')
 * is adjusted.
 * Returns true iff successful. */
_Bool le_setupterm(_Bool bypass)
{
    static _Bool once = 0;
    int err;

    reset_sigwinch();

    assert(once || le_need_term_update);
#if HAVE_TIOCGWINSZ
    if (bypass && !le_need_term_update) {
	struct winsize ws;
	if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0
		&& ws.ws_row > 0 && ws.ws_col > 0) {
	    if (getenv(VAR_LINES) == NULL)
		le_lines = ws.ws_row;
	    if (getenv(VAR_COLUMNS) == NULL)
		le_columns = ws.ws_col;
	    return 1;
	}
    }
#else
    (void) bypass;
#endif /* HAVE_TIOCGWINSZ */

    if (once)
	del_curterm(cur_term);
    if (setupterm(NULL, STDERR_FILENO, &err) == ERR)
	return 0;
    once = 1;

    if (tigetflag(TI_am) <= 0)                     return 0;
    if (!is_strcap_valid(tigetstr(TI_cub1))
	    && !is_strcap_valid(tigetstr(TI_cub))) return 0;
    if (!is_strcap_valid(tigetstr(TI_cuf1))
	    && !is_strcap_valid(tigetstr(TI_cuf))) return 0;
    if (!is_strcap_valid(tigetstr(TI_cud1))
	    && !is_strcap_valid(tigetstr(TI_cud))) return 0;
    if (!is_strcap_valid(tigetstr(TI_cuu1))
	    && !is_strcap_valid(tigetstr(TI_cuu))) return 0;
    if (!is_strcap_valid(tigetstr(TI_el)))         return 0;

    le_lines = tigetnum(TI_lines);
    le_columns = tigetnum(TI_cols);
    if (le_lines <= 0 || le_columns <= 0)
	return 0;

    le_colors = tigetnum(TI_colors);
    le_ti_xmc = tigetnum(TI_xmc);
    le_ti_xenl = tigetflag(TI_xenl) > 0;
    le_ti_msgr = tigetflag(TI_msgr) > 0;
    le_meta_bit8 = tigetflag(TI_km) > 0;

    set_up_keycodes();

    le_need_term_update = 0;
    return 1;
}

/* Initializes `le_keycodes'. */
void set_up_keycodes(void)
{
    trie_destroy(le_keycodes);

    static const struct charmap {
	char c;  const wchar_t *keyseq;
    } charmap[] = {
	{ '\01', Key_c_a,  }, { '\02', Key_c_b,   }, { '\03', Key_c_c,   },
	{ '\04', Key_c_d,  }, { '\05', Key_c_e,   }, { '\06', Key_c_f,   },
	{ '\07', Key_c_g,  }, { '\10', Key_c_h,   }, { '\11', Key_c_i,   },
	{ '\12', Key_c_j,  }, { '\13', Key_c_k,   }, { '\14', Key_c_l,   },
	{ '\15', Key_c_m,  }, { '\16', Key_c_n,   }, { '\17', Key_c_o,   },
	{ '\20', Key_c_p,  }, { '\21', Key_c_q,   }, { '\22', Key_c_r,   },
	{ '\23', Key_c_s,  }, { '\24', Key_c_t,   }, { '\25', Key_c_u,   },
	{ '\26', Key_c_v,  }, { '\27', Key_c_w,   }, { '\30', Key_c_x,   },
	{ '\31', Key_c_y,  }, { '\32', Key_c_z,   }, { '\33', Key_c_lb,  },
	{ '\34', Key_c_bs, }, { '\35', Key_c_rb,  }, { '\36', Key_c_hat, },
	{ '\37', Key_c_ul, }, { '\77', Key_c_del, },
    };
    static const struct keymap {
	char *capability;  const wchar_t *keyseq;
    } keymap [] = {
	{ TI_ka1,   Key_a1, },
	{ TI_ka3,   Key_a3, },
	{ TI_kb2,   Key_b2, },
	{ TI_kbs,   Key_backspace, },
	{ TI_kbeg,  Key_beg, },
	{ TI_kcbt,  Key_btab, },
	{ TI_kc1,   Key_c1, },
	{ TI_kc3,   Key_c3, },
	{ TI_kcan,  Key_cancel, },
	{ TI_ktbc,  Key_catab, },
	{ TI_kclr,  Key_clear, },
	{ TI_kclo,  Key_close, },
	{ TI_kcmd,  Key_command, },
	{ TI_kcpy,  Key_copy, },
	{ TI_kcrt,  Key_create, },
	{ TI_kctab, Key_ctab, },
	{ TI_kdch1, Key_delete, },
	{ TI_kdl1,  Key_dl, },
	{ TI_kcud1, Key_down, },
	{ TI_krmir, Key_eic, },
	{ TI_kend,  Key_end, },
	{ TI_kent,  Key_enter, },
	{ TI_kel,   Key_eol, },
	{ TI_ked,   Key_eos, },
	{ TI_kext,  Key_exit, },
	{ TI_kf0,   Key_f00, },
	{ TI_kf1,   Key_f01, },
	{ TI_kf2,   Key_f02, },
	{ TI_kf3,   Key_f03, },
	{ TI_kf4,   Key_f04, },
	{ TI_kf5,   Key_f05, },
	{ TI_kf6,   Key_f06, },
	{ TI_kf7,   Key_f07, },
	{ TI_kf8,   Key_f08, },
	{ TI_kf9,   Key_f09, },
	{ TI_kf10,  Key_f10, },
	{ TI_kf11,  Key_f11, },
	{ TI_kf12,  Key_f12, },
	{ TI_kf13,  Key_f13, },
	{ TI_kf14,  Key_f14, },
	{ TI_kf15,  Key_f15, },
	{ TI_kf16,  Key_f16, },
	{ TI_kf17,  Key_f17, },
	{ TI_kf18,  Key_f18, },
	{ TI_kf19,  Key_f19, },
	{ TI_kf20,  Key_f20, },
	{ TI_kf21,  Key_f21, },
	{ TI_kf22,  Key_f22, },
	{ TI_kf23,  Key_f23, },
	{ TI_kf24,  Key_f24, },
	{ TI_kf25,  Key_f25, },
	{ TI_kf26,  Key_f26, },
	{ TI_kf27,  Key_f27, },
	{ TI_kf28,  Key_f28, },
	{ TI_kf29,  Key_f29, },
	{ TI_kf30,  Key_f30, },
	{ TI_kf31,  Key_f31, },
	{ TI_kf32,  Key_f32, },
	{ TI_kf33,  Key_f33, },
	{ TI_kf34,  Key_f34, },
	{ TI_kf35,  Key_f35, },
	{ TI_kf36,  Key_f36, },
	{ TI_kf37,  Key_f37, },
	{ TI_kf38,  Key_f38, },
	{ TI_kf39,  Key_f39, },
	{ TI_kf40,  Key_f40, },
	{ TI_kf41,  Key_f41, },
	{ TI_kf42,  Key_f42, },
	{ TI_kf43,  Key_f43, },
	{ TI_kf44,  Key_f44, },
	{ TI_kf45,  Key_f45, },
	{ TI_kf46,  Key_f46, },
	{ TI_kf47,  Key_f47, },
	{ TI_kf48,  Key_f48, },
	{ TI_kf49,  Key_f49, },
	{ TI_kf50,  Key_f50, },
	{ TI_kf51,  Key_f51, },
	{ TI_kf52,  Key_f52, },
	{ TI_kf53,  Key_f53, },
	{ TI_kf54,  Key_f54, },
	{ TI_kf55,  Key_f55, },
	{ TI_kf56,  Key_f56, },
	{ TI_kf57,  Key_f57, },
	{ TI_kf58,  Key_f58, },
	{ TI_kf59,  Key_f59, },
	{ TI_kf60,  Key_f60, },
	{ TI_kf61,  Key_f61, },
	{ TI_kf62,  Key_f62, },
	{ TI_kf63,  Key_f63, },
	{ TI_kfnd,  Key_find, },
	{ TI_khlp,  Key_help, },
	{ TI_khome, Key_home, },
	{ TI_kich1, Key_insert, },
	{ TI_kil1,  Key_il, },
	{ TI_kcub1, Key_left, },
	{ TI_kll,   Key_ll, },
	{ TI_kmrk,  Key_mark, },
	{ TI_kmsg,  Key_message, },
	{ TI_kmous, Key_mouse, },
	{ TI_kmov,  Key_move, },
	{ TI_knxt,  Key_next, },
	{ TI_knp,   Key_pagedown, },
	{ TI_kopn,  Key_open, },
	{ TI_kopt,  Key_options, },
	{ TI_kpp,   Key_pageup, },
	{ TI_kprv,  Key_previous, },
	{ TI_kprt,  Key_print, },
	{ TI_krdo,  Key_redo, },
	{ TI_kref,  Key_reference, },
	{ TI_krfr,  Key_refresh, },
	{ TI_krpl,  Key_replace, },
	{ TI_krst,  Key_restart, },
	{ TI_kres,  Key_resume, },
	{ TI_kcuf1, Key_right, },
	{ TI_ksav,  Key_save, },
	{ TI_kslt,  Key_select, },
	{ TI_kind,  Key_sf, },
	{ TI_kri,   Key_sr, },
	{ TI_khts,  Key_stab, },
	{ TI_kspd,  Key_suspend, },
	{ TI_kund,  Key_undo, },
	{ TI_kcuu1, Key_up, },
	{ TI_kBEG,  Key_s_beg, },
	{ TI_kCAN,  Key_s_cancel, },
	{ TI_kCMD,  Key_s_command, },
	{ TI_kCPY,  Key_s_copy, },
	{ TI_kCRT,  Key_s_create, },
	{ TI_kDC,   Key_s_delete, },
	{ TI_kDL,   Key_s_dl, },
	{ TI_kEND,  Key_s_end, },
	{ TI_kEOL,  Key_s_eol, },
	{ TI_kEXT,  Key_s_exit, },
	{ TI_kFND,  Key_s_find, },
	{ TI_kHLP,  Key_s_help, },
	{ TI_kHOM,  Key_s_home, },
	{ TI_kIC,   Key_s_insert, },
	{ TI_kLFT,  Key_s_left, },
	{ TI_kMSG,  Key_s_message, },
	{ TI_kMOV,  Key_s_move, },
	{ TI_kNXT,  Key_s_next, },
	{ TI_kOPT,  Key_s_options, },
	{ TI_kPRV,  Key_s_prev, },
	{ TI_kPRT,  Key_s_print, },
	{ TI_kRDO,  Key_s_redo, },
	{ TI_kRPL,  Key_s_replace, },
	{ TI_kRIT,  Key_s_right, },
	{ TI_kRES,  Key_s_resume, },
	{ TI_kSAV,  Key_s_save, },
	{ TI_kSPD,  Key_s_suspend, },
	{ TI_kUND,  Key_s_undo, },
    };

    trie_T *t = trie_create();
    t = trie_set_null(t, (trievalue_T) { .keyseq = Key_c_at });
    for (size_t i = 0; i < sizeof charmap / sizeof *charmap; i++) {
	if (xiscntrl(charmap[i].c))
	    t = trie_set(t, (char []) { charmap[i].c, '\0', },
		    (trievalue_T) { .keyseq = charmap[i].keyseq });
    }
    for (size_t i = 0; i < sizeof keymap / sizeof *keymap; i++) {
	const char *seq = tigetstr(keymap[i].capability);
	if (is_strcap_valid(seq) && seq[0] != '\0')
	    t = trie_set(t, seq, (trievalue_T) { .keyseq = keymap[i].keyseq });
    }

    le_keycodes = t;
}

/* Tries to print the specified capability string to the print buffer.
 * Returns 1 iff successful. */
_Bool try_print_cap(const char *capname)
{
    char *v = tigetstr((char *) capname);
    if (is_strcap_valid(v) && v[0] != '\0')
	if (tputs(v, 1, lebuf_putchar) != ERR)
	    return 1;
    return 0;
}

/* Prints the "cr" code to the print buffer.
 * (carriage return: move cursor to first char of line) */
void lebuf_print_cr(void)
{
#if 0
    char *v = tigetstr(TI_cr);
    if (is_strcap_valid(v) && v[0] != '\0')
	tputs(v, 1, lebuf_putchar);
    else
#endif
	lebuf_putchar('\r');
    lebuf.pos.column = 0;
}

/* Prints the "nel" code to the print buffer.
 * (newline: move cursor to first char of next line) */
void lebuf_print_nel(void)
{
#if 0
    char *v = tigetstr(TI_nel);
    if (is_strcap_valid(v) && v[0] != '\0')
	tputs(v, 1, lebuf_putchar);
    else
#endif
	lebuf_putchar('\r'), lebuf_putchar('\n');
    lebuf.pos.line++, lebuf.pos.column = 0;
}

/* Moves the cursor.
 * `capone' must be one of "cub1", "cuf1", "cud1", "cuu1".
 * `capmul' must be one of "cub", "cuf", "cud", "cuu". */
void move_cursor(char *capone, char *capmul, long count, int affcnt)
{
    if (count > 0) {
	if (count == 1) {
	    if (!move_cursor_1(capone, 1))
		move_cursor_mul(capmul, 1, affcnt);
	} else {
	    if (!move_cursor_mul(capmul, count, affcnt))
		move_cursor_1(capone, count);
	}
    }
}

_Bool move_cursor_1(char *capone, long count)
{
    char *v = tigetstr(capone);
    if (is_strcap_valid(v) && v[0] != '\0') {
	do
	    tputs(v, 1, lebuf_putchar);
	while (--count > 0);
	return 1;
    } else {
	return 0;
    }
}

_Bool move_cursor_mul(char *capmul, long count, int affcnt)
{
    char *v = tigetstr(capmul);
    if (is_strcap_valid(v)) {
	v = tparm(v, count, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L);
	if (v != NULL) {
	    tputs(v, affcnt, lebuf_putchar);
	    return 1;
	}
    }
    return 0;
}

/* Prints the "cub"/"cub1" code to the print buffer.
 * (move cursor backward by `count' columns)
 * `count' must be small enough not to go beyond the screen bounds. */
void lebuf_print_cub(long count)
{
    move_cursor(TI_cub1, TI_cub, count, 1);
    lebuf.pos.column -= count;
    assert(lebuf.pos.column >= 0);
}

/* Prints the "cuf"/"cuf1" code to the print buffer.
 * (move cursor forward by `count' columns)
 * `count' must be small enough not to go beyond the screen bounds. */
void lebuf_print_cuf(long count)
{
    move_cursor(TI_cuf1, TI_cuf, count, 1);
    lebuf.pos.column += count;
    assert(lebuf.pos.column < lebuf.maxcolumn);
}

/* Prints the "cud"/"cud1" code to the print buffer.
 * (move cursor down by `count' lines)
 * `count' must be small enough not to go beyond screen bounds.
 * The cursor must be on the first column. */
void lebuf_print_cud(long count)
{
    assert(lebuf.pos.column == 0);
    move_cursor(TI_cud1, TI_cud, count, count + 1);
    lebuf.pos.line += count;
}

/* Prints the "cuu"/"cuu1" code to the print buffer.
 * (move cursor up by `count' lines)
 * `count' must be small enough not to go beyond screen bounds.
 * The cursor must be on the first column. */
void lebuf_print_cuu(long count)
{
    assert(lebuf.pos.column == 0);
    move_cursor(TI_cuu1, TI_cuu, count, count + 1);
    lebuf.pos.line -= count;
}

/* Prints the "el" code to the print buffer. (clear to end of line)
 * Returns true iff successful. */
_Bool lebuf_print_el(void)
{
    return try_print_cap(TI_el);
}

/* Prints the "ed" code to the print buffer if available.
 * (clear to end of screen)
 * Returns true iff successful. */
_Bool lebuf_print_ed(void)
{
    return try_print_cap(TI_ed);
}

/* Prints the "clear" code if available. (clear whole screen)
 * Returns true iff successful. */
_Bool lebuf_print_clear(void)
{
    if (try_print_cap(TI_clear)) {
	lebuf.pos.line = lebuf.pos.column = 0;
	return 1;
    } else {
	return 0;
    }
}

/* Prints the "op" code. (set color pairs to default)
 * Returns true iff successful. */
_Bool lebuf_print_op(void)
{
    return try_print_cap(TI_op);
}

/* Prints the "setf"/"setaf" code. */
void lebuf_print_setfg(long color)
{
    print_color_code(color, TI_setaf, TI_setf);
}

/* Prints the "setb"/"setab" code. */
void lebuf_print_setbg(long color)
{
    print_color_code(color, TI_setab, TI_setb);
}

void print_color_code(long color, char *seta, char *set)
{
    if (le_colors < 16)
	color &= 0x7L;
    if (le_colors <= color)
	return;

    char *v = tigetstr(seta);
    if (!is_strcap_valid(v) || v[0] == '\0') {
	v = tigetstr(set);
	color = ((color & 0x1L) << 2)
	      |  (color & 0x2L)
	      | ((color & 0x4L) >> 2)
	      |  (color & ~0x7L);
    }
    if (is_strcap_valid(v)) {
	v = tparm(v, color, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L);
	if (v != NULL)
	    tputs(v, 1, lebuf_putchar);
    }
}

/* Prints the "sgr0" code to the print buffer.
 * (reset the terminal font)
 * Returns true iff successful. */
_Bool lebuf_print_sgr0(void)
{
    return try_print_cap(TI_sgr0);
}

/* Prints the "smso" code to the print buffer. (start standout mode)
 * Returns true iff successful. */
_Bool lebuf_print_smso(void)
{
    if (try_print_cap(TI_smso)) {
	if (le_ti_xmc > 0)
	    lebuf_update_position(le_ti_xmc);
	return 1;
    } else {
	return 0;
    }
}

/* Prints the "smul" code to the print buffer. (start underline mode)
 * Returns true iff successful. */
_Bool lebuf_print_smul(void)
{
    return try_print_cap(TI_smul);
}

/* Prints the "rev" code to the print buffer. (start reverse mode)
 * Returns true iff successful. */
_Bool lebuf_print_rev(void)
{
    return try_print_cap(TI_rev);
}

/* Prints the "blink" code to the print buffer. (start blink mode)
 * Returns true iff successful. */
_Bool lebuf_print_blink(void)
{
    return try_print_cap(TI_blink);
}

/* Prints the "dim" code to the print buffer. (start dim mode)
 * Returns true iff successful. */
_Bool lebuf_print_dim(void)
{
    return try_print_cap(TI_dim);
}

/* Prints the "bold" code to the print buffer. (start bold mode)
 * Returns true iff successful. */
_Bool lebuf_print_bold(void)
{
    return try_print_cap(TI_bold);
}

/* Prints the "invis" code to the print buffer. (start invisible mode)
 * Returns true iff successful. */
_Bool lebuf_print_invis(void)
{
    return try_print_cap(TI_invis);
}

/* Prints the "flash" or "bel" code to alert the user.
 * If `direct_stderr' is true, the output is sent to the standard error;
 * otherwise, to the print buffer. */
void lebuf_print_alert(_Bool direct_stderr)
{
    int (*outfunc)(int) = direct_stderr ? putchar_stderr : lebuf_putchar;
    char *v = NULL;

    if (shopt_le_visiblebell)
	v = tigetstr(TI_flash);
    if (!is_strcap_valid(v) || v[0] == '\0')
	v = tigetstr(TI_bel);
    if (is_strcap_valid(v) && v[0] == '\0')
	tputs(v, 1, outfunc);
    else
	outfunc('\a');
}

/* Prints the "smkx" code to the standard error and sets the `transmit_mode'
 * flag. */
void print_smkx(void)
{
    char *v = tigetstr(TI_smkx);
    if (is_strcap_valid(v)) {
	tputs(v, 1, putchar_stderr);
	transmit_mode = 1;
    }
}

/* Prints the "rmkx" code to the standard error if the `transmit_mode' flag is
 * set. The flag is cleared in this function. */
void print_rmkx(void)
{
    if (transmit_mode) {
	char *v = tigetstr(TI_rmkx);
	if (is_strcap_valid(v))
	    tputs(v, 1, putchar_stderr);
	transmit_mode = 0;
    }
}

/* Like `putchar', but prints to `stderr'. */
int putchar_stderr(int c)
{
    return fputc(c, stderr);
}


/********** TERMIOS **********/

/* Special characters. */
int le_eof_char, le_kill_char, le_interrupt_char, le_erase_char;

static struct termios original_terminal_state;

static inline int normchar(cc_t c)
    __attribute__((const));
static void to_raw_mode(struct termios *term, _Bool isig)
    __attribute__((nonnull));
static inline int xtcgetattr(int fd, struct termios *term)
    __attribute__((nonnull));
static inline int xtcsetattr(int fd, int opt, const struct termios *term)
    __attribute__((nonnull));


/* Sets the terminal to the "raw" mode.
 * The current state is saved as `original_terminal_state'.
 * Returns true iff the terminal (the standard input) has been successfully
 * prepared. */
_Bool le_set_terminal(void)
{
    struct termios term;

    /* First we call `tcdrain' to get a clean state and to ensure the shell is
     * in the foreground. */
    tcdrain(STDIN_FILENO);

    /* get the original state */
    if (!le_save_terminal())
	return 0;
    term = original_terminal_state;
    le_eof_char       = normchar(term.c_cc[VEOF]);
    le_kill_char      = normchar(term.c_cc[VKILL]);
    le_interrupt_char = normchar(term.c_cc[VINTR]);
    le_erase_char     = normchar(term.c_cc[VERASE]);

    /* set attributes */
    to_raw_mode(&term, 0);
    if (xtcsetattr(STDIN_FILENO, TCSADRAIN, &term) != 0)
	goto fail;

    /* check if the attributes are properly set */
    if (xtcgetattr(STDIN_FILENO, &term) != 0)
	goto fail;
    if ((term.c_iflag & (INLCR | ICRNL))
	    || (term.c_lflag & (ECHO | ICANON))
	    || (term.c_cc[VTIME] != 0)
	    || (term.c_cc[VMIN] != 0))
	goto fail;

    // XXX it should be configurable whether we print smkx or not.
    print_smkx();

    return 1;

fail:
    xtcsetattr(STDIN_FILENO, TCSADRAIN, &original_terminal_state);
    return 0;
}

int normchar(cc_t c)
{
    if (c == _POSIX_VDISABLE)
	return -1;
    else
	return (unsigned char) c;
}

/* Saves the current terminal state in `original_terminal_state'.
 * This function flushes the standard error before saving the state.
 * Returns true iff the standard input is a terminal and the terminal state was
 * successfully saved. */
_Bool le_save_terminal(void)
{
    fflush(stderr);
    return xtcgetattr(STDIN_FILENO, &original_terminal_state) >= 0;
}

/* Restores the terminal to the original state saved in
 * `original_terminal_state'.
 * This function flushes the standard error before restoring the state.
 * Returns true iff the standard input is a terminal and the terminal state was
 * successfully restored. */
_Bool le_restore_terminal(void)
{
    print_rmkx();
    fflush(stderr);
    return xtcsetattr(STDIN_FILENO, TCSADRAIN, &original_terminal_state) >= 0;
}

/* Sets the ISIG flag of the terminal, which specifies whether, when the user
 * enters the INTR/QUIT/SUSP character, a corresponding signal is sent to the
 * foreground process(es).
 * If `allow' is true, a signal is sent. If `allow' is false, a signal is not
 * sent.
 * This function can be used only when the terminal is set to the "raw" mode
 * using the `le_set_terminal' function. */
_Bool le_allow_terminal_signal(_Bool allow)
{
    struct termios term = original_terminal_state;
    to_raw_mode(&term, allow);
    return xtcsetattr(STDIN_FILENO, TCSADRAIN, &term) == 0;
}

/* Modifies the specified termios structure into the "raw" mode values.
 * If `isig' is true, the ISIG flag is not cleared from `term->c_lflag'. */
void to_raw_mode(struct termios *term, _Bool isig)
{
    term->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | INLCR | IGNCR | ICRNL | IXON);
    term->c_iflag |= IGNPAR;
    term->c_lflag &= ~(ECHO | ICANON | IEXTEN) & (isig ? ~0 : ~ISIG);
    term->c_cc[VTIME] = 0;
    term->c_cc[VMIN] = 0;
}

/* Calls `tcgetattr'.
 * If it returns EINTR, re-calls it. */
int xtcgetattr(int fd, struct termios *term)
{
    int result;
    do
	result = tcgetattr(fd, term);
    while (result != 0 && errno == EINTR);
    return result;
}

/* Calls `tcsetattr'.
 * If it returns EINTR, re-calls it. */
int xtcsetattr(int fd, int opt, const struct termios *term)
{
    int result;
    do
	result = tcsetattr(fd, opt, term);
    while (result != 0 && errno == EINTR);
    return result;
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
