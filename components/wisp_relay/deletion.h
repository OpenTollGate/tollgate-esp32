#ifndef DELETION_H
#define DELETION_H

#include "storage_engine.h"

#define NOSTR_KIND_DELETION 5

int deletion_process_json(storage_engine_t *storage, const char *event_json,
                          size_t event_len);

#endif
