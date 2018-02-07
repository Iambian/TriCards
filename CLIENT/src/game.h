#ifndef __tricards_game__
#define __tricards_game__
#include <stdint.h>

#define CARDS_QUIT 0
#define CARDS_WIN 1
#define CARDS_LOSE 2
#define CARDS_DRAW 3

uint8_t startGame(char *packname,uint8_t rules);


#endif