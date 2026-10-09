#include "pipe_client.h"
#include "../src/control_win.h"
static ControlBridge bridge;
static void render(CameraCore *core,CameraFrame *frame) {
 static ControlSnapshot snapshot;
 ControlSnapshot latest;
 if(control_snapshot(&bridge,&latest))snapshot=latest;
 if(snapshot.revision!=core->revision)
  assert(camera_core_settings(core,&snapshot.settings,snapshot.revision));
 frame->reset_serial=snapshot.reset_serial;frame->epoch=bridge.epoch;
 frame->enabled=bridge.enabled&&snapshot.connected&&snapshot.configured&&
  snapshot.control_epoch==frame->epoch;
 frame->now=GetTickCount64();
 ControlTelemetry t={0};
 InterlockedExchange(&bridge.render_busy,1);
 /* No native geometry is used by this IPC fixture. Geometry tests are separate. */
 core->scene=frame->enabled?CAMERA_ACTIVE:CAMERA_DISABLED;
 if(frame->enabled) {
  core->applied_revision=core->revision;core->reset_applied=frame->reset_serial;core->frames++;
 }
 t.applied_revision=core->applied_revision;t.frames=core->frames;t.reset_applied=core->reset_applied;
 t.scene=core->scene;t.requested_radius=600*core->settings.distance_scale;
 control_publish(&bridge,&t);InterlockedExchange(&bridge.render_busy,0);
}
static HANDLE locked_event,release_event;
static DWORD WINAPI hold_gate(void *unused) {
 (void)unused;AcquireSRWLockExclusive(&bridge.gate);SetEvent(locked_event);
 assert(WaitForSingleObject(release_event,1000)==WAIT_OBJECT_0);
 ReleaseSRWLockExclusive(&bridge.gate);return 0;
}
static void nonblocking_renderer(void) {
 locked_event=CreateEventW(NULL,TRUE,FALSE,NULL);release_event=CreateEventW(NULL,TRUE,FALSE,NULL);
 assert(locked_event&&release_event);
 HANDLE thread=CreateThread(NULL,0,hold_gate,NULL,0,NULL);assert(thread);
 assert(WaitForSingleObject(locked_event,1000)==WAIT_OBJECT_0);
 ControlSnapshot sentinel={0};sentinel.revision=4242;
 assert(!control_snapshot(&bridge,&sentinel)&&sentinel.revision==4242);
 ControlTelemetry telemetry={0};control_publish(&bridge,&telemetry); /* Must also return immediately. */
 SetEvent(release_event);assert(WaitForSingleObject(thread,1000)==WAIT_OBJECT_0);
 CloseHandle(thread);CloseHandle(locked_event);CloseHandle(release_event);
}
static int client(DWORD pid,uint64_t created) {
 HANDLE pipe=connect_controller(pid,created);uint32_t id=1;
 ControlReply r=request(pipe,CONTROL_HELLO,id++,NULL);
 assert(r.error==CONTROL_OK&&r.enabled==0&&r.game_pid==pid&&r.process_created==created);
 assert(r.peer_pid==GetCurrentProcessId());
 r=request(pipe,CONTROL_ENABLE,id++,NULL);assert(r.error==CONTROL_NEED_SETTINGS&&r.enabled==0);
 CameraSettings s=camera_defaults();s.distance_scale=1.25f;s.shoulder_offset=50;
 r=request(pipe,CONTROL_SETTINGS,id++,&s);assert(r.error==CONTROL_OK&&r.accepted_revision==1);
 assert(memcmp(&r.settings,&s,sizeof(s))==0&&r.applied_revision==0);
 r=request(pipe,CONTROL_ENABLE,id++,NULL);assert(r.error==CONTROL_OK&&r.enabled==1);
 Sleep(50);r=request(pipe,CONTROL_STATUS,id++,NULL);
 assert(r.applied_revision==1&&r.frames>0&&r.requested_radius==750);
 uint64_t revision=r.accepted_revision;
 CameraSettings bad=s;bad.height_offset=NAN;
 r=request(pipe,CONTROL_SETTINGS,id++,&bad);
 assert(r.error==CONTROL_BAD_SETTINGS&&r.accepted_revision==revision&&r.settings.height_offset==0);
 for(int i=0;i<150;i++) {
  s.height_offset=(float)i;r=request(pipe,CONTROL_SETTINGS,id++,&s);
  assert(r.error==CONTROL_OK&&r.accepted_revision==revision+(uint64_t)i+1);
 }
 Sleep(30);r=request(pipe,CONTROL_STATUS,id++,NULL);
 assert(r.applied_revision==r.accepted_revision&&r.settings.height_offset==149);
 r=request(pipe,CONTROL_RESET,id++,NULL);assert(r.error==CONTROL_OK&&r.reset_accepted==1);
 Sleep(30);r=request(pipe,CONTROL_STATUS,id++,NULL);assert(r.reset_applied==1);
 r=request(pipe,CONTROL_DISABLE,id++,NULL);assert(r.error==CONTROL_OK&&r.enabled==0);
 Sleep(30);r=request(pipe,CONTROL_STATUS,id++,NULL);uint64_t frames=r.frames;
 Sleep(30);r=request(pipe,CONTROL_STATUS,id++,NULL);assert(r.frames==frames);
 for(int i=0;i<20&&r.in_flight;i++) {
  Sleep(2);r=request(pipe,CONTROL_STATUS,id++,NULL);
 }
 assert(r.in_flight==0&&r.frames==frames);
 r=request(pipe,CONTROL_ENABLE,id++,NULL);assert(r.error==CONTROL_OK);
 /* Closing the control connection turns off the runtime. */
 CloseHandle(pipe);Sleep(60);pipe=connect_controller(pid,created);id=1;
 r=request(pipe,CONTROL_HELLO,id++,NULL);assert(r.error==CONTROL_OK&&r.enabled==0);
 r=request(pipe,CONTROL_ENABLE,id++,NULL);assert(r.error==CONTROL_NEED_SETTINGS);
 r=request(pipe,CONTROL_SETTINGS,id++,&s);assert(r.error==CONTROL_OK);
 r=request(pipe,CONTROL_ENABLE,id++,NULL);assert(r.error==CONTROL_OK);
 /* Freeze the client without closing its handle; watchdog still disables it. */
 Sleep(CONTROL_TIMEOUT_MS+150);CloseHandle(pipe);Sleep(30);
 pipe=connect_controller(pid,created);id=1;r=request(pipe,CONTROL_HELLO,id++,NULL);
 assert(r.enabled==0&&r.error==CONTROL_OK);
 ControlHeader invalid={CONTROL_MAGIC,CONTROL_VERSION+1,CONTROL_PING,id++,0,0};DWORD count=0;
 assert(WriteFile(pipe,&invalid,sizeof(invalid),&count,NULL));
 unsigned char buffer[200];assert(!ReadFile(pipe,buffer,sizeof(buffer),&count,NULL));
 CloseHandle(pipe);Sleep(30);pipe=connect_controller(pid,created);id=1;
 r=request(pipe,CONTROL_HELLO,id++,NULL);assert(r.error==CONTROL_OK&&r.enabled==0);
 /* Deliberately truncated settings must not change any accepted settings. */
 invalid=(ControlHeader){CONTROL_MAGIC,CONTROL_VERSION,CONTROL_SETTINGS,id++,48,0};
 assert(WriteFile(pipe,&invalid,sizeof(invalid),&count,NULL));
 assert(!ReadFile(pipe,buffer,sizeof(buffer),&count,NULL));CloseHandle(pipe);
 Sleep(30);pipe=connect_controller(pid,created);id=1;r=request(pipe,CONTROL_HELLO,id++,NULL);
 assert(r.error==CONTROL_OK&&r.settings.height_offset==149);
 r=request(pipe,CONTROL_PING,id++,NULL);assert(r.error==CONTROL_OK);
 r=request(pipe,CONTROL_STATUS,1,NULL);assert(r.error==CONTROL_BAD_PACKET);CloseHandle(pipe);
 puts("PASS: real local pipe, peer/server PID, settings atomicity, 150 updates, reset, disable, reconnect, heartbeat, malformed/version/truncated/duplicate rejection.");
 return 0;
}
int main(int argc,char **argv) {
 if(argc==4)return client((DWORD)strtoul(argv[2],NULL,10),strtoull(argv[3],NULL,16));
 assert(control_start(&bridge));InterlockedExchange(&bridge.engine_status,2);
 nonblocking_renderer();
 char path[MAX_PATH],command[1024];GetModuleFileNameA(NULL,path,sizeof(path));
 snprintf(command,sizeof(command),"\"%s\" --client %lu %016llx",path,
  (unsigned long)bridge.pid,(unsigned long long)bridge.created);
 STARTUPINFOA start={0};start.cb=sizeof(start);PROCESS_INFORMATION child={0};
 assert(CreateProcessA(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&start,&child));
 CameraCore core;camera_core_init(&core);CameraFrame frame={0};
 ULONGLONG deadline=GetTickCount64()+15000;
 while(WaitForSingleObject(child.hProcess,0)==WAIT_TIMEOUT&&GetTickCount64()<deadline) {
  render(&core,&frame);Sleep(5);
 }
 DWORD exit_code=999;assert(GetExitCodeProcess(child.hProcess,&exit_code)&&exit_code==0);
 CloseHandle(child.hThread);CloseHandle(child.hProcess);return 0;
}
