
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

int posx[] = {
	0,             //0
	GRIDX+GRIDV*0,GRIDX+GRIDV*1,GRIDX+GRIDV*2,
	GRIDX+GRIDV*0,GRIDX+GRIDV*1,GRIDX+GRIDV*2,
	GRIDX+GRIDV*0,GRIDX+GRIDV*1,GRIDX+GRIDV*2,	
	PLAYERX,PLAYERX,PLAYERX,PLAYERX,PLAYERX,
	ENEMYX,ENEMYX,ENEMYX,ENEMYX,ENEMYX,
};

uint8_t posy[] = {
	0,
	GRIDY+GRIDV*0,GRIDY+GRIDV*0,GRIDY+GRIDV*0,
	GRIDY+GRIDV*1,GRIDY+GRIDV*1,GRIDY+GRIDV*1,
	GRIDY+GRIDV*2,GRIDY+GRIDV*2,GRIDY+GRIDV*2,
	PLAYERY+PLAYERV*0,PLAYERY+PLAYERV*1,PLAYERY+PLAYERV*2,PLAYERY+PLAYERV*3,PLAYERY+PLAYERV*4,
	ENEMYY+ENEMYV*0,ENEMYY+ENEMYV*1,ENEMYY+ENEMYV*2,ENEMYY+ENEMYV*3,ENEMYY+ENEMYV*4,
};

uint8_t colorslider[] = {
	PLAYER1_BG, //0xF4
	0xF5,0xF6,0xF7,0xFE,0xDF,0xBF,
	PLAYER2_BG //9F
};

//Uses cardbuf from main.h
uint8_t startGame(char *packname,uint8_t rules) {
	uint8_t *packptr,*dataptr,i,numcards,gamestate;
	metacard_t *curcard;
	void *ptr;
	uint8_t elementgrid[9];
	
	if ((dataptr = getdataadr(packptr = getpackadr(packname))) == NULL) return RESULT_QUIT;
	numcards = dataptr[-2];
	
	
	
	if (rules & RULE_RANDOM) {
		for(i=0;i<10;i++) {
			curcard = cardbuf[i];
			getcarddata(packptr,randInt(0,numcards-1),&curcard->c,imgpack[i]);
			curcard->gridpos = i+10;
			curcard->isplayer1 = (i<5)?1:0;
			curcard->x = posx[i+10];
			curcard->y = posy[i+10];
			curcard->playstate = (rules & RULE_OPEN) ? 1 : 0;
			curcard->color = 0;
		}
	} else { return RESULT_QUIT;} //not equipped to deal with non-random
	
	
	memset(&elementgrid,0,9);
	if (rules & RULE_ELEMENTAL) {
		for (i=0;i<9;i++) {
			if (!randInt(0,4)) elementgrid[i] = randInt(0,7)+1;
		}
	}
	
	
	//curplayer = 0;
	//selcard = 0;
	//selcard = selectfromhand(DIR_NONE); //Ensures 1st card P1 or P2
	//keywait();
	gamestate = GM_SELECTINGCARDS
	
	
	
	
	return 0;
}









