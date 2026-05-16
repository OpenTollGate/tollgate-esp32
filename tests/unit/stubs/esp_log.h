#ifndef STUBS_ESP_LOG_H
#define STUBS_ESP_LOG_H

#include <stdio.h>

#define ESP_LOGI(tag, fmt, ...) do { printf("I %s: " fmt "\n", tag, ##__VA_ARGS__); } while(0)
#define ESP_LOGW(tag, fmt, ...) do { printf("W %s: " fmt "\n", tag, ##__VA_ARGS__); } while(0)
#define ESP_LOGE(tag, fmt, ...) do { fprintf(stderr, "E %s: " fmt "\n", tag, ##__VA_ARGS__); } while(0)

#endif
