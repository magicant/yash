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
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>
#include "option.h"
#include "util.h"
#include "strbuf.h"
#include "plist.h"
#include "wfnmatch.h"
#include "path.h"
#include "parser.h"
#include "variable.h"
#include "expand.h"
#include "exec.h"


/* ブレース展開・glob で特殊な意味を持つ文字 */
#define ESCAPED_CHARS L"\\{,}"

static bool expand_word(
	const wordunit_T *restrict w, tildetype_T tilde, plist_T *restrict list)
    __attribute__((nonnull(3)));
static wchar_t *expand_tilde(const wchar_t **ss,
	bool hasnextwordunit, tildetype_T tt)
    __attribute__((nonnull,malloc,warn_unused_result));
static void **expand_param(const paramexp_T *p, bool indq, tildetype_T tilde)
    __attribute__((nonnull,malloc,warn_unused_result));
static void print_subst_as_error(const paramexp_T *p)
    __attribute__((nonnull));
static void match_each(
	void **slist, const wchar_t *pattern, paramexptype_T type)
    __attribute__((nonnull));
static void match_head_each(void **slist, const wchar_t *pattern, bool longest)
    __attribute__((nonnull));
static void match_tail_longest_each(void **slist, const wchar_t *pattern)
    __attribute__((nonnull));
static void match_tail_shortest_each(void **slist, const wchar_t *pattern)
    __attribute__((nonnull));
static void subst_each(void **slist, const wchar_t *pattern,
	const wchar_t *subst, paramexptype_T type)
    __attribute__((nonnull));
static void subst_whole_each(
	void **slist, const wchar_t *pattern, const wchar_t *subst)
    __attribute__((nonnull));
static void subst_head_each(
	void **slist, const wchar_t *pattern, const wchar_t *subst)
    __attribute__((nonnull));
static void subst_tail_each(
	void **slist, const wchar_t *pattern, const wchar_t *subst)
    __attribute__((nonnull));
static void subst_generic_each(void **slist,
	const wchar_t *pattern, const wchar_t *subst, bool substall)
    __attribute__((nonnull));
static void subst_length_each(void **slist)
    __attribute__((nonnull));
static void expand_brace_each(void **restrict slist, plist_T *restrict dest)
    __attribute__((nonnull));
static void expand_brace(wchar_t *restrict word, plist_T *restrict dest)
    __attribute__((nonnull));
static bool has_leading_zero(const wchar_t *s, bool *sign)
    __attribute__((nonnull));
static bool tryexpand_brace_sequence(
	wchar_t *word, wchar_t *startc, plist_T *restrict dest)
    __attribute__((nonnull));
static void fieldsplit(wchar_t *restrict s, const wchar_t *restrict ifs,
	plist_T *restrict dest)
    __attribute__((nonnull));
static void fieldsplit_all(void **restrict src, plist_T *restrict dest)
    __attribute__((nonnull));
static inline void add_sq(const wchar_t *restrict *ss, xwcsbuf_T *restrict buf)
    __attribute__((nonnull));
static wchar_t *reescape(const wchar_t *s)
    __attribute__((nonnull,malloc,warn_unused_result));
static void **reescape_full_array(void **wcsarray)
    __attribute__((nonnull));
static wchar_t *escaped_wcspbrk(const wchar_t *wcs, const wchar_t *accept)
    __attribute__((nonnull));
static void do_glob_each(void *const *restrict patterns, plist_T *restrict list)
    __attribute__((nonnull));
static enum wglbflags get_wglbflags(void)
    __attribute__((pure));


/********** 各種展開のエントリポイント **********/

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
    pl_init(&list1);

    /* 各種展開をする (args -> list1) */
    while (*args) {
	if (!expand_word(*args, tt_single, &list1)) {
	    recfree(pl_toary(&list1), free);
	    return false;
	}
	args++;
    }

    /* ブレース展開する (list1 -> list2) */
    if (shopt_braceexpand) {
	pl_init(&list2);
	expand_brace_each(list1.contents, &list2);
	pl_destroy(&list1);
    } else {
	list2 = list1;
    }

    /* 単語分割する (list2 -> list1) */
    pl_init(&list1);
    fieldsplit_all(pl_toary(&list2), &list1);

    /* glob する (list1 -> list2) */
    if (shopt_noglob) {
	for (size_t i = 0; i < list1.length; i++) {
	    char *v = realloc_wcstombs(list1.contents[i]);
	    if (!v) {
		xerror(0, Ngt("expanded word contains characters that "
			    "cannot be converted to wide characters and "
			    "is replaced with null string"));
		v = xstrdup("");
	    }
	    list1.contents[i] = v;
	}
	list2 = list1;
    } else {
	pl_init(&list2);
	do_glob_each(list1.contents, &list2);
	recfree(pl_toary(&list1), free);
    }

    *argcp = list2.length;
    *argvp = (char **) pl_toary(&list2);
    return true;
}

/* 一つの単語を展開する。
 * 各種展開と引用符除去を行う。
 * ただしブレース展開・フィールド分割・glob・エスケープ解除はしない。
 * エラー発生時はメッセージを出して NULL を返す。
 * 戻り値: 展開結果。新しく malloc した文字列。 */
wchar_t *expand_single(const wordunit_T *arg, tildetype_T tilde)
{
    wchar_t *result;
    plist_T list;
    pl_init(&list);

    if (!expand_word(arg, tilde, &list)) {
	recfree(pl_toary(&list), free);
	return NULL;
    }
    if (list.length != 1) {
	/* 結果の単語が複数ある場合は結合して返す */
	const wchar_t *ifs = getvar(VAR_IFS);
	result = joinwcsarray(list.contents, ifs ? ifs[0] : L' ');
	recfree(pl_toary(&list), free);
    } else {
	result = list.contents[0];
	pl_destroy(&list);
    }
    return result;
}

/* 一つの単語を展開する。
 * 各種展開と glob・引用符除去・エスケープ解除を行う。
 * ただしブレース展開・フィールド分割はしない。
 * glob の結果が一つでなければ、
 *   - posixly_correct が true なら glob 前のパターンを返し、
 *   - posixly_correct が false ならエラーを出す。
 * shopt_noglob が true なら glob は行わない。
 * shopt_nullglob が false でも true とみなす。
 * 戻り値: 新しく malloc した文字列。エラーの場合は NULL。 */
char *expand_single_with_glob(const wordunit_T *arg, tildetype_T tilde)
{
    wchar_t *exp = expand_single(arg, tilde);
    char *result;

    /* glob する */
    if (shopt_noglob || !pattern_has_special_char(exp)) {
noglob:
	result = realloc_wcstombs(unescapefree(exp));
	if (!result)
	    xerror(0, Ngt("expanded word contains characters that "
			"cannot be converted to wide characters and "
			"is replaced with null string"));
    } else {
	plist_T list;
	pl_init(&list);
	wglob(exp, get_wglbflags(), &list);
	if (list.length == 1) {
	    free(exp);
	    result = list.contents[0];
	    pl_destroy(&list);
	} else {
	    recfree(pl_toary(&list), free);
	    if (posixly_correct) {
		goto noglob;
	    } else {
		exp = unescapefree(exp);
		xerror(0, Ngt("%ls: not single file"), exp);
		free(exp);
		result = NULL;
	    }
	}
    }
    return result;
}

/* 一つの文字列を展開する。
 * パラメータ展開・数式展開・コマンド置換を行うが、引用符除去・フィールド分割・
 * ファイル名展開はしない。エラー発生時はメッセージを出して NULL を返す。 */
wchar_t *expand_string(const wordunit_T *arg);
// TODO expand: expand_string 未実装


/********** 四種展開 **********/

/* 一つの単語についてチルダ展開・パラメータ展開・コマンド置換・数式展開をする。
 * w:      展開する単語
 * tilde:  チルダ展開の種類
 * list:   結果 (新しく malloc したワイド文字列へのポインタ) を入れるリスト
 * 戻り値: エラーがなければ true。
 * 引用符 (" と ') はバックスラッシュエスケープに置き換わる。
 * list に追加する要素数は基本的に一つだが、"$@" を展開した場合は
 * 複数追加したり一つも追加しなかったりする場合もある。 */
bool expand_word(
	const wordunit_T *restrict w, tildetype_T tilde, plist_T *restrict list)
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
	    if (first && tilde != tt_none) {
		s = expand_tilde(&str, w->next, tilde);
		if (s)
		    wb_catfree(&buf, escapefree(s, ESCAPED_CHARS));
	    }
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
			s = expand_tilde(&str, w->next, tilde);
			if (s)
			    wb_catfree(&buf, escapefree(s, ESCAPED_CHARS));
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
	    array = expand_param(w->wu_param, indq, tilde);
	    if (array) {
		if (!array[0]) {
		    suppress = true;
		} else {
		    force = true;
		    for (void **a = array; ; ) {
			wb_catfree(&buf, *a);
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
		wb_catfree(&buf, escapefree(s, indq ? NULL : ESCAPED_CHARS));
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
    if (buf.length > 0 || (initlen == list->length && force && !suppress))
	pl_add(list, wb_towcs(&buf));
    else
	wb_destroy(&buf);
    return ok;
}

/* チルダ展開を行う。
 * ss: チルダがあるべき場所を指すポインタへのポインタ。
 *     *ss は展開した後の部分まで進む。
 * hasnextwordunit: 現在展開中の WT_STRING な wordunit_T の後ろに他の wordunit_T
 *     が控えているかどうか。他の wordunit_T がない場合のみ文字列全体を展開する
 * 戻り値: 新しく malloc した、展開結果。失敗なら NULL。
 * (*ss)[0] が L'~' でなければ直ちに NULL を返す。 */
wchar_t *expand_tilde(const wchar_t **ss, bool hasnextwordunit, tildetype_T tt)
{
    const wchar_t *s = *ss;
    if (*s != L'~')
	return NULL;
    s++;

    const wchar_t *end = wcspbrk(s, tt == tt_single ? L"/" : L"/:");
    wchar_t *username;
    const wchar_t *home;
    size_t usernamelen;

    if (end) {
	usernamelen = end - s;
    } else {
	if (hasnextwordunit)
	    return NULL;
	usernamelen = wcslen(s);
    }
    username = xwcsndup(s, usernamelen);
    if (username[0] == L'\0') {
	/* 空のユーザ名なら $HOME に展開 */
	home = getvar(VAR_HOME);
	goto finish;
    } else if (wcspbrk(username, L"\"'\\") != 0) {
	/* ユーザ名に引用符があれば展開しない */
	free(username);
	return NULL;
    }
    if (!posixly_correct) {
	if (wcscmp(username, L"+") == 0) {
	    home = getvar(VAR_PWD);
	    goto finish;
	} else if (wcscmp(username, L"-") == 0) {
	    home = getvar(VAR_OLDPWD);
	    goto finish;
	}  // TODO expand: expand_tilde: ディレクトリスタック対応
    }
    home = get_home_directory(username, false);
finish:
    free(username);
    if (!home)
	return NULL;
    *ss = s + usernamelen;
    return xwcsdup(home);
}

/* パラメータ展開を行い、結果を返す。
 * tilde:  入れ子の展開で行うチルダ展開
 * 戻り値: 展開結果。void * にキャストしたワイド文字列へのポインタの NULL 終端
 *         配列。配列および要素は新しく malloc したものである。
 *         エラーのときは NULL。
 * 返す各要素は、ESCAPED_CHARS をエスケープ済みである。
 * "@" または配列以外の展開結果は、必ず要素数 1 である。
 * "*" の展開結果は、IFS に従って結合済みである。 */
void **expand_param(const paramexp_T *p, bool indq, tildetype_T tilde)
{
    void **list;  /* void * にキャストした wchar_t * の配列 */
    bool concat;  /* true なら配列の内容を IFS の最初の文字で繋ぐ */
    bool unset;   /* 指定した変数が存在しなかった場合 true */
    wchar_t *match, *subst;

    /* 変数またはネストした展開の内容を取得する */
    if (p->pe_type & PT_NEST) {
	plist_T plist;
	pl_init(&plist);
	if (!expand_word(p->pe_nest, tilde, &plist)) {
	    recfree(pl_toary(&plist), free);
	    return NULL;
	}
	list = pl_toary(&plist);
	concat = true;
	unset = false;
	for (size_t i = 0; list[i]; i++)
	    list[i] = unescapefree(list[i]);
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

    /* PT_PLUS, PT_MINUS, PT_ASSIGN, PT_ERROR, PT_MATCH, PT_SUBST を処理する */
    switch (p->pe_type & PT_MASK) {
    case PT_PLUS:
	if (!unset)
	    goto subst;
	break;
    case PT_MINUS:
	if (unset) {
	    plist_T plist;
subst:
	    recfree(list, free);
	    pl_init(&plist);
	    if (expand_word(p->pe_subst, tt_single, &plist)) {
		list = pl_toary(&plist);
		return indq ? reescape_full_array(list) : list;
	    } else {
		recfree(pl_toary(&plist), free);
		return NULL;
	    }
	}
	break;
    case PT_ASSIGN:
	if (unset) {
	    recfree(list, free);
	    if (p->pe_type & PT_NEST) {
		xerror(0, Ngt("invalid assignment in parameter expansion"));
		return NULL;
	    } else if (!is_name(p->pe_name)) {
		xerror(0, Ngt("cannot assign to `%s' in parameter expansion"),
			p->pe_name);
		return NULL;
	    }
	    subst = expand_single(p->pe_subst, tt_single);
	    if (!subst)
		return NULL;
	    subst = unescapefree(subst);
	    if (!set_variable(p->pe_name, xwcsdup(subst), false, false)) {
		free(subst);
		return NULL;
	    }
	    list = xmalloc(2 * sizeof *list);
	    list[0] = subst;
	    list[1] = NULL;
	}
	break;
    case PT_ERROR:
	if (unset) {
	    recfree(list, free);
	    print_subst_as_error(p);
	    return NULL;
	}
	break;
    case PT_MATCH:
	match = expand_single(p->pe_match, tt_single);
	if (!match) {
	    recfree(list, free);
	    return NULL;
	}
	match_each(list, match, p->pe_type);
	free(match);
	break;
    case PT_SUBST:
	match = expand_single(p->pe_match, tt_single);
	subst = expand_single(p->pe_subst, tt_single);
	if (!match || !subst) {
	    free(match);
	    free(subst);
	    recfree(list, free);
	    return NULL;
	}
	subst = unescapefree(subst);
	subst_each(list, match, subst, p->pe_type);
	free(match);
	free(subst);
	break;
    }

    /* PT_NUMBER を処理する */
    if (p->pe_type & PT_NUMBER)
	subst_length_each(list);

    /* 配列の要素を連結する */
    if (concat) {
	const wchar_t *ifs = getvar(VAR_IFS);
	wchar_t *chain = joinwcsarray(list, ifs ? ifs[0] : L' ');
	recfree(list, free);
	list = xmalloc(2 * sizeof *list);
	list[0] = chain;
	list[1] = NULL;
    }

    /* 戻り値をエスケープする */
    for (size_t i = 0; list[i]; i++)
	list[i] = escapefree(list[i], indq ? NULL : ESCAPED_CHARS);

    /* 結果が空になる場合 */
    if (!indq && list[0] && !list[1] && !((wchar_t *) list[0])[0]) {
	free(list[0]);
	list[0] = NULL;
    }

    return list;
}

/* p->pe_subst を展開して、それをエラーメッセージとして表示する */
void print_subst_as_error(const paramexp_T *p)
{
    if (p->pe_subst) {
	wchar_t *subst = expand_single(p->pe_subst, tt_single);
	if (subst) {
	    subst = unescapefree(subst);
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

/* slist の要素である各ワイド文字列に対してマッチングを行い、
 * マッチした部分を削除して返す。
 * slist:   void * にキャストした free 可能なワイド文字列へのポインタの配列
 * pattern: マッチングするパターン
 * type:    PT_MATCHHEAD, PT_MATCHTAIL, PT_MATCHLONGEST フラグ
 * slist の各要素はこの関数内で realloc する。 */
void match_each(void **slist, const wchar_t *pattern, paramexptype_T type)
{
    if (type & PT_MATCHHEAD) {
	match_head_each(slist, pattern, type & PT_MATCHLONGEST);
    } else {
	assert(type & PT_MATCHTAIL);
	if (type & PT_MATCHLONGEST)
	    match_tail_longest_each(slist, pattern);
	else
	    match_tail_shortest_each(slist, pattern);
    }
}

/* PT_MATCHHEAD な match_each を実際に行う。
 * longest: true なら最長マッチ、false なら最短マッチ */
void match_head_each(void **slist, const wchar_t *pattern, bool longest)
{
    enum wfnmtype type = longest ? WFNM_LONGEST : WFNM_SHORTEST;
    enum wfnmflags flags = shopt_nocaseglob ? WFNM_CASEFOLD : 0;
    size_t minlen = shortest_match_length(pattern, flags);
    wchar_t *str;
    while ((str = *slist)) {
	size_t match = wfnmatchl(pattern, str, flags, type, minlen);
	if (match == WFNM_ERROR) {
	    break;
	} else if (match != WFNM_NOMATCH) {
	    if (match > 0)
		wmemmove(str, str + match, wcslen(str + match) + 1);
	}
	slist++;
    }
}

/* PT_MATCHTAIL かつ PT_MATCHLONGEST な match_each を実際に行う。 */
void match_tail_longest_each(void **slist, const wchar_t *pattern)
{
    enum wfnmflags flags = shopt_nocaseglob ? WFNM_CASEFOLD : 0;
    size_t minlen = shortest_match_length(pattern, flags);
    wchar_t *str;
    while ((str = *slist)) {
	size_t len = wcslen(str);
	size_t index = 0;
	while (minlen + index <= len) {
	    size_t match = wfnmatchl(
		    pattern, str + index, flags, WFNM_WHOLE, minlen);
	    if (match == WFNM_ERROR) {
		return;
	    } else if (match != WFNM_NOMATCH) {
		str[index] = L'\0';
		break;
	    }
	    index++;
	}
	slist++;
    }
}

/* PT_MATCHTAIL だが PT_MATCHLONGEST ではない match_each を実際に行う。 */
void match_tail_shortest_each(void **slist, const wchar_t *pattern)
{
    enum wfnmflags flags = shopt_nocaseglob ? WFNM_CASEFOLD : 0;
    size_t minlen = shortest_match_length(pattern, flags);
    wchar_t *str;
    while ((str = *slist)) {
	size_t len = wcslen(str);
	size_t index = len - minlen;
	do {
	    size_t match = wfnmatchl(
		    pattern, str + index, flags, WFNM_WHOLE, minlen);
	    if (match == WFNM_ERROR) {
		return;
	    } else if (match != WFNM_NOMATCH) {
		str[index] = L'\0';
		break;
	    }
	} while (index--);
	slist++;
    }
}

/* slist の要素である各ワイド文字列に対してマッチングを行い、
 * マッチした部分を置換して返す。
 * slist:   void * にキャストした free 可能なワイド文字列へのポインタの配列
 * pattern: マッチングするパターン
 * subst:   マッチした部分を置換する文字列
 * type:    PT_MATCHHEAD, PT_MATCHTAIL, PT_SUBSTALL フラグ
 * slist の各要素はこの関数内で realloc する。 */
void subst_each(void **slist, const wchar_t *pattern, const wchar_t *subst,
	paramexptype_T type)
{
    if (type & PT_MATCHHEAD) {
	if (type & PT_MATCHTAIL)
	    subst_whole_each(slist, pattern, subst);
	else
	    subst_head_each(slist, pattern, subst);
    } else if (type & PT_MATCHTAIL) {
	subst_tail_each(slist, pattern, subst);
    } else {
	subst_generic_each(slist, pattern, subst, type & PT_SUBSTALL);
    }
}

/* PT_MATCHHEAD かつ PT_MATCHTAIL な subst_each を実際に行う */
void subst_whole_each(
	void **slist, const wchar_t *pattern, const wchar_t *subst)
{
    enum wfnmflags flags = shopt_nocaseglob ? WFNM_CASEFOLD : 0;
    size_t minlen = shortest_match_length(pattern, flags);
    wchar_t *str;
    while ((str = *slist)) {
	size_t match = wfnmatchl(pattern, str, flags, WFNM_WHOLE, minlen);
	if (match == WFNM_ERROR) {
	    break;
	} else if (match != WFNM_NOMATCH) {
	    free(str);
	    *slist = xwcsdup(subst);
	}
	slist++;
    }
}

/* PT_MATCHHEAD だが PT_MATCHTAIL ではない subst_each を実際に行う */
void subst_head_each(
	void **slist, const wchar_t *pattern, const wchar_t *subst)
{
    enum wfnmflags flags = shopt_nocaseglob ? WFNM_CASEFOLD : 0;
    size_t minlen = shortest_match_length(pattern, flags);
    wchar_t *str;
    while ((str = *slist)) {
	size_t match = wfnmatchl(pattern, str, flags, WFNM_LONGEST, minlen);
	if (match == WFNM_ERROR) {
	    break;
	} else if (match != WFNM_NOMATCH) {
	    xwcsbuf_T buf;
	    wb_init(&buf);
	    wb_cat(&buf, subst);
	    wb_cat(&buf, str + match);
	    free(str);
	    *slist = wb_towcs(&buf);
	}
	slist++;
    }
}

/* PT_MATCHTAIL だが PT_MATCHHEAD ではない subst_each を実際に行う */
void subst_tail_each(
	void **slist, const wchar_t *pattern, const wchar_t *subst)
{
    enum wfnmflags flags = shopt_nocaseglob ? WFNM_CASEFOLD : 0;
    size_t minlen = shortest_match_length(pattern, flags);
    wchar_t *str;
    while ((str = *slist)) {
	size_t len = wcslen(str);
	size_t index = 0;
	while (minlen + index <= len) {
	    size_t match = wfnmatchl(
		    pattern, str + index, flags, WFNM_WHOLE, minlen);
	    if (match == WFNM_ERROR) {
		return;
	    } else if (match != WFNM_NOMATCH) {
		xwcsbuf_T buf;
		wb_init(&buf);
		wb_ncat(&buf, str, index);
		wb_cat(&buf, subst);
		free(str);
		*slist = wb_towcs(&buf);
		break;
	    }
	    index++;
	}
	slist++;
    }
}

/* PT_MATCHHEAD でも PT_MATCHTAIL でもない subst_each を実際に行う
 * substall: true なら全てのマッチを置換する */
void subst_generic_each(void **slist,
	const wchar_t *pattern, const wchar_t *subst, bool substall)
{
    enum wfnmflags flags = shopt_nocaseglob ? WFNM_CASEFOLD : 0;
    size_t minlen = shortest_match_length(pattern, flags);
    wchar_t *str;
    while ((str = *slist)) {
	size_t index = 0;
	xwcsbuf_T buf;
	wb_init(&buf);
	while (str[index]) {
	    size_t match = wfnmatchl(
		    pattern, str + index, flags, WFNM_LONGEST, minlen);
	    if (match == WFNM_ERROR) {
		return;
	    } else if (match != WFNM_NOMATCH && match > 0) {
		wb_cat(&buf, subst);
		index += match;
		if (!substall) {
		    wb_cat(&buf, str + index);
		    break;
		} else {
		    continue;
		}
	    } else {
		wb_wccat(&buf, str[index]);
	    }
	    index++;
	}
	free(str);
	*slist = wb_towcs(&buf);
	slist++;
    }
}

/* slist の要素である各ワイド文字列について、その文字数を表す数字の文字列に
 * 置き換える。
 * slist: void * にキャストした free 可能なワイド文字列へのポインタの配列
 * slist の各要素はこの関数内で realloc する。 */
void subst_length_each(void **slist)
{
    wchar_t *str;
    while ((str = *slist)) {
	*slist = malloc_wprintf(L"%zu", wcslen(str));
	free(str);
	slist++;
    }
}


/********** ブレース展開 **********/

/* 配列内の各要素をブレース展開する。
 * slist: void * にキャストした wchar_t * の NULL 終端配列。
 *        各要素はこの関数内で free する。関数自身は free しない。
 * dest:  結果 (新しく malloc したワイド文字列) を入れるリスト。
 * バックスラッシュエスケープしてあるブレースは展開しない。 */
void expand_brace_each(void **restrict slist, plist_T *restrict dest)
{
    while (*slist) {
	expand_brace(*slist, dest);
	slist++;
    }
}

/* 一つの単語をブレース展開する。
 * word: 展開する単語。この関数内で free する。
 * dest: 結果 (新しく malloc したワイド文字列) を入れるリスト。
 * バックスラッシュエスケープしてあるブレースは展開しない。 */
void expand_brace(wchar_t *restrict const word, plist_T *restrict dest)
{
    plist_T elemlist;
    wchar_t *c;
    unsigned nest;
    size_t lastindex, headlength;

    c = word;
start:
    c = escaped_wcspbrk(c, L"{");
    if (!c || !*++c) {  /* L'{' がないか、L'{' が文字列末尾にあるなら無展開 */
	pl_add(dest, word);
	return;
    } else if (tryexpand_brace_sequence(word, c, dest)) {
	return;
    }

    /* elemlist には展開する各要素の先頭の文字へのポインタを入れる */
    pl_init(&elemlist);
    pl_add(&elemlist, c);
    nest = 0;
    while ((c = escaped_wcspbrk(c, L"{,}"))) {
	switch (*c++) {
	    case L'{':
		nest++;
		break;
	    case L',':
		if (nest == 0)
		    pl_add(&elemlist, c);
		break;
	    case L'}':
		if (nest > 0) {
		    nest--;
		    break;
		} else if (elemlist.length == 1) {
		    goto restart;
		} else {
		    pl_add(&elemlist, c);
		    goto done;
		}
	}
    }
restart:
    /* 最初の L'{' に対応する L',' および L'}' が見付からなかったら、
     * 次の L'{' を探すところからやり直す */
    c = elemlist.contents[0];
    pl_destroy(&elemlist);
    goto start;

done:
    lastindex = elemlist.length - 1;
    headlength = (wchar_t *) elemlist.contents[0] - 1 - word;
    for (size_t i = 0; i < lastindex; i++) {
	xwcsbuf_T buf;
	wb_init(&buf);
	wb_ncat(&buf, word, headlength);
	wb_ncat(&buf, elemlist.contents[i],
		(wchar_t *) elemlist.contents[i + 1] -
		(wchar_t *) elemlist.contents[i    ] - 1);
	wb_cat(&buf, elemlist.contents[lastindex]);
	expand_brace(wb_towcs(&buf), dest);
    }
    pl_destroy(&elemlist);
    free(word);
}

/* {01..05} のような数列へのブレース展開を試みる。
 * 失敗すれば何もせずに false を返す。成功すれば word の完全なブレース展開結果を
 * dest に追加する。
 * word:   展開する単語全体。成功すればこの関数内で free する。
 * startc: word の最初の L'{' の直後の文字へのポインタ
 * dest: 結果 (新しく malloc したワイド文字列) を入れるリスト。 */
bool tryexpand_brace_sequence(
	wchar_t *const word, wchar_t *const startc, plist_T *restrict dest)
{
    long start, end, value;
    wchar_t *dotexpect, *braceexpect, *c;
    int startlen, endlen, len;
    bool sign = false;

    assert(startc[-1] == L'{');
    c = startc;

    /* 数列の始点を解析 */
    dotexpect = wcschr(c, L'.');
    if (!dotexpect || c == dotexpect)
	return false;
    startlen = has_leading_zero(c, &sign) ? (dotexpect - c) : 0;
    errno = 0;
    start = wcstol(c, &c, 0);
    if (errno || c != dotexpect || c[1] != L'.')
	return false;

    c += 2;

    /* 数列の終点を解析 */
    braceexpect = wcschr(c, L'}');
    if (!braceexpect || c == braceexpect)
	return false;
    endlen = has_leading_zero(c, &sign) ? (braceexpect - c) : 0;
    errno = 0;
    end = wcstol(c, &c, 0);
    if (errno || c != braceexpect)
	return false;

    /* 数列を展開 */
    value = start;
    len = (startlen > endlen) ? startlen : endlen;
    for (;;) {
	wchar_t *expansion = malloc_wprintf(
		sign ? L"%.*ls%0+*ld%ls" : L"%.*ls%0*ld%ls",
		(int) (startc - 1 - word), word,
		len, value,
		braceexpect + 1);
	expand_brace(expansion, dest);  /* 残りの部分を再帰的に展開 */

	if (value == end)
	    break;
	if (start < end)
	    value++;
	else
	    value--;
    }
    free(word);
    return true;
}

/* 数値の先頭が L'0' で始まるかどうか調べる
 * sign: 数値に正号 L'+' があれば *sign に true を代入する */
bool has_leading_zero(const wchar_t *s, bool *sign)
{
    while (iswspace(*s))
	s++;
    if (*s == L'+') {
	*sign = true;
	s++;
    } else if (*s == L'-') {
	s++;
    }
    return *s == L'0';
}


/********** 単語分割 **********/

/* 単語分割を行う。
 * s:    分割する単語。この関数内で free する。
 * dest: 結果 (新しく malloc したワイド文字列) を入れるリスト
 * 分割は、ifs に従って、バックスラッシュエスケープしていない文字の所で行う。 */
void fieldsplit(wchar_t *restrict const s, const wchar_t *restrict ifs,
	plist_T *restrict dest)
{
    size_t index = 0, startindex = 0;
    size_t savedestlen = dest->length;

    while (s[index]) {
	if (s[index] == L'\\') {
	    index++;
	    if (!s[index])
		break;
	    index++;
	} else if (wcschr(ifs, s[index])) {
	    /* IFS にある文字なので、分割する */
	    bool splitonnonspace = false, nonspace = false;
	    if (startindex < index)
		pl_add(dest, xwcsndup(s + startindex, index - startindex));
	    else
		splitonnonspace = true;
	    do {
		if (!iswspace(s[index])) {
		    if (splitonnonspace)
			pl_add(dest, xwcsdup(L""));
		    splitonnonspace = true;
		    nonspace = true;
		}
		index++;
		if (!s[index]) {
		    if (nonspace && startindex < index)
			pl_add(dest, xwcsdup(L""));
		    break;
		}
	    } while (wcschr(ifs, s[index]));
	    startindex = index;
	} else {
	    index++;
	}
    }
    if (savedestlen == dest->length) {
	assert(startindex == 0);  /* 結果的に一回も分割しなかった場合 */
	pl_add(dest, s);
    } else {
	if (startindex < index)
	    pl_add(dest, xwcsndup(s + startindex, index - startindex));
	free(s);
    }
}

/* 単語分割を行う。
 * src:  分割する単語 (ワイド文字列) の配列。この関数内で recfree する。
 * dest: 結果 (新しく malloc したワイド文字列) を入れるリスト
 * 分割は、IFS に従って、バックスラッシュエスケープしていない文字の所で行う。 */
void fieldsplit_all(void **restrict const src, plist_T *restrict dest)
{
    void **s = src;
    const wchar_t *ifs = getvar(VAR_IFS);
    if (!ifs)
	ifs = L" \t\n";
    while (*s) {
	fieldsplit(*s, ifs, dest);
	s++;
    }
    free(src);
}


/********** 文字列のエスケープ **********/

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
	    default:
		wb_wccat(buf, L'\\');
		wb_wccat(buf, **ss);
		break;
	}
	(*ss)++;
    }
}

/* s の文字のうち、t に含まれるものをバックスラッシュエスケープして、
 * 新しく malloc した文字列として返す。t が NULL なら全部の文字をエスケープする
 * 戻り値: エスケープした s */
wchar_t *escape(const wchar_t *restrict s, const wchar_t *restrict t)
{
    xwcsbuf_T buf;
    wb_init(&buf);
    while (*s) {
	if (!t || wcschr(t, *s))
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

/* 一部の文字だけがバックスラッシュエスケープしてある文字列を受け取り、
 * 全ての文字をバックスラッシュエスケープした状態にして返す。
 * 戻り値: 新しく malloc した文字列。 */
wchar_t *reescape(const wchar_t *s)
{
    xwcsbuf_T buf;

    wb_init(&buf);
    while (*s) {
	wb_wccat(&buf, L'\\');
	if (*s == L'\\')
	    s++;
	if (*s)
	    wb_wccat(&buf, *s++);
    }
    return wb_towcs(&buf);
}

/* 一部の文字だけがバックスラッシュエスケープしてある文字列へのポインタの配列を
 * 受け取り、各文字列の全ての文字をバックスラッシュエスケープした状態に
 * 置き換える。この時、配列の要素である各ポインタは realloc する。
 * 戻り値: 内容が書き換わった wcsarray */
void **reescape_full_array(void **const wcsarray)
{
    void **ary = wcsarray;
    while (*ary) {
	wchar_t *esc = reescape(*ary);
	free(*ary);
	*ary = esc;
	ary++;
    }
    return wcsarray;
}

/* wcspbrk と同じだが、wcs 内のバックスラッシュエスケープした文字は無視する */
wchar_t *escaped_wcspbrk(const wchar_t *wcs, const wchar_t *accept)
{
    while (*wcs) {
	if (*wcs == L'\\') {
	    wcs++;
	    if (!*wcs)
		break;
	    wcs++;
	    continue;
	}
	if (wcschr(accept, *wcs))
	    return (wchar_t *) wcs;
	wcs++;
    }
    return NULL;
}


/********** ファイル名展開 (glob) **********/

/* 現在のシェルの設定から、wglob のオプションフラグの値を得る */
enum wglbflags get_wglbflags(void)
{
    enum wglbflags flags = 0;
    if (shopt_nocaseglob)   flags |= WGLB_CASEFOLD;
    if (shopt_dotglob)      flags |= WGLB_PERIOD;
    if (shopt_markdirs)     flags |= WGLB_MARK;
    if (shopt_extendedglob) flags |= WGLB_RECDIR;
    return flags;
}

/* 指定した各パターンについて glob を行い、結果をリストに入れる。
 * patterns: void * にキャストしたワイド文字列へのポインタの NULL 終端配列。
 * list: 結果のマルチバイト文字列へのポインタを入れるリスト。 */
void do_glob_each(void *const *restrict patterns, plist_T *restrict list)
{
    enum wglbflags flags = get_wglbflags();

    while (*patterns) {
	const wchar_t *pat = *patterns;
	if (pattern_has_special_char(pat)) {
	    size_t oldlen = list->length;
	    wglob(pat, flags, list);
	    if (!shopt_nullglob && oldlen == list->length)
		goto addpattern;
	} else {
	    /* pat に L'*' や L'?' などの文字が入っていなければ
	     * わざわざ glob する必要はない。 */
	    char *v;
addpattern:
	    v = realloc_wcstombs(unescape(pat));
	    if (!v) {
		xerror(0, Ngt("expanded word contains characters that "
			    "cannot be converted to wide characters and "
			    "is replaced with null string"));
		v = xstrdup("");
	    }
	    pl_add(list, v);
	}
	patterns++;
    }
}


/* vim: set ts=8 sts=4 sw=4 noet: */
