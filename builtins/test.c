/* Yash: yet another shell */
/* test.c: test builtin */
/* (C) 2007-2020 magicant */

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
#include "test.h"
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#if YASH_ENABLE_DOUBLE_BRACKET
# include "../expand.h"
#endif
#include "../option.h"
#if YASH_ENABLE_DOUBLE_BRACKET
# include "../parser.h"
#endif
#include "../path.h"
#include "../plist.h"
#include "../strbuf.h"
#include "../util.h"
#include "../xfnmatch.h"


#define Exit_TRUE      0  /* Exit_SUCCESS */
#define Exit_FALSE     1  /* Exit_FAILURE */
#define Exit_TESTERROR 2  /* Exit_ERROR */

struct test_state {
    void **args;
    int argc;
    int index;
};
enum filecmp {
    FC_ID, FC_SAME, FC_NEWER, FC_OLDER, FC_UNKNOWN,
};

static inline bool test_single(void *args[static 1]);
static bool test_double(void *args[static 2]);
static bool test_file(wchar_t type, const char *file)
    __attribute__((nonnull));
static bool test_triple(void *args[static 3]);
static bool test_long_or(struct test_state *state)
    __attribute__((nonnull));
static bool test_long_and(struct test_state *state)
    __attribute__((nonnull));
static bool test_long_term(struct test_state *state)
    __attribute__((nonnull));
static bool is_term_delimiter(const wchar_t *word)
    __attribute__((nonnull,pure));
static int compare_integers(const wchar_t *left, const wchar_t *right)
    __attribute__((nonnull,pure));
static int compare_versions(const wchar_t *left, const wchar_t *right)
    __attribute__((nonnull));
static enum filecmp compare_files(const wchar_t *left, const wchar_t *right)
    __attribute__((nonnull));

#if YASH_ENABLE_DOUBLE_BRACKET
static int eval_dbexp(const dbexp_T *e)
    __attribute__((nonnull));
static inline cc_word_T expand_double_bracket_operand(const wordunit_T *w)
    __attribute__((nonnull,warn_unused_result));
static inline wchar_t *expand_double_bracket_operand_unescaped(
	const wordunit_T *w)
    __attribute__((nonnull,malloc,warn_unused_result));
static bool test_triple_db(const wchar_t *lhs, const wchar_t *op,
	const wchar_t *rhsvalue, const char *rhscc)
    __attribute__((nonnull));
static bool quote_removal_and_test_triple(
	const wchar_t *lhs, const wchar_t *op,
	const wchar_t *rhsvalue, const char *rhscc)
    __attribute__((nonnull));
static bool quote_removal_and_pattern_matching(
	const wchar_t *lhs, const wchar_t *rhsvalue, const char *rhscc)
    __attribute__((nonnull));
static bool quote_removal_and_regex_matching(
	const wchar_t *lhs, const wchar_t *rhsvalue, const char *rhscc)
    __attribute__((nonnull));
static wchar_t *quote_removal_for_regex(const wchar_t *s, const char *cc)
    __attribute__((nonnull,malloc,warn_unused_result));
static const wchar_t *skip_bracket(const wchar_t *s)
    __attribute__((nonnull,pure,warn_unused_result));
#endif


/* The "test" ("[") built-in. */
int test_builtin(int argc, void **argv)
{
    if (wcscmp(ARGV(0), L"[") == 0) {
	argc--;
	if (wcscmp(ARGV(argc), L"]") != 0) {
	    xerror(0, Ngt("`%ls' is missing"), L"]");
	    return Exit_TESTERROR;
	}
    }
    assert(argc > 0);
    argc--, argv++;

    struct test_state state;
    bool result;

    switch (argc) {
	case 0:  result = false;                 break;
	case 1:  result = test_single(argv);     break;
	case 2:  result = test_double(argv);     break;
	case 3:  result = test_triple(argv);     break;
	case 4:
	    if (wcscmp(argv[0], L"!") == 0) {
		result = !test_triple(&argv[1]);
		break;
	    }
	    if (wcscmp(argv[0], L"(") == 0 && wcscmp(argv[3], L")") == 0) {
		result = test_double(&argv[1]);
		break;
	    }
	    /* falls thru! */
	default:
	    state.args = argv;
	    state.argc = argc;
	    state.index = 0;
	    result = test_long_or(&state);
	    if (yash_error_message_count == 0 && state.index < state.argc)
		xerror(0, Ngt("`%ls' is not a valid operator"),
			(const wchar_t *) state.args[state.index]);
	    break;
    }

    if (yash_error_message_count > 0)
	return Exit_TESTERROR;
    return result ? Exit_TRUE : Exit_FALSE;
}

/* Tests the specified one-token expression. */
bool test_single(void *args[static 1])
{
    const wchar_t *arg0 = args[0];
    return arg0[0] != L'\0';
}

/* Tests the specified two-token expression. */
bool test_double(void *args[static 2])
{
    const wchar_t *op = args[0], *arg = args[1];

    if (wcscmp(op, L"!") == 0)
	return !test_single(&args[1]);
    if (!is_unary_primary(op)) {
	xerror(0, Ngt("`%ls' is not a unary operator"), op);
	return 0;
    }

    switch (op[1]) {
	case L'n':  return arg[0] != L'\0';
	case L'z':  return arg[0] == L'\0';
	case L't':
	    {
		int fd;
		return xwcstoi(arg, 10, &fd) && isatty(fd);
	    }
	case L'o':
	    if (arg[0] == L'?')
		return is_valid_option_name(&arg[1]);
	    else
		return option_is_enabled(arg);
    }

    char *mbsarg = malloc_wcstombs(arg);
    if (mbsarg == NULL) {
	xerror(EILSEQ, Ngt("unexpected error"));
	return 0;
    }

    bool result = test_file(op[1], mbsarg);
    free(mbsarg);
    return result;
}

/* An auxiliary function for file type checking. */
bool test_file(wchar_t type, const char *file) {
    switch (type) {
	case L'd':  return is_directory(file);
	case L'e':  return is_file(file);
	case L'f':  return is_regular_file(file);
	case L'r':  return is_readable(file);
	case L'w':  return is_writable(file);
	case L'x':  return is_executable(file);
    }

    struct stat st;
    switch (type) {
	case L'h':
	case L'L':
	    return (lstat(file, &st) == 0) && S_ISLNK(st.st_mode);
#if !HAVE_S_ISVTX
	case L'k':
	    return false;
#endif
    }

    if (stat(file, &st) < 0)
	return false;
    switch (type) {
	case L'b':
	    return S_ISBLK(st.st_mode);
	case L'c':
	    return S_ISCHR(st.st_mode);
	case L'G':
	    return st.st_gid == getegid();
	case L'g':
	    return st.st_mode & S_ISGID;
#if HAVE_S_ISVTX
	case L'k':
	    return st.st_mode & S_ISVTX;
#endif
	case L'N':
	    return st.st_atime < st.st_mtime
#if HAVE_ST_ATIM && HAVE_ST_MTIM
		|| (st.st_atime == st.st_mtime
			&& st.st_atim.tv_nsec < st.st_mtim.tv_nsec)
#elif HAVE_ST_ATIMESPEC && HAVE_ST_MTIMESPEC
		|| (st.st_atime == st.st_mtime
			&& st.st_atimespec.tv_nsec < st.st_mtimespec.tv_nsec)
#elif HAVE_ST_ATIMENSEC && HAVE_ST_MTIMENSEC
		|| (st.st_atime == st.st_mtime
			&& st.st_atimensec < st.st_mtimensec)
#elif HAVE___ST_ATIMENSEC && HAVE___ST_MTIMENSEC
		|| (st.st_atime == st.st_mtime
			&& st.__st_atimensec < st.__st_mtimensec)
#endif
		;
	case L'O':
	    return st.st_uid == geteuid();
	case L'p':
	    return S_ISFIFO(st.st_mode);
	case L'S':
	    return S_ISSOCK(st.st_mode);
	case L's':
	    return st.st_size > 0;
	case L'u':
	    return st.st_mode & S_ISUID;
    }

    assert(false);
}

/* Tests the specified three-token expression. */
bool test_triple(void *args[static 3])
{
    const wchar_t *left = args[0], *op = args[1], *right = args[2];

    switch (op[0]) {
	case L'=':
	    if (op[1] == L'\0' || (op[1] == L'=' && op[2] == L'\0'))
		return wcscmp(left, right) == 0;
	    if (op[1] == L'=' && op[2] == L'=' && op[3] == L'\0')
		return wcscoll(left, right) == 0;
	    if (op[1] == L'~' && op[2] == L'\0')
		return match_regex(left, right);
	    goto not_binary;
	case L'!':
	    if (op[1] == L'=' && op[2] == L'\0')
		return wcscmp(left, right) != 0;
	    if (op[1] == L'=' && op[2] == L'=' && op[3] == L'\0')
		return wcscoll(left, right) != 0;
	    goto not_binary;
	case L'<':
	    if (op[1] == L'\0')
		return wcscoll(left, right) < 0;
	    if (op[1] == L'=' && op[2] == L'\0')
		return wcscoll(left, right) <= 0;
	    goto not_binary;
	case L'>':
	    if (op[1] == L'\0')
		return wcscoll(left, right) > 0;
	    if (op[1] == L'=' && op[2] == L'\0')
		return wcscoll(left, right) >= 0;
	    goto not_binary;
	case L'-':
	    break;
	default:
	    goto not_binary;
    }

    assert(op[0] == L'-');
    switch (op[1]) {
    case L'a':
	if (op[2] == L'\0') return test_single(args) && test_single(&args[2]);
	break;
    case L'o':
	if (op[2] == L'\0') return test_single(args) || test_single(&args[2]);
	if (op[2] == L't')
	    if (op[3] == L'\0') return compare_files(left, right) == FC_OLDER;
	break;
    case L'e':
	switch (op[2]) {
	case L'f':
	    if (op[3] == L'\0') return compare_files(left, right) == FC_ID;
	    break;
	case L'q':
	    if (op[3] == L'\0') return compare_integers(left, right) == 0;
	    break;
	}
	break;
    case L'n':
	switch (op[2]) {
	case L'e':
	    if (op[3] == L'\0') return compare_integers(left, right) != 0;
	    break;
	case L't':
	    if (op[3] == L'\0') return compare_files(left, right) == FC_NEWER;
	    break;
	}
	break;
    case L'g':
	switch (op[2]) {
	case L't':
	    if (op[3] == L'\0') return compare_integers(left, right) > 0;
	    break;
	case L'e':
	    if (op[3] == L'\0') return compare_integers(left, right) >= 0;
	    break;
	}
	break;
    case L'l':
	switch (op[2]) {
	case L't':
	    if (op[3] == L'\0') return compare_integers(left, right) < 0;
	    break;
	case L'e':
	    if (op[3] == L'\0') return compare_integers(left, right) <= 0;
	    break;
	}
	break;
    case L'v':
	switch (op[2]) {
	case L'e':
	    if (op[3] == L'q' && op[4] == L'\0')
		return compare_versions(left, right) == 0;
	    break;
	case L'n':
	    if (op[3] == L'e' && op[4] == L'\0')
		return compare_versions(left, right) != 0;
	    break;
	case L'g':
	    switch (op[3]) {
	    case L't':
		if (op[4] == L'\0') return compare_versions(left, right) > 0;
		break;
	    case L'e':
		if (op[4] == L'\0') return compare_versions(left, right) >= 0;
		break;
	    }
	    break;
	case L'l':
	    switch (op[3]) {
	    case L't':
		if (op[4] == L'\0') return compare_versions(left, right) < 0;
		break;
	    case L'e':
		if (op[4] == L'\0') return compare_versions(left, right) <= 0;
		break;
	    }
	    break;
	}
	break;
    }

not_binary:
    if (wcscmp(left, L"!") == 0)
	return !test_double(&args[1]);
    if (wcscmp(left, L"(") == 0 && wcscmp(right, L")") == 0)
	return test_single(&args[1]);

    xerror(0, Ngt("`%ls' is not a binary operator"), op);
    return 0;
}

/* exp := exp "-o" and | and
 * and := and "-a" term | term
 * term := "(" exp ")" | "!" "(" exp ")" | single | double | triple
 */

/* Tests the specified long expression using `state'. */
bool test_long_or(struct test_state *state)
{
    bool result;

    result = test_long_and(state);
    while (yash_error_message_count == 0
	    && state->index < state->argc
	    && wcscmp(state->args[state->index], L"-o") == 0) {
	state->index++;
	result |= test_long_and(state);
    }
    return result;
}

/* Tests the specified long expression using `state'. */
bool test_long_and(struct test_state *state)
{
    bool result;

    result = test_long_term(state);
    while (yash_error_message_count == 0
	    && state->index < state->argc
	    && wcscmp(state->args[state->index], L"-a") == 0) {
	state->index++;
	result &= test_long_term(state);
    }
    return result;
}

/* Tests the specified long expression using `state'. */
bool test_long_term(struct test_state *state)
{
    bool result;
    bool negate = false;

    if (state->index < state->argc
	    && wcscmp(state->args[state->index], L"!") == 0) {
	state->index++;
	negate = true;
    }
    if (state->index >= state->argc) {
	assert(state->argc > 0);
	xerror(0, Ngt("an expression is missing after `%ls'"),
		(const wchar_t *) state->args[state->index - 1]);
	return 0;
    }
    if (wcscmp(state->args[state->index], L"(") == 0) {
	state->index++;
	result = test_long_or(state);
	if (state->index >= state->argc
		|| wcscmp(state->args[state->index], L")") != 0) {
	    xerror(0, Ngt("`%ls' is missing"), L")");
	    return 0;
	}
	state->index++;
    } else if (state->index + 3 <= state->argc
	    && is_binary_primary(state->args[state->index + 1])
	    && (state->index + 3 >= state->argc
		|| is_term_delimiter(state->args[state->index + 3]))) {
	result = test_triple(&state->args[state->index]);
	state->index += 3;
    } else if (state->index + 2 <= state->argc
	    && is_unary_primary(state->args[state->index])
	    && (state->index + 2 >= state->argc
		|| is_term_delimiter(state->args[state->index + 2]))) {
	result = test_double(&state->args[state->index]);
	state->index += 2;
    } else {
	result = test_single(&state->args[state->index]);
	state->index += 1;
    }
    return result ^ negate;
}

/* Checks if `word' is a unary primary operator. */
/* Note that "!" is not a primary operator. */
bool is_unary_primary(const wchar_t *word)
{
    if (word[0] != L'-' || word[1] == L'\0' || word[2] != L'\0')
	return false;
    switch (word[1]) {
	case L'b':  case L'c':  case L'd':  case L'e':  case L'f':  case L'G':
	case L'g':  case L'h':  case L'k':  case L'L':  case L'N':  case L'n':
	case L'O':  case L'o':  case L'p':  case L'r':  case L'S':  case L's':
	case L't':  case L'u':  case L'w':  case L'x':  case L'z':
	    return true;
	default:
	    return false;
    }
}

/* Checks if `word' is a binary primary operator.
 * This function returns false for "-a" and "-o". */
bool is_binary_primary(const wchar_t *word)
{
    switch (word[0]) {
	case L'=':
	    if (word[1] == L'\0' || (word[1] == L'~' && word[2] == L'\0'))
		return true;
	    /* falls thru! */
	case L'!':
	    if (word[1] != L'=')
		return false;
	    return (word[2] == L'\0') || (word[2] == L'=' && word[3] == L'\0');
	case L'<':
	case L'>':
	    return (word[1] == L'\0') || (word[1] == L'=' && word[2] == L'\0');
	case L'-':
	    break;
	default:
	    return false;
    }

    assert(word[0] == L'-');
    switch (word[1]) {
	case L'e':
	    switch (word[2]) {
		case L'f':
		case L'q':
		    return word[3] == L'\0';
	    }
	    break;
	case L'n':
	case L'g':
	case L'l':
	    switch (word[2]) {
		case L't':
		case L'e':
		    return word[3] == L'\0';
	    }
	    break;
	case L'o':
	    return word[2] == L't' && word[3] == L'\0';
	case L'v':
	    switch (word[2]) {
		case L'e':
		    return word[3] == L'q' && word[4] == L'\0';
		case L'n':
		    return word[3] == L'e' && word[4] == L'\0';
		case L'g':
		case L'l':
		    switch (word[3]) {
			case L't':
			case L'e':
			    return word[4] == L'\0';
		    }
		    break;
	    }
	    break;
    }
    return false;
}

/* Checks if `word' is a term delimiter:
 * one of ")", "-a", "-o". */
bool is_term_delimiter(const wchar_t *word)
{
    switch (word[0]) {
	case L')':
	    return word[1] == L'\0';
	case L'-':
	    switch (word[1]) {
		case L'a':
		case L'o':
		    return word[2] == L'\0';
	    }
	    break;
    }
    return false;
}

/* Converts the specified two strings into integers and compares them.
 * Returns -1, 0, 1 if the first integer is less than, equal to, or greater than
 * the second, respectively. */
int compare_integers(const wchar_t *left, const wchar_t *right)
{
    intmax_t il, ir;
    wchar_t *end;

    errno = 0;
    il = wcstoimax(left, &end, 10);
    if (errno != 0 || left[0] == L'\0' || *end != L'\0') {
	xerror(errno, Ngt("`%ls' is not a valid integer"), left);
	return 0;
    }
    errno = 0;
    ir = wcstoimax(right, &end, 10);
    if (errno != 0 || right[0] == L'\0' || *end != L'\0') {
	xerror(errno, Ngt("`%ls' is not a valid integer"), right);
	return 0;
    }
    if (il < ir)
	return -1;
    else if (il > ir)
	return 1;
    else
	return 0;
}

/* Compares the specified two strings as version numbers.
 * Returns a value less than, equal to, and greater than zero if the first is
 * less than, equal to, and greater than the second, respectively. */
int compare_versions(const wchar_t *left, const wchar_t *right)
{
    for (;;) {
	bool leftisdigit = iswdigit(*left), rightisdigit = iswdigit(*right);
	if (leftisdigit && rightisdigit) {
	    uintmax_t il, ir;

	    il = wcstoumax(left,  (wchar_t **) &left,  10);
	    ir = wcstoumax(right, (wchar_t **) &right, 10);
	    if (il > ir)
		return 1;
	    if (il < ir)
		return -1;
	} else if (leftisdigit) {
	    return 1;
	} else if (rightisdigit) {
	    return -1;
	}

	bool leftisalnum = iswalnum(*left), rightisalnum = iswalnum(*right);
	if (leftisalnum && !rightisalnum)
	    return 1;
	if (!leftisalnum && rightisalnum)
	    return -1;

	if (*left != *right)
	    return wcscoll(left, right);
	if (*left == L'\0')
	    return 0;

	left++, right++;
    }
}

/* Compares the specified two files.
 * Returns one of the followings:
 *   FC_ID:       the two files have the same inode
 *   FC_SAME:     different inodes, the same modification time
 *   FC_NEWER:    `left' has the modification time newer than `right'
 *   FC_OLDER:    `left' has the modification time older than `right'
 *   FC_UNKNOWN:  comparison error (neither file is `stat'able)
 * If either (but not both) file is not `stat'able, the `stat'able one is
 * considered newer. */
enum filecmp compare_files(const wchar_t *left, const wchar_t *right)
{
    char *mbsfile;
    struct stat sl, sr;
    bool sl_ok, sr_ok;

    mbsfile = malloc_wcstombs(left);
    if (mbsfile == NULL) {
	xerror(EILSEQ, Ngt("unexpected error"));
	return FC_UNKNOWN;
    }
    sl_ok = stat(mbsfile, &sl) >= 0;
    free(mbsfile);

    mbsfile = malloc_wcstombs(right);
    if (mbsfile == NULL) {
	xerror(EILSEQ, Ngt("unexpected error"));
	return FC_UNKNOWN;
    }
    sr_ok = stat(mbsfile, &sr) >= 0;
    free(mbsfile);

    if (!sl_ok)
	if (!sr_ok)
	    return FC_UNKNOWN;
	else
	    return FC_OLDER;
    else if (!sr_ok)
	return FC_NEWER;

    if (stat_result_same_file(&sl, &sr))
	return FC_ID;
    else if (sl.st_mtime < sr.st_mtime)
	return FC_OLDER;
    else if (sl.st_mtime > sr.st_mtime)
	return FC_NEWER;
#if HAVE_ST_MTIM
    else if (sl.st_mtim.tv_nsec < sr.st_mtim.tv_nsec)
	return FC_OLDER;
    else if (sl.st_mtim.tv_nsec > sr.st_mtim.tv_nsec)
	return FC_NEWER;
#elif HAVE_ST_MTIMESPEC
    else if (sl.st_mtimespec.tv_nsec < sr.st_mtimespec.tv_nsec)
	return FC_OLDER;
    else if (sl.st_mtimespec.tv_nsec > sr.st_mtimespec.tv_nsec)
	return FC_NEWER;
#elif HAVE_ST_MTIMENSEC
    else if (sl.st_mtimensec < sr.st_mtimensec)
	return FC_OLDER;
    else if (sl.st_mtimensec > sr.st_mtimensec)
	return FC_NEWER;
#elif HAVE___ST_MTIMENSEC
    else if (sl.__st_mtimensec < sr.__st_mtimensec)
	return FC_OLDER;
    else if (sl.__st_mtimensec > sr.__st_mtimensec)
	return FC_NEWER;
#endif
    else
	return FC_SAME;
}

#if YASH_ENABLE_HELP
const char test_help[] = Ngt(
"evaluate a conditional expression"
);
const char test_syntax[] = Ngt(
"\ttest expression\n"
"\t[ expression ]\n"
);
#endif

#if YASH_ENABLE_DOUBLE_BRACKET

/* Executes the double-bracket command, evaluating the conditional expression.
 * Returns the exit status. Prints an error message on error. */
int exec_double_bracket(const command_T *c)
{
    assert(c->c_type == CT_BRACKET);

    yash_error_message_count = 0;
    return eval_dbexp(c->c_dbexp);
}

/* Evaluates the expression of a double-bracket command.
 * You must reset `yash_error_message_count' before calling this function.
 * Returns the exit status. Prints an error message on error. */
int eval_dbexp(const dbexp_T *e)
{
    int lhs_result;
    bool result;
    wchar_t *lhs = NULL;
    cc_word_T rhs = { NULL, NULL };

    switch (e->type) {
	case DBE_OR:
	    lhs_result = eval_dbexp(e->lhs.subexp);
	    if (lhs_result != Exit_FALSE)
		return lhs_result;
	    return eval_dbexp(e->rhs.subexp);
	case DBE_AND:
	    lhs_result = eval_dbexp(e->lhs.subexp);
	    if (lhs_result != Exit_TRUE)
		return lhs_result;
	    return eval_dbexp(e->rhs.subexp);
	case DBE_NOT:
	    switch (eval_dbexp(e->rhs.subexp)) {
		case Exit_TRUE:   return Exit_FALSE;
		case Exit_FALSE:  return Exit_TRUE;
		default:          return Exit_TESTERROR;
	    }

	case DBE_UNARY:
	    rhs.value = expand_double_bracket_operand_unescaped(e->rhs.word);
	    if (rhs.value == NULL)
		return Exit_TESTERROR;
	    result = test_double((void *[]) { e->operator, rhs.value });
	    break;
	case DBE_BINARY:
	    lhs = expand_double_bracket_operand_unescaped(e->lhs.word);
	    if (lhs == NULL)
		return Exit_TESTERROR;
	    rhs = expand_double_bracket_operand(e->rhs.word);
	    if (rhs.value == NULL) {
		free(lhs);
		return Exit_TESTERROR;
	    }
	    result = test_triple_db(lhs, e->operator, rhs.value, rhs.cc);
	    break;
	case DBE_STRING:
	    rhs.value = expand_double_bracket_operand_unescaped(e->rhs.word);
	    if (rhs.value == NULL)
		return Exit_TESTERROR;
	    result = test_single((void *[]) { rhs.value });
	    break;

	default:
	    assert(false);
    }

    free(lhs);
    free(rhs.value);
    free(rhs.cc);
    if (yash_error_message_count > 0)
	return Exit_TESTERROR;
    return result ? Exit_TRUE : Exit_FALSE;
}

/* Expands the operand of a primary, but without quote removal. */
cc_word_T expand_double_bracket_operand(const wordunit_T *w)
{
    return expand_single_cc(w, TT_SINGLE, Q_WORD);
}

/* Expands the operand of a primary.
 * The result is literal (does not contain backslash escapes). */
wchar_t *expand_double_bracket_operand_unescaped(const wordunit_T *w)
{
    return expand_single(w, TT_SINGLE, Q_WORD, ES_NONE);
}

/* Tests the specified three-token (binary) primary in the double-bracket
 * command. The left-hand-side must be given literal (with quote removal already
 * performed) while the right-hand-side quoted (without quote removal). */
bool test_triple_db(const wchar_t *lhs, const wchar_t *op,
	const wchar_t *rhsvalue, const char *rhscc)
{
    /* Some string comparison primaries in the double-bracket command are
     * different from those in the test built-in. */
    switch (op[0]) {
	case L'=':
	    if (op[1] == L'~') {
		assert(op[2] == L'\0');
		return quote_removal_and_regex_matching(lhs, rhsvalue, rhscc);
	    }
	    if (op[1] == L'\0' || (op[1] == L'=' && op[2] == L'\0'))
		return quote_removal_and_pattern_matching(lhs, rhsvalue, rhscc);
	    break;
	case L'!':
	    assert(op[1] == L'=');
	    if (op[2] == L'\0')
		return
		    !quote_removal_and_pattern_matching(lhs, rhsvalue, rhscc);
	    break;
    }

    return quote_removal_and_test_triple(lhs, op, rhsvalue, rhscc);
}

/* Performs quote removal on the right hand side and then applies `test_triple'.
 */
bool quote_removal_and_test_triple(
	const wchar_t *lhs, const wchar_t *op,
	const wchar_t *rhsvalue, const char *rhscc)
{
    wchar_t *rhs = quote_removal(rhsvalue, rhscc, ES_NONE);
    void *args[] = { (void *) lhs, (void *) op, (void *) rhs, };
    bool result = test_triple(args);
    free(rhs);
    return result;
}

/* Performs quote removal on the right hand side and then pattern matching. */
bool quote_removal_and_pattern_matching(
	const wchar_t *lhs, const wchar_t *rhsvalue, const char *rhscc)
{
    wchar_t *rhs = quote_removal(rhsvalue, rhscc, ES_QUOTED);
    bool result = match_pattern(lhs, rhs);
    free(rhs);
    return result;
}

/* Performs quote removal on the right hand side and then regular expression
 * matching. */
bool quote_removal_and_regex_matching(
	const wchar_t *lhs, const wchar_t *rhsvalue, const char *rhscc)
{
    wchar_t *rhs = quote_removal_for_regex(rhsvalue, rhscc);
    bool result = match_regex(lhs, rhs);
    free(rhs);
    return result;
}

/* Removes all quotation marks in the input string `s' and add backslash escapes
 * to quoted characters that would otherwise be treated specially when parsed as
 * an extended regular expression pattern. The result is a newly malloced
 * string. */
wchar_t *quote_removal_for_regex(const wchar_t *s, const char *cc)
{
    /* First, remove quotations. */
    xwcsbuf_T tmp;
    xstrbuf_T tmpcc;
    size_t sizehint = wcslen(s);
    wb_initwithmax(&tmp, sizehint);
    sb_initwithmax(&tmpcc, sizehint);
    for (size_t i = 0; s[i] != L'\0'; i++) {
	if (cc[i] & CC_QUOTATION)
	    continue;
	wb_wccat(&tmp, s[i]);
	sb_ccat(&tmpcc, cc[i]);
    }

    /* Next, escape unquoted special chars outside brackets */
    xwcsbuf_T result;
    wb_initwithmax(&result, sizehint);
    for (size_t i = 0; tmp.contents[i] != L'\0'; ) {
	if (tmpcc.contents[i] & CC_QUOTED) {
	    if (wcschr(L"^.[$()|*+?{\\", tmp.contents[i]) != NULL)
		wb_wccat(&result, L'\\');
	    wb_wccat(&result, tmp.contents[i++]);
	} else {
	    if (tmp.contents[i] != L'[') {
		wb_wccat(&result, tmp.contents[i++]);
	    } else {
		const wchar_t *s2 = skip_bracket(&tmp.contents[i]);
		size_t j = s2 - tmp.contents;
		while (i < j)
		    wb_wccat(&result, tmp.contents[i++]);
	    }
	}
    }

    sb_destroy(&tmpcc);
    wb_destroy(&tmp);
    return wb_towcs(&result);
}

/* Skips a bracket expression in a regular expression pattern.
 * Returns a pointer to the character just after the closing L']' (or the
 * terminating null character). */
const wchar_t *skip_bracket(const wchar_t *s)
{
    assert(*s == L'[');
    s++;

    if (*s == L'^')
	s++;
    if (*s == L']')
	s++;

    while (*s != L'\0') {
	if (*s == L']')
	    return s + 1;
	if (*s++ != L'[')
	    continue;

	switch (*s) {
	    case L':': case L'.': case L'=': ;
		wchar_t end[] = { *s, L']', L'\0', };
		s++;
		const wchar_t *endp = wcsstr(s, end);
		if (endp == NULL)
		    return s + wcslen(s);
		s = endp + 2;
		break;
	}
    }

    return s;
}

#endif /* YASH_ENABLE_DOUBLE_BRACKET */


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
