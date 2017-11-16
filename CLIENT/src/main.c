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

#define TRANSPARENT_COLOR 0xF8
#define GREETINGS_DIALOG_TEXT_COLOR 0xDF
#define FILE_EXPLORER_BGCOLOR 0xBF

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

typedef struct card {
	uint8_t rank;
	char* name;
	uint8_t type;
	uint8_t top;
	uint8_t right;
	uint8_t down;
	uint8_t left;
	uint8_t element;
	gfx_sprite_t* img;
}

/* Put your function prototypes here */

void keywait();
void waitanykey();
void centerxtext(char *str,uint8_t ypos);



/* Put all your globals here */
gfx_sprite_t* playercards[5];
gfx_sprite_t* enemycards[5];
uint8_t *tenimgstore,gamemode,curopt,maxopt,curpage,maxpage,curpack,maxpack;
uint8_t *cardpack;

char *cardpacktype = "TriCrPak"

void main(void) {
	uint8_t i,*search_pos;
	kb_key_t k,k7;
	char *varname;
	
	
	/* Initialize system */
	gfx_Begin();
	gfx_SetDrawBuffer();
	gfx_SetTransparentColor(TRANSPARENT_COLOR);
	
	/* Initialize variables */
	tenimgstore = malloc(((48*32)+2)*10);
	search_pos = cardpack = NULL;
	
	
	gamemode = GM_CARDPACKSELECT;
	curpack = maxpack = 0;
	
	
	//
	// FIGURE OUT HOW MANY CARD PACKS AND SET curpack/maxpack with it.
	//
	
	
	
	while (1) {
		kb_Scan();
		if (gamemode==0) {
			break;
		}
		else if (gamemode==GM_CARDPACKSELECT) {
			gfx_FillScreen(FILE_EXPLORER_BGCOLOR);
			gfx_SetTextScale(2,2);
			centerxtext("Card Pack Selection",5);
			//
			//Show card pack details
			//
			
			k = kb_Data[1];
			k7= kb_Data[7];
			if (k&kb_Mode) gamemode=0;
			if (k&kb_2nd); //***TODO: SET CUR FILE POINTER AND MOVE TO CARD VIEWER
			if ((k7&(kb_Left|kb_Up))&&curpack) curpack--;
			if ((k7&(kb_Right|kb_Down))&&(curpack<maxpack)) curpack++;			
			if (k7) {
				//
				// Change to next card pack by resetting iterator and iterate 
				// for as many as in variable
				//
			}
			if (k|k7) keywait();
		}
	}
	gfx_End();
}

/* Put other functions here */

void keywait() {
	while (kb_AnyKey());
}

void waitanykey() {
	keywait();
	while (!kb_AnyKey());
	keywait();
}

void centerxtext(char* str,int ypos) {
	gfx_PrintStringXY(str,(LCD_WIDTH-gfx_GetStringWidth(str))/2,ypos);
}

