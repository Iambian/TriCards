

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

//#include <debug.h>
#include <decompress.h>
#include <fileioc.h>
#include <keypadc.h>
#include <graphx.h>

#include "main.h"
#include "files.h"
#include "types.h"
#include "const.h"
#include "common.h"

char *cpk_header = "TriCrPak";


char* getpack(uint8_t packnum) {
	char *varname;
	uint8_t *pos = NULL;
	while (1) {
		if ((varname = ti_Detect(&pos,cpk_header)) == NULL || !packnum) return varname;
		packnum--;
	}
}

uint8_t getnumpacks() {
	uint8_t i;
	for(i=0;getpack(i)!=NULL;i++);
	return i;
}



uint8_t* getpackadr(char *varname) {
	uint8_t *packptr;
	ti_var_t filehandle;
	
	packptr = NULL;
	if (filehandle = ti_Open(varname,"r")) {
		packptr = ti_GetDataPtr(filehandle);
		ti_Close(filehandle);
	}
	return packptr;
}

uint8_t* getdataadr(uint8_t *packptr) {
	uint8_t i;
	for (i=FILE_LONGDESC_OFFSET;*(packptr+i);i++);
	return packptr+(i+1+FILE_NUMCARDS_OFFSET_AFTER_LONGDESC);
}


//Format 0: 11b total, 2b img offset
//Format 1: 12b total, 1b file ID, 2b img offset
void getcarddata(uint8_t *packptr, uint8_t cardnum, card_t *cardadr, gfx_sprite_t *imgadr) {
	uint8_t *dataptr,filefmt,i;
	
	dataptr = getdataadr(packptr);
	filefmt = dataptr[-1];
	cardadr->rank = 0;
	if (cardnum >= dataptr[-2]) return;
	
	dataptr += cardnum*(11+filefmt);
	cardadr->rank    = dataptr[0];
	cardadr->name    = (char*)(packptr + *((uint16_t*)(dataptr+1)));
	cardadr->type    = dataptr[3];
	cardadr->up      = dataptr[4];
	cardadr->right   = dataptr[5];
	cardadr->down    = dataptr[6];
	cardadr->left    = dataptr[7];
	cardadr->element = dataptr[8];
	if (imgadr == NULL) return;
	((uint16_t*)imgadr)[0] = (CARD_HEIGHT<<8)+CARD_WIDTH;
	if (filefmt == 0) {
		//2b img offset
		cardadr->img = imgadr;
		dzx7_Turbo(packptr+*((uint16_t*)(dataptr+9)),((uint8_t*)imgadr)+2);
	} else {
		//1b file id, 2b img offset
		
	}
}

//Display-only. Called by selectpack()
void showpackdetails(char *varname,uint8_t curpack,uint8_t maxpack) {
	uint8_t *packptr,*dataptr,*cardptr,i;
	int x;
	uint8_t tempimg[CARD_WIDTH*CARD_HEIGHT+2];
	card_t tmpcard;
	
	dataptr = getdataadr(packptr = getpackadr(varname));
	textscale2();
	ctext("Card Pack Selection",5);
	textscale1();
	gfx_PrintStringXY("Displaying pack ",5,30);
	gfx_PrintUInt(curpack+1,3);
	gfx_PrintString(" of ");
	gfx_PrintUInt(maxpack,3);
	ctext((char*)(packptr+17),70);
	gfx_PrintStringXY("Filename: ",5,85);
	gfx_PrintString(varname);
	gfx_PrintString(", descriptor: ");
	gfx_PrintString((char*)(packptr+8));
	gfx_PrintStringXY("Number of cards: ",5,95);
	gfx_PrintUInt(dataptr[-2],3);
	ctext("Card pack preview",110);
	for(i=0,x=(LCD_WIDTH-(CARD_WIDTH+4)*5)/2;i<5;i++,x+=CARD_WIDTH+4) {
		getcarddata(packptr,i,&tmpcard,(gfx_sprite_t*)&tempimg);
		if (tmpcard.rank) {
			gfx_SetColor(0x00);
			gfx_Rectangle_NoClip(x-1,119,CARD_WIDTH+2,CARD_HEIGHT+2);
			gfx_TransparentSprite_NoClip((gfx_sprite_t*)tempimg,x,120);
		} else {
			gfx_SetColor(FILE_EXPLORER_BGCOLOR);
			gfx_FillRectangle_NoClip(x-1,119,CARD_WIDTH+2,CARD_HEIGHT+2);
		}
	}
	gfx_SwapDraw();
}

char* selectpack() {
	uint8_t *ptr,curpack,packnum,numpacks,i;
	char *packname;
	kb_key_t kc,kd;
	
	numpacks = getnumpacks();
	showpackdetails(getpack(packnum = curpack = 0),1,numpacks);
	
	while (1) {
		gfx_FillScreen(TITLE_BG);
		kb_Scan();
		kc = kb_Data[1];
		kd = kb_Data[7];
		
		if (kd&(kb_Up|kb_Left)) {
			packnum--;
			if (packnum>254) packnum = numpacks-1;
		}
		if (kd&(kb_Down|kb_Right)) {
			packnum++;
			if (packnum>=numpacks) packnum = 0;
		}
		packname = getpack(packnum);
		if (curpack!=packnum) {
			curpack = packnum;
			showpackdetails(packname,curpack,numpacks);
		}
		
		if (kc|kd) keywait();
		if (kc&kb_Mode) return NULL;
		if (kc&kb_2nd) return packname;
	}
}

































