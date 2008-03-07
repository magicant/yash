/* Yash: yet another shell */
/* util.h: utility functions */
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


#ifndef UTIL_H
#define UTIL_H

#ifdef NINLINE
# define NO_UTIL_INLINE
#endif

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/types.h>
#include <time.h>
#ifndef NO_UTIL_INLINE
# include <stddef.h>
# include <stdint.h>
#endif


/* General functions */

void *xcalloc(size_t nmemb, size_t size)
	__attribute__((malloc, warn_unused_result));
void *xmalloc(size_t size)
	__attribute__((malloc, warn_unused_result));
void *xrealloc(void *ptr, size_t size)
	__attribute__((malloc, warn_unused_result));
size_t xstrnlen(const char *s, size_t maxlen)
	__attribute__((nonnull));
char *xstrndup(const char *s, size_t len)
	__attribute__((malloc, warn_unused_result, nonnull));
char *xstrdup(const char *s)
	__attribute__((malloc, warn_unused_result, nonnull));
#ifndef NO_UTIL_INLINE
# ifdef HAVE_STRNLEN
#  define xstrnlen(s,maxlen) strnlen(s,maxlen)
# endif
# define xstrdup(s) xstrndup(s, SIZE_MAX)
#endif

char **strarydup(char *const *ary)
	__attribute__((malloc, warn_unused_result, nonnull));
size_t parylen(void *const *ary)
	__attribute__((nonnull));
void recfree(void **ary, void freer(void *elem));
char *skipblanks(const char *s)
	__attribute__((nonnull));
char *skipspaces(const char *s)
	__attribute__((nonnull));
char *skipwhites(const char *s)
	__attribute__((nonnull));
char *matchprefix(const char *s, const char *prefix)
	__attribute__((nonnull));
char *strchug(char *s)
	__attribute__((nonnull));
char *strchomp(char *s)
	__attribute__((nonnull));
char *strjoin(
		int argc, char *const *restrict argv, const char *restrict padding)
	__attribute__((nonnull(2)));
char *mprintf(const char *restrict format, ...)
	__attribute__((malloc, nonnull(1), format(printf,1,2)));

#define xisalnum(c)  (isalnum((unsigned char) (c)))
#define xisalpha(c)  (isalpha((unsigned char) (c)))
#define xisblank(c)  (isblank((unsigned char) (c)))
#define xiscntrl(c)  (iscntrl((unsigned char) (c)))
#define xisdigit(c)  (isdigit((unsigned char) (c)))
#define xisgraph(c)  (isgraph((unsigned char) (c)))
#define xislower(c)  (islower((unsigned char) (c)))
#define xisprint(c)  (isprint((unsigned char) (c)))
#define xispunct(c)  (ispunct((unsigned char) (c)))
#define xisspace(c)  (isspace((unsigned char) (c)))
#define xisupper(c)  (isupper((unsigned char) (c)))
#define xisxdigit(c) (isxdigit((unsigned char) (c)))
#define xtoupper(c)  (toupper((unsigned char) (c)))
#define xtolower(c)  (tolower((unsigned char) (c)))


/* Arithmetic Utilities */

/* 整数型 type が符号付きかどうか */
#define IS_TYPE_SIGNED(type) ((type) 1 > (type) -1)

/* 整数型 type を文字列に変換した場合の最大長。
 * CHAR_MAX の為に limits.h が必要。 */
#define INT_STRLEN_BOUND(type) \
	((sizeof(type) * CHAR_MAX - IS_TYPE_SIGNED(type)) * 31 / 100 \
	 + 1 + IS_TYPE_SIGNED(type))


/* Error Utilities */

extern unsigned yash_error_message_count;

void xerror(int status, int errno_, const char *restrict format, ...)
	__attribute__((format(printf,3,4)));


/* xgetopt */

extern char *xoptarg;
extern int xoptind, xoptopt;
extern bool xopterr;

struct xoption {
	const char *name;
	enum { xno_argument = 0, xrequired_argument, xoptional_argument, } has_arg;
	int *flag;
	int val;
};

int xgetopt_long(char *const *restrict argv, const char *restrict optstring,
		const struct xoption *restrict longopts, int *restrict longindex)
	__attribute__((nonnull(1,2)));
int xgetopt(char *const *restrict argv, const char *restrict optstring)
	__attribute__((nonnull(1,2)));
#ifndef NO_UTIL_INLINE
# define xgetopt(argv,optstring) xgetopt_long(argv, optstring, NULL, NULL)
#endif


/* IO Utility */

int xclose(int fd);


/* Temporary file maker */

#define XMKSTEMP_NAME "/tmp/yash-XXXXXX"
int xmkstemp(char *tempname)
	__attribute__((nonnull));


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
void sb_ninsert(struct strbuf *restrict buf,
		size_t i, const char *restrict s, size_t n)
	__attribute__((nonnull));
void sb_insert(struct strbuf *restrict buf, size_t i, const char *restrict s)
	__attribute__((nonnull));
void sb_nappend(struct strbuf *restrict buf, const char *restrict s, size_t n)
	__attribute__((nonnull));
void sb_append(struct strbuf *restrict buf, const char *restrict s)
	__attribute__((nonnull));
#ifndef NO_UTIL_INLINE
# define sb_insert(buf,i,s)  sb_ninsert(buf, i, s, SIZE_MAX)
# define sb_nappend(buf,s,n) sb_ninsert(buf, SIZE_MAX, s, n)
# define sb_append(buf,s)    sb_nappend(buf, s, SIZE_MAX)
#endif

void sb_cappend(struct strbuf *buf, char c)
	__attribute__((nonnull));
void sb_replace(struct strbuf *restrict buf,
		size_t i, size_t n, const char *restrict s)
	__attribute__((nonnull));
int sb_vprintf(struct strbuf *restrict buf,
		const char *restrict format, va_list ap)
	__attribute__((nonnull(1,2),format(printf,2,0)));
int sb_printf(struct strbuf *restrict buf, const char *restrict format, ...)
	__attribute__((nonnull(1,2),format(printf,2,3)));
size_t sb_strftime(struct strbuf *restrict buf,
		const char *restrict format, const struct tm *restrict tm)
	__attribute__((nonnull,format(strftime,2,0)));


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
void pl_insert(struct plist *list, size_t i, const void *e)
	__attribute__((nonnull(1)));
void pl_aninsert(struct plist *list, size_t i, void *const *ps, size_t n)
	__attribute__((nonnull));
void pl_append(struct plist *list, const void *e)
	__attribute__((nonnull(1)));
void pl_anappend(struct plist *list, void *const *ps, size_t n)
	__attribute__((nonnull));
void pl_ainsert(struct plist *list, size_t i, void *const *ps)
	__attribute__((nonnull));
void pl_aappend(struct plist *list, void *const *ps)
	__attribute__((nonnull));
void pl_remove(struct plist *list, size_t i, size_t count)
	__attribute__((nonnull));
#ifndef NO_UTIL_INLINE
# define pl_append(list,e)      pl_insert(list, SIZE_MAX, e)
# define pl_anappend(list,ps,n) pl_aninsert(list, SIZE_MAX, ps, n)
# define pl_ainsert(list,i,ps)  pl_aninsert(list, i, ps, parylen(ps))
# define pl_aappend(list,ps)    pl_ainsert(list, SIZE_MAX, ps)
#endif


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
struct keyvaluepair {
	const char *key;
	void       *value;
};

void ht_init(struct hasht *ht)
	__attribute__((nonnull));
void ht_destroy(struct hasht *ht)
	__attribute__((nonnull));
void ht_ensurecap(struct hasht *ht, size_t newcap)
	__attribute__((nonnull));
void ht_trim(struct hasht *ht)
	__attribute__((nonnull));
void ht_freeclear(struct hasht *ht, void freer(void *value))
	__attribute__((nonnull(1)));
void ht_clear(struct hasht *ht)
	__attribute__((nonnull));
void *ht_get(struct hasht *ht, const char *key)
	__attribute__((nonnull(1)));
void *ht_set(struct hasht *ht, const char *key, const void *value)
	__attribute__((nonnull(1)));
void *ht_remove(struct hasht *ht, const char *key)
	__attribute__((nonnull(1)));
int ht_each(struct hasht *ht, int func(const char *key, void *value))
	__attribute__((nonnull));
struct keyvaluepair ht_next(struct hasht *ht, size_t *indexp)
	__attribute__((nonnull));
#ifndef NO_UTIL_INLINE
# define ht_clear(ht) ht_freeclear(ht, NULL)
#endif


#endif /* UTIL_H */
