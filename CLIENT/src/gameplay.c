#include <string.h>

#include "tricards.h"
#include "storage.h"

#define CARD_CAPTURE_FADE_TO_WHITE_FRAMES 8
#define CARD_CAPTURE_FADE_TO_TARGET_FRAMES 8
#define CARD_CAPTURE_TOTAL_FRAMES \
    (CARD_CAPTURE_FADE_TO_WHITE_FRAMES + CARD_CAPTURE_FADE_TO_TARGET_FRAMES)
#define AI_SCORE_WIN 100000
#define AI_SCORE_MIN (-AI_SCORE_WIN * 2)
#define AI_SCORE_MAX (AI_SCORE_WIN * 2)
#define GAME_BACKGROUND_LEFT_VAR_NAME "TRICARDL"
#define GAME_BACKGROUND_RIGHT_VAR_NAME "TRICARDR"
#define GAME_BACKGROUND_HALF_WIDTH 160

static storage_blob_t game_background_left_blob;
static storage_blob_t game_background_right_blob;
static gfx_sprite_t *game_background_left_sprite;
static gfx_sprite_t *game_background_right_sprite;
static bool game_background_loaded;
static bool game_background_checked;

static void closegamebackgroundfiles(void) {
    storage_close_blob(&game_background_left_blob);
    storage_close_blob(&game_background_right_blob);
    game_background_left_sprite = NULL;
    game_background_right_sprite = NULL;
    game_background_loaded = false;
}

void closegamebackground(void) {
    closegamebackgroundfiles();
    game_background_checked = false;
}

static bool isgamebackgroundspritevalid(const storage_blob_t *blob) {
    const uint8_t *sprite_data;

    if (blob == NULL
        || blob->data == NULL
        || blob->size != (size_t)(GAME_BACKGROUND_HALF_WIDTH * LCD_HEIGHT + 2)) {
        return false;
    }
    sprite_data = blob->data;
    return sprite_data[0] == GAME_BACKGROUND_HALF_WIDTH && sprite_data[1] == LCD_HEIGHT;
}

static bool opengamebackgroundhalf(
    const char *var_name,
    storage_blob_t *blob,
    gfx_sprite_t **sprite
) {
    if (!storage_open_blob(var_name, blob)) {
        return false;
    }

    if (!isgamebackgroundspritevalid(blob)) {
        storage_close_blob(blob);
        return false;
    }

    *sprite = (gfx_sprite_t *)blob->data;
    return true;
}

static void opengamebackground(void) {
    if (game_background_loaded || game_background_checked) {
        return;
    }

    game_background_checked = true;
    closegamebackgroundfiles();
    if (!opengamebackgroundhalf(
            GAME_BACKGROUND_LEFT_VAR_NAME,
            &game_background_left_blob,
            &game_background_left_sprite
        )
        || !opengamebackgroundhalf(
            GAME_BACKGROUND_RIGHT_VAR_NAME,
            &game_background_right_blob,
            &game_background_right_sprite
        )) {
        closegamebackgroundfiles();
        return;
    }

    game_background_loaded = true;
}

static void drawgamebackground(void) {
    if (!game_background_loaded) {
        gfx_FillScreen(GAMEBOARD_BG);
        return;
    }

    gfx_Sprite_NoClip(game_background_left_sprite, 0, 0);
    gfx_Sprite_NoClip(game_background_right_sprite, GAME_BACKGROUND_HALF_WIDTH, 0);
}

static uint16_t getplayerbgcolor(uint8_t isplayer1) {
    return gfx_palette[isplayer1 ? PLAYER1_BG : PLAYER2_BG];
}

static uint8_t gettransitionamount(uint8_t frame, uint8_t max_frame) {
    uint16_t scaled_amount;

    if (max_frame == 0) {
        return 255;
    }
    if (frame >= max_frame) {
        return 255;
    }
    scaled_amount = (uint16_t)frame * 255 / max_frame;
    return (uint8_t)scaled_amount;
}

static uint16_t getcardtransitioncolor(const tricard_card_slot_t *card) {
    uint8_t phase_frame;

    if (!card->color_transition_active) {
        return getplayerbgcolor(card->isplayer1);
    }

    if (card->color_transition_frame <= CARD_CAPTURE_FADE_TO_WHITE_FRAMES) {
        return gfx_Lighten(
            card->color_transition_source,
            (uint8_t)(255 - gettransitionamount(
                card->color_transition_frame,
                CARD_CAPTURE_FADE_TO_WHITE_FRAMES
            ))
        );
    }

    phase_frame = (uint8_t)(card->color_transition_frame - CARD_CAPTURE_FADE_TO_WHITE_FRAMES);
    return gfx_Lighten(
        card->color_transition_target,
        gettransitionamount(phase_frame, CARD_CAPTURE_FADE_TO_TARGET_FRAMES)
    );
}

static void startcardtransition(tricard_card_slot_t *card, uint16_t source_color, uint16_t target_color) {
    card->color_transition_active = 1;
    card->color_transition_frame = 0;
    card->color_transition_source = source_color;
    card->color_transition_target = target_color;
}

static void updatecardtransitions(void) {
    uint8_t i;
    tricard_card_slot_t *card;

    for (i = 0; i < GAME_CARD_SLOT_COUNT; i++) {
        card = cardbuf[i];
        if (!card->color_transition_active) {
            continue;
        }
        if (card->color_transition_frame < CARD_CAPTURE_TOTAL_FRAMES) {
            card->color_transition_frame++;
            continue;
        }
        card->color_transition_active = 0;
        card->color_transition_frame = 0;
        card->color_transition_source = 0;
        card->color_transition_target = 0;
    }
}

static void resetcardtransition(tricard_card_slot_t *card) {
    card->color_transition_active = 0;
    card->color_transition_frame = 0;
    card->color_transition_source = 0;
    card->color_transition_target = 0;
}

static const char *getturnindicatorlabel(void) {
    if (curplayer == 0) {
        return "Turn: Player 1";
    }
    if (player2_ai_difficulty == PLAYER2_CONTROL_MANUAL) {
        return "Turn: Player 2";
    }
    return "Turn: AI";
}

static void drawgamehud(void) {
    const char *status_lines[3];
    uint8_t line_count;
    uint8_t i;
    uint8_t box_x;
    uint8_t box_y;
    uint8_t box_w;
    uint8_t box_h;
    uint8_t text_y;
    uint16_t max_width;
    uint16_t current_width;

    line_count = 0;
    if (sudden_death_active) {
        status_lines[line_count++] = "Sudden Death";
    }
    status_lines[line_count++] = getturnindicatorlabel();
    if (ai_thinking) {
        status_lines[line_count++] = "AI thinking...";
    }

    max_width = 0;
    for (i = 0; i < line_count; i++) {
        current_width = gfx_GetStringWidth(status_lines[i]);
        if (current_width > max_width) {
            max_width = current_width;
        }
    }

    box_w = (uint8_t)(max_width + 12);
    box_h = (uint8_t)(line_count * 8 + 6);
    box_x = (uint8_t)((LCD_WIDTH - box_w) / 2);
    box_y = 4;

    gfx_SetColor(FILE_EXPLORER_BGCOLOR);
    gfx_FillRectangle_NoClip(box_x, box_y, box_w, box_h);
    gfx_SetColor(INTERNAL_BLACK_COLOR);
    gfx_Rectangle_NoClip(box_x, box_y, box_w, box_h);
    gfx_SetTextFGColor(MENU_TEXT_COLOR);
    for (i = 0, text_y = (uint8_t)(box_y + 3); i < line_count; i++, text_y += 8) {
        gfx_PrintStringXY(
            (char *)status_lines[i],
            (LCD_WIDTH - gfx_GetStringWidth(status_lines[i])) / 2,
            text_y
        );
    }
}

static void drawcard(tricard_card_slot_t *card, bool selected) {
    int x, y, cx, cy;
    uint8_t gpos;
    uint16_t base_color;
    tricard_runtime_card_t *cdata;

    x = card->x;
    y = card->y;
    if (!card->gridpos) {
        return;
    }

    cx = posarr[card->gridpos * 2];
    cy = posarr[card->gridpos * 2 + 1];
    if (x != cx) {
        x = cx;
    }
    if (y != cy) {
        y = cy;
    }
    card->x = x;
    card->y = y;

    cdata = &card->card;
    base_color = getcardtransitioncolor(card);
    SET_CARD_SLOT_BASE_COLOR_VALUE(card, base_color);
    if (selected) {
        gpos = card->gridpos;
        if (gpos > 9 && gpos < 15) {
            x += 5;
        }
        if (gpos > 14) {
            x -= 5;
        }
    }
    gfx_SetColor(card->palette_base_index);
    gfx_FillRectangle_NoClip(x + 1, y + 1, CARD_WIDTH + 2, CARD_HEIGHT + 2);
    if (card->playstate > 0 || selected) {
        gfx_TransparentSprite_NoClip(cdata->img, x + 2, y + 2);

        gfx_TransparentSprite_NoClip(numtiles[cdata->up], x + (2 + 8 - 3), y + (2 + 0));
        gfx_TransparentSprite_NoClip(numtiles[cdata->right], x + (2 + 16 - 3 - 3), y + (2 + 8));
        gfx_TransparentSprite_NoClip(numtiles[cdata->down], x + (2 + 8 - 3), y + (2 + 16));
        gfx_TransparentSprite_NoClip(numtiles[cdata->left], x + 2, y + (2 + 8));

        if (cdata->element) {
            gfx_TransparentSprite_NoClip((gfx_sprite_t *)elemdat[cdata->element], x + 44, y + 2);
        }
    } else {
        gfx_TransparentSprite_NoClip(cardback, x + 2, y + 2);
    }

    if (selected) {
        gfx_SetColor(CARD_SEL_FG);
        gfx_Rectangle_NoClip(x + 1, y + 1, CARD_WIDTH + 2, CARD_HEIGHT + 2);
    } else {
        gfx_SetColor(INTERNAL_BLACK_COLOR);
    }
    gfx_Rectangle_NoClip(x, y, CARD_WIDTH + 4, CARD_HEIGHT + 4);
}

typedef struct {
    uint8_t direction;
    uint8_t neighbor_gridpos;
    uint8_t placed_value;
    uint8_t opposing_value;
    bool touches_wall;
    tricard_card_slot_t *neighbor;
} adjacent_card_info_t;

typedef struct {
    tricard_card_slot_t *cards[4];
    uint8_t count;
} capture_result_t;

typedef struct {
    tricard_runtime_card_t card;
    uint8_t gridpos;
    uint8_t isplayer1;
    uint8_t playstate;
} ai_sim_card_t;

typedef struct {
    ai_sim_card_t cards[GAME_CARD_SLOT_COUNT];
    uint8_t elementgrid[9];
} ai_sim_state_t;

typedef struct {
    uint8_t card_index;
    uint8_t gridpos;
} ai_move_t;

typedef struct {
    int8_t card_indices[GAME_CARD_SLOT_COUNT];
    uint8_t count;
} ai_capture_result_t;

typedef struct {
    uint8_t direction;
    uint8_t neighbor_gridpos;
    uint8_t placed_value;
    uint8_t opposing_value;
    bool touches_wall;
    int8_t neighbor_index;
} ai_adjacent_card_info_t;

static const uint8_t cardinal_directions[] = {DIR_UP, DIR_RIGHT, DIR_DOWN, DIR_LEFT};

static uint8_t clampcardvalue(int value) {
    if (value < 1) {
        return 1;
    }
    if (value > 10) {
        return 10;
    }
    return (uint8_t)value;
}

static uint8_t getcardsidevalue(const tricard_runtime_card_t *card, uint8_t direction) {
    switch (direction) {
        case DIR_UP:
            return card->up;
        case DIR_RIGHT:
            return card->right;
        case DIR_DOWN:
            return card->down;
        case DIR_LEFT:
            return card->left;
        default:
            return 0;
    }
}

static uint8_t getoppositedirection(uint8_t direction) {
    switch (direction) {
        case DIR_UP:
            return DIR_DOWN;
        case DIR_RIGHT:
            return DIR_LEFT;
        case DIR_DOWN:
            return DIR_UP;
        case DIR_LEFT:
            return DIR_RIGHT;
        default:
            return DIR_NONE;
    }
}

static uint8_t getneighborgridpos(uint8_t gridpos, uint8_t direction) {
    switch (direction) {
        case DIR_UP:
            return (gridpos > 3) ? (uint8_t)(gridpos - 3) : 0;
        case DIR_RIGHT:
            return (((gridpos - 1) % 3) < 2) ? (uint8_t)(gridpos + 1) : 0;
        case DIR_DOWN:
            return (gridpos < 7) ? (uint8_t)(gridpos + 3) : 0;
        case DIR_LEFT:
            return ((gridpos - 1) % 3) ? (uint8_t)(gridpos - 1) : 0;
        default:
            return 0;
    }
}

static uint8_t getdirectionbetween(uint8_t source_gridpos, uint8_t target_gridpos) {
    if (source_gridpos > 3 && target_gridpos == (uint8_t)(source_gridpos - 3)) {
        return DIR_UP;
    }
    if ((((source_gridpos - 1) % 3) < 2) && target_gridpos == (uint8_t)(source_gridpos + 1)) {
        return DIR_RIGHT;
    }
    if (source_gridpos < 7 && target_gridpos == (uint8_t)(source_gridpos + 3)) {
        return DIR_DOWN;
    }
    if (((source_gridpos - 1) % 3) && target_gridpos == (uint8_t)(source_gridpos - 1)) {
        return DIR_LEFT;
    }
    return DIR_NONE;
}

static void initcaptureresult(capture_result_t *result) {
    result->count = 0;
}

static bool captureresultcontains(const capture_result_t *result, tricard_card_slot_t *card) {
    uint8_t i;

    for (i = 0; i < result->count; i++) {
        if (result->cards[i] == card) {
            return true;
        }
    }
    return false;
}

static void addcaptureresult(capture_result_t *result, tricard_card_slot_t *card) {
    if (card == NULL || captureresultcontains(result, card) || result->count >= 4) {
        return;
    }
    result->cards[result->count++] = card;
}

static void collectadjacentcards(tricard_card_slot_t *placed_card, adjacent_card_info_t adjacent[4]) {
    uint8_t i;
    uint8_t direction;
    uint8_t neighbor_gridpos;

    for (i = 0; i < 4; i++) {
        direction = cardinal_directions[i];
        neighbor_gridpos = getneighborgridpos(placed_card->gridpos, direction);
        adjacent[i].direction = direction;
        adjacent[i].neighbor_gridpos = neighbor_gridpos;
        adjacent[i].placed_value = getcardsidevalue(&placed_card->card, direction);
        adjacent[i].opposing_value = 0;
        adjacent[i].touches_wall = (neighbor_gridpos == 0);
        adjacent[i].neighbor = NULL;
        if (!neighbor_gridpos) {
            adjacent[i].opposing_value = 10;
            continue;
        }
        adjacent[i].neighbor = getcardongrid(neighbor_gridpos);
        if (adjacent[i].neighbor != NULL) {
            adjacent[i].opposing_value = getcardsidevalue(
                &adjacent[i].neighbor->card,
                getoppositedirection(direction)
            );
        }
    }
}

static void applyelementalmodifier(tricard_card_slot_t *card) {
    int modifier;
    uint8_t element;

    if (!(ruleFlags & RULE_ELEMENTAL) || !card->gridpos || card->gridpos > 9) {
        return;
    }
    element = elementgrid[card->gridpos - 1];
    if (!element) {
        return;
    }

    modifier = (card->card.element == element) ? 1 : -1;
    card->card.up = clampcardvalue(card->card.up + modifier);
    card->card.right = clampcardvalue(card->card.right + modifier);
    card->card.down = clampcardvalue(card->card.down + modifier);
    card->card.left = clampcardvalue(card->card.left + modifier);
}

static bool capturecard(tricard_card_slot_t *source_card, tricard_card_slot_t *target_card) {
    uint16_t source_color;

    if (source_card == NULL || target_card == NULL || source_card->isplayer1 == target_card->isplayer1) {
        return false;
    }

    source_color = getcardtransitioncolor(target_card);
    target_card->isplayer1 = source_card->isplayer1;
    startcardtransition(target_card, source_color, getplayerbgcolor(target_card->isplayer1));
    return true;
}

static bool captureandrecord(
    tricard_card_slot_t *source_card,
    tricard_card_slot_t *target_card,
    capture_result_t *result
) {
    if (!capturecard(source_card, target_card)) {
        return false;
    }
    addcaptureresult(result, target_card);
    return true;
}

static void resolvenormalcaptures(tricard_card_slot_t *source_card, capture_result_t *result) {
    adjacent_card_info_t adjacent[4];
    uint8_t i;

    collectadjacentcards(source_card, adjacent);
    for (i = 0; i < 4; i++) {
        if (adjacent[i].neighbor == NULL) {
            continue;
        }
        if (adjacent[i].placed_value > adjacent[i].opposing_value) {
            captureandrecord(source_card, adjacent[i].neighbor, result);
        }
    }
}

static void resolvesamecaptures(tricard_card_slot_t *placed_card, capture_result_t *result) {
    adjacent_card_info_t adjacent[4];
    uint8_t i;
    uint8_t match_count;
    bool has_opponent_match;

    if (!(ruleFlags & RULE_SAME)) {
        return;
    }

    collectadjacentcards(placed_card, adjacent);
    match_count = 0;
    has_opponent_match = false;
    for (i = 0; i < 4; i++) {
        if (adjacent[i].neighbor != NULL) {
            if (adjacent[i].placed_value == adjacent[i].opposing_value) {
                match_count++;
                if (adjacent[i].neighbor->isplayer1 != placed_card->isplayer1) {
                    has_opponent_match = true;
                }
            }
            continue;
        }
        if ((ruleFlags & RULE_SAMEWALL) && adjacent[i].touches_wall && adjacent[i].placed_value == 10) {
            match_count++;
        }
    }

    if (match_count < 2 || !has_opponent_match) {
        return;
    }

    for (i = 0; i < 4; i++) {
        if (adjacent[i].neighbor == NULL) {
            continue;
        }
        if (adjacent[i].placed_value == adjacent[i].opposing_value) {
            captureandrecord(placed_card, adjacent[i].neighbor, result);
        }
    }
}

static void resolvepluscaptures(tricard_card_slot_t *placed_card, capture_result_t *result) {
    adjacent_card_info_t adjacent[4];
    bool qualifying_matches[4] = {false, false, false, false};
    uint8_t i;
    uint8_t j;

    if (!(ruleFlags & RULE_PLUS)) {
        return;
    }

    collectadjacentcards(placed_card, adjacent);
    for (i = 0; i < 4; i++) {
        if (adjacent[i].neighbor == NULL) {
            continue;
        }
        for (j = (uint8_t)(i + 1); j < 4; j++) {
            if (adjacent[j].neighbor == NULL) {
                continue;
            }
            if ((adjacent[i].placed_value + adjacent[i].opposing_value)
                == (adjacent[j].placed_value + adjacent[j].opposing_value)) {
                qualifying_matches[i] = true;
                qualifying_matches[j] = true;
            }
        }
    }

    for (i = 0; i < 4; i++) {
        if (!qualifying_matches[i] || adjacent[i].neighbor == NULL) {
            continue;
        }
        captureandrecord(placed_card, adjacent[i].neighbor, result);
    }
}

static void resolvecombocaptures(const capture_result_t *initial_result) {
    tricard_card_slot_t *capture_queue[GAME_CARD_SLOT_COUNT];
    capture_result_t combo_result;
    uint8_t head;
    uint8_t tail;
    uint8_t i;

    head = 0;
    tail = 0;
    for (i = 0; i < initial_result->count && tail < GAME_CARD_SLOT_COUNT; i++) {
        capture_queue[tail++] = initial_result->cards[i];
    }

    while (head < tail) {
        initcaptureresult(&combo_result);
        resolvenormalcaptures(capture_queue[head++], &combo_result);
        for (i = 0; i < combo_result.count && tail < GAME_CARD_SLOT_COUNT; i++) {
            capture_queue[tail++] = combo_result.cards[i];
        }
    }
}

uint8_t selectfromhand(uint8_t direction) {
    uint8_t i, found;
    tricard_card_slot_t *curcard;

    found = 255;
    for (i = 0; i < GAME_CARD_SLOT_COUNT; i++) {
        curcard = cardbuf[i];
        if ((curcard->isplayer1 == curplayer) || (curcard->playstate > 1)) {
            continue;
        }
        if (direction == DIR_UP) {
            if (i >= selcard) {
                break;
            }
            found = i;
        } else if (direction == DIR_DOWN) {
            found = i;
            if (i > selcard) {
                break;
            }
        } else {
            found = i;
            if (i >= selcard) {
                break;
            }
        }
    }
    return found;
}

tricard_card_slot_t *getcardongrid(uint8_t gridpos) {
    uint8_t i;
    tricard_card_slot_t *card;

    for (i = 0; i < GAME_CARD_SLOT_COUNT; i++) {
        card = cardbuf[i];
        if (card->gridpos == gridpos) {
            return card;
        }
    }
    return NULL;
}

void cardfight(uint8_t pidx, uint8_t eidx) {
    uint8_t direction;
    tricard_card_slot_t *pcard, *ecard;

    pcard = getcardongrid(pidx);
    ecard = getcardongrid(eidx);
    if (pcard == NULL || ecard == NULL) {
        return;
    }

    direction = getdirectionbetween(pidx, eidx);
    if (direction == DIR_NONE) {
        return;
    }
    if (getcardsidevalue(&pcard->card, direction)
        > getcardsidevalue(&ecard->card, getoppositedirection(direction))) {
        capturecard(pcard, ecard);
    }
}

void resolvecardplacement(uint8_t gridpos) {
    capture_result_t special_captures;
    capture_result_t normal_captures;
    tricard_card_slot_t *placed_card;

    placed_card = getcardongrid(gridpos);
    if (placed_card == NULL) {
        return;
    }

    applyelementalmodifier(placed_card);
    initcaptureresult(&special_captures);
    resolvesamecaptures(placed_card, &special_captures);
    resolvepluscaptures(placed_card, &special_captures);

    initcaptureresult(&normal_captures);
    resolvenormalcaptures(placed_card, &normal_captures);

    if ((ruleFlags & RULE_COMBO) && special_captures.count > 0) {
        resolvecombocaptures(&special_captures);
    }
}

static void initaistate(ai_sim_state_t *state) {
    uint8_t i;

    for (i = 0; i < GAME_CARD_SLOT_COUNT; i++) {
        state->cards[i].card = cardbuf[i]->card;
        state->cards[i].gridpos = cardbuf[i]->gridpos;
        state->cards[i].isplayer1 = cardbuf[i]->isplayer1;
        state->cards[i].playstate = cardbuf[i]->playstate;
    }
    memcpy(state->elementgrid, elementgrid, sizeof state->elementgrid);
}

static uint8_t getturnownerflag(uint8_t turn) {
    return (turn == 0) ? 1 : 0;
}

static bool aicardisinhand(const ai_sim_card_t *card, uint8_t owner_flag) {
    return (card->gridpos > 9) && (card->playstate < 2) && (card->isplayer1 == owner_flag);
}

static bool aicardisonboard(const ai_sim_card_t *card) {
    return card->gridpos > 0 && card->gridpos < 10;
}

static int8_t aigetcardindexongrid(const ai_sim_state_t *state, uint8_t gridpos) {
    uint8_t i;

    for (i = 0; i < GAME_CARD_SLOT_COUNT; i++) {
        if (state->cards[i].gridpos == gridpos) {
            return (int8_t)i;
        }
    }
    return -1;
}

static uint8_t aiemptyboardcount(const ai_sim_state_t *state) {
    uint8_t gridpos;
    uint8_t empty_count;

    empty_count = 0;
    for (gridpos = 1; gridpos <= 9; gridpos++) {
        if (aigetcardindexongrid(state, gridpos) < 0) {
            empty_count++;
        }
    }
    return empty_count;
}

static void initaicaptureresult(ai_capture_result_t *result) {
    result->count = 0;
}

static bool aicaptureresultcontains(const ai_capture_result_t *result, int8_t card_index) {
    uint8_t i;

    for (i = 0; i < result->count; i++) {
        if (result->card_indices[i] == card_index) {
            return true;
        }
    }
    return false;
}

static void addaicaptureresult(ai_capture_result_t *result, int8_t card_index) {
    if (card_index < 0
        || aicaptureresultcontains(result, card_index)
        || result->count >= GAME_CARD_SLOT_COUNT) {
        return;
    }
    result->card_indices[result->count++] = card_index;
}

static void aicollectadjacentcards(
    ai_sim_state_t *state,
    uint8_t placed_index,
    ai_adjacent_card_info_t adjacent[4]
) {
    uint8_t i;
    uint8_t direction;
    uint8_t neighbor_gridpos;
    ai_sim_card_t *placed_card;

    placed_card = &state->cards[placed_index];
    for (i = 0; i < 4; i++) {
        direction = cardinal_directions[i];
        neighbor_gridpos = getneighborgridpos(placed_card->gridpos, direction);
        adjacent[i].direction = direction;
        adjacent[i].neighbor_gridpos = neighbor_gridpos;
        adjacent[i].placed_value = getcardsidevalue(&placed_card->card, direction);
        adjacent[i].opposing_value = 0;
        adjacent[i].touches_wall = (neighbor_gridpos == 0);
        adjacent[i].neighbor_index = -1;
        if (!neighbor_gridpos) {
            adjacent[i].opposing_value = 10;
            continue;
        }
        adjacent[i].neighbor_index = aigetcardindexongrid(state, neighbor_gridpos);
        if (adjacent[i].neighbor_index >= 0) {
            adjacent[i].opposing_value = getcardsidevalue(
                &state->cards[(uint8_t)adjacent[i].neighbor_index].card,
                getoppositedirection(direction)
            );
        }
    }
}

static void aiapplyelementalmodifier(ai_sim_state_t *state, ai_sim_card_t *card) {
    int modifier;
    uint8_t element;

    if (!(ruleFlags & RULE_ELEMENTAL) || !aicardisonboard(card)) {
        return;
    }
    element = state->elementgrid[card->gridpos - 1];
    if (!element) {
        return;
    }

    modifier = (card->card.element == element) ? 1 : -1;
    card->card.up = clampcardvalue(card->card.up + modifier);
    card->card.right = clampcardvalue(card->card.right + modifier);
    card->card.down = clampcardvalue(card->card.down + modifier);
    card->card.left = clampcardvalue(card->card.left + modifier);
}

static bool aicapturecard(ai_sim_card_t *source_card, ai_sim_card_t *target_card) {
    if (source_card == NULL || target_card == NULL || source_card->isplayer1 == target_card->isplayer1) {
        return false;
    }

    target_card->isplayer1 = source_card->isplayer1;
    return true;
}

static bool aicaptureandrecord(
    ai_sim_state_t *state,
    uint8_t source_index,
    int8_t target_index,
    ai_capture_result_t *result
) {
    if (target_index < 0) {
        return false;
    }
    if (!aicapturecard(&state->cards[source_index], &state->cards[(uint8_t)target_index])) {
        return false;
    }
    addaicaptureresult(result, target_index);
    return true;
}

static void airesolvenormalcaptures(
    ai_sim_state_t *state,
    uint8_t source_index,
    ai_capture_result_t *result
) {
    ai_adjacent_card_info_t adjacent[4];
    uint8_t i;

    aicollectadjacentcards(state, source_index, adjacent);
    for (i = 0; i < 4; i++) {
        if (adjacent[i].neighbor_index < 0) {
            continue;
        }
        if (adjacent[i].placed_value > adjacent[i].opposing_value) {
            aicaptureandrecord(state, source_index, adjacent[i].neighbor_index, result);
        }
    }
}

static void airesolvesamecaptures(
    ai_sim_state_t *state,
    uint8_t placed_index,
    ai_capture_result_t *result
) {
    ai_adjacent_card_info_t adjacent[4];
    uint8_t i;
    uint8_t match_count;
    bool has_opponent_match;

    if (!(ruleFlags & RULE_SAME)) {
        return;
    }

    aicollectadjacentcards(state, placed_index, adjacent);
    match_count = 0;
    has_opponent_match = false;
    for (i = 0; i < 4; i++) {
        if (adjacent[i].neighbor_index >= 0) {
            if (adjacent[i].placed_value == adjacent[i].opposing_value) {
                match_count++;
                if (state->cards[(uint8_t)adjacent[i].neighbor_index].isplayer1
                    != state->cards[placed_index].isplayer1) {
                    has_opponent_match = true;
                }
            }
            continue;
        }
        if ((ruleFlags & RULE_SAMEWALL) && adjacent[i].touches_wall && adjacent[i].placed_value == 10) {
            match_count++;
        }
    }

    if (match_count < 2 || !has_opponent_match) {
        return;
    }

    for (i = 0; i < 4; i++) {
        if (adjacent[i].neighbor_index < 0) {
            continue;
        }
        if (adjacent[i].placed_value == adjacent[i].opposing_value) {
            aicaptureandrecord(state, placed_index, adjacent[i].neighbor_index, result);
        }
    }
}

static void airesolvepluscaptures(
    ai_sim_state_t *state,
    uint8_t placed_index,
    ai_capture_result_t *result
) {
    ai_adjacent_card_info_t adjacent[4];
    bool qualifying_matches[4] = {false, false, false, false};
    uint8_t i;
    uint8_t j;

    if (!(ruleFlags & RULE_PLUS)) {
        return;
    }

    aicollectadjacentcards(state, placed_index, adjacent);
    for (i = 0; i < 4; i++) {
        if (adjacent[i].neighbor_index < 0) {
            continue;
        }
        for (j = (uint8_t)(i + 1); j < 4; j++) {
            if (adjacent[j].neighbor_index < 0) {
                continue;
            }
            if ((adjacent[i].placed_value + adjacent[i].opposing_value)
                == (adjacent[j].placed_value + adjacent[j].opposing_value)) {
                qualifying_matches[i] = true;
                qualifying_matches[j] = true;
            }
        }
    }

    for (i = 0; i < 4; i++) {
        if (!qualifying_matches[i] || adjacent[i].neighbor_index < 0) {
            continue;
        }
        aicaptureandrecord(state, placed_index, adjacent[i].neighbor_index, result);
    }
}

static void airesolvecombocaptures(ai_sim_state_t *state, const ai_capture_result_t *initial_result) {
    int8_t capture_queue[GAME_CARD_SLOT_COUNT];
    ai_capture_result_t combo_result;
    uint8_t head;
    uint8_t tail;
    uint8_t i;

    head = 0;
    tail = 0;
    for (i = 0; i < initial_result->count && tail < GAME_CARD_SLOT_COUNT; i++) {
        capture_queue[tail++] = initial_result->card_indices[i];
    }

    while (head < tail) {
        initaicaptureresult(&combo_result);
        airesolvenormalcaptures(state, (uint8_t)capture_queue[head++], &combo_result);
        for (i = 0; i < combo_result.count && tail < GAME_CARD_SLOT_COUNT; i++) {
            capture_queue[tail++] = combo_result.card_indices[i];
        }
    }
}

static void airesolvecardplacement(ai_sim_state_t *state, uint8_t placed_index) {
    ai_capture_result_t special_captures;
    ai_capture_result_t normal_captures;

    aiapplyelementalmodifier(state, &state->cards[placed_index]);
    initaicaptureresult(&special_captures);
    airesolvesamecaptures(state, placed_index, &special_captures);
    airesolvepluscaptures(state, placed_index, &special_captures);

    initaicaptureresult(&normal_captures);
    airesolvenormalcaptures(state, placed_index, &normal_captures);

    if ((ruleFlags & RULE_COMBO) && special_captures.count > 0) {
        airesolvecombocaptures(state, &special_captures);
    }
}

static void aiplacemove(ai_sim_state_t *state, uint8_t card_index, uint8_t gridpos) {
    state->cards[card_index].gridpos = gridpos;
    state->cards[card_index].playstate = 2;
    airesolvecardplacement(state, card_index);
}

static int getboardpositionscore(uint8_t gridpos) {
    if (gridpos == 5) {
        return 10;
    }
    if (gridpos == 1 || gridpos == 3 || gridpos == 7 || gridpos == 9) {
        return 6;
    }
    return 4;
}

static int aievaluatestate(const ai_sim_state_t *state, bool can_read_player1_hand) {
    uint8_t i;
    uint8_t direction;
    uint8_t neighbor_gridpos;
    uint8_t side_value;
    uint8_t open_side_count;
    int player2_owned;
    int player1_owned;
    int ownership_diff;
    int hand_score;
    int board_score;
    int score;

    player2_owned = 0;
    player1_owned = 0;
    score = 0;
    for (i = 0; i < GAME_CARD_SLOT_COUNT; i++) {
        if (state->cards[i].isplayer1) {
            player1_owned++;
        } else {
            player2_owned++;
        }
    }
    ownership_diff = player2_owned - player1_owned;
    if (!aiemptyboardcount(state)) {
        if (ownership_diff > 0) {
            return AI_SCORE_WIN + ownership_diff * 1000;
        }
        if (ownership_diff < 0) {
            return -AI_SCORE_WIN + ownership_diff * 1000;
        }
        return 0;
    }

    score += ownership_diff * 400;
    for (i = 0; i < GAME_CARD_SLOT_COUNT; i++) {
        if (aicardisinhand(&state->cards[i], 0)) {
            hand_score = state->cards[i].card.rank * 8
                + (state->cards[i].card.up + state->cards[i].card.right
                    + state->cards[i].card.down + state->cards[i].card.left) * 3;
            score += hand_score;
            continue;
        }
        if (can_read_player1_hand && aicardisinhand(&state->cards[i], 1)) {
            hand_score = state->cards[i].card.rank * 8
                + (state->cards[i].card.up + state->cards[i].card.right
                    + state->cards[i].card.down + state->cards[i].card.left) * 3;
            score -= hand_score;
            continue;
        }
        if (!aicardisonboard(&state->cards[i])) {
            continue;
        }

        open_side_count = 0;
        board_score = getboardpositionscore(state->cards[i].gridpos);
        for (direction = 0; direction < 4; direction++) {
            neighbor_gridpos = getneighborgridpos(state->cards[i].gridpos, cardinal_directions[direction]);
            if (!neighbor_gridpos || aigetcardindexongrid(state, neighbor_gridpos) >= 0) {
                continue;
            }
            open_side_count++;
            side_value = getcardsidevalue(&state->cards[i].card, cardinal_directions[direction]);
            board_score += side_value * 6;
        }
        board_score += (4 - open_side_count) * 18;
        if (state->cards[i].isplayer1) {
            score -= board_score;
        } else {
            score += board_score;
        }
    }
    return score;
}

static uint8_t gethardsearchdepth(const ai_sim_state_t *state) {
    uint8_t empty_count;

    empty_count = aiemptyboardcount(state);
    if (empty_count > 6) {
        return 2;
    }
    if (empty_count > 4) {
        return 3;
    }
    return empty_count;
}

static int aiminimax(
    const ai_sim_state_t *state,
    uint8_t turn,
    uint8_t depth,
    bool can_read_player1_hand,
    int alpha,
    int beta
) {
    uint8_t i;
    uint8_t gridpos;
    uint8_t owner_flag;
    bool found_move;
    int best_score;

    if (depth == 0 || !aiemptyboardcount(state)) {
        return aievaluatestate(state, can_read_player1_hand);
    }

    /* Closed hands stop at Player 1 nodes so the AI does not inspect hidden stats. */
    if (turn == 0 && !can_read_player1_hand) {
        return aievaluatestate(state, false);
    }

    owner_flag = getturnownerflag(turn);
    found_move = false;
    best_score = (turn == 1) ? AI_SCORE_MIN : AI_SCORE_MAX;
    for (i = 0; i < GAME_CARD_SLOT_COUNT; i++) {
        ai_sim_state_t next_state;
        int score;

        if (!aicardisinhand(&state->cards[i], owner_flag)) {
            continue;
        }
        for (gridpos = 1; gridpos <= 9; gridpos++) {
            if (aigetcardindexongrid(state, gridpos) >= 0) {
                continue;
            }

            found_move = true;
            next_state = *state;
            aiplacemove(&next_state, i, gridpos);
            score = aiminimax(&next_state, (uint8_t)!turn, (uint8_t)(depth - 1), can_read_player1_hand, alpha, beta);
            if (turn == 1) {
                if (score > best_score) {
                    best_score = score;
                }
                if (best_score > alpha) {
                    alpha = best_score;
                }
            } else {
                if (score < best_score) {
                    best_score = score;
                }
                if (best_score < beta) {
                    beta = best_score;
                }
            }
            if (alpha >= beta) {
                return best_score;
            }
        }
    }

    if (!found_move) {
        return aievaluatestate(state, can_read_player1_hand);
    }
    return best_score;
}

static bool selecteasyplayer2move(const ai_sim_state_t *state, ai_move_t *out_move) {
    uint8_t hand_cards[5];
    uint8_t empty_positions[9];
    uint8_t hand_count;
    uint8_t empty_count;
    uint8_t i;
    uint8_t gridpos;

    hand_count = 0;
    empty_count = 0;
    for (i = 0; i < GAME_CARD_SLOT_COUNT; i++) {
        if (aicardisinhand(&state->cards[i], 0) && hand_count < 5) {
            hand_cards[hand_count++] = i;
        }
    }
    for (gridpos = 1; gridpos <= 9; gridpos++) {
        if (aigetcardindexongrid(state, gridpos) < 0 && empty_count < 9) {
            empty_positions[empty_count++] = gridpos;
        }
    }
    if (!hand_count || !empty_count) {
        return false;
    }

    out_move->card_index = hand_cards[randInt(0, hand_count - 1)];
    out_move->gridpos = empty_positions[randInt(0, empty_count - 1)];
    return true;
}

static bool selecthardplayer2move(const ai_sim_state_t *state, ai_move_t *out_move) {
    ai_move_t best_moves[GAME_CARD_SLOT_COUNT * 9];
    bool can_read_player1_hand;
    uint8_t search_depth;
    uint8_t best_move_count;
    uint8_t i;
    uint8_t gridpos;
    bool found_move;
    int best_score;

    can_read_player1_hand = (ruleFlags & RULE_OPEN) != 0;
    search_depth = gethardsearchdepth(state);
    best_move_count = 0;
    best_score = AI_SCORE_MIN;
    found_move = false;

    for (i = 0; i < GAME_CARD_SLOT_COUNT; i++) {
        ai_sim_state_t next_state;
        int score;

        if (!aicardisinhand(&state->cards[i], 0)) {
            continue;
        }
        for (gridpos = 1; gridpos <= 9; gridpos++) {
            if (aigetcardindexongrid(state, gridpos) >= 0) {
                continue;
            }

            next_state = *state;
            aiplacemove(&next_state, i, gridpos);
            if (can_read_player1_hand && search_depth > 1) {
                score = aiminimax(
                    &next_state,
                    0,
                    (uint8_t)(search_depth - 1),
                    true,
                    AI_SCORE_MIN,
                    AI_SCORE_MAX
                );
            } else {
                score = aievaluatestate(&next_state, can_read_player1_hand);
            }

            found_move = true;
            if (score > best_score) {
                best_score = score;
                best_move_count = 0;
            }
            if (score == best_score && best_move_count < (GAME_CARD_SLOT_COUNT * 9)) {
                best_moves[best_move_count].card_index = i;
                best_moves[best_move_count].gridpos = gridpos;
                best_move_count++;
            }
        }
    }

    if (!found_move || !best_move_count) {
        return false;
    }

    *out_move = best_moves[randInt(0, best_move_count - 1)];
    return true;
}

static bool selectmediumplayer2move(const ai_sim_state_t *state, ai_move_t *out_move) {
    if (!randInt(0, 1)) {
        return selecteasyplayer2move(state, out_move);
    }
    return selecthardplayer2move(state, out_move);
}

bool getplayer2aimove(uint8_t *out_card_slot, uint8_t *out_gridpos) {
    ai_sim_state_t state;
    ai_move_t move;
    bool has_move;

    if (out_card_slot == NULL || out_gridpos == NULL) {
        return false;
    }

    initaistate(&state);
    switch (player2_ai_difficulty) {
        case PLAYER2_AI_EASY:
            has_move = selecteasyplayer2move(&state, &move);
            break;
        case PLAYER2_AI_MEDIUM:
            has_move = selectmediumplayer2move(&state, &move);
            break;
        case PLAYER2_AI_HARD:
            has_move = selecthardplayer2move(&state, &move);
            break;
        default:
            return false;
    }

    if (!has_move) {
        has_move = selecteasyplayer2move(&state, &move);
    }
    if (!has_move) {
        return false;
    }
    *out_card_slot = move.card_index;
    *out_gridpos = move.gridpos;
    return true;
}

void initGame(const uint8_t *packptr) {
    uint16_t card_count;
    uint8_t i;

    card_count = getpackcardcount(packptr);
    if (!card_count) {
        gamemode = GM_TITLE;
        return;
    }

    if (ruleFlags & RULE_RANDOM) {
        for (i = 0; i < GAME_CARD_SLOT_COUNT; i++) {
            if (!loadcardslot(packptr, randInt(0, card_count - 1), i)) {
                sudden_death_active = false;
                ai_thinking = false;
                gamemode = GM_TITLE;
                return;
            }
            cardbuf[i]->gridpos = i + 10;
            cardbuf[i]->isplayer1 = (i < 5) ? 1 : 0;
            cardbuf[i]->x = posarr[(i + 10) * 2];
            cardbuf[i]->y = posarr[(i + 10) * 2 + 1];
            cardbuf[i]->playstate = (ruleFlags & RULE_OPEN) ? 1 : 0;
            resetcardtransition(cardbuf[i]);
        }
    } else {
        sudden_death_active = false;
        ai_thinking = false;
        gamemode = GM_TITLE;
        return;
    }

    sudden_death_active = false;
    ai_thinking = false;
    memset(elementgrid, 0, sizeof elementgrid);
    if (ruleFlags & RULE_ELEMENTAL) {
        for (i = 0; i < 9; i++) {
            if (!randInt(0, 4)) {
                elementgrid[i] = randInt(0, 7) + 1;
            }
        }
    }
    opengamebackground();
    match_start_player = randInt(0, 1);
    curplayer = match_start_player;
    selcard = 0;
    selcard = selectfromhand(DIR_NONE);
    keywait();
}

bool startsuddendeathround(void) {
    uint8_t i;
    uint8_t player1_gridpos;
    uint8_t player2_gridpos;

    player1_gridpos = 10;
    player2_gridpos = 15;
    for (i = 0; i < GAME_CARD_SLOT_COUNT; i++) {
        tricard_card_slot_t *card;
        uint8_t hand_gridpos;

        card = cardbuf[i];
        if (card->isplayer1) {
            if (player1_gridpos > 14) {
                sudden_death_active = false;
                ai_thinking = false;
                return false;
            }
            hand_gridpos = player1_gridpos++;
        } else {
            if (player2_gridpos > 19) {
                sudden_death_active = false;
                ai_thinking = false;
                return false;
            }
            hand_gridpos = player2_gridpos++;
        }

        card->gridpos = hand_gridpos;
        card->x = posarr[hand_gridpos * 2];
        card->y = posarr[hand_gridpos * 2 + 1];
        card->playstate = (ruleFlags & RULE_OPEN) ? 1 : 0;
        resetcardtransition(card);
    }

    if (player1_gridpos != 15 || player2_gridpos != 20) {
        sudden_death_active = false;
        ai_thinking = false;
        return false;
    }

    sudden_death_active = true;
    ai_thinking = false;
    memset(elementgrid, 0, sizeof elementgrid);
    if (ruleFlags & RULE_ELEMENTAL) {
        for (i = 0; i < 9; i++) {
            if (!randInt(0, 4)) {
                elementgrid[i] = randInt(0, 7) + 1;
            }
        }
    }
    opengamebackground();
    curplayer = match_start_player;
    selcard = 0;
    selcard = selectfromhand(DIR_NONE);
    return true;
}

void redrawboard(void) {
    uint8_t i, t;

    updatecardtransitions();
    drawgamebackground();
    drawgamehud();

    for (i = 0; i < 9; i++) {
        t = elementgrid[i];
        if (t) {
            gfx_TransparentSprite_NoClip(
                (gfx_sprite_t *)elemdat[t],
                posarr[(i + 1) * 2] + (GRIDV / 2 - 4),
                posarr[(i + 1) * 2 + 1] + (GRIDV / 2 - 4)
            );
        }
    }

    gfx_SetColor(INTERNAL_BLACK_COLOR);
    for (i = 2; i < 20; i += 2) {
        gfx_Rectangle_NoClip(posarr[i], posarr[i + 1], CARD_WIDTH + 4, CARD_HEIGHT + 4);
    }

    for (i = 0; i < GAME_CARD_SLOT_COUNT; i++) {
        if (i != selcard) {
            drawcard(cardbuf[i], false);
        }
    }
    if (selcard < GAME_CARD_SLOT_COUNT) {
        drawcard(cardbuf[selcard], true);
    }
}
