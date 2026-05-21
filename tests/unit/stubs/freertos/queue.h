#ifndef STUB_FREERTOS_QUEUE_H
#define STUB_FREERTOS_QUEUE_H

#include <stddef.h>
#include "freertos/FreeRTOS.h"

typedef void *QueueHandle_t;

static inline QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize)
{
    (void)uxQueueLength;
    (void)uxItemSize;
    return (QueueHandle_t)1;
}

static inline BaseType_t xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue, TickType_t xTicksToWait)
{
    (void)xQueue;
    (void)pvItemToQueue;
    (void)xTicksToWait;
    return pdTRUE;
}

static inline BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer, TickType_t xTicksToWait)
{
    (void)xQueue;
    (void)pvBuffer;
    (void)xTicksToWait;
    return pdFALSE;
}

static inline void vQueueDelete(QueueHandle_t xQueue)
{
    (void)xQueue;
}

#endif
