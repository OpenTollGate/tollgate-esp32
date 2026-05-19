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

static kb_layout_t s_layout = {
    .key_w = 28,
    .key_h = 36,
    .key_gap = 2,
    .start_y = 70,
    .screen_w = 320,
    .row_count = 4,
};

void kb_state_init(kb_state_t *st) {
    if (!st) return;
    memset(st, 0, sizeof(*st));
    st->layer = KB_ALPHA_LOWER;
    st->reveal = false;
}

void kb_set_layout(const kb_layout_t *layout) {
    if (layout) s_layout = *layout;
}

const kb_layout_t *kb_get_layout(void) {
    return &s_layout;
}

static const char **get_layer(kb_layer_t layer) {
    switch (layer) {
        case KB_ALPHA_UPPER: return s_alpha_upper;
        case KB_NUMSYM:      return s_numsym;
        default:             return s_alpha_lower;
    }
}

int kb_get_row_keys(int row, kb_layer_t layer, const char **keys_out) {
    if (row < 0 || row >= s_layout.row_count) {
        *keys_out = NULL;
        return 0;
    }
    const char **layer_rows = get_layer(layer);
    const char *row_str = layer_rows[row];
    *keys_out = row_str;
    return (int)strlen(row_str);
}

static int row_x_offset(int row, int total_keys) {
    int kw = s_layout.key_w;
    int gap = s_layout.key_gap;
    int total_w = total_keys * kw + (total_keys - 1) * gap;
    int margin = (s_layout.screen_w - total_w) / 2;
    if (margin < 2) margin = 2;
    switch (row) {
        case 0: return margin;
        case 1: return margin + kw / 2;
        case 2: return margin + kw;
        case 3: return margin;
        default: return margin;
    }
}

static int key_width_at(int row, int col, int total_keys) {
    int kw = s_layout.key_w;
    if (row == 3) {
        int gap = s_layout.key_gap;
        int margin = row_x_offset(3, total_keys);
        int available = s_layout.screen_w - margin * 2;
        int side_w = (available - gap) / 4;
        if (col == 0) return side_w;
        if (col == total_keys - 1) return side_w;
        return available - side_w * 2 - gap * 2;
    }
    return kw;
}

kb_result_t kb_hit_test(int tx, int ty, kb_layer_t layer) {
    kb_result_t result = {KB_ACTION_NONE, 0};
    int sy = s_layout.start_y;
    int kw = s_layout.key_w;
    int kh = s_layout.key_h;
    int gap = s_layout.key_gap;

    if (ty < sy || ty >= sy + s_layout.row_count * (kh + gap)) {
        return result;
    }

    int row = (ty - sy) / (kh + gap);
    if (row < 0 || row >= s_layout.row_count) return result;

    const char *row_str;
    int total_keys = kb_get_row_keys(row, layer, &row_str);
    if (total_keys == 0) return result;

    int x_off = row_x_offset(row, total_keys);
    int cx = x_off;

    for (int col = 0; col < total_keys; col++) {
        int key_w = key_width_at(row, col, total_keys);
        if (tx >= cx && tx < cx + key_w) {
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
        cx += key_w + gap;
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
