#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
static XINPUT_STATE pad;
DWORD WINAPI XInputGetState(DWORD index,XINPUT_STATE *state) {
 if(index!=3||!state)return ERROR_DEVICE_NOT_CONNECTED;*state=pad;return ERROR_SUCCESS;
}
void WINAPI SetTestPad(WORD buttons,SHORT rx,SHORT ry,BYTE lt,BYTE rt) {
 pad.dwPacketNumber++;pad.Gamepad.wButtons=buttons;pad.Gamepad.sThumbRX=rx;pad.Gamepad.sThumbRY=ry;
 pad.Gamepad.bLeftTrigger=lt;pad.Gamepad.bRightTrigger=rt;
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD why,void *reserved) {(void)h;(void)why;(void)reserved;return TRUE;}
