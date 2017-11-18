/*
 *--------------------------------------
 * Program Name: TRICARDS
 * Author: rawrf.
 * License: rawrf.
 * Description: rawrf.
 *--------------------------------------
*/

#define VERSION_INFO "v0.1"

#define GM_CARDPACKSELECT 4
#define GM_CARDLISTER 5

#define CARD_WIDTH 52
#define CARD_HEIGHT 52

#define TRANSPARENT_COLOR 0xFF
#define GREETINGS_DIALOG_TEXT_COLOR 0xDF
#define FILE_EXPLORER_BGCOLOR 0xBF
#define LIST_BACKGROUND_A 0x37
#define LIST_BACKGROUND_B 0x55
#define LIST_BACKGROUND_S 0x09

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
	uint8_t rank;
	char* name;
	uint8_t type;
	uint8_t top;
	uint8_t right;
	uint8_t down;
	uint8_t left;
	uint8_t element;
	gfx_sprite_t* img;
} card_t;

/* Put your function prototypes here */

char *getcardpack(uint8_t packnum);
void unpackcardgfx(int cardnum,uint8_t slot);

void keywait();
void waitanykey();
void centerxtext(char *str,uint8_t ypos);



/* Put all your globals here */
gfx_sprite_t* playercards[5];
gfx_sprite_t* enemycards[5];
uint8_t *tenimgstore,gamemode,curopt,maxopt,curpage,maxpage,curpack,maxpack;
uint8_t *cardpack,*carddatastream;
uint16_t numcards;

uint8_t alternatingfilebgcolors[] = {LIST_BACKGROUND_A,LIST_BACKGROUND_B};
char *cardpackheader = "TriCrPak";
char card_statmap[] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};

void main(void) {
	uint8_t *search_pos,*cur_card,*tcard,i,y,color;
	unsigned int t;
	kb_key_t k,k7;
	char varnamebak[10];
	char *varname;
	
	/* Initialize system */
	gfx_Begin();
	gfx_SetDrawBuffer();
	gfx_SetTransparentColor(TRANSPARENT_COLOR);
	ti_CloseAll();
	
	/* Initialize variables */
	tenimgstore = malloc(((CARD_WIDTH*CARD_HEIGHT)+2)*10);
	cur_card = tcard = search_pos = cardpack = NULL;
	curpack = maxpack = 0;
	
	while (1) {
		if ((varname = ti_Detect(&search_pos,cardpackheader)) == NULL) break;
		else maxpack++;
	}
	
	/* Testing mode */
	gamemode = GM_CARDPACKSELECT;
	getcardpack(0);
	
	if (!maxpack) {
		gfx_FillScreen(FILE_EXPLORER_BGCOLOR);
		centerxtext("ERROR",80);
		centerxtext("You need to have a card pack",90);
		centerxtext("installed before you can play",100);
		centerxtext("Check /BUILDER/bin/ for packs",110);
		gfx_SwapDraw();
		waitanykey();
	} else {
		while (1) {
			kb_Scan();
			if (gamemode==0) {
				break;
			}	
			else if (gamemode==GM_CARDPACKSELECT) {
				varname = getcardpack(curpack);
				gfx_FillScreen(FILE_EXPLORER_BGCOLOR);
				gfx_SetTextScale(2,2);
				centerxtext("Card Pack Selection",5);
				gfx_SetTextScale(1,1);
				gfx_PrintStringXY("Displaying pack ",5,30);
				gfx_PrintUInt(curpack+1,3);
				gfx_PrintString(" of ");
				gfx_PrintUInt(maxpack,3);
				centerxtext((char*)(cardpack+17),50);
				gfx_PrintStringXY("Pack filename: ",5,60);
				gfx_PrintString(varname);
				gfx_PrintStringXY("Pack descriptor: ",5,70);
				gfx_PrintString((char*)(cardpack+8));
				gfx_PrintStringXY("Number of cards: ",5,80);
				for(i=0;*(cardpack+17+i);i++);
				numcards = *((uint16_t*)(cardpack+(17+1)+i));
				gfx_PrintUInt(numcards,3);
				gfx_SwapDraw();
				k = kb_Data[1];
				k7= kb_Data[7];
				if (k&kb_Mode) gamemode=0;
				if (k&kb_2nd) {
					carddatastream = cardpack+(17+1+2)+i;
					curopt = 0;
					maxopt = (numcards>=10)?10:numcards;
					curpage = 0;
					maxpage = numcards/10;
					gamemode = GM_CARDLISTER;
					color = LIST_BACKGROUND_A;
				}
				if ((k7&(kb_Left|kb_Up))&&curpack) curpack--;
				if ((k7&(kb_Right|kb_Down))&&(curpack<(maxpack-1))) curpack++;
				if (k7);
				if (k|k7) keywait();
			}
			else if (gamemode==GM_CARDLISTER) {
				gfx_FillScreen(FILE_EXPLORER_BGCOLOR);
				gfx_SetTextScale(2,2);
				centerxtext("Card Browser",5);
				gfx_SetTextScale(1,1);
				centerxtext((char*)(cardpack+17),30);
				y = 45;
				for (i=0;i<10;i++,y+=12) {
					gfx_SetColor(alternatingfilebgcolors[i&1]);
					gfx_SetTextFGColor(0x00);
					if (i==curopt) {
						gfx_SetColor(LIST_BACKGROUND_S);
						gfx_SetTextFGColor(0xEF);
					}
					gfx_FillRectangle_NoClip(5,y,310,12);
					if ((t = curpage*10+i)<numcards) {
						tcard = carddatastream+10*t;
						if (i == curopt) cur_card = tcard;
						gfx_PrintStringXY((char*)(cardpack+ *((uint16_t*)(tcard+1))),10,y+2);
					}
				}
				unpackcardgfx(curpage*10+curopt,0);
				gfx_SetColor(0x00);
				gfx_Rectangle_NoClip(4,169,CARD_WIDTH+2,CARD_HEIGHT+2);
				gfx_TransparentSprite((gfx_sprite_t*)tenimgstore,5,170);
				gfx_SetTextFGColor(0x00);
				
				gfx_SetTextXY(70,188);
				gfx_PrintChar(card_statmap[cur_card[6]]); //left
				gfx_SetTextXY(78,180);
				gfx_PrintChar(card_statmap[cur_card[3]]); //up
				gfx_SetTextXY(78,196);
				gfx_PrintChar(card_statmap[cur_card[5]]); //down
				gfx_SetTextXY(86,188);
				gfx_PrintChar(card_statmap[cur_card[4]]); //right
				
				
				gfx_PrintStringXY("Rank: ",100,180);
				gfx_PrintUInt(cur_card[0],2);
				
				//
				//TODO: DISPLAY CARD ELEMENT, FIX COLOR PALETTE BUG IN CONVERTER
				//
				
				gfx_SwapDraw();
				k = kb_Data[1];
				k7= kb_Data[7];
				if (k&kb_Mode) gamemode=GM_CARDPACKSELECT;
				if ((k7&kb_Up)&&curopt) curopt--;
				if ((k7&kb_Down)&&curopt<(maxopt-1)) curopt++;
				if ((k7&kb_Left)&&curpage) { curpage--;curopt=0; }
				if ((k7&kb_Right)&&(curpage<(maxpage-1))) { curpage++;curopt=0;}
				if (k|k7) keywait();
				
				
			}
		}
	}
	gfx_End();
}

/* Put other functions here */

char *getcardpack(uint8_t packnum) {
	uint8_t *sp;
	char *vn;
	ti_var_t f;
	sp = NULL;
	
	do {
		vn = ti_Detect(&sp,cardpackheader);
		packnum--;
	} while (packnum!=255);
	
	f = ti_Open(vn,"r");
	cardpack = ti_GetDataPtr(f);
	ti_Close(f);
	return vn;
}
void unpackcardgfx(int cardnum,uint8_t slot) {
	uint8_t *adr;
	adr = tenimgstore + (((unsigned int)(slot))*(CARD_WIDTH*CARD_HEIGHT+2));
	adr[0] = CARD_WIDTH;
	adr[1] = CARD_HEIGHT;
	dzx7_Turbo((void*)(cardpack+ *((uint16_t*)(carddatastream+(10*cardnum)+8))),adr+2);
}

void keywait() {
	while (kb_AnyKey());
}

void waitanykey() {
	keywait();
	while (!kb_AnyKey());
	keywait();
}

void centerxtext(char* str,uint8_t ypos) {
	gfx_PrintStringXY(str,(LCD_WIDTH-gfx_GetStringWidth(str))/2,ypos);
}

