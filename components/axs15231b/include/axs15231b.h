#ifndef AXS15231B_H
#define AXS15231B_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define AXS15231B_WIDTH  480
#define AXS15231B_HEIGHT 320

#define AXS15231B_PIN_CS   45
#define AXS15231B_PIN_CLK  47
#define AXS15231B_PIN_D0   21
#define AXS15231B_PIN_D1   48
#define AXS15231B_PIN_D2   40
#define AXS15231B_PIN_D3   39
#define AXS15231B_PIN_BL   1

esp_err_t axs15231b_init(void);
void axs15231b_set_backlight(bool on);
void axs15231b_fill_screen(uint16_t color);
void axs15231b_fill_rect(int x, int y, int w, int h, uint16_t color);
void axs15231b_flush(void);
int axs15231b_get_width(void);
int axs15231b_get_height(void);

#endif
