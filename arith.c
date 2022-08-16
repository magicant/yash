/* Yash: yet another shell */
/* arith.c: arithmetic expansion */
/* (C) 2007-2022 magicant */

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
#include "arith.h"
#include <assert.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>
#include <sys/types.h>
#include <wctype.h>
#include "option.h"
#include "strbuf.h"
#include "util.h"
#include "variable.h"


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

typedef enum atokentype_T {
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
} atokentype_T;
typedef struct atoken_T {
    atokentype_T type;
    word_T word;  /* valid only for numbers and identifiers */
} atoken_T;

typedef struct evalinfo_T {
    const wchar_t *exp;  /* expression to parse and calculate */
    size_t index;        /* index of next token */
    atoken_T atoken;     /* current token */
    bool parseonly;      /* only parse the expression: don't calculate */
    bool error;          /* true if there is an error */
    char *savelocale;    /* original LC_NUMERIC locale */
} evalinfo_T;

static void evaluate(
	const wchar_t *exp, value_T *result, evalinfo_T *info, bool coerce)
    __attribute__((nonnull));
static void parse_assignment(evalinfo_T *info, value_T *result)
    __attribute__((nonnull));
static bool do_assignment(const word_T *word, const value_T *value)
    __attribute__((nonnull));
static wchar_t *value_to_string(const value_T *value)
    __attribute__((nonnull,malloc,warn_unused_result));
static bool do_binary_calculation(
	evalinfo_T *info, atokentype_T ttype,
	value_T *lhs, value_T *rhs, value_T *result)
    __attribute__((nonnull));
static bool do_long_calculation1(
	atokentype_T ttype, long v1, long v2, long *result)
    __attribute__((nonnull,warn_unused_result));
static bool do_long_calculation2(
	atokentype_T ttype, long v1, long v2, long *result)
    __attribute__((nonnull,warn_unused_result));
static long do_long_comparison(atokentype_T ttype, long v1, long v2)
    __attribute__((const,warn_unused_result));
static bool do_double_calculation(
	atokentype_T ttype, double v1, double v2, double *result)
    __attribute__((nonnull,warn_unused_result));
static long do_double_comparison(atokentype_T ttype, double v1, double v2);
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
static bool do_increment_or_decrement(atokentype_T ttype, value_T *value)
    __attribute__((nonnull,warn_unused_result));
static void parse_primary(evalinfo_T *info, value_T *result)
    __attribute__((nonnull));
static void parse_as_number(evalinfo_T *info, value_T *result)
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
static bool long_mul_will_overflow(long v1, long v2)
    __attribute__((const,warn_unused_result));


/* Evaluates the specified string as an arithmetic expression.
 * The argument string is freed in this function.
 * The result is converted into a string and returned as a newly-malloced
 * string. On error, an error message is printed to the standard error and NULL
 * is returned. */
wchar_t *evaluate_arithmetic(wchar_t *exp)
{
    value_T result;
    evalinfo_T info;

    evaluate(exp, &result, &info, posixly_correct);

    wchar_t *resultstr;
    if (info.error) {
	resultstr = NULL;
    } else if (info.atoken.type == TT_NULL) {
	resultstr = value_to_string(&result);
    } else {
	if (info.atoken.type != TT_INVALID)
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

    evaluate(exp, &result, &info, true);

    bool ok;
    if (info.error) {
	ok = false;
    } else if (info.atoken.type == TT_NULL) {
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
	    xerror(0, Ngt("the index is not an integer"));
	    ok = false;
	}
    } else {
	if (info.atoken.type != TT_INVALID)
	    xerror(0, Ngt("arithmetic: invalid syntax"));
	ok = false;
    }
    free(exp);
    return ok;
}

void evaluate(
	const wchar_t *exp, value_T *result, evalinfo_T *info, bool coerce)
{
    info->exp = exp;
    info->index = 0;
    info->parseonly = false;
    info->error = false;
    info->savelocale = xstrdup(setlocale(LC_NUMERIC, NULL));

    next_token(info);
    parse_assignment(info, result);
    if (coerce)
	coerce_number(info, result);

    free(info->savelocale);
}

/* Parses an assignment expression.
 *   AssignmentExp := ConditionalExp
 *                  | ConditionalExp AssignmentOperator AssignmentExp */
void parse_assignment(evalinfo_T *info, value_T *result)
{
    parse_conditional(info, result);

    atokentype_T ttype = info->atoken.type;
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
		    if (!do_binary_calculation(
				info, ttype, result, &rhs, result))
			break;
		    if (!do_assignment(&saveword, result))
			info->error = true, result->type = VT_INVALID;
		} else if (result->type != VT_INVALID) {
		    /* TRANSLATORS: This error message is shown when the target
		     * of an assignment is not a variable. */
		    xerror(0, Ngt("arithmetic: cannot assign to a number"));
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
    if (vstr == NULL)
	return false;

    wchar_t name[word->length + 1];
    wmemcpy(name, word->contents, word->length);
    name[word->length] = L'\0';
    return set_variable(name, vstr, SCOPE_GLOBAL, false);
}

/* Converts `value' to a newly-malloced wide string.
 * Returns NULL on error. */
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
		if (var != NULL)
		    return xwcsdup(var);
		if (shopt_unset)
		    return malloc_wprintf(L"%ld", 0L);
		xerror(0, Ngt("arithmetic: parameter `%ls' is not set"), name);
		return NULL;
	    }
    }
    assert(false);
}

/* Applies the binary operation defined by token `ttype' to operands `lhs' and
 * `rhs'. The operands may be modified as a result of coercion. The result is
 * assigned to `*result' unless there is an error.
 * Returns true iff successful. */
bool do_binary_calculation(
	evalinfo_T *info, atokentype_T ttype,
	value_T *lhs, value_T *rhs, value_T *result)
{
    switch (ttype) {
	case TT_ASTER:  case TT_ASTEREQUAL:
	case TT_SLASH:  case TT_SLASHEQUAL:
	case TT_PERCENT:  case TT_PERCENTEQUAL:
	case TT_PLUS:  case TT_PLUSEQUAL:
	case TT_MINUS:  case TT_MINUSEQUAL:
	    result->type = coerce_type(info, lhs, rhs);
	    switch (result->type) {
		case VT_LONG:
		    if (!do_long_calculation1(ttype, lhs->v_long, rhs->v_long,
				&result->v_long)) {
			info->error = true;
			result->type = VT_INVALID;
			return false;
		    }
		    break;
		case VT_DOUBLE:
		    if (!do_double_calculation(ttype, lhs->v_double,
				rhs->v_double, &result->v_double)) {
			info->error = true;
			result->type = VT_INVALID;
			return false;
		    }
		    break;
		case VT_VAR:
		    assert(false);
		case VT_INVALID:
		    break;
	    }
	    break;
	case TT_LESSLESS:  case TT_LESSLESSEQUAL:
	case TT_GREATERGREATER:  case TT_GREATERGREATEREQUAL:
	case TT_AMP:  case TT_AMPEQUAL:
	case TT_HAT:  case TT_HATEQUAL:
	case TT_PIPE:  case TT_PIPEEQUAL:
	    coerce_integer(info, lhs);
	    coerce_integer(info, rhs);
	    if (lhs->type == VT_LONG && rhs->type == VT_LONG) {
		if (do_long_calculation2(
			    ttype, lhs->v_long, rhs->v_long, &result->v_long)) {
		    result->type = VT_LONG;
		} else {
		    info->error = true;
		    result->type = VT_INVALID;
		    return false;
		}
	    } else {
		result->type = VT_INVALID;
		return false;
	    }
	    break;
	case TT_EQUAL:
	    *result = *rhs;
	    break;
	default:
	    assert(false);
    }
    return true;
}

/* Applies binary operator `ttype' to the given operands `v1' and `v2'.
 * If successful, assigns the result to `*result' and returns true.
 * Otherwise, prints an error message and returns false. */
bool do_long_calculation1(atokentype_T ttype, long v1, long v2, long *result)
{
    switch (ttype) {
	case TT_PLUS:  case TT_PLUSEQUAL:
	    if (v2 >= 0 ? LONG_MAX - v2 < v1 : v1 < LONG_MIN - v2)
		goto overflow;
	    *result = v1 + v2;
	    return true;
	case TT_MINUS:  case TT_MINUSEQUAL:
	    if (v2 < 0 ? LONG_MAX + v2 < v1 : v1 < LONG_MIN + v2)
		goto overflow;
	    *result = v1 - v2;
	    return true;
	case TT_ASTER:  case TT_ASTEREQUAL:
	    if (long_mul_will_overflow(v1, v2))
		goto overflow;
	    *result = v1 * v2;
	    return true;
	case TT_SLASH:  case TT_SLASHEQUAL:
	    if (v2 == 0)
		goto division_by_zero;
	    if (v1 == LONG_MIN && v2 == -1)
		goto overflow;
	    *result = v1 / v2;
	    return true;
	case TT_PERCENT:  case TT_PERCENTEQUAL:
	    if (v2 == 0)
		goto division_by_zero;
	    if (v1 == LONG_MIN && v2 == -1)
		goto overflow;
	    *result = v1 % v2;
	    return true;
	default:
	    assert(false);
    }

overflow:
    xerror(0, Ngt("arithmetic: overflow"));
    return false;
division_by_zero:
    xerror(0, Ngt("arithmetic: division by zero"));
    return false;
}

/* Applies binary operator `ttype' to the given operands `v1' and `v2'.
 * If successful, assigns the result to `*result' and returns true.
 * Otherwise, prints an error message and returns false. */
bool do_long_calculation2(atokentype_T ttype, long v1, long v2, long *result)
{
    switch (ttype) {
	case TT_LESSLESS:  case TT_LESSLESSEQUAL:
	    if (v1 < 0)
		goto negative_left_shift;
	    if (v2 < 0 || v2 >= LONG_BIT)
		goto invalid_shift_width;
	    unsigned long u1 = (unsigned long) v1;
	    if ((u1 << v2 & (unsigned long) LONG_MAX) >> v2 != u1)
		goto overflow;
	    *result = v1 << v2;
	    return true;
	case TT_GREATERGREATER:  case TT_GREATERGREATEREQUAL:
	    if (v2 < 0 || v2 >= LONG_BIT)
		goto invalid_shift_width;
	    *result = v1 >> v2;
	    return true;
	case TT_AMP:  case TT_AMPEQUAL:
	    *result = v1 & v2;
	    return true;
	case TT_HAT:  case TT_HATEQUAL:
	    *result = v1 ^ v2;
	    return true;
	case TT_PIPE:  case TT_PIPEEQUAL:
	    *result = v1 | v2;
	    return true;
	default:
	    assert(false);
    }

overflow:
    xerror(0, Ngt("arithmetic: overflow"));
    return false;
negative_left_shift:
    xerror(0, Ngt("arithmetic: negative value cannot be shifted to left"));
    return false;
invalid_shift_width:
    xerror(0, Ngt("arithmetic: invalid shift width"));
    return false;
}

/* Applies binary operator `ttype' to the given operands `v1' and `v2'. */
long do_long_comparison(atokentype_T ttype, long v1, long v2)
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

/* Applies binary operator `ttype' to the given operands `v1' and `v2'.
 * If successful, assigns the result to `*result' and returns true.
 * Otherwise, prints an error message and returns false. */
bool do_double_calculation(
	atokentype_T ttype, double v1, double v2, double *result)
{
    switch (ttype) {
	case TT_PLUS:  case TT_PLUSEQUAL:
	    *result = v1 + v2;
	    return true;
	case TT_MINUS:  case TT_MINUSEQUAL:
	    *result = v1 - v2;
	    return true;
	case TT_ASTER:  case TT_ASTEREQUAL:
	    *result = v1 * v2;
	    return true;
	case TT_SLASH:  case TT_SLASHEQUAL:
#if DOUBLE_DIVISION_BY_ZERO_ERROR
	    if (v2 == 0.0)
		goto division_by_zero;
#endif
	    *result = v1 / v2;
	    return true;
	case TT_PERCENT:  case TT_PERCENTEQUAL:
#if DOUBLE_DIVISION_BY_ZERO_ERROR
	    if (v2 == 0.0)
		goto division_by_zero;
#endif
	    *result = fmod(v1, v2);
	    return true;
	default:
	    assert(false);
    }

#if DOUBLE_DIVISION_BY_ZERO_ERROR
division_by_zero:
    xerror(0, Ngt("arithmetic: division by zero"));
    return false;
#endif
}

/* Does double comparison according to the specified operator token. */
long do_double_comparison(atokentype_T ttype, double v1, double v2)
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

    for (;;) {
	value_T dummy;
	value_T *result2;

	result2 = info->parseonly ? &dummy : result;
	parse_logical_or(info, result2);
	if (info->atoken.type != TT_QUESTION)
	    break;

	bool cond, valid = true;

	coerce_number(info, result2);
	next_token(info);
	switch (result2->type) {
	    case VT_INVALID:  valid = false, cond = true;   break;
	    case VT_LONG:     cond = result2->v_long;       break;
	    case VT_DOUBLE:   cond = result2->v_double;     break;
	    default:          assert(false);
	}

	bool saveparseonly2 = info->parseonly || !valid;
	info->parseonly = saveparseonly2 || !cond;

	result2 = info->parseonly ? &dummy : result;
	parse_assignment(info, result2);
	if (info->atoken.type != TT_COLON) {
	    xerror(0, Ngt("arithmetic: `%ls' is missing"), L":");
	    info->error = true;
	    result->type = VT_INVALID;
	    break;
	}

	next_token(info);
	info->parseonly = saveparseonly2 || cond;
    }

    info->parseonly = saveparseonly;
    if (info->parseonly)
	result->type = VT_INVALID;
}

/* Parses a logical OR expression.
 *   LogicalOrExp := LogicalAndExp | LogicalOrExp "||" LogicalAndExp */
void parse_logical_or(evalinfo_T *info, value_T *result)
{
    bool saveparseonly = info->parseonly;

    parse_logical_and(info, result);
    while (info->atoken.type == TT_PIPEPIPE) {
	bool value, valid = true;

	coerce_number(info, result);
	next_token(info);
	switch (result->type) {
	    case VT_INVALID: valid = false, value = true;  break;
	    case VT_LONG:    value = result->v_long;       break;
	    case VT_DOUBLE:  value = result->v_double;     break;
	    default:         assert(false);
	}

	info->parseonly |= value;
	parse_logical_and(info, result);
	coerce_number(info, result);
	if (!value) switch (result->type) {
	    case VT_INVALID: valid = false;             break;
	    case VT_LONG:    value = result->v_long;    break;
	    case VT_DOUBLE:  value = result->v_double;  break;
	    default:         assert(false);
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
    while (info->atoken.type == TT_AMPAMP) {
	bool value, valid = true;

	coerce_number(info, result);
	next_token(info);
	switch (result->type) {
	    case VT_INVALID: valid = false, value = false;  break;
	    case VT_LONG:    value = result->v_long;        break;
	    case VT_DOUBLE:  value = result->v_double;      break;
	    default:         assert(false);
	}

	info->parseonly |= !value;
	parse_inclusive_or(info, result);
	coerce_number(info, result);
	if (value) switch (result->type) {
	    case VT_INVALID: valid = false;             break;
	    case VT_LONG:    value = result->v_long;    break;
	    case VT_DOUBLE:  value = result->v_double;  break;
	    default:         assert(false);
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
	atokentype_T ttype = info->atoken.type;
	value_T rhs;
	switch (ttype) {
	    case TT_PIPE:
		next_token(info);
		parse_exclusive_or(info, &rhs);
		do_binary_calculation(info, TT_PIPE, result, &rhs, result);
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
	atokentype_T ttype = info->atoken.type;
	value_T rhs;
	switch (ttype) {
	    case TT_HAT:
		next_token(info);
		parse_and(info, &rhs);
		do_binary_calculation(info, TT_HAT, result, &rhs, result);
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
	atokentype_T ttype = info->atoken.type;
	value_T rhs;
	switch (ttype) {
	    case TT_AMP:
		next_token(info);
		parse_equality(info, &rhs);
		do_binary_calculation(info, TT_AMP, result, &rhs, result);
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
	atokentype_T ttype = info->atoken.type;
	value_T rhs;
	switch (ttype) {
	    case TT_EQUALEQUAL:
	    case TT_EXCLEQUAL:
		next_token(info);
		parse_relational(info, &rhs);
		switch (coerce_type(info, result, &rhs)) {
		    case VT_LONG:
			result->v_long = do_long_comparison(ttype,
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
		    case VT_VAR:
			assert(false);
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
	atokentype_T ttype = info->atoken.type;
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
			result->v_long = do_long_comparison(ttype,
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
		    case VT_VAR:
			assert(false);
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
	atokentype_T ttype = info->atoken.type;
	value_T rhs;
	switch (ttype) {
	    case TT_LESSLESS:
	    case TT_GREATERGREATER:
		next_token(info);
		parse_additive(info, &rhs);
		do_binary_calculation(info, ttype, result, &rhs, result);
		break;
	    default:
		return;
	}
    }
}

/* Parses an additive expression.
 *   AdditiveExp := MultiplicativeExp
 *                | AdditiveExp "+" MultiplicativeExp
 *                | AdditiveExp "-" MultiplicativeExp */
void parse_additive(evalinfo_T *info, value_T *result)
{
    parse_multiplicative(info, result);
    for (;;) {
	atokentype_T ttype = info->atoken.type;
	value_T rhs;
	switch (ttype) {
	    case TT_PLUS:
	    case TT_MINUS:
		next_token(info);
		parse_multiplicative(info, &rhs);
		do_binary_calculation(info, ttype, result, &rhs, result);
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
	atokentype_T ttype = info->atoken.type;
	value_T rhs;
	switch (ttype) {
	    case TT_ASTER:
	    case TT_SLASH:
	    case TT_PERCENT:
		next_token(info);
		parse_prefix(info, &rhs);
		do_binary_calculation(info, ttype, result, &rhs, result);
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
    atokentype_T ttype = info->atoken.type;
    switch (ttype) {
	case TT_PLUSPLUS:
	case TT_MINUSMINUS:
	    next_token(info);
	    parse_prefix(info, result);
	    if (posixly_correct) {
		xerror(0, Ngt("arithmetic: operator `%ls' is not supported"),
			(ttype == TT_PLUSPLUS) ? L"++" : L"--");
		info->error = true;
		result->type = VT_INVALID;
	    } else if (result->type == VT_VAR) {
		word_T saveword = result->v_var;
		coerce_number(info, result);
		if (!do_increment_or_decrement(ttype, result) ||
			!do_assignment(&saveword, result))
		    info->error = true, result->type = VT_INVALID;
	    } else if (result->type != VT_INVALID) {
		/* TRANSLATORS: This error message is shown when the operand of
		 * the "++" or "--" operator is not a variable. */
		xerror(0, Ngt("arithmetic: operator `%ls' requires a variable"),
			(info->atoken.type == TT_PLUSPLUS) ? L"++" : L"--");
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
		case VT_LONG:
#if LONG_MIN < -LONG_MAX
		    if (result->v_long == LONG_MIN) {
			xerror(0, Ngt("arithmetic: overflow"));
			info->error = true;
			result->type = VT_INVALID;
			break;
		    }
#endif
		    result->v_long = -result->v_long;
		    break;
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
	switch (info->atoken.type) {
	    case TT_PLUSPLUS:
	    case TT_MINUSMINUS:
		if (posixly_correct) {
		    xerror(0,
			    Ngt("arithmetic: operator `%ls' is not supported"),
			    (info->atoken.type == TT_PLUSPLUS) ? L"++" : L"--");
		    info->error = true;
		    result->type = VT_INVALID;
		} else if (result->type == VT_VAR) {
		    word_T saveword = result->v_var;
		    coerce_number(info, result);
		    value_T value = *result;
		    if (!do_increment_or_decrement(info->atoken.type, &value) ||
			    !do_assignment(&saveword, &value)) {
			info->error = true;
			result->type = VT_INVALID;
		    }
		} else if (result->type != VT_INVALID) {
		    xerror(0, Ngt("arithmetic: "
				"operator `%ls' requires a variable"),
			    (info->atoken.type == TT_PLUSPLUS) ? L"++" : L"--");
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
 * `coerce_number'ed.
 * Returns false on error. */
bool do_increment_or_decrement(atokentype_T ttype, value_T *value)
{
    if (ttype == TT_PLUSPLUS) {
	switch (value->type) {
	    case VT_LONG:
		if (value->v_long == LONG_MAX) {
		    xerror(0, Ngt("arithmetic: overflow"));
		    return false;
		}
		value->v_long++;
		break;
	    case VT_DOUBLE:  value->v_double++;  break;
	    case VT_INVALID: break;
	    default:         assert(false);
	}
    } else {
	switch (value->type) {
	    case VT_LONG:
		if (value->v_long == LONG_MIN) {
		    xerror(0, Ngt("arithmetic: overflow"));
		    return false;
		}
		value->v_long--;
		break;
	    case VT_DOUBLE:  value->v_double--;  break;
	    case VT_INVALID: break;
	    default:         assert(false);
	}
    }
    return true;
}

/* Parses a primary expression.
 *   PrimaryExp := "(" AssignmentExp ")" | Number | Identifier */
void parse_primary(evalinfo_T *info, value_T *result)
{
    switch (info->atoken.type) {
	case TT_LPAREN:
	    next_token(info);
	    parse_assignment(info, result);
	    if (info->atoken.type == TT_RPAREN) {
		next_token(info);
	    } else {
		xerror(0, Ngt("arithmetic: `%ls' is missing"), L")");
		info->error = true;
		result->type = VT_INVALID;
	    }
	    break;
	case TT_NUMBER:
	    parse_as_number(info, result);
	    next_token(info);
	    break;
	case TT_IDENTIFIER:
	    result->type = VT_VAR;
	    result->v_var = info->atoken.word;
	    next_token(info);
	    break;
	default:
	    xerror(0, Ngt("arithmetic: a value is missing"));
	    info->error = true;
	    result->type = VT_INVALID;
	    break;
    }
    if (info->parseonly)
	result->type = VT_INVALID;
}

/* Parses the current word as a number literal. */
void parse_as_number(evalinfo_T *info, value_T *result)
{
    word_T *word = &info->atoken.word;
    wchar_t wordstr[word->length + 1];
    wcsncpy(wordstr, word->contents, word->length);
    wordstr[word->length] = L'\0';

    long longresult;
    if (xwcstol(wordstr, 0, &longresult)) {
	result->type = VT_LONG;
	result->v_long = longresult;
	return;
    }
    if (!posixly_correct) {
	double doubleresult;
	wchar_t *end;
	setlocale(LC_NUMERIC, "C");
	errno = 0;
	doubleresult = wcstod(wordstr, &end);
	bool ok = (errno == 0 && *end == L'\0');
	setlocale(LC_NUMERIC, info->savelocale);
	if (ok) {
	    result->type = VT_DOUBLE;
	    result->v_double = doubleresult;
	    return;
	}
    }
    xerror(0, Ngt("arithmetic: `%ls' is not a valid number"), wordstr);
    info->error = true;
    result->type = VT_INVALID;
}

/* If the value is of the VT_VAR type, change it into VT_LONG/VT_DOUBLE.
 * If the variable specified by the value is unset, it is assumed 0.
 * On parse error, false is returned and the value is unspecified. */
void coerce_number(evalinfo_T *info, value_T *value)
{
    if (value->type != VT_VAR)
	return;

    const wchar_t *varvalue;
    {
	word_T *name = &value->v_var;
	wchar_t namestr[name->length + 1];
	wmemcpy(namestr, name->contents, name->length);
	namestr[name->length] = L'\0';
	varvalue = getvar(namestr);

	if (varvalue == NULL && !shopt_unset) {
	    xerror(0, Ngt("arithmetic: parameter `%ls' is not set"), namestr);
	    info->error = true;
	    value->type = VT_INVALID;
	    return;
	}
    }
    if (varvalue == NULL || varvalue[0] == L'\0') {
	value->type = VT_LONG;
	value->v_long = 0;
	return;
    }

    long longresult;
    if (xwcstol(varvalue, 0, &longresult)) {
	value->type = VT_LONG;
	value->v_long = longresult;
	return;
    }
    if (!posixly_correct) {
	double doubleresult;
	wchar_t *end;
	errno = 0;
	doubleresult = wcstod(varvalue, &end);
	if (errno == 0 && *end == L'\0') {
	    value->type = VT_DOUBLE;
	    value->v_double = doubleresult;
	    return;
	}
    }
    xerror(0, Ngt("arithmetic: `%ls' is not a valid number"), varvalue);
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
 * First, the given values are `coerce_number'ed.
 * Then, if one of the values is of VT_LONG and the other is of VT_DOUBLE, the
 * VT_LONG value is converted into VT_DOUBLE.
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

    value_T *value_to_coerce = (value1->type == VT_LONG) ? value1 : value2;
    value_to_coerce->type = VT_DOUBLE;
    value_to_coerce->v_double = (double) value_to_coerce->v_long;
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
	    info->atoken.type = TT_NULL;
	    return;
	case L'(':
	    info->atoken.type = TT_LPAREN;
	    info->index++;
	    break;
	case L')':
	    info->atoken.type = TT_RPAREN;
	    info->index++;
	    break;
	case L'~':
	    info->atoken.type = TT_TILDE;
	    info->index++;
	    break;
	case L'!':
	    switch (info->exp[info->index + 1]) {
		case L'=':
		    info->atoken.type = TT_EXCLEQUAL;
		    info->index += 2;
		    break;
		default:
		    info->atoken.type = TT_EXCL;
		    info->index += 1;
		    break;
	    }
	    break;
	case L'%':
	    switch (info->exp[info->index + 1]) {
		case L'=':
		    info->atoken.type = TT_PERCENTEQUAL;
		    info->index += 2;
		    break;
		default:
		    info->atoken.type = TT_PERCENT;
		    info->index += 1;
		    break;
	    }
	    break;
	case L'+':
	    switch (info->exp[info->index + 1]) {
		case L'+':
		    info->atoken.type = TT_PLUSPLUS;
		    info->index += 2;
		    break;
		case L'=':
		    info->atoken.type = TT_PLUSEQUAL;
		    info->index += 2;
		    break;
		default:
		    info->atoken.type = TT_PLUS;
		    info->index += 1;
		    break;
	    }
	    break;
	case L'-':
	    switch (info->exp[info->index + 1]) {
		case L'-':
		    info->atoken.type = TT_MINUSMINUS;
		    info->index += 2;
		    break;
		case L'=':
		    info->atoken.type = TT_MINUSEQUAL;
		    info->index += 2;
		    break;
		default:
		    info->atoken.type = TT_MINUS;
		    info->index += 1;
		    break;
	    }
	    break;
	case L'*':
	    switch (info->exp[info->index + 1]) {
		case L'=':
		    info->atoken.type = TT_ASTEREQUAL;
		    info->index += 2;
		    break;
		default:
		    info->atoken.type = TT_ASTER;
		    info->index += 1;
		    break;
	    }
	    break;
	case L'/':
	    switch (info->exp[info->index + 1]) {
		case L'=':
		    info->atoken.type = TT_SLASHEQUAL;
		    info->index += 2;
		    break;
		default:
		    info->atoken.type = TT_SLASH;
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
		info->atoken.type = twin
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
		info->atoken.type = twin
		    ? equal ? TT_GREATERGREATEREQUAL : TT_GREATERGREATER
		    : equal ? TT_GREATEREQUAL : TT_GREATER;
	    }
	    break;
	case L'=':
	    switch (info->exp[info->index + 1]) {
		case L'=':
		    info->atoken.type = TT_EQUALEQUAL;
		    info->index += 2;
		    break;
		default:
		    info->atoken.type = TT_EQUAL;
		    info->index += 1;
		    break;
	    }
	    break;
	case L'&':
	    switch (info->exp[info->index + 1]) {
		case L'&':
		    info->atoken.type = TT_AMPAMP;
		    info->index += 2;
		    break;
		case L'=':
		    info->atoken.type = TT_AMPEQUAL;
		    info->index += 2;
		    break;
		default:
		    info->atoken.type = TT_AMP;
		    info->index += 1;
		    break;
	    }
	    break;
	case L'|':
	    switch (info->exp[info->index + 1]) {
		case L'|':
		    info->atoken.type = TT_PIPEPIPE;
		    info->index += 2;
		    break;
		case L'=':
		    info->atoken.type = TT_PIPEEQUAL;
		    info->index += 2;
		    break;
		default:
		    info->atoken.type = TT_PIPE;
		    info->index += 1;
		    break;
	    }
	    break;
	case L'^':
	    switch (info->exp[info->index + 1]) {
		case L'=':
		    info->atoken.type = TT_HATEQUAL;
		    info->index += 2;
		    break;
		default:
		    info->atoken.type = TT_HAT;
		    info->index += 1;
		    break;
	    }
	    break;
	case L'?':
	    info->atoken.type = TT_QUESTION;
	    info->index++;
	    break;
	case L':':
	    info->atoken.type = TT_COLON;
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
		info->atoken.type = TT_NUMBER;
		info->atoken.word.contents = &info->exp[startindex];
		info->atoken.word.length = info->index - startindex;
	    } else if (iswalpha(c)) {
parse_identifier:;
		size_t startindex = info->index;
		do
		    c = info->exp[++info->index];
		while (c == L'_' || iswalnum(c));
		info->atoken.type = TT_IDENTIFIER;
		info->atoken.word.contents = &info->exp[startindex];
		info->atoken.word.length = info->index - startindex;
	    } else {
		xerror(0, Ngt("arithmetic: `%lc' is not "
			    "a valid number or operator"), (wint_t) c);
		info->error = true;
		info->atoken.type = TT_INVALID;
	    }
	    break;
    }
}

/* Tests whether the multiplication of the given two long values will overflow.
 */
bool long_mul_will_overflow(long v1, long v2)
{
    if (v1 == 0 || v1 == 1 || v2 == 0 || v2 == 1)
	return false;
#if LONG_MIN < -LONG_MAX
    if (v1 == LONG_MIN || v2 == LONG_MIN)
	return true;
#endif
    unsigned long u1 = labs(v1), u2 = labs(v2);
    unsigned long prod = u1 * u2;
#if LONG_MIN < -LONG_MAX
    if (prod == (unsigned long) LONG_MIN)
	return ((v1 >= 0) == (v2 >= 0)) || prod / u2 != u1;
#endif
    return (prod & (unsigned long) LONG_MAX) / u2 != u1;
}

/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
