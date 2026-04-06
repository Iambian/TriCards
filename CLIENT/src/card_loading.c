#include <string.h>

#include <compression.h>

#include "tricards.h"
#include "storage.h"

static storage_blob_t current_pack_blob;
static storage_blob_t current_shard_blob;
static const uint8_t *current_pack_ptr;
static const uint8_t *current_shard_ptr;
static char current_pack_name[TRICARD_VAR_NAME_LENGTH + 1];
static char current_shard_name[TRICARD_VAR_NAME_LENGTH + 1];

static void copyvarname(char *destination, const char *source) {
    memset(destination, 0, TRICARD_VAR_NAME_LENGTH + 1);
    if (source != NULL) {
        memcpy(destination, source, TRICARD_VAR_NAME_LENGTH);
    }
}

static bool headermatches(
    const tricard_pack_header_t *header,
    const char *magic,
    uint8_t version
) {
    if (header == NULL) {
        return false;
    }
    return memcmp(header->magic, magic, TRICARD_MAGIC_LENGTH) == 0 && header->version == version;
}

static bool issinglefilepack(const tricard_pack_header_t *header) {
    return headermatches(header, TRICARD_SINGLE_FILE_MAGIC, TRICARD_SINGLE_FILE_VERSION);
}

static bool ismanifestpack(const tricard_pack_header_t *header) {
    return headermatches(header, TRICARD_MANIFEST_MAGIC, TRICARD_MULTIFILE_VERSION);
}

static bool isshardpack(const tricard_pack_header_t *header) {
    return headermatches(header, TRICARD_SHARD_MAGIC, TRICARD_MULTIFILE_VERSION);
}

static void closeshard(void) {
    storage_close_blob(&current_shard_blob);
    current_shard_ptr = NULL;
    current_shard_name[0] = 0;
}

static bool openpackfile(const char *var_name, storage_blob_t *blob, const uint8_t **packptr) {
    if (!storage_open_blob(var_name, blob)) {
        return false;
    }

    if (blob->size < sizeof(tricard_pack_header_t)) {
        storage_close_blob(blob);
        return false;
    }

    *packptr = blob->data;
    return true;
}

static uint16_t getpackrecordsize(const tricard_pack_header_t *header) {
    uint16_t record_table_size;

    if (header == NULL || header->card_count == 0) {
        return 0;
    }
    if (header->string_table_offset < header->record_table_offset) {
        return 0;
    }

    record_table_size = header->string_table_offset - header->record_table_offset;
    if ((record_table_size % header->card_count) != 0) {
        return 0;
    }

    return record_table_size / header->card_count;
}

static uint8_t getpackpaletteentries(const tricard_pack_header_t *header) {
    uint16_t record_size;

    record_size = getpackrecordsize(header);
    if (record_size < sizeof(tricard_card_metadata_t)) {
        return 0;
    }
    if (((record_size - sizeof(tricard_card_metadata_t)) % sizeof(uint16_t)) != 0) {
        return 0;
    }

    return (uint8_t)((record_size - sizeof(tricard_card_metadata_t)) / sizeof(uint16_t));
}

static uint16_t getmanifestshardcount(const tricard_pack_header_t *header) {
    uint16_t directory_size;

    if (!ismanifestpack(header)) {
        return 0;
    }
    if (header->string_table_offset < header->record_table_offset) {
        return 0;
    }

    directory_size = header->string_table_offset - header->record_table_offset;
    if ((directory_size % sizeof(tricard_manifest_shard_t)) != 0) {
        return 0;
    }

    return (uint16_t)(directory_size / sizeof(tricard_manifest_shard_t));
}

static const tricard_manifest_shard_t *getmanifestshardentry(
    const uint8_t *packptr,
    uint16_t shard_index
) {
    const tricard_pack_header_t *header;
    const tricard_manifest_shard_t *entries;
    uint16_t shard_count;

    header = getpackheader(packptr);
    shard_count = getmanifestshardcount(header);
    if (shard_index >= shard_count) {
        return NULL;
    }

    entries = (const tricard_manifest_shard_t *)(packptr + header->record_table_offset);
    return &entries[shard_index];
}

static const tricard_card_metadata_t *getlocalcardmetadata(const uint8_t *packptr, uint16_t cardnum) {
    const tricard_pack_header_t *header;
    const uint8_t *record_base;
    uint16_t record_size;

    header = getpackheader(packptr);
    if (header == NULL || cardnum >= header->card_count) {
        return NULL;
    }

    record_base = packptr + header->record_table_offset;
    record_size = getpackrecordsize(header);
    if (record_size < sizeof(tricard_card_metadata_t)) {
        return NULL;
    }

    return (const tricard_card_metadata_t *)(record_base + (cardnum * record_size));
}

static bool openmanifestshard(
    const uint8_t *manifestptr,
    const tricard_manifest_shard_t *entry
) {
    const tricard_pack_header_t *manifest_header;
    const tricard_pack_header_t *shard_header;
    char shard_var_name[TRICARD_VAR_NAME_LENGTH + 1];

    if (manifestptr == NULL || entry == NULL) {
        return false;
    }

    copyvarname(shard_var_name, entry->var_name);
    if (current_shard_ptr != NULL && strcmp(current_shard_name, shard_var_name) == 0) {
        return true;
    }

    manifest_header = getpackheader(manifestptr);
    closeshard();
    if (!openpackfile(shard_var_name, &current_shard_blob, &current_shard_ptr)) {
        return false;
    }

    shard_header = getpackheader(current_shard_ptr);
    if (!isshardpack(shard_header)
        || shard_header->card_count != entry->card_count
        || shard_header->palette_entry_count != manifest_header->palette_entry_count
        || shard_header->transparent_color != manifest_header->transparent_color
        || shard_header->compression_method != manifest_header->compression_method
        || memcmp(
            shard_header->pack_identifier,
            manifest_header->pack_identifier,
            sizeof shard_header->pack_identifier
        ) != 0) {
        closeshard();
        return false;
    }

    copyvarname(current_shard_name, shard_var_name);
    return true;
}

static const uint8_t *resolvepackpayload(
    const uint8_t *packptr,
    uint16_t cardnum,
    uint16_t *local_cardnum
) {
    const tricard_pack_header_t *header;
    const tricard_manifest_shard_t *entry;
    uint16_t shard_index;
    uint16_t shard_count;
    uint32_t shard_end;

    header = getpackheader(packptr);
    if (header == NULL || cardnum >= header->card_count) {
        return NULL;
    }

    if (issinglefilepack(header) || isshardpack(header)) {
        if (local_cardnum != NULL) {
            *local_cardnum = cardnum;
        }
        return packptr;
    }
    if (!ismanifestpack(header)) {
        return NULL;
    }

    shard_count = getmanifestshardcount(header);
    for (shard_index = 0; shard_index < shard_count; shard_index++) {
        entry = getmanifestshardentry(packptr, shard_index);
        if (entry == NULL || entry->card_count == 0) {
            continue;
        }

        shard_end = (uint32_t)entry->first_card_index + entry->card_count;
        if ((uint32_t)cardnum < entry->first_card_index || (uint32_t)cardnum >= shard_end) {
            continue;
        }
        if (!openmanifestshard(packptr, entry)) {
            return NULL;
        }

        if (local_cardnum != NULL) {
            *local_cardnum = (uint16_t)(cardnum - entry->first_card_index);
        }
        return current_shard_ptr;
    }

    return NULL;
}

static bool transformcardimagedata(
    const uint16_t *palette_data,
    uint8_t palette_entries,
    uint8_t cardslot,
    uint8_t *sprite_data
) {
    uint16_t palette_base;
    uint16_t palette_limit;
    uint16_t pixel_index;
    uint8_t *pixel_data;

    palette_base = CARD_SLOT_PALETTE_INDEX(cardslot, palette_entries);
    palette_limit = palette_base + CARD_PALETTE_SLICE_SIZE(palette_entries);
    if (palette_limit > 256) {
        return false;
    }

    if (palette_entries > 0) {
        memcpy(&gfx_palette[palette_base + 1], palette_data, palette_entries * sizeof(*palette_data));
    }

    pixel_data = sprite_data + 2;
    for (pixel_index = 0; pixel_index < (CARD_WIDTH * CARD_HEIGHT); pixel_index++) {
        if (pixel_data[pixel_index] == CARD_IMAGE_TRANSPARENT_SENTINEL) {
            pixel_data[pixel_index] = (uint8_t)palette_base;
            continue;
        }
        if (pixel_data[pixel_index] >= palette_entries) {
            return false;
        }
        pixel_data[pixel_index] = (uint8_t)(palette_base + 1 + pixel_data[pixel_index]);
    }

    return true;
}

uint8_t countpacks(void) {
    uint8_t count;
    uint8_t manifest_count;

    count = storage_count_by_magic(TRICARD_SINGLE_FILE_MAGIC);
    manifest_count = storage_count_by_magic(TRICARD_MANIFEST_MAGIC);
    if (manifest_count > (uint8_t)(0xFF - count)) {
        return 0xFF;
    }
    return (uint8_t)(count + manifest_count);
}

bool getpackname(uint8_t pack_index, char *out_name) {
    uint8_t single_file_count;

    if (out_name == NULL) {
        return false;
    }

    if (storage_get_name_by_magic(TRICARD_SINGLE_FILE_MAGIC, pack_index, out_name)) {
        return true;
    }
    single_file_count = storage_count_by_magic(TRICARD_SINGLE_FILE_MAGIC);
    if (pack_index >= single_file_count
        && storage_get_name_by_magic(
            TRICARD_MANIFEST_MAGIC,
            (uint8_t)(pack_index - single_file_count),
            out_name
        )) {
        return true;
    }

    out_name[0] = 0;
    return false;
}

const uint8_t *getpackadr(const char *vn) {
    const tricard_pack_header_t *header;

    closepack();
    if (!openpackfile(vn, &current_pack_blob, &current_pack_ptr)) {
        return NULL;
    }

    header = getpackheader(current_pack_ptr);
    if (!issinglefilepack(header) && !ismanifestpack(header)) {
        closepack();
        return NULL;
    }
    if (ismanifestpack(header) && getmanifestshardcount(header) == 0) {
        closepack();
        return NULL;
    }

    copyvarname(current_pack_name, vn);
    return current_pack_ptr;
}

void closepack(void) {
    closeshard();
    storage_close_blob(&current_pack_blob);
    current_pack_ptr = NULL;
    current_pack_name[0] = 0;
}

const tricard_pack_header_t *getpackheader(const uint8_t *packptr) {
    if (packptr == NULL) {
        return NULL;
    }
    return (const tricard_pack_header_t *)packptr;
}

const char *getpackdescription(const uint8_t *packptr) {
    const tricard_pack_header_t *header;

    header = getpackheader(packptr);
    if (header == NULL) {
        return NULL;
    }
    return (const char *)(packptr + header->description_offset);
}

uint16_t getpackcardcount(const uint8_t *packptr) {
    const tricard_pack_header_t *header;

    header = getpackheader(packptr);
    if (header == NULL) {
        return 0;
    }
    return header->card_count;
}

const tricard_card_metadata_t *getcardmetadata(const uint8_t *packptr, uint16_t cardnum) {
    uint16_t local_cardnum;
    const uint8_t *resolved_packptr;

    resolved_packptr = resolvepackpayload(packptr, cardnum, &local_cardnum);
    if (resolved_packptr == NULL) {
        return NULL;
    }

    return getlocalcardmetadata(resolved_packptr, local_cardnum);
}

const char *getcardname(const uint8_t *packptr, uint16_t cardnum) {
    const tricard_card_metadata_t *metadata;
    uint16_t local_cardnum;
    const uint8_t *resolved_packptr;

    resolved_packptr = resolvepackpayload(packptr, cardnum, &local_cardnum);
    if (resolved_packptr == NULL) {
        return NULL;
    }

    metadata = getlocalcardmetadata(resolved_packptr, local_cardnum);
    if (metadata == NULL) {
        return NULL;
    }

    return (const char *)(resolved_packptr + metadata->name_offset);
}

void resetcardslot(uint8_t cardslot) {
    uint8_t *buffer;

    buffer = card_image_pool + (CARD_IMAGE_BUFFER_SIZE * cardslot);
    memset(cardbuf[cardslot], 0, sizeof *cardbuf[cardslot]);
    cardbuf[cardslot]->slot_index = cardslot;
    cardbuf[cardslot]->palette_base_index = 0;
    cardbuf[cardslot]->source.decompressed_card_data = buffer;
    cardbuf[cardslot]->card.img = (gfx_sprite_t *)buffer;
    buffer[0] = CARD_WIDTH;
    buffer[1] = CARD_HEIGHT;
    memset(buffer + 2, CARD_IMAGE_TRANSPARENT_SENTINEL, CARD_WIDTH * CARD_HEIGHT);
}

bool loadcardslot(const uint8_t *packptr, uint16_t cardnum, uint8_t cardslot) {
    const tricard_pack_header_t *header;
    const tricard_card_metadata_t *metadata;
    const uint16_t *palette_data;
    const uint8_t *compressed_card_data;
    tricard_card_slot_t *slot;
    uint16_t local_cardnum;
    uint8_t palette_entries;
    const uint8_t *resolved_packptr;
    uint8_t *sprite_data;

    resolved_packptr = resolvepackpayload(packptr, cardnum, &local_cardnum);
    if (resolved_packptr == NULL || cardslot >= CARD_SLOT_COUNT) {
        return false;
    }

    header = getpackheader(resolved_packptr);
    metadata = getlocalcardmetadata(resolved_packptr, local_cardnum);
    palette_entries = getpackpaletteentries(header);
    if (metadata == NULL) {
        return false;
    }

    resetcardslot(cardslot);
    slot = cardbuf[cardslot];
    sprite_data = slot->source.decompressed_card_data;
    palette_data = (const uint16_t *)((const uint8_t *)metadata + sizeof *metadata);
    compressed_card_data = resolved_packptr + metadata->image_offset;
    slot->palette_base_index = (uint8_t)CARD_SLOT_PALETTE_INDEX(cardslot, palette_entries);

    slot->source.metadata = NULL;
    slot->source.card_name = NULL;
    slot->source.card_image_name = NULL;
    slot->source.image_data_in_file = NULL;
    slot->source.compressed_card_data = NULL;

    slot->card.rank = metadata->rank;
    slot->card.name = NULL;
    slot->card.type = metadata->type;
    slot->card.up = metadata->up;
    slot->card.right = metadata->right;
    slot->card.down = metadata->down;
    slot->card.left = metadata->left;
    slot->card.element = metadata->element;
    slot->card.img = (gfx_sprite_t *)sprite_data;

    sprite_data[0] = CARD_WIDTH;
    sprite_data[1] = CARD_HEIGHT;
    if (header->compression_method == PACK_COMPRESSION_ZX7) {
        zx7_Decompress(sprite_data + 2, compressed_card_data);
    } else if (header->compression_method == PACK_COMPRESSION_ZX0) {
        zx0_Decompress(sprite_data + 2, compressed_card_data);
    } else {
        memset(sprite_data + 2, CARD_IMAGE_TRANSPARENT_SENTINEL, CARD_WIDTH * CARD_HEIGHT);
    }

    return transformcardimagedata(
        palette_data,
        palette_entries,
        cardslot,
        sprite_data
    );
}
