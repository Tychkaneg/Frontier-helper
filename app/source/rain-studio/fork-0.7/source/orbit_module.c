#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "orbit_math.h"
#include "orbit_session.h"
#include "collision_math.h"
#include "engine_guards.h"
typedef float * (WINAPI *LookAt)(float *,const float *,const float *,const float *);
typedef DWORD (WINAPI *PadRead)(DWORD,XINPUT_STATE *);
static HMODULE self;static BYTE *base;static LookAt original;static PadRead read_pad;
static volatile LONG status=0,enabled=1,epoch=0,busy=0,applied=0,blocked=0,no_pad=0,bad_actor=0;
static volatile LONG collision_calls=0,collision_hits=0,collision_unavailable=0,collision_invalid=0,boundary_hits=0,floor_calls=0,floor_hits=0,ground_calls=0,ground_hits=0,ground_unavailable=0;
static Orbit orbit;static OrbitSession session;static volatile LONG scene_resets=0;
static ULONGLONG last_tick=0;static WORD previous_buttons=0;
static float target_height=0;static char config[MAX_PATH],logfile[MAX_PATH];
static double yaw_speed,pitch_speed,lo,hi,response,deadzone,reset_pitch,distance_scale;
static double resolved_radius=0,collision_size,collision_margin,collision_recovery;
static int ground_collision,ground_samples;
static int native_collision,floor_collision;static unsigned collision_mask,floor_mask;
static int pad_index;
static const double RAD=0.017453292519943295;
static int safe_read(uintptr_t address,void *out,SIZE_T n){SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),(void*)address,out,n,&got)&&got==n;}
static double option(const char *key,double fallback,double min,double max){char b[80],def[80];snprintf(def,sizeof(def),"%.5f",fallback);GetPrivateProfileStringA("Camera",key,def,b,sizeof(b),config);double v=strtod(b,0);return isfinite(v)&&v>=min&&v<=max?v:fallback;}
static void log_state(const char *message){FILE *f=fopen(logfile,"a");if(f){fprintf(f,"%llu v0.7-rain %s status=%ld enabled=%ld applied=%ld blocked=%ld no_pad=%ld bad_actor=%ld collision_calls=%ld collision_hits=%ld collision_unavailable=%ld collision_invalid=%ld boundary_hits=%ld floor_calls=%ld floor_hits=%ld ground_calls=%ld ground_hits=%ld ground_unavailable=%ld\n",GetTickCount64(),message,status,enabled,applied,blocked,no_pad,bad_actor,collision_calls,collision_hits,collision_unavailable,collision_invalid,boundary_hits,floor_calls,floor_hits,ground_calls,ground_hits,ground_unavailable);fclose(f);}}
// ECX=start; four caller-cleaned stack arguments; EAX=hit boolean.
__attribute__((naked,noinline)) static int engine_trace(void *fn,const float *start,const float *end,float radius,float *out,unsigned mask){
 __asm__ volatile("push %ebp\nmov %esp,%ebp\npush 28(%ebp)\npush 24(%ebp)\npush 20(%ebp)\npush 16(%ebp)\nmov 12(%ebp),%ecx\ncall *8(%ebp)\nadd $16,%esp\nleave\nret");
}
// Stock camera ground wrapper takes ESI=position, returns XMM0, includes city geometry.
__attribute__((naked,noinline)) static void engine_ground(void *fn,const float *point,float *height){
 __asm__ volatile("push %ebp\nmov %esp,%ebp\npush %esi\nmov 12(%ebp),%esi\ncall *8(%ebp)\npop %esi\nmov 16(%ebp),%edx\nmovss %xmm0,(%edx)\nleave\nret");
}
typedef struct {uint32_t grid[13];double reference,clearance;} GroundContext;
static int ground_height(const float point[3],double *height,void *opaque){
 GroundContext *c=(GroundContext*)opaque;uint32_t *g=c->grid;
 double low=*(float*)(base+0x18e469c);
 if(point[0]<low||point[2]<low||point[0]>=(double)g[11]*g[8]-low||point[2]>=(double)g[10]*g[9]-low)return 0;
 uint32_t x=(uint32_t)(point[0]/g[8]),z=(uint32_t)(point[2]/g[9]);
 uint32_t cell=0;
 if(!safe_read((uintptr_t)g[12]+((uintptr_t)x*g[10]+z)*4,&cell,4)||!cell)return 0;
 float probe[3]={point[0],(float)fmax(c->reference,point[1]+c->clearance),point[2]},h;
 InterlockedIncrement(&ground_calls);engine_ground(base+0x8202a0,probe,&h);
 // No matching triangle returns probe Y; don't turn that into a fictitious floor.
 if(!isfinite(h)||fabs(h-probe[1])<0.001||fabs(h-probe[1])>10000)return 0;
 *height=h;return 1;
}
static int collision_resolve(const float target[3],float wanted[3],double dt){
 if(!native_collision)return 1;
 uint32_t map=0,grid[6];
 if(!safe_read((uintptr_t)base+0xe77dc2c,&map,4)||!map||!safe_read(map,grid,sizeof(grid))||
    grid[2]==0||grid[3]==0||grid[4]==0||grid[5]==0||grid[2]>100000||grid[3]>100000||grid[4]>100000||grid[5]>100000){
  InterlockedIncrement(&collision_unavailable);return 0;
 }
 double low=*(float*)(base+0x18e469c)+collision_size;
 double xmax=(double)grid[5]*grid[2]-low,zmax=(double)grid[4]*grid[3]-low;
 double fraction=collision_box_fraction(target,wanted,low,xmax,low,zmax);
 if(fraction<0||xmax<=low||zmax<=low){InterlockedIncrement(&collision_unavailable);return 0;}
 if(fraction<1){for(int i=0;i<3;i++)wanted[i]=(float)(target[i]+(wanted[i]-target[i])*fraction);InterlockedIncrement(&boundary_hits);}
 float adjusted[3];memcpy(adjusted,wanted,sizeof(adjusted));
 if(collision_calls==0)log_state("calling stock geometry query for first time");
 InterlockedIncrement(&collision_calls);
 int hit=engine_trace(base+0x8d0f10,target,wanted,(float)collision_size,adjusted,collision_mask);
 if(collision_calls==1)log_state("stock geometry query returned");
 double d[3],length=0;for(int i=0;i<3;i++){d[i]=wanted[i]-target[i];length+=d[i]*d[i];}
 length=sqrt(length);double allowed=length;
 if(hit){
  for(int i=0;i<3;i++)if(!isfinite(adjusted[i])){InterlockedIncrement(&collision_invalid);return 0;}
  allowed=fmax(0,collision_fraction(target,wanted,adjusted)*length-collision_margin);
  InterlockedIncrement(&collision_hits);
  if(collision_hits<=4){char message[240];snprintf(message,sizeof(message),"hit target=(%.1f %.1f %.1f) desired=(%.1f %.1f %.1f) adjusted=(%.1f %.1f %.1f) allowed=%.1f",target[0],target[1],target[2],wanted[0],wanted[1],wanted[2],adjusted[0],adjusted[1],adjusted[2],allowed);log_state(message);}
 }
 // Keep the stock wall mask; independently include surfaces it excludes.
 // Mask 4 is also used by the stock camera, but floor flags need runtime confirmation.
 if(floor_collision){
  float floor_adjusted[3];memcpy(floor_adjusted,wanted,sizeof(floor_adjusted));
  InterlockedIncrement(&floor_calls);
  int floor_hit=engine_trace(base+0x8d0f10,target,wanted,(float)collision_size,floor_adjusted,floor_mask);
  if(floor_hit){
   for(int i=0;i<3;i++)if(!isfinite(floor_adjusted[i])){InterlockedIncrement(&collision_invalid);return 0;}
   double floor_allowed=fmax(0,collision_fraction(target,wanted,floor_adjusted)*length-collision_margin);
   allowed=collision_nearest_limit(allowed,floor_allowed);
   InterlockedIncrement(&floor_hits);
   if(floor_hits<=8){char message[260];snprintf(message,sizeof(message),"floor-pass mask=0x%x target=(%.1f %.1f %.1f) desired=(%.1f %.1f %.1f) adjusted=(%.1f %.1f %.1f) allowed=%.1f",floor_mask,target[0],target[1],target[2],wanted[0],wanted[1],wanted[2],floor_adjusted[0],floor_adjusted[1],floor_adjusted[2],allowed);log_state(message);}
  }
 }
 if(ground_collision){
  GroundContext c;memset(&c,0,sizeof(c));c.reference=target[1];c.clearance=collision_size+collision_margin;
  if(safe_read(map,c.grid,sizeof(c.grid))&&c.grid[12]&&c.grid[8]>0&&c.grid[8]<=100000&&c.grid[9]>0&&c.grid[9]<=100000&&c.grid[10]>0&&c.grid[10]<=100000&&c.grid[11]>0&&c.grid[11]<=100000){
   float endpoint[3];for(int i=0;i<3;i++)endpoint[i]=(float)(target[i]+d[i]*allowed/fmax(length,.01));
   double f=collision_ground_fraction(target,endpoint,c.clearance,ground_samples,ground_height,&c);
   if(f<1){allowed*=f;InterlockedIncrement(&ground_hits);
    if(ground_hits<=8){char message[220];snprintf(message,sizeof(message),"ground-height clipped targetY=%.1f wantedY=%.1f allowed=%.1f fraction=%.5f",target[1],wanted[1],allowed,f);log_state(message);}
   }
  }else InterlockedIncrement(&ground_unavailable);
 }
 resolved_radius=collision_radius(resolved_radius,allowed,dt,collision_recovery);
 double actual=fmin(allowed,fmax(0.01,resolved_radius));
 if(length<0.01||actual<0.01){InterlockedIncrement(&collision_invalid);return 0;}
 for(int i=0;i<3;i++)wanted[i]=(float)(target[i]+d[i]*actual/length);
 return 1;
}
static int pad(XINPUT_STATE *p){if(pad_index>=0)return read_pad(pad_index,p)==ERROR_SUCCESS;for(DWORD i=0;i<4;i++)if(read_pad(i,p)==ERROR_SUCCESS)return 1;return 0;}
static void apply_camera(const float *eye,const float *target){
 if(!enabled){orbit_session_suspend(&session);return;}
 BYTE channel=*(BYTE*)(base+0x1c4a409);signed char special=*(signed char*)(base+0x1c4a41d);
 if(channel!=0||special!=-1){InterlockedIncrement(&blocked);orbit_session_suspend(&session);return;}
 DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
 if(foreground!=GetCurrentProcessId()){last_tick=GetTickCount64();return;}
 XINPUT_STATE p;if(!pad(&p)){InterlockedIncrement(&no_pad);orbit_session_suspend(&session);return;}
 uint32_t actor=0;float pos[3];short heading=0;
 if(!safe_read((uintptr_t)base+0x1c49d50+0x384,&actor,4)||!actor||
    !safe_read(actor+0xac,pos,sizeof(pos))||!safe_read(actor+0xa4,&heading,2)){
  InterlockedIncrement(&bad_actor);orbit_session_suspend(&session);return;
 }
 for(int i=0;i<3;i++)if(!isfinite(pos[i])||!isfinite(eye[i])||!isfinite(target[i])){InterlockedIncrement(&bad_actor);orbit_session_suspend(&session);return;}
 ULONGLONG now=GetTickCount64();double dt=(now-last_tick)/1000.0;last_tick=now;
 uint32_t map=0,grid[13]={0};
 int map_ready=safe_read((uintptr_t)base+0xe77dc2c,&map,4)&&map&&safe_read(map,grid,sizeof(grid));
 if(native_collision&&(!map_ready||!grid[2]||!grid[3]||!grid[4]||!grid[5])){
  orbit_session_suspend(&session);return;
 }
 if(!orbit_session_ready(&session,now,actor,map,grid[6],grid[12],pos,epoch))return;
 if(session.pending){
  double x=target[0]-eye[0],y=target[1]-eye[1],z=target[2]-eye[2];
  int first=!session.calibrated;
  if(!orbit_session_calibrate(&session,sqrt(x*x+y*y+z*z),target[1]-pos[1],distance_scale)){
   InterlockedIncrement(&bad_actor);return;
  }
  orbit.radius=session.radius;target_height=(float)session.height;
  orbit.yaw=orbit.want_yaw=first?atan2(x,z):heading*6.283185307179586/65536.0;
  double pitch=first?atan2(y,sqrt(x*x+z*z)):reset_pitch;
  orbit.pitch=orbit.want_pitch=fmax(lo,fmin(hi,pitch));
  resolved_radius=orbit.radius;session.pending=0;
  previous_buttons=p.Gamepad.wButtons;dt=0;
  InterlockedIncrement(&scene_resets);
  char message[160];snprintf(message,sizeof(message),"scene resync #%ld reference_radius=%.1f target_height=%.1f map=%08lx",scene_resets,orbit.radius,target_height,(unsigned long)map);log_state(message);
 }

 WORD buttons=p.Gamepad.wButtons;
 int reset=(buttons&XINPUT_GAMEPAD_LEFT_THUMB)&&!(previous_buttons&XINPUT_GAMEPAD_LEFT_THUMB);
 previous_buttons=buttons;
 orbit_tick(&orbit,orbit_axis(p.Gamepad.sThumbRX,deadzone),orbit_axis(p.Gamepad.sThumbRY,deadzone),dt,
  yaw_speed,pitch_speed,lo,hi,response,reset,heading*6.283185307179586/65536.0,reset_pitch);
 float t[3]={pos[0],pos[1]+target_height,pos[2]},e[3];orbit_eye(&orbit,t,e);
 if(!collision_resolve(t,e,dt))return;
 // Only called at the engine's LookAt consumption on its rendering thread.
 memcpy((void*)target,t,sizeof(t));memcpy((void*)eye,e,sizeof(e));
 // Keep the already-validated legacy yaw state aligned with the view.
 uint16_t yaw_raw=(uint16_t)(lrint(orbit.yaw*65536.0/6.283185307179586)-32768);
 memcpy(base+0x1c49d50+0x418,&yaw_raw,2);memcpy(base+0x1c49d50+0x41a,&yaw_raw,2);
 InterlockedIncrement(&applied);
}
static float * WINAPI hooked_lookat(float *out,const float *eye,const float *target,const float *up){
 uintptr_t camera=*(uint32_t*)(base+0xedaad60);
 if(camera==(uintptr_t)base+0xe7fff40&&(uintptr_t)eye==camera+0x80&&(uintptr_t)target==camera+0x8c){
  if(InterlockedCompareExchange(&busy,1,0)==0){apply_camera(eye,target);InterlockedExchange(&busy,0);}
 }
 return original(out,eye,target,up);
}
__declspec(dllexport) DWORD WINAPI OrbitStatus(void *unused){(void)unused;return status;}
__declspec(dllexport) DWORD WINAPI OrbitDisable(void *unused){(void)unused;InterlockedExchange(&enabled,0);InterlockedIncrement(&epoch);return 1;}
__declspec(dllexport) DWORD WINAPI OrbitEnable(void *unused){(void)unused;InterlockedExchange(&enabled,1);InterlockedIncrement(&epoch);return 1;}
static DWORD WINAPI initialize(void *unused){
 (void)unused;char path[MAX_PATH];GetModuleFileNameA(self,path,sizeof(path));char *slash=strrchr(path,'\\');if(!slash){status=-1;return 0;}slash[1]=0;
 snprintf(config,sizeof(config),"%sorbit.ini",path);snprintf(logfile,sizeof(logfile),"%sorbit.log",path);
 log_state("initializing");base=(BYTE*)0x10000000;if(!base){status=-2;log_state("HD module absent");return 0;}
 if(!engine_guards(base)){status=-3;log_state("code guards mismatch");return 0;}
 HMODULE d3dx=GetModuleHandleA("d3dx9_43.dll");if(!d3dx){status=-4;log_state("D3DX absent");return 0;}
 original=(LookAt)GetProcAddress(d3dx,"D3DXMatrixLookAtRH");
 LookAt *slot=(LookAt*)(base+0x15d242c);if(!original||*slot!=original){status=-5;log_state("LookAt slot already altered");return 0;}
 HMODULE xi=LoadLibraryA("xinput1_4.dll");if(!xi)xi=LoadLibraryA("xinput9_1_0.dll");
 read_pad=xi?(PadRead)GetProcAddress(xi,"XInputGetState"):0;if(!read_pad){status=-6;log_state("XInput unavailable");return 0;}
 yaw_speed=option("YawSpeed",180,20,720)*RAD;pitch_speed=option("PitchSpeed",100,10,360)*RAD;
 lo=option("MinPitch",-65,-85,0)*RAD;hi=option("MaxPitch",35,0,85)*RAD;
 response=option("Response",16,1,100);deadzone=option("Deadzone",0.18,0.02,0.5);
 reset_pitch=option("ResetPitch",-15,-65,35)*RAD;distance_scale=option("DistanceScale",1,0.5,1.5);
 pad_index=(int)option("GamepadIndex",-1,-1,3);
 native_collision=(int)option("NativeCollision",1,0,1);
 collision_size=option("CollisionRadius",28,5,80);collision_margin=option("CollisionMargin",4,0,20);
 collision_recovery=option("CollisionRecovery",5,1,30);collision_mask=(unsigned)option("CollisionMask",32777,0,65535);
 floor_collision=(int)option("FloorCollision",1,0,1);floor_mask=(unsigned)option("FloorMask",4,0,65535);
 ground_collision=(int)option("GroundCollision",1,0,1);ground_samples=(int)option("GroundSamples",24,8,64);
 DWORD old=0,ignored=0;if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old)){status=-7;log_state("IAT protection failed");return 0;}
 void *previous=InterlockedCompareExchangePointer((void*volatile*)slot,(void*)hooked_lookat,(void*)original);
 BOOL protection_ok=VirtualProtect(slot,sizeof(void*),old,&ignored);
 if(previous!=(void*)original){status=-8;log_state("IAT changed concurrently");return 0;}
 if(!protection_ok){InterlockedExchange(&enabled,0);status=-9;log_state("page protection restore failed; restart game");return 0;}
 status=2;log_state("HOOK INSTALLED; F8 toggles, L3 resets");Beep(850,80);
 int was_down=0;ULONGLONG logged=0;
 while(1){
  DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
  int down=(GetAsyncKeyState(VK_F8)&0x8000)!=0;
  if(down&&!was_down&&foreground==GetCurrentProcessId()){
   InterlockedExchange(&enabled,!enabled);InterlockedIncrement(&epoch);log_state("F8 toggle");Beep(enabled?850:450,70);
  }
  was_down=down;ULONGLONG now=GetTickCount64();if(now-logged>2000){log_state("heartbeat");logged=now;}
  Sleep(20);
 }
 return 0;
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,void *reserved){(void)reserved;if(reason==DLL_PROCESS_ATTACH){self=h;DisableThreadLibraryCalls(h);HANDLE t=CreateThread(0,0,initialize,0,0,0);if(t)CloseHandle(t);else status=-10;}return TRUE;}
