#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

#define KB_INPUT_MAX     64

typedef enum {
    KB_ALPHA_LOWER,
    KB_ALPHA_UPPER,
    KB_NUMSYM
} kb_layer_t;

typedef enum {
    KB_ACTION_NONE = 0,
    KB_ACTION_CHAR,
    KB_ACTION_SHIFT,
    KB_ACTION_BACKSPACE,
    KB_ACTION_DONE,
    KB_ACTION_LAYER,
    KB_ACTION_SPACE
} kb_action_t;

typedef struct {
    char input[KB_INPUT_MAX + 1];
    int cursor;
    bool reveal;
    kb_layer_t layer;
} kb_state_t;

typedef struct {
    kb_action_t action;
    char ch;
} kb_result_t;

typedef struct {
    int key_w;
    int key_h;
    int key_gap;
    int start_y;
    int screen_w;
    int row_count;
} kb_layout_t;

void kb_state_init(kb_state_t *st);
void kb_set_layout(const kb_layout_t *layout);
const kb_layout_t *kb_get_layout(void);
int kb_get_row_keys(int row, kb_layer_t layer, const char **keys_out);
kb_result_t kb_hit_test(int tx, int ty, kb_layer_t layer);
void kb_apply(kb_state_t *st, kb_result_t result);

#endif
