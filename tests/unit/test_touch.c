#include "test_framework.h"
#include "../../main/touch.h"
#include <string.h>

int main(void)
{
    touch_point_t pt;
    uint8_t data[8];

    printf("=== test_touch ===\n");

    memset(data, 0, sizeof(data));
    touch_parse_raw(data, &pt);
    ASSERT(!pt.touched, "All-zero data = no touch");

    data[0] = 1;
    data[1] = 1;
    data[2] = 0;
    data[3] = 0;
    touch_parse_raw(data, &pt);
    ASSERT(!pt.touched, "data[0]=1 = no touch (gesture byte nonzero)");

    data[0] = 0;
    data[1] = 0;
    touch_parse_raw(data, &pt);
    ASSERT(!pt.touched, "data[1]=0 = no touch (touch count zero)");

    data[0] = 0;
    data[1] = 1;
    data[2] = 0x00;
    data[3] = 0x64;
    data[4] = 0x00;
    data[5] = 0xC8;
    data[6] = 0;
    data[7] = 0;
    touch_parse_raw(data, &pt);
    ASSERT(pt.touched, "Valid touch: touched=true");
    ASSERT_EQ_INT(100, (int)pt.x, "Valid touch: x=100");
    ASSERT_EQ_INT(200, (int)pt.y, "Valid touch: y=200");

    data[0] = 0;
    data[1] = 1;
    data[2] = 0x0F;
    data[3] = 0xFF;
    data[4] = 0x0F;
    data[5] = 0xFF;
    touch_parse_raw(data, &pt);
    ASSERT(pt.touched, "Max raw coords: touched=true");
    ASSERT_EQ_INT(TOUCH_MAX_X, (int)pt.x, "Max raw coords clamped to 319");
    ASSERT_EQ_INT(TOUCH_MAX_Y, (int)pt.y, "Max raw coords clamped to 479");

    data[0] = 0;
    data[1] = 1;
    data[2] = 0x05;
    data[3] = 0x00;
    data[4] = 0x08;
    data[5] = 0x00;
    touch_parse_raw(data, &pt);
    ASSERT(pt.touched, "12-bit coords: touched=true");
    ASSERT_EQ_INT(TOUCH_MAX_X, (int)pt.x, "12-bit x: (0x05 << 8) | 0x00 = 1280, clamped to 319");

    data[0] = 0;
    data[1] = 2;
    touch_parse_raw(data, &pt);
    ASSERT(!pt.touched, "data[1]=2 = too many touches, reject");

    touch_parse_raw(NULL, &pt);
    ASSERT(!pt.touched, "NULL data = no touch");

    data[0] = 0;
    data[1] = 1;
    data[2] = 0x00;
    data[3] = 0x00;
    data[4] = 0x00;
    data[5] = 0x00;
    touch_parse_raw(data, &pt);
    ASSERT(pt.touched, "Origin (0,0): touched=true");
    ASSERT_EQ_INT(0, (int)pt.x, "Origin: x=0");
    ASSERT_EQ_INT(0, (int)pt.y, "Origin: y=0");

    data[0] = 0;
    data[1] = 1;
    data[2] = 0x01;
    data[3] = 0x3F;
    data[4] = 0x01;
    data[5] = 0xDF;
    touch_parse_raw(data, &pt);
    ASSERT(pt.touched, "Mid-screen: touched=true");
    ASSERT_EQ_INT(319, (int)pt.x, "Mid-screen: x=0x13F=319");
    ASSERT_EQ_INT(479, (int)pt.y, "Mid-screen: y=0x1DF=479");

    TEST_SUMMARY();
}
