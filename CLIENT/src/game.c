
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

/* Define globals/constants here */
uint8_t selcard,curplayer,issuddendeath;

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
uint8_t elementgrid[9];

/* Define function prototypes here */
uint8_t gethandcardidx(uint8_t direction);
metacard_t *getgridcard(uint8_t gridpos);
void cardfight(uint8_t p_idx, uint8_t e_idx);
void redrawboard(void);
void gamewait();
void gamewaitany();


//Uses cardbuf from main.h
uint8_t startGame(char *packname,uint8_t rules) {
	uint8_t *packptr,*dataptr,i,numcards,gamestate,cardposbackup;
	metacard_t *curcard;
	void *ptr;
	kb_key_t kc,kd;
	
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
	
	curplayer = 0;
	selcard = 0;
	selcard = gethandcardidx(DIR_NONE); //Ensures 1st card P1 or P2
	//keywait();
	gamestate = GM_SELECTINGCARDS;
	
	//DEBUG DUMMY CALL
	redrawboard();
	
	while (1) {
		kb_Scan();
		kc = kb_Data[1];
		kd = kb_Data[7];
		curcard = cardbuf[selcard];
		if (gamestate == GM_SELECTINGCARDS) {
			if (kc&kb_2nd) {
				cardposbackup = curcard->gridpos;
				curcard->gridpos = 5;
				gamestate = GM_SELECTINGPLACE;
			}
			i = 255;
			if (kd&kb_Up) i = gethandcardidx(DIR_UP);
			if (kd&kb_Down) i = gethandcardidx(DIR_DOWN);
			if (i<10) selcard = i;
		} else if (gamestate == GM_SELECTINGPLACE) {
			
			
		}
		if (kb_Data[2]&kb_Math && kb_Data[6]&kb_Clear) return RESULT_QUIT;
		redrawboard();
		if (kc|kd) gamewait();
		
		
		
	}
	return 0;
}

void gamewait() {
	while (kb_AnyKey()) redrawboard();
}
void gamewaitany() {
	gamewait();
	while (!kb_AnyKey()) redrawboard();
	gamewait();
}


//returns 0-9 for selectable card slot, or -1 if card not findable.
//Uses globals: curplayer, selcard
uint8_t gethandcardidx(uint8_t direction) {
	uint8_t i,t,found;
	metacard_t *card;
	found = 255;
	
	for (i=0;i<10;i++) {
		card = cardbuf[i];
		if ((card->isplayer1==curplayer)||(card->playstate > 1)) continue;
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

metacard_t *getgridcard(uint8_t gridpos) {
	uint8_t i;
	metacard_t *card;
	
	for(i=0;i<10;i++) {
		card = cardbuf[i];
		if (card->gridpos == gridpos) return card;
	}
	return NULL;
}

void cardfight(uint8_t p_idx, uint8_t e_idx) {
	uint8_t i;
	int8_t prank,erank;
	metacard_t *pcard,*ecard;
	
	pcard = getgridcard(p_idx);
	if ((ecard=getgridcard(e_idx)) == NULL) return;
	if (pcard->isplayer1 == ecard->isplayer1) return;
	i = e_idx - p_idx; //up: 253, left: 255, right: 1, down: 3
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
	
	if (prank>erank) {
		ecard->isplayer1 = pcard->isplayer1;
		if (issuddendeath==1) issuddendeath++;
	}
}

/* =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= */


int adjpos(int curp,int newp) {
	int d;
	if (curp==newp || !(d = (newp-curp) / 2)) return newp;
	return curp+d;
}

void drawcard(metacard_t *card,bool selected) {
	int x,y,nx,ny,t;
	uint8_t gridpos,curcolor,i,flag,defaultcolor;
	card_t *carddata;
	gridpos = card->gridpos;
	carddata = &card->c;
	//Adjust position
	card->x = x = adjpos(card->x,nx=posx[gridpos]);
	card->y = y = adjpos(card->y,ny=posy[gridpos]);
	//Adjust color
	flag = 0;
	curcolor = card->color;
	defaultcolor = (card->isplayer1)?PLAYER1_BG:PLAYER2_BG;
	if (curcolor != defaultcolor) {
		if (card->isplayer1) {
			for (i=7;i;i--) {
				if (curcolor == colorslider[i]) {
					curcolor = colorslider[i-1];
					flag++;
					break;
				}
			}
		} else {
			for (i=0;i<7;i++) {
				if (curcolor == colorslider[i]) {
					curcolor = colorslider[i+1];
					flag++;
					break;
				}
			}
		}
	}
	if (!flag) curcolor = defaultcolor;
	card->color = curcolor;
	//Adjust offset in case it is a card being held and selected.
	if (selected) {
		if (gridpos>9 && gridpos<15) x+=5;
		if (gridpos>14) x-=5;
	}
	gfx_SetColor(curcolor);
	gfx_FillRectangle_NoClip(x+1,y+1,CARD_WIDTH+2,CARD_HEIGHT+2);
	if (card->playstate >0 || selected) {  //if card is showing
		gfx_TransparentSprite_NoClip(carddata->img,x+2,y+2);
		gfx_TransparentSprite_NoClip(numtiles[carddata->up],x+(2+8-3),y+(2+0));
		gfx_TransparentSprite_NoClip(numtiles[carddata->right],x+(2+16-3-3),y+(2+8));
		gfx_TransparentSprite_NoClip(numtiles[carddata->down],x+(2+8-3),y+(2+16));
		gfx_TransparentSprite_NoClip(numtiles[carddata->left],x+2,y+(2+8));
		if (carddata->element) {
			gfx_TransparentSprite_NoClip((gfx_sprite_t*)elemgfx[carddata->element],x+44,y+2);
		}
	} else {  //card is not showing
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

void redrawboard(void) {
	uint8_t i,j,temp,y;
	int x;
	//Draw background
	gfx_FillScreen(GAMEBOARD_BG);
	gfx_SetColor(0);
	//Redraw elemental affinity symbols and render card grid borders on-field
	for (i=0,j=1;i<9;i++,j++) {
		if (temp=elementgrid[i]) gfx_TransparentSprite_NoClip(elemgfx[i],posx[i+1]+GRIDV/2-4,posy[i+1]+GRIDV/2-4);
		gfx_Rectangle_NoClip(posx[j],posy[j],CARD_WIDTH+4,CARD_HEIGHT+4);
	}
	//Render all cards except the selected card
	for (i=0;i<10;i++) if (i!=selcard) drawcard(cardbuf[i],0);
	drawcard(cardbuf[selcard],1);
	gfx_SwapDraw();
}












