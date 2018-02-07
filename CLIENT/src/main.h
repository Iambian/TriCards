#ifndef __tricards_main__
#define __tricards_main__
#include <stdint.h>

#include "gfx/num_gfx.h"
#include "types.h"

#define NUM_ELEMENTS 9

typedef struct stats_t {
	int unsigned wins;
	int unsigned losses;
	int unsigned quits;
	int unsigned draws;
	char fn[10];
} stats_t;

extern metacard_t *cardbuf[];
extern gfx_sprite_t *imgpack[];
extern gfx_sprite_t *numtiles[numtiles_tiles_num];
extern gfx_sprite_t *cardback;
extern gfx_sprite_t *elemgfx[NUM_ELEMENTS];















#endif