
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

#include <keypadc.h>
#include <graphx.h>

#include "const.h"
#include "common.h"

void keywait() { while (kb_AnyKey()); }
void waitanykey() {	keywait(); 	while (!kb_AnyKey()); keywait(); }
void ctext(char* s,uint8_t y) {	gfx_PrintStringXY(s,(LCD_WIDTH-gfx_GetStringWidth(s))/2,y); }
void textscale2() { gfx_SetTextScale(2,2); }
void textscale1() { gfx_SetTextScale(1,1); }
void dmenu(char **s,uint8_t c,uint8_t m) { uint8_t i,y; textscale2(); for(i=0,y=(240-24*m)/2;i<m;i++,y+=24) {	if (i==c) gfx_SetTextFGColor(MENU_TEXT_SELECTED); ctext(s[i],y); gfx_SetTextFGColor(MENU_TEXT_COLOR); } textscale1(); }
void pcharxy(char c,int x,uint8_t y) { gfx_SetTextXY(x,y); gfx_PrintChar(c); }




