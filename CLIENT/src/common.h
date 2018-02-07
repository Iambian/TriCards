#ifndef __commonroutines__
#define __commonroutines__

extern void keywait();
extern void waitanykey();
extern void ctext(char* s,uint8_t y);
extern void textscale2();
extern void textscale1();
extern void dmenu(char **s,uint8_t c,uint8_t m);
extern void pcharxy(char c,int x,uint8_t y);

#endif