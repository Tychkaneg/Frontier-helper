#include <assert.h>
#include <stdio.h>
#include "monster_target.h"
int main(void) {
 float p[3]={100,20,100},m[3]={200,20,100};double d=0,y=0;
 assert(monster_candidate(p,m,1,1,1,100,&d,&y)&&fabs(y-1.5707963267948966)<1e-9);
 m[0]=100;m[2]=0;assert(monster_candidate(p,m,1,1,1,100,&d,&y)&&fabs(fabs(y)-3.141592653589793)<1e-9);
 assert(!monster_candidate(p,m,1,1,0,100,&d,&y));assert(!monster_candidate(p,m,1,0,1,100,&d,&y));
 assert(!monster_candidate(p,m,1,1,1,0,&d,&y));assert(!monster_candidate(p,m,1,1,1,-1,&d,&y));
 m[0]=NAN;assert(!monster_candidate(p,m,1,1,1,100,&d,&y));m[0]=INFINITY;assert(!monster_candidate(p,m,1,1,1,100,&d,&y));
 for(int i=-32768;i<=32767;i++) {
  uint16_t value=(uint16_t)(i^0x31ab^0xffffu^0x85442351u^0x102d5ec0u);
  uint16_t encoded=(uint16_t)((value<<5)|(value>>11));
  assert(monster_health(encoded,0x31ab,0x85442351u)==i);
 }
 for(unsigned i=0;i<65536;i++)assert(monster_camera_buttons((uint16_t)i)==(i&~0x40u));
 assert(monster_press(0,0x40,1,1));assert(!monster_press(0x40,0x40,1,1));
 assert(!monster_press(0,0x40,0,1));assert(!monster_press(0,0x40,1,0));assert(!monster_press(0,0x80,1,1));
 puts("PASS: directions, alive/visible/large filters, invalid coordinates, all HP values, only L3 masked, press/focus guards");return 0;
}
