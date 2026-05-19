#include "touch.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "touch";

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;
static bool s_initialized = false;
static int s_rotation = 0;

static const uint8_t s_read_cmd[11] = {
    0xb5, 0xab, 0xa5, 0x5a, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00
};

void touch_parse_raw(const uint8_t *data, touch_point_t *pt) {
    memset(pt, 0, sizeof(*pt));

    if (!data || data[0] != 0 || data[1] == 0 || data[1] > 1) {
        pt->touched = false;
        return;
    }

    uint16_t raw_x = ((data[2] & 0x0F) << 8) | data[3];
    uint16_t raw_y = ((data[4] & 0x0F) << 8) | data[5];

    if (raw_x > TOUCH_MAX_X) raw_x = TOUCH_MAX_X;
    if (raw_y > TOUCH_MAX_Y) raw_y = TOUCH_MAX_Y;

    pt->x = raw_x;
    pt->y = raw_y;
    pt->touched = true;
}

esp_err_t touch_init(void) {
    if (s_initialized) return ESP_OK;

    gpio_config_t rst_conf = {
        .pin_bit_mask = (1ULL << TOUCH_RST_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&rst_conf);

    gpio_set_level(TOUCH_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(TOUCH_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(200));

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = TOUCH_SDA_PIN,
        .scl_io_num = TOUCH_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = 1,
            .allow_pd = 0,
        },
    };

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TOUCH_I2C_ADDR,
        .scl_speed_hz = 400000,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = 0,
        },
    };

    ret = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Touch initialized (I2C addr 0x%02X)", TOUCH_I2C_ADDR);
    return ESP_OK;
}

bool touch_read(touch_point_t *pt) {
    if (!s_initialized || !s_dev || !pt) {
        if (pt) pt->touched = false;
        return false;
    }

    esp_err_t ret = i2c_master_transmit(s_dev, s_read_cmd, sizeof(s_read_cmd), 100);
    if (ret != ESP_OK) {
        pt->touched = false;
        return false;
    }

    uint8_t data[8] = {0};
    ret = i2c_master_receive(s_dev, data, sizeof(data), 100);
    if (ret != ESP_OK) {
        pt->touched = false;
        return false;
    }

    touch_parse_raw(data, pt);

    if (pt->touched && s_rotation != 0) {
        uint16_t raw_x = pt->x;
        uint16_t raw_y = pt->y;
        switch (s_rotation) {
            case 1:
                pt->x = raw_y;
                pt->y = TOUCH_MAX_X - raw_x;
                break;
            case 2:
                pt->x = TOUCH_MAX_X - raw_x;
                pt->y = TOUCH_MAX_Y - raw_y;
                break;
            case 3:
                pt->x = TOUCH_MAX_Y - raw_y;
                pt->y = raw_x;
                break;
        }
    }

    return pt->touched;
}

void touch_set_rotation(int rotation) {
    s_rotation = rotation;
}

void touch_deinit(void) {
    if (s_dev) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
    if (s_bus) {
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
    }
    s_initialized = false;
}
