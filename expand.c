/* Yash: yet another shell */
/* expand.c: word expansion */
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
#include <stdbool.h>
#include <stdlib.h>
#include "util.h"
#include "strbuf.h"
#include "plist.h"
#include "path.h"
#include "parser.h"
#include "variable.h"
#include "expand.h"


#define ESCAPED_CHARS L"*?[{"  /* ブレース展開・glob で特殊な意味を持つ文字 */

__attribute__((nonnull(4)))
static bool expand_word(const wordunit_T *arg,
	tildetype_T tilde, const wchar_t *ifs, plist_T *list);
__attribute__((nonnull,malloc,warn_unused_result))
static wchar_t *expand_tilde(const wchar_t **ss);
__attribute__((nonnull,malloc,warn_unused_result))
static void **expand_param(const paramexp_T *p, bool indq, const wchar_t *ifs);
__attribute__((nonnull))
static wchar_t *command_substitution(const wchar_t *code);
__attribute__((nonnull))
static inline void add_sq(const wchar_t *restrict *ss, xwcsbuf_T *restrict buf);
__attribute__((nonnull(2,3)))
static void add_splitting(wchar_t *s,
	plist_T *list, xwcsbuf_T *buf, const wchar_t *ifs);


/* コマンドライン上の各種展開を行う。
 * args: void * にキャストした const wordunit_T へのポインタの配列。配列内の
 *       各ワードが展開される。配列の最後の要素は NULL でなければならない。
 * argcp: このポインタが指すところに展開結果の個数が入る。
 * argvp: このポインタが指すところに展開結果が入る。結果は、マルチバイト文字列
 *       へのポインタの配列へのポインタであり、配列の最後の要素は NULL である。
 * args で与えられる内容は変更されない。
 * 戻り値: 成功すると true、エラーがあると false。
 * エラーがあった場合、*argcp, *argvp の値は不定である */
bool expand_line(void *const *restrict args,
    int *restrict argcp, char ***restrict argvp)
{
    plist_T list1, list2;
    const wchar_t *ifs = getvar(VAR_IFS);

    pl_init(&list1);

    /* 各種展開をする */
    while (*args) {
	if (!expand_word(*args, tt_single, ifs, &list1)) {
	    recfree(pl_toary(&list1), free);
	    return false;
	}
	args++;
    }

    /* ブレース展開する */
    if (false) {  // TODO expand: expand_line: ブレース展開
    } else {
	list2 = list1;
    }

    /* glob する */
    if (false) {  // TODO expand: expand_line: no_glob オプション
	for (size_t i = 0; i < list1.length; i++)
	    list1.contents[i] = realloc_wcstombs(list2.contents[i]);
	list1 = list2;
    } else {
	enum wglbflags flags = 0;
	pl_init(&list1);
	// TODO expand: expand_line: wglob のフラグオプション
	for (size_t i = 0; i < list2.length; i++) {
	    size_t oldlen = list1.length;
	    wglob(list2.contents[i], flags, &list1);
	    if (oldlen == list1.length)
		pl_add(&list1, realloc_wcstombs(unescape(list2.contents[i])));
	    free(list2.contents[i]);
	}
	pl_destroy(&list2);
    }

    *argcp = list1.length;
    *argvp = (char **) pl_toary(&list1);
    return true;
}

/* 一つの単語を展開する。
 * 各種展開と引用符除去を行う。フィールド分割はしない。
 * glob が true ならファイル名展開も行うが、結果が一つでない場合は……
 *   - posixly_correct が true ならファイル名展開前のパターンを返し、
 *   - posixly_correct が false ならエラーとする。
 * エラー発生時はメッセージを出して NULL を返す。 */
wchar_t *expand_single(const wordunit_T *arg, tildetype_T tilde, bool glob);

/* 一つの文字列を展開する。
 * パラメータ展開・数式展開・コマンド置換を行うが、引用符除去・フィールド分割・
 * ファイル名展開はしない。エラー発生時はメッセージを出して NULL を返す。 */
wchar_t *expand_string(const wordunit_T *arg);

/* 一つの単語を展開する。
 * チルダ展開・パラメータ展開・コマンド置換・数式展開をし、単語分割をする。
 * w:      展開する単語
 * tilde:  チルダ展開の種類
 * ifs:    非 NULL なら、これに従って単語分割する。
 * list:   結果を入れるリスト
 * 戻り値: エラーがなければ true。
 * ifs が NULL なら、結果は常に 1 要素である。
 * 引用符 (" と ') はバックスラッシュエスケープに置き換わる。 */
bool expand_word(const wordunit_T *w,
	tildetype_T tilde, const wchar_t *ifs, plist_T *list)
{
    bool ok = true;
    bool indq = false;     /* 二重引用符 " の中かどうか */
    bool first = true;     /* 最初の word unit かどうか */
    bool force = false;    /* 展開結果が空文字列でも追加する */
    bool suppress = false; /* force を無効にする */
    size_t initlen = list->length;
    xwcsbuf_T buf;
    const wchar_t *str;
    wchar_t *s;
    void **array;

    wb_init(&buf);
    while (w) {
	switch (w->wu_type) {
	case WT_STRING:
	    str = w->wu_string;
	    if (first && tilde != tt_none)
		add_splitting(expand_tilde(&str), list, &buf, ifs);
	    for (;;) {
		switch (*str) {
		case L'\0':
		    goto out;
		case L'"':
		    indq = !indq;
		    force = true;
		    break;
		case L'\'':
		    if (indq)
			goto default_case;
		    force = true;
		    add_sq(&str, &buf);
		    break;
		case L'\\':
		    wb_wccat(&buf, L'\\');
		    if (*++str)
			wb_wccat(&buf, *str++);
		    continue;
		case L':':
		    if (!indq && tilde == tt_multi) {
			wb_wccat(&buf, L':');
			str++;
			add_splitting(expand_tilde(&str), list, &buf, ifs);
			continue;
		    }
		    /* falls thru! */
		default:  default_case:
		    if (indq)
			wb_wccat(&buf, L'\\');
		    wb_wccat(&buf, *str);
		    break;
		}
		str++;
	    }
out:
	    break;
	case WT_PARAM:
	    array = expand_param(w->wu_param, indq, ifs);
	    if (array) {
		if (!*array) {
		    suppress = true;
		} else {
		    const wchar_t *iifs = indq ? L"" : ifs;
		    for (void **a = array; ; ) {
			add_splitting(*a, list, &buf, iifs);
			if (!*++a)
			    break;
			if (!iifs || buf.length > 0) {
			    pl_add(list, wb_towcs(&buf));
			    wb_init(&buf);
			}
		    }
		}
		free(array);
	    } else {
		ok = false;
	    }
	    break;
	case WT_CMDSUB:
	    s = command_substitution(w->wu_cmdsub);
	    if (s) {
		add_splitting(escape(s, ESCAPED_CHARS), list, &buf,
			indq ? L"" : ifs);
		free(s);
	    } else {
		ok = false;
	    }
	    break;
	case WT_ARITH:
	    ok = false;  // TODO expand: expand_word: 数式展開の実装
	    xerror(0, 0, "arithmetic expansion not implemented");
	    break;
	}
	w = w->next;
	first = false;
    }

    if (!ifs || buf.length > 0
	    || (initlen == list->length && force && !suppress))
	pl_add(list, wb_towcs(&buf));
    else
	wb_destroy(&buf);

    return ok;
}

/* 単一引用符の中身を、エスケープしながら加える。
 * 最初 *ss は開く引用符を指していて、返るとき *ss は閉じる引用符を指す。 */
void add_sq(const wchar_t *restrict *ss, xwcsbuf_T *restrict buf)
{
    (*ss)++;
    for (;;) {
	switch (**ss) {
	    case L'\0':
		assert(false);
	    case L'\'':
		return;
	    case L'*':  case L'?':  case L'[':  case L'{':  /* ESCAPED_CHARS */
		wb_wccat(buf, L'\\');
		/* falls thru! */
	    default:
		wb_wccat(buf, **ss);
		break;
	}
	(*ss)++;
    }
}

/* チルダ展開を行う。
 * ss: チルダがあるべき場所を指すポインタへのポインタ。
 *     *ss は展開した後の部分まで進む。
 * 戻り値: 新しく malloc した、展開結果。失敗なら NULL。
 * (*ss)[0] が L'~' でなければ直ちに NULL を返す。 */
wchar_t *expand_tilde(const wchar_t **ss)
{
    // TODO expand:expand_tilde
    (void) ss;
    return NULL;
}

/* パラメータ展開を行い、結果を返す。
 * 戻り値: 展開結果。void * にキャストしたワイド文字列へのポインタの NULL 終端
 *         配列。配列および要素は新しく malloc したものである。
 *         エラーのときは NULL。
 * 返す各要素は、ESCAPED_CHARS をエスケープ済みである。
 * "@" または配列以外の展開結果は、必ず要素数 1 である。
 * "*" の展開結果は、ifs に従って結合済みである。 */
void **expand_param(const paramexp_T *p, bool indq, const wchar_t *ifs)
{
    (void) p, (void) indq, (void) ifs;
    // TODO expand:expand_param
    return NULL;
}

/* コマンド置換を行い、結果を返す。
 * 戻り値: 新しく malloc した、置換結果の文字列。エラーなら NULL。 */
wchar_t *command_substitution(const wchar_t *code)
{
    (void) code;
    // TODO expand:command_substitution
    return NULL;
}

/* 単語をバッファに追加する。
 * s:    追加する単語。この関数内で free する。NULL なら何もしない。
 * list: 単語分割した場合に結果を追加するリスト
 * buf:  s を追加するバッファ
 * ifs:  非 NULL なら s を追加する際にこれに従って単語分割する。 */
void add_splitting(wchar_t *s,
	plist_T *list, xwcsbuf_T *buf, const wchar_t *ifs)
{
    free(s);
    // TODO expand:add_splitting
    (void) list, (void) buf, (void) ifs;
}

/* s の文字のうち、t に含まれるものをバックスラッシュエスケープして、
 * 新しく malloc した文字列として返す。
 * 戻り値: エスケープした s */
wchar_t *escape(const wchar_t *restrict s, const wchar_t *restrict t)
{
    xwcsbuf_T buf;
    wb_init(&buf);
    while (*s) {
	if (wcschr(t, *s))
	    wb_wccat(&buf, L'\\');
	wb_wccat(&buf, *s);
	s++;
    }
    return wb_towcs(&buf);
}

/* バックスラッシュエスケープを削除して新しく malloc した文字列として返す。 */
wchar_t *unescape(const wchar_t *s)
{
    xwcsbuf_T buf;
    wb_init(&buf);
    while (*s) {
	if (*s == L'\\') {
	    if (*(s+1) == L'\0')
		break;
	    else
		s++;
	}
	wb_wccat(&buf, *s);
	s++;
    }
    return wb_towcs(&buf);
}


/* vim: set ts=8 sts=4 sw=4 noet: */
