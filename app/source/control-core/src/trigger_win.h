#ifndef FRONTIER_TRIGGER_WIN_H
#define FRONTIER_TRIGGER_WIN_H
#include <windows.h>
#include <xinput.h>
typedef struct {volatile LONG status,calls,applied,bytes,left,right;} FrontierInputStats;
typedef DWORD (WINAPI *FrontierPadRead)(DWORD,XINPUT_STATE*);
typedef int (*FrontierInputContext)(void *opaque,int *gamepad_index);
/* Hook status: 1 installed; 2 observed in-game input; negative means unavailable. */
int frontier_trigger_install(FrontierPadRead read_pad,FrontierInputContext context,void *opaque,
 unsigned left,unsigned right,const GUID *preferred,FrontierInputStats *stats);
int frontier_trigger_bindings(const wchar_t *exe,unsigned *left,unsigned *right,GUID *preferred);
void frontier_trigger_restore(void);
#endif
