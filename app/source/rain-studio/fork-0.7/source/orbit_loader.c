#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <wchar.h>
#include <string.h>
#include "engine_profiles.h"
#include "remote_engine_guards.h"
static HANDLE process;static DWORD pid;
static uintptr_t module(const wchar_t *name,wchar_t *path){
 HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
 if(snapshot==INVALID_HANDLE_VALUE)return 0;
 MODULEENTRY32W e;memset(&e,0,sizeof(e));e.dwSize=sizeof(e);uintptr_t result=0;
 if(Module32FirstW(snapshot,&e))do{if(!_wcsicmp(e.szModule,name)){result=(uintptr_t)e.modBaseAddr;if(path)wcscpy(path,e.szExePath);break;}}while(Module32NextW(snapshot,&e));
 CloseHandle(snapshot);return result;
}
static uintptr_t rain_engine(wchar_t *path){
 wchar_t exe[MAX_PATH];if(!module(L"client.exe",exe))return 0;
 HANDLE reader=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid);if(!reader)return 0;
 uintptr_t address=0x10000000;int valid=remote_engine_guards(reader,(const unsigned char*)address);CloseHandle(reader);if(!valid)return 0;
 if(path){wcscpy(path,exe);wchar_t *slash=wcsrchr(path,L'\\');if(!slash)return 0;wcscpy(slash+1,L"client.dll");}
 return address;
}
static int engine_hash(const wchar_t *path){
 HCRYPTPROV provider=0;HCRYPTHASH hash=0;BYTE b[65536],digest[32];DWORD got=0,n=32;int result=0;

 HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,0,OPEN_EXISTING,0,0);if(file==INVALID_HANDLE_VALUE)return 0;
 if(!CryptAcquireContextW(&provider,0,0,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)||!CryptCreateHash(provider,CALG_SHA_256,0,0,&hash))goto cleanup;
 while(1){if(!ReadFile(file,b,sizeof(b),&got,0))goto cleanup;if(!got)break;if(!CryptHashData(hash,b,got,0))goto cleanup;}
 if(CryptGetHashParam(hash,HP_HASHVAL,digest,&n,0)&&n==32){
  for(unsigned i=0;i<sizeof(engine_profiles)/sizeof(engine_profiles[0]);i++){
   if(!memcmp(digest,engine_profiles[i].sha256,32)){printf("Verified engine: %s\n",engine_profiles[i].name);result=1;break;}
  }
 }
 cleanup:if(hash)CryptDestroyHash(hash);if(provider)CryptReleaseContext(provider,0);CloseHandle(file);return result;
}
static DWORD remote_call(uintptr_t function,void *argument,int *ok){
 *ok=0;HANDLE t=CreateRemoteThread(process,0,0,(LPTHREAD_START_ROUTINE)function,argument,0,0);if(!t)return 0;
 if(WaitForSingleObject(t,10000)!=WAIT_OBJECT_0){CloseHandle(t);return 0;}
 DWORD value=0;if(GetExitCodeThread(t,&value))*ok=1;CloseHandle(t);return value;
}
static uintptr_t remote_system_function(const char *name){
 FARPROC p=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),name);HMODULE owner=0;
 if(!p||!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)p,&owner))return 0;
 wchar_t path[MAX_PATH];if(!GetModuleFileNameW(owner,path,MAX_PATH))return 0;wchar_t *name_start=wcsrchr(path,L'\\');name_start=name_start?name_start+1:path;
 uintptr_t remote_base=module(name_start,0);return remote_base?remote_base+((uintptr_t)p-(uintptr_t)owner):0;
}
static int run(int argc,char **argv){
 int disable=0;for(int i=1;i<argc;i++){if(!strcmp(argv[i],"--disable"))disable=1;else if(!strcmp(argv[i],"--pid")&&i+1<argc)pid=(DWORD)strtoul(argv[++i],0,10);else{puts("Usage: OrbitLoader.exe [--pid PID] [--disable]");return 1;}}
 if(!pid){
  HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);PROCESSENTRY32W e;memset(&e,0,sizeof(e));e.dwSize=sizeof(e);unsigned count=0;DWORD selected=0;
  if(snapshot==INVALID_HANDLE_VALUE)return 2;
  if(Process32FirstW(snapshot,&e))do{if(!_wcsicmp(e.szExeFile,L"client.exe")){pid=e.th32ProcessID;if(rain_engine(0)){selected=pid;count++;}}}while(Process32NextW(snapshot,&e));
  CloseHandle(snapshot);pid=selected;if(count!=1){puts("Keep one Rain game open in the city, or specify --pid.");return 3;}
 }
 printf("Game PID: %lu\n",pid);
 wchar_t engine_path[MAX_PATH];if(!rain_engine(engine_path)){puts("Supported HD engine not loaded.");return 4;}
 if(!engine_hash(engine_path)){puts("HD engine SHA256 mismatch. Nothing installed.");return 5;}
 const wchar_t *older[]={L"FrontierOrbit01.dll",L"FrontierOrbit02.dll",L"FrontierOrbit03.dll",L"FrontierOrbit04.dll",L"FrontierOrbit05.dll",L"FrontierOrbit06.dll"};
 for(unsigned i=0;i<sizeof(older)/sizeof(older[0]);i++)if(module(older[i],0)){
  puts("Older Orbit module is still loaded. Close and restart the game before using Rain 0.7.");return 17;
 }
 process=OpenProcess(PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_VM_READ,FALSE,pid);
 if(!process){printf("Cannot open game, error %lu. Match game privilege level.\n",GetLastError());return 6;}
 wchar_t dll_path[MAX_PATH];DWORD length=GetModuleFileNameW(0,dll_path,MAX_PATH);
 if(!length||length>=MAX_PATH-30){puts("Loader folder path is too long.");return 7;}
 wchar_t *slash=wcsrchr(dll_path,L'\\');if(!slash)return 7;wcscpy(slash+1,L"FrontierOrbitRain07.dll");
 HMODULE local=LoadLibraryExW(dll_path,0,DONT_RESOLVE_DLL_REFERENCES);if(!local){puts("FrontierOrbitRain07.dll missing or invalid.");return 8;}
 FARPROC state_fn=GetProcAddress(local,"OrbitStatus"),action_fn=GetProcAddress(local,disable?"OrbitDisable":"OrbitEnable");
 if(!state_fn)state_fn=GetProcAddress(local,"OrbitStatus@4");
 if(!action_fn)action_fn=GetProcAddress(local,disable?"OrbitDisable@4":"OrbitEnable@4");
 if(!state_fn||!action_fn){FreeLibrary(local);puts("DLL exports invalid.");return 9;}
 uintptr_t remote=module(L"FrontierOrbitRain07.dll",0);int ok=0;
 if(!remote){
  if(disable){FreeLibrary(local);puts("Mod is not loaded; game is stock.");return 0;}
  SIZE_T bytes=(wcslen(dll_path)+1)*sizeof(wchar_t),written=0;
  void *argument=VirtualAllocEx(process,0,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
  if(!argument){FreeLibrary(local);puts("Remote path allocation failed.");return 10;}
  if(!WriteProcessMemory(process,argument,dll_path,bytes,&written)||written!=bytes){VirtualFreeEx(process,argument,0,MEM_RELEASE);FreeLibrary(local);puts("Remote path write failed.");return 11;}
  uintptr_t load=remote_system_function("LoadLibraryW");
  if(!load){VirtualFreeEx(process,argument,0,MEM_RELEASE);FreeLibrary(local);puts("Cannot resolve target LoadLibraryW.");return 12;}
  remote=(uintptr_t)remote_call(load,argument,&ok);
  if(!ok){FreeLibrary(local);puts("Load thread failed or timed out; restart game before retrying.");return 13;}
  VirtualFreeEx(process,argument,0,MEM_RELEASE);
  if(!remote){FreeLibrary(local);puts("Windows refused to load module.");return 14;}
 }
 DWORD state=0;for(int i=0;i<40;i++){state=remote_call(remote+((uintptr_t)state_fn-(uintptr_t)local),0,&ok);if(!ok||state!=0)break;Sleep(100);}
 if(!ok||state!=2){printf("Mod initialization failed: %ld. Read orbit.log.\n",(LONG)state);FreeLibrary(local);return 15;}
 DWORD action=remote_call(remote+((uintptr_t)action_fn-(uintptr_t)local),0,&ok);FreeLibrary(local);
 if(!ok||action!=1){puts("Mod control call failed.");return 16;}
 puts(disable?"MOD DISABLED. Stock camera passes through. Restart game to unload module.":"MOD INSTALLED. Right stick: orbit. L3: reset behind hunter. F8: toggle.");
 puts("orbit.log reports applied frames. Installation alone does not prove camera rendering.");return 0;
}
int main(int argc,char **argv){int result=run(argc,argv);if(process)CloseHandle(process);return result;}
