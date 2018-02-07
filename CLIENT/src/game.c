
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

uint8_t colorslider[] = {
	PLAYER1_BG, //0xF4
	0xF5,
	0xF6,
	0xF7,
	0xFE,
	0xDF,
	0xBF,
	PLAYER2_BG //9F
};











