/* Yash: yet another shell */
/* test.c: test builtin */
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


#include "../common.h"
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wchar.h>
#include "../path.h"
#include "../plist.h"
#include "../strbuf.h"
#include "../util.h"
#include "test.h"


#define Exit_TRUE      0  /* Exit_SUCCESS */
#define Exit_FALSE     1  /* Exit_FAILURE */
#define Exit_TESTERROR 2  /* Exit_ERROR */

static bool test_error;
static struct {
    char **args;     /* the expression words */
    size_t length;   /* number of the words */
    size_t index;    /* the current position */
} state;

static inline bool test_single(char *args[static 1]);
static bool test_double(char *args[static 2]);
static bool test_triple(char *args[static 3]);
static bool test_long_or(void);
static bool test_long_and(void);
static bool test_long_term(void);
static bool is_unary_primary(const char *word)
    __attribute__((nonnull,pure));
static bool is_binary_primary(const char *word)
    __attribute__((nonnull,pure));
static bool is_term_delimiter(const char *word)
    __attribute__((nonnull,pure));
static int compare_integers(const char *word1, const char *word2)
    __attribute__((nonnull,pure));
static int compare_files(const char *file1, const char *file2)
    __attribute__((nonnull));


/* The "test" ("[") builtin */
int test_builtin(int argc, void **argv)
{
    if (wcscmp(ARGV(0), L"[") == 0) {
	argc--;
	if (wcscmp(ARGV(argc), L"]") != 0) {
	    xerror(0, Ngt("`%ls' missing"), L"]");
	    return Exit_TESTERROR;
	}
    }
    assert(argc > 0);
    argc--, argv++;
    test_error = false;

    /* convert wide strings into multibyte strings */
    char *args[argc];
    for (int i = 0; i < argc; i++) {
	args[i] = malloc_wcstombs(ARGV(i));
	if (!args[i])
	    test_error = true;
    }

    bool result = result;
    if (!test_error) {
	switch (argc) {
	    case 0:  result = false;                 break;
	    case 1:  result = test_single(args);     break;
	    case 2:  result = test_double(args);     break;
	    case 3:  result = test_triple(args);     break;
	    case 4:
		if (strcmp(args[0], "!") == 0) {
		    result = !test_triple(args + 1);
		    break;
		}
		if (strcmp(args[0], "(") == 0 && strcmp(args[3], ")") == 0) {
		    result = test_double(args + 1);
		    break;
		}
		/* falls thru! */
	    default:
		state.args = args;
		state.length = argc;
		state.index = 0;
		result = test_long_or();
		if (state.index < state.length) {
		    xerror(0, Ngt("expression incomplete"));
		    test_error = true;
		}
		break;
	}
    }

    for (int i = 0; i < argc; i++)
	free(args[i]);
    return test_error ? Exit_TESTERROR : result ? Exit_TRUE : Exit_FALSE;
}

/* Tests a one-token expression. */
bool test_single(char *args[static 1])
{
    return args[0][0] != '\0';
}

/* Tests a two-token expression. */
bool test_double(char *args[static 2])
{
    if (strcmp(args[0], "!") == 0)
	return !test_single(args + 1);
    if (xstrnlen(args[0], 3) != 2 || args[0][0] != '-') {
	xerror(0, Ngt("`%s %s': not a unary operation"), args[0], args[1]);
	test_error = true;
	return 0;
    }

    switch (args[0][1]) {
	case 'n':  return args[1][0] != '\0';
	case 'z':  return args[1][0] == '\0';
	case 't':
	    {
		int fd;
		char *end;

		errno = 0;
		fd = strtol(args[1], &end, 10);
		if (errno || !args[1][0] || *end)
		    return false;
		return isatty(fd);
	    }
    }

    struct stat st;
    switch (args[0][1]) {
	case 'r':  return is_readable(args[1]);
	case 'w':  return is_writable(args[1]);
	case 'x':  return is_executable(args[1]);
	case 'b':
	    return (stat(args[1], &st) == 0) && S_ISBLK(st.st_mode);
	case 'c':
	    return (stat(args[1], &st) == 0) && S_ISCHR(st.st_mode);
	case 'd':
	    return (stat(args[1], &st) == 0) && S_ISDIR(st.st_mode);
	case 'e':
	    return (stat(args[1], &st) == 0);
	case 'f':
	    return (stat(args[1], &st) == 0) && S_ISREG(st.st_mode);
	case 'g':
	    return (stat(args[1], &st) == 0) && (st.st_mode & S_ISGID);
	case 'h':  case 'L':
	    return (lstat(args[1], &st) == 0) && S_ISLNK(st.st_mode);
	case 'k':
#if HAVE_S_ISVTX
	    return (stat(args[1], &st) == 0) && (st.st_mode & S_ISVTX);
#else
	    return false;
#endif
	case 'p':
	    return (stat(args[1], &st) == 0) && S_ISFIFO(st.st_mode);
	case 'S':
	    return (stat(args[1], &st) == 0) && S_ISSOCK(st.st_mode);
	case 's':
	    return (stat(args[1], &st) == 0) && st.st_size > 0;
	case 'u':
	    return (stat(args[1], &st) == 0) && (st.st_mode & S_ISUID);
	default:
	    xerror(0, Ngt("`%s': invalid operator"), args[0]);
	    test_error = true;
	    return false;
    }
}

/* Tests a three-token expression. */
bool test_triple(char *args[static 3])
{
    /* first, check if args[1] is a binary primary operator */
    if (strcmp(args[1], "=") == 0)
	return strcmp(args[0], args[2]) == 0;
    if (strcmp(args[1], "!=") == 0)
	return strcmp(args[0], args[2]) != 0;
    if (args[1][0] != '-')
	goto not_binary;
    switch (xstrnlen(args[1], 4)) {
	case 2:
	    if (args[1][1] == 'a')
		return test_single(args) && test_single(args + 2);
	    if (args[1][1] == 'o')
		return test_single(args) || test_single(args + 2);
	    break;
	case 3:
	    switch (args[1][1]) {
		case 'e':
		    if (args[1][2] == 'f')
			return is_same_file(args[0], args[2]);
		    if (args[1][2] == 'q')
			return compare_integers(args[0], args[2]) == 0;
		    goto not_binary;
		case 'g':
		    if (args[1][2] == 't')
			return compare_integers(args[0], args[2]) > 0;
		    if (args[1][2] == 'e')
			return compare_integers(args[0], args[2]) >= 0;
		    goto not_binary;
		case 'l':
		    if (args[1][2] == 't')
			return compare_integers(args[0], args[2]) < 0;
		    if (args[1][2] == 'e')
			return compare_integers(args[0], args[2]) <= 0;
		    goto not_binary;
		case 'n':
		    if (args[1][2] == 'e')
			return compare_integers(args[0], args[2]) != 0;
		    if (args[1][2] == 't')
			return compare_files(args[0], args[2]) == 1;
		    goto not_binary;
		case 'o':
		    if (args[1][2] == 't')
			return compare_files(args[0], args[2]) == -1;
		    goto not_binary;
	    }
    }

not_binary:
    if (strcmp(args[0], "!") == 0)
	return !test_double(args + 1);
    if (strcmp(args[0], "(") == 0 && strcmp(args[2], ")") == 0)
	return test_single(args + 1);

    xerror(0, Ngt("`%s %s %s': invalid expression"), args[0], args[1], args[2]);
    test_error = true;
    return 0;
}

/* exp := exp "-o" and | and
 * and := and "-a" term | term
 * term := "(" exp ")" | "!" "(" exp ")" | single | double | triple
 */

/* Tests a long expression using `state'. */
bool test_long_or(void)
{
    bool result;

    result = test_long_and();
    while (state.index < state.length
	    && strcmp(state.args[state.index], "-o") == 0) {
	state.index++;
	result |= test_long_and();
    }
    return result;
}

/* Tests a long expression using `state'. */
bool test_long_and(void)
{
    bool result;

    result = test_long_term();
    while (state.index < state.length
	    && strcmp(state.args[state.index], "-a") == 0) {
	state.index++;
	result &= test_long_term();
    }
    return result;
}

/* Tests a long expression using `state'. */
bool test_long_term(void)
{
    bool result;
    bool negate = false;

    if (state.index < state.length
	    && strcmp(state.args[state.index], "!") == 0) {
	state.index++;
	negate = true;
    }
    if (state.index >= state.length) {
	assert(state.length > 0);
	xerror(0, Ngt("expression missing after `%s'"),
		state.args[state.index - 1]);
	test_error = true;
	return 0;
    }
    if (strcmp(state.args[state.index], "(") == 0) {
	state.index++;
	result = test_long_or();
	if (state.index >= state.length
		|| strcmp(state.args[state.index], ")") != 0) {
	    xerror(0, Ngt("`%ls' missing"), L")");
	    test_error = true;
	    return 0;
	}
	state.index++;
    } else if (state.index + 3 <= state.length
	    && is_binary_primary(state.args[state.index + 1])
	    && (state.index + 3 >= state.length
		|| is_term_delimiter(state.args[state.index + 3]))) {
	result = test_triple(state.args + state.index);
	state.index += 3;
    } else if (state.index + 2 <= state.length
	    && is_unary_primary(state.args[state.index])
	    && (state.index + 2 >= state.length
		|| is_term_delimiter(state.args[state.index + 2]))) {
	result = test_double(state.args + state.index);
	state.index += 2;
    } else {
	result = test_single(state.args + state.index);
	state.index += 1;
    }
    return result ^ negate;
}

/* Checks if `word' is a unary primary operator. */
/* Note that "!" is not a primary operator. */
bool is_unary_primary(const char *word)
{
    if (xstrnlen(word, 3) != 2 || word[0] != '-')
	return false;
    switch (word[1]) {
	case 'b':  case 'c':  case 'd':  case 'e':  case 'f':  case 'g':
	case 'h':  case 'k':  case 'L':  case 'n':  case 'p':  case 'r':
	case 'S':  case 's':  case 't':  case 'u':  case 'w':  case 'x':
	case 'z':
	    return true;
	default:
	    return false;
    }
}

/* Checks if `word' is a binary primary operator.
 * This function returns false for "-a" and "-o". */
bool is_binary_primary(const char *word)
{
    if (strcmp(word, "=") == 0 || strcmp(word, "!=") == 0)
	return true;
    if (word[0] != '-')
	return false;
    if (xstrnlen(word, 4) != 3)
	return false;
    switch (word[1]) {
	case 'e':
	    return word[2] == 'f' || word[2] == 'q';
	case 'g':  case 'l':
	    return word[2] == 't' || word[2] == 'e';
	case 'n':
	    return word[2] == 'e' || word[2] == 't';
	case 'o':
	    return word[2] == 't';
	default:
	    return false;
    }
}

/* Checks if `word' is a term delimiter:
 * one of ")", "-a", "-o". */
bool is_term_delimiter(const char *word)
{
    return (word[0] == ')' && !word[1])
	|| (word[0] == '-' && (word[1] == 'a' || word[1] == 'o') && !word[2]);
}

/* Converts two strings into integers and compares them.
 * Returns -1, 0, 1 if the first integer is less than, equal to, or greater than
 * the second respectively. */
int compare_integers(const char *word1, const char *word2)
{
    int i1, i2;
    char *end;

    errno = 0;
    i1 = strtoimax(word1, &end, 10);
    if (errno || !word1[0] || *end) {
	xerror(errno, Ngt("`%s' is not a valid integer"), word1);
	test_error = true;
	return false;
    }
    errno = 0;
    i2 = strtoimax(word2, &end, 10);
    if (errno || !word2[0] || *end) {
	xerror(errno, Ngt("`%s' is not a valid integer"), word2);
	test_error = true;
	return false;
    }
    if (i1 < i2)
	return -1;
    else if (i1 > i2)
	return 1;
    else
	return 0;
}

/* Compares the modification time of two files.
 * Returns -1, 0, 1 if `file1' is older than, as new as, or newer than `file2'
 * respectively. If `file1' and/or `file2' do not exists, returns -2. */
int compare_files(const char *file1, const char *file2)
{
    time_t t1, t2;
    struct stat st;

    if (stat(file1, &st) < 0)
	return -2;
    t1 = st.st_mtime;
    if (stat(file2, &st) < 0)
	return -2;
    t2 = st.st_mtime;

    if (t1 < t2)
	return -1;
    else if (t1 > t2)
	return 1;
    else
	return 0;
}

const char test_help[] = Ngt(
"test, [ - evaluate conditional expression\n"
"\ttest expression\n"
"\t[ expression ]\n"
"Evaluates <expression> as a conditional expression described below. The exit\n"
"status is 0 if the condition is true, or 1 otherwise.\n"
"\n"
"Unary operators to test files:\n"
"  -b file    <file> is a block special file\n"
"  -c file    <file> is a character special file\n"
"  -d file    <file> is a directory\n"
"  -e file    <file> exists\n"
"  -f file    <file> is a regular file\n"
"  -g file    <file>'s set-group-ID flag is set\n"
"  -h file    same as -L\n"
"  -k file    <file>'s sticky bit is set\n"
"  -L file    <file> is a symbolic link\n"
"  -p file    <file> is a FIFO (named pipe)\n"
"  -r file    <file> is readable\n"
"  -S file    <file> is a socket\n"
"  -s file    <file> is not empty\n"
"  -u file    <file>'s set-user-ID flag is set\n"
"  -w file    <file> is writable\n"
"  -x file    <file> is executable\n"
"Unary operator to test file descriptors:\n"
"  -t fd      <fd> is associated with a terminal\n"
"Unary operators to test strings:\n"
"  -n string    <string> is not empty\n"
"  -z string    <string> is empty\n"
"\n"
"Binary operators to compare files:\n"
"  file1 -nt file2       <file1> is newer than <file2>\n"
"  file1 -ot file2       <file1> is older than <file2>\n"
"  file1 -ef file2       <file1> is a hard link to <file2>\n"
"Binary operators to compare strings:\n"
"  string1 = string2     <string1> is the same string as <string2>\n"
"  string1 != string2    <string1> is not the same string as <string2>\n"
"Binary operators to compare integers:\n"
"  v1 -eq v2    <v1> is equal to <v2>\n"
"  v1 -ne v2    <v1> is not equal to <v2>\n"
"  v1 -gt v2    <v1> is greater than <v2>\n"
"  v1 -ge v2    <v1> is greater than or equal to <v2>\n"
"  v1 -lt v2    <v1> is less than <v2>\n"
"  v1 -le v2    <v1> is less than or equal to <v2>\n"
"\n"
"Operators to make complex expressions:\n"
"  ! exp           negate (reverse) the result\n"
"  ( exp )         change operator precedence\n"
"  exp1 -a exp2    logical conjunction (and)\n"
"  exp1 -o exp2    logical disjunction (or)\n"
"Using these operators may cause confusion and should be avoided. Use the\n"
"shell's compound commands.\n"
"\n"
"If the expression is a single word without operators, the -n operator is\n"
"assumed. An empty expression evaluates false.\n"
);


/* vim: set ts=8 sts=4 sw=4 noet: */
