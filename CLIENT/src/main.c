//
// This file will eventually become the new main.c file.
// 
//

#define VERSION_INFO "v0.1"

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

#include "main.h"
#include "types.h"
#include "const.h"
#include "files.h"
#include "common.h"
#include "game.h"
#include "viewer.h"

#include "gfx/element_gfx.h"
#include "gfx/num_gfx.h"
#include "gfx/misc_gfx.h"

uint8_t *elemcdat[] = {	blanksym_compressed,poison_compressed,fire_compressed,
						wind_compressed,earth_compressed,water_compressed,
						ice_compressed,thunder_compressed,holy_compressed};
char *main_menu_text[] = {"Start Game","Card Pack Browser","Options","Quit Game"};
char *option_menu_text[] = {"Go back","","",""};

metacard_t mc_tmp;
metacard_t *cardbuf[10];
gfx_sprite_t *imgpack[10];
gfx_sprite_t *numtiles[numtiles_tiles_num];
gfx_sprite_t *cardback;
gfx_sprite_t *elemgfx[NUM_ELEMENTS];
stats_t stats;

void main(void) {
	uint8_t *ptr,curpack,numpacks,i,curopt,maxopt,gamemode;
	kb_key_t kc,kd;
	//Check if we have any card packs. If not, quit.
	if (!(numpacks = getnumpacks())) {
		os_ClrHomeFull();
		             //v------------------------vv------------------------vv------------------------v
		os_PutStrFull("A TriCard card pack wasn'tfound. Please download a  card pack before playing.");
		waitanykey();
		return;
	}
	//Initialize libraries
	gfx_Begin();
	gfx_SetDrawBuffer();
	gfx_SetTransparentColor(TRANSPARENT_COLOR);
	ti_CloseAll();
	//Malloc metacard and image space
	for (i=0;i<10;i++) {
		cardbuf[i] = malloc(sizeof mc_tmp);
		imgpack[i] = malloc((CARD_WIDTH*CARD_HEIGHT)+2);
	}
	//Init number tile graphics
	ptr = malloc(66*numtiles_tiles_num);
	for (i=0;i<numtiles_tiles_num;i++,ptr+=66) {
		dzx7_Turbo(numtiles_tiles_compressed[i],numtiles[i]=(gfx_sprite_t*) ptr);
	}
	//Init card back graphics
	dzx7_Turbo(cardback_compressed,cardback=malloc((CARD_WIDTH*CARD_HEIGHT)+2));
	//Init element symbol grapics
	for(i=0;i<NUM_ELEMENTS;i++) {
		dzx7_Turbo(elemcdat[i],elemgfx[i] = malloc(66));
	}
	//Init variables
	maxopt = curopt = gamemode = 0;
	
	//Menus, card browser, game.
	do {
		randInt(0,255);  //Keep the randomizer rolling
		kb_Scan();
		kc = kb_Data[1];
		kd = kb_Data[7];
		if (kc|kd) keywait();
		if (kd&(kb_Up|kb_Left))    curopt--;
		if (kd&(kb_Down|kb_Right)) curopt++;
		gfx_FillScreen(TITLE_BG);
		if (gamemode == GM_TITLE) {
			if (kc&kb_2nd) {
				switch(curopt) {
					case 0:
						//Start game. play with current options or force
						//card pack selection if none was found
						break;
					case 1:
						//Card pack browser
						viewpack(selectpack());
						break;
					case 2:
						gamemode = GM_OPTIONS;
						curopt = 0;
						break;
					default:
						kc = kb_Mode;  //send keypress QUIT
						break;
				}
				continue;
			}
			curopt &= 3;
			textscale2();
			ctext("TriCards",5);
			dmenu(main_menu_text,curopt,4);
		} else if (gamemode == GM_OPTIONS) {
			if (kc&kb_2nd) {
				switch(curopt) {
					default:
						kc = kb_Mode;
						break;
				}
			}
			if (curopt>128) curopt = 0;
			if (curopt>0) curopt = 0;
			if (kc&kb_Mode) { curopt = 2; kc = 0; gamemode = GM_TITLE; }
			textscale2();
			ctext("Game Options",5);
			dmenu(option_menu_text,curopt,1);
		}
		textscale1();
		gfx_PrintStringXY(VERSION_INFO,290,230);
		gfx_SwapDraw();
	} while (kc!=kb_Mode);
	//Here, you should add in stats file saving
	gfx_End();
	return;
}
	
	
	
