#include <string.h>

#include "tricards.h"

static void drawcard(tricard_card_slot_t *card, bool selected) {
    int x, y, cx, cy;
    uint8_t gpos;
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
    if (selected) {
        gpos = card->gridpos;
        if (gpos > 9 && gpos < 15) {
            x += 5;
        }
        if (gpos > 14) {
            x -= 5;
        }
    }
    gfx_SetColor(card->isplayer1 ? PLAYER1_BG : PLAYER2_BG);
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
        ecard->isplayer1 = pcard->isplayer1;
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
