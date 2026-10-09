#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* Sparse synthetic image, not game data. */
__declspec(dllexport) unsigned char fixture_arena[0xf200000];
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,void *reserved) {
 (void)h;(void)reason;(void)reserved;return TRUE;
}
