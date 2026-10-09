#ifndef FRONTIER_ORBIT_SESSION_H
#define FRONTIER_ORBIT_SESSION_H
#include <stdint.h>
#include <math.h>
/* Reference distance is independent of collision-shortened eye coordinates. */
typedef struct {
 double radius,height;
 int calibrated,active,pending,have_position;
 uintptr_t actor,map,walls,floor;
 uint64_t stable_since,last_seen;
 long epoch;
 float position[3];
} OrbitSession;
static void orbit_session_suspend(OrbitSession *s){s->active=0;s->have_position=0;}
static int orbit_session_ready(OrbitSession *s,uint64_t now,uintptr_t actor,
 uintptr_t map,uintptr_t walls,uintptr_t floor,const float position[3],long epoch){
 double moved=0;
 if(s->have_position)for(int i=0;i<3;i++){double d=position[i]-s->position[i];moved+=d*d;}
 int changed=!s->active||s->actor!=actor||s->map!=map||s->walls!=walls||s->floor!=floor||
  s->epoch!=epoch||(now>s->last_seen&&now-s->last_seen>500)||moved>1500.0*1500.0;
 if(changed){s->pending=1;s->stable_since=now;}
 s->active=1;s->actor=actor;s->map=map;s->walls=walls;s->floor=floor;s->epoch=epoch;
 s->last_seen=now;s->have_position=1;for(int i=0;i<3;i++)s->position[i]=position[i];
 return !s->pending||now-s->stable_since>=500;
}
static int orbit_session_calibrate(OrbitSession *s,double radius,double height,double scale){
 if(s->calibrated)return 1;
 if(!isfinite(radius)||radius<80||radius>2500||!isfinite(height)||fabs(height)>1000)return 0;
 s->radius=radius*scale;s->height=height;s->calibrated=1;return 1;
}
#endif
