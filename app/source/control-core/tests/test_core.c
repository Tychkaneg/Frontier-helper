#include <assert.h>
#include <stdio.h>
#include "../src/control_protocol.h"
typedef struct {int calls;double wall,floor,ceiling,slope;int fail;} Room;
static int height(const float point[3],double *out,void *opaque) {
 Room *r=opaque;*out=r->floor+r->slope*point[0];return 1;
}
static int sweep(const float a[3],const float b[3],double *fraction,void *opaque) {
 Room *r=opaque;r->calls++;if(r->fail)return 0;
 double f=collision_box_fraction(a,b,-r->wall+32,r->wall-32,-r->wall+32,r->wall-32);
 if(f<0||a[1]>r->ceiling-32)return 0;
 if(b[1]>r->ceiling-32)f=fmin(f,(r->ceiling-32-a[1])/(b[1]-a[1]));
 float bounded[3];for(int i=0;i<3;i++)bounded[i]=(float)(a[i]+(b[i]-a[i])*f);
 f*=collision_ground_fraction(a,bounded,32,24,height,r);
 *fraction=f;return 1;
}
static CameraFrame frame(void) {
 CameraFrame f={0};f.now=1000;f.enabled=f.scene_valid=f.focus_allowed=1;
 f.input_allowed=f.pad_present=1;f.actor=1;f.map=2;f.walls=3;f.floor=4;
 f.native_target[1]=100;f.native_eye[1]=100;f.native_eye[2]=-600;
 return f;
}
static void settle(CameraCore *c,CameraFrame *f,Room *r,CameraView *v) {
 assert(camera_core_step(c,f,sweep,r,v)==CAMERA_SETTLING);
 f->now+=500;assert(camera_core_step(c,f,sweep,r,v)==CAMERA_ACTIVE);
}
static void defaults_and_transitions(void) {
 CameraCore c;camera_core_init(&c);CameraFrame f=frame();CameraView v;
 Room room={0,5000,0,5000,0,0};settle(&c,&f,&room,&v);
 assert(room.calls==1&&v.eye[2]==-600&&v.target[1]==100);
 CameraSettings s=camera_defaults();s.distance_scale=1.5f;
 double yaw=c.orbit.yaw;uint64_t stable=c.reference.stable_since;
 assert(camera_core_settings(&c,&s,1));
 assert(c.reference.radius==600&&c.orbit.radius==900&&c.orbit.yaw==yaw);
 assert(c.reference.stable_since==stable&&!c.reference.pending);
 f.now+=16;assert(camera_core_step(&c,&f,sweep,&room,&v)==CAMERA_ACTIVE);
 assert(v.requested_radius==900&&v.actual_radius>600&&v.actual_radius<900);
 /* Walls shorten current distance without contaminating the profile or raw reference. */
 room.wall=200;f.now+=16;assert(camera_core_step(&c,&f,sweep,&room,&v)==CAMERA_ACTIVE);
 assert(v.actual_radius<=168.001&&c.reference.radius==600&&c.settings.distance_scale==1.5f);
 f.map=20;f.now+=16;f.native_eye[2]=-85;
 assert(camera_core_step(&c,&f,sweep,&room,&v)==CAMERA_SETTLING);
 stable=c.reference.stable_since;s.distance_scale=.75f;
 assert(camera_core_settings(&c,&s,2));assert(c.reference.stable_since==stable);
 f.now+=400;assert(camera_core_step(&c,&f,sweep,&room,&v)==CAMERA_SETTLING);
 f.now+=100;room.wall=5000;assert(camera_core_step(&c,&f,sweep,&room,&v)==CAMERA_ACTIVE);
 assert(c.reference.radius==600&&c.reference.height==100&&v.requested_radius==450);
 assert(fabs(v.actual_radius-450)<1e-5&&c.applied_revision==2);
 /* Settings defaults do not recalibrate from the shortened native view. */
 s=camera_defaults();assert(camera_core_settings(&c,&s,3));
 f.epoch++;f.now+=16;assert(camera_core_step(&c,&f,sweep,&room,&v)==CAMERA_SETTLING);
 f.now+=500;assert(camera_core_step(&c,&f,sweep,&room,&v)==CAMERA_ACTIVE);
 assert(v.requested_radius==600&&c.reference.height==100);
}
static void validation_and_limits(void) {
 CameraCore c;camera_core_init(&c);CameraSettings d=camera_defaults(),bad;
 assert(camera_settings_valid(&d));
 for(int i=0;i<10;i++) {
  float *field;
  bad=d;field=(float*)&bad;field[i]=NAN;assert(!camera_core_settings(&c,&bad,99));
  bad=d;field=(float*)&bad;field[i]=INFINITY;assert(!camera_core_settings(&c,&bad,99));
 }
 assert(c.revision==0&&memcmp(&c.settings,&d,sizeof(d))==0);
 bad=d;bad.reset_pitch=-80;assert(!camera_settings_valid(&bad));
 bad=d;bad.gamepad_index=4;assert(!camera_settings_valid(&bad));
 bad=d;bad.preview=2;assert(!camera_settings_valid(&bad));
 bad=d;bad.distance_scale=0;assert(!camera_settings_valid(&bad));
 bad=d;bad.height_offset=251;assert(!camera_settings_valid(&bad));
 c.orbit.pitch=-80*CAMERA_RAD;c.orbit.want_pitch=60*CAMERA_RAD;c.orbit.yaw=.8;
 bad=d;bad.min_pitch=-10;bad.max_pitch=10;bad.reset_pitch=0;
 assert(camera_core_settings(&c,&bad,1));
 assert(fabs(c.orbit.pitch+10*CAMERA_RAD)<1e-9&&fabs(c.orbit.want_pitch-10*CAMERA_RAD)<1e-9);
 assert(c.orbit.yaw==.8);
}
static void preview_input_and_reset(void) {
 assert(camera_focus_allowed(1,1,0,0,0));
 assert(camera_focus_allowed(1,2,2,1,1));
 assert(!camera_focus_allowed(1,3,2,1,1));
 assert(!camera_focus_allowed(1,2,2,0,1));
 assert(!camera_focus_allowed(1,2,2,1,0));
 CameraCore c;camera_core_init(&c);CameraFrame f=frame();CameraView v;
 Room room={0,5000,0,5000,0,0};f.pad_present=0;f.input_allowed=0;
 settle(&c,&f,&room,&v);assert(c.scene==CAMERA_ACTIVE); /* Geometry works with no pad. */
 CameraSettings s=camera_defaults();s.height_offset=50;
 assert(camera_core_settings(&c,&s,1));f.now+=16;f.stick_x=32767;
 assert(camera_core_step(&c,&f,sweep,&room,&v)==CAMERA_ACTIVE);
 assert(v.target[1]==150&&c.orbit.want_yaw==0); /* App focus ignores stick. */
 f.pad_present=f.input_allowed=1;f.buttons=0x40;f.heading=1;f.stick_x=0;f.now+=16;
 assert(camera_core_step(&c,&f,sweep,&room,&v)==CAMERA_ACTIVE);
 assert(c.orbit.want_yaw==0); /* Held L3 while returning focus is not a new press. */
 f.buttons=0;f.now+=16;camera_core_step(&c,&f,sweep,&room,&v);
 f.buttons=0x40;f.now+=16;camera_core_step(&c,&f,sweep,&room,&v);
 assert(fabs(c.orbit.want_yaw-1)<1e-9&&c.settings.height_offset==50);
 f.scene_valid=0;f.reset_serial=7;f.now+=16;
 assert(camera_core_step(&c,&f,sweep,&room,&v)==CAMERA_WAIT_SCENE&&c.reset_applied==0);
 f.scene_valid=1;f.heading=2;f.now+=16;
 assert(camera_core_step(&c,&f,sweep,&room,&v)==CAMERA_SETTLING);
 f.now+=500;assert(camera_core_step(&c,&f,sweep,&room,&v)==CAMERA_ACTIVE);
 assert(c.reset_applied==7&&fabs(c.orbit.want_yaw-2)<1e-9&&c.settings.height_offset==50);
 f.enabled=0;f.now+=16;assert(camera_core_step(&c,&f,sweep,&room,&v)==CAMERA_DISABLED);
}
static void pivot_and_eye_geometry(void) {
 CameraCore c;camera_core_init(&c);CameraFrame f=frame();CameraView v;
 Room r={0,200,0,250,0,0};settle(&c,&f,&r,&v);
 CameraSettings s=camera_defaults();s.shoulder_offset=300;s.height_offset=250;
 assert(camera_core_settings(&c,&s,1));int calls=r.calls;f.now+=16;
 assert(camera_core_step(&c,&f,sweep,&r,&v)==CAMERA_ACTIVE);
 assert(r.calls==calls+2&&v.target[0]<=168.001&&v.target[1]<=218.001);
 assert(v.eye[0]<=168.001&&v.eye[1]<=218.001&&v.eye[2]>=-168.001);
 s.shoulder_offset=0;s.height_offset=-250;camera_core_settings(&c,&s,2);f.now+=16;
 assert(camera_core_step(&c,&f,sweep,&r,&v)==CAMERA_ACTIVE);
 assert(v.target[1]>=31.999&&v.eye[1]>=31.999);
 /* Stateless pivot checks must not shrink the camera recovery cache to pivot distance. */
 r.wall=5000;r.ceiling=5000;s.height_offset=50;camera_core_settings(&c,&s,3);f.now+=16;
 double previous=c.resolved_radius;
 assert(camera_core_step(&c,&f,sweep,&r,&v)==CAMERA_ACTIVE);
 assert(v.actual_radius>=previous&&v.requested_radius==600&&v.target[1]==150);
 /* Failed geometry publishes neither a successful revision nor a reset acknowledgement. */
 CameraView untouched=v;uint64_t applied=c.applied_revision,frames=c.frames;
 s.height_offset=60;camera_core_settings(&c,&s,4);r.fail=1;f.reset_serial=8;f.now+=16;
 assert(camera_core_step(&c,&f,sweep,&r,&v)==CAMERA_GEOMETRY_UNAVAILABLE);
 assert(c.applied_revision==applied&&c.frames==frames&&c.reset_applied!=8);
 assert(memcmp(&untouched,&v,sizeof(v))==0);
}
static uint32_t random_state=1234567;
static double random_unit(void) {
 random_state=random_state*1664525u+1013904223u;return random_state/4294967295.0;
}
static void geometry_properties(void) {
 CameraCore c;camera_core_init(&c);CameraFrame f=frame();CameraView v;
 Room r={0,500,-20,350,.15,0};settle(&c,&f,&r,&v);
 for(int i=0;i<5000;i++) {
  CameraSettings s=camera_defaults();s.distance_scale=(float)(.5+random_unit());
  s.shoulder_offset=(float)(600*random_unit()-300);s.height_offset=(float)(500*random_unit()-250);
  assert(camera_core_settings(&c,&s,(uint64_t)i+1));
  c.orbit.yaw=c.orbit.want_yaw=6.283185307179586*random_unit();
  c.orbit.pitch=c.orbit.want_pitch=(-65+100*random_unit())*CAMERA_RAD;f.now+=16;
  enum CameraScene scene=camera_core_step(&c,&f,sweep,&r,&v);
  assert(c.reference.radius==600&&s.distance_scale==c.settings.distance_scale);
  if(scene!=CAMERA_ACTIVE){assert(scene==CAMERA_GEOMETRY_UNAVAILABLE);continue;}
  for(int k=0;k<2;k++) {
   const float *p=k?v.target:v.eye;
   assert(fabs(p[0])<=468.002&&fabs(p[2])<=468.002&&p[1]<=318.002);
   assert(p[1]>=r.floor+r.slope*p[0]+31.998);
  }
  assert(v.actual_radius<=v.requested_radius+.001&&v.actual_radius>0);
 }
}
static void protocol_validation(void) {
 ControlHeader h={CONTROL_MAGIC,CONTROL_VERSION,CONTROL_HELLO,1,0,0};
 assert(control_header_valid(&h,20));
 unsigned char *bytes=(unsigned char*)&h;
 assert(bytes[0]=='O'&&bytes[1]=='R'&&bytes[2]=='B'&&bytes[3]=='T');
 h.command=CONTROL_SETTINGS;h.payload_size=48;assert(control_header_valid(&h,68));
 assert(!control_header_valid(&h,67)&&!control_header_valid(&h,69));
 h.version++;assert(!control_header_valid(&h,68));h.version--;
 h.reserved=1;assert(!control_header_valid(&h,68));h.reserved=0;
 h.command=CONTROL_ENABLE;assert(!control_header_valid(&h,68));
 h.payload_size=0;h.request_id=0;assert(!control_header_valid(&h,20));
}
int main(void) {
 defaults_and_transitions();validation_and_limits();preview_input_and_reset();
 pivot_and_eye_geometry();geometry_properties();protocol_validation();
 puts("PASS: live settings, raw reference retention, limits, focus/no-pad/reset, two-stage walls/floor/ceiling/slope, 5000 geometry cases, wire validation.");
}
