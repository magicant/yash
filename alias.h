/* Yash: yet another shell */
/* alias.h: interface to alias functionality */
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


#ifndef ALIAS_H
#define ALIAS_H

#include <stdbool.h>
#include <stddef.h>


extern bool enable_alias;
extern struct hasht aliases;

/* エイリアスのエントリ。 */
/* processing はエイリアスを展開する際に使う。 */
typedef struct alias {
	char   *value;
	bool    endblank; /* value の末尾が blank かどうか */
	bool    global;   /* グローバルエイリアスかどうか */
	bool    processing;
} ALIAS;

void init_alias(void);
void finalize_alias(void);
void set_alias(const char *name, const char *value, bool global);
int remove_alias(const char *name);
void remove_all_aliases(void);
ALIAS *get_alias(const char *name);
int for_all_aliases(int (*func)(const char *name, ALIAS *alias));
void subst_alias(struct strbuf *buf, size_t i, bool global)
	__attribute__((nonnull));


#endif /* ALIAS_H */
