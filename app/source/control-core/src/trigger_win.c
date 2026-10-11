#define DIRECTINPUT_VERSION 0x0800
#define COBJMACROS
#include "trigger_win.h"
#include "trigger_core.h"
#include <dinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <ctype.h>
typedef HRESULT (WINAPI *DeviceState)(void*,DWORD,void*);
typedef HRESULT (WINAPI *DeviceInfo)(void*,void*);
typedef HRESULT (WINAPI *CreateInput)(HINSTANCE,DWORD,REFIID,void**,IUnknown*);
static struct {
 void **slot;DeviceState original;int wide;
} trigger_hooks[2];
static volatile LONG trigger_hook_count;
static unsigned trigger_left,trigger_right;
static GUID trigger_device;
static FrontierPadRead trigger_pad;
static FrontierInputContext trigger_context;
static void *trigger_opaque;
static FrontierInputStats *trigger_stats;
static HMODULE trigger_module;
static int same_guid(const GUID *a,const GUID *b){return !memcmp(a,b,sizeof(*a));}
static int same_device(void *device,int wide){
 void **vtable=*(void***)device;DeviceInfo info=(DeviceInfo)vtable[15];
 if(wide){DIDEVICEINSTANCEW d={0};d.dwSize=sizeof(d);return SUCCEEDED(info(device,&d))&&same_guid(&d.guidInstance,&trigger_device);}
 DIDEVICEINSTANCEA d={0};d.dwSize=sizeof(d);return SUCCEEDED(info(device,&d))&&same_guid(&d.guidInstance,&trigger_device);
}
static HRESULT WINAPI trigger_get_state(void *device,DWORD bytes,void *buffer){
 void **slot=*(void***)device+9;unsigned i,count=(unsigned)InterlockedCompareExchange(&trigger_hook_count,0,0);
 for(i=0;i<count;i++)if(trigger_hooks[i].slot==slot)break;
 if(i==count)return DIERR_GENERIC;
 HRESULT hr=trigger_hooks[i].original(device,bytes,buffer);
 if(FAILED(hr)||!buffer||(bytes!=sizeof(DIJOYSTATE)&&bytes!=sizeof(DIJOYSTATE2))||
  !same_device(device,trigger_hooks[i].wide))return hr;
 InterlockedIncrement(&trigger_stats->calls);InterlockedExchange(&trigger_stats->bytes,(LONG)bytes);
 int index=-1;if(InterlockedCompareExchange(&trigger_stats->status,0,0)<=0||!trigger_context(trigger_opaque,&index))return hr;
 XINPUT_STATE state={0};DWORD result=ERROR_DEVICE_NOT_CONNECTED;
 if(index>=0&&index<4)result=trigger_pad((DWORD)index,&state);
 else if(index==-1)for(DWORD p=0;p<4;p++)if((result=trigger_pad(p,&state))==ERROR_SUCCESS)break;
 if(result!=ERROR_SUCCESS)return hr;
 BYTE *buttons=(BYTE*)buffer+offsetof(DIJOYSTATE,rgbButtons);
 if(trigger_apply(buttons,128,trigger_left,trigger_right,state.Gamepad.bLeftTrigger,state.Gamepad.bRightTrigger,XINPUT_GAMEPAD_TRIGGER_THRESHOLD)){
  InterlockedIncrement(&trigger_stats->applied);InterlockedExchange(&trigger_stats->status,2);
  InterlockedExchange(&trigger_stats->left,state.Gamepad.bLeftTrigger);
  InterlockedExchange(&trigger_stats->right,state.Gamepad.bRightTrigger);
 }
 return hr;
}
static int patch_device(void *device,int wide){
 void **slot=*(void***)device+9;
 for(unsigned i=0;i<(unsigned)InterlockedCompareExchange(&trigger_hook_count,0,0);i++)if(trigger_hooks[i].slot==slot)return 1;
 if(trigger_hook_count>=2)return 0;
 MEMORY_BASIC_INFORMATION table,code;
 if(!VirtualQuery(slot,&table,sizeof(table))||table.AllocationBase!=trigger_module||
  !VirtualQuery(*slot,&code,sizeof(code))||code.AllocationBase!=trigger_module||
  !(code.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return 0;
 unsigned i=trigger_hook_count;trigger_hooks[i].slot=slot;trigger_hooks[i].original=(DeviceState)*slot;trigger_hooks[i].wide=wide;
 /* Publish the original before a game thread can enter the wrapper. */
 InterlockedExchange(&trigger_hook_count,(LONG)i+1);MemoryBarrier();
 DWORD old,ignored;if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old)){InterlockedExchange(&trigger_hook_count,(LONG)i);return 0;}
 void *previous=InterlockedCompareExchangePointer((void*volatile*)slot,(void*)trigger_get_state,(void*)trigger_hooks[i].original);
 BOOL restored=VirtualProtect(slot,sizeof(void*),old,&ignored);
 if(previous!=(void*)trigger_hooks[i].original){InterlockedExchange(&trigger_hook_count,(LONG)i);return 0;}
 return restored!=0;
}
typedef struct {IDirectInput8A *input;GUID preferred,chosen;int prefer,found;} Enumerate;
static BOOL CALLBACK find_controller(const DIDEVICEINSTANCEA *d,void *opaque){
 Enumerate *e=opaque;
 if(e->prefer&&!same_guid(&d->guidInstance,&e->preferred))return DIENUM_CONTINUE;
 IDirectInputDevice8A *device=NULL;
 if(FAILED(e->input->lpVtbl->CreateDevice(e->input,&d->guidInstance,&device,NULL)))return DIENUM_CONTINUE;
 DIPROPGUIDANDPATH path={0};path.diph.dwSize=sizeof(path);path.diph.dwHeaderSize=sizeof(path.diph);path.diph.dwHow=DIPH_DEVICE;
 int xinput=SUCCEEDED(device->lpVtbl->GetProperty(device,DIPROP_GUIDANDPATH,&path.diph))&&wcsstr(path.wszPath,L"IG_")!=NULL;
 if(!xinput){char name[MAX_PATH];size_t n=strlen(d->tszProductName);if(n>=sizeof(name))n=sizeof(name)-1;
  for(size_t i=0;i<n;i++)name[i]=(char)tolower((unsigned char)d->tszProductName[i]);name[n]=0;xinput=strstr(name,"xbox")!=NULL;}
 device->lpVtbl->Release(device);
 if(!xinput)return DIENUM_CONTINUE;
 e->chosen=d->guidInstance;e->found=1;return DIENUM_STOP;
}
int frontier_trigger_install(FrontierPadRead read_pad,FrontierInputContext context,void *opaque,
 unsigned left,unsigned right,const GUID *preferred,FrontierInputStats *stats){
 if(!stats)return 0;memset(stats,0,sizeof(*stats));
 if(!read_pad||!context||left>=16||right>=16||left==right){stats->status=-2;return 0;}
 trigger_module=GetModuleHandleA("dinput8.dll");if(!trigger_module){stats->status=-1;return 0;}
 CreateInput create=(CreateInput)GetProcAddress(trigger_module,"DirectInput8Create");if(!create){stats->status=-1;return 0;}
 IDirectInput8A *a=NULL;IDirectInput8W *w=NULL;
 if(FAILED(create(GetModuleHandleA(NULL),DIRECTINPUT_VERSION,&IID_IDirectInput8A,(void**)&a,NULL))){stats->status=-3;return 0;}
 Enumerate e={0};e.input=a;if(preferred){e.preferred=*preferred;e.prefer=1;}
 a->lpVtbl->EnumDevices(a,DI8DEVCLASS_GAMECTRL,find_controller,&e,DIEDFL_ATTACHEDONLY);
 if(!e.found&&e.prefer){e.prefer=0;a->lpVtbl->EnumDevices(a,DI8DEVCLASS_GAMECTRL,find_controller,&e,DIEDFL_ATTACHEDONLY);}
 if(!e.found){a->lpVtbl->Release(a);stats->status=-3;return 0;}
 trigger_device=e.chosen;trigger_left=left;trigger_right=right;trigger_pad=read_pad;
 trigger_context=context;trigger_opaque=opaque;trigger_stats=stats;
 IDirectInputDevice8A *da=NULL;IDirectInputDevice8W *dw=NULL;int ok=0;
 if(SUCCEEDED(a->lpVtbl->CreateDevice(a,&e.chosen,&da,NULL))){ok=patch_device(da,0);da->lpVtbl->Release(da);}
 if(SUCCEEDED(create(GetModuleHandleA(NULL),DIRECTINPUT_VERSION,&IID_IDirectInput8W,(void**)&w,NULL))){
  if(SUCCEEDED(w->lpVtbl->CreateDevice(w,&e.chosen,&dw,NULL))){ok=patch_device(dw,1)||ok;dw->lpVtbl->Release(dw);}w->lpVtbl->Release(w);}
 a->lpVtbl->Release(a);if(!ok){stats->status=-4;return 0;}InterlockedExchange(&stats->status,1);return 1;
}
void frontier_trigger_restore(void){
 for(unsigned i=0;i<(unsigned)InterlockedCompareExchange(&trigger_hook_count,0,0);i++){
  DWORD old,ignored;void **slot=trigger_hooks[i].slot;
  if(VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old)){
   InterlockedCompareExchangePointer((void*volatile*)slot,(void*)trigger_hooks[i].original,(void*)trigger_get_state);
   VirtualProtect(slot,sizeof(void*),old,&ignored);
  }
 }
 if(trigger_stats)InterlockedExchange(&trigger_stats->status,0);
}
static int binding(const char *header,const char *name,unsigned *button){
 char key[40];snprintf(key,sizeof(key),"%s=\"",name);const char *p=strstr(header,key);if(!p)return 0;
 char *end;unsigned long mask=strtoul(p+strlen(key),&end,16);
 if(*end!='"'||!mask||mask>0xffff||(mask&(mask-1)))return 0;
 *button=0;while((mask>>*button)!=1)(*button)++;return 1;
}
int frontier_trigger_bindings(const wchar_t *exe,unsigned *left,unsigned *right,GUID *preferred){
 wchar_t path[MAX_PATH];if(wcslen(exe)>=MAX_PATH)return 0;wcscpy(path,exe);wchar_t *slash=wcsrchr(path,L'\\');if(!slash)return 0;
 wcscpy(slash+1,L"keyconfig.cfg");HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
 if(file==INVALID_HANDLE_VALUE)return 0;char data[8193];DWORD got=0;BOOL ok=ReadFile(file,data,sizeof(data)-1,&got,NULL);CloseHandle(file);if(!ok)return 0;data[got]=0;
 char *start=strstr(data,"<controller "),*end=start?strchr(start,'>'):NULL;if(!end)return 0;*end=0;
 if(!binding(start,"ASSIGN_L2",left)||!binding(start,"ASSIGN_R2",right)||*left==*right)return 0;
 memset(preferred,0,sizeof(*preferred));wcscpy(slash+1,L"mhf.ini");wchar_t priority[256];
 GetPrivateProfileStringW(L"GAMEPAD_PRIORITY",L"00",L"",priority,256,path);
 unsigned v[11];
 if(swscanf(priority,L"%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x",&v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7],&v[8],&v[9],&v[10])==11){
  preferred->Data1=v[0];preferred->Data2=(WORD)v[1];preferred->Data3=(WORD)v[2];for(unsigned i=0;i<8;i++)preferred->Data4[i]=(BYTE)v[i+3];
 }
 return 1;
}
