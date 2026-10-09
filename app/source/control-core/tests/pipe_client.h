#ifndef TEST_PIPE_CLIENT_H
#define TEST_PIPE_CLIENT_H
#ifdef NDEBUG
#error Tests require enabled assertions
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include "../src/control_protocol.h"
static HANDLE connect_controller(DWORD pid,uint64_t created) {
 char name[120];snprintf(name,sizeof(name),"\\\\.\\pipe\\FrontierOrbit.%lu.%016llx",
  (unsigned long)pid,(unsigned long long)created);
 HANDLE pipe=INVALID_HANDLE_VALUE;
 for(int i=0;i<100;i++) {
  pipe=CreateFileA(name,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
  if(pipe!=INVALID_HANDLE_VALUE)break;Sleep(20);
 }
 assert(pipe!=INVALID_HANDLE_VALUE);
 DWORD server=0,mode=PIPE_READMODE_MESSAGE;
 assert(GetNamedPipeServerProcessId(pipe,&server)&&server==pid);
 assert(SetNamedPipeHandleState(pipe,&mode,NULL,NULL));return pipe;
}
static ControlReply request(HANDLE pipe,unsigned command,uint32_t id,const CameraSettings *settings) {
 struct {ControlHeader header;CameraSettings settings;} message;
 message.header=(ControlHeader){CONTROL_MAGIC,CONTROL_VERSION,(uint16_t)command,id,
  settings?sizeof(*settings):0,0};
 if(settings)message.settings=*settings;
 DWORD count=0,size=sizeof(ControlHeader)+message.header.payload_size;
 assert(WriteFile(pipe,&message,size,&count,NULL)&&count==size);
 struct {ControlHeader header;ControlReply reply;} response;
 assert(ReadFile(pipe,&response,sizeof(response),&count,NULL)&&count==sizeof(response));
 assert(response.header.magic==CONTROL_MAGIC&&response.header.version==CONTROL_VERSION&&
  response.header.command==(command|CONTROL_REPLY_BIT)&&response.header.request_id==id&&
  response.header.payload_size==sizeof(ControlReply)&&response.header.reserved==0);
 return response.reply;
}
static uint64_t process_created(void) {
 FILETIME c,e,k,u;assert(GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u));
 return ((uint64_t)c.dwHighDateTime<<32)|c.dwLowDateTime;
}
#endif
