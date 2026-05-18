#ifndef STUBS_DRIVER_GPIO_H
#define STUBS_DRIVER_GPIO_H

#include <stdint.h>

typedef enum {
    GPIO_MODE_DISABLE = 0,
    GPIO_MODE_INPUT,
    GPIO_MODE_OUTPUT,
    GPIO_MODE_OUTPUT_OD,
    GPIO_MODE_INPUT_OUTPUT_OD,
    GPIO_MODE_INPUT_OUTPUT,
} gpio_mode_t;

typedef enum {
    GPIO_INTR_DISABLE = 0,
} gpio_int_type_t;

typedef enum {
    GPIO_PULLUP_DISABLE = 0,
    GPIO_PULLUP_ENABLE,
} gpio_pullup_t;

typedef enum {
    GPIO_PULLDOWN_DISABLE = 0,
    GPIO_PULLDOWN_ENABLE,
} gpio_pulldown_t;

typedef struct {
    uint64_t pin_bit_mask;
    gpio_mode_t mode;
    gpio_pullup_t pull_up_en;
    gpio_pulldown_t pull_down_en;
    gpio_int_type_t intr_type;
} gpio_config_t;

static inline int gpio_config(const gpio_config_t *cfg) { (void)cfg; return 0; }
static inline int gpio_set_level(uint32_t gpio_num, uint32_t level) { (void)gpio_num; (void)level; return 0; }

#define GPIO_INTR_DISABLE 0
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0

#endif
