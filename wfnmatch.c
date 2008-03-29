/* Yash: yet another shell */
/* strmatch.c: fnmatch for wide-character strings */
/* © 2007-2008 magicant */

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
#include <fnmatch.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>
#include "strbuf.h"
#include "wfnmatch.h"

#define NOESCAPE (!!(flags & WFNM_NOESCAPE))
#define PATHNAME (!!(flags & WFNM_PATHNAME))
#define PERIOD   (!!(flags & WFNM_PERIOD))
#define CASEFOLD (!!(flags & WFNM_CASEFOLD))

#define FOLD(c) (CASEFOLD ? towlower(c) : (wint_t) (c))

__attribute__((nonnull))
static wchar_t *skip_bracket(const wchar_t *pat, enum wfnmflags flags);
__attribute__((nonnull))
static size_t wfnmatchn(const wchar_t *p, const wchar_t *s, size_t lendiff,
	enum wfnmflags flags, enum wfnmtype type);
__attribute__((nonnull))
static size_t match_bracket(
	const wchar_t **pp, const wchar_t *s, enum wfnmflags flags);
__attribute__((nonnull))
static wchar_t *check_char_class(const wchar_t *p, wint_t c, bool *match);


/* ワイド文字列に対するパターンマッチを行う。
 * pat:    マッチさせるパターン。
 * str:    マッチの対象となる文字列。
 * flags:  マッチの種類を指定するフラグ。以下の値のビットごとの OR。
 *         WFNM_NOESCAPE: バックスラッシュをエスケープとして扱わない
 *         WFNM_PATHNAME: L'/' を L"*" や L"?" にマッチさせない。
 *         WFNM_PERIOD: 先頭の L'.' を L"*" や L"?" にマッチさせない。
 *         WFNM_CASEFOLD: 大文字小文字を区別しない。
 * type:   マッチ結果の長さの優先順位を指定する値。
 *         WFNM_WHOLE なら pat が str 全体にマッチする場合のみ成功とする。
 *         WFNM_LONGEST なら str の先頭部分にできるだけ長くマッチさせる。
 *         WFNM_SHORTEST なら str の先頭部分にできるだけ短くマッチさせる。
 * 戻り値: マッチしたら、その str の先頭部分の文字数。
 *         マッチしなかったら WFNM_NOMATCH、エラーなら WFNM_ERROR。
 * エラーが返るのは基本的にパターンが不正な場合だが、パターンが不正でも常に
 * エラーを返すわけではない。 */
size_t wfnmatch(const wchar_t *pat, const wchar_t *str,
	enum wfnmflags flags, enum wfnmtype type)
{
    return wfnmatchl(pat, str, flags, type, shortest_match_length(pat, flags));
}

/* ワイド文字列に対するパターンマッチを行う。
 * pat:    マッチさせるパターン。
 * str:    マッチの対象となる文字列。
 * flags:  マッチの種類を指定するフラグ。以下の値のビットごとの OR。
 *         WFNM_NOESCAPE: バックスラッシュをエスケープとして扱わない
 *         WFNM_PATHNAME: L'/' を L"*" や L"?" にマッチさせない。
 *         WFNM_PERIOD: 先頭の L'.' を L"*" や L"?" にマッチさせない。
 *         WFNM_CASEFOLD: 大文字小文字を区別しない。
 * type:   マッチ結果の長さの優先順位を指定する値。
 *         WFNM_WHOLE なら pat が str 全体にマッチする場合のみ成功とする。
 *         WFNM_LONGEST なら str の先頭部分にできるだけ長くマッチさせる。
 *         WFNM_SHORTEST なら str の先頭部分にできるだけ短くマッチさせる。
 * minlen: 予め計算した shortest_match_length(pat, flags) の値。
 * 戻り値: マッチしたら、その str の先頭部分の文字数。
 *         マッチしなかったら WFNM_NOMATCH、エラーなら WFNM_ERROR。
 * エラーが返るのは基本的にパターンが不正な場合だが、パターンが不正でも常に
 * エラーを返すわけではない。 */
size_t wfnmatchl(const wchar_t *pat, const wchar_t *str,
	enum wfnmflags flags, enum wfnmtype type, size_t minlen)
{
    const wchar_t *const savestr = str;

    if (!*pat)
	return WFNM_NOMATCH;

    /* 先頭のピリオドをチェック */
    if (PERIOD && FOLD(str[0]) == L'.' && pat[0] != L'.')
	return WFNM_NOMATCH;

    size_t strlen = wcslen(str);
    if (strlen < minlen)
	return WFNM_NOMATCH;

    if (type == WFNM_WHOLE) {
	/* パターンの末尾が一致するか、一文字だけチェック */
	size_t patlen = wcslen(pat);
	if (patlen > 0 && strlen > 0) {
	    switch (pat[patlen-1]) {
		case L']':  case L'?':  case L'*':  case L'\\':
		    break;
		default:
		    if (pat[patlen-1] != str[strlen-1])
			return WFNM_NOMATCH;
	    }
	}
    }

    size_t r = wfnmatchn(pat, str, strlen - minlen, flags, type);
    if (r == WFNM_NOMATCH || r == WFNM_ERROR)
	return r;
    else
	return r + (str - savestr);
}

/* 指定したパターンがマッチする最小の文字数をカウントする。 */
size_t shortest_match_length(const wchar_t *pat, enum wfnmflags flags)
{
    size_t count = 0;

    for (;;) {
	const wchar_t *p;
	switch (*pat) {
	    case L'\0':  eop:
		return count;
	    case L'*':
		break;
	    case L'[':
		p = skip_bracket(pat, flags);
		if (pat != p) {
		    count++;
		    pat = p;
		    continue;
		}
		goto default_case;
	    case L'\\':
		if (!NOESCAPE) {
		    pat++;
		    if (*pat == L'\0')
			goto eop;
		}
		/* falls thru! */
	    default:  case L'?':  default_case:
		count++;
		break;
	}
	pat++;
    }
}

/* L'[' で始まるパターンブラケット式を飛ばして L']' の次の文字へのポインタを
 * 返す。対応する L']' がなければ pat をそのまま返す。 */
wchar_t *skip_bracket(const wchar_t *pat, enum wfnmflags flags)
{
    const wchar_t *savepat = pat;
    assert(*pat == L'[');
    pat++;

    for (;;) {
	switch (*pat) {
	    case L'\0':
		goto fail;
	    case L'[':
		pat++;
		const wchar_t *p;
		switch (*pat) {
		    case L'.':
			p = wcsstr(pat + 1, L".]");
			break;
		    case L':':
			p = wcsstr(pat + 1, L":]");
			break;
		    case L'=':
			p = wcsstr(pat + 1, L"=]");
			break;
		    default:
			continue;
		}
		if (!p)
		    goto fail;
		pat = p + 2;
		continue;
	    case L']':
		if (pat > savepat + 1)
		    return (wchar_t *) pat + 1;
		break;
	    case L'/':
		/* PATHNAME では L'/' を含むブラケット記法は使えない */
		if (PATHNAME)
		    goto fail;
		break;
	    default:
		break;
	}
	pat++;
    }

fail:
    return (wchar_t *) savepat;
}

/* 実際にマッチングを行う。
 * lendiff: wcslen(s) - shortest_match_length(p, flags)
 * 他の引数、戻り値は wfnmatch に準ずる。 */
size_t wfnmatchn(const wchar_t *p, const wchar_t *s, size_t lendiff,
	enum wfnmflags flags, enum wfnmtype type)
{
    const wchar_t *const saves = s;
    wint_t sc;

    for (;;) {
	switch (*p) {
	case L'\0':  eop:
	    if (type == WFNM_WHOLE && *s != L'\0')
		return WFNM_NOMATCH;
	    else
		return s - saves;
	case L'*':
	    do p++; while (*p == L'*');  /* L'*' 以外のパターンまでスキップ */
	    if (*p == L'\0') {
		/* パターンが L'*' で終わっているならマッチ完了 */
		switch (type) {
		case WFNM_WHOLE:
		case WFNM_LONGEST:  return (s - saves) + wcslen(s);
		case WFNM_SHORTEST: return (s - saves);
		}
	    }
	    /* L'*' に何文字分マッチするかを変えながら、パターンの残りを試す */
	    size_t submatchlen = WFNM_NOMATCH;
	    do {
		size_t r = wfnmatchn(p, s, lendiff, flags, type);
		if (r == WFNM_ERROR)
		    return WFNM_ERROR;
		if (r != WFNM_NOMATCH) {
		    if (type == WFNM_SHORTEST)
			return r + (s - saves);
		    else
			submatchlen = r + (s - saves);
		}
		if (PATHNAME && FOLD(*s) == L'/')
		    break;
		s++;
	    } while (lendiff-- != 0);  /* lendiff が負になったらやめる */
	    return submatchlen;
	case L'?':
	    if ((PATHNAME && FOLD(*s) == L'/') || (*s == L'\0'))
		return WFNM_NOMATCH;
	    break;
	case L'[':
	    {
		size_t r = match_bracket(&p, s, flags);
		if (r == WFNM_ERROR || r == WFNM_NOMATCH) return r;
		s += r;
		if (r)
		    continue;
		else
		    goto default_case;
	    }
	case L'\\':
	    if (!NOESCAPE) {
		p++;
		if (*p == L'\0')
		    goto eop;
	    }
	    /* falls thru! */
	default:  default_case:
	    sc = FOLD(*s);
	    if (FOLD(*p) != sc)
		return WFNM_NOMATCH;
	    if (PATHNAME && PERIOD) {
		/* PATHNAME && PERIOD の場合、L"/." の二文字は一気に処理する */
		if (sc == L'/' && FOLD(*(s+1)) == L'.') {
		    if (*(p+1) != L'.') return WFNM_NOMATCH;
		    p += 2;  s += 2;
		    continue;
		}
	    }
	    break;
	}
	p++; s++;
    }
}

/* ブラケット記法を解析し、マッチするか調べる。
 * マッチしたらその分 *pp を進める。しなかったらそのまま。
 * 戻り値は wfnmatch に準ずる。
 * 指定したパターンがブラケット記法ではなかった場合、エラーにすべきかどうかに
 * よって 0 か WFNM_ERROR を返す。 */
size_t match_bracket(
	const wchar_t **pp, const wchar_t *s, enum wfnmflags flags)
{
    const wchar_t *p = *pp;
    wint_t sc = *s, fsc = FOLD(sc);
    bool matched = false;

    assert(*p == L'[');
    p++;

    /* マッチングの否定かどうかを調べる。
     * PATHNAME がオフのとき、L'[' の直後に L'^' がある場合はマッチングの成否を
     * 逆転する。PATHNAME がオンのときは L'^' の代わりに L'!' を使うことに
     * なっているが、POSIX ではこの場合の L'^' の扱いを定めていない。
     * この実装では L'^' は常に成否を逆転するものとして扱う。 */
    bool neg = (*p == L'^') || (PATHNAME && *p == L'!');
    if (neg)
	p++;

    /* この実装は、L"[.ch.]" のようなコレーティングシンボルや
     * L"[=a=]" のような等価クラス記法には厳密には対応しない。
     * これらはどちらも最初の一文字にマッチするだけである。
     * 例えば、L"[.ch.]" は単なる L"c" に等しい。 */

    for (bool initial = true; ; initial = false) {
	wint_t rangestart, rangeend;
	switch (*p) {
	case L'\0':
	    return WFNM_ERROR;
	case L']':
	    if (initial)
		goto default_case;
	    *pp = p + 1;
	    return (matched != neg) ? 1 : WFNM_NOMATCH;
	case L'[':
	    switch (*(p+1)) {
		case L'.':
		    rangestart = *(p+2);
		    p = wcsstr(p+2, L".]");
		    if (!p)
			return WFNM_ERROR;
		    p += 2;
		    break;
		case L'=':
		    rangestart = *(p+2);
		    p = wcsstr(p+2, L"=]");
		    if (!p)
			return WFNM_ERROR;
		    p += 2;
		    break;
		case L':':
		    p = check_char_class(p+2, sc, &matched);
		    if (!p)
			return WFNM_ERROR;
		    continue;
		default:
		    goto default_case;
	    }
	    break;
	case L'/':
	    if (PATHNAME)
		return 0;
	    /* falls thru! */
	default:  default_case:
	    rangestart = *p;
	    p++;
	    break;
	}
	if (*p == L'-' && *(p+1) != L']') {
	    switch (*(p+1)) {
	    case L'\0':
		return WFNM_ERROR;
	    case L'[':
		switch (*(p+2)) {
		    case L'.':
			rangeend = *(p+3);
			p = wcsstr(p+3, L".]");
			if (!p)
			    return WFNM_ERROR;
			p += 2;
			break;
		    case L'=':
			rangeend = *(p+3);
			p = wcsstr(p+3, L"=]");
			if (!p)
			    return WFNM_ERROR;
			p += 2;
			break;
		    case L':':
			return WFNM_ERROR;
		    default:
			goto default_case2;
		}
		break;
	    case L'/':
		if (PATHNAME)
		    return 0;
		/* falls thru! */
	    default:  default_case2:
		rangeend = *(p+1);
		p += 2;
		break;
	    }
	} else {
	    rangeend = rangestart;
	}
	if (rangestart)
	    if ((rangestart <= sc && sc <= rangeend)
		    || (rangestart <= fsc && fsc <= rangeend))
		matched = true;
    }
}

/* L"[:" で始まる文字クラスのマッチングを判定する。
 * p:      L"[:" の直後の位置
 * c:      判定する文字。
 * 戻り値: L":]" の直後の位置。またはパターンが不正なら NULL。 */
wchar_t *check_char_class(const wchar_t *p, wint_t c, bool *match)
{
    const wchar_t *end = wcsstr(p, L":]");
    if (!end)
	return NULL;

    char *classname = malloc_wcsntombs(p, end - p);
    if (!classname)
	return NULL;

    wctype_t type = wctype(classname);
    free(classname);
    if (iswctype(c, type))
	*match = true;
    return (wchar_t *) end + 2;
}

/* WFNM_PATHNAME が有効な状態においてパターン内に L'/' が最初に現れるまでの
 * 文字数を返す。
 * 例えば count_pattern_length_before_slash(L"[abc]/def") は 5 となる。
 * パターン内に L'/' がなければ文字列の長さを返す。 */
size_t count_pattern_length_before_slash(const wchar_t *pat)
{
    size_t count = 0;

    for (;;) {
	const wchar_t *p;
	switch (pat[count]) {
	    case L'\0':
	    case L'/':
		return count;
	    case L'[':
		p = skip_bracket(pat + count, WFNM_PATHNAME);
		size_t diff = p - (pat + count);
		if (diff) {
		    count += diff;
		    continue;
		}
		/* falls thru! */
	    default:
		break;
	}
	count++;
    }
}

/* WFNM_PATHNAME が有効な状態においてパターン内に L"**" が最初に現れるまでの
 * 文字数を返す。ただし返す文字数は最初の L'*' も含む。
 * 例えば count_pattern_length_before_double_stars(L"abc**def") は 4 となる。
 * パターン内に L"**" がなければ 0 を返す。 */
size_t count_pattern_length_before_double_stars(const wchar_t *pat)
{
    size_t count = 0;

    for (;;) {
	const wchar_t *p;
	switch (pat[count]) {
	    case L'\0':
		return 0;
	    case L'*':
		if (pat[count + 1] == L'*')
		    return count + 1;
		break;
	    case L'[':
		p = skip_bracket(pat + count, WFNM_PATHNAME);
		size_t diff = p - (pat + count);
		if (diff) {
		    count += diff;
		    continue;
		}
		/* falls thru! */
	    default:
		break;
	}
	count++;
    }
}

/* パターン内に L'*' や L'?' などの特殊文字があるかどうか調べる。
 * 結果が false ならば、pat にマッチする文字列は pat それ自身のみである。 */
extern bool pattern_with_special_char(const wchar_t *pat)
{
    return wcspbrk(pat, L"*?[\\") != NULL;
}


/* vim: set ts=8 sts=4 sw=4 noet: */
