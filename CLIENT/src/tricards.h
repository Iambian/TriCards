#ifndef TRICARDS_H
#define TRICARDS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <tice.h>

#include <graphx.h>

#define VERSION_INFO "v0.2"

#define GM_TITLE 0
#define GM_BROWSEPACK 4
#define GM_CARDLISTER 5
#define GM_OPTIONS 6
#define GM_GAMESELECT 7
#define GM_RULESELECT 8
#define GM_SELECTINGCARDS 9
#define GM_SELECTINGPLACE 10
#define GM_GAMEXIT 255

#define CARD_WIDTH 52
#define CARD_HEIGHT 52
#define GAME_CARD_SLOT_COUNT 10
#define PACK_SELECTOR_PREVIEW_COUNT 5
#define CARD_PACK_SELECTOR_SLOT_BASE 0
#define CARD_BROWSER_PREVIEW_SLOT GAME_CARD_SLOT_COUNT
#define CARD_SLOT_COUNT (CARD_BROWSER_PREVIEW_SLOT + 1)
#define CARD_IMAGE_BUFFER_SIZE (CARD_WIDTH * CARD_HEIGHT + 2)
#define INTERNAL_PALETTE_BASE_INDEX 0
#define INTERNAL_PALETTE_COLOR(index) (INTERNAL_PALETTE_BASE_INDEX + (index))
#define INTERNAL_TRANSPARENT_INDEX INTERNAL_PALETTE_COLOR(0)
#define INTERNAL_BLACK_COLOR INTERNAL_PALETTE_COLOR(1)
#define MENU_TEXT_SELECTED INTERNAL_PALETTE_COLOR(2)
#define LIST_BG_S INTERNAL_PALETTE_COLOR(3)
#define LIST_BG_A INTERNAL_PALETTE_COLOR(4)
#define LIST_BG_B INTERNAL_PALETTE_COLOR(5)
#define CARD_SEL_FG INTERNAL_PALETTE_COLOR(6)
#define PLAYER2_BG INTERNAL_PALETTE_COLOR(7)
#define FILE_EXPLORER_BGCOLOR INTERNAL_PALETTE_COLOR(8)
#define GAMEBOARD_BG INTERNAL_PALETTE_COLOR(9)
#define GREETINGS_DIALOG_TEXT_COLOR INTERNAL_PALETTE_COLOR(10)
#define LIST_TX_S INTERNAL_PALETTE_COLOR(11)
#define PLAYER1_BG INTERNAL_PALETTE_COLOR(12)
#define INTERNAL_WHITE_COLOR INTERNAL_PALETTE_COLOR(13)
#define CARD_PALETTE_BASE_INDEX 64
#define CARD_PALETTE_SLICE_SIZE(palette_entries) ((palette_entries) + 1)
#define CARD_SLOT_PALETTE_INDEX(slot, palette_entries) \
    (CARD_PALETTE_BASE_INDEX + ((slot) * CARD_PALETTE_SLICE_SIZE(palette_entries)))
#define SET_CARD_SLOT_BASE_COLOR(card_slot, color) \
    do { \
        gfx_palette[(card_slot)->palette_base_index] = gfx_palette[(color)]; \
    } while (0)
#define SET_CARD_SLOT_BASE_COLOR_VALUE(card_slot, color_value) \
    do { \
        gfx_palette[(card_slot)->palette_base_index] = (color_value); \
    } while (0)

#define CARD_IMAGE_TRANSPARENT_SENTINEL 0xFF
#define LIST_LINE_HEIGHT 12

#define MENU_TEXT_COLOR INTERNAL_BLACK_COLOR
#define RULE_ENABLED_TEXT_COLOR GREETINGS_DIALOG_TEXT_COLOR
#define OPTIONS_PER_PAGE 11

#define GMBOX_X (LCD_WIDTH / 4)
#define GMBOX_Y (LCD_HEIGHT / 2 - LCD_HEIGHT / 8)
#define GMBOX_W (LCD_WIDTH / 2)
#define GMBOX_H (LCD_HEIGHT / 4)

#define GRIDX 72
#define GRIDY 32
#define GRIDV 60
#define PLAYERX 5
#define PLAYERY 32
#define PLAYERV 30
#define ENEMYX 260
#define ENEMYY 32
#define ENEMYV 30

enum cardtype { monster = 0, boss, gf, player };
enum element { none = 0, poison, fire, wind, earth, water, ice, thunder, holy };
enum directionValues { DIR_NONE = 0, DIR_DOWN, DIR_LEFT, DIR_RIGHT, DIR_UP };
enum playRuleFlags {
    RULE_OPEN = 1,
    RULE_SAME = 2,
    RULE_SAMEWALL = 4,
    RULE_SUDDENDEATH = 8,
    RULE_RANDOM = 16,
    RULE_PLUS = 32,
    RULE_COMBO = 64,
    RULE_ELEMENTAL = 128
};

enum player2ControlMode {
    PLAYER2_CONTROL_MANUAL = 0,
    PLAYER2_AI_EASY = 1,
    PLAYER2_AI_MEDIUM = 2,
    PLAYER2_AI_HARD = 3
};

#define DEFAULT_RULE_FLAGS (RULE_OPEN | RULE_RANDOM | RULE_ELEMENTAL | RULE_SUDDENDEATH)

enum packCompressionMethod {
    PACK_COMPRESSION_ZX7 = 0,
    PACK_COMPRESSION_ZX0 = 1
};

#define TRICARD_MAGIC_LENGTH 8
#define TRICARD_VAR_NAME_LENGTH 8
#define TRICARD_SINGLE_FILE_MAGIC "Tri2Pak!"
#define TRICARD_MANIFEST_MAGIC "Tri2Mft!"
#define TRICARD_SHARD_MAGIC "Tri2Shd!"
#define TRICARD_SINGLE_FILE_VERSION 2
#define TRICARD_MULTIFILE_VERSION 3

typedef struct __attribute__((packed)) tricard_pack_header_t {
    char magic[8];
    uint8_t version;
    uint16_t card_count;
    uint8_t palette_entry_count;
    uint16_t transparent_color;
    char pack_identifier[9];
    uint8_t compression_method;
    uint16_t description_offset;
    uint16_t record_table_offset;
    uint16_t string_table_offset;
    uint16_t image_blob_offset;
} tricard_pack_header_t;

typedef struct __attribute__((packed)) tricard_card_metadata_t {
    uint8_t rank;
    uint8_t type;
    uint8_t up;
    uint8_t right;
    uint8_t down;
    uint8_t left;
    uint8_t element;
    uint8_t reserved;
    uint16_t name_offset;
    uint16_t image_name_offset;
    uint16_t image_offset;
    uint16_t image_size;
} tricard_card_metadata_t;

typedef struct __attribute__((packed)) tricard_manifest_shard_t {
    char var_name[8];
    uint16_t first_card_index;
    uint16_t card_count;
    uint16_t payload_size;
    uint16_t reserved;
} tricard_manifest_shard_t;

_Static_assert(sizeof(tricard_pack_header_t) == 32, "tricard_pack_header_t must stay 32 bytes");
_Static_assert(sizeof(tricard_card_metadata_t) == 16, "tricard_card_metadata_t must stay 16 bytes");
_Static_assert(sizeof(tricard_manifest_shard_t) == 16, "tricard_manifest_shard_t must stay 16 bytes");

typedef struct tricard_card_source_t {
    const tricard_card_metadata_t *metadata;
    const char *card_name;
    const char *card_image_name;
    const uint8_t *image_data_in_file;
    const uint8_t *compressed_card_data;
    uint8_t *decompressed_card_data;
} tricard_card_source_t;

typedef struct tricard_runtime_card_t {
    uint8_t rank;
    const char *name;
    uint8_t type;
    uint8_t up;
    uint8_t right;
    uint8_t down;
    uint8_t left;
    uint8_t element;
    gfx_sprite_t *img;
} tricard_runtime_card_t;

typedef struct tricard_card_slot_t {
    tricard_card_source_t source;
    tricard_runtime_card_t card;
    int x;
    int y;
    uint8_t playstate;
    uint8_t gridpos;
    uint8_t isplayer1;
    uint8_t slot_index;
    uint8_t palette_base_index;
    uint8_t color_transition_active;
    uint8_t color_transition_frame;
    uint16_t color_transition_source;
    uint16_t color_transition_target;
} tricard_card_slot_t;

extern int posarr[];

extern uint8_t *card_image_pool;
extern uint8_t curpack;
extern uint8_t maxpack;
extern uint8_t gamemode;
extern uint8_t selcard;
extern uint8_t curplayer;
extern uint8_t match_start_player;
extern uint8_t player2_ai_difficulty;
extern uint8_t ruleFlags;
extern bool sudden_death_active;
extern bool ai_thinking;

extern uint8_t *elemdat[9];
extern tricard_card_slot_t *cardbuf[CARD_SLOT_COUNT];
extern gfx_sprite_t *numtiles[12];
extern gfx_sprite_t *cardback;
extern uint8_t elementgrid[9];

extern char *card_pack_header;
extern char *main_menu_text[];
extern uint8_t main_menu_dest[];
extern uint8_t listcolors[];
extern char stat2char[];
extern char *cardtype[];
extern uint8_t *elemcdat[];

void keywait(void);
void waitanykey(void);
void ctext(char *s, uint8_t y);
void textscale2(void);
void textscale1(void);
void dmenu(char **strarr, uint8_t curopt, uint8_t maxopt);
void pcharxy(char c, int x, uint8_t y);
void drawbg(void);
void closegamebackground(void);

char *selectpack(void);
uint8_t countpacks(void);
bool getpackname(uint8_t pack_index, char *out_name);
const uint8_t *getpackadr(const char *varname);
void closepack(void);
const tricard_pack_header_t *getpackheader(const uint8_t *packptr);
const tricard_card_metadata_t *getcardmetadata(const uint8_t *packptr, uint16_t cardnum);
const char *getpackdescription(const uint8_t *packptr);
uint16_t getpackcardcount(const uint8_t *packptr);
const char *getcardname(const uint8_t *packptr, uint16_t cardnum);
void resetcardslot(uint8_t cardslot);
bool loadcardslot(const uint8_t *packptr, uint16_t cardnum, uint8_t cardslot);

void redrawboard(void);
uint8_t selectfromhand(uint8_t direction);
tricard_card_slot_t *getcardongrid(uint8_t gridpos);
bool getplayer2aimove(uint8_t *out_card_slot, uint8_t *out_gridpos);
void cardfight(uint8_t pidx, uint8_t eidx);
void resolvecardplacement(uint8_t gridpos);
void initGame(const uint8_t *packptr);
bool startsuddendeathround(void);

#endif
