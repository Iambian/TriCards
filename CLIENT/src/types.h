#ifndef __tricards_types__
#define __tricards_types__
#include <stdint.h>

#include <graphx.h>

extern enum cardtype {monster=0,boss,gf,player};
extern enum element {none=0,poison,fire,wind,earth,water,ice,thunder,holy};
extern enum directionValues { DIR_NONE = 0,DIR_DOWN,DIR_LEFT,DIR_RIGHT,DIR_UP };
extern enum playRuleFlags { RULE_OPEN = 1, RULE_SAME = 2, RULE_SAMEWALL = 4,
					 RULE_SUDDENDEATH = 8, RULE_RANDOM = 16, RULE_PLUS = 32,
					 RULE_COMBO = 64, RULE_ELEMENTAL = 128 };

typedef struct card_t {
	uint8_t rank; char* name; uint8_t type;
	uint8_t up; uint8_t right; uint8_t down; uint8_t left;
	uint8_t element;
	gfx_sprite_t* img;
} card_t;

typedef struct metacard_t {
	card_t c;
	int x;
	int y;
	uint8_t playstate; //0=hiddenInHand 1=showingInhand 2=onField
	uint8_t gridpos;
	uint8_t isplayer1;
	uint8_t color;
} metacard_t;






#endif
