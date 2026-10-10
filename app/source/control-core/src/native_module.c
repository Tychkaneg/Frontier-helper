#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include "control_win.h"
#include <xinput.h>
#include <stdio.h>
#ifdef FRONTIER_RAIN
#include "../../src/engine_compat.h"
#else
#include "../../src/engine_guards.h"
#endif

typedef float *(WINAPI *LookAt)(float *,const float *,const float *,const float *);
typedef DWORD (WINAPI *PadRead)(DWORD,XINPUT_STATE *);
static BYTE *engine;
static LookAt original;
static PadRead read_pad;
static ControlBridge bridge;
static ControlSnapshot snapshot;
static CameraCore camera;

static int safe_read(uintptr_t address,void *out,SIZE_T size) {
 SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),(void*)address,out,size,&got)&&got==size;
}
/* Existing, researched x86 engine ABI. Only called on the game's rendering thread. */
__attribute__((naked,noinline)) static int engine_trace(void *fn,const float *start,
 const float *end,float radius,float *out,unsigned mask) {
 __asm__ volatile("push %ebp\nmov %esp,%ebp\npush 28(%ebp)\npush 24(%ebp)\npush 20(%ebp)\npush 16(%ebp)\nmov 12(%ebp),%ecx\ncall *8(%ebp)\nadd $16,%esp\nleave\nret");
}
__attribute__((naked,noinline)) static void engine_ground(void *fn,const float *point,float *height) {
 __asm__ volatile("push %ebp\nmov %esp,%ebp\npush %esi\nmov 12(%ebp),%esi\ncall *8(%ebp)\npop %esi\nmov 16(%ebp),%edx\nmovss %xmm0,(%edx)\nleave\nret");
}
typedef struct {uint32_t grid[13];double reference,clearance,low;} GroundContext;
static int ground_height(const float point[3],double *height,void *opaque) {
 GroundContext *c=(GroundContext*)opaque;uint32_t *g=c->grid;
 if(point[0]<c->low||point[2]<c->low||point[0]<0||point[2]<0||
  point[0]>=(double)g[11]*g[8]-c->low||point[2]>=(double)g[10]*g[9]-c->low)return 0;
 uint32_t x=(uint32_t)(point[0]/g[8]),z=(uint32_t)(point[2]/g[9]),cell=0;
 if(!safe_read((uintptr_t)g[12]+((uintptr_t)x*g[10]+z)*4,&cell,4)||!cell)return 0;
 float probe[3]={point[0],(float)fmax(c->reference,point[1]+c->clearance),point[2]},h;
 engine_ground(engine+0x8202a0,probe,&h);
 if(!isfinite(h)||fabs(h-probe[1])<.001||fabs(h-probe[1])>10000)return 0;
 *height=h;return 1;
}
/* Stateless geometry: never changes the distance recovery cache. */
static int native_sweep(const float start[3],const float end[3],double *fraction,void *unused) {
 (void)unused;
 const double radius=28,margin=4;
 uint32_t map=0;GroundContext ground={0};float raw_low=0;
 if(!safe_read((uintptr_t)engine+0xe77dc2c,&map,4)||!map||
  !safe_read(map,ground.grid,sizeof(ground.grid))||
  !safe_read((uintptr_t)engine+0x18e469c,&raw_low,4)||!isfinite(raw_low))return 0;
 uint32_t *g=ground.grid;
 if(!g[2]||!g[3]||!g[4]||!g[5]||g[2]>100000||g[3]>100000||g[4]>100000||g[5]>100000)return 0;
 double low=raw_low+radius,xmax=(double)g[5]*g[2]-low,zmax=(double)g[4]*g[3]-low;
 double box=collision_box_fraction(start,end,low,xmax,low,zmax);
 if(box<0||xmax<=low||zmax<=low)return 0;
 float bounded[3];double total=0,length=0;
 for(int i=0;i<3;i++) {
  double delta=end[i]-start[i];total+=delta*delta;
  bounded[i]=(float)(start[i]+delta*box);
  double d=bounded[i]-start[i];length+=d*d;
 }
 total=sqrt(total);length=sqrt(length);
 if(total<.00001){*fraction=1;return 1;}
 double allowed=length;
 const unsigned masks[]={0x8009,4}; /* Prior Orbit wall and independent floor passes. */
 for(unsigned pass=0;pass<2;pass++) {
  float hit[3];memcpy(hit,bounded,sizeof(hit));
  if(engine_trace(engine+0x8d0f10,start,bounded,(float)radius,hit,masks[pass])) {
   for(int i=0;i<3;i++)if(!isfinite(hit[i]))return 0;
   allowed=fmin(allowed,fmax(0,collision_fraction(start,bounded,hit)*length-margin));
  }
 }
 ground.reference=start[1];ground.clearance=radius+margin;ground.low=raw_low;
 if(g[12]&&g[8]>0&&g[8]<=100000&&g[9]>0&&g[9]<=100000&&
  g[10]>0&&g[10]<=100000&&g[11]>0&&g[11]<=100000) {
  float endpoint[3];
  for(int i=0;i<3;i++)endpoint[i]=(float)(start[i]+(bounded[i]-start[i])*allowed/fmax(length,.01));
  allowed*=collision_ground_fraction(start,endpoint,ground.clearance,24,ground_height,&ground);
 }else return 0; /* Missing floor context: keep the stock camera. */
 *fraction=fmax(0,fmin(1,allowed/total));return 1;
}
static int pad(XINPUT_STATE *p,int index) {
 if(!read_pad)return 0;
 if(index>=0)return read_pad((DWORD)index,p)==ERROR_SUCCESS;
 for(DWORD i=0;i<4;i++)if(read_pad(i,p)==ERROR_SUCCESS)return 1;
 return 0;
}
static void apply_camera(const float *eye,const float *target) {
 ControlSnapshot latest;
 if(control_snapshot(&bridge,&latest))snapshot=latest;
 if(snapshot.revision!=camera.revision)
  camera_core_settings(&camera,&snapshot.settings,snapshot.revision);
 CameraFrame frame={0};frame.now=GetTickCount64();frame.reset_serial=snapshot.reset_serial;
 frame.enabled=InterlockedCompareExchange(&bridge.enabled,0,0);
 frame.epoch=InterlockedCompareExchange(&bridge.epoch,0,0);
 /* A newly enabled connection must consume its own profile before the first write. */
 frame.enabled=frame.enabled&&snapshot.connected&&snapshot.configured&&
  snapshot.control_epoch==frame.epoch;
 DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
 frame.focus_allowed=camera_focus_allowed(bridge.pid,foreground,snapshot.peer_pid,
  snapshot.connected,snapshot.settings.preview);
 frame.input_allowed=foreground==bridge.pid;
 BYTE channel=1;signed char special=0;uint32_t actor=0,map=0,grid[13]={0};short heading=0;
 frame.scene_valid=safe_read((uintptr_t)engine+0x1c4a409,&channel,1)&&channel==0&&
  safe_read((uintptr_t)engine+0x1c4a41d,&special,1)&&special==-1&&
  safe_read((uintptr_t)engine+0x1c4a0d4,&actor,4)&&actor&&
  safe_read(actor+0xac,frame.position,sizeof(frame.position))&&safe_read(actor+0xa4,&heading,2)&&
  safe_read((uintptr_t)engine+0xe77dc2c,&map,4)&&map&&safe_read(map,grid,sizeof(grid))&&
  grid[2]&&grid[3]&&grid[4]&&grid[5];
 frame.actor=actor;frame.map=map;frame.walls=grid[6];frame.floor=grid[12];
 frame.heading=heading*6.283185307179586/65536.0;
 memcpy(frame.native_eye,eye,sizeof(frame.native_eye));
 memcpy(frame.native_target,target,sizeof(frame.native_target));
 XINPUT_STATE p={0};frame.pad_present=pad(&p,snapshot.settings.gamepad_index);
 frame.stick_x=p.Gamepad.sThumbRX;frame.stick_y=p.Gamepad.sThumbRY;frame.buttons=p.Gamepad.wButtons;
 CameraView view={0};
 if(camera_core_step(&camera,&frame,native_sweep,NULL,&view)==CAMERA_ACTIVE) {
  memcpy((void*)target,view.target,sizeof(view.target));memcpy((void*)eye,view.eye,sizeof(view.eye));
  uint16_t yaw=(uint16_t)(lrint(camera.orbit.yaw*65536.0/6.283185307179586)-32768);
  memcpy(engine+0x1c4a168,&yaw,2);memcpy(engine+0x1c4a16a,&yaw,2);
 }
 ControlTelemetry t={0};
 t.applied_revision=camera.applied_revision;t.frames=camera.frames;t.reset_applied=camera.reset_applied;
 t.scene=camera.scene;t.pad_present=frame.pad_present;
 t.requested_radius=(float)(camera.reference.radius*camera.settings.distance_scale);
 t.actual_radius=(float)view.actual_radius;t.reference_radius=(float)camera.reference.radius;
 t.reference_height=(float)camera.reference.height;
 t.yaw=(float)(camera.orbit.yaw/CAMERA_RAD);t.pitch=(float)(camera.orbit.pitch/CAMERA_RAD);
 control_publish(&bridge,&t);
}
static float *WINAPI hooked_lookat(float *out,const float *eye,const float *target,const float *up) {
 uint32_t object=0;
 if(safe_read((uintptr_t)engine+0xedaad60,&object,4)&&object==(uintptr_t)engine+0xe7fff40&&
  (uintptr_t)eye==object+0x80&&(uintptr_t)target==object+0x8c&&
  InterlockedCompareExchange(&bridge.render_busy,1,0)==0) {
  apply_camera(eye,target);InterlockedExchange(&bridge.render_busy,0);
 }
 return original(out,eye,target,up);
}
static int checked_guards(BYTE *base) {
#ifdef FRONTIER_RAIN
 return frontier_engine_compatible(GetCurrentProcess(),(uintptr_t)base,NULL);
#else
 static const uintptr_t offsets[]={12250640,12251904,12250464,8524368,8519312,
  9244432,9243456,9217888,20874464,9223728,9220096,9204928,0x8202a0};
 BYTE bytes[64];
 for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++)
  if(!safe_read((uintptr_t)base+offsets[i],bytes,sizeof(bytes)))return 0;
 return engine_guards(base);
#endif
}
__declspec(dllexport) DWORD WINAPI OrbitControlVersion(void *unused) {(void)unused;return CONTROL_VERSION;}
__declspec(dllexport) DWORD WINAPI OrbitStatus(void *unused) {
 (void)unused;return (DWORD)InterlockedCompareExchange(&bridge.engine_status,0,0);
}
static DWORD WINAPI initialize(void *unused) {
 (void)unused;camera_core_init(&camera);snapshot.settings=camera_defaults();
 if(!control_start(&bridge)){InterlockedExchange(&bridge.engine_status,-11);return 0;}
#ifdef FRONTIER_RAIN
 /* Rain 0.7's verified engine is manually mapped, not a loader-registered DLL. */
 engine=(BYTE*)0x10000000;
#else
 engine=(BYTE*)GetModuleHandleA("mhfo-hd.dll");
#endif
 if(!engine){InterlockedExchange(&bridge.engine_status,-2);return 0;}
 if(!checked_guards(engine)){InterlockedExchange(&bridge.engine_status,-3);return 0;}
 HMODULE d3dx=GetModuleHandleA("d3dx9_43.dll");
 original=d3dx?(LookAt)GetProcAddress(d3dx,"D3DXMatrixLookAtRH"):NULL;
 LookAt *slot=(LookAt*)(engine+0x15d242c);LookAt current=NULL;
 if(!original||!safe_read((uintptr_t)slot,&current,sizeof(current))||current!=original) {
  InterlockedExchange(&bridge.engine_status,-5);return 0;
 }
 HMODULE xi=LoadLibraryA("xinput1_4.dll");if(!xi)xi=LoadLibraryA("xinput9_1_0.dll");
 read_pad=xi?(PadRead)GetProcAddress(xi,"XInputGetState"):NULL;
 /* A missing gamepad library no longer blocks settings preview. */
 DWORD old=0,ignored=0;
 if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old)) {
  InterlockedExchange(&bridge.engine_status,-7);return 0;
 }
 void *previous=InterlockedCompareExchangePointer((void*volatile*)slot,(void*)hooked_lookat,(void*)original);
 BOOL restored=VirtualProtect(slot,sizeof(void*),old,&ignored);
 if(previous!=(void*)original){InterlockedExchange(&bridge.engine_status,-8);return 0;}
 if(!restored){InterlockedExchange(&bridge.engine_status,-9);return 0;}
 InterlockedExchange(&bridge.engine_status,2);
 int was_down=0;
 for(;;) {
  DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
  int down=(GetAsyncKeyState(VK_F8)&0x8000)!=0;
  if(down&&!was_down&&foreground==bridge.pid)control_toggle(&bridge);
  was_down=down;Sleep(20);
 }
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,void *reserved) {
 (void)reserved;
 if(reason==DLL_PROCESS_ATTACH) {
  DisableThreadLibraryCalls(h);HANDLE thread=CreateThread(NULL,0,initialize,NULL,0,NULL);
  if(thread)CloseHandle(thread);else InterlockedExchange(&bridge.engine_status,-10);
 }
 return TRUE;
}
