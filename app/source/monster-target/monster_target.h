#ifndef FRONTIER_MONSTER_TARGET_H
#define FRONTIER_MONSTER_TARGET_H
#include <math.h>
#include <stdint.h>
/* A press chooses the nearest live, rendered large monster. No target leaves yaw untouched. */
static inline int monster_candidate(const float player[3],const float monster[3],
 int active,int rendered,int large,int hp,double *distance,double *yaw) {
 if(!active||!rendered||!large||hp<=0)return 0;
 double delta[3];for(int i=0;i<3;i++) {
  if(!isfinite(player[i])||!isfinite(monster[i])||fabs(player[i])>1000000||fabs(monster[i])>1000000)return 0;
  delta[i]=(double)monster[i]-player[i];
 }
 double horizontal=delta[0]*delta[0]+delta[2]*delta[2];
 if(horizontal<1)return 0;
 *distance=horizontal+delta[1]*delta[1];*yaw=atan2(delta[0],delta[2]);return 1;
}
static inline int monster_health(uint16_t encoded,uint16_t slot_key,uint32_t session_key) {
 uint16_t rotated=(uint16_t)((encoded>>5)|(encoded<<11));
 return (int16_t)(uint16_t)(rotated^slot_key^0xffffu^session_key^0x102d5ec0u);
}
static inline uint16_t monster_camera_buttons(uint16_t buttons) {return buttons&~(uint16_t)0x40;}
static inline int monster_press(uint16_t before,uint16_t now,int allowed,int was_allowed) {
 return allowed&&was_allowed&&(now&0x40)&&!(before&0x40);
}
#endif
