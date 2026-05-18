#include "keyboard.h"
#include <string.h>

static const char *s_alpha_lower[] = {
    "qwertyuiop",
    "asdfghjkl",
    "\001zxcvbnm\b",
    "\002\003\004"
};

static const char *s_alpha_upper[] = {
    "QWERTYUIOP",
    "ASDFGHJKL",
    "\001ZXCVBNM\b",
    "\002\003\004"
};

static const char *s_numsym[] = {
    "1234567890",
    "-/:;()$&@\"",
    "\001.,?!'\\b",
    "\002\003\004"
};

#define CTRL_SHIFT  '\001'
#define CTRL_LAYER  '\002'
#define CTRL_SPACE  '\003'
#define CTRL_DONE   '\004'
#define CTRL_BS     '\b'

void kb_state_init(kb_state_t *st) {
    if (!st) return;
    memset(st, 0, sizeof(*st));
    st->layer = KB_ALPHA_LOWER;
    st->reveal = false;
}

static const char **get_layer(kb_layer_t layer) {
    switch (layer) {
        case KB_ALPHA_UPPER: return s_alpha_upper;
        case KB_NUMSYM:      return s_numsym;
        default:             return s_alpha_lower;
    }
}

static int key_is_ctrl(char c) {
    return c == CTRL_SHIFT || c == CTRL_LAYER || c == CTRL_SPACE || c == CTRL_DONE || c == CTRL_BS;
}

int kb_get_row_keys(int row, kb_layer_t layer, const char **keys_out) {
    if (row < 0 || row >= KB_ROW_COUNT) {
        *keys_out = NULL;
        return 0;
    }
    const char **layer_rows = get_layer(layer);
    const char *row_str = layer_rows[row];
    *keys_out = row_str;
    return (int)strlen(row_str);
}

static int row_x_offset(int row) {
    switch (row) {
        case 0: return 5;
        case 1: return 14;
        case 2: return 23;
        case 3: return 5;
        default: return 0;
    }
}

static int key_width_at(int row, int col, int total_keys) {
    (void)col;
    if (row == 3) {
        if (col == 0) return 42;
        if (col == total_keys - 1) return 50;
        return 168;
    }
    return KB_KEY_W;
}

kb_result_t kb_hit_test(int tx, int ty, kb_layer_t layer) {
    kb_result_t result = {KB_ACTION_NONE, 0};

    if (ty < KB_START_Y || ty >= KB_START_Y + KB_ROW_COUNT * (KB_KEY_H + KB_KEY_GAP)) {
        return result;
    }

    int row = (ty - KB_START_Y) / (KB_KEY_H + KB_KEY_GAP);
    if (row < 0 || row >= KB_ROW_COUNT) return result;

    const char *row_str;
    int total_keys = kb_get_row_keys(row, layer, &row_str);
    if (total_keys == 0) return result;

    int x_off = row_x_offset(row);
    int cx = x_off;

    for (int col = 0; col < total_keys; col++) {
        int kw = key_width_at(row, col, total_keys);
        if (tx >= cx && tx < cx + kw) {
            char c = row_str[col];
            if (c == CTRL_SHIFT) {
                result.action = KB_ACTION_SHIFT;
            } else if (c == CTRL_LAYER) {
                result.action = KB_ACTION_LAYER;
            } else if (c == CTRL_SPACE) {
                result.action = KB_ACTION_SPACE;
                result.ch = ' ';
            } else if (c == CTRL_DONE) {
                result.action = KB_ACTION_DONE;
            } else if (c == CTRL_BS) {
                result.action = KB_ACTION_BACKSPACE;
            } else {
                result.action = KB_ACTION_CHAR;
                result.ch = c;
            }
            return result;
        }
        cx += kw + KB_KEY_GAP;
    }

    return result;
}

void kb_apply(kb_state_t *st, kb_result_t result) {
    if (!st || result.action == KB_ACTION_NONE) return;

    switch (result.action) {
        case KB_ACTION_CHAR:
            if (st->cursor < KB_INPUT_MAX) {
                st->input[st->cursor++] = result.ch;
                st->input[st->cursor] = '\0';
            }
            break;
        case KB_ACTION_BACKSPACE:
            if (st->cursor > 0) {
                st->cursor--;
                st->input[st->cursor] = '\0';
            }
            break;
        case KB_ACTION_SHIFT:
            if (st->layer == KB_ALPHA_LOWER) st->layer = KB_ALPHA_UPPER;
            else if (st->layer == KB_ALPHA_UPPER) st->layer = KB_ALPHA_LOWER;
            break;
        case KB_ACTION_LAYER:
            if (st->layer == KB_NUMSYM) st->layer = KB_ALPHA_LOWER;
            else st->layer = KB_NUMSYM;
            break;
        case KB_ACTION_SPACE:
            if (st->cursor < KB_INPUT_MAX) {
                st->input[st->cursor++] = ' ';
                st->input[st->cursor] = '\0';
            }
            break;
        case KB_ACTION_DONE:
            break;
        default:
            break;
    }
}
