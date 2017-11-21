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
#define LIST_BACKGROUND_A 0x37
#define LIST_BACKGROUND_B 0x55
#define LIST_BACKGROUND_S 0x09

#define MENU_TEXT_COLOR 0x00
#define MENU_TEXT_SELECTED 0x0C


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

enum cardtype {monster=0,boss,gf,player};
enum element {none=0,poison,fire,wind,earth,water,ice,thunder,holy};

typedef struct card_t {
	uint8_t rank; char* name; uint8_t type;
	uint8_t top; uint8_t right; uint8_t down; uint8_t left;
	uint8_t element;
	gfx_sprite_t* img;
} card_t;

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
void drawbg();
//***
char *selectpack();  //returns variable name of pack chosen
uint8_t *getpackadr(char *varname);
uint8_t *getdataadr(uint8_t *packadr);
void getcarddata(uint8_t *packptr, uint8_t cardnum);




/* Put all your globals here */
uint8_t *imgpack;
uint8_t curpack,maxpack;
uint8_t gamemode;
uint8_t tmpimg[CARD_WIDTH*CARD_HEIGHT+2];
card_t tmpcard;

/* Put all constants here */
char *card_pack_header = "TriCrPak";
char *main_menu_text[] = {"Start Game","Card Pack Browser","Options","Quit Game"};
uint8_t main_menu_dest[] = {GM_GAMESELECT,GM_BROWSEPACK,GM_OPTIONS,GM_GAMEXIT};



void main(void) {
	char *varname;
	uint8_t *sp,*packptr,i,copt,mopt;
	kb_key_t k,k7;
	
	
	/* Initialize system */
	gfx_Begin();
	gfx_SetDrawBuffer();
	gfx_SetTransparentColor(TRANSPARENT_COLOR);
	ti_CloseAll();
	/* Initialize variables */
	copt = mopt = gamemode = curpack = maxpack = 0;
	sp = NULL;
	imgpack = malloc(((CARD_WIDTH*CARD_HEIGHT)+2)*10);
	while ( ti_Detect(&sp,card_pack_header) ) { maxpack++; }
	
	if (maxpack) {
		while (1) {
			kb_Scan();
			k = kb_Data[1];
			k7= kb_Data[7];
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
				
				
				
				
				
				
				break;
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

//check card rank. Is zero if failure
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
		tmpcard.top  = cptr[4];
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

