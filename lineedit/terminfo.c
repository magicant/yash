/* Yash: yet another shell */
/* terminfo.c: interface to terminfo and termios */
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


#define _XOPEN_SOURCE_EXTENDED 1
#include "../common.h"
#include <assert.h>
#include <ctype.h>
#include <curses.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <term.h>
#include <termios.h>
#include <unistd.h>
#include "../util.h"
#include "key.h"
#include "terminfo.h"
#include "trie.h"


/********** TERMINFO **********/


/* Number of lines and columns in the current terminal. */
/* Initialized by `yle_setupterm'. */
int yle_lines, yle_columns;

/* True if the meta key inputs character whose 8th bit is set. */
bool yle_meta_bit8;

/* Strings sent by terminal when special key is pressed.
 * Values of entries are `keyseq'. */
trie_T *yle_keycodes = NULL;


static void set_up_keycodes(void);
static int putchar_stderr(int c);


/* Calls `setupterm' and checks if terminfo data is available.
 * Returns true iff successful. */
_Bool yle_setupterm(void)
{
    int err;

    if (setupterm(NULL, STDERR_FILENO, &err) != OK)
	return 0;

    // XXX should we do some more checks?

    yle_lines = tigetnum(TI_lines);
    yle_columns = tigetnum(TI_cols);
    yle_meta_bit8 = tigetflag(TI_km) > 0;
    if (yle_lines <= 0 || yle_columns <= 0)
	return 0;

    set_up_keycodes();

    return 1;
}

/* Initialized `yle_keycodes'. */
void set_up_keycodes(void)
{
    trie_destroy(yle_keycodes);

    trie_T *t = trie_create();
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

    t = trie_set_null(t, (trievalue_T) { .keyseq = Key_c_at });
    for (size_t i = 0; i < sizeof charmap / sizeof *charmap; i++) {
	if (xiscntrl(charmap[i].c))
	    t = trie_set(t, (char []) { charmap[i].c, '\0', },
		    (trievalue_T) { .keyseq = charmap[i].keyseq });
    }
    for (size_t i = 0; i < sizeof keymap / sizeof *keymap; i++) {
	const char *seq = tigetstr(keymap[i].capability);
	if (seq != NULL && seq != (char *) -1)
	    t = trie_set(t, seq, (trievalue_T) { .keyseq = keymap[i].keyseq });
    }

    yle_keycodes = t;
}

/* Prints "sgr" variable. */
void yle_print_sgr(long standout, long underline, long reverse, long blink,
	long dim, long bold, long invisible)
{
    char *v = tigetstr(TI_sgr);
    if (v) {
	v = tparm(v, standout, underline, reverse, blink, dim, bold, invisible,
		0, 0);
	if (v)
	    tputs(v, 1, putchar_stderr);
    }
}

/* Prints "op" variable. */
void yle_print_op(void)
{
    char *v = tigetstr(TI_op);
    if (v)
	tputs(v, 1, putchar_stderr);
}

/* Prints "setf"/"setaf" variable. */
void yle_print_setfg(int color)
{
    char *v = tigetstr(TI_setaf);
    if (!v)
	v = tigetstr(TI_setf);
    if (v) {
	v = tparm(v, color);
	if (v)
	    tputs(v, 1, putchar_stderr);
    }
}

/* Prints "setb"/"setab" variable. */
void yle_print_setbg(int color)
{
    char *v = tigetstr(TI_setab);
    if (!v)
	v = tigetstr(TI_setb);
    if (v) {
	v = tparm(v, color);
	if (v)
	    tputs(v, 1, putchar_stderr);
    }
}

/* Like `putchar', but prints to `stderr'. */
int putchar_stderr(int c)
{
    return fputc(c, stderr);
}

/* Alerts the user by flash or bell, without moving the cursor. */
void yle_alert(void)
{
    putchar_stderr('\a');
}


/********** TERMIOS **********/

/* Special characters. */
char yle_eof_char, yle_kill_char, yle_interrupt_char, yle_erase_char;

static struct termios original_terminal_state;

/* Sets the terminal to the "raw" mode.
 * The current state is saved as `original_terminal_state'.
 * `stdin' must be the terminal.
 * Returns true iff successful. */
_Bool yle_set_terminal(void)
{
    struct termios term;

    if (tcgetattr(STDIN_FILENO, &term) != 0)
	return 0;
    original_terminal_state = term;
    yle_eof_char       = TO_CHAR(term.c_cc[VEOF]);
    yle_kill_char      = TO_CHAR(term.c_cc[VKILL]);
    yle_interrupt_char = TO_CHAR(term.c_cc[VINTR]);
    yle_erase_char     = TO_CHAR(term.c_cc[VERASE]);

    /* set attributes */
    term.c_iflag &= ~(IGNBRK | ISTRIP | INLCR | IGNCR | ICRNL);
    term.c_iflag |= BRKINT;
    term.c_oflag &= ~OPOST;
    term.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN);
    term.c_lflag |= ISIG;
    term.c_cflag &= ~CSIZE;
    term.c_cflag |= CS8;
    term.c_cc[VTIME] = 0;
    term.c_cc[VMIN] = 0;
    if (tcsetattr(STDIN_FILENO, TCSADRAIN, &term) != 0)
	goto fail;

    /* check if the attributes are properly set */
    if (tcgetattr(STDIN_FILENO, &term) != 0)
	goto fail;
    if ((term.c_iflag & (IGNBRK | ISTRIP | INLCR | IGNCR | ICRNL))
	    || !(term.c_iflag & BRKINT)
	    ||  (term.c_oflag & OPOST)
	    ||  (term.c_lflag & (ECHO | ECHONL | ICANON | IEXTEN))
	    || !(term.c_lflag & ISIG)
	    ||  ((term.c_cflag & CSIZE) != CS8)
	    ||  (term.c_cc[VTIME] != 0)
	    ||  (term.c_cc[VMIN] != 0))
	goto fail;

    return 1;

fail:
    tcsetattr(STDIN_FILENO, TCSADRAIN, &original_terminal_state);
    return 0;
}

/* Restores the terminal to the original state.
 * `stdin' must be the terminal.
 * Returns true iff successful. */
_Bool yle_restore_terminal(void)
{
    return tcsetattr(STDIN_FILENO, TCSADRAIN, &original_terminal_state) == 0;
}


/* vim: set ts=8 sts=4 sw=4 noet: */
