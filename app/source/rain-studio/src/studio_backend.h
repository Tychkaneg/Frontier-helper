#ifndef ORBIT_STUDIO_BACKEND_H
#define ORBIT_STUDIO_BACKEND_H
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../../control-core/src/control_protocol.h"
#define STUDIO_LOG (WM_APP+1)
#define STUDIO_UPDATE (WM_APP+2)
#define STUDIO_CLOSED (WM_APP+3)
enum StudioAction {ACTION_NONE,ACTION_START,ACTION_STOP,ACTION_RESET,ACTION_QUIT};
typedef struct {int level;wchar_t text[512];} StudioLog;
typedef struct {
 SRWLOCK lock,log_lock;
 HWND window;
 CameraSettings settings;
 uint64_t settings_serial;
 enum StudioAction action;
 DWORD target_pid;
 wchar_t selected_exe[MAX_PATH],core_path[MAX_PATH],log_path[MAX_PATH];
 ControlReply status;
 int connected,working,flush_settings;
 HANDLE thread;
} StudioBackend;
void studio_log(StudioBackend *b,int level,const wchar_t *format,...);
int studio_backend_start(StudioBackend *b,HWND window,const wchar_t *core);
void studio_settings(StudioBackend *b,const CameraSettings *s);
void studio_flush(StudioBackend *b);
void studio_action(StudioBackend *b,enum StudioAction action,DWORD pid);
void studio_select_exe(StudioBackend *b,const wchar_t *path);
#endif
