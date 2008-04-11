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
#include <wctype.h>
#include "option.h"
#include "util.h"
#include "strbuf.h"
#include "plist.h"
#include "path.h"
#include "parser.h"
#include "variable.h"
#include "expand.h"
#include "exec.h"


#define ESCAPED_CHARS L"\\*?[{"  /* ブレース展開・glob で特殊な意味を持つ文字 */

__attribute__((nonnull(4)))
static bool expand_word(const wordunit_T *arg,
	tildetype_T tilde, const wchar_t *ifs, plist_T *list);
__attribute__((nonnull))
static inline void add_sq(const wchar_t *restrict *ss, xwcsbuf_T *restrict buf);
__attribute__((nonnull,malloc,warn_unused_result))
static wchar_t *expand_tilde(const wchar_t **ss);
__attribute__((nonnull(1),malloc,warn_unused_result))
static void **expand_param(
	const paramexp_T *p, bool indq, tildetype_T tilde, const wchar_t *ifs);
__attribute__((nonnull))
static void print_subst_as_error(const paramexp_T *p);
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

    if (!ifs)
	ifs = L" \t\n";
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
 * 各種展開と引用符除去・エスケープ解除を行う。
 * ただしブレース展開・フィールド分割はしない。
 * glob が true ならファイル名展開も行うが、結果が一つでない場合は……
 *   - posixly_correct が true ならファイル名展開前のパターンを返し、
 *   - posixly_correct が false ならエラーとする。
 * エラー発生時はメッセージを出して NULL を返す。
 * 戻り値: 展開結果。新しく malloc した文字列。 */
wchar_t *expand_single(const wordunit_T *arg, tildetype_T tilde, bool glob)
{
    wchar_t *result;
    plist_T list;
    pl_init(&list);

    /* 展開する */
    if (!expand_word(arg, tilde, NULL, &list)) {
	recfree(pl_toary(&list), free);
	return NULL;
    }
    result = list.contents[0];
    pl_destroy(&list);

    /* glob する */
    if (glob) {
	enum wglbflags flags = 0;
	// TODO expand: expand_line: wglob のフラグオプション
	pl_init(&list);
	wglob(result, flags, &list);
	if (list.length == 1) {
	    free(result);
	    result = list.contents[0];
	    pl_destroy(&list);
	} else {
	    recfree(pl_toary(&list), free);
	    if (posixly_correct) {
		goto noglob;
	    } else {
		xerror(0, Ngt("%ls: glob resulted in multiple words"), result);
		free(result);
		result = NULL;
	    }
	}
    } else {
noglob:
	result = unescapefree(result);
    }

    return result;
}

/* 一つの文字列を展開する。
 * パラメータ展開・数式展開・コマンド置換を行うが、引用符除去・フィールド分割・
 * ファイル名展開はしない。エラー発生時はメッセージを出して NULL を返す。 */
wchar_t *expand_string(const wordunit_T *arg);

/* 一つの単語を展開する。
 * チルダ展開・パラメータ展開・コマンド置換・数式展開をし、単語分割をする。
 * w:      展開する単語
 * tilde:  チルダ展開の種類
 * ifs:    非 NULL なら、これに従って単語分割する。NULL なら単語分割しない。
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
		    if (indq && !wcschr(L"$`\"\\", str[1])) {
			goto default_case;
		    } else {
			wb_wccat(&buf, L'\\');
			if (*++str)
			    wb_wccat(&buf, *str++);
			continue;
		    }
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
	    array = expand_param(w->wu_param, indq, tilde, ifs);
	    if (array) {
		if (!array[0]) {
		    suppress = true;
		} else {
		    for (void **a = array; ; ) {
			if (indq) {
			    wb_cat(&buf, *a);
			    free(*a);
			} else {
			    add_splitting(*a, list, &buf, ifs);
			}
			a++;
			if (!*a)
			    break;
			pl_add(list, wb_towcs(&buf));
			wb_init(&buf);
		    }
		}
		free(array);
	    } else {
		ok = false;
	    }
	    break;
	case WT_CMDSUB:
	    s = exec_command_substitution(w->wu_cmdsub);
	    if (s) {
		if (indq) {
		    wchar_t *ss = escapefree(s, ESCAPED_CHARS);
		    wb_cat(&buf, ss);
		    free(ss);
		} else {
		    add_splitting(s, list, &buf, ifs);
		}
	    } else {
		ok = false;
	    }
	    break;
	case WT_ARITH:
	    ok = false;  // TODO expand: expand_word: 数式展開の実装
	    xerror(0, "arithmetic expansion not implemented");
	    break;
	}
	w = w->next;
	first = false;
    }

    /* "" や '' のような空の単語はここで追加する。
     * 引用符が出た段階で force が true になり、単語を追加すべきことを示す。
     * 例外として、"$@" は (引用符があるが) 内容が空なら追加しない。これは
     * suppress によって示す。 */
    if (!ifs || buf.length > 0
	    || (initlen == list->length && force && !suppress))
	pl_add(list, wb_towcs(&buf));
    else
	wb_destroy(&buf);

    assert(ifs || list->length - initlen == 1);
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
 * tilde:  入れ子の展開で行うチルダ展開
 * 戻り値: 展開結果。void * にキャストしたワイド文字列へのポインタの NULL 終端
 *         配列。配列および要素は新しく malloc したものである。
 *         エラーのときは NULL。
 * 返す各要素は、ESCAPED_CHARS をエスケープ済みである。(indq が true の場合)
 * "@" または配列以外の展開結果は、必ず要素数 1 である。
 * "*" の展開結果は、ifs に従って結合済みである。 */
void **expand_param(
	const paramexp_T *p, bool indq, tildetype_T tilde, const wchar_t *ifs)
{
    void **list;  /* void * にキャストした wchar_t * の配列 */
    bool concat;  /* true なら配列の内容を IFS の最初の文字で繋ぐ */
    bool unset;   /* 指定した変数が存在しなかった場合 true */
    wchar_t *match, *subst;

    /* 変数またはネストした展開の内容を取得する */
    if (p->pe_type & PT_NEST) {
	plist_T plist;
	pl_init(&plist);
	if (!expand_word(p->pe_nest, tilde, NULL, &plist)) {
	    recfree(pl_toary(&plist), free);
	    return NULL;
	}
	list = pl_toary(&plist);
	concat = unset = false;
	assert(list[0] && !list[1]);  /* 要素数は 1 */
	list[0] = unescapefree(list[0]);
    } else {
	list = get_variable(p->pe_name, &concat);
	if (list) {
	    unset = false;
	} else {
	    /* 指定した名前の変数が存在しなければ、空文字列を返す */
	    plist_T plist;
	    list = pl_toary(pl_add(pl_init(&plist), xwcsdup(L"")));
	    unset = true;
	}
    }

    /* この時点で、list の内容はバックスラッシュエスケープしていない
     * 生の文字列である。 */

    /* PT_COLON フラグが立っているなら、変数の値が空文字列の場合も
     * 変数が存在しないとみなす。 */
    if ((p->pe_type & PT_COLON)
	    && (!list[0] || (!((char *) list[0])[0] && !list[1])))
	unset = true;

    /* PT_MINUS, PT_PLUS, PT_ASSIGN, PT_ERROR を処理する */
    switch (p->pe_type & PT_MASK) {
    case PT_PLUS:
	if (!unset)
	    goto subst;
	break;
    case PT_MINUS:
	if (unset) {
subst:
	    recfree(list, free);
	    subst = expand_single(p->pe_subst, tt_single, false);
	    if (!subst)
		return NULL;
	    list = xmalloc(2 * sizeof *list);
	    list[0] = subst;
	    list[1] = NULL;
	}
	break;
    case PT_ASSIGN:
	// TODO expand: expand_param: PT_ASSIGN
	break;
    case PT_ERROR:
	if (unset) {
	    recfree(list, free);
	    print_subst_as_error(p);
	    return NULL;
	}
	break;
    }

    // TODO PT_MATCH などの処理
    (void) match;

    /* 配列の要素を連結する */
    if (concat) {
	wchar_t joiner = ifs ? ifs[0] : L' ';
	xwcsbuf_T buf;
	wb_init(&buf);
	for (size_t i = 0; list[i]; i++) {
	    wb_cat(&buf, list[i]);
	    free(list[i]);
	    if (joiner != L'\0' && list[i + 1])
		wb_wccat(&buf, joiner);
	}
	list = xrealloc(list, 2 * sizeof *list);
	list[0] = wb_towcs(&buf);
	list[1] = NULL;
    }

    /* 戻り値をエスケープする */
    for (size_t i = 0; list[i]; i++)
	list[i] = escapefree(list[i], indq ? ESCAPED_CHARS : L"\\");

    return list;
}

/* p->pe_subst を展開して、それをエラーメッセージとして表示する */
void print_subst_as_error(const paramexp_T *p)
{
    if (p->pe_subst) {
	wchar_t *subst = expand_single(p->pe_subst, tt_single, false);
	if (subst) {
	    if (p->pe_type & PT_NEST)
		xerror(0, "%ls", subst);
	    else
		xerror(0, "%s: %ls", p->pe_name, subst);
	    free(subst);
	}
    } else {
	if (p->pe_type & PT_NEST)
	    xerror(0, Ngt("parameter null"));
	else
	    xerror(0, (p->pe_type & PT_COLON)
		    ? Ngt("%s: parameter null or not set")
		    : Ngt("%s: parameter not set"),
		    p->pe_name);
    }
}

/* 単語をバッファに追加する。
 * s:    追加する単語。この関数内で free する。NULL なら何もしない。
 * list: 単語分割した場合に結果を追加するリスト
 * buf:  s を追加するバッファ
 * ifs:  非 NULL なら s を追加する際にこれに従って単語分割する。 */
void add_splitting(wchar_t *const s,
	plist_T *list, xwcsbuf_T *buf, const wchar_t *ifs)
{
    if (!s)
	return;

    for (wchar_t *ss = s; *ss; ss++) {
	if (ifs && wcschr(ifs, *ss)) {
	    /* ifs に含まれる文字なので分割する */
	    bool split = false;
	    while (wcschr(ifs, *ss)) {
		if (!iswspace(*ss)) {
		    pl_add(list, wb_towcs(buf));
		    wb_init(buf);
		    split = true;
		}
		ss++;
	    }
	    if (!split) {
		pl_add(list, wb_towcs(buf));
		wb_init(buf);
	    }
	    if (*ss)
		wb_wccat(buf, *ss);
	} else {
	    wb_wccat(buf, *ss);
	}
    }
    free(s);
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
