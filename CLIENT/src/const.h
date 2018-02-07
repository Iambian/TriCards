#ifndef __tricards_const__
#define __tricards_const__
#include <stdint.h>


#define GM_TITLE 0
#define GM_BROWSEPACK 4
#define GM_CARDLISTER 5
#define GM_OPTIONS 6
#define GM_GAMESELECT 7
#define GM_SELECTINGCARDS 8
#define GM_SELECTINGPLACE 9
#define GM_GAMEXIT 255

#define GAMEBOARD_BG 0xC5
#define PLAYER1_BG 0xF4
#define PLAYER2_BG 0x9F
#define CARD_SEL_FG 0x8C

#define CARD_WIDTH 52
#define CARD_HEIGHT 52

#define TRANSPARENT_COLOR 0xFF
#define GREETINGS_DIALOG_TEXT_COLOR 0xDF
#define FILE_EXPLORER_BGCOLOR 0xBF
#define TITLE_BG 0xBF
#define LIST_BG_A 0x1D
#define LIST_BG_B 0x5E
#define LIST_BG_S 0x0A
#define LIST_TX_S 0xE7
#define LIST_LINE_HEIGHT 12

#define MENU_TEXT_COLOR 0x00
#define MENU_TEXT_SELECTED 0x8B
#define OPTIONS_PER_PAGE 11

#define GMBOX_X (LCD_WIDTH/4)
#define GMBOX_Y (LCD_HEIGHT/2-LCD_HEIGHT/8)
#define GMBOX_W (LCD_WIDTH/2)
#define GMBOX_H (LCD_HEIGHT/4)

#define GRIDX 72
#define GRIDY 32
#define GRIDV 60
#define PLAYERX 5
#define PLAYERY 32
#define PLAYERV 30
#define ENEMYX 260
#define ENEMYY 32
#define ENEMYV 30

/* Board positions: | 
	10	1 2 3	15  | (005,032) (000,000) (000,000) (000,000) (260,032)
	11	     	16  | (005,056)                               (260,056)
	12	4 5 6	17  | (005,080) (000,000) (000,000) (000,000) (260,080)
	13	     	18  | (005,104)                               (260,104)
	14	7 8 9	19  | (005,128) (000,000) (000,000) (000,000) (260,128)
	3x3 grid with 4px gaps: +0,+0: +60,+60, +120,+120 grid area 56*3+4*2=176
	Centered top left at (72,32)
	Sides are off from the edge by 4px, so X:5,260; Y:32,152
	Cards on sides are overlapping so based on Y. 5 steps is +24 per iter.
*/


#endif
