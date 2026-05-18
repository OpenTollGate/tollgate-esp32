#ifndef TOUCH_H
#define TOUCH_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define TOUCH_SDA_PIN    4
#define TOUCH_SCL_PIN    8
#define TOUCH_RST_PIN    12
#define TOUCH_INT_PIN    11
#define TOUCH_I2C_ADDR   0x3B
#define TOUCH_MAX_X      319
#define TOUCH_MAX_Y      479

typedef struct {
    uint16_t x;
    uint16_t y;
    bool touched;
} touch_point_t;

esp_err_t touch_init(void);
bool touch_read(touch_point_t *pt);
void touch_deinit(void);
void touch_set_rotation(int rotation);

void touch_parse_raw(const uint8_t *data, touch_point_t *pt);

#endif
