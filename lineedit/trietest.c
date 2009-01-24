//This is a test tool, not part of yash
//!make trie.o && c99 -o trietest trietest.c trie.o
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "trie.h"

void alloc_failed(void)
{
	abort();
}

void print(trie_T *t, const char *key)
{
	trieget_T tg = trie_get(t, key, strlen(key));

	printf("%s: ", key);
	switch (tg.type) {
		case TG_NOMATCH:
			printf("nomatch %zu\n", tg.matchlength);
			break;
		case TG_UNIQUE:
			printf("unique %zu (%ls)\n", tg.matchlength, tg.value.keyseq);
			break;
		case TG_AMBIGUOUS:
			printf("ambig %zu\n", tg.matchlength);
			break;
		default:
			printf("ERROR: type=%d", (int) tg.type);
			assert(false);
	}
}

void print_null(trie_T *t)
{
	trieget_T tg = trie_get(t, "", 1);

	printf("<null>: ");
	switch (tg.type) {
		case TG_NOMATCH:
			printf("nomatch %zu\n", tg.matchlength);
			break;
		case TG_UNIQUE:
			printf("unique %zu (%ls)\n", tg.matchlength, tg.value.keyseq);
			break;
		case TG_AMBIGUOUS:
			printf("ambig %zu\n", tg.matchlength);
			break;
		default:
			printf("ERROR: type=%d", (int) tg.type);
			assert(false);
	}
}

int main(int argc, char **argv)
{
	(void) argc, (void) argv;

	trie_T *t = trie_create();

	print_null(t);
	print(t, "");

	t = trie_set_null(t, (trievalue_T) (const wchar_t *) L"Null");
	t = trie_set(t, "abc", (trievalue_T) (const wchar_t *) L"ABC");
	t = trie_set(t, "abca", (trievalue_T) (const wchar_t *) L"ABCA");
	t = trie_set(t, "abcb", (trievalue_T) (const wchar_t *) L"ABCB");
	t = trie_set(t, "abcc", (trievalue_T) (const wchar_t *) L"ABCC");
	t = trie_set(t, "abcd", (trievalue_T) (const wchar_t *) L"");
	t = trie_set(t, "abcd", (trievalue_T) (const wchar_t *) L"ABCD");
	t = trie_set(t, "abcde", (trievalue_T) (const wchar_t *) L"ABCDE");
	t = trie_set(t, "abce", (trievalue_T) (const wchar_t *) L"ABCE");
	t = trie_set(t, "abcf", (trievalue_T) (const wchar_t *) L"ABCF");
	t = trie_set(t, "abcg", (trievalue_T) (const wchar_t *) L"ABCG");
	t = trie_set(t, "abch", (trievalue_T) (const wchar_t *) L"ABCH");
	t = trie_set(t, "b", (trievalue_T) (const wchar_t *) L"B");

	print_null(t);
	print(t, "");
	print(t, "ab");
	print(t, "abc");
	print(t, "abcd");
	print(t, "abcde");
	print(t, "abcdex");
	print(t, "abcdx");
	print(t, "abcx");
	print(t, "abx");
	print(t, "ax");
	print(t, "b");
	print(t, "x");

	t = trie_remove(t, "b");
	t = trie_remove(t, "c");
	t = trie_set(t, "a", (trievalue_T) (const wchar_t *) L"A");

	print(t, "a");
	print(t, "ax");
	print(t, "ab");
	print(t, "abx");
	print(t, "abc");
	print(t, "abcx");
	print(t, "b");
	print(t, "x");

	trie_destroy(t);

	exit(EXIT_SUCCESS);
}
