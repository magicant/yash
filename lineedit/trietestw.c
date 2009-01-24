//This is a test tool, not part of yash
//!make trie.o && c99 -o trietestw trietestw.c trie.o
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "trie.h"

void alloc_failed(void)
{
	abort();
}

void print(trie_T *t, const wchar_t *key)
{
	trieget_T tg = trie_getw(t, key);

	printf("%ls: ", key);
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

	print(t, L"");

	t = trie_setw(t, L"abc", (trievalue_T) L"ABC");
	t = trie_setw(t, L"abca", (trievalue_T) L"ABCA");
	t = trie_setw(t, L"abcb", (trievalue_T) L"ABCB");
	t = trie_setw(t, L"abcc", (trievalue_T) L"ABCC");
	t = trie_setw(t, L"abcd", (trievalue_T) L"");
	t = trie_setw(t, L"abcd", (trievalue_T) L"ABCD");
	t = trie_setw(t, L"abcde", (trievalue_T) L"ABCDE");
	t = trie_setw(t, L"abce", (trievalue_T) L"ABCE");
	t = trie_setw(t, L"abcf", (trievalue_T) L"ABCF");
	t = trie_setw(t, L"abcg", (trievalue_T) L"ABCG");
	t = trie_setw(t, L"abch", (trievalue_T) L"ABCH");
	t = trie_setw(t, L"b", (trievalue_T) L"B");

	print(t, L"");
	print(t, L"ab");
	print(t, L"abc");
	print(t, L"abcd");
	print(t, L"abcde");
	print(t, L"abcdex");
	print(t, L"abcdx");
	print(t, L"abcx");
	print(t, L"abx");
	print(t, L"ax");
	print(t, L"b");
	print(t, L"x");

	t = trie_removew(t, L"b");
	t = trie_removew(t, L"c");
	t = trie_setw(t, L"a", (trievalue_T) L"A");

	print(t, L"a");
	print(t, L"ax");
	print(t, L"ab");
	print(t, L"abx");
	print(t, L"abc");
	print(t, L"abcx");
	print(t, L"b");
	print(t, L"x");

	trie_destroy(t);

	exit(EXIT_SUCCESS);
}
