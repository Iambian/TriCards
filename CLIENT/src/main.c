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
#define GM_GAMEXIT 255

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
#define PLAYERV 24
#define ENEMYX 260
#define ENEMYY 32
#define ENEMYV 24

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


enum cardtype {monster=0,boss,gf,player};
enum element {none=0,poison,fire,wind,earth,water,ice,thunder,holy};

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




/* Put all your globals here */
uint8_t *imgpack;
uint8_t curpack,maxpack;
uint8_t gamemode;
uint8_t tmpimg[CARD_WIDTH*CARD_HEIGHT+2];

card_t tmpcard;
card_t selcard;
uint8_t *elemdat[9];
metacard_t *cardbuf[10];
metacard_t tmpmeta;

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
	int x;
	kb_key_t k,k7;
	
	
	/* Initialize system */
	gfx_Begin();
	gfx_SetDrawBuffer();
	gfx_SetTransparentColor(TRANSPARENT_COLOR);
	ti_CloseAll();
	/* Initialize variables */
	cpage = mpage = copt = mopt = gamemode = curpack = maxpack = 0;
	sp = packptr = dataptr = NULL;
	for (i=0;i<10;i++) cardbuf[i] = malloc(sizeof tmpmeta); //card data buffer
	imgpack = malloc(((CARD_WIDTH*CARD_HEIGHT)+2)*10);      //card image buffer
	while ( ti_Detect(&sp,card_pack_header) ) { maxpack++; }
	dataptr = malloc(9*(8*8+2));
	for(i=0;i<9;i++,dataptr+=66) dzx7_Turbo(elemcdat[i],elemdat[i]=dataptr);
	
	
	if (maxpack) {
		while (1) {
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
				//A game mode should have been selected, but let's go full
				//random in our testing.
				for (i=0;i<10;i++) {
					getcarddata(packptr,randInt(0,dataptr[-2]));
					putcarddata(i);
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
	carddata = cardbuf[cardslot];
	memset(carddata,0,sizeof carddata);
	memcpy(&carddata->c,&tmpcard,sizeof tmpcard);
}







