#define WIN32_LEAN_AND_MEAN
#include <windows.h>
__declspec(dllexport) float *WINAPI D3DXMatrixLookAtRH(float *out,
 const float *eye,const float *target,const float *up) {
 (void)eye;(void)target;(void)up;
 for(int i=0;i<16;i++)out[i]=i%5==0?1:0;return out;
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,void *reserved) {
 (void)h;(void)reason;(void)reserved;return TRUE;
}
