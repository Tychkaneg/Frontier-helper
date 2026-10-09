#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "../../control-core/build/fixture_guards.h"
typedef float *(WINAPI *LookAt)(float*,const float*,const float*,const float*);
typedef DWORD (WINAPI *Status)(void*);
static BYTE *engine;
static int map_changed;
static void put(unsigned rva,const void *data,SIZE_T n) {
 uintptr_t page=((uintptr_t)engine+rva)&~(uintptr_t)4095;
 SIZE_T size=(((uintptr_t)engine+rva+n+4095)&~(uintptr_t)4095)-page;
 assert(VirtualAlloc((void*)page,size,MEM_COMMIT,PAGE_EXECUTE_READWRITE));
 DWORD old;assert(VirtualProtect((void*)page,size,PAGE_EXECUTE_READWRITE,&old));
 memcpy(engine+rva,data,n);FlushInstructionCache(GetCurrentProcess(),engine+rva,n);
}
static void u32(unsigned rva,uint32_t v){put(rva,&v,4);}
static Status core_status(void) {
 HMODULE core=GetModuleHandleA("FrontierOrbitRain08.dll");if(!core)return NULL;
 Status fn=(Status)GetProcAddress(core,"OrbitStatus@4");return fn?fn:(Status)GetProcAddress(core,"OrbitStatus");
}
static void render(void) {
 Status state=core_status();if(state&&state(NULL)!=2)return;
 float target[3]={1000,100,1000},eye[3]={1000,100,map_changed?915:400},up[3]={0,1,0},matrix[16];
 put(0xe7fff40+0x80,eye,sizeof(eye));put(0xe7fff40+0x8c,target,sizeof(target));
 (*(LookAt*)(engine+0x15d242c))(matrix,(float*)(engine+0xe7fff40+0x80),(float*)(engine+0xe7fff40+0x8c),up);
}
static void report(void) {
 Status state=core_status();float *eye=(float*)(engine+0xe7fff40+0x80),*target=(float*)(engine+0xe7fff40+0x8c);
 FILE *f=fopen("fixture-state.json","wb");assert(f);
 fprintf(f,"{\"pid\":%lu,\"registered_engine\":false,\"core_loaded\":%s,\"core_status\":%ld,\"map_changed\":%d,\"eye\":[%.3f,%.3f,%.3f],\"target\":[%.3f,%.3f,%.3f]}\n",
  GetCurrentProcessId(),state?"true":"false",state?(LONG)state(NULL):0,map_changed,
  eye[0],eye[1],eye[2],target[0],target[1],target[2]);fclose(f);
}
static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM w,LPARAM l) {
 (void)l;
 if(message==WM_TIMER){render();return 0;}
 if(message==WM_APP+10){map_changed=1;u32(0xe77dc2c,(uint32_t)(uintptr_t)engine+0xe011000);return 0;}
 if(message==WM_APP+11){report();return 0;}
 if(message==WM_CLOSE){DestroyWindow(window);return 0;}
 if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
 return DefWindowProcW(window,message,w,l);
}
int main(void) {
 HMODULE matrix=LoadLibraryA("d3dx9_43.dll");assert(matrix);
 engine=VirtualAlloc((void*)0x10000000,0xf200000,MEM_RESERVE,PAGE_NOACCESS);assert(engine==(BYTE*)0x10000000);
 assert(!GetModuleHandleA("mhfo-hd.dll"));
 for(unsigned i=0;i<sizeof(fixture_guards)/sizeof(fixture_guards[0]);i++)put(fixture_guards[i].rva,fixture_guards[i].bytes,fixture_guards[i].length);
 /* Keep every guarded prefix intact. Trace finishes immediately after its
    guarded prologue; ground takes its existing early exit to a test epilogue. */
 const BYTE trace_tail[]={0x31,0xc0,0x5f,0x5e,0x5b,0xc9,0xc3};
 const BYTE ground_tail[]={0x0f,0x57,0xc0,0x5f,0xc9,0xc3};
 put(0x8d0f10+fixture_guards[5].length,trace_tail,sizeof(trace_tail));
 put(0x8202a0+80,ground_tail,sizeof(ground_tail));
 u32(0x19cf884,0xa5a5a5a5);u32(0xe7fff3c,(uint32_t)(uintptr_t)engine+0xe040000);
 BYTE world_flag=0;put(0xe040000+0x23ff,&world_flag,1);
 LookAt fn=(LookAt)GetProcAddress(matrix,"D3DXMatrixLookAtRH");assert(fn);put(0x15d242c,&fn,sizeof(fn));
 u32(0xedaad60,(uint32_t)(uintptr_t)engine+0xe7fff40);
 u32(0x1c4a0d4,(uint32_t)(uintptr_t)engine+0xe000000);
 u32(0xe77dc2c,(uint32_t)(uintptr_t)engine+0xe010000);
 BYTE channel=0;signed char special=-1;put(0x1c4a409,&channel,1);put(0x1c4a41d,&special,1);
 float actor[3]={1000,0,1000},low=0;put(0xe000000+0xac,actor,sizeof(actor));put(0x18e469c,&low,4);
 uint32_t grid[13]={0};grid[2]=grid[3]=grid[8]=grid[9]=128;grid[4]=grid[5]=grid[10]=grid[11]=16;
 grid[6]=(uint32_t)(uintptr_t)engine+0xe030000;grid[12]=(uint32_t)(uintptr_t)engine+0xe020000;
 put(0xe010000,grid,sizeof(grid));put(0xe011000,grid,sizeof(grid));
 uint32_t cells[256];for(int i=0;i<256;i++)cells[i]=1;put(0xe020000,cells,sizeof(cells));
 WNDCLASSW cls={0};cls.hInstance=GetModuleHandleW(NULL);cls.lpfnWndProc=proc;cls.lpszClassName=L"FakeRainWindow";
 assert(RegisterClassW(&cls));HWND window=CreateWindowW(cls.lpszClassName,L"Synthetic Rain engine — test fixture",
  WS_OVERLAPPEDWINDOW,25,25,360,200,NULL,NULL,cls.hInstance,NULL);assert(window);
 ShowWindow(window,SW_SHOW);SetTimer(window,1,33,NULL);render();
 MSG message;while(GetMessageW(&message,NULL,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
 return 0;
}
