#ifndef FRONTIER_NATIVE_LAYOUT_H
#define FRONTIER_NATIVE_LAYOUT_H
#include <windows.h>
#include <stdint.h>
#include <math.h>
/* Private ABI only. No old camera algorithm is rebuilt into the adapter. */
#define CAMERA_RAD 0.017453292519943295
#define CAMERA_ACTIVE 6
#define CAMERA_DISABLED 0
typedef struct {
 float yaw_speed,pitch_speed,min_pitch,max_pitch,reset_pitch,response,deadzone,
  distance_scale,height_offset,shoulder_offset;
 int32_t gamepad_index;uint32_t preview;
} CameraSettings;
typedef struct {double yaw,pitch,want_yaw,want_pitch,radius;} NativeOrbit;
typedef struct {
 double radius,height;int calibrated,active,pending,have_position;
 uintptr_t actor,map,walls,floor;uint64_t stable_since,last_seen;
 long epoch;float position[3];
} NativeReference;
typedef struct {
 NativeOrbit orbit;NativeReference reference;CameraSettings settings;
 uint64_t revision,applied_revision,last_tick,reset_applied,frames;
 double resolved_radius;uint16_t previous_buttons;int input_active,scene;
} CameraCore;
typedef struct {
 CameraSettings settings;uint64_t revision,reset_serial;
 uint32_t peer_pid;int32_t control_epoch;int connected,configured;
} NativeControlSnapshot;
typedef struct {
 uint64_t applied_revision,frames,reset_applied;uint32_t scene,pad_present;
 float requested_radius,actual_radius,reference_radius,reference_height,yaw,pitch;
} NativeControlTelemetry;
typedef struct {volatile LONG status,calls,applied,bytes,lt,rt;} NativeInputStats;
typedef struct {
 SRWLOCK gate;NativeControlSnapshot pending;NativeControlTelemetry telemetry;
 NativeInputStats triggers;volatile LONG enabled,epoch,engine_status,render_busy;
 uint64_t created,last_beat;DWORD pid;char endpoint[120];
} ControlBridge;
static inline int camera_range(float value,float lo,float hi) {return isfinite(value)&&value>=lo&&value<=hi;}
static inline int camera_settings_valid(const CameraSettings *s) {
 return camera_range(s->yaw_speed,20,720)&&camera_range(s->pitch_speed,10,360)&&
  camera_range(s->min_pitch,-85,0)&&camera_range(s->max_pitch,0,85)&&
  camera_range(s->reset_pitch,s->min_pitch,s->max_pitch)&&camera_range(s->response,1,100)&&
  camera_range(s->deadzone,.02f,.5f)&&camera_range(s->distance_scale,.5f,1.5f)&&
  camera_range(s->height_offset,-250,250)&&camera_range(s->shoulder_offset,-300,300)&&
  s->gamepad_index>=-1&&s->gamepad_index<=3&&s->preview<=1;
}
#endif
