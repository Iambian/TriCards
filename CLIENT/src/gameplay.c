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

    for (i = 0; i < CARD_SLOT_COUNT; i++) {
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

uint8_t selectfromhand(uint8_t direction) {
    uint8_t i, found;
    tricard_card_slot_t *curcard;

    found = 255;
    for (i = 0; i < 10; i++) {
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

    for (i = 0; i < 10; i++) {
        card = cardbuf[i];
        if (card->gridpos == gridpos) {
            return card;
        }
    }
    return NULL;
}

void cardfight(uint8_t pidx, uint8_t eidx) {
    uint8_t i;
    int8_t prank, erank;
    tricard_card_slot_t *pcard, *ecard;

    pcard = getcardongrid(pidx);
    if ((ecard = getcardongrid(eidx)) == NULL) {
        return;
    }
    if (pcard->isplayer1 == ecard->isplayer1) {
        return;
    }
    i = eidx - pidx;
    if (i == 253) {
        prank = pcard->card.up;
        erank = ecard->card.down;
    } else if (i == 255) {
        prank = pcard->card.left;
        erank = ecard->card.right;
    } else if (i == 1) {
        prank = pcard->card.right;
        erank = ecard->card.left;
    } else if (i == 3) {
        prank = pcard->card.down;
        erank = ecard->card.up;
    } else {
        prank = 0;
        erank = 0;
    }

    if (prank > erank) {
        uint16_t source_color;

        source_color = getcardtransitioncolor(ecard);
        ecard->isplayer1 = pcard->isplayer1;
        startcardtransition(ecard, source_color, getplayerbgcolor(ecard->isplayer1));
        if (issuddendeath == 1) {
            issuddendeath++;
        }
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
        for (i = 0; i < 10; i++) {
            loadcardslot(packptr, randInt(0, card_count - 1), i);
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
        for (i = 0; i < 10; i++) {
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

    for (i = 0; i < 10; i++) {
        if (i != selcard) {
            drawcard(cardbuf[i], false);
        }
    }
    if (selcard < 10) {
        drawcard(cardbuf[selcard], true);
    }
}
