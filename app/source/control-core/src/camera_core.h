#ifndef FRONTIER_CAMERA_CORE_H
#define FRONTIER_CAMERA_CORE_H
#include <stdint.h>
#include <string.h>
#include "../../src/orbit_math.h"
#include "../../src/orbit_session.h"
#include "../../src/collision_math.h"

#define CAMERA_RAD (0.017453292519943295)
/* Fixed-width wire schema: degrees, seconds, game units; no pointers or bools. */
typedef struct {
 float yaw_speed, pitch_speed, min_pitch, max_pitch, reset_pitch;
 float response, deadzone, distance_scale, height_offset, shoulder_offset;
 int32_t gamepad_index;
 uint32_t preview;
} CameraSettings;
_Static_assert(sizeof(CameraSettings)==48,"settings ABI");

static inline CameraSettings camera_defaults(void) {
 CameraSettings s={180,100,-65,35,-15,16,.18f,1,0,0,-1,1};return s;
}
static inline int camera_range(float v,float lo,float hi) {
 return isfinite(v)&&v>=lo&&v<=hi;
}
static inline int camera_settings_valid(const CameraSettings *s) {
 return camera_range(s->yaw_speed,20,720)&&camera_range(s->pitch_speed,10,360)&&
  camera_range(s->min_pitch,-85,0)&&camera_range(s->max_pitch,0,85)&&
  camera_range(s->reset_pitch,s->min_pitch,s->max_pitch)&&
  camera_range(s->response,1,100)&&camera_range(s->deadzone,.02f,.5f)&&
  camera_range(s->distance_scale,.5f,1.5f)&&
  /* Provisional input bounds, not a promise about a particular level's geometry. */
  camera_range(s->height_offset,-250,250)&&camera_range(s->shoulder_offset,-300,300)&&
  s->gamepad_index>=-1&&s->gamepad_index<=3&&s->preview<=1;
}
/* Preview is limited to the connected local controller process. */
static inline int camera_focus_allowed(uint32_t game,uint32_t foreground,
 uint32_t peer,int connected,int preview) {
 return foreground==game||(connected&&preview&&peer!=0&&foreground==peer);
}
enum CameraScene {
 CAMERA_DISABLED, CAMERA_WAIT_SCENE, CAMERA_WAIT_FOCUS,
 CAMERA_SETTLING, CAMERA_BAD_REFERENCE, CAMERA_GEOMETRY_UNAVAILABLE, CAMERA_ACTIVE
};
typedef struct {
 Orbit orbit;
 OrbitSession reference; /* radius always R0, never scaled or collision-shortened */
 CameraSettings settings;
 uint64_t revision, applied_revision, last_tick, reset_applied, frames;
 double resolved_radius;
 uint16_t previous_buttons;
 int input_active;
 enum CameraScene scene;
} CameraCore;
typedef struct {
 uint64_t now, reset_serial;
 long epoch;
 uintptr_t actor,map,walls,floor;
 float position[3], native_eye[3], native_target[3];
 double heading;
 short stick_x,stick_y;
 uint16_t buttons;
 int enabled,scene_valid,focus_allowed,input_allowed,pad_present;
} CameraFrame;
typedef struct {
 float eye[3],target[3];
 double requested_radius,actual_radius;
} CameraView;
/* Geometry callback is stateless: safe fraction of the requested segment.
   It must run on the game's rendering thread, including the pivot sweep. */
typedef int (*CameraSweep)(const float start[3],const float end[3],double *fraction,void *context);

static inline void camera_core_init(CameraCore *c) {
 memset(c,0,sizeof(*c));c->settings=camera_defaults();
}
static inline int camera_core_settings(CameraCore *c,const CameraSettings *s,uint64_t revision) {
 if(!camera_settings_valid(s))return 0;
 if(c->settings.gamepad_index!=s->gamepad_index)c->input_active=0;
 c->settings=*s;c->revision=revision;
 double lo=s->min_pitch*CAMERA_RAD,hi=s->max_pitch*CAMERA_RAD;
 c->orbit.pitch=fmax(lo,fmin(hi,c->orbit.pitch));
 c->orbit.want_pitch=fmax(lo,fmin(hi,c->orbit.want_pitch));
 if(c->reference.calibrated)c->orbit.radius=c->reference.radius*s->distance_scale;
 /* Deliberately preserve reference, epoch, stable_since and yaw. */
 return 1;
}
static inline int camera_sweep(CameraSweep sweep,void *ctx,const float a[3],const float b[3],double *f) {
 if(!sweep||!sweep(a,b,f,ctx)||!isfinite(*f)||*f<0||*f>1)return 0;
 return 1;
}
static inline enum CameraScene camera_pause(CameraCore *c,enum CameraScene why,int suspend) {
 if(suspend)orbit_session_suspend(&c->reference);
 c->input_active=0;c->scene=why;return why;
}
static inline enum CameraScene camera_core_step(CameraCore *c,const CameraFrame *f,
 CameraSweep sweep,void *context,CameraView *view) {
 double dt=f->now>=c->last_tick?(f->now-c->last_tick)/1000.0:0;c->last_tick=f->now;
 if(!f->enabled)return camera_pause(c,CAMERA_DISABLED,1);
 if(!f->scene_valid)return camera_pause(c,CAMERA_WAIT_SCENE,1);
 if(!f->focus_allowed)return camera_pause(c,CAMERA_WAIT_FOCUS,0);
 for(int i=0;i<3;i++)if(!isfinite(f->position[i])||!isfinite(f->native_eye[i])||
  !isfinite(f->native_target[i]))return camera_pause(c,CAMERA_WAIT_SCENE,1);
 if(!isfinite(f->heading))return camera_pause(c,CAMERA_WAIT_SCENE,1);
 if(!orbit_session_ready(&c->reference,f->now,f->actor,f->map,f->walls,f->floor,
  f->position,f->epoch))return camera_pause(c,CAMERA_SETTLING,0);
 int input=f->input_allowed&&f->pad_present;
 if(c->reference.pending) {
  double x=f->native_target[0]-f->native_eye[0],y=f->native_target[1]-f->native_eye[1],
   z=f->native_target[2]-f->native_eye[2];
  int first=!c->reference.calibrated;
  if(!orbit_session_calibrate(&c->reference,sqrt(x*x+y*y+z*z),
   f->native_target[1]-f->position[1],1))return camera_pause(c,CAMERA_BAD_REFERENCE,0);
  c->orbit.radius=c->reference.radius*c->settings.distance_scale;
  c->orbit.yaw=c->orbit.want_yaw=first?atan2(x,z):orbit_wrap(f->heading);
  double p=first?atan2(y,sqrt(x*x+z*z)):c->settings.reset_pitch*CAMERA_RAD;
  c->orbit.pitch=c->orbit.want_pitch=fmax(c->settings.min_pitch*CAMERA_RAD,
   fmin(c->settings.max_pitch*CAMERA_RAD,p));
  c->resolved_radius=c->orbit.radius;c->reference.pending=0;
  c->previous_buttons=f->buttons;c->input_active=input;dt=0;
 }
 if(input&&!c->input_active)c->previous_buttons=f->buttons;
 int reset=f->reset_serial!=c->reset_applied||
  (input&&(f->buttons&0x40)&&!(c->previous_buttons&0x40)); /* XINPUT left thumb */
 c->previous_buttons=f->buttons;c->input_active=input;
 orbit_tick(&c->orbit,input?orbit_axis(f->stick_x,c->settings.deadzone):0,
  input?orbit_axis(f->stick_y,c->settings.deadzone):0,dt,
  c->settings.yaw_speed*CAMERA_RAD,c->settings.pitch_speed*CAMERA_RAD,
  c->settings.min_pitch*CAMERA_RAD,c->settings.max_pitch*CAMERA_RAD,
  c->settings.response,reset,f->heading,c->settings.reset_pitch*CAMERA_RAD);
 float anchor[3]={f->position[0],(float)(f->position[1]+c->reference.height),f->position[2]};
 float target[3]={
  (float)(anchor[0]+cos(c->orbit.yaw)*c->settings.shoulder_offset),
  anchor[1]+c->settings.height_offset,
  (float)(anchor[2]-sin(c->orbit.yaw)*c->settings.shoulder_offset)
 };
 if(c->settings.height_offset!=0||c->settings.shoulder_offset!=0) {
  double fraction;
  if(!camera_sweep(sweep,context,anchor,target,&fraction))
   return camera_pause(c,CAMERA_GEOMETRY_UNAVAILABLE,0);
  for(int i=0;i<3;i++)target[i]=(float)(anchor[i]+(target[i]-anchor[i])*fraction);
 }
 c->orbit.radius=c->reference.radius*c->settings.distance_scale;
 float eye[3];orbit_eye(&c->orbit,target,eye);double fraction;
 if(!camera_sweep(sweep,context,target,eye,&fraction))
  return camera_pause(c,CAMERA_GEOMETRY_UNAVAILABLE,0);
 double allowed=c->orbit.radius*fraction;
 c->resolved_radius=collision_radius(c->resolved_radius,allowed,dt,5);
 double actual=fmin(allowed,c->resolved_radius);
 if(actual<.01)return camera_pause(c,CAMERA_GEOMETRY_UNAVAILABLE,0);
 for(int i=0;i<3;i++) {
  view->target[i]=target[i];
  view->eye[i]=(float)(target[i]+(eye[i]-target[i])*actual/c->orbit.radius);
 }
 view->requested_radius=c->orbit.radius;view->actual_radius=actual;
 /* This is prepared for a write. Caller marks telemetry only after writing it. */
 c->reset_applied=f->reset_serial;c->applied_revision=c->revision;
 c->frames++;c->scene=CAMERA_ACTIVE;return CAMERA_ACTIVE;
}
#endif
