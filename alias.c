/* Yash: yet another shell */
/* alias.c: alias functionality */
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


#define  _POSIX_C_SOURCE 200112L
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "yash.h"
#include "util.h"
#include "alias.h"
#include <assert.h>


#ifndef ALIAS_EXPAND_MAX
# define ALIAS_EXPAND_MAX 16
#endif


/* エイリアス置換を行うかどうか。 */
bool enable_alias = true;

/* エイリアスの集合。エイリアス名から ALIAS へのポインタへのハッシュテーブル */
static struct hasht aliases;


static void aliasfree(ALIAS *a)
{
	if (a) {
		free(a->value);
		free(a);
	}
}

/* エイリアスモジュールを初期化する */
void init_alias(void)
{
	ht_init(&aliases);
}

/* エイリアスを追加/変更する。
 * エイリアスの名前 name は 1 文字以上でなければならない。 */
void set_alias(const char *name, const char *value, bool global)
{
	assert(name && value && *name);

	ALIAS *a = xmalloc(sizeof *a);
	*a = (ALIAS) {
		.value = xstrdup(value),
		.endblank = value[0] && xisblank(value[strlen(value) - 1]),
		.global = global,
		.processing = false,
	};
	aliasfree(ht_set(&aliases, name, a));
}

/* エイリアスを削除する
 * 戻り値: エイリアスが正しく削除されたら 0、見付からなかったら -1。 */
int remove_alias(const char *name)
{
	ALIAS *a = ht_remove(&aliases, name);
	if (a) {
		aliasfree(a);
		return 0;
	} else {
		return -1;
	}
}

/* 全エイリアスを削除する。 */
void remove_all_aliases(void)
{
	ht_freeclear(&aliases, (void (*)(void *)) aliasfree);
}

/* エイリアスを取得する
 * 戻り値: 見付かったエイリアス。見付からなかったら NULL。*/
ALIAS *get_alias(const char *name)
{
	return ht_get(&aliases, name);
}

/* 全てのエイリアスに対して一度ずつ関数 func を呼び出す。
 * エイリアスの順番は、不定である。
 * 関数 func が非 0 を返すと、処理を中止してただちにその値を返す。
 * func が最後まで全て 0 を返すと、0 を返す。
 * 関数 func の中で name や value を変更したり、
 * add_alias や remove_alias を呼び出したりしてはならない。 */
int for_all_aliases(int (*func)(const char *name, ALIAS *alias))
{
	return ht_each(&aliases, (int (*)(const char *, void *)) func);
}

/* エイリアスを置換する。
 * 文字列バッファの指定した位置にあるトークンにエイリアスが設定されていれば、
 * そのトークンをエイリアスに置き換える。
 * global が true ならばグローバルエイリアスのみ展開する。 */
void subst_alias(struct strbuf *buf, size_t i, bool global)
{
	static unsigned recur = 0;
	char *s;
	size_t tokenlen, remlen;
	ALIAS *alias;

	if (!enable_alias || recur == ALIAS_EXPAND_MAX)
		return;
	recur++;

	s = buf->contents + i;
	remlen = buf->length - i;
	tokenlen = strcspn(s, " \t$<>\\'\"`;&|()#\n\r");
	if (tokenlen > 0 && (!s[tokenlen] || !strchr("$\\'\"`", s[tokenlen]))) {
		char savechar = s[tokenlen];
		s[tokenlen] = '\0';
		alias = get_alias(s);
		s[tokenlen] = savechar;
		if (alias && !alias->processing && (!global || alias->global)) {
			sb_replace(buf, i, tokenlen, alias->value);
			if (alias->endblank) {
				/* endblank なら次のトークンも置換 */
				size_t ii = buf->length - (remlen - tokenlen);
				ii = skipblanks(buf->contents + ii) - buf->contents;
				subst_alias(buf, ii, global);
			}
			alias->processing = true;
			subst_alias(buf, i, global);
			alias->processing = false;
		}
	}
	recur--;
}
