#ifndef STUB_TOLLGATE_API_H
#define STUB_TOLLGATE_API_H

#include "freertos/queue.h"

static inline void tls_worker_set_queue(QueueHandle_t q)
{
    (void)q;
}

#endif
