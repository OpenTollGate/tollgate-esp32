#include "test_framework.h"
#include <string.h>
#include <stdio.h>

static int escape_wifi_field(const char *src, char *dst, int dst_size) {
    int si = 0, di = 0;
    while (src[si] && di < dst_size - 2) {
        char c = src[si];
        if (c == '\\' || c == ';' || c == ':' || c == ',' || c == '"') {
            if (di + 2 >= dst_size) break;
            dst[di++] = '\\';
            dst[di++] = c;
        } else {
            dst[di++] = c;
        }
        si++;
    }
    dst[di] = '\0';
    return di;
}

static int test_escape_no_special(void) {
    char dst[64];
    int len = escape_wifi_field("HelloWorld", dst, sizeof(dst));
    ASSERT(strcmp(dst, "HelloWorld") == 0, "no special chars unchanged");
    ASSERT(len == 10, "no special chars length correct");
    return 0;
}

static int test_escape_semicolon(void) {
    char dst[64];
    int len = escape_wifi_field("Hello;World", dst, sizeof(dst));
    ASSERT(strcmp(dst, "Hello\\;World") == 0, "semicolon escaped");
    ASSERT(len == 12, "semicolon escaped length correct");
    return 0;
}

static int test_escape_colon(void) {
    char dst[64];
    int len = escape_wifi_field("Hello:World", dst, sizeof(dst));
    ASSERT(strcmp(dst, "Hello\\:World") == 0, "colon escaped");
    ASSERT(len == 12, "colon escaped length correct");
    return 0;
}

static int test_escape_backslash(void) {
    char dst[64];
    int len = escape_wifi_field("Hello\\World", dst, sizeof(dst));
    ASSERT(strcmp(dst, "Hello\\\\World") == 0, "backslash escaped");
    ASSERT(len == 12, "backslash escaped length correct");
    return 0;
}

static int test_escape_comma(void) {
    char dst[64];
    int len = escape_wifi_field("Hello,World", dst, sizeof(dst));
    ASSERT(strcmp(dst, "Hello\\,World") == 0, "comma escaped");
    ASSERT(len == 12, "comma escaped length correct");
    return 0;
}

static int test_escape_quote(void) {
    char dst[64];
    int len = escape_wifi_field("Hello\"World", dst, sizeof(dst));
    ASSERT(strcmp(dst, "Hello\\\"World") == 0, "quote escaped");
    ASSERT(len == 12, "quote escaped length correct");
    return 0;
}

static int test_escape_multiple(void) {
    char dst[64];
    int len = escape_wifi_field("a;b:c\\d", dst, sizeof(dst));
    ASSERT(strcmp(dst, "a\\;b\\:c\\\\d") == 0, "multiple special chars escaped");
    ASSERT(len == 10, "multiple special chars length correct");
    return 0;
}

static int test_escape_empty(void) {
    char dst[64];
    int len = escape_wifi_field("", dst, sizeof(dst));
    ASSERT(strcmp(dst, "") == 0, "empty string stays empty");
    ASSERT(len == 0, "empty string length is 0");
    return 0;
}

static int test_escape_overflow(void) {
    char dst[5];
    int len = escape_wifi_field("Hello;World", dst, sizeof(dst));
    ASSERT(len < (int)sizeof(dst), "output truncated on overflow");
    ASSERT(dst[len] == '\0', "still null-terminated after truncation");
    return 0;
}

static int test_escape_ssid_like(void) {
    char dst[64];
    int len = escape_wifi_field("TollGate-C0E9CA", dst, sizeof(dst));
    ASSERT(strcmp(dst, "TollGate-C0E9CA") == 0, "TollGate SSID no escaping needed");
    ASSERT(len == 15, "TollGate SSID length correct");
    return 0;
}

static int test_escape_all_special_in_one(void) {
    char dst[64];
    int len = escape_wifi_field("\\;:,\"", dst, sizeof(dst));
    ASSERT(strcmp(dst, "\\\\\\;\\:\\,\\\"") == 0, "all special chars in sequence");
    ASSERT(len == 10, "all special chars length correct");
    return 0;
}

int main(void) {
    int failed = 0;
    failed += test_escape_no_special();
    failed += test_escape_semicolon();
    failed += test_escape_colon();
    failed += test_escape_backslash();
    failed += test_escape_comma();
    failed += test_escape_quote();
    failed += test_escape_multiple();
    failed += test_escape_empty();
    failed += test_escape_overflow();
    failed += test_escape_ssid_like();
    failed += test_escape_all_special_in_one();

    if (failed == 0) {
        printf("\n=== ALL DISPLAY TESTS PASSED ===\n");
    }
    return failed;
}
