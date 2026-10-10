#ifndef FRONTIER_ENGINE_COMPAT_H
#define FRONTIER_ENGINE_COMPAT_H
#include <windows.h>
#include <stdint.h>
#include <string.h>
/* Known HD ABI: exact instructions AND address operands. File hashes are diagnostic only. */
static const uintptr_t frontier_engine_base=0x10000000;
static const unsigned char frontier_code_0[]={85,139,236,131,228,240,131,236,104,161,132,248,156,17,51,196,137,68,36,100,139,21,96,173,218,30,243,15,16,130,140,0,0,0,243,15,92,130,128,0,0,0,15,87,210,243,15,16,138,144,0,0,0,243,15,92,138,132,0,0,0,15,46,194};
static const unsigned char frontier_code_1[]={81,161,96,173,218,30,217,128,180,0,0,0,131,236,16,217,92,36,12,217,128,176,0,0,0,217,92,36,8,217,5,104,215,157,17,217,92,36,4,217,128,184,0,0,0,217,28,36,104,80,239,135,30,255,21,32,36,93,17,106,32};
static const unsigned char frontier_code_2[]={86,139,53,96,173,218,30,104,128,0,0,0,106,0,86,232,220,83,160,0,161,24,124,133,17,243,15,16,5,112,102,155,17,137,134,128,0,0,0,139,13,28,124,133,17,137,142,132,0,0,0,139,21,32,124,133,17,137,150,136,0,0,0};
static const unsigned char frontier_code_3[]={85,139,236,131,228,240,129,236,196,0,0,0,161,132,248,156,17,51,196,137,132,36,192,0,0,0,243,15,16,5,112,70,142,17,83,139,93,12,51,201,86,139,117,8,87,137,116,36,44,137,92,36,40,137,76,36,84,137,76,36,52};
static const unsigned char frontier_code_4[]={85,139,236,131,236,40,161,60,255,127,30,128,184,255,35,0,0,0,83,86,87,139,61,212,160,196,17,198,69,255,0,198,69,254,0,137,125,248,117,17,129,61,4,221,119,30,240,207,0,0,117,5,232,167,11,0,0,246,5,24,255,234,30,64};
static const unsigned char frontier_code_5[]={85,139,236,129,236,140,4,0,0,161,132,248,156,17,51,197,137,69,252,139,21,60,255,127,30,128,186,255,35,0,0,0,139,69,16,83,139,93,8,86,139,53,44,220,119,30,87,137,141,240,251,255,255,137,133,176,251,255,255};
static const unsigned char frontier_code_6[]={85,139,236,129,236,144,0,0,0,161,132,248,156,17,51,197,137,69,248,83,86,87,139,69,8,243,15,16,89,8,243,15,16,80,16,243,15,16,97,12,243,15,16,65,4,243,15,16,72,12,243,15,16,105,40,243,15,16,113,48,141,89,4};
static const unsigned char frontier_code_7[]={85,139,236,131,236,24,243,15,16,1,243,15,16,61,156,70,142,17,15,47,248,243,15,17,69,248,15,135,238,1,0,0,243,15,16,73,8,15,47,249,243,15,17,77,244,15,135,219,1,0,0,86,139,53,44,220,119,30,139,70,20};
static const unsigned char frontier_code_8[]={85,139,236,131,228,240,129,236,152,0,0,0,161,132,248,156,17,51,196,137,132,36,148,0,0,0,139,69,12,83,139,93,8,86,137,68,36,12,199,68,36,8,0,0,0,0,190,252,139,141,30,139,70,60,133,192,15,132,0,1,0,0};
static const unsigned char frontier_code_9[]={85,139,236,131,236,124,161,132,248,156,17,51,197,137,69,252,83,86,87,139,249,243,15,16,71,4,139,199,137,125,200,51,219,243,15,17,69,212,232,165,241,255,255,131,248,1,15,133,192,1,0,0,139,53,44,220,119,30,139,70,32,219,70,32};
static const unsigned char frontier_code_10[]={85,139,236,131,236,12,243,15,16,0,243,15,16,13,156,70,142,17,15,47,200,243,15,17,69,252,119,110,243,15,16,64,8,15,47,200,243,15,17,69,248,119,95,139,13,44,220,119,30,139,65,44,15,175,65,32,137,69,244,219,69,244,133,192};
static const unsigned char frontier_code_11[]={85,139,236,81,139,13,60,255,127,30,131,121,52,66,15,87,192,243,15,17,0,117,39,131,61,48,219,128,30,0,116,30,186,118,1,0,0,102,57,81,20,117,19,80,139,198,232,221,195,171,0,132,192,120,7,15,190,192,139,229,93,195,139,214};
static const unsigned char frontier_code_12[]={85,139,236,161,60,255,127,30,131,236,20,128,184,255,35,0,0,0,87,116,59,139,14,139,86,4,139,70,8,137,77,240,141,77,252,81,141,125,240,137,85,244,137,69,248,232,206,134,188,0,131,248,1,117,25,139,206,232,82,187,10,0,243,15};
static const struct {uintptr_t rva;const unsigned char *code;SIZE_T size;} frontier_blocks[]={{12250640,frontier_code_0,sizeof(frontier_code_0)},{12251904,frontier_code_1,sizeof(frontier_code_1)},{12250464,frontier_code_2,sizeof(frontier_code_2)},{8524368,frontier_code_3,sizeof(frontier_code_3)},{8519312,frontier_code_4,sizeof(frontier_code_4)},{9244432,frontier_code_5,sizeof(frontier_code_5)},{9243456,frontier_code_6,sizeof(frontier_code_6)},{9217888,frontier_code_7,sizeof(frontier_code_7)},{20874464,frontier_code_8,sizeof(frontier_code_8)},{9223728,frontier_code_9,sizeof(frontier_code_9)},{9220096,frontier_code_10,sizeof(frontier_code_10)},{9204928,frontier_code_11,sizeof(frontier_code_11)},{0x8202a0,frontier_code_12,sizeof(frontier_code_12)}};
static int frontier_range(HANDLE process,uintptr_t base,uintptr_t rva,SIZE_T size,int executable){
 MEMORY_BASIC_INFORMATION info;uintptr_t address=base+rva;
 if(!VirtualQueryEx(process,(void*)address,&info,sizeof(info)))return 0;
 if(info.State!=MEM_COMMIT||(info.Protect&(PAGE_GUARD|PAGE_NOACCESS))||info.AllocationBase!=(void*)base)return 0;
 if(address<(uintptr_t)info.BaseAddress||size>info.RegionSize-(address-(uintptr_t)info.BaseAddress))return 0;
 return !executable||(info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
}
static int frontier_engine_compatible(HANDLE process,uintptr_t base,unsigned *failure){
 if(failure)*failure=0;
 if(base!=frontier_engine_base){if(failure)*failure=200;return 0;}
 for(unsigned i=0;i<sizeof(frontier_blocks)/sizeof(frontier_blocks[0]);i++){
  unsigned char actual[64];SIZE_T got=0;
  if(frontier_blocks[i].size>sizeof(actual)||!frontier_range(process,base,frontier_blocks[i].rva,frontier_blocks[i].size,1)||
   !ReadProcessMemory(process,(void*)(base+frontier_blocks[i].rva),actual,frontier_blocks[i].size,&got)||got!=frontier_blocks[i].size||
   memcmp(actual,frontier_blocks[i].code,frontier_blocks[i].size)){
   if(failure)*failure=i+1;return 0;
  }
 }
 static const struct {uintptr_t rva;SIZE_T size;} data[]={
  {0x15d242c,4},{0x18e469c,4},{0x1c4a0d4,4},{0x1c4a168,4},
  {0x1c4a409,1},{0x1c4a41d,1},{0xe77dc2c,4},{0xe7fff40,0x98},{0xedaad60,4}
 };
 for(unsigned i=0;i<sizeof(data)/sizeof(data[0]);i++)if(!frontier_range(process,base,data[i].rva,data[i].size,0)){
  if(failure)*failure=100+i;return 0;
 }
 return 1;
}
#endif
