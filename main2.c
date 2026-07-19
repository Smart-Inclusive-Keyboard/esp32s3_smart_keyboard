#include <stdio.h>
#include <string.h>
#include "display.h"
#include "board.h"
static uint16_t fb[480*320];
void display_register_backend(uint16_t*,int,int,void(*)(int,int,int,int),void(*)(int));
static void showcell(int cell, uint32_t cp){
  int W=320,H=240;
  for(int i=0;i<W*H;i++) fb[i]=0;
  int tx=(cell-12)/2, ty=(cell-16)/2;
  display_draw_glyph_12x16_cp(tx,ty,cp,1,0,1);
  printf("cp U+%04X in %dpx cell (native 12x16 at %d,%d):\n",cp,cell,tx,ty);
  for(int y=0;y<cell;y++){char l[64];int n=0;for(int x=0;x<cell;x++)l[n++]=fb[y*W+x]?'#':'.';l[n]=0;printf("  %s\n",l);}
}
int main(){
  display_register_backend(fb,320,240,0,0);
  showcell(18,0x0439);
  showcell(18,0x0449);
  showcell(18,0x0436);
  return 0;
}
const board_t *board_get(void){return 0;}
