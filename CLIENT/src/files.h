#ifndef __tricards_files__
#define __tricards_files__
#include <stdint.h>


#define FILE_ID_OFFSET 0
#define FILE_SHORTDESC_OFFSET FILE_ID_OFFSET+8
#define FILE_LONGDESC_OFFSET FILE_SHORTDESC_OFFSET+9
#define FILE_NUMCARDS_OFFSET_AFTER_LONGDESC 2

extern char *getpack(uint8_t packnum);
extern uint8_t getnumpacks();

extern uint8_t* getpackadr(char *varname);
extern uint8_t* getdataadr(uint8_t *packadr);
extern void getcarddata(uint8_t *packptr, uint8_t cardnum, card_t *cardadr, gfx_sprite_t *imgadr);
//extern void getcardadr


















#endif
