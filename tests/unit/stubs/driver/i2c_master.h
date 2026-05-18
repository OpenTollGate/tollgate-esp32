#ifndef STUBS_DRIVER_I2C_MASTER_H
#define STUBS_DRIVER_I2C_MASTER_H

#include "driver/i2c_types.h"
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

static inline esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *cfg, i2c_master_bus_handle_t *ret) {
    (void)cfg; (void)ret;
    return ESP_OK;
}

static inline esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *cfg, i2c_master_dev_handle_t *ret) {
    (void)bus; (void)cfg; (void)ret;
    return ESP_OK;
}

static inline esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t *buf, size_t len, int timeout_ms) {
    (void)dev; (void)buf; (void)len; (void)timeout_ms;
    return ESP_OK;
}

static inline esp_err_t i2c_master_receive(i2c_master_dev_handle_t dev, uint8_t *buf, size_t len, int timeout_ms) {
    (void)dev; (void)buf; (void)len; (void)timeout_ms;
    return ESP_OK;
}

static inline esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t dev) {
    (void)dev;
    return ESP_OK;
}

static inline esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus) {
    (void)bus;
    return ESP_OK;
}

#endif
