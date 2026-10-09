#include <assert.h>
#include <stdio.h>
#include "orbit_math.h"
int main(void){
 const double pi=3.141592653589793,rad=pi/180;
 assert(orbit_axis(0,.18)==0&&orbit_axis(1000,.18)==0);
 assert(fabs(orbit_axis(32767,.18)-1)<1e-12&&fabs(orbit_axis(-32768,.18)+1)<1e-12);
 Orbit o={0,0,0,0,600};
 for(int i=0;i<240;i++)orbit_tick(&o,1,0,1.0/120,180*rad,100*rad,-65*rad,35*rad,16,0,0,-15*rad);
 assert(fabs(o.want_yaw)<1e-10); // full 360-degree revolution
 for(int i=0;i<600;i++)orbit_tick(&o,0,1,1.0/120,180*rad,100*rad,-65*rad,35*rad,16,0,0,-15*rad);
 assert(fabs(o.want_pitch+65*rad)<1e-12);
 for(int i=0;i<1200;i++)orbit_tick(&o,0,-1,1.0/120,180*rad,100*rad,-65*rad,35*rad,16,0,0,-15*rad);
 assert(fabs(o.want_pitch-35*rad)<1e-12);
 orbit_tick(&o,0,0,1.0/120,180*rad,100*rad,-65*rad,35*rad,16,1,179*rad,-15*rad);
 for(int i=0;i<300;i++)orbit_tick(&o,0,0,1.0/120,180*rad,100*rad,-65*rad,35*rad,16,0,0,-15*rad);
 assert(fabs(orbit_wrap(o.yaw-179*rad))<1e-10&&fabs(o.pitch+15*rad)<1e-10);
 float target[3]={100,200,300},eye[3];orbit_eye(&o,target,eye);
 double distance=sqrt(pow(target[0]-eye[0],2)+pow(target[1]-eye[1],2)+pow(target[2]-eye[2],2));assert(fabs(distance-600)<0.001);
 Orbit seam={179*rad,0,-179*rad,0,600};orbit_tick(&seam,0,0,.01,180*rad,100*rad,-65*rad,35*rad,16,0,0,0);
 assert(orbit_wrap(seam.yaw-179*rad)>0&&orbit_wrap(seam.yaw-179*rad)<2*rad);
 Orbit diag={0,0,0,0,600};orbit_tick(&diag,1,1,.02,180*rad,100*rad,-65*rad,35*rad,16,0,0,0);
 assert(diag.yaw<0&&diag.pitch<0);
 puts("PASS: analog deadzone, complete orbit, vertical limits, reset, radius, shortest wrap, diagonal input.");
 return 0;
}
