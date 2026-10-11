#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <xinput.h>
#include <stddef.h>
#include <stdio.h>
#include "native_layout.h"
#include "monster_target.h"
#include "monster_guards.h"
#include "large_monsters.h"
/* Layout verified against the exact FPS-sync native binary, never a generic DLL. */
_Static_assert(sizeof(CameraCore)==232,"native core size");
_Static_assert(sizeof(ControlBridge)==328,"native bridge size");
_Static_assert(offsetof(CameraCore,settings)==120,"native settings offset");
_Static_assert(offsetof(CameraCore,frames)==200,"native frame offset");
_Static_assert(offsetof(CameraCore,scene)==224,"native scene offset");
_Static_assert(offsetof(ControlBridge,enabled)==168,"native enable offset");
_Static_assert(offsetof(ControlBridge,engine_status)==176,"native status offset");
_Static_assert(offsetof(ControlBridge,pid)==200,"native pid offset");
typedef float *(WINAPI *LookAt)(float*,const float*,const float*,const float*);
typedef DWORD (WINAPI *Status)(void*);
typedef BOOL (WINAPI *SnapshotRead)(void*,DWORD);
typedef DWORD (WINAPI *PadRead)(DWORD,XINPUT_STATE*);
static HMODULE self,native;
static HANDLE native_file=INVALID_HANDLE_VALUE;
static LookAt next;
static SnapshotRead native_snapshot;
static Status native_status;
static PadRead pad_read;
static CameraCore *core;
static ControlBridge *bridge;
static BYTE *engine=(BYTE*)0x10000000;
static volatile LONG adapter_status,busy,adoptions;
static uint16_t last_buttons;
static int was_allowed;
static FILE *journal;
static void log_state(const char *state,long detail) {
 if(journal){fprintf(journal,"%llu %s %ld\n",(unsigned long long)GetTickCount64(),state,detail);fflush(journal);}
}
static int read_memory(uintptr_t address,void *out,SIZE_T size) {
 SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),(void*)address,out,size,&got)&&got==size;
}
static int own_range(HMODULE owner,uintptr_t offset,SIZE_T size,int writable) {
 MEMORY_BASIC_INFORMATION m;BYTE *p=(BYTE*)owner+offset;
 if(!VirtualQuery(p,&m,sizeof(m))||m.AllocationBase!=owner||m.State!=MEM_COMMIT||
  (m.Protect&(PAGE_GUARD|PAGE_NOACCESS))||size>m.RegionSize-((uintptr_t)p-(uintptr_t)m.BaseAddress))return 0;
 return !writable||(m.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
}
static int exact_file(const wchar_t *path,const BYTE expected[32],long long expected_size,HANDLE *pinned) {
 HCRYPTPROV provider=0;HCRYPTHASH hash=0;BYTE bytes[8192],digest[32];DWORD got=0,length=32;int ok=0;
 HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
 if(file==INVALID_HANDLE_VALUE)return 0;
 LARGE_INTEGER size;if(!GetFileSizeEx(file,&size)||size.QuadPart!=expected_size)goto done;
 if(!CryptAcquireContextW(&provider,NULL,NULL,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)||
  !CryptCreateHash(provider,CALG_SHA_256,0,0,&hash))goto done;
 for(;;) {
  if(!ReadFile(file,bytes,sizeof(bytes),&got,NULL))goto done;
  if(!got)break;if(!CryptHashData(hash,bytes,got,0))goto done;
 }
 ok=CryptGetHashParam(hash,HP_HASHVAL,digest,&length,0)&&length==32&&!memcmp(digest,expected,32);
 done:if(hash)CryptDestroyHash(hash);if(provider)CryptReleaseContext(provider,0);
 if(ok&&pinned)*pinned=file;else CloseHandle(file);return ok;
}
static int exact_native(const wchar_t *path) {
 static const BYTE expected[32]={0x6c,0xb4,0x5a,0xc1,0x30,0xa9,0x80,0xe1,0x61,0x13,0xdc,0xa9,0x52,0xbe,0x13,0xfa,0x59,0x60,0x2d,0x5a,0x73,0xcd,0xb8,0x58,0x69,0x71,0x6a,0x4b,0x76,0xb9,0xdb,0x08};
 return exact_file(path,expected,243712,&native_file);
}
static int gamepad(XINPUT_STATE *state) {
 if(!pad_read)return 0;
 int index=core->settings.gamepad_index;
 if(index>=0&&index<=3)return pad_read((DWORD)index,state)==ERROR_SUCCESS;
 for(DWORD i=0;i<4;i++)if(pad_read(i,state)==ERROR_SUCCESS)return 1;return 0;
}
static DWORD WINAPI camera_pad(DWORD index,XINPUT_STATE *state) {
 DWORD result=pad_read(index,state);
 if(result==ERROR_SUCCESS&&state)state->Gamepad.wButtons=monster_camera_buttons(state->Gamepad.wButtons);
 return result;
}
static int monster_engine_guards(void) {
 BYTE actual[128];
 for(unsigned i=0;i<sizeof(monster_guards)/sizeof(monster_guards[0]);i++) {
  if(monster_guards[i].size>sizeof(actual)||
   !read_memory((uintptr_t)engine+monster_guards[i].rva,actual,monster_guards[i].size)||
   memcmp(actual,monster_guards[i].data,monster_guards[i].size))return 0;
 }
 return 1;
}
static int nearest_monster(const float player[3],double *yaw,unsigned *species,unsigned *chosen) {
 uint32_t table=0,key=0;double best=HUGE_VAL;int found=0;
 if(!read_memory((uintptr_t)engine+0xed7ad2c,&table,4)||!table||
    !read_memory((uintptr_t)engine+0x1a422c4,&key,4))return 0;
 for(unsigned i=0;i<40;i++) {
  BYTE header[16];float position[3];uint16_t encoded=0,slot_key=0;
  uintptr_t actor=(uintptr_t)table+i*0xef0;
  if(!read_memory(actor,header,sizeof(header))||!header[0]||!header[1]||!frontier_large[header[3]])continue;
  uint16_t slot=0;memcpy(&slot,header+0xc,2);if(slot>=40)continue;
  if(!read_memory(actor+0x624,&encoded,2)||
   !read_memory((uintptr_t)engine+0xedb768e - slot*2,&slot_key,2)||
   !read_memory(actor+0xac,position,sizeof(position)))continue;
  double distance=0,angle=0;
  if(monster_candidate(player,position,1,1,1,monster_health(encoded,slot_key,key),&distance,&angle)&&distance<best) {
   best=distance;*yaw=angle;*species=header[3];*chosen=i;found=1;
  }
 }
 return found;
}
static int target_allowed(void) {
 DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
 uint32_t actor=0,map=0;BYTE channel=1;signed char special=0;
 uint64_t now=GetTickCount64();
 return InterlockedCompareExchange(&adapter_status,0,0)==2&&core&&bridge&&core->scene==CAMERA_ACTIVE&&core->input_active&&
  core->reference.active&&!core->reference.pending&&now>=core->last_tick&&now-core->last_tick<150&&
  core->reference.epoch==InterlockedCompareExchange(&bridge->epoch,0,0)&&
  InterlockedCompareExchange(&bridge->enabled,0,0)&&foreground==GetCurrentProcessId()&&
  read_memory((uintptr_t)engine+0x1c4a409,&channel,1)&&channel==0&&
  read_memory((uintptr_t)engine+0x1c4a41d,&special,1)&&special==-1&&
  read_memory((uintptr_t)engine+0x1c4a0d4,&actor,4)&&actor==core->reference.actor&&
  read_memory((uintptr_t)engine+0xe77dc2c,&map,4)&&map==core->reference.map&&camera_settings_valid(&core->settings);
}
static float *WINAPI target_lookat(float *out,const float *eye,const float *target,const float *up) {
 uint32_t object=0;
 if((uintptr_t)eye!=(uintptr_t)engine+0xe7fffc0||
    (uintptr_t)target!=(uintptr_t)engine+0xe7fffcc||
    !read_memory((uintptr_t)engine+0xedaad60,&object,4)||object!=(uintptr_t)engine+0xe7fff40||
    InterlockedCompareExchange(&busy,1,0)!=0)return next(out,eye,target,up);
 XINPUT_STATE pad={0};int present=gamepad(&pad);uint16_t buttons=present?pad.Gamepad.wButtons:0;
 int allowed=target_allowed()&&present;
 if(monster_press(last_buttons,buttons,allowed,was_allowed)) {
  float player[3];double yaw=0;unsigned species=0,chosen=0;
  if(read_memory(core->reference.actor+0xac,player,sizeof(player))&&nearest_monster(player,&yaw,&species,&chosen)) {
   core->orbit.want_yaw=yaw;InterlockedIncrement(&adoptions);
   if(journal){fprintf(journal,"%llu target species=%u slot=%u yaw=%.3f\n",
    (unsigned long long)GetTickCount64(),species,chosen,yaw);fflush(journal);}
  }else log_state("no-monster",0);
 }
 /* L3 is masked only in the native camera reader; LT/RT use their unchanged reader. */
 float *result=next(out,eye,target,up);last_buttons=buttons;was_allowed=allowed;
 InterlockedExchange(&busy,0);return result;
}
__declspec(dllexport) BOOL WINAPI FrontierGetCameraSnapshot(void *out,DWORD bytes) {
 SnapshotRead fn=(SnapshotRead)InterlockedCompareExchangePointer((void*volatile*)&native_snapshot,NULL,NULL);
 return fn?fn(out,bytes):FALSE;
}
__declspec(dllexport) DWORD WINAPI OrbitControlVersion(void *unused) {(void)unused;return 2;}
__declspec(dllexport) DWORD WINAPI OrbitStatus(void *unused) {
 Status fn=(Status)InterlockedCompareExchangePointer((void*volatile*)&native_status,NULL,NULL);
 return fn?fn(unused):(DWORD)InterlockedCompareExchange(&adapter_status,0,0);
}
__declspec(dllexport) DWORD WINAPI FrontierTargetAdapterStatus(void *unused) {
 (void)unused;return (DWORD)InterlockedCompareExchange(&adapter_status,0,0);
}
static int existing_target(void) {
 HMODULE old=GetModuleHandleW(L"FrontierMonsterTarget-L3-Test1.dll");if(!old||old==self)return 0;
 static const BYTE expected[32]={0xe9,0x17,0xbb,0xe1,0x5a,0x47,0xf6,0x9a,0x87,0x7b,0xbc,0x27,0xcf,0xe0,0x33,0x7e,0xe6,0xd6,0xf7,0xdc,0x3e,0x2d,0xfa,0xa9,0x72,0x30,0xbe,0x69,0x97,0x8f,0x7e,0x13};
 wchar_t path[MAX_PATH];
 if(!GetModuleFileNameW(old,path,MAX_PATH)||!exact_file(path,expected,203776,NULL))return -1;
 Status status=(Status)GetProcAddress(old,"FrontierTargetAdapterStatus@4");
 LookAt hook=NULL;PadRead input=NULL;MEMORY_BASIC_INFORMATION view,pad;
 if(!status||status(NULL)!=2||!read_memory((uintptr_t)engine+0x15d242c,&hook,4)||
  !read_memory((uintptr_t)native+0x38380,&input,4)||
  !VirtualQuery((void*)hook,&view,sizeof(view))||view.AllocationBase!=old||
  !VirtualQuery((void*)input,&pad,sizeof(pad))||pad.AllocationBase!=old)return -1;
 return 1;
}
static DWORD WINAPI initialize(void *unused) {
 (void)unused;wchar_t folder[MAX_PATH],file[MAX_PATH];
 DWORD length=GetModuleFileNameW(self,folder,MAX_PATH);if(!length||length>=MAX_PATH-60){InterlockedExchange(&adapter_status,-20);return 0;}
 wchar_t *slash=wcsrchr(folder,L'\\');if(!slash){InterlockedExchange(&adapter_status,-20);return 0;}slash[1]=0;
 swprintf(file,MAX_PATH,L"%lsmonster-target-adapter-%lu.log",folder,GetCurrentProcessId());journal=_wfopen(file,L"w");
 /* Live addon can attach to the exact original already loaded by Helper, avoiding a quest restart. */
 HMODULE existing=GetModuleHandleW(L"FrontierOrbitRain08.dll");
 if(existing&&existing!=self) {
  if(!GetModuleFileNameW(existing,file,MAX_PATH)||!exact_native(file)) {
   log_state("loaded-native-hash-rejected",-21);InterlockedExchange(&adapter_status,-21);return 0;
  }
  native=existing;log_state("live-addon",1);
 }else {
  swprintf(file,MAX_PATH,L"%lsFrontierOrbitRain08.dll",folder);
  if(!exact_native(file)){log_state("native-hash-rejected",-21);InterlockedExchange(&adapter_status,-21);return 0;}
  native=LoadLibraryExW(file,NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
 }
 if(!native){log_state("native-load-failed",GetLastError());InterlockedExchange(&adapter_status,-22);return 0;}
 Status status=(Status)GetProcAddress(native,"OrbitStatus@4");
 SnapshotRead snapshot=(SnapshotRead)GetProcAddress(native,"FrontierGetCameraSnapshot@8");
 if(!status||!snapshot){log_state("native-exports-rejected",-23);InterlockedExchange(&adapter_status,-23);return 0;}
 InterlockedExchangePointer((void*volatile*)&native_status,(void*)status);
 InterlockedExchangePointer((void*volatile*)&native_snapshot,(void*)snapshot);
 int ready=0;for(unsigned i=0;i<200;i++) {ready=(int32_t)status(NULL);if(ready)break;Sleep(10);}
 if(ready!=2){log_state("native-engine-status",ready);InterlockedExchange(&adapter_status,ready?ready:-24);return 0;}
 if(!own_range(native,0x38240,sizeof(CameraCore),1)||!own_range(native,0x380f8,sizeof(ControlBridge),1)||
  !own_range(native,0x2190,32,0)||!own_range(native,0x38380,sizeof(PadRead),1)){log_state("native-layout-rejected",-25);InterlockedExchange(&adapter_status,-25);return 0;}
 core=(CameraCore*)((BYTE*)native+0x38240);bridge=(ControlBridge*)((BYTE*)native+0x380f8);
 if(!monster_engine_guards()) {InterlockedExchange(&bridge->engine_status,-26);InterlockedExchange(&adapter_status,-26);log_state("monster-guards-rejected",-26);return 0;}
 int existing_ready=existing_target();
 if(existing_ready==1) {InterlockedExchange(&adapter_status,2);log_state("reusing-installed-L3",2);return 0;}
 if(existing_ready<0) {InterlockedExchange(&bridge->engine_status,-28);InterlockedExchange(&adapter_status,-28);log_state("existing-target-rejected",-28);return 0;}
 InterlockedExchange(&bridge->engine_status,0);
 if(!camera_settings_valid(&core->settings)||bridge->pid!=GetCurrentProcessId())goto failed;
 if(!monster_engine_guards())goto failed;
 PadRead *pad_slot=(PadRead*)((BYTE*)native+0x38380);pad_read=*pad_slot;
 HMODULE xi=GetModuleHandleA("xinput1_4.dll");
 if(!xi||pad_read!=(PadRead)GetProcAddress(xi,"XInputGetState")) {
  xi=GetModuleHandleA("xinput9_1_0.dll");
  if(!xi||pad_read!=(PadRead)GetProcAddress(xi,"XInputGetState"))goto failed;
 }
 LookAt *slot=(LookAt*)(engine+0x15d242c),current=NULL;next=(LookAt)((BYTE*)native+0x2190);
 if(!read_memory((uintptr_t)slot,&current,sizeof(current))||current!=next)goto failed;
 DWORD old=0,ignored=0;if(!VirtualProtect(slot,sizeof(*slot),PAGE_READWRITE,&old))goto failed;
 void *previous=InterlockedCompareExchangePointer((void*volatile*)slot,(void*)target_lookat,(void*)next);
 BOOL restored=VirtualProtect(slot,sizeof(*slot),old,&ignored);
 if(previous!=(void*)next||!restored)goto failed;
 if(InterlockedCompareExchangePointer((void*volatile*)pad_slot,(void*)camera_pad,(void*)pad_read)!=(void*)pad_read) {
  DWORD restore_old=0,restore_ignored=0;
  if(VirtualProtect(slot,sizeof(*slot),PAGE_READWRITE,&restore_old)) {
   InterlockedCompareExchangePointer((void*volatile*)slot,(void*)next,(void*)target_lookat);
   VirtualProtect(slot,sizeof(*slot),restore_old,&restore_ignored);
  }
  goto failed;
 }
 InterlockedExchange(&adapter_status,2);InterlockedExchange(&bridge->engine_status,2);log_state("ready-L3-only",2);return 0;
 failed:InterlockedExchange(&bridge->engine_status,-26);InterlockedExchange(&adapter_status,-26);log_state("adapter-install-rejected",-26);return 0;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,void *reserved) {
 (void)reserved;if(reason==DLL_PROCESS_ATTACH) {
  self=instance;DisableThreadLibraryCalls(instance);HANDLE thread=CreateThread(NULL,0,initialize,NULL,0,NULL);
  if(thread)CloseHandle(thread);else InterlockedExchange(&adapter_status,-27);
 }return TRUE;
}
