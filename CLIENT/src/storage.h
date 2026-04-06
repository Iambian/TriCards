#ifndef STORAGE_H
#define STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STORAGE_BLOB_NAME_LENGTH 8

typedef struct storage_blob_t {
    const uint8_t *data;
    size_t size;
    uintptr_t backend_handle;
    bool owns_data;
} storage_blob_t;

bool storage_open_blob(const char *name, storage_blob_t *out_blob);
void storage_close_blob(storage_blob_t *blob);
uint8_t storage_count_by_magic(const char *magic);
bool storage_get_name_by_magic(const char *magic, uint8_t index, char *out_name);

#endif
