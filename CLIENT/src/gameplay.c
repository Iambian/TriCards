#include <string.h>

#include "tricards.h"

#define CARD_CAPTURE_FADE_TO_WHITE_FRAMES 8
#define CARD_CAPTURE_FADE_TO_TARGET_FRAMES 8
#define CARD_CAPTURE_TOTAL_FRAMES \
    (CARD_CAPTURE_FADE_TO_WHITE_FRAMES + CARD_CAPTURE_FADE_TO_TARGET_FRAMES)

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
    if (issuddendeath == 1) {
        issuddendeath++;
    }
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

void initGame(uint8_t *packptr) {
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
                gamemode = GM_TITLE;
                return;
            }
            cardbuf[i]->gridpos = i + 10;
            cardbuf[i]->isplayer1 = (i < 5) ? 1 : 0;
            cardbuf[i]->x = posarr[(i + 10) * 2];
            cardbuf[i]->y = posarr[(i + 10) * 2 + 1];
            cardbuf[i]->playstate = 0;
        }
    } else {
        gamemode = GM_TITLE;
    }
    if (ruleFlags & RULE_OPEN) {
        for (i = 0; i < GAME_CARD_SLOT_COUNT; i++) {
            cardbuf[i]->playstate = 1;
        }
    }

    memset(elementgrid, 0, sizeof elementgrid);
    if (ruleFlags & RULE_ELEMENTAL) {
        for (i = 0; i < 9; i++) {
            if (!randInt(0, 4)) {
                elementgrid[i] = randInt(0, 7) + 1;
            }
        }
    }
    curplayer = 0;
    selcard = 0;
    selcard = selectfromhand(DIR_NONE);
    keywait();
}

void redrawboard(void) {
    uint8_t i, t;

    updatecardtransitions();
    gfx_FillScreen(GAMEBOARD_BG);

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
