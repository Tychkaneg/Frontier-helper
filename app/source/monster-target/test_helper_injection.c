#include "../control-core/tests/pipe_client.h"
#include "../src/engine_compat.h"
#include "monster_guards.h"
#include <xinput.h>
typedef float *(WINAPI *LookAt)(float*,const float*,const float*,const float*);
typedef DWORD (WINAPI *Status)(void*);
typedef BOOL (WINAPI *SnapshotRead)(void*,DWORD);
static BYTE *engine;
static void commit(uintptr_t rva,SIZE_T size,DWORD protect) {
 uintptr_t begin=rva&~(uintptr_t)4095,end=(rva+size+4095)&~(uintptr_t)4095;
 assert(VirtualAlloc(engine+begin,end-begin,MEM_COMMIT,protect)==engine+begin);
}
static void put(uintptr_t rva,const void *bytes,SIZE_T size) {memcpy(engine+rva,bytes,size);}
static void u32(uintptr_t rva,uint32_t value) {put(rva,&value,4);}
static void prepare_engine(void) {
 engine=VirtualAlloc((void*)0x10000000,0xf200000,MEM_RESERVE,PAGE_NOACCESS);assert(engine==(BYTE*)0x10000000);
 for(unsigned i=0;i<sizeof(frontier_blocks)/sizeof(frontier_blocks[0]);i++) {
  commit(frontier_blocks[i].rva,frontier_blocks[i].size,PAGE_EXECUTE_READWRITE);
  put(frontier_blocks[i].rva,frontier_blocks[i].code,frontier_blocks[i].size);
 }
 for(unsigned i=0;i<sizeof(monster_guards)/sizeof(monster_guards[0]);i++) {commit(monster_guards[i].rva,monster_guards[i].size,PAGE_EXECUTE_READWRITE);put(monster_guards[i].rva,monster_guards[i].data,monster_guards[i].size);}
 const uintptr_t data[]={0xed7ad2c,0xedb7600,0x1a422c4,0x15d242c,0x18e469c,0x1c4a0d4,0x1c4a168,0x1c4a409,0x1c4a41d,
  0xe77dc2c,0xe7fff40,0xedaad60,0xe000000,0xe010000,0xe011000,0xe020000,0xe030000};
 for(unsigned i=0;i<sizeof(data)/sizeof(data[0]);i++)commit(data[i],256,PAGE_READWRITE);
 commit(0xe040000,0xef0*40,PAGE_READWRITE);u32(0xed7ad2c,0x1e040000);u32(0x1a422c4,0x85442351);
 HMODULE d3dx=LoadLibraryA("d3dx9_43.dll");assert(d3dx);
 LookAt fn=(LookAt)GetProcAddress(d3dx,"D3DXMatrixLookAtRH");assert(fn);put(0x15d242c,&fn,4);
 u32(0xedaad60,0x1e7fff40);u32(0x1c4a0d4,0x1e000000);u32(0xe77dc2c,0x1e010000);
 engine[0x1c4a409]=0;engine[0x1c4a41d]=255;
 float actor[3]={1000,0,1000};put(0xe000000+0xac,actor,sizeof(actor));
 uint32_t grid[13]={0};grid[2]=grid[3]=grid[8]=grid[9]=128;
 grid[4]=grid[5]=grid[10]=grid[11]=16;grid[6]=0x1e030000;grid[12]=0x1e020000;
 put(0xe010000,grid,sizeof(grid));put(0xe011000,grid,sizeof(grid));
 for(unsigned i=0;i<256;i++)u32(0xe020000+i*4,1);
}
static void geometry_stubs(void) {
 const BYTE trace[]={0x31,0xc0,0xc3},ground[]={0x0f,0x57,0xc0,0xc3};
 put(0x8d0f10,trace,sizeof(trace));put(0x8202a0,ground,sizeof(ground));
}
static void render(int count,HANDLE pipe,uint32_t *id) {
 for(int i=0;i<count;i++) {
  float eye[3]={1000,100,400},target[3]={1000,100,1000},up[3]={0,1,0},matrix[16];
  put(0xe7fff40+0x80,eye,sizeof(eye));put(0xe7fff40+0x8c,target,sizeof(target));
  LookAt fn=*(LookAt*)(engine+0x15d242c);
  assert(fn(matrix,(float*)(engine+0xe7fffc0),(float*)(engine+0xe7fffcc),up)==matrix);
  if(i%20==0)assert(request(pipe,CONTROL_PING,(*id)++,NULL).error==CONTROL_OK);
  MSG message;while(PeekMessageA(&message,NULL,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageA(&message);}
  Sleep(16);
 }
}
typedef void (WINAPI *SetPad)(WORD,SHORT,SHORT,BYTE,BYTE);
static DWORD fake_focus;
static HWND WINAPI focus_window(void) {return (HWND)1;}
static DWORD WINAPI focus_pid(HWND window,DWORD *pid) {(void)window;if(pid)*pid=fake_focus;return 1;}
/* Only this isolated fixture's imports change; the real game's window/focus is untouched. */
static void fixture_focus(HMODULE m) {
 BYTE *b=(BYTE*)m;IMAGE_DOS_HEADER *dos=(IMAGE_DOS_HEADER*)b;IMAGE_NT_HEADERS *nt=(IMAGE_NT_HEADERS*)(b+dos->e_lfanew);
 IMAGE_IMPORT_DESCRIPTOR *d=(IMAGE_IMPORT_DESCRIPTOR*)(b+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
 for(;d->Name;d++) {
  if(!d->OriginalFirstThunk)continue;
  IMAGE_THUNK_DATA *names=(IMAGE_THUNK_DATA*)(b+d->OriginalFirstThunk),*slots=(IMAGE_THUNK_DATA*)(b+d->FirstThunk);
  for(;names->u1.AddressOfData;names++,slots++) {
   if(IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))continue;
   char *name=(char*)((IMAGE_IMPORT_BY_NAME*)(b+names->u1.AddressOfData))->Name;void *fn=NULL;
   if(!strcmp(name,"GetForegroundWindow"))fn=(void*)focus_window;
   if(!strcmp(name,"GetWindowThreadProcessId"))fn=(void*)focus_pid;
   if(fn){DWORD old=0,ignored=0;assert(VirtualProtect(&slots->u1.Function,4,PAGE_READWRITE,&old));slots->u1.Function=(DWORD)(uintptr_t)fn;assert(VirtualProtect(&slots->u1.Function,4,old,&ignored));}
  }
 }
}
static void monster(unsigned slot,BYTE species,int hp,float x,float y,float z) {
 uintptr_t actor=0xe040000+slot*0xef0;engine[actor]=1;engine[actor+1]=1;engine[actor+3]=species;
 uint16_t index=(uint16_t)slot;put(actor+0xc,&index,2);float p[3]={x,y,z};put(actor+0xac,p,12);
 uint16_t slot_key=(uint16_t)(0x1234+slot*7);put(0xedb768e - slot*2,&slot_key,2);
 uint16_t raw=(uint16_t)(hp^slot_key^0xffffu^0x85442351u^0x102d5ec0u);raw=(uint16_t)((raw<<5)|(raw>>11));put(actor+0x624,&raw,2);
}
int main(int argc,char **argv) {
 int bad_engine=argc>1&&!strcmp(argv[1],"--bad-engine");
 int bad_hash=argc>1&&!strcmp(argv[1],"--bad-hash");
 int bad_monster=argc>1&&!strcmp(argv[1],"--bad-monster");
 if(!bad_hash)prepare_engine();
 if(bad_engine)engine[frontier_blocks[0].rva]=0;
 if(bad_monster)engine[monster_guards[0].rva]=0;
 HMODULE input=LoadLibraryA("xinput1_4.dll");assert(input);SetPad pad=(SetPad)GetProcAddress(input,"SetTestPad");assert(pad);
 HMODULE module=LoadLibraryA("FrontierTargetL3Mod.dll");assert(module);
 Status state=(Status)GetProcAddress(module,"FrontierTargetAdapterStatus@4");assert(state);
 SnapshotRead sync=(SnapshotRead)GetProcAddress(module,"FrontierGetCameraSnapshot@8");assert(sync);
 for(int i=0;i<300&&state(NULL)==0;i++)Sleep(10);
 int result=(int32_t)state(NULL);printf("Adapter status: %d\n",result);fflush(stdout);
 if(bad_hash){assert(result==-21);assert(!GetModuleHandleA("FrontierOrbitRain08.dll"));puts("PASS: changed native DLL rejected before loading");return 0;}
 if(bad_engine){assert(result==-3);puts("PASS: native engine guards reject incompatible engine");return 0;}
 if(bad_monster){assert(result==-26);puts("PASS: monster-specific engine guards reject changed actor layout");return 0;}
 assert(result==2);geometry_stubs();
 HMODULE native=GetModuleHandleA("FrontierOrbitRain08.dll");assert(native);
 SnapshotRead original_sync=(SnapshotRead)GetProcAddress(native,"FrontierGetCameraSnapshot@8");assert(original_sync);
 fake_focus=GetCurrentProcessId();fixture_focus(module);fixture_focus(native);
 uint32_t id=1;HANDLE pipe=connect_controller(GetCurrentProcessId(),process_created());
 assert(request(pipe,CONTROL_HELLO,id++,NULL).error==CONTROL_OK);
 CameraSettings settings=camera_defaults();settings.gamepad_index=3;
 assert(request(pipe,CONTROL_SETTINGS,id++,&settings).error==CONTROL_OK);
 assert(request(pipe,CONTROL_ENABLE,id++,NULL).error==CONTROL_OK);render(40,pipe,&id);
 ControlReply reply=request(pipe,CONTROL_STATUS,id++,NULL);assert(reply.scene==CAMERA_ACTIVE&&reply.reference_radius==600&&fabs(reply.yaw)<.01);
 /* Original LB/native recentering cannot drive the new targeting adapter. */
 uint16_t stock=49152;put(0x1c4a168,&stock,2);pad(XINPUT_GAMEPAD_LEFT_SHOULDER,0,0,223,197);render(2,pipe,&id);
 reply=request(pipe,CONTROL_STATUS,id++,NULL);assert(fabs(reply.yaw)<.01);
 monster(0,114,20000,2000,0,1000);monster(1,5,100,1000,0,1100);monster(2,11,0,1000,0,1050);
 pad(XINPUT_GAMEPAD_LEFT_THUMB,0,0,223,197);render(35,pipe,&id);
 reply=request(pipe,CONTROL_STATUS,id++,NULL);printf("L3 target yaw=%f\n",reply.yaw);fflush(stdout);assert(fabs(reply.yaw-90)<.1);
 monster(0,114,20000,1000,0,2000);render(4,pipe,&id);reply=request(pipe,CONTROL_STATUS,id++,NULL);assert(fabs(reply.yaw-90)<.1);
 /* The unchanged camera receives every other button and both trigger bytes. */
 typedef DWORD(WINAPI *ReadPad)(DWORD,XINPUT_STATE*);
 ReadPad camera_reader=*(ReadPad*)((BYTE*)native+0x38380);XINPUT_STATE actual;
 WORD mixed=XINPUT_GAMEPAD_LEFT_THUMB|XINPUT_GAMEPAD_RIGHT_THUMB|XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_A;
 pad(mixed,1200,-2400,223,197);assert(camera_reader(3,&actual)==ERROR_SUCCESS);
 assert(actual.Gamepad.wButtons==(mixed&~XINPUT_GAMEPAD_LEFT_THUMB)&&actual.Gamepad.bLeftTrigger==223&&actual.Gamepad.bRightTrigger==197);
 pad(0,0,0,0,0);render(1,pipe,&id);pad(0x40,0,0,0,0);render(35,pipe,&id);
 reply=request(pipe,CONTROL_STATUS,id++,NULL);assert(fabs(reply.yaw)<.1);
 /* No valid large monster: no behind-player fallback. */
 monster(0,114,0,1000,0,2000);CameraCore *core=(CameraCore*)((BYTE*)native+0x38240);core->orbit.yaw=core->orbit.want_yaw=1.0;
 pad(0,0,0,0,0);render(1,pipe,&id);pad(0x40,0,0,0,0);render(15,pipe,&id);
 reply=request(pipe,CONTROL_STATUS,id++,NULL);assert(fabs(reply.yaw-1.0/CAMERA_RAD)<.1);
 /* Held L3 on focus return is not a fresh target press. */
 monster(0,114,20000,2000,0,1000);fake_focus=0;pad(0,0,0,0,0);render(1,pipe,&id);
 pad(0x40,0,0,0,0);render(1,pipe,&id);fake_focus=GetCurrentProcessId();render(10,pipe,&id);
 reply=request(pipe,CONTROL_STATUS,id++,NULL);assert(fabs(reply.yaw-1.0/CAMERA_RAD)<.1);
 BYTE snapshot[60],direct[60];assert(!sync(NULL,60)&&!sync(snapshot,59));
 assert(sync(snapshot,60)&&original_sync(direct,60)&&!memcmp(snapshot,direct,60));
 uint32_t fields[15];memcpy(fields,snapshot,60);assert(fields[0]==0x46534331&&fields[1]==1&&fields[2]==60&&fields[3]==GetCurrentProcessId()&&fields[6]==6);
 settings.distance_scale=1.5f;settings.height_offset=40;settings.shoulder_offset=70;
 assert(request(pipe,CONTROL_SETTINGS,id++,&settings).error==CONTROL_OK);render(3,pipe,&id);
 reply=request(pipe,CONTROL_STATUS,id++,NULL);assert(reply.requested_radius==900&&reply.settings.height_offset==40&&fabs(reply.yaw-1.0/CAMERA_RAD)<.1);
 assert(request(pipe,CONTROL_DISABLE,id++,NULL).error==CONTROL_OK);render(1,pipe,&id);
 stock=54321;put(0x1c4a168,&stock,2);render(2,pipe,&id);
 reply=request(pipe,CONTROL_STATUS,id++,NULL);assert(reply.scene==CAMERA_DISABLED&&*(uint16_t*)(engine+0x1c4a168)==54321);
 assert(sync(snapshot,60)&&original_sync(direct,60)&&!memcmp(snapshot,direct,60));CloseHandle(pipe);
 puts("PASS: exact new binary, L3 monster turn, small/dead filtering, hold/no-target/focus behavior, all other inputs, profiles, snapshot delegation and disable");return 0;
}
