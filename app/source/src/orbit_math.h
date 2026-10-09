#ifndef FRONTIER_ORBIT_MATH_H
#define FRONTIER_ORBIT_MATH_H
#include <math.h>
typedef struct { double yaw,pitch,want_yaw,want_pitch,radius; } Orbit;
static double orbit_wrap(double x) { return atan2(sin(x),cos(x)); }
static double orbit_axis(short value,double deadzone) {
 double x=value<0?value/32768.0:value/32767.0;
 double a=fabs(x);return a<=deadzone?0:copysign((a-deadzone)/(1-deadzone),x);
}
static void orbit_tick(Orbit *o,double x,double y,double dt,double yaw_speed,double pitch_speed,
 double lo,double hi,double response,int reset,double heading,double reset_pitch) {
 if(dt<0)dt=0;
 if(dt>0.05)dt=0.05;
 if(reset){o->want_yaw=orbit_wrap(heading);o->want_pitch=reset_pitch;}
 else{o->want_yaw=orbit_wrap(o->want_yaw-x*yaw_speed*dt);o->want_pitch-=y*pitch_speed*dt;}
 if(o->want_pitch<lo)o->want_pitch=lo;
 if(o->want_pitch>hi)o->want_pitch=hi;
 double a=1-exp(-response*dt);
 o->yaw=orbit_wrap(o->yaw+orbit_wrap(o->want_yaw-o->yaw)*a);
 o->pitch+=(o->want_pitch-o->pitch)*a;
}
static void orbit_eye(const Orbit *o,const float target[3],float eye[3]) {
 double h=cos(o->pitch)*o->radius;
 eye[0]=(float)(target[0]-sin(o->yaw)*h);
 eye[1]=(float)(target[1]-sin(o->pitch)*o->radius);
 eye[2]=(float)(target[2]-cos(o->yaw)*h);
}
#endif
