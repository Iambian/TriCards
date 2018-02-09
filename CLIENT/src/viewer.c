
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

char *cardtype[] = {"Monster","Boss","GF","Player"};
char stat2char[] = {'0','1','2','3','4','5','6','7','8','9','A','B'};


void viewpack(char* packname) {
	uint8_t *imgptr,*packptr,*dataptr,*cardptr,i,j,k,y;
	uint8_t curpage,maxpage,curopt,maxopt;
	void *ptr;
	int x;
	kb_key_t kc,kd;
	card_t tempcard;
	uint8_t imgdata[CARD_HEIGHT*CARD_WIDTH+2];
	
	if (packname==NULL) return;
	curpage = curopt = 0;
	dataptr = getdataadr(packptr = getpackadr(packname));
	maxopt  = (dataptr[-2] >= OPTIONS_PER_PAGE) ? OPTIONS_PER_PAGE : dataptr[-2];
	maxpage = dataptr[-2]/OPTIONS_PER_PAGE;
	
	while (1) {
		kb_Scan();
		kc = kb_Data[1];
		kd = kb_Data[7];
		
		if (kd&kb_Up && curopt) curopt--;
		if (kd&kb_Down && curopt<maxopt-1) curopt++;
		if (kd&kb_Left && curpage) { curpage--; maxopt = OPTIONS_PER_PAGE; }
		if (kd&kb_Right&& curpage<maxpage-1) curpage++;
		
		gfx_FillScreen(TITLE_BG);
		textscale2();
		// Screen headings
		ctext("Card Browser",5);
		textscale1();
		gfx_PrintStringXY("Showing page ",5,30);
		gfx_PrintUInt(curpage + 1,2);
		gfx_PrintString(" of ");
		gfx_PrintUInt(maxpage,2);
		// Render card name list
		for (i=0,j=curpage*OPTIONS_PER_PAGE,y=50;i<OPTIONS_PER_PAGE;i++,j++,y+=LIST_LINE_HEIGHT) {
			if (i==curopt) {
				imgptr = &imgdata;
				gfx_SetColor(LIST_BG_S);
				gfx_SetTextFGColor(LIST_TX_S);
			} else {
				imgptr = NULL;
				gfx_SetColor((i&1) ? LIST_BG_A : LIST_BG_B);
			}
			gfx_FillRectangle_NoClip(5,y,200,LIST_LINE_HEIGHT);
			getcarddata(packptr,j,&tempcard,(gfx_sprite_t*)imgptr);
			if (tempcard.rank) {
				gfx_PrintStringXY(tempcard.name,10,y+2);
				gfx_SetTextFGColor(MENU_TEXT_COLOR);
				if (imgptr!=NULL) {
					gfx_SetColor(0);
					//Print card type
					ptr = cardtype[tempcard.type];
					gfx_PrintStringXY(ptr,200+(120-gfx_GetStringWidth(ptr))/2,52);
					//Draw card
					gfx_Rectangle_NoClip(234,65,CARD_WIDTH+2,CARD_HEIGHT+2);
					gfx_TransparentSprite_NoClip((gfx_sprite_t*)imgptr,235,66);
					//Print card stats (directional ranks)
					gfx_PrintStringXY("Stats",210,130);
					gfx_Rectangle_NoClip(207,127,43,40);
					pcharxy(stat2char[tempcard.up]   ,220+5,140);
					pcharxy(stat2char[tempcard.right],228+5,148);
					pcharxy(stat2char[tempcard.down] ,220+5,156);
					pcharxy(stat2char[tempcard.left] ,212+5,148);
					//Print card overall rank
					gfx_PrintStringXY("Rank ",260,130);
					gfx_PrintUInt(tempcard.rank,2);
					//Print/draw card element
					gfx_PrintStringXY("Element",260,145);
					if (tempcard.element) gfx_TransparentSprite_NoClip((gfx_sprite_t*)elemgfx[tempcard.element],280,155);
					else gfx_PrintStringXY("N/A",275,155);
				}
			gfx_SetTextFGColor(MENU_TEXT_COLOR);
			}
		}
		//End render card name list
		gfx_SwapDraw();
		if (kc|kd) keywait();
		if (kc&kb_Mode) return;
	}
}










