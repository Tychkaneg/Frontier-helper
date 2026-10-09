#include "studio_backend.h"
#include <tlhelp32.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#include <shlobj.h>
#include "../fork-0.7/source/engine_profiles.h"
#include "../fork-0.7/source/remote_engine_guards.h"

typedef struct {
 StudioBackend *ui;
 HANDLE process,pipe;
 DWORD pid,id;
 uint64_t created,sent_serial;
} Connection;
static void log_path(StudioBackend *b) {
 wchar_t folder[MAX_PATH];
 if(!GetModuleFileNameW(NULL,folder,MAX_PATH))return;
 wchar_t *slash=wcsrchr(folder,L'\\');if(!slash)return;slash[1]=0;
 if(wcslen(folder)>MAX_PATH-85)return;
 wcscat(folder,L"logs");if(!CreateDirectoryW(folder,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS)return;
 SYSTEMTIME now;GetSystemTime(&now);
 swprintf(b->log_path,MAX_PATH,L"%ls\\attach-%04u%02u%02u-%02u%02u%02u-%lu.txt",folder,
  now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,GetCurrentProcessId());
}
static void persist_log(StudioBackend *b,const wchar_t *message) {
 if(!*b->log_path)return;SYSTEMTIME now;GetSystemTime(&now);wchar_t line[640];char utf8[2048];
 swprintf(line,640,L"%04u-%02u-%02u %02u:%02u:%02u.%03u UTC  %ls\r\n",now.wYear,now.wMonth,
  now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds,message);
 int bytes=WideCharToMultiByte(CP_UTF8,0,line,-1,utf8,sizeof(utf8),NULL,NULL);if(bytes<1)return;
 AcquireSRWLockExclusive(&b->log_lock);
 HANDLE file=CreateFileW(b->log_path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
 if(file!=INVALID_HANDLE_VALUE){DWORD written;WriteFile(file,utf8,(DWORD)bytes-1,&written,NULL);FlushFileBuffers(file);CloseHandle(file);}
 ReleaseSRWLockExclusive(&b->log_lock);
}
void studio_log(StudioBackend *b,int level,const wchar_t *format,...) {
 StudioLog *message=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*message));if(!message)return;
 message->level=level;va_list args;va_start(args,format);
 vswprintf(message->text,sizeof(message->text)/sizeof(wchar_t),format,args);va_end(args);
 persist_log(b,message->text);
 if(!PostMessageW(b->window,STUDIO_LOG,0,(LPARAM)message))HeapFree(GetProcessHeap(),0,message);
}
static uintptr_t module(DWORD pid,const wchar_t *name,wchar_t *path) {
 HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
 if(snapshot==INVALID_HANDLE_VALUE)return 0;
 MODULEENTRY32W e={0};e.dwSize=sizeof(e);uintptr_t found=0;
 if(Module32FirstW(snapshot,&e)){do {
  if(!_wcsicmp(e.szModule,name)) {
   found=(uintptr_t)e.modBaseAddr;if(path)wcscpy(path,e.szExePath);break;
  }
 }while(Module32NextW(snapshot,&e));}CloseHandle(snapshot);return found;
}
static int hash_file(const wchar_t *path,BYTE digest[32]) {
 HCRYPTPROV provider=0;HCRYPTHASH hash=0;int result=0;
 HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
  NULL,OPEN_EXISTING,0,NULL);if(file==INVALID_HANDLE_VALUE)return 0;
 if(!CryptAcquireContextW(&provider,NULL,NULL,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)||
  !CryptCreateHash(provider,CALG_SHA_256,0,0,&hash))goto cleanup;
 BYTE buffer[65536];DWORD got=0,size=32;
 for(;;) {
  if(!ReadFile(file,buffer,sizeof(buffer),&got,NULL))goto cleanup;
  if(!got)break;if(!CryptHashData(hash,buffer,got,0))goto cleanup;
 }
 result=CryptGetHashParam(hash,HP_HASHVAL,digest,&size,0)&&size==32;
cleanup:
 if(hash)CryptDestroyHash(hash);if(provider)CryptReleaseContext(provider,0);CloseHandle(file);return result;
}
static int identify(DWORD pid,wchar_t *exe,uint64_t *created) {
 HANDLE process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid);if(!process)return 0;
 DWORD length=MAX_PATH;BOOL self_wow=FALSE,game_wow=FALSE;
 int valid=QueryFullProcessImageNameW(process,0,exe,&length)&&
  IsWow64Process(GetCurrentProcess(),&self_wow)&&IsWow64Process(process,&game_wow)&&
  (!self_wow||game_wow)&&remote_engine_guards(process,(BYTE*)0x10000000);
 FILETIME c,e,k,u;
 if(valid&&GetProcessTimes(process,&c,&e,&k,&u))*created=((uint64_t)c.dwHighDateTime<<32)|c.dwLowDateTime;
 else valid=0;
 wchar_t *name=wcsrchr(exe,L'\\');name=name?name+1:exe;
 if(valid&&_wcsicmp(name,L"client.exe"))valid=0;CloseHandle(process);return valid;
}
static DWORD find_game(StudioBackend *ui,DWORD requested,const wchar_t *selected,wchar_t *exe,uint64_t *created) {
 if(requested) {
  if(identify(requested,exe,created))return requested;
  studio_log(ui,2,L"[error] PID %lu is not the supported Rain HD client.",requested);return 0;
 }
 HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
 if(snapshot==INVALID_HANDLE_VALUE)return 0;
 PROCESSENTRY32W e={0};e.dwSize=sizeof(e);DWORD pid=0;unsigned count=0;
 if(Process32FirstW(snapshot,&e)){do {
  if(!_wcsicmp(e.szExeFile,L"client.exe")) {
   wchar_t path[MAX_PATH]={0};uint64_t time=0;
   if(identify(e.th32ProcessID,path,&time)&&(!*selected||!_wcsicmp(selected,path))) {
    pid=e.th32ProcessID;wcscpy(exe,path);*created=time;count++;
   }
  }
 }while(Process32NextW(snapshot,&e));}CloseHandle(snapshot);
 if(count!=1) {
  studio_log(ui,2,count?L"[error] Multiple Rain clients found. Select the client executable in Files or close the extra client.":
   L"[waiting] Open Rain HD, enter the city, then click Start.");return 0;
 }
 return pid;
}
static uintptr_t remote_system_function(DWORD pid,const char *name) {
 FARPROC p=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),name);HMODULE owner=0;
 if(!p||!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|
  GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)p,&owner))return 0;
 wchar_t path[MAX_PATH];if(!GetModuleFileNameW(owner,path,MAX_PATH))return 0;
 wchar_t *n=wcsrchr(path,L'\\');n=n?n+1:path;
 uintptr_t remote=module(pid,n,NULL);return remote?remote+((uintptr_t)p-(uintptr_t)owner):0;
}
static int inject(Connection *c,const wchar_t *path) {
 const wchar_t *old[]={L"FrontierOrbitRain07.dll",L"FrontierOrbitControl.dll",L"FrontierOrbit07.dll",
  L"FrontierOrbit06.dll",L"FrontierOrbit05.dll",L"FrontierOrbit04.dll",L"FrontierOrbit03.dll",
  L"FrontierOrbit02.dll",L"FrontierOrbit01.dll"};
 for(unsigned i=0;i<sizeof(old)/sizeof(old[0]);i++)if(module(c->pid,old[i],NULL)) {
  studio_log(c->ui,2,L"[error] Earlier Orbit is loaded. Restart the game before using Studio.");return 0;
 }
 wchar_t loaded_path[MAX_PATH];
 if(module(c->pid,L"FrontierOrbitRain08.dll",loaded_path)) {
  BYTE local[32],remote[32];
  if(!hash_file(path,local)||!hash_file(loaded_path,remote)||memcmp(local,remote,32)) {
   studio_log(c->ui,2,L"[error] A different Studio core is loaded. Restart the game.");return 0;
  }
  studio_log(c->ui,0,L"[core] Reusing the existing Rain 0.8 module.");return 1;
 }
 if(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES) {
  studio_log(c->ui,2,L"[error] FrontierOrbitRain08.dll is missing next to Studio.");return 0;
 }
 uintptr_t load=remote_system_function(c->pid,"LoadLibraryW");
 if(!load){studio_log(c->ui,2,L"[error] Cannot resolve the target's LoadLibraryW.");return 0;}
 studio_log(c->ui,0,L"[attach 1/5] Opening a temporary loader handle for PID %lu.",c->pid);
 c->process=OpenProcess(PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|PROCESS_VM_OPERATION|
  PROCESS_VM_WRITE|PROCESS_VM_READ,FALSE,c->pid);
 if(!c->process){studio_log(c->ui,2,L"[error] Cannot open the loader handle (Windows %lu). Match game privileges.",GetLastError());return 0;}
 SIZE_T bytes=(wcslen(path)+1)*sizeof(wchar_t),written=0;
 void *argument=VirtualAllocEx(c->process,NULL,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
 if(!argument||!WriteProcessMemory(c->process,argument,path,bytes,&written)||written!=bytes) {
  if(argument)VirtualFreeEx(c->process,argument,0,MEM_RELEASE);
  studio_log(c->ui,2,L"[error] Cannot prepare the core path (Windows %lu).",GetLastError());return 0;
 }
 studio_log(c->ui,0,L"[attach 2/5] Calling the same LoadLibraryW bootstrap as Rain 0.7.");
 HANDLE thread=CreateRemoteThread(c->process,NULL,0,(LPTHREAD_START_ROUTINE)load,argument,0,NULL);
 if(!thread){DWORD error=GetLastError();VirtualFreeEx(c->process,argument,0,MEM_RELEASE);
  studio_log(c->ui,2,L"[error] Loader thread creation failed (Windows %lu).",error);return 0;}
 DWORD wait=WaitForSingleObject(thread,10000),result=0;
 int ok=wait==WAIT_OBJECT_0&&GetExitCodeThread(thread,&result)&&result!=0;CloseHandle(thread);
 /* A timed-out loader may still read its argument: preserve it and require a restart. */
 if(wait==WAIT_OBJECT_0)VirtualFreeEx(c->process,argument,0,MEM_RELEASE);
 if(!ok)studio_log(c->ui,2,L"[error] Loading failed/timed out (wait=%lu result=0x%08lx). Restart Rain before retrying.",wait,result);
 else studio_log(c->ui,0,L"[core] Rain adapter loaded at 0x%08lx; camera starts disabled.",result);return ok;
}
static int transfer(HANDLE pipe,void *buffer,DWORD size,int writing,DWORD *count) {
 OVERLAPPED ov={0};ov.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);if(!ov.hEvent)return 0;
 BOOL ok=writing?WriteFile(pipe,buffer,size,count,&ov):ReadFile(pipe,buffer,size,count,&ov);
 if(!ok&&GetLastError()==ERROR_IO_PENDING) {
  if(WaitForSingleObject(ov.hEvent,500)!=WAIT_OBJECT_0) {
   CancelIoEx(pipe,&ov);GetOverlappedResult(pipe,&ov,count,TRUE);CloseHandle(ov.hEvent);return 0;
  }
  ok=GetOverlappedResult(pipe,&ov,count,FALSE);
 }
 CloseHandle(ov.hEvent);return ok!=0;
}
static int send(Connection *c,unsigned command,const CameraSettings *settings,ControlReply *reply) {
 struct {ControlHeader h;CameraSettings s;} request;
 request.h=(ControlHeader){CONTROL_MAGIC,CONTROL_VERSION,(uint16_t)command,++c->id,
  settings?sizeof(*settings):0,0};if(settings)request.s=*settings;
 DWORD count=0,bytes=sizeof(ControlHeader)+request.h.payload_size;
 if(!transfer(c->pipe,&request,bytes,1,&count)||count!=bytes)return 0;
 struct {ControlHeader h;ControlReply r;} response;
 if(!transfer(c->pipe,&response,sizeof(response),0,&count)||count!=sizeof(response)||
  response.h.magic!=CONTROL_MAGIC||response.h.version!=CONTROL_VERSION||
  response.h.command!=(command|CONTROL_REPLY_BIT)||response.h.request_id!=c->id||
  response.h.payload_size!=sizeof(ControlReply)||response.h.reserved||
  response.r.game_pid!=c->pid||response.r.process_created!=c->created)return 0;
 *reply=response.r;return 1;
}
static void publish(Connection *c,const ControlReply *reply,int connected) {
 AcquireSRWLockExclusive(&c->ui->lock);
 c->ui->connected=connected;if(reply)c->ui->status=*reply;
 else {c->ui->status.enabled=0;c->ui->status.peer_pid=0;}
 ReleaseSRWLockExclusive(&c->ui->lock);PostMessageW(c->ui->window,STUDIO_UPDATE,0,0);
}
static void disconnect(Connection *c) {
 if(c->pipe&&c->pipe!=INVALID_HANDLE_VALUE) {
  ControlReply reply;send(c,CONTROL_DISABLE,NULL,&reply);CloseHandle(c->pipe);
 }
 if(c->process){CloseHandle(c->process);studio_log(c->ui,0,L"[release] Temporary loader handle closed after a failed attach.");}
 c->process=NULL;c->pipe=NULL;c->pid=0;
 c->id=0;c->sent_serial=0;publish(c,NULL,0);
}
static int connect_game(Connection *c,DWORD requested,const wchar_t *selected,const wchar_t *core) {
 wchar_t exe[MAX_PATH]={0};studio_log(c->ui,0,L"[scan] Looking for Rain's mapped HD engine...");
 c->pid=find_game(c->ui,requested,selected,exe,&c->created);if(!c->pid)return 0;
 wchar_t identity[MAX_PATH];wcscpy(identity,exe);wchar_t *slash=wcsrchr(identity,L'\\');
 if(!slash)return 0;wcscpy(slash+1,L"client.dll");BYTE digest[32];
 studio_log(c->ui,0,L"[verify] Checking client.dll SHA256 and 13 engine guards...");
 if(!hash_file(identity,digest)||memcmp(digest,engine_profiles[0].sha256,32)) {
  studio_log(c->ui,2,L"[error] client.dll differs from the supported Rain 20260929141936 build.");return 0;
 }
 studio_log(c->ui,1,L"[verified] Rain HD / PID %lu / 13 of 13 code guards.",c->pid);
 if(!inject(c,core))return 0;
 /* The pipe is the only control channel. Do not keep VM_WRITE/CREATE_THREAD
    rights to a protected game for the lifetime of the external GUI. */
 if(c->process){
  if(!CloseHandle(c->process)){studio_log(c->ui,2,L"[error] Closing the loader handle failed (Windows %lu).",GetLastError());return 0;}
  c->process=NULL;
 }
 studio_log(c->ui,0,L"[attach 3/5] Loader handle closed; waiting for the local control channel.");
 char endpoint[120];snprintf(endpoint,sizeof(endpoint),"\\\\.\\pipe\\FrontierOrbit.%lu.%016llx",
  (unsigned long)c->pid,(unsigned long long)c->created);
 for(int i=0;i<100;i++) {
  c->pipe=CreateFileA(endpoint,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,NULL);
  if(c->pipe!=INVALID_HANDLE_VALUE)break;Sleep(20);
 }
 if(c->pipe==INVALID_HANDLE_VALUE){DWORD error=GetLastError();c->pipe=NULL;
  studio_log(c->ui,2,L"[error] Core control channel is unavailable (Windows %lu). Rain may have exited before the handshake.",error);return 0;}
 DWORD owner=0,mode=PIPE_READMODE_MESSAGE;
 if(!GetNamedPipeServerProcessId(c->pipe,&owner)||owner!=c->pid||
  !SetNamedPipeHandleState(c->pipe,&mode,NULL,NULL)){
  studio_log(c->ui,2,L"[error] Control channel identity/mode check failed (Windows %lu).",GetLastError());return 0;}
 studio_log(c->ui,0,L"[attach 4/5] Channel owner verified; sending Hello.");
 ControlReply reply;
 if(!send(c,CONTROL_HELLO,NULL,&reply)){studio_log(c->ui,2,L"[error] Hello transport failed; game/core stopped responding.");return 0;}
 if(reply.error!=CONTROL_OK||reply.peer_pid!=GetCurrentProcessId()){
  studio_log(c->ui,2,L"[error] Hello was rejected (error=%lu peer=%lu).",reply.error,reply.peer_pid);return 0;}
 for(int i=0;i<40&&reply.engine_status==0;i++) {
  Sleep(50);if(!send(c,CONTROL_STATUS,NULL,&reply))return 0;
 }
 if(reply.engine_status!=2) {
  studio_log(c->ui,2,L"[error] Core initialization rejected the engine (%ld).",reply.engine_status);return 0;
 }
 studio_log(c->ui,0,L"[attach 5/5] Engine status 2; ready for Settings and Enable.");
 publish(c,&reply,1);studio_log(c->ui,1,L"[channel] Connected / protocol v1 / local process verified.");return 1;
}
static DWORD WINAPI worker(void *opaque) {
 StudioBackend *ui=opaque;Connection c={0};c.ui=ui;
 ULONGLONG last_settings=0,last_status=0;uint32_t previous_scene=99;uint64_t last_applied=0;
 for(;;) {
  CameraSettings settings;enum StudioAction action;uint64_t serial;DWORD requested;int flush;
  wchar_t selected[MAX_PATH],core[MAX_PATH];
  AcquireSRWLockExclusive(&ui->lock);settings=ui->settings;serial=ui->settings_serial;
  action=ui->action;ui->action=ACTION_NONE;requested=ui->target_pid;
  flush=ui->flush_settings;ui->flush_settings=0;
  wcscpy(selected,ui->selected_exe);wcscpy(core,ui->core_path);ReleaseSRWLockExclusive(&ui->lock);
  if(action==ACTION_QUIT){disconnect(&c);PostMessageW(ui->window,STUDIO_CLOSED,0,0);return 0;}
  if(action==ACTION_START) {
   AcquireSRWLockExclusive(&ui->lock);ui->working=1;ReleaseSRWLockExclusive(&ui->lock);
   if(c.pipe&&((requested&&requested!=c.pid)||*selected))disconnect(&c);
   int ok=c.pipe!=NULL||connect_game(&c,requested,selected,core);ControlReply reply;
   if(ok)ok=send(&c,CONTROL_SETTINGS,&settings,&reply)&&reply.error==CONTROL_OK&&
    send(&c,CONTROL_ENABLE,NULL,&reply)&&reply.error==CONTROL_OK;
   if(ok) {
    c.sent_serial=serial;publish(&c,&reply,1);previous_scene=99;last_applied=0;
    studio_log(ui,1,L"[start] Camera enabled. Right stick / L3 reset / F8 toggle.");
   }else {disconnect(&c);studio_log(ui,2,L"[start] Camera was not enabled.");}
   AcquireSRWLockExclusive(&ui->lock);ui->working=0;ReleaseSRWLockExclusive(&ui->lock);
   PostMessageW(ui->window,STUDIO_UPDATE,0,0);
  }
  if(c.pipe) {
   ControlReply reply;int ok=1;
   if(action==ACTION_STOP||action==ACTION_RESET) {
    ok=send(&c,action==ACTION_STOP?CONTROL_DISABLE:CONTROL_RESET,NULL,&reply)&&reply.error==CONTROL_OK;
    if(ok){publish(&c,&reply,1);studio_log(ui,0,action==ACTION_STOP?L"[stop] Stock camera restored.":L"[reset] Return behind hunter queued.");}
   }
   ULONGLONG now=GetTickCount64();
   if(ok&&serial!=c.sent_serial&&(flush||now-last_settings>=33)) {
    ok=send(&c,CONTROL_SETTINGS,&settings,&reply)&&reply.error==CONTROL_OK;last_settings=now;
    if(ok){c.sent_serial=serial;publish(&c,&reply,1);}
   }
   if(ok&&now-last_status>=100) {
    ok=send(&c,CONTROL_STATUS,NULL,&reply)&&reply.error==CONTROL_OK;last_status=now;
    if(ok) {
     publish(&c,&reply,1);
     if(reply.scene!=previous_scene) {
      const wchar_t *scenes[]={L"disabled",L"waiting for gameplay",L"waiting for focus",
       L"settling after transition",L"waiting for reference camera",L"geometry fallback",L"active"};
      studio_log(ui,0,L"[scene] %ls",reply.scene<7?scenes[reply.scene]:L"unknown");previous_scene=reply.scene;
     }
     if(reply.applied_revision!=last_applied&&reply.applied_revision==reply.accepted_revision) {
      studio_log(ui,1,L"[applied] Profile revision %llu / distance %.0f -> %.0f",
       (unsigned long long)reply.applied_revision,reply.requested_radius,reply.actual_radius);
      last_applied=reply.applied_revision;
     }
    }
   }
   if(!ok){studio_log(ui,2,L"[link] Connection lost. Camera will return to native mode.");disconnect(&c);}
  }else if(action==ACTION_STOP||action==ACTION_RESET)studio_log(ui,0,L"[info] Connect with start first.");
  Sleep(10);
 }
}
int studio_backend_start(StudioBackend *b,HWND window,const wchar_t *core) {
 memset(b,0,sizeof(*b));InitializeSRWLock(&b->lock);InitializeSRWLock(&b->log_lock);log_path(b);b->settings=camera_defaults();
 b->window=window;wcscpy(b->core_path,core);b->settings_serial=1;
 studio_log(b,0,L"[version] Frontier Helper 0.10.0 / Compact GUI / Rain core from Studio 0.8.1.");
 studio_log(b,0,*b->log_path?L"[log] Persistent attach journal enabled.":L"[log] Persistent journal could not be created.");
 b->thread=CreateThread(NULL,0,worker,b,0,NULL);return b->thread!=NULL;
}
void studio_settings(StudioBackend *b,const CameraSettings *s) {
 if(!camera_settings_valid(s))return;
 AcquireSRWLockExclusive(&b->lock);b->settings=*s;b->settings_serial++;ReleaseSRWLockExclusive(&b->lock);
}
void studio_flush(StudioBackend *b) {
 AcquireSRWLockExclusive(&b->lock);b->flush_settings=1;ReleaseSRWLockExclusive(&b->lock);
}
void studio_action(StudioBackend *b,enum StudioAction action,DWORD pid) {
 AcquireSRWLockExclusive(&b->lock);
 /* Closing is never overwritten by a late button or slider message. */
 if(b->action!=ACTION_QUIT){b->action=action;if(action==ACTION_START){b->target_pid=pid;b->working=1;}}
 ReleaseSRWLockExclusive(&b->lock);
}
void studio_select_exe(StudioBackend *b,const wchar_t *path) {
 AcquireSRWLockExclusive(&b->lock);wcscpy(b->selected_exe,path);ReleaseSRWLockExclusive(&b->lock);
}
