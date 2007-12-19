/* Yash: yet another shell */
/* util.h: utility functions */
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


#ifndef UTIL_H
#define UTIL_H

#include <sys/types.h>


/* General functions */

void *xcalloc(size_t nmemb, size_t size)
	__attribute__((malloc));
void *xmalloc(size_t size)
	__attribute__((malloc));
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *s)
	__attribute__((malloc));
char *xstrndup(const char *s, size_t len)
	__attribute__((malloc));
char **straryclone(char **ary)
	__attribute__((malloc));
void recfree(void **ary);
char *skipblanks(const char *s);
char *skipspaces(const char *s);
char *skipwhites(const char *s);
char *strchug(char *s);
char *strchomp(char *s);
char *strjoin(int argc, char *const *argv, const char *padding);

#define MAX(X,Y) \
	({ typeof(X) _X = (X); typeof(Y) _Y = (Y); _X > _Y ? _X : _Y; })
#define MIN(X,Y) \
	({ typeof(X) _X = (X); typeof(Y) _Y = (Y); _X < _Y ? _X : _Y; })


/* String buffers */

#define STRBUF_INITSIZE 15
struct strbuf {
	char *contents;
	size_t length;
	size_t maxlength;
};

void strbuf_init(struct strbuf *buf);
void strbuf_destroy(struct strbuf *buf);
char *strbuf_tostr(struct strbuf *buf);
void strbuf_setmax(struct strbuf *buf, size_t newmax);
void strbuf_trim(struct strbuf *buf);
void strbuf_clear(struct strbuf *buf);
void strbuf_ninsert(struct strbuf *buf, size_t i, const char *s, size_t n);
void strbuf_insert(struct strbuf *buf, size_t i, const char *s);
void strbuf_nappend(struct strbuf *buf, const char *s, size_t n);
void strbuf_append(struct strbuf *buf, const char *s);
void strbuf_cappend(struct strbuf *buf, char c);
void strbuf_replace(struct strbuf *buf, size_t i, size_t n, const char *s);
int strbuf_vprintf(struct strbuf *buf, const char *format, va_list ap)
	__attribute__((format (printf, 2, 0)));
int strbuf_printf(struct strbuf *buf, const char *format, ...)
	__attribute__((format (printf, 2, 3)));


/* Pointer lists */

#define PLIST_INITSIZE 7
struct plist {
	void **contents;
	size_t length;
	size_t maxlength;
};
void plist_init(struct plist *list);
void plist_destroy(struct plist *list);
void **plist_toary(struct plist *list);
void plist_setmax(struct plist *list, size_t newmax);
void plist_trim(struct plist *list);
void plist_clear(struct plist *list);
void plist_insert(struct plist *list, size_t i, void *e);
void plist_append(struct plist *list, void *e);


/* Hashtables */

#define HASHT_INITSIZE 5
struct hasht {
	size_t   capacity;
	size_t   count;
	ssize_t *indices;
	ssize_t  nullindex;
	ssize_t  tailindex;
	struct hash_entry {
		ssize_t   next;
		unsigned  hash;
		char     *key;
		void     *value;
	}       *entries;
};
void ht_init(struct hasht *ht);
void ht_destroy(struct hasht *ht);
void ht_ensurecap(struct hasht *ht, size_t newcap);
void ht_trim(struct hasht *ht);
void ht_clear(struct hasht *ht);
void *ht_get(struct hasht *ht, const char *key);
void *ht_set(struct hasht *ht, const char *key, void *value);
void *ht_remove(struct hasht *ht, const char *key);
int ht_each(struct hasht *ht, int (*func)(const char *key, void *value));


#endif /* UTIL_H */
