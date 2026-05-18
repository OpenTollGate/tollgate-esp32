#include "axs15231b.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <esp_heap_caps.h>

static const char *TAG = "axs15231b";

#define SWRESET  0x01
#define SLPIN    0x10
#define SLPOUT   0x11
#define INVOFF   0x20
#define INVON    0x21
#define DISPOFF  0x28
#define DISPON   0x29
#define CASET    0x2A
#define RASET    0x2B
#define RAMWR    0x2C
#define COLMOD   0x3A
#define MADCTL   0x36

#define MADCTL_MY 0x80
#define MADCTL_MX 0x40
#define MADCTL_MV 0x20
#define MADCTL_RGB 0x00

#define QSPI_CMD_REG_WRITE  0x02
#define QSPI_CMD_DATA_WRITE 0x32
#define QSPI_DATA_ADDR      0x003C00

static spi_device_handle_t s_spi = NULL;
static uint16_t *s_fb = NULL;
static int s_width = AXS15231B_WIDTH;
static int s_height = AXS15231B_HEIGHT;
static uint8_t *s_swap_buf = NULL;
#define SWAP_BUF_PIXELS 2048

typedef struct {
    uint8_t cmd;
    uint8_t data_len;
    const uint8_t *data;
    uint16_t delay_ms;
} init_cmd_t;

static inline void cs_low(void) {
    gpio_set_level(AXS15231B_PIN_CS, 0);
}

static inline void cs_high(void) {
    gpio_set_level(AXS15231B_PIN_CS, 1);
}

static void cs_init(void) {
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << AXS15231B_PIN_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(AXS15231B_PIN_CS, 1);
}

static void qspi_write_command(uint8_t lcd_cmd) {
    spi_transaction_ext_t t = {0};
    t.base.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    t.base.cmd = QSPI_CMD_REG_WRITE;
    t.base.addr = ((uint32_t)lcd_cmd) << 8;
    t.base.tx_buffer = NULL;
    t.base.length = 0;
    cs_low();
    spi_device_polling_transmit(s_spi, (spi_transaction_t *)&t);
    cs_high();
}

static void qspi_write_cmd_data8(uint8_t lcd_cmd, uint8_t d) {
    spi_transaction_ext_t t = {0};
    t.base.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    t.base.cmd = QSPI_CMD_REG_WRITE;
    t.base.addr = ((uint32_t)lcd_cmd) << 8;
    t.base.tx_data[0] = d;
    t.base.length = 8;
    cs_low();
    spi_device_polling_transmit(s_spi, (spi_transaction_t *)&t);
    cs_high();
}

static void qspi_write_cmd_data16(uint8_t lcd_cmd, uint16_t d) {
    spi_transaction_ext_t t = {0};
    t.base.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    t.base.cmd = QSPI_CMD_REG_WRITE;
    t.base.addr = ((uint32_t)lcd_cmd) << 8;
    t.base.tx_data[0] = d >> 8;
    t.base.tx_data[1] = d & 0xFF;
    t.base.length = 16;
    cs_low();
    spi_device_polling_transmit(s_spi, (spi_transaction_t *)&t);
    cs_high();
}

static void qspi_write_cmd_bytes(uint8_t lcd_cmd, const uint8_t *data, int len) {
    if (len == 0) {
        qspi_write_command(lcd_cmd);
        return;
    }
    spi_transaction_ext_t t = {0};
    t.base.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    t.base.cmd = QSPI_CMD_REG_WRITE;
    t.base.addr = ((uint32_t)lcd_cmd) << 8;
    t.base.tx_buffer = data;
    t.base.length = len * 8;
    cs_low();
    spi_device_polling_transmit(s_spi, (spi_transaction_t *)&t);
    cs_high();
}

static void qspi_write_cmd_d16d16(uint8_t lcd_cmd, uint16_t d1, uint16_t d2) {
    spi_transaction_ext_t t = {0};
    t.base.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    t.base.cmd = QSPI_CMD_REG_WRITE;
    t.base.addr = ((uint32_t)lcd_cmd) << 8;
    t.base.tx_data[0] = d1 >> 8;
    t.base.tx_data[1] = d1 & 0xFF;
    t.base.tx_data[2] = d2 >> 8;
    t.base.tx_data[3] = d2 & 0xFF;
    t.base.length = 32;
    cs_low();
    spi_device_polling_transmit(s_spi, (spi_transaction_t *)&t);
    cs_high();
}

static const uint8_t init_bb[] = {0x00,0x00,0x00,0x00,0x00,0x00,0x5A,0xA5};
static const uint8_t init_a0[] = {0xC0,0x10,0x00,0x02,0x00,0x00,0x04,0x3F,0x20,0x05,0x3F,0x3F,0x00,0x00,0x00,0x00,0x00};
static const uint8_t init_a2[] = {0x30,0x3C,0x24,0x14,0xD0,0x20,0xFF,0xE0,0x40,0x19,0x80,0x80,0x80,0x20,0xF9,0x10,0x02,0xFF,0xFF,0xF0,0x90,0x01,0x32,0xA0,0x91,0xE0,0x20,0x7F,0xFF,0x00,0x5A};
static const uint8_t init_d0[] = {0xE0,0x40,0x51,0x24,0x08,0x05,0x10,0x01,0x20,0x15,0xC2,0x42,0x22,0x22,0xAA,0x03,0x10,0x12,0x60,0x14,0x1E,0x51,0x15,0x00,0x8A,0x20,0x00,0x03,0x3A,0x12};
static const uint8_t init_a3[] = {0xA0,0x06,0xAA,0x00,0x08,0x02,0x0A,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x00,0x55,0x55};
static const uint8_t init_c1[] = {0x31,0x04,0x02,0x02,0x71,0x05,0x24,0x55,0x02,0x00,0x41,0x00,0x53,0xFF,0xFF,0xFF,0x4F,0x52,0x00,0x4F,0x52,0x00,0x45,0x3B,0x0B,0x02,0x0D,0x00,0xFF,0x40};
static const uint8_t init_c3[] = {0x00,0x00,0x00,0x50,0x03,0x00,0x00,0x00,0x01,0x80,0x01};
static const uint8_t init_c4[] = {0x00,0x24,0x33,0x80,0x00,0xEA,0x64,0x32,0xC8,0x64,0xC8,0x32,0x90,0x90,0x11,0x06,0xDC,0xFA,0x00,0x00,0x80,0xFE,0x10,0x10,0x00,0x0A,0x0A,0x44,0x50};
static const uint8_t init_c5[] = {0x18,0x00,0x00,0x03,0xFE,0x3A,0x4A,0x20,0x30,0x10,0x88,0xDE,0x0D,0x08,0x0F,0x0F,0x01,0x3A,0x4A,0x20,0x10,0x10,0x00};
static const uint8_t init_c6[] = {0x05,0x0A,0x05,0x0A,0x00,0xE0,0x2E,0x0B,0x12,0x22,0x12,0x22,0x01,0x03,0x00,0x3F,0x6A,0x18,0xC8,0x22};
static const uint8_t init_c7[] = {0x50,0x32,0x28,0x00,0xA2,0x80,0x8F,0x00,0x80,0xFF,0x07,0x11,0x9C,0x67,0xFF,0x24,0x0C,0x0D,0x0E,0x0F};
static const uint8_t init_c9[] = {0x33,0x44,0x44,0x01};
static const uint8_t init_cf[] = {0x2C,0x1E,0x88,0x58,0x13,0x18,0x56,0x18,0x1E,0x68,0x88,0x00,0x65,0x09,0x22,0xC4,0x0C,0x77,0x22,0x44,0xAA,0x55,0x08,0x08,0x12,0xA0,0x08};
static const uint8_t init_d5[] = {0x40,0x8E,0x8D,0x01,0x35,0x04,0x92,0x74,0x04,0x92,0x74,0x04,0x08,0x6A,0x04,0x46,0x03,0x03,0x03,0x03,0x82,0x01,0x03,0x00,0xE0,0x51,0xA1,0x00,0x00,0x00};
static const uint8_t init_d6[] = {0x10,0x32,0x54,0x76,0x98,0xBA,0xDC,0xFE,0x93,0x00,0x01,0x83,0x07,0x07,0x00,0x07,0x07,0x00,0x03,0x03,0x03,0x03,0x03,0x03,0x00,0x84,0x00,0x20,0x01,0x00};
static const uint8_t init_d7[] = {0x03,0x01,0x0B,0x09,0x0F,0x0D,0x1E,0x1F,0x18,0x1D,0x1F,0x19,0x40,0x8E,0x04,0x00,0x20,0xA0,0x1F};
static const uint8_t init_d8[] = {0x02,0x00,0x0A,0x08,0x0E,0x0C,0x1E,0x1F,0x18,0x1D,0x1F,0x19};
static const uint8_t init_d9[] = {0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F};
static const uint8_t init_dd[] = {0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F};
static const uint8_t init_df[] = {0x44,0x73,0x4B,0x69,0x00,0x0A,0x02,0x90};
static const uint8_t init_e0[] = {0x3B,0x28,0x10,0x16,0x0C,0x06,0x11,0x28,0x5C,0x21,0x0D,0x35,0x13,0x2C,0x33,0x28,0x0D};
static const uint8_t init_e1[] = {0x37,0x28,0x10,0x16,0x0B,0x06,0x11,0x28,0x5C,0x21,0x0D,0x35,0x14,0x2C,0x33,0x28,0x0F};
static const uint8_t init_e2[] = {0x3B,0x07,0x12,0x18,0x0E,0x0D,0x17,0x35,0x44,0x32,0x0C,0x14,0x14,0x36,0x3A,0x2F,0x0D};
static const uint8_t init_e3[] = {0x37,0x07,0x12,0x18,0x0E,0x0D,0x17,0x35,0x44,0x32,0x0C,0x14,0x14,0x36,0x32,0x2F,0x0F};
static const uint8_t init_e4[] = {0x3B,0x07,0x12,0x18,0x0E,0x0D,0x17,0x39,0x44,0x2E,0x0C,0x14,0x14,0x36,0x3A,0x2F,0x0D};
static const uint8_t init_e5[] = {0x37,0x07,0x12,0x18,0x0E,0x0D,0x17,0x39,0x44,0x2E,0x0C,0x14,0x14,0x36,0x3A,0x2F,0x0F};
static const uint8_t init_a4_1[] = {0x85,0x85,0x95,0x82,0xAF,0xAA,0xAA,0x80,0x10,0x30,0x40,0x40,0x20,0xFF,0x60,0x30};
static const uint8_t init_a4_2[] = {0x85,0x85,0x95,0x85};
static const uint8_t init_bb2[] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};

static const init_cmd_t s_init_cmds[] = {
    {0xBB, sizeof(init_bb), init_bb, 0},
    {0xA0, sizeof(init_a0), init_a0, 0},
    {0xA2, sizeof(init_a2), init_a2, 0},
    {0xD0, sizeof(init_d0), init_d0, 0},
    {0xA3, sizeof(init_a3), init_a3, 0},
    {0xC1, sizeof(init_c1), init_c1, 0},
    {0xC3, sizeof(init_c3), init_c3, 0},
    {0xC4, sizeof(init_c4), init_c4, 0},
    {0xC5, sizeof(init_c5), init_c5, 0},
    {0xC6, sizeof(init_c6), init_c6, 0},
    {0xC7, sizeof(init_c7), init_c7, 0},
    {0xC9, sizeof(init_c9), init_c9, 0},
    {0xCF, sizeof(init_cf), init_cf, 0},
    {0xD5, sizeof(init_d5), init_d5, 0},
    {0xD6, sizeof(init_d6), init_d6, 0},
    {0xD7, sizeof(init_d7), init_d7, 0},
    {0xD8, sizeof(init_d8), init_d8, 0},
    {0xD9, sizeof(init_d9), init_d9, 0},
    {0xDD, sizeof(init_dd), init_dd, 0},
    {0xDF, sizeof(init_df), init_df, 0},
    {0xE0, sizeof(init_e0), init_e0, 0},
    {0xE1, sizeof(init_e1), init_e1, 0},
    {0xE2, sizeof(init_e2), init_e2, 0},
    {0xE3, sizeof(init_e3), init_e3, 0},
    {0xE4, sizeof(init_e4), init_e4, 0},
    {0xE5, sizeof(init_e5), init_e5, 0},
    {0xA4, sizeof(init_a4_1), init_a4_1, 0},
    {0xA4, sizeof(init_a4_2), init_a4_2, 0},
    {0xBB, sizeof(init_bb2), init_bb2, 0},
    {SLPOUT, 0, NULL, 200},
    {DISPON, 0, NULL, 100},
};
#define INIT_CMD_COUNT (sizeof(s_init_cmds) / sizeof(s_init_cmds[0]))

esp_err_t axs15231b_init(void) {
    ESP_LOGI(TAG, "Initializing AXS15231B display...");

    esp_err_t ret;

    spi_bus_config_t buscfg = {
        .data0_io_num = AXS15231B_PIN_D0,
        .data1_io_num = AXS15231B_PIN_D1,
        .sclk_io_num = AXS15231B_PIN_CLK,
        .data2_io_num = AXS15231B_PIN_D2,
        .data3_io_num = AXS15231B_PIN_D3,
        .max_transfer_sz = 32768,
    };

    spi_device_interface_config_t devcfg = {
        .command_bits = 8,
        .address_bits = 24,
        .dummy_bits = 0,
        .clock_speed_hz = 40 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 7,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };

    ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
        return ret;
    }

    spi_device_acquire_bus(s_spi, portMAX_DELAY);

    cs_init();

    size_t fb_size = (size_t)s_width * s_height * 2;
    s_fb = heap_caps_malloc(fb_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_fb) {
        ESP_LOGE(TAG, "Failed to allocate framebuffer (%zu bytes)", fb_size);
        return ESP_ERR_NO_MEM;
    }
    memset(s_fb, 0, fb_size);
    ESP_LOGI(TAG, "Framebuffer allocated: %zu bytes in PSRAM", fb_size);

    s_swap_buf = heap_caps_aligned_alloc(16, SWAP_BUF_PIXELS * 2, MALLOC_CAP_DMA);
    if (!s_swap_buf) {
        ESP_LOGE(TAG, "Failed to allocate DMA swap buffer (%d bytes)", SWAP_BUF_PIXELS * 2);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "DMA swap buffer: %d bytes in internal RAM", SWAP_BUF_PIXELS * 2);

    gpio_config_t bl_cfg = {
        .pin_bit_mask = (1ULL << AXS15231B_PIN_BL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&bl_cfg);

    qspi_write_command(SWRESET);
    vTaskDelay(pdMS_TO_TICKS(200));

    for (int i = 0; i < INIT_CMD_COUNT; i++) {
        qspi_write_cmd_bytes(s_init_cmds[i].cmd, s_init_cmds[i].data, s_init_cmds[i].data_len);
        if (s_init_cmds[i].delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(s_init_cmds[i].delay_ms));
        }
    }

    uint8_t madctl_val = MADCTL_RGB;
    qspi_write_cmd_data8(MADCTL, madctl_val);

    uint8_t colmod_val = 0x55;
    qspi_write_cmd_data8(COLMOD, colmod_val);

    axs15231b_fill_screen(0x0000);
    axs15231b_flush();

    axs15231b_set_backlight(true);

    ESP_LOGI(TAG, "AXS15231B initialized: %dx%d portrait", s_width, s_height);
    return ESP_OK;
}

void axs15231b_set_backlight(bool on) {
    gpio_set_level(AXS15231B_PIN_BL, on ? 1 : 0);
}

void axs15231b_fill_screen(uint16_t color) {
    uint32_t pixels = (uint32_t)s_width * s_height;
    for (uint32_t i = 0; i < pixels; i++) {
        s_fb[i] = color;
    }
}

void axs15231b_fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (x < 0 || y < 0 || x + w > s_width || y + h > s_height) return;
    for (int row = y; row < y + h; row++) {
        for (int col = x; col < x + w; col++) {
            s_fb[row * s_width + col] = color;
        }
    }
}

void axs15231b_flush(void) {
    if (!s_spi || !s_fb || !s_swap_buf) return;

    qspi_write_cmd_d16d16(CASET, 0, s_width - 1);
    qspi_write_cmd_d16d16(RASET, 0, s_height - 1);

    int total_pixels = s_width * s_height;
    int pixel_offset = 0;
    bool first = true;

    cs_low();
    while (pixel_offset < total_pixels) {
        int remaining = total_pixels - pixel_offset;
        int chunk_pixels = remaining < SWAP_BUF_PIXELS ? remaining : SWAP_BUF_PIXELS;
        int chunk_bytes = chunk_pixels * 2;

        uint8_t *src = (uint8_t *)(s_fb + pixel_offset);
        for (int i = 0; i < chunk_bytes; i += 2) {
            s_swap_buf[i] = src[i + 1];
            s_swap_buf[i + 1] = src[i];
        }

        spi_transaction_ext_t t = {0};
        if (first) {
            t.base.flags = SPI_TRANS_MODE_QIO;
            t.base.cmd = QSPI_CMD_DATA_WRITE;
            t.base.addr = QSPI_DATA_ADDR;
            first = false;
        } else {
            t.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD |
                           SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
        }
        t.base.tx_buffer = s_swap_buf;
        t.base.length = chunk_pixels * 16;
        spi_device_polling_transmit(s_spi, (spi_transaction_t *)&t);
        pixel_offset += chunk_pixels;
    }
    cs_high();
}

int axs15231b_get_width(void) { return s_width; }
int axs15231b_get_height(void) { return s_height; }
