#include <string.h>

#include <compression.h>
#include <fileioc.h>

#include "tricards.h"

static ti_var_t current_pack_file;

static uint16_t getpackrecordsize(const tricard_pack_header_t *header) {
    uint16_t record_table_size;

    if (header->card_count == 0) {
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

uint8_t *getpackadr(char *vn) {
    closepack();
    if ((current_pack_file = ti_Open(vn, "r"))) {
        return ti_GetDataPtr(current_pack_file);
    }

    return NULL;
}

void closepack(void) {
    if (current_pack_file) {
        ti_Close(current_pack_file);
        current_pack_file = 0;
    }
}

const tricard_pack_header_t *getpackheader(uint8_t *packptr) {
    return (const tricard_pack_header_t *)packptr;
}

const char *getpackdescription(uint8_t *packptr) {
    const tricard_pack_header_t *header;

    header = getpackheader(packptr);
    return (const char *)(packptr + header->description_offset);
}

uint16_t getpackcardcount(uint8_t *packptr) {
    return getpackheader(packptr)->card_count;
}

const tricard_card_metadata_t *getcardmetadata(uint8_t *packptr, uint16_t cardnum) {
    const tricard_pack_header_t *header;
    uint8_t *record_base;
    uint16_t record_size;

    header = getpackheader(packptr);
    if (cardnum >= header->card_count) {
        return NULL;
    }

    record_base = packptr + header->record_table_offset;
    record_size = getpackrecordsize(header);
    if (record_size < sizeof(tricard_card_metadata_t)) {
        return NULL;
    }

    return (const tricard_card_metadata_t *)(record_base + (cardnum * record_size));
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

bool loadcardslot(uint8_t *packptr, uint16_t cardnum, uint8_t cardslot) {
    const tricard_pack_header_t *header;
    const tricard_card_metadata_t *metadata;
    const uint16_t *palette_data;
    tricard_card_slot_t *slot;
    uint8_t palette_entries;
    uint8_t *sprite_data;

    header = getpackheader(packptr);
    metadata = getcardmetadata(packptr, cardnum);
    palette_entries = getpackpaletteentries(header);
    if (metadata == NULL || cardslot >= CARD_SLOT_COUNT) {
        return false;
    }

    resetcardslot(cardslot);
    slot = cardbuf[cardslot];
    sprite_data = slot->source.decompressed_card_data;
    palette_data = (const uint16_t *)((const uint8_t *)metadata + sizeof *metadata);
    slot->palette_base_index = (uint8_t)CARD_SLOT_PALETTE_INDEX(cardslot, palette_entries);

    slot->source.metadata = metadata;
    slot->source.card_name = (const char *)(packptr + metadata->name_offset);
    slot->source.card_image_name = (const char *)(packptr + metadata->image_name_offset);
    slot->source.image_data_in_file = packptr + metadata->image_offset;
    slot->source.compressed_card_data = packptr + metadata->image_offset;

    slot->card.rank = metadata->rank;
    slot->card.name = slot->source.card_name;
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
        zx7_Decompress(sprite_data + 2, slot->source.compressed_card_data);
    } else if (header->compression_method == PACK_COMPRESSION_ZX0) {
        zx0_Decompress(sprite_data + 2, slot->source.compressed_card_data);
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
