#ifndef STUBS_ESP_ERR_H
#define STUBS_ESP_ERR_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef int esp_err_t;

#define ESP_OK          0
#define ESP_FAIL        -1
#define ESP_ERR_INVALID_ARG  0x102
#define ESP_ERR_NO_MEM       0x101
#define ESP_ERR_NOT_FOUND    0x104

#define ESP_ERROR_CHECK(x) do { if ((x) != 0) { fprintf(stderr, "ESP_ERROR_CHECK failed: 0x%x\n", (int)(x)); abort(); } } while(0)

#endif
