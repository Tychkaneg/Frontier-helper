#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include "control_win.h"
#include <sddl.h>
#include <stdio.h>

static LONG atomic_read(volatile LONG *p) {return InterlockedCompareExchange(p,0,0);}
static void set_enabled(ControlBridge *b,int enabled) {
 LONG previous=InterlockedExchange(&b->enabled,enabled);
 if(previous!=enabled)b->pending.control_epoch=InterlockedIncrement(&b->epoch);
}
static void disconnect(ControlBridge *b,DWORD peer) {
 AcquireSRWLockExclusive(&b->gate);
 /* A timed-out old connection must not disable a later connection. */
 if(b->pending.peer_pid==peer) {
  set_enabled(b,0);b->pending.connected=0;b->pending.configured=0;b->pending.peer_pid=0;
 }
 ReleaseSRWLockExclusive(&b->gate);
}
int control_snapshot(ControlBridge *b,ControlSnapshot *out) {
 if(!TryAcquireSRWLockExclusive(&b->gate))return 0;
 *out=b->pending;ReleaseSRWLockExclusive(&b->gate);return 1;
}
void control_publish(ControlBridge *b,const ControlTelemetry *t) {
 if(TryAcquireSRWLockExclusive(&b->gate)) {
  b->telemetry=*t;ReleaseSRWLockExclusive(&b->gate);
 }
}
void control_toggle(ControlBridge *b) {
 AcquireSRWLockExclusive(&b->gate);
 if(b->pending.connected&&b->pending.configured&&atomic_read(&b->engine_status)==2)
  set_enabled(b,!atomic_read(&b->enabled));
 ReleaseSRWLockExclusive(&b->gate);
}
static void reply_snapshot(ControlBridge *b,ControlReply *r,uint32_t error) {
 memset(r,0,sizeof(*r));r->error=error;
 AcquireSRWLockExclusive(&b->gate);
 r->engine_status=atomic_read(&b->engine_status);r->game_pid=b->pid;
 r->peer_pid=b->pending.peer_pid;r->enabled=atomic_read(&b->enabled);
 r->scene=b->telemetry.scene;r->pad_present=b->telemetry.pad_present;
 r->in_flight=atomic_read(&b->render_busy);r->process_created=b->created;
 r->accepted_revision=b->pending.revision;r->applied_revision=b->telemetry.applied_revision;
 r->frames=b->telemetry.frames;r->reset_accepted=b->pending.reset_serial;
 r->reset_applied=b->telemetry.reset_applied;r->settings=b->pending.settings;
 r->requested_radius=b->telemetry.requested_radius;r->actual_radius=b->telemetry.actual_radius;
 r->reference_radius=b->telemetry.reference_radius;r->reference_height=b->telemetry.reference_height;
 r->yaw=b->telemetry.yaw;r->pitch=b->telemetry.pitch;
 ReleaseSRWLockExclusive(&b->gate);
}
/* No pipe I/O takes the snapshot lock. Timeout cancels the exact pending operation. */
static int pipe_transfer(HANDLE pipe,void *buffer,DWORD size,int writing,DWORD *transferred) {
 OVERLAPPED ov={0};ov.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);if(!ov.hEvent)return 0;
 DWORD count=0;
 BOOL ok=writing?WriteFile(pipe,buffer,size,&count,&ov):ReadFile(pipe,buffer,size,&count,&ov);
 if(!ok&&GetLastError()==ERROR_IO_PENDING) {
  DWORD wait=WaitForSingleObject(ov.hEvent,CONTROL_TIMEOUT_MS);
  if(wait!=WAIT_OBJECT_0) {
   CancelIoEx(pipe,&ov);GetOverlappedResult(pipe,&ov,&count,TRUE);CloseHandle(ov.hEvent);return 0;
  }
  ok=GetOverlappedResult(pipe,&ov,&count,FALSE);
 }
 *transferred=count;CloseHandle(ov.hEvent);return ok!=0;
}
static int pipe_connect(HANDLE pipe) {
 OVERLAPPED ov={0};ov.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);if(!ov.hEvent)return 0;
 BOOL ok=ConnectNamedPipe(pipe,&ov);
 if(!ok) {
  DWORD error=GetLastError();
  if(error==ERROR_PIPE_CONNECTED)ok=TRUE;
  else if(error==ERROR_IO_PENDING) {
   DWORD count;ok=WaitForSingleObject(ov.hEvent,INFINITE)==WAIT_OBJECT_0&&
    GetOverlappedResult(pipe,&ov,&count,FALSE);
  }
 }
 CloseHandle(ov.hEvent);return ok!=0;
}
static uint32_t dispatch(ControlBridge *b,DWORD peer,const ControlHeader *h,const BYTE *payload,
 int *hello,uint32_t *last_id) {
 if(h->request_id<=*last_id)return CONTROL_BAD_PACKET;
 *last_id=h->request_id;
 if(!*hello&&h->command!=CONTROL_HELLO)return CONTROL_NEED_HELLO;
 if(h->command==CONTROL_HELLO) {
  if(*hello)return CONTROL_BAD_PACKET;
  *hello=1;
  AcquireSRWLockExclusive(&b->gate);
  b->pending.peer_pid=peer;b->pending.connected=1;b->pending.configured=0;
  b->last_beat=GetTickCount64();set_enabled(b,0);
  ReleaseSRWLockExclusive(&b->gate);return CONTROL_OK;
 }
 CameraSettings settings;
 if(h->command==CONTROL_SETTINGS) {
  memcpy(&settings,payload,sizeof(settings));
  if(!camera_settings_valid(&settings))return CONTROL_BAD_SETTINGS;
 }
 uint32_t result=CONTROL_OK;
 AcquireSRWLockExclusive(&b->gate);
 if(!b->pending.connected||b->pending.peer_pid!=peer)result=CONTROL_NEED_HELLO;
 else {
  b->last_beat=GetTickCount64();
  switch(h->command) {
  case CONTROL_SETTINGS:
   b->pending.settings=settings;b->pending.revision++;b->pending.configured=1;break;
  case CONTROL_ENABLE:
   if(!b->pending.configured)result=CONTROL_NEED_SETTINGS;
   else if(atomic_read(&b->engine_status)!=2)result=CONTROL_ENGINE_UNAVAILABLE;
   else set_enabled(b,1);
   break;
  case CONTROL_DISABLE:set_enabled(b,0);break;
  case CONTROL_RESET:b->pending.reset_serial++;break;
  default:break;
  }
 }
 ReleaseSRWLockExclusive(&b->gate);return result;
}
static DWORD WINAPI pipe_worker(void *opaque) {
 ControlBridge *b=(ControlBridge*)opaque;
 /* Same local user only. No remote clients; at most one connected controller. */
 HANDLE token=NULL;BYTE token_buf[1024];DWORD needed=0;LPSTR sid=NULL;
 if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return 1;
 BOOL ok=GetTokenInformation(token,TokenUser,token_buf,sizeof(token_buf),&needed)&&
  ConvertSidToStringSidA(((TOKEN_USER*)token_buf)->User.Sid,&sid);
 CloseHandle(token);if(!ok)return 1;
 char sddl[256];snprintf(sddl,sizeof(sddl),"D:P(A;;GA;;;%s)",sid);LocalFree(sid);
 PSECURITY_DESCRIPTOR descriptor=NULL;
 if(!ConvertStringSecurityDescriptorToSecurityDescriptorA(sddl,SDDL_REVISION_1,&descriptor,NULL))return 1;
 SECURITY_ATTRIBUTES security={sizeof(security),descriptor,FALSE};
 for(;;) {
  HANDLE pipe=CreateNamedPipeA(b->endpoint,PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
   PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,4096,4096,0,&security);
  if(pipe==INVALID_HANDLE_VALUE)break;
  DWORD peer=0;
  if(pipe_connect(pipe)&&GetNamedPipeClientProcessId(pipe,&peer)&&peer!=0) {
   int hello=0;uint32_t last_id=0;
   for(;;) {
    BYTE packet[CONTROL_MAX_PACKET];DWORD count=0;
    if(!pipe_transfer(pipe,packet,sizeof(packet),0,&count)||count<sizeof(ControlHeader))break;
    ControlHeader header;memcpy(&header,packet,sizeof(header));
    if(!control_header_valid(&header,count))break;
    uint32_t error=dispatch(b,peer,&header,packet+sizeof(header),&hello,&last_id);
    struct {ControlHeader header;ControlReply reply;} response;
    response.header=(ControlHeader){CONTROL_MAGIC,CONTROL_VERSION,
     (uint16_t)(header.command|CONTROL_REPLY_BIT),header.request_id,sizeof(ControlReply),0};
    reply_snapshot(b,&response.reply,error);
    if(!pipe_transfer(pipe,&response,sizeof(response),1,&count)||count!=sizeof(response))break;
    if(error==CONTROL_BAD_PACKET||error==CONTROL_NEED_HELLO)break;
   }
  }
  if(peer)disconnect(b,peer);
  DisconnectNamedPipe(pipe);CloseHandle(pipe);
 }
 LocalFree(descriptor);return 0;
}
static DWORD WINAPI watchdog(void *opaque) {
 ControlBridge *b=(ControlBridge*)opaque;
 for(;;) {
  AcquireSRWLockExclusive(&b->gate);
  if(b->pending.connected&&GetTickCount64()-b->last_beat>=CONTROL_TIMEOUT_MS) {
   set_enabled(b,0);b->pending.connected=0;b->pending.configured=0;b->pending.peer_pid=0;
  }
  ReleaseSRWLockExclusive(&b->gate);Sleep(20);
 }
}
int control_start(ControlBridge *b) {
 memset(b,0,sizeof(*b));InitializeSRWLock(&b->gate);b->pending.settings=camera_defaults();
 b->pid=GetCurrentProcessId();FILETIME create,exit,kernel,user;
 if(!GetProcessTimes(GetCurrentProcess(),&create,&exit,&kernel,&user))return 0;
 b->created=((uint64_t)create.dwHighDateTime<<32)|create.dwLowDateTime;
 snprintf(b->endpoint,sizeof(b->endpoint),"\\\\.\\pipe\\FrontierOrbit.%lu.%016llx",
  (unsigned long)b->pid,(unsigned long long)b->created);
 HANDLE thread=CreateThread(NULL,0,watchdog,b,0,NULL);if(!thread)return 0;CloseHandle(thread);
 thread=CreateThread(NULL,0,pipe_worker,b,0,NULL);if(!thread)return 0;CloseHandle(thread);return 1;
}
