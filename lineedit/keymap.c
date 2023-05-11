/* Yash: yet another shell */
/* keymap.c: mappings from keys to functions */
/* (C) 2007-2023 magicant */

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
#include "keymap.h"
#include <assert.h>
#include <errno.h>
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "../builtin.h"
#include "../exec.h"
#include "../expand.h"
#include "../util.h"
#include "../xfnmatch.h"
#include "complete.h"
#include "editing.h"
#include "key.h"
#include "trie.h"


/* Definition of editing modes. */
le_mode_T le_modes[LE_MODE_N];

/* The current editing mode.
 * Points to one of the modes in `le_modes'. */
le_mode_T *le_current_mode;

/* Array of pairs of a command name and function.
 * Sorted by name. */
static const struct command_name_pair {
    const char *name;
    le_command_func_T *command;
} commands[] = {
#include "commands.in"
};


/* Initializes `le_modes' if not yet initialized.
 * May be called more than once but does nothing if so. */
void le_keymap_init(void)
{
    static bool initialized = false;
    if (initialized)
	return;
    initialized = true;

    trie_T *t;

#define Set(key, cmd) \
    (t = trie_setw(t, key, (trievalue_T) { .cmdfunc = (cmd) }))

    le_modes[LE_MODE_VI_INSERT].default_command = cmd_self_insert;
    t = trie_create();
    Set(Key_backslash, cmd_self_insert);
    Set(Key_c_v,       cmd_expect_verbatim);
    Set(Key_right,     cmd_forward_char);
    Set(Key_left,      cmd_backward_char);
    Set(Key_home,      cmd_beginning_of_line);
    Set(Key_end,       cmd_end_of_line);
    Set(Key_enter,     cmd_accept_line);
    Set(Key_c_j,       cmd_accept_line);
    Set(Key_c_m,       cmd_accept_line);
    Set(Key_interrupt, cmd_abort_line);
    Set(Key_c_c,       cmd_abort_line);
    Set(Key_eof,       cmd_eof_if_empty);
    Set(Key_c_d,       cmd_eof_if_empty);
    Set(Key_escape,    cmd_setmode_vicommand);
    Set(Key_c_l,       cmd_redraw_all);
    Set(Key_delete,    cmd_delete_char);
    Set(Key_backspace, cmd_backward_delete_char);
    Set(Key_erase,     cmd_backward_delete_char);
    Set(Key_c_h,       cmd_backward_delete_char);
    Set(Key_c_w,       cmd_backward_delete_semiword);
    Set(Key_kill,      cmd_backward_delete_line);
    Set(Key_c_u,       cmd_backward_delete_line);
    Set(Key_tab,       cmd_complete_next_candidate);
    Set(Key_btab,      cmd_complete_prev_candidate);
    Set(Key_down,      cmd_next_history_eol);
    Set(Key_c_n,       cmd_next_history_eol);
    Set(Key_up,        cmd_prev_history_eol);
    Set(Key_c_p,       cmd_prev_history_eol);
    le_modes[LE_MODE_VI_INSERT].keymap = t;

    le_modes[LE_MODE_VI_COMMAND].default_command = cmd_alert;
    t = trie_create();
    Set(Key_escape,    cmd_noop);
    Set(L"1",          cmd_digit_argument);
    Set(L"2",          cmd_digit_argument);
    Set(L"3",          cmd_digit_argument);
    Set(L"4",          cmd_digit_argument);
    Set(L"5",          cmd_digit_argument);
    Set(L"6",          cmd_digit_argument);
    Set(L"7",          cmd_digit_argument);
    Set(L"8",          cmd_digit_argument);
    Set(L"9",          cmd_digit_argument);
    Set(L"0",          cmd_bol_or_digit);
    Set(Key_enter,     cmd_accept_line);
    Set(Key_c_j,       cmd_accept_line);
    Set(Key_c_m,       cmd_accept_line);
    Set(Key_interrupt, cmd_abort_line);
    Set(Key_c_c,       cmd_abort_line);
    Set(Key_eof,       cmd_eof_if_empty);
    Set(Key_c_d,       cmd_eof_if_empty);
    Set(L"#",          cmd_accept_with_hash);
    Set(L"i",          cmd_setmode_viinsert);
    Set(Key_insert,    cmd_setmode_viinsert);
    Set(Key_c_l,       cmd_redraw_all);
    Set(L"l",          cmd_forward_char);
    Set(L" ",          cmd_forward_char);
    Set(Key_right,     cmd_forward_char);
    Set(L"h",          cmd_backward_char);
    Set(Key_left,      cmd_backward_char);
    Set(Key_backspace, cmd_backward_char);
    Set(Key_erase,     cmd_backward_char);
    Set(Key_c_h,       cmd_backward_char);
    Set(L"W",          cmd_forward_bigword);
    Set(L"E",          cmd_end_of_bigword);
    Set(L"B",          cmd_backward_bigword);
    Set(L"w",          cmd_forward_viword);
    Set(L"e",          cmd_end_of_viword);
    Set(L"b",          cmd_backward_viword);
    Set(Key_home,      cmd_beginning_of_line);
    Set(L"$",          cmd_end_of_line);
    Set(Key_end,       cmd_end_of_line);
    Set(L"^",          cmd_first_nonblank);
    Set(L"f",          cmd_find_char);
    Set(L"F",          cmd_find_char_rev);
    Set(L"t",          cmd_till_char);
    Set(L"T",          cmd_till_char_rev);
    Set(L";",          cmd_refind_char);
    Set(L",",          cmd_refind_char_rev);
    Set(L"x",          cmd_kill_char);
    Set(Key_delete,    cmd_kill_char);
    Set(L"X",          cmd_backward_kill_char);
    Set(L"P",          cmd_put_before);
    Set(L"p",          cmd_put);
    Set(L"u",          cmd_undo);
    Set(L"U",          cmd_undo_all);
    Set(Key_c_r,       cmd_cancel_undo);
    Set(L".",          cmd_redo);
    Set(L"|",          cmd_go_to_column);
    Set(L"r",          cmd_vi_replace_char);
    Set(L"I",          cmd_vi_insert_beginning);
    Set(L"a",          cmd_vi_append);
    Set(L"A",          cmd_vi_append_to_eol);
    Set(L"R",          cmd_vi_replace);
    Set(L"~",          cmd_vi_switch_case_char);
    Set(L"y",          cmd_vi_yank);
    Set(L"Y",          cmd_vi_yank_to_eol);
    Set(L"d",          cmd_vi_delete);
    Set(L"D",          cmd_vi_delete_to_eol);
    Set(L"c",          cmd_vi_change);
    Set(L"C",          cmd_vi_change_to_eol);
    Set(L"S",          cmd_vi_change_line);
    Set(L"s",          cmd_vi_substitute);
    Set(L"_",          cmd_vi_append_last_bigword);
    Set(L"@",          cmd_vi_exec_alias);
    Set(L"v",          cmd_vi_edit_and_accept);
    Set(L"=",          cmd_vi_complete_list);
    Set(L"*",          cmd_vi_complete_all);
    Set(Key_backslash, cmd_vi_complete_max);
    Set(L"?",          cmd_vi_search_forward);
    Set(L"/",          cmd_vi_search_backward);
    Set(L"n",          cmd_search_again);
    Set(L"N",          cmd_search_again_rev);
    Set(L"G",          cmd_oldest_history_bol);
    Set(L"g",          cmd_return_history_bol);
    Set(L"j",          cmd_next_history_bol);
    Set(L"+",          cmd_next_history_bol);
    Set(Key_down,      cmd_next_history_bol);
    Set(Key_c_n,       cmd_next_history_bol);
    Set(L"k",          cmd_prev_history_bol);
    Set(L"-",          cmd_prev_history_bol);
    Set(Key_up,        cmd_prev_history_bol);
    Set(Key_c_p,       cmd_prev_history_bol);
    le_modes[LE_MODE_VI_COMMAND].keymap = t;

    le_modes[LE_MODE_VI_SEARCH].default_command = cmd_srch_self_insert;
    t = trie_create();
    Set(Key_c_v,       cmd_expect_verbatim);
    Set(Key_interrupt, cmd_abort_line);
    Set(Key_c_c,       cmd_abort_line);
    Set(Key_c_l,       cmd_redraw_all);
    Set(Key_backslash, cmd_srch_self_insert);
    Set(Key_backspace, cmd_srch_backward_delete_char);
    Set(Key_erase,     cmd_srch_backward_delete_char);
    Set(Key_c_h,       cmd_srch_backward_delete_char);
    Set(Key_kill,      cmd_srch_backward_delete_line);
    Set(Key_c_u,       cmd_srch_backward_delete_line);
    Set(Key_enter,     cmd_srch_accept_search);
    Set(Key_c_j,       cmd_srch_accept_search);
    Set(Key_c_m,       cmd_srch_accept_search);
    Set(Key_escape,    cmd_srch_abort_search);
    le_modes[LE_MODE_VI_SEARCH].keymap = t;

    le_modes[LE_MODE_EMACS].default_command = cmd_self_insert;
    t = trie_create();
    Set(Key_backslash,      cmd_self_insert);
    Set(Key_escape Key_c_i, cmd_insert_tab);
    Set(Key_c_q,            cmd_expect_verbatim);
    Set(Key_c_v,            cmd_expect_verbatim);
    Set(Key_escape "0",     cmd_digit_argument);
    Set(Key_escape "1",     cmd_digit_argument);
    Set(Key_escape "2",     cmd_digit_argument);
    Set(Key_escape "3",     cmd_digit_argument);
    Set(Key_escape "4",     cmd_digit_argument);
    Set(Key_escape "5",     cmd_digit_argument);
    Set(Key_escape "6",     cmd_digit_argument);
    Set(Key_escape "7",     cmd_digit_argument);
    Set(Key_escape "8",     cmd_digit_argument);
    Set(Key_escape "9",     cmd_digit_argument);
    Set(Key_escape "-",     cmd_digit_argument);
    Set(Key_enter,          cmd_accept_line);
    Set(Key_c_j,            cmd_accept_line);
    Set(Key_c_m,            cmd_accept_line);
    Set(Key_interrupt,      cmd_abort_line);
    Set(Key_c_c,            cmd_abort_line);
    Set(Key_eof,            cmd_eof_or_delete);
    Set(Key_c_d,            cmd_eof_or_delete);
    Set(Key_escape "#",     cmd_accept_with_hash);
    Set(Key_c_l,            cmd_redraw_all);
    Set(Key_right,          cmd_forward_char);
    Set(Key_c_f,            cmd_forward_char);
    Set(Key_left,           cmd_backward_char);
    Set(Key_c_b,            cmd_backward_char);
    Set(Key_escape "f",     cmd_forward_emacsword);
    Set(Key_escape "F",     cmd_forward_emacsword);
    Set(Key_escape "b",     cmd_backward_emacsword);
    Set(Key_escape "B",     cmd_backward_emacsword);
    Set(Key_home,           cmd_beginning_of_line);
    Set(Key_c_a,            cmd_beginning_of_line);
    Set(Key_end,            cmd_end_of_line);
    Set(Key_c_e,            cmd_end_of_line);
    Set(Key_c_rb,           cmd_find_char);
    Set(Key_escape Key_c_rb, cmd_find_char_rev);
    Set(Key_delete,         cmd_delete_char);
    Set(Key_backspace,      cmd_backward_delete_char);
    Set(Key_erase,          cmd_backward_delete_char);
    Set(Key_c_h,            cmd_backward_delete_char);
    Set(Key_escape "d",     cmd_kill_emacsword);
    Set(Key_escape "D",     cmd_kill_emacsword);
    Set(Key_escape Key_backspace, cmd_backward_kill_emacsword);
    Set(Key_escape Key_erase, cmd_backward_kill_emacsword);
    Set(Key_escape Key_c_h, cmd_backward_kill_emacsword);
    Set(Key_c_k,            cmd_forward_kill_line);
    Set(Key_kill,           cmd_backward_kill_line);
    Set(Key_c_u,            cmd_backward_kill_line);
    Set(Key_c_x Key_backspace, cmd_backward_kill_line);
    Set(Key_c_x Key_erase,  cmd_backward_kill_line);
    Set(Key_c_y,            cmd_put_left);
    Set(Key_escape "y",     cmd_put_pop);
    Set(Key_escape "Y",     cmd_put_pop);
    Set(Key_c_w,            cmd_backward_kill_bigword);
    Set(Key_c_ul,           cmd_undo);
    Set(Key_c_x Key_c_u,    cmd_undo);
    Set(Key_c_x Key_kill,   cmd_undo);
    Set(Key_escape Key_c_r, cmd_undo_all);
    Set(Key_escape "r",     cmd_undo_all);
    Set(Key_escape "R",     cmd_undo_all);
    Set(Key_tab,            cmd_complete_next_candidate);
    Set(Key_btab,           cmd_complete_prev_candidate);
    Set(Key_escape "=",     cmd_complete_list);
    Set(Key_escape "?",     cmd_complete_list);
    Set(Key_escape "*",     cmd_complete_all);
    Set(Key_c_t,            cmd_emacs_transpose_chars);
    Set(Key_escape "t",     cmd_emacs_transpose_words);
    Set(Key_escape "T",     cmd_emacs_transpose_words);
    Set(Key_escape "l",     cmd_emacs_downcase_word);
    Set(Key_escape "L",     cmd_emacs_downcase_word);
    Set(Key_escape "u",     cmd_emacs_upcase_word);
    Set(Key_escape "U",     cmd_emacs_upcase_word);
    Set(Key_escape "c",     cmd_emacs_capitalize_word);
    Set(Key_escape "C",     cmd_emacs_capitalize_word);
    Set(Key_escape Key_backslash, cmd_emacs_delete_horizontal_space);
    Set(Key_escape " ",     cmd_emacs_just_one_space);
    Set(Key_c_s,            cmd_emacs_search_forward);
    Set(Key_c_r,            cmd_emacs_search_backward);
    Set(Key_escape "<",     cmd_oldest_history_eol);
    Set(Key_escape ">",     cmd_return_history_eol);
    Set(Key_down,           cmd_next_history_eol);
    Set(Key_c_n,            cmd_next_history_eol);
    Set(Key_up,             cmd_prev_history_eol);
    Set(Key_c_p,            cmd_prev_history_eol);
    le_modes[LE_MODE_EMACS].keymap = t;

    le_modes[LE_MODE_EMACS_SEARCH].default_command = cmd_srch_self_insert;
    t = trie_create();
    Set(Key_c_v,       cmd_expect_verbatim);
    Set(Key_enter,     cmd_accept_line);
    Set(Key_c_m,       cmd_accept_line);
    Set(Key_interrupt, cmd_abort_line);
    Set(Key_c_c,       cmd_abort_line);
    Set(Key_c_l,       cmd_redraw_all);
    Set(Key_backslash, cmd_srch_self_insert);
    Set(Key_backspace, cmd_srch_backward_delete_char);
    Set(Key_erase,     cmd_srch_backward_delete_char);
    Set(Key_c_h,       cmd_srch_backward_delete_char);
    Set(Key_kill,      cmd_srch_backward_delete_line);
    Set(Key_c_u,       cmd_srch_backward_delete_line);
    Set(Key_c_s,       cmd_srch_continue_forward);
    Set(Key_c_r,       cmd_srch_continue_backward);
    Set(Key_c_j,       cmd_srch_accept_search);
    Set(Key_escape,    cmd_srch_accept_search);
    Set(Key_c_g,       cmd_srch_abort_search);
    le_modes[LE_MODE_EMACS_SEARCH].keymap = t;

    le_modes[LE_MODE_CHAR_EXPECT].default_command = cmd_expect_char;
    t = trie_create();
    Set(Key_c_v,       cmd_expect_verbatim);
    Set(Key_interrupt, cmd_abort_line);
    Set(Key_c_c,       cmd_abort_line);
    Set(Key_backslash, cmd_expect_char);
    Set(Key_escape,    cmd_abort_expect_char);
    le_modes[LE_MODE_CHAR_EXPECT].keymap = t;

#undef Set
}

/* Sets the editing mode to the one specified by `id'. */
void le_set_mode(le_mode_id_T id)
{
    assert(id < LE_MODE_N);
    le_current_mode = le_id_to_mode(id);
}

/* Generates completion candidates for editing command names matching the
 * pattern. */
/* The prototype of this function is declared in "complete.h". */
void generate_bindkey_candidates(const le_compopt_T *compopt)
{
    if (!(compopt->type & CGT_BINDKEY))
	return;

    le_compdebug("adding lineedit command name candidates");
    if (!le_compile_cpatterns(compopt))
	return;

    for (size_t i = 0; i < sizeof commands / sizeof *commands; i++)
	if (le_match_comppatterns(compopt, commands[i].name))
	    le_new_candidate(CT_BINDKEY,
		    malloc_mbstowcs(commands[i].name), NULL, compopt);
}


/********** Built-in **********/

static int print_all_commands(void);
static int set_key_binding(
	le_mode_id_T mode, const wchar_t *keyseq, const wchar_t *commandname)
    __attribute__((nonnull));
static le_command_func_T *get_command_from_name(const char *name)
    __attribute__((nonnull,pure));
static int command_name_compare(const void *p1, const void *p2)
    __attribute__((nonnull,pure));
static int print_binding(le_mode_id_T mode, const wchar_t *keyseq)
    __attribute__((nonnull));
static int print_binding_main(
	void *mode, const wchar_t *keyseq, le_command_func_T *cmd)
    __attribute__((nonnull));
static const char *get_command_name(le_command_func_T *command)
    __attribute__((nonnull,const));

/* Options for the "bindkey" built-in. */
const struct xgetopt_T bindkey_options[] = {
    { L'v', L"vi-insert",  OPTARG_NONE, false, NULL, },
    { L'a', L"vi-command", OPTARG_NONE, false, NULL, },
    { L'e', L"emacs",      OPTARG_NONE, false, NULL, },
    { L'l', L"list",       OPTARG_NONE, false, NULL, },
#if YASH_ENABLE_HELP
    { L'-', L"help",       OPTARG_NONE, false, NULL, },
#endif
    { L'\0', NULL, 0, false, NULL, },
};

/* The "bindkey" built-in, which accepts the following options:
 *  -v: select the "vi-insert" mode
 *  -a: select the "vi-command" mode
 *  -e: select the "emacs" mode
 *  -l: list names of available commands */
int bindkey_builtin(int argc, void **argv)
{
    bool list = false;
    le_mode_id_T mode = LE_MODE_N;

    const struct xgetopt_T *opt;
    xoptind = 0;
    while ((opt = xgetopt(argv, bindkey_options, 0)) != NULL) {
	switch (opt->shortopt) {
	    case L'a':  mode = LE_MODE_VI_COMMAND;  break;
	    case L'e':  mode = LE_MODE_EMACS;       break;
	    case L'v':  mode = LE_MODE_VI_INSERT;   break;
	    case L'l':  list = true;                break;
#if YASH_ENABLE_HELP
	    case L'-':
		return print_builtin_help(ARGV(0));
#endif
	    default:
		return Exit_ERROR;
	}
    }

    le_keymap_init();

    if (list) {
	if (!validate_operand_count(argc - xoptind, 0, 0))
	    return Exit_ERROR;
	if (mode != LE_MODE_N) {
	    xerror(0, Ngt("option combination is invalid"));
	    return Exit_ERROR;
	}
	return print_all_commands();
    }

    if (mode == LE_MODE_N) {
	xerror(0, Ngt("no option is specified"));
	return Exit_ERROR;
    }

    switch (argc - xoptind) {
	case 0:;
	    /* print all key bindings */
	    le_mode_T *m = le_id_to_mode(mode);
	    return trie_foreachw(m->keymap, print_binding_main, m);
	case 1:
	    return print_binding(mode, ARGV(xoptind));
	case 2:
	    return set_key_binding(mode, ARGV(xoptind), ARGV(xoptind + 1));
	default:
	    return too_many_operands_error(2);
    }
}

/* Prints all available commands to the standard output. */
int print_all_commands(void)
{
    for (size_t i = 0; i < sizeof commands / sizeof *commands; i++)
	if (!xprintf("%s\n", commands[i].name))
	    return Exit_FAILURE;
    return Exit_SUCCESS;
}

/* Binds the specified key sequence to the specified command.
 * If `commandname' is L"-", the binding is removed.
 * If the specified command is not found, it is an error. */
int set_key_binding(
	le_mode_id_T mode, const wchar_t *keyseq, const wchar_t *commandname)
{
    if (keyseq[0] == L'\0') {
	xerror(0, Ngt("cannot bind an empty key sequence"));
	return Exit_FAILURE;
    }

    if (wcscmp(commandname, L"-") == 0) {
	/* delete key binding */
	register trie_T *t = le_modes[mode].keymap;
	t = trie_removew(t, keyseq);
	le_modes[mode].keymap = t;
    } else {
	/* set key binding */
	char *mbsname = malloc_wcstombs(commandname);
	if (mbsname == NULL) {
	    xerror(EILSEQ, Ngt("unexpected error"));
	    return Exit_FAILURE;
	}

	le_command_func_T *cmd = get_command_from_name(mbsname);
	free(mbsname);
	if (cmd) {
	    register trie_T *t = le_modes[mode].keymap;
	    t = trie_setw(t, keyseq, (trievalue_T) { .cmdfunc = cmd });
	    le_modes[mode].keymap = t;
	} else {
	    xerror(0, Ngt("no such editing command `%ls'"), commandname);
	    return Exit_FAILURE;
	}
    }
    return Exit_SUCCESS;
}

/* Returns the command function of the specified name.
 * Returns a null pointer if no such command is found. */
le_command_func_T *get_command_from_name(const char *name)
{
    struct command_name_pair *cnp = bsearch(name, commands,
	    sizeof commands / sizeof *commands,
	    sizeof *commands,
	    command_name_compare);

    return (cnp != NULL) ? cnp->command : 0;
}

int command_name_compare(const void *p1, const void *p2)
{
    return strcmp((const char *) p1,
	    ((const struct command_name_pair *) p2)->name);
}

/* Prints the binding for the given key sequence. */
int print_binding(le_mode_id_T mode, const wchar_t *keyseq)
{
    trieget_T tg = trie_getw(le_modes[mode].keymap, keyseq);

    if ((tg.type & TG_EXACTMATCH) && tg.matchlength == wcslen(keyseq)) {
	return print_binding_main(
		le_id_to_mode(mode), keyseq, tg.value.cmdfunc);
    } else {
	xerror(0, Ngt("key sequence `%ls' is not bound"), keyseq);
	return Exit_FAILURE;
    }
}

/* Prints a command to restore the specified key binding.
 * `mode' must be a pointer to one of the modes in `le_modes'. */
int print_binding_main(
	void *mode, const wchar_t *keyseq, le_command_func_T *cmd)
{
    const char *format;
    char modechar;
    wchar_t *keyseqquote;
    const char *commandname;

    switch (le_mode_to_id(mode)) {
	case LE_MODE_VI_INSERT:     modechar = 'v';  break;
	case LE_MODE_VI_COMMAND:    modechar = 'a';  break;
	case LE_MODE_VI_SEARCH:     modechar = 'V';  break;
	case LE_MODE_EMACS:         modechar = 'e';  break;
	case LE_MODE_EMACS_SEARCH:  modechar = 'E';  break;
	case LE_MODE_CHAR_EXPECT:   modechar = 'c';  break;
	default:                    assert(false);
    }
    if (keyseq[0] == L'-')
	format = "bindkey -%c -- %ls %s\n";
    else
	format = "bindkey -%c %ls %s\n";
    keyseqquote = quote_as_word(keyseq);
    commandname = get_command_name(cmd);
    xprintf(format, modechar, keyseqquote, commandname);
    free(keyseqquote);
    return (yash_error_message_count == 0) ? Exit_SUCCESS : Exit_FAILURE;
}

/* Returns the name of the specified command. */
const char *get_command_name(le_command_func_T *command)
{
    for (size_t i = 0; i < sizeof commands / sizeof *commands; i++)
	if (commands[i].command == command)
	    return commands[i].name;
    return NULL;
}

#if YASH_ENABLE_HELP
const char bindkey_help[] = Ngt(
"set or print key bindings for line-editing"
);
const char bindkey_syntax[] = Ngt(
"\tbindkey -aev [key_sequence [command]]\n"
"\tbindkey -l\n"
);
#endif


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
