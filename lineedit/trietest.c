// This is a test tool, not part of yash
//   make trie.o
//   (cd .. && make strbuf.o)
//   c99 -o trietest trietest.c trie.o ../strbuf.o ../util.o
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "trie.h"

void print(const trie_T *t, const char *key)
{
    trieget_T tg = trie_get(t, key, strlen(key));

    printf("%-10s: ", key);
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

void print_null(const trie_T *t)
{
    trieget_T tg = trie_get(t, "", 1);

    printf("<null>    : ");
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

    print_null(t);
    print(t, "");

#define make_trievalue(s) ((trievalue_T) { .keyseq = (s) })

    t = trie_set_null(t, make_trievalue(L"Null"));
    t = trie_set(t, "abc", make_trievalue(L"ABC"));
    t = trie_set(t, "abca", make_trievalue(L"ABCA"));
    t = trie_set(t, "abcb", make_trievalue(L"ABCB"));
    t = trie_set(t, "abcc", make_trievalue(L"ABCC"));
    t = trie_set(t, "abcd", make_trievalue(L""));
    t = trie_set(t, "abcd", make_trievalue(L"ABCD"));
    t = trie_set(t, "abcde", make_trievalue(L"ABCDE"));
    t = trie_set(t, "abce", make_trievalue(L"ABCE"));
    t = trie_set(t, "abcf", make_trievalue(L"ABCF"));
    t = trie_set(t, "abcg", make_trievalue(L"ABCG"));
    t = trie_set(t, "abch", make_trievalue(L"ABCH"));
    t = trie_set(t, "b", make_trievalue(L"B"));

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
    t = trie_set(t, "a", make_trievalue(L"A"));

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

/* vim: set ts=8 sts=4 sw=4 et tw=80: */
