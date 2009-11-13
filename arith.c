/* Yash: yet another shell */
/* arith.c: arithmetic expansion */
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


#include "common.h"
#include <assert.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>
#include <sys/types.h>
/* #include <wctype.h> */
#include "arith.h"
#include "option.h"
#include "strbuf.h"
#include "util.h"
#include "variable.h"
/* Must not include <ctype.h> or <wctype.h> because symbol names conflict.
 * (to[a-z]*) */


/* we declare these functions instead of including <wctype.h> */
extern int iswalpha(wint_t wc);
extern int iswalnum(wint_t wc);
extern int iswdigit(wint_t wc);
extern int iswspace(wint_t wc);


typedef struct word_T {
    const wchar_t *contents;
    size_t length;
} word_T;

typedef enum valuetype_T {
    VT_INVALID, VT_LONG, VT_DOUBLE, VT_VAR,
} valuetype_T;
typedef struct value_T {
    valuetype_T type;
    union {
	long longvalue;
	double doublevalue;
	word_T varvalue;
    } value;
} value_T;
#define v_long   value.longvalue
#define v_double value.doublevalue
#define v_var    value.varvalue

typedef enum tokentype_T {
    TT_NULL, TT_INVALID,
    /* symbols */
    TT_LPAREN, TT_RPAREN, TT_TILDE, TT_EXCL, TT_PERCENT,
    TT_PLUS, TT_MINUS, TT_PLUSPLUS, TT_MINUSMINUS, TT_ASTER, TT_SLASH,
    TT_LESS, TT_GREATER, TT_LESSLESS, TT_GREATERGREATER,
    TT_LESSEQUAL, TT_GREATEREQUAL, TT_EQUALEQUAL, TT_EXCLEQUAL,
    TT_AMP, TT_AMPAMP, TT_HAT, TT_PIPE, TT_PIPEPIPE, TT_QUESTION, TT_COLON,
    TT_EQUAL, TT_PLUSEQUAL, TT_MINUSEQUAL, TT_ASTEREQUAL, TT_SLASHEQUAL,
    TT_PERCENTEQUAL, TT_LESSLESSEQUAL, TT_GREATERGREATEREQUAL,
    TT_AMPEQUAL, TT_HATEQUAL, TT_PIPEEQUAL,
    /* numbers */
    TT_NUMBER,
    /* identifiers */
    TT_IDENTIFIER,
} tokentype_T;
typedef struct token_T {
    tokentype_T type;
    word_T word;  /* valid only for numbers and identifiers */
} token_T;

typedef struct evalinfo_T {
    const wchar_t *exp;  /* expression to parse and calculate */
    size_t index;        /* index of next token */
    token_T token;       /* current token */
    bool parseonly;      /* only parse the expression: don't calculate */
    bool error;          /* true if there is an error */
} evalinfo_T;

static void parse_assignment(evalinfo_T *info, value_T *result)
    __attribute__((nonnull));
static bool do_assignment(const word_T *word, const value_T *value)
    __attribute__((nonnull));
static wchar_t *value_to_string(const value_T *value)
    __attribute__((nonnull));
static long do_long_calculation(tokentype_T ttype, long v1, long v2)
    __attribute__((nonnull));
static double do_double_calculation(tokentype_T ttype, double v1, double v2)
    __attribute__((nonnull));
static long do_double_comparison(tokentype_T ttype, double v1, double v2)
    __attribute__((nonnull));
static void parse_conditional(evalinfo_T *info, value_T *result)
    __attribute__((nonnull));
static void parse_logical_or(evalinfo_T *info, value_T *result)
    __attribute__((nonnull));
static void parse_logical_and(evalinfo_T *info, value_T *result)
    __attribute__((nonnull));
static void parse_inclusive_or(evalinfo_T *info, value_T *result)
    __attribute__((nonnull));
static void parse_exclusive_or(evalinfo_T *info, value_T *result)
    __attribute__((nonnull));
static void parse_and(evalinfo_T *info, value_T *result)
    __attribute__((nonnull));
static void parse_equality(evalinfo_T *info, value_T *result)
    __attribute__((nonnull));
static void parse_relational(evalinfo_T *info, value_T *result)
    __attribute__((nonnull));
static void parse_shift(evalinfo_T *info, value_T *result)
    __attribute__((nonnull));
static void parse_additive(evalinfo_T *info, value_T *result)
    __attribute__((nonnull));
static void parse_multiplicative(evalinfo_T *info, value_T *result)
    __attribute__((nonnull));
static void parse_prefix(evalinfo_T *info, value_T *result)
    __attribute__((nonnull));
static void parse_postfix(evalinfo_T *info, value_T *result)
    __attribute__((nonnull));
static void do_increment_or_decrement(tokentype_T ttype, value_T *value)
    __attribute__((nonnull));
static void parse_primary(evalinfo_T *info, value_T *result)
    __attribute__((nonnull));
static void parse_as_number(word_T *word, value_T *result)
    __attribute__((nonnull));
static void coerce_number(evalinfo_T *info, value_T *value)
    __attribute__((nonnull));
static void coerce_integer(evalinfo_T *info, value_T *value)
    __attribute__((nonnull));
static valuetype_T coerce_type(evalinfo_T *info,
	value_T *value1, value_T *value2)
    __attribute__((nonnull));
static void next_token(evalinfo_T *info)
    __attribute__((nonnull));


/* Evaluates the specified string as an arithmetic expression.
 * The argument string is freed in this function.
 * The result is converted into a string and returned as a newly-malloced
 * string. On error, an error message is printed to stderr and NULL is
 * returned. */
wchar_t *evaluate_arithmetic(wchar_t *exp)
{
    value_T result;
    evalinfo_T info;

    info.exp = exp;
    info.index = 0;
    info.parseonly = false;
    info.error = false;

    next_token(&info);
    parse_assignment(&info, &result);
    if (posixly_correct)
	coerce_number(&info, &result);

    wchar_t *resultstr;
    if (info.error) {
	resultstr = NULL;
    } else if (info.token.type == TT_NULL) {
	resultstr = value_to_string(&result);
    } else {
	if (info.token.type != TT_INVALID)
	    xerror(0, Ngt("arithmetic: invalid syntax"));
	resultstr = NULL;
    }
    free(exp);
    return resultstr;
}

/* Evaluates the specified string as an arithmetic expression.
 * The argument string is freed in this function.
 * The expression must yield a valid integer value, which is assigned to
 * `*valuep'. Otherwise, an error message is printed.
 * Returns true iff successful. */
bool evaluate_index(wchar_t *exp, ssize_t *valuep)
{
    value_T result;
    evalinfo_T info;

    info.exp = exp;
    info.index = 0;
    info.parseonly = false;
    info.error = false;

    next_token(&info);
    parse_assignment(&info, &result);
    coerce_number(&info, &result);

    bool ok;
    if (info.error) {
	ok = false;
    } else if (info.token.type == TT_NULL) {
	if (result.type == VT_LONG) {
#if LONG_MAX > SSIZE_MAX
	    if (result.v_long > (long) SSIZE_MAX)
		*valuep = SSIZE_MAX;
	    else
#endif
#if LONG_MIN < -SSIZE_MAX
	    if (result.v_long < (long) -SSIZE_MAX)
		*valuep = -SSIZE_MAX;
	    else
#endif
		*valuep = (ssize_t) result.v_long;
	    ok = true;
	} else {
	    xerror(0, Ngt("index: not an integer"));
	    ok = false;
	}
    } else {
	if (info.token.type != TT_INVALID)
	    xerror(0, Ngt("arithmetic: invalid syntax"));
	ok = false;
    }
    free(exp);
    return ok;
}

/* Parses an assignment expression.
 *   AssignmentExp := ConditionalExp
 *                  | ConditionalExp AssignmentOperator AssignmentExp */
void parse_assignment(evalinfo_T *info, value_T *result)
{
    parse_conditional(info, result);

    tokentype_T ttype = info->token.type;
    switch (ttype) {
	case TT_EQUAL:          case TT_PLUSEQUAL:   case TT_MINUSEQUAL:
	case TT_ASTEREQUAL:     case TT_SLASHEQUAL:  case TT_PERCENTEQUAL:
	case TT_LESSLESSEQUAL:  case TT_GREATERGREATEREQUAL:
	case TT_AMPEQUAL:       case TT_HATEQUAL:    case TT_PIPEEQUAL:
	    {
		value_T rhs;
		next_token(info);
		parse_assignment(info, &rhs);
		if (result->type == VT_VAR) {
		    word_T saveword = result->v_var;
		    switch (coerce_type(info, result, &rhs)) {
			case VT_LONG:
			    result->v_long = do_long_calculation(ttype,
				    result->v_long, rhs.v_long);
			    break;
			case VT_DOUBLE:
			    result->v_double = do_double_calculation(ttype,
				    result->v_double, rhs.v_double);
			    break;
			case VT_INVALID:
			    result->type = VT_INVALID;
			    break;
			case VT_VAR:
			    assert(false);
		    }
		    if (!do_assignment(&saveword, result))
			info->error = true, result->type = VT_INVALID;
		} else if (result->type != VT_INVALID) {
		    xerror(0, Ngt("arithmetic: cannot assign to number"));
		    info->error = true;
		    result->type = VT_INVALID;
		}
		break;
	    }
	default:
	    break;
    }
}

/* Assigns the specified `value' to the variable specified by `word'.
 * Returns false on error. */
bool do_assignment(const word_T *word, const value_T *value)
{
    wchar_t *vstr = value_to_string(value);
    if (!vstr)
	return false;

    wchar_t name[word->length + 1];
    wmemcpy(name, word->contents, word->length);
    name[word->length] = L'\0';
    return set_variable(name, vstr, SCOPE_GLOBAL, false);
}

/* Converts `value' to a newly-malloced wide string.
 * Returns NULL for VT_INVALID. */
wchar_t *value_to_string(const value_T *value)
{
    switch (value->type) {
	case VT_INVALID:
	    return NULL;
	case VT_LONG:
	    return malloc_wprintf(L"%ld", value->v_long);
	case VT_DOUBLE:
	    return malloc_wprintf(L"%.*g", DBL_DIG, value->v_double);
	case VT_VAR:
	    {
		wchar_t name[value->v_var.length + 1];
		wmemcpy(name, value->v_var.contents, value->v_var.length);
		name[value->v_var.length] = L'\0';
		const wchar_t *var = getvar(name);
		return var ? xwcsdup(var) : NULL;
	    }
	default:
	    assert(false);
    }
}

/* Does unary or binary long calculation according to the specified operator
 * token. Division by zero is not allowed. */
long do_long_calculation(tokentype_T ttype, long v1, long v2)
{
    switch (ttype) {
	case TT_PLUS:  case TT_PLUSEQUAL:
	    return v1 + v2;
	case TT_MINUS:  case TT_MINUSEQUAL:
	    return v1 - v2;
	case TT_ASTER:  case TT_ASTEREQUAL:
	    return v1 * v2;
	case TT_SLASH:  case TT_SLASHEQUAL:
	    return v1 / v2;
	case TT_PERCENT:  case TT_PERCENTEQUAL:
	    return v1 % v2;
	case TT_LESSLESS:  case TT_LESSLESSEQUAL:
	    return v1 << v2;
	case TT_GREATERGREATER:  case TT_GREATERGREATEREQUAL:
	    return v1 >> v2;
	case TT_LESS:
	    return v1 < v2;
	case TT_LESSEQUAL:
	    return v1 <= v2;
	case TT_GREATER:
	    return v1 > v2;
	case TT_GREATEREQUAL:
	    return v1 >= v2;
	case TT_EQUALEQUAL:
	    return v1 == v2;
	case TT_EXCLEQUAL:
	    return v1 != v2;
	case TT_AMP:  case TT_AMPEQUAL:
	    return v1 & v2;
	case TT_HAT:  case TT_HATEQUAL:
	    return v1 ^ v2;
	case TT_PIPE:  case TT_PIPEEQUAL:
	    return v1 | v2;
	case TT_EQUAL:
	    return v2;
	default:
	    assert(false);
    }
}

/* Does unary or binary double calculation according to the specified operator
 * token. */
double do_double_calculation(tokentype_T ttype, double v1, double v2)
{
    switch (ttype) {
	case TT_PLUS:  case TT_PLUSEQUAL:
	    return v1 + v2;
	case TT_MINUS:  case TT_MINUSEQUAL:
	    return v1 - v2;
	case TT_ASTER:  case TT_ASTEREQUAL:
	    return v1 * v2;
	case TT_SLASH:  case TT_SLASHEQUAL:
	    return v1 / v2;
	case TT_PERCENT:  case TT_PERCENTEQUAL:
	    return fmod(v1, v2);
	default:
	    assert(false);
    }
}

/* Does double comparison according to the specified operator token. */
long do_double_comparison(tokentype_T ttype, double v1, double v2)
{
    switch (ttype) {
	case TT_LESS:
	    return v1 < v2;
	case TT_LESSEQUAL:
	    return v1 <= v2;
	case TT_GREATER:
	    return v1 > v2;
	case TT_GREATEREQUAL:
	    return v1 >= v2;
	case TT_EQUALEQUAL:
	    return v1 == v2;
	case TT_EXCLEQUAL:
	    return v1 != v2;
	default:
	    assert(false);
    }
}

/* Parses a conditional expression.
 *   ConditionalExp := LogicalOrExp
 *                   | LogicalOrExp "?" AssignmentExp ":" ConditionalExp */
void parse_conditional(evalinfo_T *info, value_T *result)
{
    bool saveparseonly = info->parseonly;
start:
    parse_logical_or(info, result);
    if (info->token.type == TT_QUESTION) {
	bool cond, valid = true;
	value_T dummy;

	coerce_number(info, result);
	next_token(info);
	switch (result->type) {
	    case VT_INVALID:  valid = false, cond = true;  break;
	    case VT_LONG:     cond = result->v_long;       break;
	    case VT_DOUBLE:   cond = result->v_double;     break;
	    default:          assert(false);
	}
	if (cond) {
	    parse_assignment(info, result);
	    if (info->token.type == TT_COLON) {
		next_token(info);
		info->parseonly = true;
		parse_conditional(info, &dummy);
		info->parseonly = saveparseonly;
	    } else {
		xerror(0, Ngt("arithmetic: `%ls' missing"), L":");
		info->error = true;
		result->type = VT_INVALID;
	    }
	} else {
	    info->parseonly = true;
	    parse_assignment(info, &dummy);
	    info->parseonly = saveparseonly;
	    if (info->token.type == TT_COLON) {
		next_token(info);
		goto start;
	    } else {
		xerror(0, Ngt("arithmetic: `%ls' missing"), L":");
		info->error = true;
		result->type = VT_INVALID;
	    }
	}
    }
}

/* Parses a logical OR expression.
 *   LogicalOrExp := LogicalAndExp | LogicalOrExp "||" LogicalAndExp */
void parse_logical_or(evalinfo_T *info, value_T *result)
{
    bool saveparseonly = info->parseonly;
    parse_logical_and(info, result);
    while (info->token.type == TT_PIPEPIPE) {
#ifdef NDEBUG
	bool lhs, value, valid = true;
#else
	bool lhs, value = value, valid = true;
#endif
	coerce_number(info, result);
	next_token(info);
	switch (result->type) {
	    case VT_INVALID: valid = false, lhs = true;  break;
	    case VT_LONG:    lhs = result->v_long;       break;
	    case VT_DOUBLE:  lhs = result->v_double;     break;
	    default:         assert(false);
	}
	info->parseonly |= lhs;
	parse_logical_and(info, result);
	coerce_number(info, result);
	if (!lhs) switch (result->type) {
	    case VT_INVALID: valid = false;             break;
	    case VT_LONG:    value = result->v_long;    break;
	    case VT_DOUBLE:  value = result->v_double;  break;
	    default:         assert(false);
	} else {
	    value = true;
	}
	if (valid)
	    result->type = VT_LONG, result->v_long = value;
	else
	    result->type = VT_INVALID;
    }
    info->parseonly = saveparseonly;
}

/* Parses a logical AND expression.
 *   LogicalAndExp := InclusiveOrExp | LogicalAndExp "&&" InclusiveOrExp */
void parse_logical_and(evalinfo_T *info, value_T *result)
{
    bool saveparseonly = info->parseonly;
    parse_inclusive_or(info, result);
    while (info->token.type == TT_AMPAMP) {
#ifdef NDEBUG
	bool lhs, value, valid = true;
#else
	bool lhs, value = value, valid = true;
#endif
	coerce_number(info, result);
	next_token(info);
	switch (result->type) {
	    case VT_INVALID: valid = false, lhs = false;  break;
	    case VT_LONG:    lhs = result->v_long;        break;
	    case VT_DOUBLE:  lhs = result->v_double;      break;
	    default:         assert(false);
	}
	info->parseonly |= !lhs;
	parse_inclusive_or(info, result);
	coerce_number(info, result);
	if (lhs) switch (result->type) {
	    case VT_INVALID: valid = false;             break;
	    case VT_LONG:    value = result->v_long;    break;
	    case VT_DOUBLE:  value = result->v_double;  break;
	    default:         assert(false);
	} else {
	    value = false;
	}
	if (valid)
	    result->type = VT_LONG, result->v_long = value;
	else
	    result->type = VT_INVALID;
    }
    info->parseonly = saveparseonly;
}

/* Parses an inclusive OR expression.
 *   InclusiveOrExp := ExclusiveOrExp | InclusiveOrExp "|" ExclusiveOrExp */
void parse_inclusive_or(evalinfo_T *info, value_T *result)
{
    parse_exclusive_or(info, result);
    for (;;) {
	tokentype_T ttype = info->token.type;
	value_T rhs;
	switch (ttype) {
	    case TT_PIPE:
		next_token(info);
		parse_exclusive_or(info, &rhs);
		coerce_integer(info, result);
		coerce_integer(info, &rhs);
		if (result->type == VT_LONG && rhs.type == VT_LONG) {
		    result->v_long = do_long_calculation(ttype,
			    result->v_long, rhs.v_long);
		} else {
		    result->type = VT_INVALID;
		}
		break;
	    default:
		return;
	}
    }
}

/* Parses an exclusive OR expression.
 *   ExclusiveOrExp := AndExp | ExclusiveOrExp "^" AndExp */
void parse_exclusive_or(evalinfo_T *info, value_T *result)
{
    parse_and(info, result);
    for (;;) {
	tokentype_T ttype = info->token.type;
	value_T rhs;
	switch (ttype) {
	    case TT_HAT:
		next_token(info);
		parse_and(info, &rhs);
		coerce_integer(info, result);
		coerce_integer(info, &rhs);
		if (result->type == VT_LONG && rhs.type == VT_LONG) {
		    result->v_long = do_long_calculation(ttype,
			    result->v_long, rhs.v_long);
		} else {
		    result->type = VT_INVALID;
		}
		break;
	    default:
		return;
	}
    }
}

/* Parses an AND expression.
 *   AndExp := EqualityExp | AndExp "&" EqualityExp */
void parse_and(evalinfo_T *info, value_T *result)
{
    parse_equality(info, result);
    for (;;) {
	tokentype_T ttype = info->token.type;
	value_T rhs;
	switch (ttype) {
	    case TT_AMP:
		next_token(info);
		parse_equality(info, &rhs);
		coerce_integer(info, result);
		coerce_integer(info, &rhs);
		if (result->type == VT_LONG && rhs.type == VT_LONG) {
		    result->v_long = do_long_calculation(ttype,
			    result->v_long, rhs.v_long);
		} else {
		    result->type = VT_INVALID;
		}
		break;
	    default:
		return;
	}
    }
}

/* Parses an equality expression.
 *   EqualityExp := RelationalExp
 *                | EqualityExp "==" RelationalExp
 *                | EqualityExp "!=" RelationalExp */
void parse_equality(evalinfo_T *info, value_T *result)
{
    parse_relational(info, result);
    for (;;) {
	tokentype_T ttype = info->token.type;
	value_T rhs;
	switch (ttype) {
	    case TT_EQUALEQUAL:
	    case TT_EXCLEQUAL:
		next_token(info);
		parse_relational(info, &rhs);
		switch (coerce_type(info, result, &rhs)) {
		    case VT_LONG:
			result->v_long = do_long_calculation(ttype,
				result->v_long, rhs.v_long);
			break;
		    case VT_DOUBLE:
			result->v_long = do_double_comparison(ttype,
				result->v_double, rhs.v_double);
			result->type = VT_LONG;
			break;
		    case VT_INVALID:
			result->type = VT_INVALID;
			break;
		    case VT_VAR:     assert(false);
		}
		break;
	    default:
		return;
	}
    }
}

/* Parses a relational expression.
 *   RelationalExp := ShiftExp
 *                  | RelationalExp "<" ShiftExp
 *                  | RelationalExp ">" ShiftExp
 *                  | RelationalExp "<=" ShiftExp
 *                  | RelationalExp ">=" ShiftExp */
void parse_relational(evalinfo_T *info, value_T *result)
{
    parse_shift(info, result);
    for (;;) {
	tokentype_T ttype = info->token.type;
	value_T rhs;
	switch (ttype) {
	    case TT_LESS:
	    case TT_LESSEQUAL:
	    case TT_GREATER:
	    case TT_GREATEREQUAL:
		next_token(info);
		parse_shift(info, &rhs);
		switch (coerce_type(info, result, &rhs)) {
		    case VT_LONG:
			result->v_long = do_long_calculation(ttype,
				result->v_long, rhs.v_long);
			break;
		    case VT_DOUBLE:
			result->v_long = do_double_comparison(ttype,
				result->v_double, rhs.v_double);
			result->type = VT_LONG;
			break;
		    case VT_INVALID:
			result->type = VT_INVALID;
			break;
		    case VT_VAR:     assert(false);
		}
		break;
	    default:
		return;
	}
    }
}

/* Parses a shift expression.
 *   ShiftExp := AdditiveExp
 *             | ShiftExp "<<" AdditiveExp | ShiftExp ">>" AdditiveExp */
void parse_shift(evalinfo_T *info, value_T *result)
{
    parse_additive(info, result);
    for (;;) {
	tokentype_T ttype = info->token.type;
	value_T rhs;
	switch (ttype) {
	    case TT_LESSLESS:
	    case TT_GREATERGREATER:
		next_token(info);
		parse_additive(info, &rhs);
		coerce_integer(info, result);
		coerce_integer(info, &rhs);
		if (result->type == VT_LONG && rhs.type == VT_LONG) {
		    result->v_long = do_long_calculation(ttype,
			    result->v_long, rhs.v_long);
		} else {
		    result->type = VT_INVALID;
		}
		break;
	    default:
		return;
	}
    }
}

/* Parses a additive expression.
 *   AdditiveExp := MultiplicativeExp
 *                | AdditiveExp "+" MultiplicativeExp
 *                | AdditiveExp "-" MultiplicativeExp */
void parse_additive(evalinfo_T *info, value_T *result)
{
    parse_multiplicative(info, result);
    for (;;) {
	tokentype_T ttype = info->token.type;
	value_T rhs;
	switch (ttype) {
	    case TT_PLUS:
	    case TT_MINUS:
		next_token(info);
		parse_multiplicative(info, &rhs);
		switch (coerce_type(info, result, &rhs)) {
		    case VT_LONG:
			result->v_long = do_long_calculation(ttype,
				result->v_long, rhs.v_long);
			break;
		    case VT_DOUBLE:
			result->v_double = do_double_calculation(ttype,
				result->v_double, rhs.v_double);
			break;
		    case VT_INVALID:
			result->type = VT_INVALID;
			break;
		    case VT_VAR:     assert(false);
		}
		break;
	    default:
		return;
	}
    }
}

/* Parses a multiplicative expression.
 *   MultiplicativeExp := PrefixExp
 *                      | MultiplicativeExp "*" PrefixExp
 *                      | MultiplicativeExp "/" PrefixExp
 *                      | MultiplicativeExp "%" PrefixExp */
void parse_multiplicative(evalinfo_T *info, value_T *result)
{
    parse_prefix(info, result);
    for (;;) {
	tokentype_T ttype = info->token.type;
	value_T rhs;
	switch (ttype) {
	    case TT_ASTER:
	    case TT_SLASH:
	    case TT_PERCENT:
		next_token(info);
		parse_prefix(info, &rhs);
		switch (coerce_type(info, result, &rhs)) {
		    case VT_LONG:
			if (ttype != TT_ASTER && rhs.v_long == 0) {
			    xerror(0, Ngt("arithmetic: division by zero"));
			    info->error = true;
			    result->type = VT_INVALID;
			    break;
			}
			result->v_long = do_long_calculation(ttype,
				result->v_long, rhs.v_long);
			break;
		    case VT_DOUBLE:
#if DOUBLE_DIVISION_BY_ZERO_ERROR
			if (ttype != TT_ASTER && rhs.v_double == 0.0) {
			    xerror(0, Ngt("arithmetic: division by zero"));
			    info->error = true;
			    result->type = VT_INVALID;
			    break;
			}
#endif
			result->v_double = do_double_calculation(ttype,
				result->v_double, rhs.v_double);
			break;
		    case VT_INVALID:
			result->type = VT_INVALID;
			break;
		    case VT_VAR:     assert(false);
		}
		break;
	    default:
		return;
	}
    }
}

/* Parses a prefix expression.
 *   PrefixExp := PostfixExp
 *              | "++" PrefixExp | "--" PrefixExp
 *              | "+" PrefixExp | "-" PrefixExp
 *              | "~" PrefixExp | "!" PrefixExp */
void parse_prefix(evalinfo_T *info, value_T *result)
{
    tokentype_T ttype = info->token.type;
    switch (ttype) {
	case TT_PLUSPLUS:
	case TT_MINUSMINUS:
	    next_token(info);
	    parse_prefix(info, result);
	    if (result->type == VT_VAR) {
		word_T saveword = result->v_var;
		coerce_number(info, result);
		do_increment_or_decrement(ttype, result);
		if (!do_assignment(&saveword, result))
		    info->error = true, result->type = VT_INVALID;
	    } else if (result->type != VT_INVALID) {
		xerror(0, Ngt("arithmetic: `%ls' operator requires a variable"),
			(info->token.type == TT_PLUSPLUS) ? L"++" : L"--");
		info->error = true;
		result->type = VT_INVALID;
	    }
	    break;
	case TT_PLUS:
	case TT_MINUS:
	    next_token(info);
	    parse_prefix(info, result);
	    coerce_number(info, result);
	    if (ttype == TT_MINUS) {
		switch (result->type) {
		case VT_LONG:     result->v_long = -result->v_long;      break;
		case VT_DOUBLE:   result->v_double = -result->v_double;  break;
		case VT_INVALID:  break;
		default:          assert(false);
		}
	    }
	    break;
	case TT_TILDE:
	    next_token(info);
	    parse_prefix(info, result);
	    coerce_integer(info, result);
	    if (result->type == VT_LONG)
		result->v_long = ~result->v_long;
	    break;
	case TT_EXCL:
	    next_token(info);
	    parse_prefix(info, result);
	    coerce_number(info, result);
	    switch (result->type) {
		case VT_LONG:
		    result->v_long = !result->v_long;
		    break;
		case VT_DOUBLE:
		    result->type = VT_LONG;
		    result->v_long = !result->v_double;
		    break;
		case VT_INVALID:
		    break;
		default:
		    assert(false);
	    }
	    break;
	default:
	    parse_postfix(info, result);
	    break;
    }
}

/* Parses a postfix expression.
 *   PostfixExp := PrimaryExp
 *               | PostfixExp "++" | PostfixExp "--" */
void parse_postfix(evalinfo_T *info, value_T *result)
{
    parse_primary(info, result);
    for (;;) {
	switch (info->token.type) {
	    case TT_PLUSPLUS:
	    case TT_MINUSMINUS:
		if (result->type == VT_VAR) {
		    word_T saveword = result->v_var;
		    coerce_number(info, result);
		    value_T value = *result;
		    do_increment_or_decrement(info->token.type, &value);
		    if (!do_assignment(&saveword, &value))
			info->error = true, result->type = VT_INVALID;
		} else if (result->type != VT_INVALID) {
		    xerror(0, Ngt("arithmetic: "
				"`%ls' operator requires a variable"),
			    (info->token.type == TT_PLUSPLUS) ? L"++" : L"--");
		    info->error = true;
		    result->type = VT_INVALID;
		}
		next_token(info);
		break;
	    default:
		return;
	}
    }
}

/* Increment or decrement the specified value.
 * `ttype' must be either TT_PLUSPLUS or TT_MINUSMINUS and the `value' must be
 * `coerce_number'ed. */
void do_increment_or_decrement(tokentype_T ttype, value_T *value)
{
    if (ttype == TT_PLUSPLUS) {
	switch (value->type) {
	    case VT_LONG:    value->v_long++;    break;
	    case VT_DOUBLE:  value->v_double++;  break;
	    case VT_INVALID: break;
	    default:         assert(false);
	}
    } else {
	switch (value->type) {
	    case VT_LONG:    value->v_long--;    break;
	    case VT_DOUBLE:  value->v_double--;  break;
	    case VT_INVALID: break;
	    default:         assert(false);
	}
    }
}

/* Parses a primary expression.
 *   PrimaryExp := "(" AssignmentExp ")" | Number | Identifier */
void parse_primary(evalinfo_T *info, value_T *result)
{
    switch (info->token.type) {
	case TT_LPAREN:
	    next_token(info);
	    parse_assignment(info, result);
	    if (info->token.type == TT_RPAREN) {
		next_token(info);
	    } else {
		xerror(0, Ngt("arithmetic: `%ls' missing"), L")");
		info->error = true;
		result->type = VT_INVALID;
	    }
	    break;
	case TT_NUMBER:
	    parse_as_number(&info->token.word, result);
	    if (result->type == VT_INVALID)
		info->error = true;
	    next_token(info);
	    break;
	case TT_IDENTIFIER:
	    result->type = VT_VAR;
	    result->v_var = info->token.word;
	    next_token(info);
	    break;
	default:
	    xerror(0, Ngt("arithmetic: value not given"));
	    info->error = true;
	    result->type = VT_INVALID;
	    break;
    }
    if (info->parseonly)
	result->type = VT_INVALID;
}

/* Parses the specified `word' as a number literal. */
void parse_as_number(word_T *word, value_T *result)
{
    wchar_t w[word->length + 1];
    wcsncpy(w, word->contents, word->length);
    w[word->length] = L'\0';

    long longresult;
    if (xwcstol(w, 0, &longresult)) {
	result->type = VT_LONG;
	result->v_long = longresult;
	return;
    }
    if (!posixly_correct) {
	double doubleresult;
	wchar_t *end;
	errno = 0;
	doubleresult = wcstod(w, &end);
	if (!errno && !*end) {
	    result->type = VT_DOUBLE;
	    result->v_double = doubleresult;
	    return;
	}
    }
    xerror(0, Ngt("arithmetic: `%ls': not a valid number"), w);
    result->type = VT_INVALID;
    return;
}

/* If the value is of the VT_VAR type, change it into VT_LONG/VT_DOUBLE.
 * If the variable specified by the value is unset, it is assumed to be 0.
 * On parse error, false is returned and the value is unspecified. */
void coerce_number(evalinfo_T *info, value_T *value)
{
    if (value->type != VT_VAR)
	return;

    // TODO: arith: coerce_number: recursively parse the value of the variable
    //                             as an expression

    const wchar_t *v;
    {
	wchar_t name[value->v_var.length + 1];
	wmemcpy(name, value->v_var.contents, value->v_var.length);
	name[value->v_var.length] = L'\0';
	v = getvar(name);
    }
    if (!v || !v[0]) {
	value->type = VT_LONG;
	value->v_long = 0;
	return;
    }

    long longresult;
    if (xwcstol(v, 0, &longresult)) {
	value->type = VT_LONG;
	value->v_long = longresult;
	return;
    }
    if (!posixly_correct) {
	double doubleresult;
	wchar_t *end;
	errno = 0;
	doubleresult = wcstod(v, &end);
	if (!errno && !*end) {
	    value->type = VT_DOUBLE;
	    value->v_double = doubleresult;
	    return;
	}
    }
    xerror(0, Ngt("arithmetic: `%ls': not a valid number"), v);
    info->error = true;
    value->type = VT_INVALID;
    return;
}

/* Does `coerce_number' and if the result is of VT_DOUBLE, converts into
 * VT_LONG. */
void coerce_integer(evalinfo_T *info, value_T *value)
{
    coerce_number(info, value);
    if (value->type == VT_DOUBLE) {
	value->type = VT_LONG;
	value->v_long = (long) value->v_double;
    }
}

/* Unifies the types of the numeric values.
 * If a given value is VT_VAR, it is `coerce_number'ed.
 * Then, if one of the specified values is of VT_LONG and the other is of
 * VT_DOUBLE, the VT_LONG value is converted into VT_DOUBLE.
 * If the both values are of VT_LONG, VT_LONG is returned without conversion.
 * If either value is of VT_DOUBLE and the other is not VT_INVALID, VT_DOUBLE
 * is returned after conversion. If either value is VT_INVALID, the return
 * value is VT_INVALID. This function never returns VT_VAR. */
valuetype_T coerce_type(evalinfo_T *info, value_T *value1, value_T *value2)
{
    coerce_number(info, value1);
    coerce_number(info, value2);
    if (value1->type == value2->type)
	return value1->type;
    if (value1->type == VT_INVALID || value2->type == VT_INVALID)
	return VT_INVALID;
    assert(value1->type == VT_LONG || value1->type == VT_DOUBLE);
    assert(value2->type == VT_LONG || value2->type == VT_DOUBLE);

    value_T *value = (value1->type == VT_LONG) ? value1 : value2;
    value->type = VT_DOUBLE;
    value->v_double = (double) value->v_long;
    return VT_DOUBLE;
}

/* Moves to the next token.
 * The contents of `*info' is updated.
 * If there is no more token, `info->index' indicates the terminating null char
 * and a TT_NULL token is returned. On error, TT_INVALID is returned. */
void next_token(evalinfo_T *info)
{
    /* skip spaces */
    while (iswspace(info->exp[info->index]))
	info->index++;

    wchar_t c = info->exp[info->index];
    switch (c) {
	case L'\0':
	    info->token.type = TT_NULL;
	    return;
	case L'(':
	    info->token.type = TT_LPAREN;
	    info->index++;
	    break;
	case L')':
	    info->token.type = TT_RPAREN;
	    info->index++;
	    break;
	case L'~':
	    info->token.type = TT_TILDE;
	    info->index++;
	    break;
	case L'!':
	    switch (info->exp[info->index + 1]) {
		case L'=':
		    info->token.type = TT_EXCLEQUAL;
		    info->index += 2;
		    break;
		default:
		    info->token.type = TT_EXCL;
		    info->index += 1;
		    break;
	    }
	    break;
	case L'%':
	    switch (info->exp[info->index + 1]) {
		case L'=':
		    info->token.type = TT_PERCENTEQUAL;
		    info->index += 2;
		    break;
		default:
		    info->token.type = TT_PERCENT;
		    info->index += 1;
		    break;
	    }
	    break;
	case L'+':
	    switch (info->exp[info->index + 1]) {
		case L'+':
		    info->token.type = TT_PLUSPLUS;
		    info->index += 2;
		    break;
		case L'=':
		    info->token.type = TT_PLUSEQUAL;
		    info->index += 2;
		    break;
		default:
		    info->token.type = TT_PLUS;
		    info->index += 1;
		    break;
	    }
	    break;
	case L'-':
	    switch (info->exp[info->index + 1]) {
		case L'-':
		    info->token.type = TT_MINUSMINUS;
		    info->index += 2;
		    break;
		case L'=':
		    info->token.type = TT_MINUSEQUAL;
		    info->index += 2;
		    break;
		default:
		    info->token.type = TT_MINUS;
		    info->index += 1;
		    break;
	    }
	    break;
	case L'*':
	    switch (info->exp[info->index + 1]) {
		case L'=':
		    info->token.type = TT_ASTEREQUAL;
		    info->index += 2;
		    break;
		default:
		    info->token.type = TT_ASTER;
		    info->index += 1;
		    break;
	    }
	    break;
	case L'/':
	    switch (info->exp[info->index + 1]) {
		case L'=':
		    info->token.type = TT_SLASHEQUAL;
		    info->index += 2;
		    break;
		default:
		    info->token.type = TT_SLASH;
		    info->index += 1;
		    break;
	    }
	    break;
	case L'<':
	    {
		info->index++;
		bool twin = info->exp[info->index] == L'<';
		if (twin)
		    info->index++;
		bool equal = info->exp[info->index] == L'=';
		if (equal)
		    info->index++;
		info->token.type = twin
		    ? equal ? TT_LESSLESSEQUAL : TT_LESSLESS
		    : equal ? TT_LESSEQUAL : TT_LESS;
	    }
	    break;
	case L'>':
	    {
		info->index++;
		bool twin = info->exp[info->index] == L'>';
		if (twin)
		    info->index++;
		bool equal = info->exp[info->index] == L'=';
		if (equal)
		    info->index++;
		info->token.type = twin
		    ? equal ? TT_GREATERGREATEREQUAL : TT_GREATERGREATER
		    : equal ? TT_GREATEREQUAL : TT_GREATER;
	    }
	    break;
	case L'=':
	    switch (info->exp[info->index + 1]) {
		case L'=':
		    info->token.type = TT_EQUALEQUAL;
		    info->index += 2;
		    break;
		default:
		    info->token.type = TT_EQUAL;
		    info->index += 1;
		    break;
	    }
	    break;
	case L'&':
	    switch (info->exp[info->index + 1]) {
		case L'&':
		    info->token.type = TT_AMPAMP;
		    info->index += 2;
		    break;
		case L'=':
		    info->token.type = TT_AMPEQUAL;
		    info->index += 2;
		    break;
		default:
		    info->token.type = TT_AMP;
		    info->index += 1;
		    break;
	    }
	    break;
	case L'|':
	    switch (info->exp[info->index + 1]) {
		case L'|':
		    info->token.type = TT_PIPEPIPE;
		    info->index += 2;
		    break;
		case L'=':
		    info->token.type = TT_PIPEEQUAL;
		    info->index += 2;
		    break;
		default:
		    info->token.type = TT_PIPE;
		    info->index += 1;
		    break;
	    }
	    break;
	case L'^':
	    switch (info->exp[info->index + 1]) {
		case L'=':
		    info->token.type = TT_HATEQUAL;
		    info->index += 2;
		    break;
		default:
		    info->token.type = TT_HAT;
		    info->index += 1;
		    break;
	    }
	    break;
	case L'?':
	    info->token.type = TT_QUESTION;
	    info->index++;
	    break;
	case L':':
	    info->token.type = TT_COLON;
	    info->index++;
	    break;
	case L'_':
	    goto parse_identifier;
	default:
	    assert(!iswspace(c));
	    if (c == L'.' || iswdigit(c)) {
		/* number */
		size_t startindex = info->index;
parse_num:
		do
		    c = info->exp[++info->index];
		while (c == L'.' || iswalnum(c));
		if (c == L'+' || c == L'-') {
		    c = info->exp[info->index - 1];
		    if (c == L'E' || c == L'e')
			goto parse_num;
		}
		info->token.type = TT_NUMBER;
		info->token.word.contents = &info->exp[startindex];
		info->token.word.length = info->index - startindex;
	    } else if (iswalpha(c)) {
parse_identifier:;
		size_t startindex = info->index;
		do
		    c = info->exp[++info->index];
		while (c == L'_' || iswalnum(c));
		info->token.type = TT_IDENTIFIER;
		info->token.word.contents = &info->exp[startindex];
		info->token.word.length = info->index - startindex;
	    } else {
		xerror(0, Ngt("arithmetic: invalid character `%lc'"),
			(wint_t) c);
		info->error = true;
		info->token.type = TT_INVALID;
	    }
	    break;
    }
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
