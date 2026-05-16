#include "test_framework.h"
#include "../../main/geohash.h"
#include <string.h>

int main(void)
{
    char buf[16];

    printf("=== test_geohash ===\n");

    geohash_encode(48.1351, 11.5820, 9, buf);
    ASSERT_EQ_STR("u281zd9z2", buf, "Munich (48.1351, 11.5820) precision 9");

    geohash_encode(40.7128, -74.0060, 6, buf);
    ASSERT(buf[0] == 'd', "NYC starts with 'd'");
    ASSERT(buf[1] == 'r', "NYC second char 'r'");
    ASSERT_EQ_INT(6, (int)strlen(buf), "NYC precision 6 has length 6");

    geohash_encode(0.0, 0.0, 8, buf);
    ASSERT_EQ_STR("s0000000", buf, "Origin (0,0) precision 8");

    geohash_encode(90.0, 180.0, 5, buf);
    ASSERT_EQ_INT(5, (int)strlen(buf), "North pole max lon precision 5");

    geohash_encode(-90.0, -180.0, 5, buf);
    ASSERT_EQ_INT(5, (int)strlen(buf), "South pole min lon precision 5");

    geohash_encode(48.1351, 11.5820, 1, buf);
    ASSERT_EQ_INT(1, (int)strlen(buf), "Precision 1 produces 1 char");
    ASSERT(buf[0] == 'u', "Munich precision 1 = 'u'");

    geohash_encode(48.1351, 11.5820, 4, buf);
    ASSERT_EQ_STR("u281", buf, "Munich precision 4");

    char buf2[16];
    geohash_encode(48.1351, 11.5820, 9, buf2);
    ASSERT_EQ_STR("u281zd9z2", buf2, "Munich determinism check");

    TEST_SUMMARY();
}
