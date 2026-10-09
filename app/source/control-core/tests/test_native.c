#include "pipe_client.h"
#include "../build/fixture_guards.h"
typedef float *(WINAPI *LookAt)(float*,const float*,const float*,const float*);
typedef DWORD (WINAPI *Status)(void*);
static BYTE *engine;
static void put(unsigned rva,const void *data,size_t size) {
 DWORD old;assert(VirtualProtect(engine+rva,size,PAGE_EXECUTE_READWRITE,&old));
 memcpy(engine+rva,data,size);assert(FlushInstructionCache(GetCurrentProcess(),engine+rva,size));
}
static void u32(unsigned rva,uint32_t value) {put(rva,&value,4);}
static void render_frames(int count,HANDLE pipe,uint32_t *id,float radius) {
 for(int i=0;i<count;i++) {
  float eye[3]={1000,100,1000-radius},target[3]={1000,100,1000},up[3]={0,1,0},matrix[16];
  put(0xe7fff40+0x80,eye,sizeof(eye));put(0xe7fff40+0x8c,target,sizeof(target));
  LookAt fn=*(LookAt*)(engine+0x15d242c);
  assert(fn(matrix,(float*)(engine+0xe7fff40+0x80),(float*)(engine+0xe7fff40+0x8c),up)==matrix);
  if(i%20==0)assert(request(pipe,CONTROL_PING,(*id)++,NULL).error==CONTROL_OK);
  MSG message;while(PeekMessageA(&message,NULL,0,0,PM_REMOVE)) {
   TranslateMessage(&message);DispatchMessageA(&message);
  }
  Sleep(16);
 }
}
int main(int argc,char **argv) {
 int bad=argc>1&&!strcmp(argv[1],"--bad-guards");
 HMODULE matrix=LoadLibraryA("d3dx9_43.dll");assert(matrix);
 engine=(BYTE*)LoadLibraryA("mhfo-hd.dll");assert(engine);
 for(unsigned i=0;i<sizeof(fixture_guards)/sizeof(fixture_guards[0]);i++)
  put(fixture_guards[i].rva,fixture_guards[i].bytes,fixture_guards[i].length);
 if(bad){BYTE invalid=0;put(fixture_guards[0].rva,&invalid,1);}
 LookAt original=(LookAt)GetProcAddress(matrix,"D3DXMatrixLookAtRH");assert(original);
 put(0x15d242c,&original,sizeof(original));
 u32(0xedaad60,(uint32_t)(uintptr_t)engine+0xe7fff40);
 u32(0x1c4a0d4,(uint32_t)(uintptr_t)engine+0xe000000);
 u32(0xe77dc2c,(uint32_t)(uintptr_t)engine+0xe010000);
 BYTE channel=0;signed char special=-1;put(0x1c4a409,&channel,1);put(0x1c4a41d,&special,1);
 float actor[3]={1000,0,1000};put(0xe000000+0xac,actor,sizeof(actor));
 uint32_t grid[13]={0};grid[2]=grid[3]=grid[8]=grid[9]=128;
 grid[4]=grid[5]=grid[10]=grid[11]=16;grid[6]=(uint32_t)(uintptr_t)engine+0xe030000;
 grid[12]=(uint32_t)(uintptr_t)engine+0xe020000;
 put(0xe010000,grid,sizeof(grid));put(0xe011000,grid,sizeof(grid));
 uint32_t cells[256];for(int i=0;i<256;i++)cells[i]=1;put(0xe020000,cells,sizeof(cells));
 float low=0;put(0x18e469c,&low,sizeof(low));
 HMODULE module=LoadLibraryA("FrontierOrbitControl.dll");assert(module);
 Status status=(Status)GetProcAddress(module,"OrbitStatus@4");
 if(!status)status=(Status)GetProcAddress(module,"OrbitStatus");assert(status);
 for(int i=0;i<100&&status(NULL)==0;i++)Sleep(20);
 assert((int32_t)status(NULL)==(bad?-3:2));
 uint32_t id=1;HANDLE pipe=connect_controller(GetCurrentProcessId(),process_created());
 assert(request(pipe,CONTROL_HELLO,id++,NULL).error==CONTROL_OK);
 CameraSettings settings=camera_defaults();
 assert(request(pipe,CONTROL_SETTINGS,id++,&settings).error==CONTROL_OK);
 if(bad) {
  assert(request(pipe,CONTROL_ENABLE,id++,NULL).error==CONTROL_ENGINE_UNAVAILABLE);
  assert(*(LookAt*)(engine+0x15d242c)==original);CloseHandle(pipe);
  puts("PASS: production DLL rejects mismatched engine without installing the camera hook.");return 0;
 }
 /* Replace only the synthetic geometry functions after the real guard check.
    Query returns no wall hit; ground wrapper returns height 0 in XMM0. */
 const BYTE trace_stub[]={0x31,0xc0,0xc3},ground_stub[]={0x0f,0x57,0xc0,0xc3};
 put(0x8d0f10,trace_stub,sizeof(trace_stub));put(0x8202a0,ground_stub,sizeof(ground_stub));
 HWND window=CreateWindowExA(0,"STATIC","Orbit native test fixture",WS_OVERLAPPEDWINDOW,
  0,0,320,200,NULL,NULL,GetModuleHandleA(NULL),NULL);assert(window);
 ShowWindow(window,SW_SHOW);SetForegroundWindow(window);
 DWORD fg=0;GetWindowThreadProcessId(GetForegroundWindow(),&fg);assert(fg==GetCurrentProcessId());
 render_frames(2,pipe,&id,600);assert(((float*)(engine+0xe7fff40+0x80))[2]==400);
 assert(request(pipe,CONTROL_ENABLE,id++,NULL).error==CONTROL_OK);
 render_frames(40,pipe,&id,600);
 ControlReply reply=request(pipe,CONTROL_STATUS,id++,NULL);
 assert(reply.scene==CAMERA_ACTIVE&&reply.frames>0&&reply.reference_radius==600);
 settings.distance_scale=1.5f;settings.height_offset=40;settings.shoulder_offset=70;
 reply=request(pipe,CONTROL_SETTINGS,id++,&settings);uint64_t revision=reply.accepted_revision;
 render_frames(5,pipe,&id,600);reply=request(pipe,CONTROL_STATUS,id++,NULL);
 assert(reply.applied_revision==revision&&reply.requested_radius==900&&reply.actual_radius>600);
 float *target=(float*)(engine+0xe7fff40+0x8c);
 assert(fabs(target[0]-1070)<.01&&fabs(target[1]-140)<.01);
 /* Simulate map replacement plus a transient native camera 85 units away. */
 u32(0xe77dc2c,(uint32_t)(uintptr_t)engine+0xe011000);
 render_frames(40,pipe,&id,85);reply=request(pipe,CONTROL_STATUS,id++,NULL);
 assert(reply.scene==CAMERA_ACTIVE&&reply.reference_radius==600&&reply.requested_radius==900);
 assert(reply.settings.height_offset==40&&reply.actual_radius>800);
 settings.height_offset=-250;settings.shoulder_offset=0;
 settings.min_pitch=0;settings.reset_pitch=0;
 assert(request(pipe,CONTROL_SETTINGS,id++,&settings).error==CONTROL_OK);
 render_frames(5,pipe,&id,600);reply=request(pipe,CONTROL_STATUS,id++,NULL);
 assert(reply.scene==CAMERA_ACTIVE&&target[1]>=31.999&&target[1]<32.1);
 assert(((float*)(engine+0xe7fff40+0x80))[1]>=31.999);
 assert(request(pipe,CONTROL_RESET,id++,NULL).error==CONTROL_OK);
 render_frames(2,pipe,&id,600);reply=request(pipe,CONTROL_STATUS,id++,NULL);
 assert(reply.reset_applied==reply.reset_accepted);
 assert(request(pipe,CONTROL_DISABLE,id++,NULL).error==CONTROL_OK);
 uint64_t frames=reply.frames;render_frames(2,pipe,&id,600);
 reply=request(pipe,CONTROL_STATUS,id++,NULL);
 assert(reply.frames==frames&&reply.enabled==0&&reply.scene==CAMERA_DISABLED);
 assert(((float*)(engine+0xe7fff40+0x80))[2]==400&&target[1]==100);
 CloseHandle(pipe);DestroyWindow(window);
 puts("PASS: actual x86 DLL loader/hook, default disabled, live profile, native ABI floor clamp, map-short-distance regression, reset and native return. Synthetic engine only.");
 return 0;
}
