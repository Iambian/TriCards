/*
 *--------------------------------------
 * Program Name: TRICARDS
 * Author: rawrf.
 * License: rawrf.
 * Description: rawrf.
 *--------------------------------------
 */

#include "tricards.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <compression.h>
#include <fileioc.h>
#include <graphx.h>
#include <keypadc.h>

#include "gfx/out/element_gfx.h"
#include "gfx/out/internal_palette.h"
#include "gfx/out/num_gfx.h"
#include "gfx/out/misc_gfx.h"

int posarr[] = {
    0,0,
    GRIDX+GRIDV*0,GRIDY+GRIDV*0,
    GRIDX+GRIDV*1,GRIDY+GRIDV*0,
    GRIDX+GRIDV*2,GRIDY+GRIDV*0,
    GRIDX+GRIDV*0,GRIDY+GRIDV*1,
    GRIDX+GRIDV*1,GRIDY+GRIDV*1,
    GRIDX+GRIDV*2,GRIDY+GRIDV*1,
    GRIDX+GRIDV*0,GRIDY+GRIDV*2,
    GRIDX+GRIDV*1,GRIDY+GRIDV*2,
    GRIDX+GRIDV*2,GRIDY+GRIDV*2,

    PLAYERX,PLAYERY+PLAYERV*0,
    PLAYERX,PLAYERY+PLAYERV*1,
    PLAYERX,PLAYERY+PLAYERV*2,
    PLAYERX,PLAYERY+PLAYERV*3,
    PLAYERX,PLAYERY+PLAYERV*4,

    ENEMYX,ENEMYY+ENEMYV*0,
    ENEMYX,ENEMYY+ENEMYV*1,
    ENEMYX,ENEMYY+ENEMYV*2,
    ENEMYX,ENEMYY+ENEMYV*3,
    ENEMYX,ENEMYY+ENEMYV*4,
};

struct {
    unsigned int wins;
    unsigned int losses;
    char fn[10];
} stats;

static uint8_t card_image_pool_storage[CARD_IMAGE_BUFFER_SIZE * CARD_SLOT_COUNT];
uint8_t *card_image_pool = card_image_pool_storage;
uint8_t curpack, maxpack;
uint8_t gamemode;
uint8_t selcard;
uint8_t curplayer;
uint8_t player2_ai_difficulty = PLAYER2_AI_HARD;
uint8_t ruleFlags;
uint8_t issuddendeath;

uint8_t *elemdat[9];
tricard_card_slot_t card_slots[CARD_SLOT_COUNT];
tricard_card_slot_t *cardbuf[CARD_SLOT_COUNT];
gfx_sprite_t *numtiles[12];
gfx_sprite_t *cardback;
uint8_t elementgrid[9];

char *card_pack_header = "Tri2Pak!";
char *main_menu_text[] = {"Start Game","Card Pack Browser","Options","Quit Game"};
uint8_t main_menu_dest[] = {GM_GAMESELECT,GM_BROWSEPACK,GM_OPTIONS,GM_GAMEXIT};
uint8_t listcolors[] = {LIST_BG_A,LIST_BG_B};
char stat2char[] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};
char *cardtype[] = {"Monster","Boss","GF","Player"};
uint8_t *elemcdat[] = {
    blanksym_compressed,poison_compressed,fire_compressed,
    wind_compressed,earth_compressed,water_compressed,
    ice_compressed,thunder_compressed,holy_compressed
};

static const char *rule_menu_text[] = {
    "Start Game",
    "Open",
    "Random",
    "Elemental",
    "Sudden Death",
    "Same",
    "Same Wall",
    "Plus",
    "Combo"
};

static const uint8_t rule_menu_flags[] = {
    0,
    RULE_OPEN,
    RULE_RANDOM,
    RULE_ELEMENTAL,
    RULE_SUDDENDEATH,
    RULE_SAME,
    RULE_SAMEWALL,
    RULE_PLUS,
    RULE_COMBO
};

typedef struct {
    const char *label;
    const char * const *state_labels;
    uint8_t state_count;
    uint8_t *value;
} option_menu_entry_t;

static const char *const player2_ai_option_states[] = {"Manual","Easy","Medium","Hard"};

static const option_menu_entry_t option_menu_entries[] = {
    {
        "AI Difficulty",
        player2_ai_option_states,
        (uint8_t)(sizeof player2_ai_option_states / sizeof *player2_ai_option_states),
        &player2_ai_difficulty
    }
};

enum moveResolutionResult {
    MOVE_RESOLUTION_INVALID = 0,
    MOVE_RESOLUTION_PLACED = 1,
    MOVE_RESOLUTION_FINISHED_GAME = 2
};

static uint8_t getrulemenuoptioncount(void) {
    return (uint8_t)(sizeof rule_menu_text / sizeof *rule_menu_text);
}

static bool isruleoptionenabled(uint8_t option_index) {
    if (option_index == 0) {
        return false;
    }
    return (ruleFlags & rule_menu_flags[option_index]) != 0;
}

static void normalizeruleflags(void) {
    ruleFlags |= RULE_RANDOM;
    if (!(ruleFlags & RULE_SAME)) {
        ruleFlags &= (uint8_t)~RULE_SAMEWALL;
    }
}

static void toggleruleoption(uint8_t option_index) {
    uint8_t option_flag;

    if (option_index == 0) {
        return;
    }

    option_flag = rule_menu_flags[option_index];
    if (option_flag == RULE_RANDOM) {
        return;
    }
    if (option_flag == RULE_SAMEWALL && !(ruleFlags & RULE_SAME)) {
        return;
    }

    ruleFlags ^= option_flag;
    normalizeruleflags();
}

static const char *getruleoptionstate(uint8_t option_index) {
    if (option_index == 0) {
        return NULL;
    }
    return isruleoptionenabled(option_index) ? "ON" : "OFF";
}

static uint8_t getoptionmenuoptioncount(void) {
    return (uint8_t)(sizeof option_menu_entries / sizeof *option_menu_entries);
}

static void normalizeoptionmenuvalues(void) {
    uint8_t i;

    for (i = 0; i < getoptionmenuoptioncount(); i++) {
        if (*option_menu_entries[i].value >= option_menu_entries[i].state_count) {
            *option_menu_entries[i].value = 0;
        }
    }
}

static const char *getoptionmenuoptionstate(uint8_t option_index) {
    const option_menu_entry_t *option;
    uint8_t option_value;

    option = &option_menu_entries[option_index];
    option_value = *option->value;
    if (option_value >= option->state_count) {
        option_value = 0;
    }
    return option->state_labels[option_value];
}

static void cycleoptionmenuoption(uint8_t option_index) {
    const option_menu_entry_t *option;
    uint8_t option_value;

    option = &option_menu_entries[option_index];
    option_value = (uint8_t)(*option->value + 1);
    if (option_value >= option->state_count) {
        option_value = 0;
    }
    *option->value = option_value;
}

static void drawruleselectmenu(uint8_t current_option) {
    uint8_t i;
    uint8_t option_count;
    uint8_t box_x;
    uint8_t box_y;
    uint8_t box_w;
    uint8_t row_y;
    const char *state_text;

    option_count = getrulemenuoptioncount();
    box_w = 200;
    box_x = (uint8_t)((LCD_WIDTH - box_w) / 2);
    box_y = 56;

    drawbg();
    textscale2();
    ctext("Match Rules",5);
    textscale1();
    gfx_PrintStringXY("Pack: ", box_x, 32);
    gfx_PrintString(stats.fn);
    gfx_PrintStringXY("Random stays enabled.", box_x, 202);
    gfx_PrintStringXY("2nd: toggle/start", box_x, 216);
    gfx_PrintStringXY("Mode: choose another pack", box_x, 228);

    for (i = 0, row_y = box_y; i < option_count; i++, row_y += LIST_LINE_HEIGHT) {
        gfx_SetColor(listcolors[i & 1]);
        if (i == current_option) {
            gfx_SetColor(LIST_BG_S);
            gfx_SetTextFGColor(LIST_TX_S);
        } else if (isruleoptionenabled(i)) {
            gfx_SetTextFGColor(RULE_ENABLED_TEXT_COLOR);
        } else {
            gfx_SetTextFGColor(MENU_TEXT_COLOR);
        }

        gfx_FillRectangle_NoClip(box_x, row_y, box_w, LIST_LINE_HEIGHT);
        if (i == 0) {
            gfx_PrintStringXY(
                (char *)rule_menu_text[i],
                (LCD_WIDTH - gfx_GetStringWidth(rule_menu_text[i])) / 2,
                row_y + 2
            );
        } else {
            gfx_PrintStringXY((char *)rule_menu_text[i], box_x + 8, row_y + 2);
            state_text = getruleoptionstate(i);
            if (state_text != NULL) {
                gfx_PrintStringXY(
                    (char *)state_text,
                    box_x + box_w - gfx_GetStringWidth(state_text) - 8,
                    row_y + 2
                );
            }
        }
        gfx_SetTextFGColor(MENU_TEXT_COLOR);
    }
}

static void drawoptionsmenu(uint8_t current_option) {
    uint8_t i;
    uint8_t option_count;
    uint8_t box_x;
    uint8_t box_y;
    uint8_t box_w;
    uint8_t row_y;
    const char *state_text;

    option_count = getoptionmenuoptioncount();
    box_w = 200;
    box_x = (uint8_t)((LCD_WIDTH - box_w) / 2);
    box_y = 56;

    drawbg();
    textscale2();
    ctext("Options",5);
    textscale1();
    gfx_PrintStringXY("Settings apply immediately.", box_x, 32);
    gfx_PrintStringXY("2nd: cycle setting", box_x, 216);
    gfx_PrintStringXY("Mode: return to title", box_x, 228);

    for (i = 0, row_y = box_y; i < option_count; i++, row_y += LIST_LINE_HEIGHT) {
        gfx_SetColor(listcolors[i & 1]);
        if (i == current_option) {
            gfx_SetColor(LIST_BG_S);
            gfx_SetTextFGColor(LIST_TX_S);
        } else {
            gfx_SetTextFGColor(MENU_TEXT_COLOR);
        }

        gfx_FillRectangle_NoClip(box_x, row_y, box_w, LIST_LINE_HEIGHT);
        gfx_PrintStringXY((char *)option_menu_entries[i].label, box_x + 8, row_y + 2);
        state_text = getoptionmenuoptionstate(i);
        gfx_PrintStringXY(
            (char *)state_text,
            box_x + box_w - gfx_GetStringWidth(state_text) - 8,
            row_y + 2
        );
        gfx_SetTextFGColor(MENU_TEXT_COLOR);
    }
}

static bool isselectedcardplacementvalid(void) {
    uint8_t i;
    uint8_t gridpos;

    if (selcard >= GAME_CARD_SLOT_COUNT) {
        return false;
    }

    gridpos = cardbuf[selcard]->gridpos;
    if (gridpos < 1 || gridpos > 9) {
        return false;
    }

    for (i = 0; i < GAME_CARD_SLOT_COUNT; i++) {
        if (i == selcard) {
            continue;
        }
        if (cardbuf[i]->gridpos == gridpos) {
            return false;
        }
    }
    return true;
}

static uint8_t finishselectedmove(uint8_t *packptr) {
    uint8_t i;
    uint8_t player1_count;
    uint8_t player2_count;

    if (!isselectedcardplacementvalid()) {
        return MOVE_RESOLUTION_INVALID;
    }

    cardbuf[selcard]->playstate = 2;
    resolvecardplacement(cardbuf[selcard]->gridpos);

    for (i = player1_count = player2_count = 0; i < GAME_CARD_SLOT_COUNT; i++) {
        if (cardbuf[i]->gridpos < 10) {
            if (cardbuf[i]->isplayer1) {
                player1_count++;
            } else {
                player2_count++;
            }
        }
    }

    if (player1_count + player2_count == 9 || issuddendeath == 2) {
        for (i = player1_count = player2_count = 0; i < GAME_CARD_SLOT_COUNT; i++) {
            if (cardbuf[i]->isplayer1) {
                player1_count++;
            } else {
                player2_count++;
            }
        }
        redrawboard();
        if (player1_count > player2_count) {
            gfx_PrintStringXY("Player 1 has won!",5,230);
        } else if (player1_count < player2_count) {
            gfx_PrintStringXY("Player 2 has won!",5,230);
        } else {
            gfx_PrintStringXY("The game ended in a draw!",5,230);
            if (ruleFlags & RULE_SUDDENDEATH) {
                gfx_PrintString(" Sudden Death!");
                issuddendeath = 1;
                initGame(packptr);
            }
        }
        if (issuddendeath != 1) {
            gamemode = GM_TITLE;
        } else {
            gamemode = GM_SELECTINGCARDS;
        }
        gfx_SwapDraw();
        waitanykey();
        return MOVE_RESOLUTION_FINISHED_GAME;
    }

    curplayer = !curplayer;
    selcard = 0;
    selcard = selectfromhand(DIR_NONE);
    gamemode = GM_SELECTINGCARDS;
    redrawboard();
    return MOVE_RESOLUTION_PLACED;
}

static uint8_t getbrowserpagecount(uint16_t card_count) {
    return (uint8_t)((card_count + OPTIONS_PER_PAGE - 1) / OPTIONS_PER_PAGE);
}

static uint8_t getbrowserpageoptions(uint16_t card_count, uint8_t page) {
    uint16_t start_index;
    uint16_t remaining_cards;

    start_index = (uint16_t)page * OPTIONS_PER_PAGE;
    if (start_index >= card_count) {
        return 0;
    }

    remaining_cards = card_count - start_index;
    if (remaining_cards > OPTIONS_PER_PAGE) {
        return OPTIONS_PER_PAGE;
    }

    return (uint8_t)remaining_cards;
}

int main(void) {
    char *varname,*cardtypestr;
    uint8_t *packptr,*dataptr,i,y,copt,mopt,cpage,mpage;
    uint16_t card_count;
    uint16_t card_index;
    uint8_t cardposbackup;
    int x;
    kb_key_t k,k7;

    gfx_Begin();
    memcpy(&gfx_palette[INTERNAL_PALETTE_BASE_INDEX], internal_palette, sizeof_internal_palette);
    gfx_SetDrawBuffer();
    gfx_SetTransparentColor(INTERNAL_TRANSPARENT_INDEX);
    cardposbackup = cpage = mpage = copt = mopt = gamemode = curpack = maxpack = 0;
    packptr = dataptr = NULL;
    for (i = 0; i < CARD_SLOT_COUNT; i++) {
        cardbuf[i] = &card_slots[i];
        cardbuf[i]->slot_index = i;
    }
    dataptr = malloc((8*8+2)*12);
    for (i=0;i<12;i++,dataptr+=(8*8+2)) {
        zx7_Decompress(numtiles[i] =(void*) dataptr,numtiles_tiles_compressed[i]);
    }
    for (i = 0; i < CARD_SLOT_COUNT; i++) {
        resetcardslot(i);
    }
    zx7_Decompress(cardback = malloc(CARD_WIDTH*CARD_HEIGHT+2),cardback_compressed);

    maxpack = countpacks();
    dataptr = malloc(9*(8*8+2));
    for(i=0;i<9;i++,dataptr+=66) zx7_Decompress(elemdat[i]=dataptr,elemcdat[i]);

    if (maxpack) {
        while (1) {
            i = randInt(0,255);
            kb_Scan();
            k = kb_Data[1];
            k7= kb_Data[7];
            if (gamemode == GM_TITLE) {
                closegamebackground();
                if (packptr != NULL) {
                    closepack();
                    packptr = NULL;
                }
            }
            if (k|k7) keywait();
            if (gamemode==GM_TITLE) {
                if (k&kb_2nd) {
                    gamemode = main_menu_dest[copt];
                    if (gamemode == GM_OPTIONS) {
                        copt = 0;
                    }
                    continue;
                }
                if (k&kb_Mode) { break; }
                if (k7&(kb_Up|kb_Left)) copt--;
                if (k7&(kb_Down|kb_Right)) copt++;
                copt&=3;
                drawbg();
                textscale2();
                ctext("TriCards",5);
                dmenu(main_menu_text,copt,4);
                textscale1();
                gfx_PrintStringXY(VERSION_INFO,290,230);
            }
            else if (gamemode == GM_OPTIONS) {
                uint8_t option_count;

                option_count = getoptionmenuoptioncount();
                if (!option_count) {
                    gamemode = GM_TITLE;
                    continue;
                }

                normalizeoptionmenuvalues();
                if (copt >= option_count) {
                    copt = (uint8_t)(option_count - 1);
                }

                drawoptionsmenu(copt);
                if (k & kb_Mode) {
                    gamemode = GM_TITLE;
                    continue;
                }
                if ((k7 & kb_Up) && copt) {
                    copt--;
                }
                if ((k7 & kb_Down) && copt < (option_count - 1)) {
                    copt++;
                }
                if (k & kb_2nd) {
                    cycleoptionmenuoption(copt);
                }
            }
            else if (gamemode == GM_BROWSEPACK) {
                if ((varname = selectpack()) == NULL) { gamemode = GM_TITLE; continue; }
                packptr = getpackadr(varname);
                if (packptr == NULL) {
                    gamemode = GM_TITLE;
                    continue;
                }
                copt = cpage = 0;
                card_count = getpackcardcount(packptr);
                mpage = getbrowserpagecount(card_count);
                mopt = getbrowserpageoptions(card_count, cpage);
                if (mopt == 0) {
                    closepack();
                    packptr = NULL;
                    gamemode = GM_BROWSEPACK;
                    continue;
                }
                gamemode = GM_CARDLISTER;
            }
            else if (gamemode == GM_CARDLISTER) {
                const char *card_name;
                tricard_card_slot_t *preview_slot;

                if (packptr == NULL) {
                    gamemode = GM_BROWSEPACK;
                    continue;
                }

                card_count = getpackcardcount(packptr);
                mpage = getbrowserpagecount(card_count);
                mopt = getbrowserpageoptions(card_count, cpage);
                if (mopt == 0) {
                    closepack();
                    packptr = NULL;
                    gamemode = GM_BROWSEPACK;
                    continue;
                }
                if (copt >= mopt) {
                    copt = mopt - 1;
                }

                drawbg();
                textscale2();
                ctext("Card Browser",5);
                textscale1();
                gfx_PrintStringXY("Showing page ",5,30);
                gfx_PrintUInt(cpage+1,2);
                gfx_PrintString(" of ");
                gfx_PrintUInt(mpage,2);
                for(i=0,card_index=(uint16_t)cpage*OPTIONS_PER_PAGE,y=50;i<OPTIONS_PER_PAGE;i++,card_index++,y+=LIST_LINE_HEIGHT){
                    gfx_SetColor(listcolors[i&1]);
                    if (i==copt) {
                        gfx_SetColor(LIST_BG_S);
                        gfx_SetTextFGColor(LIST_TX_S);
                    }
                    gfx_FillRectangle_NoClip(5,y,200,LIST_LINE_HEIGHT);
                    card_name = getcardname(packptr, card_index);
                    if (card_name != NULL) {
                        gfx_PrintStringXY((char *)card_name,10,y+2);
                    }
                    gfx_SetTextFGColor(MENU_TEXT_COLOR);
                }

                if (loadcardslot(packptr,((uint16_t)cpage * OPTIONS_PER_PAGE) + copt,CARD_BROWSER_PREVIEW_SLOT)) {
                    preview_slot = cardbuf[CARD_BROWSER_PREVIEW_SLOT];
                    SET_CARD_SLOT_BASE_COLOR(preview_slot, FILE_EXPLORER_BGCOLOR);
                    gfx_SetColor(INTERNAL_BLACK_COLOR);
                    cardtypestr = (preview_slot->card.type < 4) ? cardtype[preview_slot->card.type] : "Unknown";
                    x = 200+(120-gfx_GetStringWidth(cardtypestr))/2;
                    gfx_PrintStringXY(cardtypestr,x,52);

                    gfx_Rectangle_NoClip(234,65,CARD_WIDTH+2,CARD_HEIGHT+2);
                    gfx_TransparentSprite_NoClip(preview_slot->card.img,235,66);
                    gfx_Rectangle_NoClip(207,127,43,40);
                    gfx_PrintStringXY("Stats",210,130);
                    pcharxy(stat2char[preview_slot->card.up]   ,220+5,140);
                    pcharxy(stat2char[preview_slot->card.right],228+5,148);
                    pcharxy(stat2char[preview_slot->card.down] ,220+5,156);
                    pcharxy(stat2char[preview_slot->card.left] ,212+5,148);
                    gfx_PrintStringXY("Rank ",260,130);
                    gfx_PrintUInt(preview_slot->card.rank,2);
                    gfx_PrintStringXY("Element",260,145);
                    if (preview_slot->card.element > 0 && preview_slot->card.element < 9) {
                        gfx_TransparentSprite_NoClip((gfx_sprite_t*)elemdat[preview_slot->card.element],280,155);
                    } else gfx_PrintStringXY("N/A",275,155);
                }
                if (k&kb_Mode) {
                    closepack();
                    packptr = NULL;
                    gamemode = GM_BROWSEPACK;
                }
                if ((k7&kb_Up)&&copt) copt--;
                if ((k7&kb_Down)&&copt<(mopt-1)) copt++;
                if ((k7&kb_Left)&&cpage) {
                    cpage--;
                    mopt = getbrowserpageoptions(card_count, cpage);
                    if (copt >= mopt) {
                        copt = mopt - 1;
                    }
                }
                if ((k7&kb_Right)&&(cpage<(mpage-1))) {
                    cpage++;
                    mopt = getbrowserpageoptions(card_count, cpage);
                    if (copt >= mopt) {
                        copt = mopt - 1;
                    }
                }
            }
            else if (gamemode == GM_GAMESELECT) {
                if ((packptr = getpackadr(stats.fn)) == NULL) {
                    if ((varname = selectpack()) == NULL) { gamemode = GM_TITLE; continue; }
                    packptr = getpackadr(varname);
                    strncpy(stats.fn,varname,9);
                    stats.fn[9] = 0x00;
                }
                ruleFlags = DEFAULT_RULE_FLAGS;
                normalizeruleflags();
                copt = 0;
                gamemode = GM_RULESELECT;
                continue;
            }
            else if (gamemode == GM_RULESELECT) {
                if (packptr == NULL) {
                    gamemode = GM_GAMESELECT;
                    continue;
                }

                drawruleselectmenu(copt);
                if (k & kb_Mode) {
                    closepack();
                    packptr = NULL;
                    stats.fn[0] = 0x00;
                    gamemode = GM_GAMESELECT;
                    continue;
                }
                if ((k7 & kb_Up) && copt) {
                    copt--;
                }
                if ((k7 & kb_Down) && copt < (getrulemenuoptioncount() - 1)) {
                    copt++;
                }
                if (k & kb_2nd) {
                    if (copt == 0) {
                        initGame(packptr);
                        issuddendeath = 0;
                        gamemode = GM_SELECTINGCARDS;
                    } else {
                        toggleruleoption(copt);
                    }
                    continue;
                }
            }
            else if (gamemode == GM_SELECTINGCARDS) {
                uint8_t move_result;

                if (curplayer == 1 && player2_ai_difficulty != PLAYER2_CONTROL_MANUAL) {
                    uint8_t ai_card_index;
                    uint8_t ai_gridpos;

                    move_result = MOVE_RESOLUTION_INVALID;
                    if (getplayer2aimove(&ai_card_index, &ai_gridpos)) {
                        uint8_t ai_cardposbackup;

                        ai_cardposbackup = cardbuf[ai_card_index]->gridpos;
                        selcard = ai_card_index;
                        cardbuf[selcard]->gridpos = ai_gridpos;
                        move_result = finishselectedmove(packptr);
                        if (move_result == MOVE_RESOLUTION_INVALID) {
                            cardbuf[selcard]->gridpos = ai_cardposbackup;
                        }
                    }
                    if (move_result == MOVE_RESOLUTION_FINISHED_GAME) {
                        continue;
                    }
                    if (move_result == MOVE_RESOLUTION_INVALID) {
                        redrawboard();
                    }
                } else {
                    i = 255;
                    if (k&kb_2nd) {
                        cardposbackup = cardbuf[selcard]->gridpos;
                        cardbuf[selcard]->gridpos = 5;
                        gamemode = GM_SELECTINGPLACE;
                    }
                    if (k&kb_Mode) gamemode = GM_TITLE;
                    if (k7&kb_Up) i = selectfromhand(DIR_UP);
                    if (k7&kb_Down) i = selectfromhand(DIR_DOWN);
                    if (i < GAME_CARD_SLOT_COUNT) selcard = i;
                    redrawboard();
                }
            }
            else if (gamemode == GM_SELECTINGPLACE) {
                uint8_t move_result;

                if (k&kb_Mode) {
                    cardbuf[selcard]->gridpos = cardposbackup;
                    gamemode = GM_SELECTINGCARDS;
                }
                i = cardbuf[selcard]->gridpos;
                if ((k7&kb_Up)&&(i>3)) i -= 3;
                if ((k7&kb_Down)&&(i<7)) i += 3;
                if ((k7&kb_Left)&&((i-1)%3)) i -= 1;
                if ((k7&kb_Right)&&(~(i-1)%3)) i += 1;
                cardbuf[selcard]->gridpos = i;
                redrawboard();
                if (k&kb_2nd) {
                    move_result = finishselectedmove(packptr);
                    if (move_result == MOVE_RESOLUTION_FINISHED_GAME) {
                        continue;
                    }
                }
            }
            else { break; }
            gfx_SwapDraw();
        }
    } else {
        drawbg();
        ctext("ERROR",80);
        ctext("You need to have a card pack",90);
        ctext("installed before you can play",100);
        ctext("Check /BUILDER/bin/ for packs",110);
        gfx_SwapDraw();
        waitanykey();
    }
    closegamebackground();
    closepack();
    gfx_End();
    return 0;
}

void keywait(void) { while (kb_AnyKey()); }
void waitanykey(void) {
    keywait();
    while (!kb_AnyKey()) {
    }
    keywait();
}
void ctext(char* s,uint8_t y) { gfx_PrintStringXY(s,(LCD_WIDTH-gfx_GetStringWidth(s))/2,y); }
void textscale2(void) { gfx_SetTextScale(2,2); }
void textscale1(void) { gfx_SetTextScale(1,1); }
void dmenu(char **s,uint8_t c,uint8_t m) { uint8_t i,y; textscale2(); for(i=0,y=(240-24*m)/2;i<m;i++,y+=24) { if (i==c) gfx_SetTextFGColor(MENU_TEXT_SELECTED); ctext(s[i],y); gfx_SetTextFGColor(MENU_TEXT_COLOR); } textscale1(); }
void pcharxy(char c,int x,uint8_t y) { gfx_SetTextXY(x,y); gfx_PrintChar(c); }
void drawbg(void) { gfx_FillScreen(FILE_EXPLORER_BGCOLOR); }

char *selectpack(void) {
    static char selected_pack_name[TRICARD_VAR_NAME_LENGTH + 1];
    char pack_name[TRICARD_VAR_NAME_LENGTH + 1];
    uint8_t *packptr,i;
    uint16_t card_count;
    int x;
    char pack_identifier[10];
    kb_key_t k,k7;

    while (1) {
        kb_Scan();
        k = kb_Data[1];
        k7= kb_Data[7];
        drawbg();

        if (!getpackname(curpack, pack_name)) {
            closepack();
            return NULL;
        }
        packptr = getpackadr(pack_name);
        if (packptr == NULL) {
            continue;
        }
        card_count = getpackcardcount(packptr);

        textscale2();
        ctext("Card Pack Selection",5);
        textscale1();
        gfx_PrintStringXY("Displaying pack ",5,30);
        gfx_PrintUInt(curpack+1,3);
        gfx_PrintString(" of ");
        gfx_PrintUInt(maxpack,3);

        ctext((char*)getpackdescription(packptr),70);
        gfx_PrintStringXY("Filename: ",5,85);
        gfx_PrintString(pack_name);
        gfx_PrintString(", descriptor: ");
        memcpy(pack_identifier, getpackheader(packptr)->pack_identifier, 9);
        pack_identifier[9] = 0x00;
        gfx_PrintString(pack_identifier);
        gfx_PrintStringXY("Number of cards: ",5,95);
        gfx_PrintUInt(card_count,3);
        ctext("Card pack preview",110);
        for(i=0,x=(LCD_WIDTH-(CARD_WIDTH+4)*PACK_SELECTOR_PREVIEW_COUNT)/2;i<PACK_SELECTOR_PREVIEW_COUNT;i++,x+=CARD_WIDTH+4) {
            uint8_t preview_slot_index;

            preview_slot_index = (uint8_t)(CARD_PACK_SELECTOR_SLOT_BASE + i);
            if (loadcardslot(packptr,i,preview_slot_index) && cardbuf[preview_slot_index]->card.rank) {
                SET_CARD_SLOT_BASE_COLOR(cardbuf[preview_slot_index], FILE_EXPLORER_BGCOLOR);
                gfx_SetColor(INTERNAL_BLACK_COLOR);
                gfx_Rectangle_NoClip(x-1,119,CARD_WIDTH+2,CARD_HEIGHT+2);
                gfx_TransparentSprite_NoClip(cardbuf[preview_slot_index]->card.img,x,120);
            } else {
                gfx_SetColor(FILE_EXPLORER_BGCOLOR);
                gfx_FillRectangle_NoClip(x-1,119,CARD_WIDTH+2,CARD_HEIGHT+2);
            }
        }
        gfx_SwapDraw();

        if (k|k7) keywait();
        if (k&kb_Mode) {
            closepack();
            return NULL;
        }
        if (k&kb_2nd) {
            strncpy(selected_pack_name, pack_name, TRICARD_VAR_NAME_LENGTH);
            selected_pack_name[TRICARD_VAR_NAME_LENGTH] = 0x00;
            closepack();
            return selected_pack_name;
        }
        if ((k7&(kb_Left|kb_Up))&&curpack) curpack--;
        if ((k7&(kb_Right|kb_Down))&&(curpack<(maxpack-1))) curpack++;
    }
}
