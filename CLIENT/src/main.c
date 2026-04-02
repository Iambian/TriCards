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

#include "gfx/element_gfx.h"
#include "gfx/internal_palette.h"
#include "gfx/num_gfx.h"
#include "gfx/misc_gfx.h"

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

uint8_t *card_image_pool;
uint8_t curpack, maxpack;
uint8_t gamemode;
uint8_t selcard;
uint8_t curplayer;
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
    void *vat_ptr;
    uint8_t *packptr,*dataptr,i,j,y,copt,mopt,cpage,mpage;
    uint16_t card_count;
    uint16_t card_index;
    uint8_t cardposbackup;
    int8_t s;
    tricard_card_slot_t *pcard;
    int x;
    kb_key_t k,k7;

    gfx_Begin();
    memcpy(&gfx_palette[INTERNAL_PALETTE_BASE_INDEX], internal_palette, sizeof_internal_palette);
    gfx_SetDrawBuffer();
    gfx_SetTransparentColor(INTERNAL_TRANSPARENT_INDEX);
    cardposbackup = cpage = mpage = copt = mopt = gamemode = curpack = maxpack = 0;
    vat_ptr = NULL;
    packptr = dataptr = NULL;
    for (i = 0; i < CARD_SLOT_COUNT; i++) {
        cardbuf[i] = &card_slots[i];
        cardbuf[i]->slot_index = i;
    }
    dataptr = malloc((8*8+2)*12);
    for (i=0;i<12;i++,dataptr+=(8*8+2)) {
        zx7_Decompress(numtiles[i] =(void*) dataptr,numtiles_tiles_compressed[i]);
    }
    card_image_pool = malloc(CARD_IMAGE_BUFFER_SIZE * CARD_SLOT_COUNT);
    for (i = 0; i < CARD_SLOT_COUNT; i++) {
        resetcardslot(i);
    }
    zx7_Decompress(cardback = malloc(CARD_WIDTH*CARD_HEIGHT+2),cardback_compressed);

    while (ti_Detect(&vat_ptr,card_pack_header)) { maxpack++; }
    dataptr = malloc(9*(8*8+2));
    for(i=0;i<9;i++,dataptr+=66) zx7_Decompress(elemdat[i]=dataptr,elemcdat[i]);

    if (maxpack) {
        while (1) {
            i = randInt(0,255);
            kb_Scan();
            k = kb_Data[1];
            k7= kb_Data[7];
            if (gamemode == GM_TITLE && packptr != NULL) {
                closepack();
                packptr = NULL;
            }
            if (k|k7) keywait();
            if (gamemode==GM_TITLE) {
                if (k&kb_2nd) { gamemode = main_menu_dest[copt]; continue; }
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
                const tricard_card_metadata_t *metadata;
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
                    metadata = getcardmetadata(packptr, card_index);
                    if (metadata != NULL && metadata->rank) {
                        gfx_PrintStringXY((char *)(packptr + metadata->name_offset),10,y+2);
                    }
                    gfx_SetTextFGColor(MENU_TEXT_COLOR);
                }

                if (loadcardslot(packptr,((uint16_t)cpage * OPTIONS_PER_PAGE) + copt,CARD_BROWSER_PREVIEW_SLOT)) {
                    preview_slot = cardbuf[CARD_BROWSER_PREVIEW_SLOT];
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
                ruleFlags = RULE_OPEN | RULE_RANDOM | RULE_ELEMENTAL | RULE_SUDDENDEATH;

                initGame(packptr);
                issuddendeath = 0;
                gamemode = GM_SELECTINGCARDS;
                continue;
            }
            else if (gamemode == GM_SELECTINGCARDS) {
                i = 255;
                if (k&kb_2nd) {
                    cardposbackup = cardbuf[selcard]->gridpos;
                    cardbuf[selcard]->gridpos = 5;
                    gamemode = GM_SELECTINGPLACE;
                }
                if (k&kb_Mode) gamemode = GM_TITLE;
                if (k7&kb_Up) i = selectfromhand(DIR_UP);
                if (k7&kb_Down) i = selectfromhand(DIR_DOWN);
                if (i < 10) selcard = i;
                redrawboard();
            }
            else if (gamemode == GM_SELECTINGPLACE) {

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
                    for (j=0;j<10;j++) {
                        if (j==selcard) continue;
                        if (cardbuf[selcard]->gridpos == cardbuf[j]->gridpos) {
                            j = 255;
                            break;
                        }
                    }
                    if (j<11) {
                        pcard = cardbuf[selcard];
                        pcard->playstate = 2;
                        if ((j = elementgrid[i-1])) {
                            if (pcard->card.element == j) s = 1;
                            else s = -1;
                            pcard->card.up = pcard->card.up + s;
                            pcard->card.right = pcard->card.right + s;
                            pcard->card.down = pcard->card.down + s;
                            pcard->card.left = pcard->card.left + s;
                        }

                        if (i>3) cardfight(i,i-3);
                        if (i<7) cardfight(i,i+3);
                        if ((i-1)%3) cardfight(i,i-1);
                        if (~(i-1)%3) cardfight(i,i+1);

                        for(i=j=k=0;i<10;i++) {
                            if (cardbuf[i]->gridpos < 10) {
                                if (cardbuf[i]->isplayer1) j++;
                                else k++;
                            }
                        }
                        if (j+k == 9 || issuddendeath == 2) {
                            for (i=j=k=0;i<10;i++) {
                                if (cardbuf[i]->isplayer1) j++;
                                else k++;
                            }
                            redrawboard();
                            if (j>k) {
                                gfx_PrintStringXY("Player 1 has won!",5,230);
                            } else if (j<k) {
                                gfx_PrintStringXY("Player 2 has won!",5,230);
                            } else {
                                gfx_PrintStringXY("The game ended in a draw!",5,230);
                                if (ruleFlags & RULE_SUDDENDEATH) {
                                    gfx_PrintString(" Sudden Death!");
                                    issuddendeath = 1;
                                    initGame(packptr);
                                }
                            }
                            if (issuddendeath != 1) gamemode = GM_TITLE;
                            else gamemode = GM_SELECTINGCARDS;
                            gfx_SwapDraw();
                            waitanykey();
                            continue;
                        }
                        curplayer = !curplayer;
                        selcard = 0;
                        selcard = selectfromhand(DIR_NONE);
                        gamemode = GM_SELECTINGCARDS;
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
    void *vat_ptr;
    uint8_t *packptr,i;
    uint16_t card_count;
    int x;
    char pack_identifier[10];
    char *vn;
    kb_key_t k,k7;

    vat_ptr = NULL;
    vn = NULL;
    while (1) {
        kb_Scan();
        k = kb_Data[1];
        k7= kb_Data[7];
        drawbg();

        vn = NULL;
        for (i=0,vat_ptr=NULL;i<(curpack+1);i++,vn=ti_Detect(&vat_ptr,card_pack_header));
        if (!vn) {
            closepack();
            return NULL;
        }
        packptr = getpackadr(vn);
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
        gfx_PrintString(vn);
        gfx_PrintString(", descriptor: ");
        memcpy(pack_identifier, getpackheader(packptr)->pack_identifier, 9);
        pack_identifier[9] = 0x00;
        gfx_PrintString(pack_identifier);
        gfx_PrintStringXY("Number of cards: ",5,95);
        gfx_PrintUInt(card_count,3);
        ctext("Card pack preview",110);
        for(i=0,x=(LCD_WIDTH-(CARD_WIDTH+4)*5)/2;i<5;i++,x+=CARD_WIDTH+4) {
            if (loadcardslot(packptr,i,i) && cardbuf[i]->card.rank) {
                gfx_SetColor(INTERNAL_BLACK_COLOR);
                gfx_Rectangle_NoClip(x-1,119,CARD_WIDTH+2,CARD_HEIGHT+2);
                gfx_TransparentSprite_NoClip(cardbuf[i]->card.img,x,120);
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
            closepack();
            return vn;
        }
        if ((k7&(kb_Left|kb_Up))&&curpack) curpack--;
        if ((k7&(kb_Right|kb_Down))&&(curpack<(maxpack-1))) curpack++;
    }
}
