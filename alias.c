/* Yash: yet another shell */
/* alias.c: alias functionality */
/* © 2007 magicant */

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


#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "yash.h"
#include "util.h"
#include "alias.h"
#include <assert.h>


static void aliasfree(ALIAS *a);
void init_alias(void);
void set_alias(const char *name, const char *value, bool global);
int remove_alias(const char *name);
void remove_all_aliases(void);
ALIAS *get_alias(const char *name);
int for_all_aliases(int (*func)(const char *name, ALIAS *alias));
void alias_reset(void);
void expand_alias(struct strbuf *buf, size_t i, bool global);

#ifndef ALIAS_EXPAND_MAX
# define ALIAS_EXPAND_MAX 16
#endif


/* エイリアスの集合。エイリアス名から ALIAS へのポインタへのハッシュテーブル */
struct hasht aliases;


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
		.global = global,
		.valid_len = SIZE_MAX,
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
	int freer(const char *name __attribute__((unused)), ALIAS *a) {
		aliasfree(a);
		return 0;
	}
	ht_each(&aliases, (int (*)(const char *, void *)) freer);
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

/* エイリアスの展開を行う前に状態をリセットする。
 * この関数は一度の read_and_parse につき一度呼び出される。 */
void alias_reset(void)
{
	int reset(const char *name __attribute__((unused)), ALIAS *alias) {
		alias->valid_len = SIZE_MAX;
		return 0;
	}
	for_all_aliases(reset);
}

/* エイリアスを展開する。
 * 文字列バッファの指定した位置にあるトークンにエイリアスが設定されていれば、
 * そのトークンをエイリアスに置き換える。
 * global が true ならばグローバルエイリアスのみ展開する。 */
void expand_alias(struct strbuf *buf, size_t i, bool global)
{
	char *s;
	size_t tokenlen, remlen;
	int count = 0;
	ALIAS *alias;

start:
	s = buf->contents + i;
	remlen = buf->length - i;
	tokenlen = strcspn(s, " \t$<>\\'\"`;&|()#\n\r");
	if (tokenlen) {
		char savechar = s[tokenlen];
		s[tokenlen] = '\0';
		alias = get_alias(s);
		s[tokenlen] = savechar;
		if (alias && remlen <= alias->valid_len && (!global || alias->global)) {
			alias->valid_len = remlen - tokenlen;
			sb_replace(buf, i, tokenlen, alias->value);
			if (++count <= ALIAS_EXPAND_MAX)
				goto start;
		}
	}
}
