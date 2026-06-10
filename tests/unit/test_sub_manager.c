#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#define ASSERT(cond, msg) do { if (!(cond)) { printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); return 1; } } while(0)

static bool hex_prefix_match(const char *prefix, size_t prefix_len,
                              const char *full, size_t full_len)
{
    if (prefix_len == 0) return true;
    if (prefix_len > full_len) return false;
    return memcmp(prefix, full, prefix_len) == 0;
}

typedef struct {
    char *authors[4];
    size_t authors_count;
    int32_t kinds[4];
    size_t kinds_count;
    int64_t since;
    int64_t until;
} test_filter_t;

static bool filter_matches_event(const test_filter_t *f, int event_kind,
                                  const char *pubkey_hex, uint64_t created_at)
{
    if (f->kinds_count > 0) {
        bool found = false;
        for (size_t i = 0; i < f->kinds_count; i++) {
            if (f->kinds[i] == event_kind) { found = true; break; }
        }
        if (!found) return false;
    }
    if (f->authors_count > 0) {
        bool found = false;
        for (size_t i = 0; i < f->authors_count; i++) {
            if (hex_prefix_match(f->authors[i], strlen(f->authors[i]),
                                 pubkey_hex, strlen(pubkey_hex))) {
                found = true; break;
            }
        }
        if (!found) return false;
    }
    if (f->since > 0 && (int64_t)created_at < f->since) return false;
    if (f->until > 0 && (int64_t)created_at > f->until) return false;
    return true;
}

int main(void)
{
    int passed = 0;

    printf("--- hex_prefix_match empty prefix ---\n");
    {
        ASSERT(hex_prefix_match("", 0, "abcdef", 6) == true, "empty prefix matches");
        passed++;
    }

    printf("--- hex_prefix_match exact match ---\n");
    {
        ASSERT(hex_prefix_match("abc", 3, "abcdef", 6) == true, "prefix match");
        passed++;
    }

    printf("--- hex_prefix_match no match ---\n");
    {
        ASSERT(hex_prefix_match("xyz", 3, "abcdef", 6) == false, "no match");
        passed++;
    }

    printf("--- hex_prefix_match prefix longer than full ---\n");
    {
        ASSERT(hex_prefix_match("abcdefgh", 8, "abc", 3) == false, "prefix too long");
        passed++;
    }

    printf("--- hex_prefix_match exact full ---\n");
    {
        ASSERT(hex_prefix_match("abcdef", 6, "abcdef", 6) == true, "exact match");
        passed++;
    }

    printf("--- filter_matches_event empty filter ---\n");
    {
        test_filter_t f = {0};
        ASSERT(filter_matches_event(&f, 1, "abc", 100) == true, "empty filter matches all");
        passed++;
    }

    printf("--- filter_matches_event kind match ---\n");
    {
        test_filter_t f = {0};
        f.kinds[0] = 1;
        f.kinds_count = 1;
        ASSERT(filter_matches_event(&f, 1, "abc", 100) == true, "kind 1 matches");
        ASSERT(filter_matches_event(&f, 2, "abc", 100) == false, "kind 2 no match");
        passed += 2;
    }

    printf("--- filter_matches_event multiple kinds ---\n");
    {
        test_filter_t f = {0};
        f.kinds[0] = 1;
        f.kinds[1] = 5;
        f.kinds_count = 2;
        ASSERT(filter_matches_event(&f, 5, "abc", 100) == true, "kind 5 in list");
        ASSERT(filter_matches_event(&f, 3, "abc", 100) == false, "kind 3 not in list");
        passed += 2;
    }

    printf("--- filter_matches_event author match ---\n");
    {
        test_filter_t f = {0};
        f.authors[0] = "abcd";
        f.authors_count = 1;
        ASSERT(filter_matches_event(&f, 1, "abcdef", 100) == true, "author prefix match");
        ASSERT(filter_matches_event(&f, 1, "xyz123", 100) == false, "author no match");
        passed += 2;
    }

    printf("--- filter_matches_event since ---\n");
    {
        test_filter_t f = {0};
        f.since = 50;
        ASSERT(filter_matches_event(&f, 1, "abc", 100) == true, "created_at >= since");
        ASSERT(filter_matches_event(&f, 1, "abc", 49) == false, "created_at < since");
        ASSERT(filter_matches_event(&f, 1, "abc", 50) == true, "created_at == since");
        passed += 3;
    }

    printf("--- filter_matches_event until ---\n");
    {
        test_filter_t f = {0};
        f.until = 100;
        ASSERT(filter_matches_event(&f, 1, "abc", 99) == true, "created_at <= until");
        ASSERT(filter_matches_event(&f, 1, "abc", 101) == false, "created_at > until");
        ASSERT(filter_matches_event(&f, 1, "abc", 100) == true, "created_at == until");
        passed += 3;
    }

    printf("--- filter_matches_event combined ---\n");
    {
        test_filter_t f = {0};
        f.kinds[0] = 1;
        f.kinds_count = 1;
        f.since = 50;
        f.until = 150;
        f.authors[0] = "abc";
        f.authors_count = 1;
        ASSERT(filter_matches_event(&f, 1, "abcdef", 100) == true, "all match");
        ASSERT(filter_matches_event(&f, 2, "abcdef", 100) == false, "wrong kind");
        ASSERT(filter_matches_event(&f, 1, "xyz", 100) == false, "wrong author");
        ASSERT(filter_matches_event(&f, 1, "abcdef", 40) == false, "before since");
        ASSERT(filter_matches_event(&f, 1, "abcdef", 160) == false, "after until");
        passed += 5;
    }

    printf("\n=== Results: %d passed, 0 failed ===\n", passed);
    return 0;
}
