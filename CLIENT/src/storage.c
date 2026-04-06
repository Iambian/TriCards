#include <stdlib.h>
#include <string.h>

#include <fileioc.h>

#include "storage.h"

static void copyblobname(char *destination, const char *source) {
    memset(destination, 0, STORAGE_BLOB_NAME_LENGTH + 1);
    if (source != NULL) {
        memcpy(destination, source, STORAGE_BLOB_NAME_LENGTH);
    }
}

bool storage_open_blob(const char *name, storage_blob_t *out_blob) {
    ti_var_t handle;
    storage_blob_t blob;

    if (name == NULL || out_blob == NULL) {
        return false;
    }

    handle = ti_Open(name, "r");
    if (!handle) {
        return false;
    }

    blob.data = ti_GetDataPtr(handle);
    if (blob.data == NULL) {
        ti_Close(handle);
        return false;
    }

    blob.size = ti_GetSize(handle);
    blob.backend_handle = handle;
    blob.owns_data = false;
    *out_blob = blob;
    return true;
}

void storage_close_blob(storage_blob_t *blob) {
    if (blob == NULL) {
        return;
    }

    if (blob->backend_handle) {
        ti_Close((ti_var_t)blob->backend_handle);
    }
    if (blob->owns_data && blob->data != NULL) {
        free((void *)blob->data);
    }

    blob->data = NULL;
    blob->size = 0;
    blob->backend_handle = 0;
    blob->owns_data = false;
}

uint8_t storage_count_by_magic(const char *magic) {
    void *vat_ptr;
    uint8_t count;

    count = 0;
    vat_ptr = NULL;
    while (ti_Detect(&vat_ptr, magic) != NULL) {
        if (count == 0xFF) {
            return count;
        }
        count++;
    }
    return count;
}

bool storage_get_name_by_magic(const char *magic, uint8_t index, char *out_name) {
    void *vat_ptr;
    char *blob_name;
    uint8_t current_index;

    if (out_name == NULL) {
        return false;
    }

    current_index = 0;
    vat_ptr = NULL;
    while ((blob_name = ti_Detect(&vat_ptr, magic)) != NULL) {
        if (current_index == index) {
            copyblobname(out_name, blob_name);
            return true;
        }
        current_index++;
    }

    out_name[0] = 0;
    return false;
}
