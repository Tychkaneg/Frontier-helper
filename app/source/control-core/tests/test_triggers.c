#define DIRECTINPUT_VERSION 0x0800
#define COBJMACROS
#include <windows.h>
#include <dinput.h>
#include <xinput.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/trigger_win.h"
#include "../src/trigger_core.h"
static IDirectInput8A *input;
static GUID selected;
static IDirectInputDevice8A *device;
static BYTE lt,rt;
static int allowed=1,connected=1;
static DWORD WINAPI fake_pad(DWORD index,XINPUT_STATE *s){
 memset(s,0,sizeof(*s));if(index!=0||!connected)return ERROR_DEVICE_NOT_CONNECTED;
 s->Gamepad.bLeftTrigger=lt;s->Gamepad.bRightTrigger=rt;return ERROR_SUCCESS;
}
static int context(void *unused,int *index){(void)unused;*index=0;return allowed;}
static BOOL CALLBACK enumerate(const DIDEVICEINSTANCEA *d,void *unused){
 (void)unused;if(!strstr(d->tszProductName,"Xbox")&&!strstr(d->tszProductName,"XBOX"))return DIENUM_CONTINUE;
 selected=d->guidInstance;
 HRESULT hr=input->lpVtbl->CreateDevice(input,&selected,&device,NULL);return SUCCEEDED(hr)?DIENUM_STOP:DIENUM_CONTINUE;
}
static void verify(void *p,int wide,DWORD size,int left,int right){
 DIJOYSTATE2 s={0};HRESULT hr;
 if(wide){IDirectInputDevice8W *d=p;d->lpVtbl->Poll(d);hr=d->lpVtbl->GetDeviceState(d,size,&s);}
 else {IDirectInputDevice8A *d=p;d->lpVtbl->Poll(d);hr=d->lpVtbl->GetDeviceState(d,size,&s);}
 assert(SUCCEEDED(hr));assert(!!s.rgbButtons[10]==left&&!!s.rgbButtons[11]==right);
}
int main(void){
 BYTE buttons[128],before[128];memset(buttons,0x5a,sizeof(buttons));memcpy(before,buttons,sizeof(buttons));
 assert(trigger_apply(buttons,128,10,11,255,255,30));assert(buttons[10]==0x80&&buttons[11]==0x80);
 for(unsigned i=0;i<128;i++)if(i!=10&&i!=11)assert(buttons[i]==before[i]);
 memcpy(buttons,before,128);assert(!trigger_apply(buttons,128,10,10,255,255,30));assert(!memcmp(before,buttons,128));
 unsigned left,right;GUID preferred;
 wchar_t binding_exe[MAX_PATH];GetModuleFileNameW(NULL,binding_exe,MAX_PATH);wchar_t *slash=wcsrchr(binding_exe,L'\\');assert(slash);
 wcscpy(slash+1,L"test-input-bindings");assert(CreateDirectoryW(binding_exe,NULL)||GetLastError()==ERROR_ALREADY_EXISTS);
 wcscat(binding_exe,L"\\keyconfig.cfg");
 FILE *cfg=_wfopen(binding_exe,L"wb");assert(cfg);fputs("<controller ASSIGN_L2=\"0x00000400\" ASSIGN_R2=\"0x00000800\"/>",cfg);fclose(cfg);
 slash=wcsrchr(binding_exe,L'\\');wcscpy(slash+1,L"client.exe");
 assert(frontier_trigger_bindings(binding_exe,&left,&right,&preferred)&&left==10&&right==11);
 HMODULE dll=LoadLibraryA("dinput8.dll");assert(dll);
 typedef HRESULT (WINAPI *Create)(HINSTANCE,DWORD,REFIID,void**,IUnknown*);
 Create create=(Create)GetProcAddress(dll,"DirectInput8Create");assert(create);
 assert(SUCCEEDED(create(GetModuleHandleA(NULL),DIRECTINPUT_VERSION,&IID_IDirectInput8A,(void**)&input,NULL)));
 input->lpVtbl->EnumDevices(input,DI8DEVCLASS_GAMECTRL,enumerate,NULL,DIEDFL_ATTACHEDONLY);assert(device);
 IDirectInput8W *input_w=NULL;IDirectInputDevice8W *device_w=NULL;
 assert(SUCCEEDED(create(GetModuleHandleA(NULL),DIRECTINPUT_VERSION,&IID_IDirectInput8W,(void**)&input_w,NULL)));
 assert(SUCCEEDED(input_w->lpVtbl->CreateDevice(input_w,&selected,&device_w,NULL)));
 HWND window=CreateWindowExA(0,"STATIC","Input fixture",WS_OVERLAPPEDWINDOW,0,0,100,100,NULL,NULL,GetModuleHandleA(NULL),NULL);assert(window);
 assert(SUCCEEDED(device->lpVtbl->SetCooperativeLevel(device,window,DISCL_BACKGROUND|DISCL_NONEXCLUSIVE)));
 assert(SUCCEEDED(device_w->lpVtbl->SetCooperativeLevel(device_w,window,DISCL_BACKGROUND|DISCL_NONEXCLUSIVE)));
 assert(SUCCEEDED(device->lpVtbl->SetDataFormat(device,&c_dfDIJoystick2)));
 assert(SUCCEEDED(device_w->lpVtbl->SetDataFormat(device_w,&c_dfDIJoystick2)));
 assert(SUCCEEDED(device->lpVtbl->Acquire(device)));assert(SUCCEEDED(device_w->lpVtbl->Acquire(device_w)));
 void *original_a=(void*)device->lpVtbl->GetDeviceState,*original_w=(void*)device_w->lpVtbl->GetDeviceState;
 FrontierInputStats stats={0};assert(frontier_trigger_install(fake_pad,context,NULL,left,right,&preferred,&stats));assert(stats.status==1);
 assert((void*)device->lpVtbl->GetDeviceState!=original_a&&(void*)device_w->lpVtbl->GetDeviceState!=original_w);
 Sleep(100);
 lt=0;rt=0;verify(device,0,sizeof(DIJOYSTATE2),0,0);
 lt=255;rt=0;verify(device,0,sizeof(DIJOYSTATE2),1,0);
 lt=0;rt=255;verify(device,0,sizeof(DIJOYSTATE2),0,1);
 lt=255;rt=255;verify(device,0,sizeof(DIJOYSTATE2),1,1);verify(device_w,1,sizeof(DIJOYSTATE2),1,1);
 lt=30;rt=31;verify(device,0,sizeof(DIJOYSTATE2),0,1);
 lt=0;rt=0;verify(device,0,sizeof(DIJOYSTATE2),0,0);
 lt=255;rt=255;allowed=0;verify(device,0,sizeof(DIJOYSTATE2),0,0);allowed=1;
 connected=0;verify(device,0,sizeof(DIJOYSTATE2),0,0);connected=1;
 BYTE bad[123];memset(bad,0x5a,sizeof(bad));LONG applied=stats.applied;
 assert(FAILED(device->lpVtbl->GetDeviceState(device,sizeof(bad),bad)));assert(stats.applied==applied);
 assert(SUCCEEDED(device->lpVtbl->Unacquire(device)));assert(SUCCEEDED(device->lpVtbl->SetDataFormat(device,&c_dfDIJoystick)));
 assert(SUCCEEDED(device->lpVtbl->Acquire(device)));verify(device,0,sizeof(DIJOYSTATE),1,1);
 assert(stats.status==2&&stats.applied>0&&stats.calls>=stats.applied&&stats.bytes==sizeof(DIJOYSTATE));
 frontier_trigger_restore();assert((void*)device->lpVtbl->GetDeviceState==original_a&&(void*)device_w->lpVtbl->GetDeviceState==original_w);
 device->lpVtbl->Unacquire(device);device_w->lpVtbl->Unacquire(device_w);device->lpVtbl->Release(device);device_w->lpVtbl->Release(device_w);
 input->lpVtbl->Release(input);input_w->lpVtbl->Release(input_w);DestroyWindow(window);
 puts("PASS: real pre-existing DirectInput A/W devices; independent LT/RT and both together; threshold/release; context/disconnect gate; failed-read passthrough; DIJOYSTATE/DIJOYSTATE2; other buttons preserved; restore. No game files written.");return 0;
}
