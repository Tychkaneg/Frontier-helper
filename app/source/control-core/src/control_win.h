#ifndef FRONTIER_CONTROL_WIN_H
#define FRONTIER_CONTROL_WIN_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "control_protocol.h"
typedef struct {
 CameraSettings settings;
 uint64_t revision,reset_serial;
 uint32_t peer_pid;
 int32_t control_epoch;
 int connected,configured;
} ControlSnapshot;
typedef struct {
 uint64_t applied_revision,frames,reset_applied;
 uint32_t scene,pad_present;
 float requested_radius,actual_radius,reference_radius,reference_height,yaw,pitch;
} ControlTelemetry;
typedef struct {
 SRWLOCK gate;
 ControlSnapshot pending;
 ControlTelemetry telemetry;
 volatile LONG enabled,epoch,engine_status,render_busy;
 uint64_t created,last_beat;
 DWORD pid;
 char endpoint[120];
} ControlBridge;
int control_start(ControlBridge *b);
int control_snapshot(ControlBridge *b,ControlSnapshot *out);
void control_publish(ControlBridge *b,const ControlTelemetry *t);
void control_toggle(ControlBridge *b);
#endif
