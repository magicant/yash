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
	__attribute__((malloc, warn_unused_result));
void *xmalloc(size_t size)
	__attribute__((malloc, warn_unused_result));
void *xrealloc(void *ptr, size_t size)
	__attribute__((malloc, warn_unused_result));
char *xstrndup(const char *s, size_t len)
	__attribute__((malloc, warn_unused_result, nonnull));

#ifdef NINLINE
char *xstrdup(const char *s)
	__attribute__((malloc, warn_unused_result, nonnull));
#else
# include <stdint.h>
static inline __attribute__((malloc, warn_unused_result, nonnull))
char *xstrdup(const char *s)
{
	return xstrndup(s, SIZE_MAX);
}
#endif /* !NINLINE */

char **strarydup(char **ary)
	__attribute__((malloc, warn_unused_result, nonnull));
size_t parylen(void *const *ary)
	__attribute__((nonnull));
void recfree(void **ary);
char *skipblanks(const char *s)
	__attribute__((nonnull));
char *skipspaces(const char *s)
	__attribute__((nonnull));
char *skipwhites(const char *s)
	__attribute__((nonnull));
char *strchug(char *s)
	__attribute__((nonnull));
char *strchomp(char *s)
	__attribute__((nonnull));
char *strjoin(int argc, char *const *argv, const char *padding)
	__attribute__((nonnull(2)));
char *read_all(int fd)
	__attribute__((malloc));

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

void sb_init(struct strbuf *buf)
	__attribute__((nonnull));
void sb_destroy(struct strbuf *buf)
	__attribute__((nonnull));
char *sb_tostr(struct strbuf *buf)
	__attribute__((nonnull));
void sb_setmax(struct strbuf *buf, size_t newmax)
	__attribute__((nonnull));
void sb_trim(struct strbuf *buf)
	__attribute__((nonnull));
void sb_clear(struct strbuf *buf)
	__attribute__((nonnull));
void sb_ninsert(struct strbuf *buf, size_t i, const char *s, size_t n)
	__attribute__((nonnull));

#ifdef NINLINE
void sb_insert(struct strbuf *buf, size_t i, const char *s)
	__attribute__((nonnull));
void sb_nappend(struct strbuf *buf, const char *s, size_t n)
	__attribute__((nonnull));
void sb_append(struct strbuf *buf, const char *s)
	__attribute__((nonnull));
#else
static inline __attribute__((nonnull))
void sb_insert(struct strbuf *buf, size_t i, const char *s)
{
	return sb_ninsert(buf, i, s, SIZE_MAX);
}
static inline __attribute__((nonnull))
void sb_nappend(struct strbuf *buf, const char *s, size_t n)
{
	return sb_ninsert(buf, SIZE_MAX, s, n);
}
static inline __attribute__((nonnull))
void sb_append(struct strbuf *buf, const char *s)
{
	return sb_nappend(buf, s, SIZE_MAX);
}
#endif /* !NINLINE */

void sb_cappend(struct strbuf *buf, char c)
	__attribute__((nonnull));
void sb_replace(struct strbuf *buf, size_t i, size_t n, const char *s)
	__attribute__((nonnull));
int sb_vprintf(struct strbuf *buf, const char *format, va_list ap)
	__attribute__((nonnull, format(printf, 2, 0)));
int sb_printf(struct strbuf *buf, const char *format, ...)
	__attribute__((nonnull, format(printf, 2, 3)));


/* Pointer lists */

#define PLIST_INITSIZE 7
struct plist {
	void **contents;
	size_t length;
	size_t maxlength;
};
void pl_init(struct plist *list)
	__attribute__((nonnull));
void pl_destroy(struct plist *list)
	__attribute__((nonnull));
void **pl_toary(struct plist *list)
	__attribute__((nonnull));
void pl_setmax(struct plist *list, size_t newmax)
	__attribute__((nonnull));
void pl_trim(struct plist *list)
	__attribute__((nonnull));
void pl_clear(struct plist *list)
	__attribute__((nonnull));
void pl_insert(struct plist *list, size_t i, void *e)
	__attribute__((nonnull(1)));
void pl_aninsert(struct plist *list, size_t i, void **ps, size_t n)
	__attribute__((nonnull));

#ifdef NINLINE
void pl_append(struct plist *list, void *e)
	__attribute__((nonnull(1)));
void pl_anappend(struct plist *list, void **ps, size_t n)
	__attribute__((nonnull));
void pl_ainsert(struct plist *list, size_t i, void **ps)
	__attribute__((nonnull));
void pl_aappend(struct plist *list, void **ps)
	__attribute__((nonnull));
#else
static inline __attribute__((nonnull(1)))
void pl_append(struct plist *list, void *e)
{
	return pl_insert(list, SIZE_MAX, e);
}
static inline __attribute__((nonnull))
void pl_anappend(struct plist *list, void **ps, size_t n)
{
	return pl_aninsert(list, SIZE_MAX, ps, n);
}
static inline __attribute__((nonnull))
void pl_ainsert(struct plist *list, size_t i, void **ps)
{
	return pl_aninsert(list, i, ps, parylen(ps));
}
static inline __attribute__((nonnull))
void pl_aappend(struct plist *list, void **ps)
{
	return pl_aninsert(list, SIZE_MAX, ps, parylen(ps));
}
#endif /* !NINLINE */


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
void ht_init(struct hasht *ht)
	__attribute__((nonnull));
void ht_destroy(struct hasht *ht)
	__attribute__((nonnull));
void ht_ensurecap(struct hasht *ht, size_t newcap)
	__attribute__((nonnull));
void ht_trim(struct hasht *ht)
	__attribute__((nonnull));
void ht_clear(struct hasht *ht)
	__attribute__((nonnull));
void *ht_get(struct hasht *ht, const char *key)
	__attribute__((nonnull(1)));
void *ht_set(struct hasht *ht, const char *key, void *value)
	__attribute__((nonnull(1)));
void *ht_remove(struct hasht *ht, const char *key)
	__attribute__((nonnull(1)));
int ht_each(struct hasht *ht, int (*func)(const char *key, void *value))
	__attribute__((nonnull));


#endif /* UTIL_H */
