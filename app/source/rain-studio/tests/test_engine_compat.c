#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../src/engine_compat.h"
static BYTE *image;
static void page(uintptr_t rva,SIZE_T size,DWORD protection){
 uintptr_t first=rva&~(uintptr_t)4095,last=(rva+size+4095)&~(uintptr_t)4095;
 assert(VirtualAlloc(image+first,last-first,MEM_COMMIT,protection));
}
int main(int argc,char **argv){
 unsigned failure=0;
 if(argc==2){DWORD pid=(DWORD)strtoul(argv[1],NULL,10);HANDLE target=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid);assert(target);
  int ok=frontier_engine_compatible(target,frontier_engine_base,&failure);CloseHandle(target);
  printf("LIVE engine compatibility: %s; failed check=%u\n",ok?"PASS":"FAIL",failure);return ok?0:1;
 }
 image=VirtualAlloc((void*)frontier_engine_base,0xf200000,MEM_RESERVE,PAGE_NOACCESS);assert(image==(BYTE*)frontier_engine_base);
 for(unsigned i=0;i<sizeof(frontier_blocks)/sizeof(frontier_blocks[0]);i++){page(frontier_blocks[i].rva,frontier_blocks[i].size,PAGE_EXECUTE_READWRITE);memcpy(image+frontier_blocks[i].rva,frontier_blocks[i].code,frontier_blocks[i].size);}
 const uintptr_t data[]={0x15d242c,0x18e469c,0x1c4a0d4,0x1c4a168,0x1c4a409,0x1c4a41d,0xe77dc2c,0xe7fff40,0xedaad60};
 for(unsigned i=0;i<sizeof(data)/sizeof(data[0]);i++)page(data[i],0x98,PAGE_READWRITE);
 assert(frontier_engine_compatible(GetCurrentProcess(),(uintptr_t)image,&failure));
 for(unsigned i=0;i<sizeof(frontier_blocks)/sizeof(frontier_blocks[0]);i++){
  BYTE *b=image+frontier_blocks[i].rva;b[0]^=1;assert(!frontier_engine_compatible(GetCurrentProcess(),(uintptr_t)image,&failure)&&failure==i+1);b[0]^=1;
 }
 /* Formerly masked absolute address operand must not auto-accept a moved data layout. */
 image[frontier_blocks[0].rva+12]^=1;assert(!frontier_engine_compatible(GetCurrentProcess(),(uintptr_t)image,&failure)&&failure==1);image[frontier_blocks[0].rva+12]^=1;
 DWORD old;assert(VirtualProtect(image+frontier_blocks[0].rva,64,PAGE_READWRITE,&old));assert(!frontier_engine_compatible(GetCurrentProcess(),(uintptr_t)image,&failure)&&failure==1);DWORD ignored;assert(VirtualProtect(image+frontier_blocks[0].rva,64,old,&ignored));
 assert(VirtualProtect(image+data[8],4,PAGE_NOACCESS,&old));assert(!frontier_engine_compatible(GetCurrentProcess(),(uintptr_t)image,&failure)&&failure==108);assert(VirtualProtect(image+data[8],4,old,&ignored));
 assert(!frontier_engine_compatible(GetCurrentProcess(),(uintptr_t)image+4096,&failure)&&failure==200);
 assert(frontier_engine_compatible(GetCurrentProcess(),(uintptr_t)image,&failure));
 assert(VirtualFree(image,0,MEM_RELEASE));
 puts("PASS: valid layout, 13 changed code blocks, changed address operand, non-executable code, inaccessible data, wrong base and restored layout.");return 0;
}
