/*
 *--------------------------------------
 * Program Name: TRICARDS
 * Author: rawrf.
 * License: rawrf.
 * Description: rawrf.
 *--------------------------------------
*/

#define VERSION_INFO "v0.1"

#define GM_TITLE 0
#define GM_BROWSEPACK 4
#define GM_CARDLISTER 5
#define GM_OPTIONS 6
#define GM_GAMESELECT 7
#define GM_SELECTINGCARDS 8
#define GM_SELECTINGPLACE 9
#define GM_GAMEXIT 255

#define GAMEBOARD_BG 0xC5
#define PLAYER1_BG 0xF4
#define PLAYER2_BG 0x9F
#define CARD_SEL_FG 0x8C

#define CARD_WIDTH 52
#define CARD_HEIGHT 52

#define TRANSPARENT_COLOR 0xFF
#define GREETINGS_DIALOG_TEXT_COLOR 0xDF
#define FILE_EXPLORER_BGCOLOR 0xBF
#define LIST_BG_A 0x1D
#define LIST_BG_B 0x5E
#define LIST_BG_S 0x0A
#define LIST_TX_S 0xE7
#define LIST_LINE_HEIGHT 12

#define MENU_TEXT_COLOR 0x00
#define MENU_TEXT_SELECTED 0x8B
#define OPTIONS_PER_PAGE 11

#define GMBOX_X (LCD_WIDTH/4)
#define GMBOX_Y (LCD_HEIGHT/2-LCD_HEIGHT/8)
#define GMBOX_W (LCD_WIDTH/2)
#define GMBOX_H (LCD_HEIGHT/4)

/* Keep these headers */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <tice.h>

/* Standard headers (recommended) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <debug.h>
#include <keypadc.h>
#include <graphx.h>
#include <decompress.h>
#include <fileioc.h>

#include "gfx/element_gfx.h"
#include "gfx/num_gfx.h"
#include "gfx/misc_gfx.h"


/* Board positions: | 
	10	1 2 3	15  | (005,032) (000,000) (000,000) (000,000) (260,032)
	11	     	16  | (005,056)                               (260,056)
	12	4 5 6	17  | (005,080) (000,000) (000,000) (000,000) (260,080)
	13	     	18  | (005,104)                               (260,104)
	14	7 8 9	19  | (005,128) (000,000) (000,000) (000,000) (260,128)
	3x3 grid with 4px gaps: +0,+0: +60,+60, +120,+120 grid area 56*3+4*2=176
	Centered top left at (72,32)
	Sides are off from the edge by 4px, so X:5,260; Y:32,152
	Cards on sides are overlapping so based on Y. 5 steps is +24 per iter.
*/
#define GRIDX 72
#define GRIDY 32
#define GRIDV 60
#define PLAYERX 5
#define PLAYERY 32
#define PLAYERV 30
#define ENEMYX 260
#define ENEMYY 32
#define ENEMYV 30

int posarr[] = {
	0,0,                         //0
	GRIDX+GRIDV*0,GRIDY+GRIDV*0, //1
	GRIDX+GRIDV*1,GRIDY+GRIDV*0,
	GRIDX+GRIDV*2,GRIDY+GRIDV*0,
	GRIDX+GRIDV*0,GRIDY+GRIDV*1,
	GRIDX+GRIDV*1,GRIDY+GRIDV*1,
	GRIDX+GRIDV*2,GRIDY+GRIDV*1,
	GRIDX+GRIDV*0,GRIDY+GRIDV*2,
	GRIDX+GRIDV*1,GRIDY+GRIDV*2,
	GRIDX+GRIDV*2,GRIDY+GRIDV*2,
	
	PLAYERX,PLAYERY+PLAYERV*0, //10
	PLAYERX,PLAYERY+PLAYERV*1,
	PLAYERX,PLAYERY+PLAYERV*2,
	PLAYERX,PLAYERY+PLAYERV*3,
	PLAYERX,PLAYERY+PLAYERV*4,
	
	ENEMYX,ENEMYY+ENEMYV*0, //15
	ENEMYX,ENEMYY+ENEMYV*1,
	ENEMYX,ENEMYY+ENEMYV*2,
	ENEMYX,ENEMYY+ENEMYV*3,
	ENEMYX,ENEMYY+ENEMYV*4,
	
};


enum cardtype {monster=0,boss,gf,player};
enum element {none=0,poison,fire,wind,earth,water,ice,thunder,holy};
enum directionValues { DIR_NONE = 0,DIR_DOWN,DIR_LEFT,DIR_RIGHT,DIR_UP };
enum playRuleFlags { RULE_OPEN = 1, RULE_SAME = 2, RULE_SAMEWALL = 4,
					 RULE_SUDDENDEATH = 8, RULE_RANDOM = 16, RULE_PLUS = 32,
					 RULE_COMBO = 64, RULE_ELEMENTAL = 128 };

typedef struct card_t {
	uint8_t rank; char* name; uint8_t type;
	uint8_t up; uint8_t right; uint8_t down; uint8_t left;
	uint8_t element;
	gfx_sprite_t* img;
} card_t;

typedef struct metacard_t {
	card_t c;
	int x;
	int y;
	uint8_t playstate; //0=hiddenInHand 1=showingInhand 2=onField
	uint8_t gridpos;
	uint8_t isplayer1;
} metacard_t;

struct {
	int unsigned wins;
	int unsigned losses;
	char fn[10];  //Name of most recently-opened card pack
} stats;

	
	

/* Put your function prototypes here */
void keywait();
void waitanykey();
void ctext(char *s,uint8_t y);
void textscale2();
void textscale1();
void dmenu(char **strarr,uint8_t curopt,uint8_t maxopt);
void pcharxy(char c,int x,uint8_t y);
void drawbg();
//***
char *selectpack();  //returns variable name of pack chosen
uint8_t *getpackadr(char *varname);
uint8_t *getdataadr(uint8_t *packadr);
void getcarddata(uint8_t *packptr, uint8_t cardnum); //from file to tmpcard
void putcarddata(uint8_t cardslot);                  //tmpcard/tmpimg to slotnum
void redrawboard();  //draws game board and cards in position.
void drawcard(metacard_t *card,bool selected);
uint8_t selectfromhand(uint8_t direction); //uses direection, selcard, curplayer
metacard_t *getcardongrid(uint8_t gridpos);
void cardfight(uint8_t pidx,uint8_t eidx);


/* Put all your globals here */
uint8_t *imgpack;
uint8_t curpack,maxpack;
uint8_t gamemode;
uint8_t tmpimg[CARD_WIDTH*CARD_HEIGHT+2];
uint8_t selcard;    //0-9
uint8_t curplayer;  //0=player1, 1=player2
uint8_t ruleFlags;

card_t tmpcard;
uint8_t *elemdat[9];
metacard_t *cardbuf[10];
metacard_t tmpmeta;
gfx_sprite_t *numtiles[11];
gfx_sprite_t *cardback;

/* Put all constants here */
char *card_pack_header = "TriCrPak";
char *main_menu_text[] = {"Start Game","Card Pack Browser","Options","Quit Game"};
uint8_t main_menu_dest[] = {GM_GAMESELECT,GM_BROWSEPACK,GM_OPTIONS,GM_GAMEXIT};
uint8_t listcolors[] = {LIST_BG_A,LIST_BG_B};
char stat2char[] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};
char *cardtype[] = {"Monster","Boss","GF","Player"};
uint8_t *elemcdat[] = {	blanksym_compressed,poison_compressed,fire_compressed,
						wind_compressed,earth_compressed,water_compressed,
						ice_compressed,thunder_compressed,holy_compressed};
void main(void) {
	char *varname,*cardtypestr;
	uint8_t *sp,*packptr,*dataptr,i,j,y,copt,mopt,cpage,mpage;
	uint8_t cardposbackup;
	metacard_t card;
	metacard_t *pcard,*ecard;
	int x;
	kb_key_t k,k7;
	
	/* Initialize system */
	gfx_Begin();
	gfx_SetDrawBuffer();
	gfx_SetTransparentColor(TRANSPARENT_COLOR);
	ti_CloseAll();
	/* Initialize variables */
	cardposbackup = cpage = mpage = copt = mopt = gamemode = curpack = maxpack = 0;
	sp = packptr = dataptr = NULL;
	for (i=0;i<10;i++) cardbuf[i] = malloc(sizeof tmpmeta); //card data buffer
	dataptr = malloc((8*8+2)*11);  //sizeof 11 8x8 sprite objects
	for (i=0;i<11;i++,dataptr+=(8*8+2)) {
		dzx7_Turbo(numtiles_tiles_compressed[i],numtiles[i] =(void*) dataptr);
	}
	imgpack = malloc(((CARD_WIDTH*CARD_HEIGHT)+2)*10);      //card image buffer
	dzx7_Turbo(cardback_compressed,cardback = malloc(CARD_WIDTH*CARD_HEIGHT+2));
	
	while ( ti_Detect(&sp,card_pack_header) ) { maxpack++; }
	dataptr = malloc(9*(8*8+2));
	for(i=0;i<9;i++,dataptr+=66) dzx7_Turbo(elemcdat[i],elemdat[i]=dataptr);
	
	
	if (maxpack) {
		while (1) {
			i = randInt(0,255);  //keep randomizing
			kb_Scan();
			k = kb_Data[1];
			k7= kb_Data[7];
			if (k|k7) keywait();
			//---
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
				dataptr = getdataadr(packptr);
				copt = cpage = 0;
				mopt = (dataptr[-2]>=OPTIONS_PER_PAGE)?OPTIONS_PER_PAGE:dataptr[-2];
				mpage= dataptr[-2]/OPTIONS_PER_PAGE;
				gamemode = GM_CARDLISTER;
			}
			else if (gamemode == GM_CARDLISTER) {
				drawbg();
				textscale2();
				ctext("Card Browser",5);
				textscale1();
				gfx_PrintStringXY("Showing page ",5,30);
				gfx_PrintUInt(cpage+1,2);
				gfx_PrintString(" of ");
				gfx_PrintUInt(mpage,2);
				for(i=0,j=cpage*OPTIONS_PER_PAGE,y=50;i<OPTIONS_PER_PAGE;i++,j++,y+=LIST_LINE_HEIGHT){
					gfx_SetColor(listcolors[i&1]);
					if (i==copt) {
						gfx_SetColor(LIST_BG_S);
						gfx_SetTextFGColor(LIST_TX_S);
					}
					gfx_FillRectangle_NoClip(5,y,200,LIST_LINE_HEIGHT);
					getcarddata(packptr,j);
					if (tmpcard.rank) gfx_PrintStringXY(tmpcard.name,10,y+2);
					gfx_SetTextFGColor(MENU_TEXT_COLOR);
				}
				
				getcarddata(packptr,cpage*OPTIONS_PER_PAGE+copt);
				if (tmpcard.rank) {  //in case you browse an empty pack
					gfx_SetColor(0x00);
					cardtypestr = cardtype[tmpcard.type];
					x = 200+(120-gfx_GetStringWidth(cardtypestr))/2;
					gfx_PrintStringXY(cardtypestr,x,52);
					
					gfx_Rectangle_NoClip(234,65,CARD_WIDTH+2,CARD_HEIGHT+2);
					gfx_TransparentSprite_NoClip((gfx_sprite_t*)tmpimg,235,66);
					gfx_Rectangle_NoClip(207,127,43,40);
					gfx_PrintStringXY("Stats",210,130);
					pcharxy(stat2char[tmpcard.up]   ,220+5,140);
					pcharxy(stat2char[tmpcard.right],228+5,148);
					pcharxy(stat2char[tmpcard.down] ,220+5,156);
					pcharxy(stat2char[tmpcard.left] ,212+5,148);
					gfx_PrintStringXY("Rank ",260,130);
					gfx_PrintUInt(tmpcard.rank,2);
					gfx_PrintStringXY("Element",260,145);
					if (tmpcard.element) {
						gfx_TransparentSprite_NoClip((gfx_sprite_t*)elemdat[tmpcard.element],280,155);
					} else gfx_PrintStringXY("N/A",275,155);
				}
				if (k&kb_Mode) gamemode = GM_BROWSEPACK;
				if ((k7&kb_Up)&&copt) copt--;
				if ((k7&kb_Down)&&copt<(mopt-1)) copt++;
				if ((k7&kb_Left)&&cpage) { cpage--; mopt=OPTIONS_PER_PAGE;}
				if ((k7&kb_Right)&&(cpage<(mpage-1))) cpage++;
			}
			else if (gamemode == GM_GAMESELECT) {
				if ((packptr = getpackadr(stats.fn)) == NULL) {
					if ((varname = selectpack()) == NULL) { gamemode = GM_TITLE; continue; }
					packptr = getpackadr(varname);
					dataptr = getdataadr(packptr);
					strncpy(stats.fn,varname,9);
					stats.fn[9] = 0x00;  //ensure null terminator is added
				}
				/* DEBUGGING/TESTING */
				ruleFlags = RULE_OPEN | RULE_RANDOM;
				/* END DEBUGGING/TESTING CODE */
				
				if (ruleFlags & RULE_RANDOM) {
					for (i=0;i<10;i++) {
						getcarddata(packptr,randInt(0,dataptr[-2]));
						putcarddata(i);
						cardbuf[i]->gridpos = i+10;
						cardbuf[i]->isplayer1 = (i<5)?1:0;
						cardbuf[i]->x = posarr[(i+10)*2];
						cardbuf[i]->y = posarr[(i+10)*2+1];
						cardbuf[i]->playstate = 0;
					}
				} else {
					// We... uh. Don't really have a way of playing
					// non-random games?
					gamemode = GM_TITLE;
				}
				if (ruleFlags & RULE_OPEN) {
					for (i=0;i<10;i++) cardbuf[i]->playstate = 1;
				}
				//You'll want to prepare the board for possible play of
				//elemental rule set. Need to create an array for this.
				curplayer = 0;
				selcard = 0;
				selcard = selectfromhand(DIR_NONE); //Ensures 1st card P1 or P2
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
						// :: You want to do elemental rule card
						// :: stat modificationsat this point.
						
						//Fight without special rules
						if (i>3) cardfight(i,i-3);
						if (i<7) cardfight(i,i+3);
						if ((i-1)%3) cardfight(i,i-1);
						if (~(i-1)%3) cardfight(i,i+1);
						
						
						//
						// You'll do battle/fight logic here.
						//
						//Check amount of cards you vs other owned (including
						//unplayed card) and decide winner.
						for(i=j=k=0;i<10;i++) {
							if (cardbuf[i]->gridpos < 10) {
								if (cardbuf[i]->isplayer1) j++;
								else k++;
							}
						}
						if (j+k == 9) {
							for (i=j=k=0;i<10;i++) {
								if (cardbuf[i]->isplayer1) j++;
								else k++;
							}
							
							if (j>k) {
								gfx_PrintStringXY("Player 1 has won!",5,230);
							} else if (j<k) {
								gfx_PrintStringXY("Player 2 has won!",5,230);
							} else {
								gfx_PrintStringXY("The game ended in a draw!",5,230);
							}
							gamemode = GM_TITLE;
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
	gfx_End();
}

void keywait() { while (kb_AnyKey()); }
void waitanykey() {	keywait(); 	while (!kb_AnyKey()); keywait(); }
void ctext(char* s,uint8_t y) {	gfx_PrintStringXY(s,(LCD_WIDTH-gfx_GetStringWidth(s))/2,y); }
void textscale2() { gfx_SetTextScale(2,2); }
void textscale1() { gfx_SetTextScale(1,1); }
void dmenu(char **s,uint8_t c,uint8_t m) { uint8_t i,y; textscale2(); for(i=0,y=(240-24*m)/2;i<m;i++,y+=24) {	if (i==c) gfx_SetTextFGColor(MENU_TEXT_SELECTED); ctext(s[i],y); gfx_SetTextFGColor(MENU_TEXT_COLOR); } textscale1(); }
void pcharxy(char c,int x,uint8_t y) { gfx_SetTextXY(x,y); gfx_PrintChar(c); }
void drawbg() { gfx_FillScreen(FILE_EXPLORER_BGCOLOR); }

//***

char *selectpack() {
	uint8_t *sp,*packptr,*cardptr,i;
	int x;
	char *vn;
	kb_key_t k,k7;
	
	vn = NULL;
	while (1) {
		kb_Scan();
		k = kb_Data[1];
		k7= kb_Data[7];
		drawbg();
		
		vn = NULL;
		for (i=0,sp=NULL;i<(curpack+1);i++,vn=ti_Detect(&sp,card_pack_header));
		if (!vn) return NULL;
		packptr = getpackadr(vn);
		cardptr = getdataadr(packptr);
		
		textscale2();
		ctext("Card Pack Selection",5);
		textscale1();
		gfx_PrintStringXY("Displaying pack ",5,30);
		gfx_PrintUInt(curpack+1,3);
		gfx_PrintString(" of ");
		gfx_PrintUInt(maxpack,3);
		
		ctext((char*)(packptr+17),70);
		gfx_PrintStringXY("Filename: ",5,85);
		gfx_PrintString(vn);
		gfx_PrintString(", descriptor: ");
		gfx_PrintString((char*)(packptr+8));
		gfx_PrintStringXY("Number of cards: ",5,95);
		gfx_PrintUInt(cardptr[-2],3);
		ctext("Card pack preview",110);
		for(i=0,x=(LCD_WIDTH-(CARD_WIDTH+4)*5)/2;i<5;i++,x+=CARD_WIDTH+4) {
			getcarddata(packptr,i);
			if (tmpcard.rank) {
				gfx_SetColor(0x00);
				gfx_Rectangle_NoClip(x-1,119,CARD_WIDTH+2,CARD_HEIGHT+2);
				gfx_TransparentSprite_NoClip((gfx_sprite_t*)tmpimg,x,120);
			} else {
				gfx_SetColor(FILE_EXPLORER_BGCOLOR);
				gfx_FillRectangle_NoClip(x-1,119,CARD_WIDTH+2,CARD_HEIGHT+2);
			}
		}
		gfx_SwapDraw();
		
		if (k|k7) keywait();
		if (k&kb_Mode) return NULL;
		if (k&kb_2nd) return vn;
		if ((k7&(kb_Left|kb_Up))&&curpack) curpack--;
		if ((k7&(kb_Right|kb_Down))&&(curpack<(maxpack-1))) curpack++;
	}
}

uint8_t *getpackadr(char *vn) {
	uint8_t *packptr;
	ti_var_t f;
	if (f = ti_Open(vn,"r")) {
		packptr = ti_GetDataPtr(f);
		ti_Close(f);
		return packptr;
	} else { return NULL;}
}

uint8_t *getdataadr(uint8_t *pptr) {
	uint8_t i;
	for(i=0;*(pptr+17+i);i++);
	return pptr+(17+1+2)+i;
}

//on return, check output card rank. Failed to locate card if rank==0
void getcarddata(uint8_t *pptr, uint8_t cardnum) {
	uint8_t *cptr,fmt,i;
	
	tmpcard.rank = 0;
	cptr = getdataadr(pptr);
	fmt = cptr[-1];
	if (cardnum >= cptr[-2]) return;
	
	if ( 0 == fmt ) {
		//11b, 2b img offset
		cptr += cardnum*11;
		tmpcard.rank = cptr[0];
		tmpcard.name = (char*)(pptr + *((uint16_t*)(cptr+1)));
		tmpcard.type = cptr[3];
		tmpcard.up   = cptr[4];
		tmpcard.right= cptr[5];
		tmpcard.down = cptr[6];
		tmpcard.left = cptr[7];
		tmpcard.element=cptr[8];
		tmpcard.img  = (gfx_sprite_t*)tmpimg;
		//dbg_sprintf(dbgout,"img %i adr %x\n",cardnum,pptr+*((uint16_t*)(cptr+9)));
		dzx7_Turbo(pptr+*((uint16_t*)(cptr+9)),tmpimg+2);
		tmpimg[0] = CARD_WIDTH;
		tmpimg[1] = CARD_HEIGHT;
	}
	else if (1 == fmt) {
		//12b, 1b file id, 2b img offset
	}
}

 //tmpcard/tmpimg to slotnum
void putcarddata(uint8_t cardslot) {
	uint8_t i;
	uint8_t *imgpckptr;
	metacard_t *carddata;
	
	//load image data to image pack
	imgpckptr = imgpack+(sizeof(tmpimg)*cardslot);
	memcpy(imgpckptr,tmpimg,sizeof tmpimg);
	//load card data to card pack
	tmpcard.img = (void*) imgpckptr;
	carddata = cardbuf[cardslot];
	memset(carddata,0,sizeof carddata);
	memcpy(&carddata->c,&tmpcard,sizeof tmpcard);
}

void redrawboard() {
	uint8_t i,t;
	int x,y;
	gfx_FillScreen(GAMEBOARD_BG);
	
	gfx_SetColor(0x00);  //set later to card border color
	for (i=2;i<20;i+=2) {
		gfx_Rectangle_NoClip(posarr[i],posarr[i+1],CARD_WIDTH+4,CARD_HEIGHT+4);
	}
	
	for (i=0;i<10;i++) {
		if (i != selcard) drawcard(cardbuf[i],0);
	}
	if (selcard<10) drawcard(cardbuf[selcard],1);
	
}

void drawcard(metacard_t *card, bool selected) {
		int x,y,cx,cy;
		uint8_t gpos;
		card_t *cdata;
		
		x = card->x;
		y = card->y;
		cx = posarr[card->gridpos*2];
		cy = posarr[card->gridpos*2+1];
		if (x != cx) x = cx;  //Later change these to fancier compares to let
		if (y != cy) y = cy;  //cards slide across the game board
		card->x = x;
		card->y = y;
		
		cdata = &card->c;
		if (selected) {
			gpos = card->gridpos;
			if (gpos > 9 && gpos < 15) x+=5;
			if (gpos >14) x-=5;
		}
		gfx_SetColor((card->isplayer1)?PLAYER1_BG:PLAYER2_BG);
		gfx_FillRectangle_NoClip(x+1,y+1,CARD_WIDTH+2,CARD_HEIGHT+2);
		if (card->playstate >0 || selected) {
			//if card is showing
			gfx_TransparentSprite_NoClip(cdata->img,x+2,y+2);
			
			gfx_TransparentSprite_NoClip(numtiles[cdata->up],x+2+8,y+2+0);
			gfx_TransparentSprite_NoClip(numtiles[cdata->right],x+2+16,y+2+8);
			gfx_TransparentSprite_NoClip(numtiles[cdata->down],x+2+8,y+2+16);
			gfx_TransparentSprite_NoClip(numtiles[cdata->left],x+2,y+2+8);
			
			if (cdata->element) {
				gfx_TransparentSprite_NoClip((gfx_sprite_t*)elemdat[cdata->element],x+44,y+2);
			}
		}
		else {
			//If card is in defense mode
			gfx_TransparentSprite_NoClip(cardback,x+2,y+2);
		}
		
		if (selected) {
			gfx_SetColor(CARD_SEL_FG);
			gfx_Rectangle_NoClip(x+1,y+1,CARD_WIDTH+2,CARD_HEIGHT+2);
		} else {
			gfx_SetColor(0x00);  //set later to card border color
		}
		gfx_Rectangle_NoClip(x,y,CARD_WIDTH+4,CARD_HEIGHT+4);

}

//returns 0-9 for selectable card slot, or -1 if card not findable.
uint8_t selectfromhand(uint8_t direction) {
	uint8_t i,t,found;
	metacard_t *curcard;
	found = 255;
	
	for (i=0;i<10;i++) {
		curcard = cardbuf[i];
		if ((curcard->isplayer1==curplayer)||(curcard->playstate > 1)) continue;
		if (direction == DIR_UP) {
			if (i>=selcard) break;
			found = i;
		}
		else if (direction == DIR_DOWN) {
			found = i;
			if (i>selcard) break;
		}
		else {
			found = i;
			if (i>=selcard) break;
		}
	}
	return found;
}

metacard_t *getcardongrid(uint8_t gridpos) {
	uint8_t i;
	metacard_t *card;
	
	for(i=0;i<10;i++) {
		card = cardbuf[i];
		if (card->gridpos == gridpos) return card;
	}
	return NULL;
}

void cardfight(uint8_t pidx,uint8_t eidx) {
	uint8_t i;
	int8_t prank,erank;
	metacard_t *pcard,*ecard;
	
	pcard = getcardongrid(pidx);
	if ((ecard=getcardongrid(eidx)) == NULL) return;
	if (pcard->isplayer1 == ecard->isplayer1) return;
	i = eidx - pidx; //up: 253, left: 255, right: 1, down: 3
	if (i==253) { //attack up. player top, enemy bottom.
		prank = pcard->c.up;
		erank = ecard->c.down;
	} else if (i==255) { //attack left. player left, enemy right.
		prank = pcard->c.left;
		erank = ecard->c.right;
	} else if (i==1) { //attack right. player right, enemy left.
		prank = pcard->c.right;
		erank = ecard->c.left;
	} else if (i==3) { //attack down. player down, enemy up
		prank = pcard->c.down;
		erank = ecard->c.up;
	} else prank = erank = 0;
	
	if (prank>erank) ecard->isplayer1 = pcard->isplayer1;
	
	
	
}