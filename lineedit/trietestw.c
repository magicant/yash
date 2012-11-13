// This is a test tool, not part of yash
//   make trie.o 
//   (cd .. && make strbuf.o)
//   c99 -o trietestw trietestw.c trie.o ../strbuf.o ../util.o
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "trie.h"

void print(const trie_T *t, const wchar_t *key)
{
    trieget_T tg = trie_getw(t, key);

    printf("%-10ls: ", key);
    switch (tg.type) {
	case TG_NOMATCH:
	    printf("nomatch %zu\n", tg.matchlength);
	    break;
	case TG_EXACTMATCH:
	    printf("exact   %zu (%ls)\n", tg.matchlength, tg.value.keyseq);
	    break;
	case TG_PREFIXMATCH:
	    printf("prefix  %zu\n", tg.matchlength);
	    break;
	case TG_AMBIGUOUS:
	    printf("ambig   %zu (%ls)\n", tg.matchlength, tg.value.keyseq);
	    break;
	default:
	    printf("ERROR: type=%d\n", (int) tg.type);
	    assert(false);
    }
}

int main(int argc, char **argv)
{
    (void) argc, (void) argv;

    trie_T *t = trie_create();

    print(t, L"");

#define make_trievalue(s) ((trievalue_T) { .keyseq = (s) })

    t = trie_setw(t, L"abc", make_trievalue(L"ABC"));
    t = trie_setw(t, L"abca", make_trievalue(L"ABCA"));
    t = trie_setw(t, L"abcb", make_trievalue(L"ABCB"));
    t = trie_setw(t, L"abcc", make_trievalue(L"ABCC"));
    t = trie_setw(t, L"abcd", make_trievalue(L""));
    t = trie_setw(t, L"abcd", make_trievalue(L"ABCD"));
    t = trie_setw(t, L"abcde", make_trievalue(L"ABCDE"));
    t = trie_setw(t, L"abce", make_trievalue(L"ABCE"));
    t = trie_setw(t, L"abcf", make_trievalue(L"ABCF"));
    t = trie_setw(t, L"abcg", make_trievalue(L"ABCG"));
    t = trie_setw(t, L"abch", make_trievalue(L"ABCH"));
    t = trie_setw(t, L"b", make_trievalue(L"B"));

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
    t = trie_setw(t, L"a", make_trievalue(L"A"));

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

/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
