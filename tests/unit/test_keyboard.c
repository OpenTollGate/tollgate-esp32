#include "test_framework.h"
#include "../../main/keyboard.h"
#include <string.h>

int main(void)
{
    printf("=== test_keyboard ===\n");

    const char *keys;
    int count;

    count = kb_get_row_keys(0, KB_ALPHA_LOWER, &keys);
    ASSERT_EQ_INT(10, count, "Row 0 alpha lower has 10 keys");
    ASSERT_EQ_INT('q', keys[0], "Row 0 starts with 'q'");
    ASSERT_EQ_INT('p', keys[9], "Row 0 ends with 'p'");

    count = kb_get_row_keys(1, KB_ALPHA_LOWER, &keys);
    ASSERT_EQ_INT(9, count, "Row 1 alpha lower has 9 keys");
    ASSERT_EQ_INT('a', keys[0], "Row 1 starts with 'a'");

    count = kb_get_row_keys(2, KB_ALPHA_LOWER, &keys);
    ASSERT(count > 0, "Row 2 alpha lower has keys");
    ASSERT_EQ_INT('\001', keys[0], "Row 2 starts with SHIFT control char");

    count = kb_get_row_keys(0, KB_ALPHA_UPPER, &keys);
    ASSERT_EQ_INT(10, count, "Row 0 alpha upper has 10 keys");
    ASSERT_EQ_INT('Q', keys[0], "Row 0 upper starts with 'Q'");

    count = kb_get_row_keys(0, KB_NUMSYM, &keys);
    ASSERT_EQ_INT(10, count, "Row 0 numsym has 10 keys");
    ASSERT_EQ_INT('1', keys[0], "Row 0 numsym starts with '1'");
    ASSERT_EQ_INT('0', keys[9], "Row 0 numsym ends with '0'");

    count = kb_get_row_keys(-1, KB_ALPHA_LOWER, &keys);
    ASSERT_EQ_INT(0, count, "Invalid row -1 returns 0");

    count = kb_get_row_keys(99, KB_ALPHA_LOWER, &keys);
    ASSERT_EQ_INT(0, count, "Invalid row 99 returns 0");

    {
        kb_result_t r = kb_hit_test(160, 100, KB_ALPHA_LOWER);
        ASSERT(r.action == KB_ACTION_NONE, "Touch above keyboard = NONE");

        r = kb_hit_test(160, 500, KB_ALPHA_LOWER);
        ASSERT(r.action == KB_ACTION_NONE, "Touch below keyboard = NONE");
    }

    {
        int mid_x = 5 + KB_KEY_W / 2;
        int mid_y = KB_START_Y + KB_KEY_H / 2;
        kb_result_t r = kb_hit_test(mid_x, mid_y, KB_ALPHA_LOWER);
        ASSERT(r.action == KB_ACTION_CHAR, "Row 0 first key is a char");
        ASSERT_EQ_INT('q', r.ch, "Row 0 first key = 'q'");
    }

    {
        int x = 5 + KB_KEY_W + KB_KEY_GAP + KB_KEY_W / 2;
        int y = KB_START_Y + KB_KEY_H / 2;
        kb_result_t r = kb_hit_test(x, y, KB_ALPHA_LOWER);
        ASSERT(r.action == KB_ACTION_CHAR, "Row 0 second key is a char");
        ASSERT_EQ_INT('w', r.ch, "Row 0 second key = 'w'");
    }

    {
        int y_row1 = KB_START_Y + (KB_KEY_H + KB_KEY_GAP) + KB_KEY_H / 2;
        int x_row1 = 14 + KB_KEY_W / 2;
        kb_result_t r = kb_hit_test(x_row1, y_row1, KB_ALPHA_LOWER);
        ASSERT(r.action == KB_ACTION_CHAR, "Row 1 first key is a char");
        ASSERT_EQ_INT('a', r.ch, "Row 1 first key = 'a'");
    }

    {
        int y_row2 = KB_START_Y + 2 * (KB_KEY_H + KB_KEY_GAP) + KB_KEY_H / 2;
        int x_row2 = 23 + 42 / 2;
        kb_result_t r = kb_hit_test(x_row2, y_row2, KB_ALPHA_LOWER);
        ASSERT(r.action == KB_ACTION_SHIFT, "Row 2 first key = SHIFT");
    }

    {
        kb_state_t st;
        kb_state_init(&st);
        ASSERT_EQ_INT(0, st.cursor, "Initial cursor = 0");
        ASSERT_EQ_INT(KB_ALPHA_LOWER, st.layer, "Initial layer = lower");
        ASSERT_EQ_STR("", st.input, "Initial input is empty");

        kb_apply(&st, (kb_result_t){KB_ACTION_CHAR, 'h'});
        ASSERT_EQ_STR("h", st.input, "After typing 'h': input='h'");
        ASSERT_EQ_INT(1, st.cursor, "After typing 'h': cursor=1");

        kb_apply(&st, (kb_result_t){KB_ACTION_CHAR, 'i'});
        ASSERT_EQ_STR("hi", st.input, "After typing 'i': input='hi'");

        kb_apply(&st, (kb_result_t){KB_ACTION_BACKSPACE, 0});
        ASSERT_EQ_STR("h", st.input, "After backspace: input='h'");
        ASSERT_EQ_INT(1, st.cursor, "After backspace: cursor=1");

        kb_apply(&st, (kb_result_t){KB_ACTION_BACKSPACE, 0});
        ASSERT_EQ_STR("", st.input, "After second backspace: empty");
        ASSERT_EQ_INT(0, st.cursor, "After second backspace: cursor=0");

        kb_apply(&st, (kb_result_t){KB_ACTION_BACKSPACE, 0});
        ASSERT_EQ_INT(0, st.cursor, "Backspace on empty stays at 0");
    }

    {
        kb_state_t st;
        kb_state_init(&st);

        kb_apply(&st, (kb_result_t){KB_ACTION_SHIFT, 0});
        ASSERT_EQ_INT(KB_ALPHA_UPPER, st.layer, "Shift: lower->upper");

        kb_apply(&st, (kb_result_t){KB_ACTION_SHIFT, 0});
        ASSERT_EQ_INT(KB_ALPHA_LOWER, st.layer, "Shift: upper->lower");

        kb_apply(&st, (kb_result_t){KB_ACTION_LAYER, 0});
        ASSERT_EQ_INT(KB_NUMSYM, st.layer, "Layer: lower->numsym");

        kb_apply(&st, (kb_result_t){KB_ACTION_LAYER, 0});
        ASSERT_EQ_INT(KB_ALPHA_LOWER, st.layer, "Layer: numsym->lower");
    }

    {
        kb_state_t st;
        kb_state_init(&st);

        for (int i = 0; i < KB_INPUT_MAX; i++) {
            kb_apply(&st, (kb_result_t){KB_ACTION_CHAR, 'a' + (i % 26)});
        }
        ASSERT_EQ_INT(KB_INPUT_MAX, st.cursor, "Filled to max");
        ASSERT_EQ_INT(KB_INPUT_MAX, (int)strlen(st.input), "String length = max");

        kb_apply(&st, (kb_result_t){KB_ACTION_CHAR, 'Z'});
        ASSERT_EQ_INT(KB_INPUT_MAX, st.cursor, "Overflow blocked");
        ASSERT_EQ_INT(KB_INPUT_MAX, (int)strlen(st.input), "Length unchanged after overflow");
    }

    {
        kb_state_t st;
        kb_state_init(&st);

        kb_apply(&st, (kb_result_t){KB_ACTION_SPACE, ' '});
        ASSERT_EQ_STR(" ", st.input, "Space adds space char");
        ASSERT_EQ_INT(1, st.cursor, "Space increments cursor");
    }

    {
        kb_state_t st;
        kb_state_init(&st);
        kb_result_t none = {KB_ACTION_NONE, 0};
        kb_apply(&st, none);
        ASSERT_EQ_STR("", st.input, "NONE action does nothing");

        kb_apply(NULL, (kb_result_t){KB_ACTION_CHAR, 'x'});
    }

    {
        kb_state_t st;
        kb_state_init(&st);

        kb_apply(&st, (kb_result_t){KB_ACTION_CHAR, 'P'});
        kb_apply(&st, (kb_result_t){KB_ACTION_CHAR, '@'});
        kb_apply(&st, (kb_result_t){KB_ACTION_CHAR, 's'});
        kb_apply(&st, (kb_result_t){KB_ACTION_CHAR, 's'});
        ASSERT_EQ_STR("P@ss", st.input, "Password build: P@ss");
    }

    TEST_SUMMARY();
}
