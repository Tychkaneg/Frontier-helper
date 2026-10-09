#include "studio_backend.h"
#include "profile_store.h"
#include "helper_renderer.h"
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <stdarg.h>
#include <wchar.h>
#include <stdio.h>
#include <stddef.h>
#include <math.h>

#define MAX_LINES 500
#define MAX_MENU 256
#define ID_COMMAND 101
#define ROW_HEIGHT 30
enum {MENU_NONE, MENU_HELP, MENU_CAMERA, MENU_SETUPS, MENU_FILES};
enum {TEXT_MUTED, TEXT_OK, TEXT_ERROR, TEXT_NORMAL, TEXT_COMMAND, TEXT_HEADING, TEXT_MENU};
typedef struct {wchar_t text[512],label[128],detail[256]; int level;} Line;
typedef struct {const wchar_t *label,*key; size_t offset; float lo,hi,step; const wchar_t *unit;} Parameter;
static const Parameter parameters[]={
 {L"Distance",L"distance",offsetof(CameraSettings,distance_scale),.5f,1.5f,.01f,L"x"},
 {L"Height offset",L"height",offsetof(CameraSettings,height_offset),-250,250,1,L"u"},
 {L"Shoulder",L"shoulder",offsetof(CameraSettings,shoulder_offset),-300,300,1,L"u"},
 {L"Response",L"response",offsetof(CameraSettings,response),1,100,.5f,L""},
 {L"Deadzone",L"deadzone",offsetof(CameraSettings,deadzone),.02f,.5f,.01f,L""},
 {L"Yaw speed",L"yaw",offsetof(CameraSettings,yaw_speed),20,720,1,L"deg/s"},
 {L"Pitch speed",L"pitch",offsetof(CameraSettings,pitch_speed),10,360,1,L"deg/s"},
 {L"Up limit",L"min",offsetof(CameraSettings,min_pitch),-85,0,1,L"deg"},
 {L"Down limit",L"max",offsetof(CameraSettings,max_pitch),0,85,1,L"deg"},
 {L"Reset tilt",L"resetpitch",offsetof(CameraSettings,reset_pitch),-85,85,1,L"deg"}
};
static HWND window,command_edit;
static WNDPROC original_edit;
static StudioBackend backend;
static CameraSettings settings;
static ControlReply status;
static int width=1120,height=800,dpi=96,connected,working,closing,self_test_mode;
static HFONT font_mono,font_meta,font_title,font_logo;
static HBRUSH edit_brush;
static int canvas_active,last_render_ok;
static HRESULT frame_config_result=E_FAIL;
static Line lines[MAX_LINES]; static int line_count,scroll_rows;
static int menu_kind,menu_start=-1,menu_count,menu_selected,edit_parameter=-1,edit_name;
static wchar_t menu_labels[MAX_MENU][128],menu_values[MAX_MENU][256];
static SavedSetup *setups; static int setup_count;
static wchar_t folder[MAX_PATH],app_folder[MAX_PATH],client_path[MAX_PATH],current_name[128]=L"My setup";
static wchar_t history[64][256]; static int history_count,history_cursor;
static RECT menu_hits[MAX_MENU]; static int menu_hit_items[MAX_MENU],menu_hit_count;
static ULONGLONG last_frames_tick; static uint64_t observed_frames;
static const COLORREF bg=RGB(11,11,11),surface=RGB(20,20,20),selection=RGB(45,27,12),
 border=RGB(53,48,42),muted=RGB(164,155,145),ink=RGB(243,238,232),
 orange=RGB(255,138,36),soft=RGB(255,171,87),success=RGB(255,192,120),error=RGB(255,104,40);
static void command(const wchar_t *line);
static void open_menu(int kind);
static void activate_item(void);
static void layout(void);
static void create_fonts(void);
static void configure_frame(HWND h){
 enum DWMNCRENDERINGPOLICY policy=DWMNCRP_DISABLED;
 frame_config_result=DwmSetWindowAttribute(h,DWMWA_NCRENDERING_POLICY,&policy,sizeof(policy));
 COLORREF color=DWMWA_COLOR_NONE;
 DwmSetWindowAttribute(h,DWMWA_BORDER_COLOR,&color,sizeof(color));
 /* Keep native sizing hit tests without letting either compositor draw a frame. */
 SetWindowPos(h,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
}

static int px(int v){return MulDiv(v,dpi,96);}
static RECT rect(int x,int y,int w,int h){RECT r={px(x),px(y),px(x+w),px(y+h)};return r;}
static void input_prompt(wchar_t s[80]){
 swprintf(s,80,L"%ls >",edit_parameter>=0?parameters[edit_parameter].key:edit_name?L"name":L"helper");
}
static void input_geometry(RECT *bounds,RECT *format,TEXTMETRICW *metrics){
 GetWindowRect(command_edit,bounds);MapWindowPoints(NULL,window,(POINT*)bounds,2);
 SendMessageW(command_edit,EM_GETRECT,0,(LPARAM)format);
 HDC dc=GetDC(command_edit);HGDIOBJ old=SelectObject(dc,font_mono);GetTextMetricsW(dc,metrics);SelectObject(dc,old);ReleaseDC(command_edit,dc);
}
static void draw_input_prompt(HDC dc){
 if(!command_edit)return;
 wchar_t prompt[80];input_prompt(prompt);RECT bounds,format;TEXTMETRICW metrics;input_geometry(&bounds,&format,&metrics);
 int top=format.top;LRESULT first=SendMessageW(command_edit,EM_POSFROMCHAR,0,0);
 if(first!=-1)top=(SHORT)HIWORD(first);
 /* Use the edit control's font and origin for both halves of this text line. */
 HGDIOBJ old=SelectObject(dc,font_mono);SetTextColor(dc,orange);SetBkMode(dc,TRANSPARENT);
 TextOutW(dc,px(40),bounds.top+top,prompt,(int)wcslen(prompt));SelectObject(dc,old);
}
static void redraw(void){if(window)InvalidateRect(window,NULL,FALSE);}
static void fill(HDC dc,int x,int y,int w,int h,COLORREF color){
 if(canvas_active){helper_canvas_fill((float)x,(float)y,(float)w,(float)h,color);return;}
 RECT r=rect(x,y,w,h);HBRUSH brush=CreateSolidBrush(color);FillRect(dc,&r,brush);DeleteObject(brush);
}
static void text(HDC dc,int x,int y,const wchar_t *s,HFONT font,COLORREF color){
 if(canvas_active){helper_canvas_text((float)x,(float)y,(float)(width-x),font==font_title||font==font_logo?36.f:26.f,s,font,color,0,1);return;}
 SelectObject(dc,font);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);TextOutW(dc,px(x),px(y),s,(int)wcslen(s));
}
static void line(int level,const wchar_t *format,...){
 wchar_t s[512];va_list args;va_start(args,format);vswprintf(s,512,format,args);va_end(args);
 if(line_count==MAX_LINES){memmove(lines,lines+1,(MAX_LINES-1)*sizeof(*lines));line_count--;if(menu_start>=0)menu_start--;}
 lines[line_count].level=level;wcsncpy(lines[line_count].text,s,511);lines[line_count].text[511]=0;lines[line_count].label[0]=0;lines[line_count].detail[0]=0;line_count++;scroll_rows=0;redraw();
}
static float value(int i){float v;memcpy(&v,(BYTE*)&settings+parameters[i].offset,sizeof(v));return v;}
static float lower(int i){return i==9?settings.min_pitch:parameters[i].lo;}
static float upper(int i){return i==9?settings.max_pitch:parameters[i].hi;}
static void flush_settings(void){if(!self_test_mode){studio_settings(&backend,&settings);studio_flush(&backend);}redraw();}
static void change(int i,float v){
 v=fmaxf(lower(i),fminf(upper(i),v));memcpy((BYTE*)&settings+parameters[i].offset,&v,sizeof(v));
 settings.reset_pitch=fmaxf(settings.min_pitch,fminf(settings.max_pitch,settings.reset_pitch));flush_settings();
}
static const wchar_t *scene_name(void){
 if(closing)return L"CLOSING";if(working)return L"CONNECTING";if(!connected)return L"DISCONNECTED";
 if(!status.enabled)return L"STOPPED";
 if(status.scene==CAMERA_ACTIVE&&(status.applied_revision!=status.accepted_revision||GetTickCount64()-last_frames_tick>1200))return L"WAITING FOR FRAME";
 const wchar_t *names[]={L"DISABLED",L"WAITING FOR SCENE",L"WAITING FOR FOCUS",L"SETTLING",L"REFERENCE PENDING",L"GEOMETRY FALLBACK",L"ACTIVE"};
 return status.scene<7?names[status.scene]:L"UNKNOWN";
}
static void refresh_setups(void){
 int count=setup_list(folder,NULL,0);SavedSetup *next=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(size_t)(count+1)*sizeof(SavedSetup));
 if(!next)return;if(setups)HeapFree(GetProcessHeap(),0,setups);setups=next;setup_count=setup_list(folder,setups,count+1);
}
static int file_dialog(wchar_t path[MAX_PATH],int save,const wchar_t *filter,const wchar_t *extension){
 OPENFILENAMEW o={0};o.lStructSize=sizeof(o);o.hwndOwner=window;o.lpstrFile=path;o.nMaxFile=MAX_PATH;
 o.lpstrFilter=filter;o.lpstrDefExt=extension;o.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
 return save?GetSaveFileNameW(&o):GetOpenFileNameW(&o);
}
static void close_menu(void){menu_kind=MENU_NONE;menu_start=-1;menu_count=0;menu_hit_count=0;redraw();}
static void save_setup(const wchar_t *name){
 if(!*name){line(TEXT_ERROR,L"Use save <name>.");return;}
 StudioProfile p={0};p.camera=settings;
 if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,name,-1,p.name,sizeof(p.name),NULL,NULL)){line(TEXT_ERROR,L"Profile name is too long.");return;}
 refresh_setups();wchar_t path[MAX_PATH]=L"";
 for(int i=0;i<setup_count;i++)if(!_wcsicmp(name,setups[i].name)){wcscpy(path,setups[i].path);break;}
 if(!*path){FILETIME ft;GetSystemTimeAsFileTime(&ft);swprintf(path,MAX_PATH,L"%ls\\setup-%08lx%08lx.json",folder,ft.dwHighDateTime,ft.dwLowDateTime);}
 if(!setup_write(path,&p)){line(TEXT_ERROR,L"Cannot save profile. Check the setups folder.");return;}
 wcsncpy(current_name,name,127);current_name[127]=0;refresh_setups();line(TEXT_OK,L"Saved %ls.",current_name);
}
static void load_setup(int index){
 if(index<0||index>=setup_count)return;settings=setups[index].data.camera;flush_settings();wcscpy(current_name,setups[index].name);
 line(TEXT_OK,L"Loaded %ls. Camera values updated.",current_name);
}
static void import_profile(const wchar_t *argument){
 wchar_t path[MAX_PATH]=L"";if(*argument){if(wcslen(argument)>=MAX_PATH){line(TEXT_ERROR,L"Path is too long.");return;}wcscpy(path,argument);}
 else if(!file_dialog(path,0,L"Camera profile\0*.json\0\0",L"json"))return;
 StudioProfile p;if(!setup_read(path,&p)){line(TEXT_ERROR,L"Invalid profile; camera values kept.");return;}
 settings=p.camera;MultiByteToWideChar(CP_UTF8,0,p.name,-1,current_name,128);flush_settings();line(TEXT_OK,L"Imported %ls. Type save to keep a local copy.",current_name);
}
static void export_profile(const wchar_t *argument){
 wchar_t path[MAX_PATH]=L"camera-setup.json";
 if(*argument){if(wcslen(argument)>=MAX_PATH){line(TEXT_ERROR,L"Path is too long.");return;}wcscpy(path,argument);}
 else if(!file_dialog(path,1,L"Camera profile\0*.json\0\0",L"json"))return;
 StudioProfile p={0};p.camera=settings;
 if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,current_name,-1,p.name,sizeof(p.name),NULL,NULL)){line(TEXT_ERROR,L"Profile name is too long.");return;}
 int ok=setup_write(path,&p);line(ok?TEXT_OK:TEXT_ERROR,ok?L"Profile exported.":L"Cannot export profile.");
}
static void export_log(void){
 wchar_t path[MAX_PATH]=L"helper-session.txt";if(!file_dialog(path,1,L"Text log\0*.txt\0\0",L"txt"))return;
 if(!*backend.log_path||!CopyFileW(backend.log_path,path,FALSE)){line(TEXT_ERROR,L"Cannot export the session journal.");return;}line(TEXT_OK,L"Session journal exported.");
}
static void show_status(void){
 line(TEXT_HEADING,L"SESSION / TELEMETRY");line(TEXT_NORMAL,L"State                 %ls",scene_name());
 if(!connected){line(TEXT_MUTED,L"Open Rain, enter the city, then type start.");return;}
 line(TEXT_NORMAL,L"Process               %lu / x86",status.game_pid);
 line(TEXT_NORMAL,L"Controller            %ls",status.pad_present?L"connected":L"not detected");
 line(TEXT_NORMAL,L"Applied frames        %llu",(unsigned long long)status.frames);
 line(TEXT_NORMAL,L"Distance / requested  %.0f / %.0f",status.actual_radius,status.requested_radius);
 line(TEXT_NORMAL,L"Yaw / pitch           %.0f / %.0f",status.yaw,status.pitch);
 line(TEXT_NORMAL,L"Profile / requested   %llu / %llu",(unsigned long long)status.applied_revision,(unsigned long long)status.accepted_revision);
}
static void menu_item(const wchar_t *label,const wchar_t *v){
 if(menu_count>=MAX_MENU)return;wcsncpy(menu_labels[menu_count],label,127);menu_labels[menu_count][127]=0;
 wcsncpy(menu_values[menu_count],v,255);menu_values[menu_count][255]=0;
 line(TEXT_MENU,L"  %-22ls  %ls",label,v);wcscpy(lines[line_count-1].label,menu_labels[menu_count]);wcscpy(lines[line_count-1].detail,menu_values[menu_count]);menu_count++;
}
static void open_menu(int kind){
 close_menu();menu_kind=kind;menu_selected=0;
 const wchar_t *head=kind==MENU_HELP?L"AVAILABLE COMMANDS":kind==MENU_CAMERA?L"CAMERA / CURRENT VALUES":kind==MENU_SETUPS?L"SAVED SETUPS":L"SESSION FILES";
 line(TEXT_HEADING,L"%ls",head);menu_start=line_count;
 if(kind==MENU_HELP){
  menu_item(L"camera",L"Position, rotation and limits");menu_item(L"setups",L"Load a camera profile");menu_item(L"files",L"Client, profiles and journal");
  menu_item(L"start",L"Attach to Rain manually");menu_item(L"stop",L"Restore the native camera");menu_item(L"status",L"Connection and telemetry");menu_item(L"save",L"Save current camera values");
  line(TEXT_MUTED,L"Also: set <parameter> <value> / import / export / defaults / clear");
 }else if(kind==MENU_CAMERA){
  for(int i=0;i<10;i++){wchar_t s[64];swprintf(s,64,i==0||i==4?L"%.2f%ls":L"%.3g%ls",value(i),parameters[i].unit);menu_item(parameters[i].label,s);}
  menu_item(L"Live preview",settings.preview?L"on":L"off");wchar_t pad[32];swprintf(pad,32,settings.gamepad_index<0?L"auto":L"%d",settings.gamepad_index+1);
  menu_item(L"Gamepad",pad);menu_item(L"Defaults",L"Restore default values");menu_item(L"Reset view",L"Recenter in game");
 }else if(kind==MENU_SETUPS){
  refresh_setups();for(int i=0;i<setup_count&&menu_count<MAX_MENU;i++){wchar_t s[96];swprintf(s,96,L"%.2fx  H%+.0f  S%+.0f",setups[i].data.camera.distance_scale,setups[i].data.camera.height_offset,setups[i].data.camera.shoulder_offset);menu_item(setups[i].name,s);}
  if(!setup_count)line(TEXT_MUTED,L"No saved profiles yet. Type save <name> to create one.");
  if(setup_count>MAX_MENU)line(TEXT_MUTED,L"Showing first 256 profiles. Use load <name> for another profile.");
  line(TEXT_MUTED,L"save <name> / import / export / folder");
 }else{
  menu_item(L"Rain client",*client_path?client_path:L"Select executable (optional)");
  menu_item(L"Core",L"FrontierOrbitRain08.dll");menu_item(L"Saved setups",L"Open profile folder");menu_item(L"Attach journal",L"Open session log");
  menu_item(L"Export diagnostics",L"Save session journal");menu_item(L"Import profile",L"Open a JSON profile");menu_item(L"Export profile",L"Save current profile as JSON");
 }
 if(menu_count)line(TEXT_MUTED,L"Up / Down select   Enter open / edit   Esc close");else close_menu();redraw();
}
static void begin_parameter(int index){
 close_menu();edit_parameter=index;edit_name=0;line(TEXT_HEADING,L"%ls / %.3g - %.3g%ls",parameters[index].label,lower(index),upper(index),parameters[index].unit);
 line(TEXT_MUTED,L"Type a value or use Left / Right. Enter applies; Esc cancels.");
 wchar_t s[64];swprintf(s,64,L"%.3g",value(index));SetWindowTextW(command_edit,s);layout();SetFocus(command_edit);SendMessageW(command_edit,EM_SETSEL,0,-1);
}
static void begin_name(void){close_menu();edit_parameter=-1;edit_name=1;line(TEXT_HEADING,L"SAVE PROFILE / Enter a name; Esc cancels.");SetWindowTextW(command_edit,current_name);layout();SetFocus(command_edit);SendMessageW(command_edit,EM_SETSEL,0,-1);}
static void activate_item(void){
 int kind=menu_kind,index=menu_selected;if(!kind||index<0||index>=menu_count)return;
 if(kind==MENU_HELP){const wchar_t *cmd[]={L"camera",L"setups",L"files",L"start",L"stop",L"status",L"save"};command(cmd[index]);}
 else if(kind==MENU_CAMERA){
  if(index<10)begin_parameter(index);
  else if(index==10){settings.preview=!settings.preview;flush_settings();open_menu(MENU_CAMERA);menu_selected=10;}
  else if(index==11){settings.gamepad_index++;if(settings.gamepad_index>3)settings.gamepad_index=-1;flush_settings();open_menu(MENU_CAMERA);menu_selected=11;}
  else if(index==12)command(L"defaults");else command(L"reset");
 }else if(kind==MENU_SETUPS){close_menu();load_setup(index);}
 else if(kind==MENU_FILES){
  close_menu();
  if(index==0){wchar_t path[MAX_PATH]=L"";if(file_dialog(path,0,L"Rain executable\0*.exe\0\0",L"exe")){wcscpy(client_path,path);studio_select_exe(&backend,path);line(TEXT_OK,L"Client selected. Type start to connect.");}}
  else if(index==1)line(TEXT_NORMAL,L"Core: %ls",backend.core_path);
  else if(index==2)ShellExecuteW(window,L"open",folder,NULL,NULL,SW_SHOWNORMAL);
  else if(index==3){if(*backend.log_path)ShellExecuteW(window,L"open",backend.log_path,NULL,NULL,SW_SHOWNORMAL);else line(TEXT_ERROR,L"Journal unavailable.");}
  else if(index==4)export_log();else if(index==5)import_profile(L"");else if(index==6)export_profile(L"");
 }redraw();
}
static void command(const wchar_t *input){
 wchar_t buffer[256];wcsncpy(buffer,input,255);buffer[255]=0;wchar_t *c=buffer;while(*c==L' '||*c==L'/')c++;
 size_t len=wcslen(c);while(len&&c[len-1]==L' ')c[--len]=0;if(!*c)return;
 close_menu();line(TEXT_COMMAND,L"helper > %ls",c);wchar_t *args=wcschr(c,L' ');if(args){*args++=0;while(*args==L' ')args++;}else args=L"";
 if(!_wcsicmp(c,L"help"))open_menu(MENU_HELP);
 else if(!_wcsicmp(c,L"camera"))open_menu(MENU_CAMERA);
 else if(!_wcsicmp(c,L"setups"))open_menu(MENU_SETUPS);
 else if(!_wcsicmp(c,L"files"))open_menu(MENU_FILES);
 else if(!_wcsicmp(c,L"save")){if(*args)save_setup(args);else begin_name();}
 else if(!_wcsicmp(c,L"load")){
  refresh_setups();int found=0;for(int i=0;i<setup_count;i++)if(!_wcsicmp(args,setups[i].name)){load_setup(i);found=1;break;}if(!found)line(TEXT_ERROR,L"Profile not found. Type setups to browse.");
 }else if(!_wcsicmp(c,L"set")){
  wchar_t *p=wcschr(args,L' ');if(!p){line(TEXT_ERROR,L"Use set <parameter> <value>.");return;}*p++=0;
  for(wchar_t *q=p;*q;q++)if(*q==L',')*q=L'.';wchar_t *end;double v=wcstod(p,&end);while(*end==L' ')end++;
  int index=-1;for(int i=0;i<10;i++)if(!_wcsicmp(args,parameters[i].key))index=i;
  if(end==p||*end||!isfinite(v)||index<0||v<lower(index)||v>upper(index)){line(TEXT_ERROR,L"Unknown parameter or value outside its limits.");return;}
  change(index,(float)v);line(TEXT_OK,L"%ls = %.3g%ls",args,v,parameters[index].unit);
 }else if(!_wcsicmp(c,L"start")){
  DWORD pid=0;if(*args){wchar_t *end;unsigned long n=wcstoul(args,&end,10);if(*end||!n){line(TEXT_ERROR,L"Use start or start <PID>.");return;}pid=(DWORD)n;}
  if(working||closing){line(TEXT_MUTED,L"Wait for the current connection action.");return;}
  if(!self_test_mode){line(TEXT_MUTED,L"Connecting to the running Rain client...");studio_action(&backend,ACTION_START,pid);}
 }else if(!_wcsicmp(c,L"stop")){if(!self_test_mode)studio_action(&backend,ACTION_STOP,0);}
 else if(!_wcsicmp(c,L"reset")){if(!self_test_mode)studio_action(&backend,ACTION_RESET,0);line(TEXT_MUTED,L"Recenter requested. Type status to check the session.");}
 else if(!_wcsicmp(c,L"defaults")){settings=camera_defaults();flush_settings();line(TEXT_OK,L"Default camera values restored.");}
 else if(!_wcsicmp(c,L"preview")){
  if(!_wcsicmp(args,L"on"))settings.preview=1;else if(!_wcsicmp(args,L"off"))settings.preview=0;else {line(TEXT_ERROR,L"Use preview on or preview off.");return;}flush_settings();line(TEXT_OK,L"Live preview %ls.",settings.preview?L"on":L"off");
 }else if(!_wcsicmp(c,L"pad")){
  if(!_wcsicmp(args,L"auto"))settings.gamepad_index=-1;else if(wcslen(args)==1&&*args>=L'1'&&*args<=L'4')settings.gamepad_index=*args-L'1';else {line(TEXT_ERROR,L"Use pad auto or pad 1..4.");return;}flush_settings();line(TEXT_OK,L"Controller selection updated.");
 }else if(!_wcsicmp(c,L"status"))show_status();
 else if(!_wcsicmp(c,L"import"))import_profile(args);
 else if(!_wcsicmp(c,L"export"))export_profile(args);
 else if(!_wcsicmp(c,L"folder"))ShellExecuteW(window,L"open",folder,NULL,NULL,SW_SHOWNORMAL);
 else if(!_wcsicmp(c,L"log")){
  if(!_wcsicmp(args,L"export"))export_log();else if(*backend.log_path)ShellExecuteW(window,L"open",backend.log_path,NULL,NULL,SW_SHOWNORMAL);else line(TEXT_ERROR,L"Journal unavailable.");
 }else if(!_wcsicmp(c,L"clear")){line_count=0;scroll_rows=0;}
 else if(!_wcsicmp(c,L"quit")||!_wcsicmp(c,L"exit"))PostMessageW(window,WM_CLOSE,0,0);
 else line(TEXT_ERROR,L"Unknown command. Type help.");redraw();
}
static void submit_input(void){
 wchar_t s[256];GetWindowTextW(command_edit,s,256);
 if(edit_parameter>=0){
  for(wchar_t *p=s;*p;p++)if(*p==L',')*p=L'.';wchar_t *end;double v=wcstod(s,&end);while(*end==L' ')end++;
  if(end==s||*end||!isfinite(v)||v<lower(edit_parameter)||v>upper(edit_parameter)){line(TEXT_ERROR,L"Enter a value between %.3g and %.3g.",lower(edit_parameter),upper(edit_parameter));return;}
  int i=edit_parameter;edit_parameter=-1;change(i,(float)v);SetWindowTextW(command_edit,L"");line(TEXT_OK,L"%ls = %.3g%ls",parameters[i].key,v,parameters[i].unit);open_menu(MENU_CAMERA);menu_selected=i;return;
 }
 if(edit_name){if(!*s){line(TEXT_ERROR,L"Profile name cannot be empty.");return;}edit_name=0;SetWindowTextW(command_edit,L"");save_setup(s);return;}
 if(!*s){if(menu_kind)activate_item();return;}
 if(history_count==64){memmove(history,history+1,63*sizeof(history[0]));history_count--;}
 wcscpy(history[history_count++],s);history_cursor=history_count;SetWindowTextW(command_edit,L"");command(s);
}
static void complete_input(void){
 static const wchar_t *commands[]={L"help",L"camera",L"setups",L"files",L"start",L"stop",L"status",L"save",L"load",L"set",L"defaults",L"reset",L"preview",L"pad",L"import",L"export",L"folder",L"log",L"clear",L"quit"};
 wchar_t s[256];GetWindowTextW(command_edit,s,256);size_t n=wcslen(s);if(!n)return;
 const wchar_t *match=NULL;int count=0;for(size_t i=0;i<sizeof(commands)/sizeof(*commands);i++)if(!_wcsnicmp(s,commands[i],n)){match=commands[i];count++;}
 if(count==1){SetWindowTextW(command_edit,match);SendMessageW(command_edit,EM_SETSEL,256,256);}else if(count>1)line(TEXT_MUTED,L"Several commands match. Type another letter.");
}
static LRESULT CALLBACK edit_proc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_CHAR&&(w==VK_RETURN||w==VK_ESCAPE||w==VK_TAB))return 0;
 if(m==WM_KEYDOWN&&w==VK_RETURN){submit_input();layout();return 0;}
 if(m==WM_KEYDOWN&&w==VK_ESCAPE){
  int had_editor=edit_parameter>=0||edit_name;edit_parameter=-1;edit_name=0;close_menu();SetWindowTextW(h,L"");if(had_editor)line(TEXT_MUTED,L"Edit cancelled. Values kept.");layout();return 0;
 }
 if(m==WM_KEYDOWN&&w==VK_TAB&&!edit_name&&edit_parameter<0){complete_input();return 0;}
 if(m==WM_KEYDOWN&&(w==VK_LEFT||w==VK_RIGHT)&&edit_parameter>=0){
  wchar_t s[64],*end;GetWindowTextW(h,s,64);double v=wcstod(s,&end);if(end!=s&&!*end&&isfinite(v)){
   v=fmax(lower(edit_parameter),fmin(upper(edit_parameter),v+(w==VK_LEFT?-1:1)*parameters[edit_parameter].step));swprintf(s,64,L"%.5g",v);SetWindowTextW(h,s);SendMessageW(h,EM_SETSEL,0,-1);
  }return 0;
 }
 if(m==WM_KEYDOWN&&(w==VK_UP||w==VK_DOWN)&&edit_parameter<0&&!edit_name){
  if(menu_kind&&GetWindowTextLengthW(h)==0){menu_selected+=(w==VK_UP?-1:1);if(menu_selected<0)menu_selected=menu_count-1;if(menu_selected>=menu_count)menu_selected=0;scroll_rows=0;redraw();}
  else {history_cursor+=(w==VK_UP?-1:1);if(history_cursor<0)history_cursor=0;if(history_cursor>history_count)history_cursor=history_count;SetWindowTextW(h,history_cursor<history_count?history[history_cursor]:L"");SendMessageW(h,EM_SETSEL,256,256);}return 0;
 }
 if(m==WM_KEYDOWN&&(GetKeyState(VK_CONTROL)&0x8000)&&w=='S'){begin_name();return 0;}
 if(m==WM_KEYDOWN&&(GetKeyState(VK_CONTROL)&0x8000)&&w=='L'){edit_parameter=-1;edit_name=0;SetWindowTextW(h,L"");close_menu();line_count=0;scroll_rows=0;redraw();return 0;}
 return CallWindowProcW(original_edit,h,m,w,l);
}
static void draw_transcript(HDC dc){
 typedef struct {wchar_t s[512];int source,level;} Visual;
 Visual *visual=HeapAlloc(GetProcessHeap(),0,sizeof(*visual)*MAX_LINES*8);if(!visual)return;
 SelectObject(dc,font_mono);SIZE cell;GetTextExtentPoint32W(dc,L"M",1,&cell);int cw=MulDiv(cell.cx,96,dpi);if(cw<1)cw=8;
 int columns=(width-92)/cw;if(columns<12)columns=12;int total=0,selected_row=-1;
 for(int i=0;i<line_count&&total<MAX_LINES*8;i++){
  if(lines[i].level==TEXT_MENU&&*lines[i].label){
   if(i==menu_start+menu_selected&&menu_kind)selected_row=total;
   visual[total].s[0]=0;visual[total].source=i;visual[total].level=TEXT_MENU;total++;continue;
  }
  const wchar_t *p=lines[i].text;do{
   int n=(int)wcslen(p);if(n>columns)n=columns;if(n==columns&&p[n]){int space=n;while(space>columns/2&&p[space]!=L' ')space--;if(space>columns/2)n=space;}
   if(i==menu_start+menu_selected&&menu_kind&&selected_row<0)selected_row=total;
   wcsncpy(visual[total].s,p,(size_t)n);visual[total].s[n]=0;visual[total].source=i;visual[total].level=lines[i].level;total++;
   p+=n;while(*p==L' ')p++;
  }while(*p&&total<MAX_LINES*8);
 }
 int y=142,h=height-274,visible=h/ROW_HEIGHT;if(visible<1)visible=1;
 int max_scroll=total-visible;if(max_scroll<0)max_scroll=0;if(scroll_rows>max_scroll)scroll_rows=max_scroll;
 int start=total-visible-scroll_rows;if(start<0)start=0;
 if(menu_kind&&selected_row>=0&&!scroll_rows){if(selected_row<start)start=selected_row;else if(selected_row>=start+visible)start=selected_row-visible+1;}
 RECT clip=rect(40,y,width-80,h);int saved=SaveDC(dc);IntersectClipRect(dc,clip.left,clip.top,clip.right,clip.bottom);if(canvas_active)helper_canvas_clip(40,(float)y,(float)(width-80),(float)h);menu_hit_count=0;
 for(int i=start;i<total&&i<start+visible;i++){
  Visual *v=&visual[i];int sy=y+(i-start)*ROW_HEIGHT;int item=menu_kind&&v->source>=menu_start&&v->source<menu_start+menu_count?v->source-menu_start:-1;
  int selected=item>=0&&item==menu_selected;
  COLORREF color=v->level==TEXT_OK?success:v->level==TEXT_ERROR?error:v->level==TEXT_COMMAND?orange:v->level==TEXT_HEADING?soft:v->level==TEXT_MUTED?muted:ink;
  int menu_w=width-80;if(menu_w>760)menu_w=760;
  if(selected){fill(dc,40,sy,menu_w,ROW_HEIGHT,selection);color=orange;if(canvas_active)helper_canvas_text(52,(float)sy,20,ROW_HEIGHT,L"›",font_mono,orange,0,1);else text(dc,52,sy+3,L">",font_mono,orange);}
  Line *source=&lines[v->source];
  if(v->level==TEXT_MENU&&*source->label){
   if(canvas_active){
    helper_canvas_text(84,(float)sy,250,ROW_HEIGHT,source->label,font_mono,color,0,1);
    helper_canvas_text(350,(float)sy,(float)(menu_w-326),ROW_HEIGHT,source->detail,font_mono,selected?orange:muted,1,1);
   }else{
    SelectObject(dc,font_mono);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);
    RECT label_rect=rect(84,sy,250,ROW_HEIGHT);DrawTextW(dc,source->label,-1,&label_rect,DT_SINGLELINE|DT_END_ELLIPSIS|DT_VCENTER);
    SetTextColor(dc,selected?orange:muted);RECT value_rect=rect(350,sy,menu_w-326,ROW_HEIGHT);
    DrawTextW(dc,source->detail,-1,&value_rect,DT_SINGLELINE|DT_RIGHT|DT_END_ELLIPSIS|DT_VCENTER);
   }
  }else if(canvas_active)helper_canvas_text(40,(float)sy,(float)(width-80),ROW_HEIGHT,v->s,font_mono,color,0,1);
  else text(dc,40,sy+3,v->s,font_mono,color);
  if(item>=0&&menu_hit_count<MAX_MENU){menu_hits[menu_hit_count]=(RECT){40,sy,40+menu_w,sy+ROW_HEIGHT};menu_hit_items[menu_hit_count++]=item;}
 }
 if(canvas_active)helper_canvas_unclip();RestoreDC(dc,saved);HeapFree(GetProcessHeap(),0,visual);
 if(scroll_rows)text(dc,width-185,125,L"scroll / history",font_meta,muted);
}
static void paint(HDC target){
 RECT client;GetClientRect(window,&client);HDC dc=CreateCompatibleDC(target);HBITMAP bitmap=CreateCompatibleBitmap(target,client.right,client.bottom);HGDIOBJ old=SelectObject(dc,bitmap);
 canvas_active=helper_canvas_begin(dc,client.right,client.bottom,dpi);
 fill(dc,0,0,width,height,bg);fill(dc,0,0,width,52,surface);text(dc,24,9,L"Z",font_logo,orange);
 text(dc,width-370,14,L"RAIN EDITION / 0.9.3 TEST",font_meta,soft);
 if(canvas_active){
  helper_canvas_line((float)(width-115),26,(float)(width-105),26,muted);
  helper_canvas_line((float)(width-71),21,(float)(width-61),21,muted);helper_canvas_line((float)(width-71),31,(float)(width-61),31,muted);
  helper_canvas_line((float)(width-71),21,(float)(width-71),31,muted);helper_canvas_line((float)(width-61),21,(float)(width-61),31,muted);
  helper_canvas_line((float)(width-27),21,(float)(width-17),31,muted);helper_canvas_line((float)(width-17),21,(float)(width-27),31,muted);
 }else {text(dc,width-118,15,L"_",font_mono,muted);text(dc,width-77,15,L"□",font_mono,muted);text(dc,width-34,15,L"×",font_mono,muted);}
 text(dc,40,83,L"FRONTIER HELPER",font_title,ink);fill(dc,40,124,width-80,1,border);draw_transcript(dc);
 fill(dc,40,height-116,width-80,1,border);
 text(dc,40,height-65,edit_parameter>=0?L"Left / Right adjust   Enter apply   Esc cancel":edit_name?L"Enter save   Esc cancel":menu_kind?L"Up / Down select   Enter open / edit   Esc close":L"Enter run   Up / Down history   Tab complete   help for commands",font_meta,muted);
 fill(dc,0,height-36,width,36,surface);text(dc,40,height-31,scene_name(),font_meta,orange);
 text(dc,245,height-31,connected?L"camera channel connected":L"waiting for start",font_meta,muted);text(dc,width-150,height-31,L"LOCAL SESSION",font_meta,soft);
 if(canvas_active)last_render_ok=helper_canvas_end();else last_render_ok=0;canvas_active=0;
 draw_input_prompt(dc);
 BitBlt(target,0,0,client.right,client.bottom,dc,0,0,SRCCOPY);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
static void layout(void){
 RECT r;GetClientRect(window,&r);width=MulDiv(r.right,96,dpi);height=MulDiv(r.bottom,96,dpi);
 wchar_t prompt[80];input_prompt(prompt);HDC dc=GetDC(command_edit);HGDIOBJ old=SelectObject(dc,font_mono);
 SIZE prompt_size,space;TEXTMETRICW metrics;GetTextExtentPoint32W(dc,prompt,(int)wcslen(prompt),&prompt_size);GetTextExtentPoint32W(dc,L" ",1,&space);GetTextMetricsW(dc,&metrics);
 SelectObject(dc,old);ReleaseDC(command_edit,dc);
 int input_height=metrics.tmHeight+px(2);
 int input_x=px(40)+prompt_size.cx+space.cx,input_y=px(height-100)+(px(26)-input_height)/2;
 SendMessageW(command_edit,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,0);
 MoveWindow(command_edit,input_x,input_y,r.right-input_x-px(40),input_height,TRUE);redraw();
}
static LRESULT CALLBACK window_proc(HWND h,UINT message,WPARAM w,LPARAM l){
 switch(message){
 case WM_NCCALCSIZE:return 0;
 case WM_NCPAINT:return 0;
 case WM_NCACTIVATE:return TRUE;
 case WM_DWMCOMPOSITIONCHANGED:configure_frame(h);return 0;
 case WM_NCHITTEST:{POINT p={GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&p);int x=MulDiv(p.x,96,dpi),y=MulDiv(p.y,96,dpi);
  if(!IsZoomed(h)){if(x<6&&y<6)return HTTOPLEFT;if(x>width-6&&y<6)return HTTOPRIGHT;if(x<6&&y>height-6)return HTBOTTOMLEFT;if(x>width-6&&y>height-6)return HTBOTTOMRIGHT;if(x<6)return HTLEFT;if(x>width-6)return HTRIGHT;if(y<6)return HTTOP;if(y>height-6)return HTBOTTOM;}
  if(y<52&&x<width-135)return HTCAPTION;return HTCLIENT;}
 case WM_GETMINMAXINFO:{MINMAXINFO *info=(MINMAXINFO*)l;info->ptMinTrackSize=(POINT){px(860),px(620)};
  MONITORINFO monitor={0};monitor.cbSize=sizeof(monitor);if(GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&monitor)){
   info->ptMaxPosition=(POINT){monitor.rcWork.left-monitor.rcMonitor.left,monitor.rcWork.top-monitor.rcMonitor.top};
   info->ptMaxSize=(POINT){monitor.rcWork.right-monitor.rcWork.left,monitor.rcWork.bottom-monitor.rcWork.top};
  }return 0;}
 case WM_DPICHANGED:{dpi=HIWORD(w);create_fonts();if(command_edit)SendMessageW(command_edit,WM_SETFONT,(WPARAM)font_mono,TRUE);
  RECT *r=(RECT*)l;SetWindowPos(h,NULL,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);if(command_edit)layout();return 0;}
 case WM_SIZE:if(command_edit)layout();return 0;
 case WM_ERASEBKGND:return 1;
 case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);paint(dc);EndPaint(h,&ps);return 0;}
 case WM_CTLCOLOREDIT:SetTextColor((HDC)w,ink);SetBkColor((HDC)w,bg);return (LRESULT)edit_brush;
 case WM_TIMER:
 case STUDIO_UPDATE:
  if(TryAcquireSRWLockExclusive(&backend.lock)){status=backend.status;connected=backend.connected;working=backend.working;ReleaseSRWLockExclusive(&backend.lock);}
  if(status.frames!=observed_frames){observed_frames=status.frames;last_frames_tick=GetTickCount64();}if(connected||working||closing||message==STUDIO_UPDATE)redraw();return 0;
 case STUDIO_LOG:{StudioLog *entry=(StudioLog*)l;
  /* Routine frame telemetry stays in the journal. Attach steps and errors appear inline. */
  if(wcsstr(entry->text,L"[attach")||wcsstr(entry->text,L"[error")||entry->level==2||wcsstr(entry->text,L"[stop")||wcsstr(entry->text,L"[disconnect"))line(entry->level==2?TEXT_ERROR:TEXT_MUTED,L"%ls",entry->text);
  HeapFree(GetProcessHeap(),0,entry);return 0;}
 case STUDIO_CLOSED:DestroyWindow(h);return 0;
 case WM_LBUTTONDOWN:{int x=MulDiv(GET_X_LPARAM(l),96,dpi),y=MulDiv(GET_Y_LPARAM(l),96,dpi);
  if(y<52&&x>width-135){if(x>width-48)PostMessageW(h,WM_CLOSE,0,0);else if(x>width-90)ShowWindow(h,IsZoomed(h)?SW_RESTORE:SW_MAXIMIZE);else ShowWindow(h,SW_MINIMIZE);return 0;}
  if(edit_parameter<0&&!edit_name)for(int i=0;i<menu_hit_count;i++)if(PtInRect(&menu_hits[i],(POINT){x,y})){menu_selected=menu_hit_items[i];activate_item();break;}
  layout();SetFocus(command_edit);return 0;}
 case WM_MOUSEWHEEL:scroll_rows+=GET_WHEEL_DELTA_WPARAM(w)>0?3:-3;if(scroll_rows<0)scroll_rows=0;redraw();return 0;
 case WM_KEYDOWN:SetFocus(command_edit);SendMessageW(command_edit,message,w,l);return 0;
 case WM_CLOSE:if(self_test_mode){DestroyWindow(h);return 0;}if(!closing){closing=1;EnableWindow(command_edit,FALSE);studio_action(&backend,ACTION_QUIT,0);redraw();}return 0;
 case WM_DESTROY:KillTimer(h,1);PostQuitMessage(0);return 0;
 }return DefWindowProcW(h,message,w,l);
}
static HFONT make_font(int size,int weight){return CreateFontW(-px(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,FIXED_PITCH,L"Consolas");}
static void create_fonts(void){
 helper_canvas_release();if(font_mono)DeleteObject(font_mono);if(font_meta)DeleteObject(font_meta);if(font_title)DeleteObject(font_title);if(font_logo)DeleteObject(font_logo);
 font_mono=make_font(14,FW_NORMAL);font_meta=make_font(12,FW_NORMAL);font_title=make_font(22,FW_BOLD);font_logo=make_font(30,FW_HEAVY);
}
/* Render our own scene into an image for layout QA; no desktop/window capture. */
static int render_preview(const wchar_t *name){
 layout();RECT r;GetClientRect(window,&r);
 BITMAPINFO info={0};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=r.right;info.bmiHeader.biHeight=-r.bottom;
 info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
 HDC dc=CreateCompatibleDC(NULL);void *pixels=NULL;HBITMAP bmp=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,NULL,0);
 if(!bmp||!pixels){DeleteDC(dc);return 0;}HGDIOBJ old=SelectObject(dc,bmp);paint(dc);
 /* Preview the editor text with its actual font, character origin and clip box.
    This is layout QA of our own scene, not a capture of a live window. */
 RECT input,format;TEXTMETRICW metrics;input_geometry(&input,&format,&metrics);
 wchar_t input_text[256];GetWindowTextW(command_edit,input_text,256);LRESULT origin=SendMessageW(command_edit,EM_POSFROMCHAR,0,0);
 int saved=SaveDC(dc);IntersectClipRect(dc,input.left,input.top,input.right,input.bottom);SelectObject(dc,font_mono);SetTextColor(dc,ink);SetBkMode(dc,TRANSPARENT);
 if(*input_text&&origin!=-1)TextOutW(dc,input.left+(SHORT)LOWORD(origin),input.top+(SHORT)HIWORD(origin),input_text,(int)wcslen(input_text));RestoreDC(dc,saved);
 int input_drawn=GetWindowTextLengthW(command_edit)==0;
 for(int y=input.top;y<input.bottom&&!input_drawn;y++)for(int x=input.left;x<input.right;x++){
  DWORD color=((DWORD*)pixels)[y*r.right+x]&0xffffff;
  if(color!=0x0b0b0b){input_drawn=1;break;}
 }
 BITMAPFILEHEADER file={0};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info.bmiHeader);file.bfSize=file.bfOffBits+(DWORD)(r.right*r.bottom*4);
 wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,L"%ls%ls",app_folder,name);FILE *out=_wfopen(path,L"wb");int ok=0;
 if(out){ok=fwrite(&file,1,sizeof(file),out)==sizeof(file)&&fwrite(&info.bmiHeader,1,sizeof(info.bmiHeader),out)==sizeof(info.bmiHeader)&&fwrite(pixels,1,(size_t)(r.right*r.bottom*4),out)==(size_t)(r.right*r.bottom*4);fclose(out);}
 SelectObject(dc,old);DeleteObject(bmp);DeleteDC(dc);return ok&&last_render_ok&&input_drawn;
}
static int run_self_test(void){
 int checks=0,failures=0;
 #define CHECK(c) do{checks++;if(!(c))failures++;}while(0)
 CHECK(menu_kind==MENU_NONE&&menu_count==0&&line_count==0&&edit_parameter<0&&!edit_name);
 CHECK(GetWindowTextLengthW(command_edit)==0);
 SendMessageW(command_edit,WM_KEYDOWN,VK_RETURN,0);CHECK(menu_kind==MENU_NONE&&line_count==0);
 CHECK(render_preview(L"preview-ready.bmp"));
 SetWindowTextW(command_edit,L"helper >");CHECK(render_preview(L"preview-input.bmp"));
 RECT input_bounds,input_format;TEXTMETRICW input_metrics;input_geometry(&input_bounds,&input_format,&input_metrics);
 CHECK(input_bounds.bottom-input_bounds.top>=input_metrics.tmHeight);
 CHECK(input_format.left==0);
 CHECK(input_format.bottom-input_format.top>=input_metrics.tmHeight);
 CHECK((HFONT)SendMessageW(command_edit,WM_GETFONT,0,0)==font_mono);
 SetWindowTextW(command_edit,L"");
 SetWindowTextW(command_edit,L"camera");CHECK(menu_kind==MENU_NONE&&line_count==0);
 SendMessageW(command_edit,WM_KEYDOWN,VK_RETURN,0);CHECK(menu_kind==MENU_CAMERA&&menu_count==14);
 SendMessageW(command_edit,WM_KEYDOWN,VK_ESCAPE,0);CHECK(menu_kind==MENU_NONE&&GetWindowTextLengthW(command_edit)==0);
 command(L"clear");
 command(L"help");CHECK(menu_kind==MENU_HELP&&menu_count==7);
 command(L"camera");CHECK(menu_kind==MENU_CAMERA&&menu_count==14);
 menu_selected=0;activate_item();CHECK(edit_parameter==0);
 SetWindowTextW(command_edit,L"1.20");submit_input();CHECK(fabsf(value(0)-1.2f)<.0001f&&edit_parameter<0&&menu_kind==MENU_CAMERA);
 menu_selected=0;activate_item();SetWindowTextW(command_edit,L"9");submit_input();CHECK(edit_parameter==0&&fabsf(value(0)-1.2f)<.0001f);
 SendMessageW(command_edit,WM_KEYDOWN,VK_ESCAPE,0);CHECK(edit_parameter<0&&fabsf(value(0)-1.2f)<.0001f);
 command(L"set height 25");CHECK(value(1)==25);command(L"set height 999");CHECK(value(1)==25);
 command(L"set distance nan");CHECK(fabsf(value(0)-1.2f)<.0001f);
 command(L"set resetpitch -15");command(L"set min -10");CHECK(value(9)==-10);
 command(L"preview off");CHECK(!settings.preview);command(L"pad 4");CHECK(settings.gamepad_index==3);
 command(L"save Self test");CHECK(setup_count==1);command(L"defaults");CHECK(fabsf(value(0)-1)<.0001f);
 command(L"load Self test");CHECK(fabsf(value(0)-1.2f)<.0001f&&value(1)==25);
 command(L"setups");CHECK(menu_count==1&&menu_kind==MENU_SETUPS);
 command(L"files");CHECK(menu_count==7&&menu_kind==MENU_FILES);
 command(L"clear");CHECK(line_count==0&&menu_kind==MENU_NONE);
 SetWindowTextW(command_edit,L"cam");complete_input();wchar_t s[64];GetWindowTextW(command_edit,s,64);CHECK(!wcscmp(s,L"camera"));
 for(int i=0;i<550;i++)line(TEXT_NORMAL,L"history %d",i);command(L"camera");CHECK(line_count==MAX_LINES&&menu_start>=0&&menu_start+menu_count<=line_count);
 RECT client,outer;GetClientRect(window,&client);GetWindowRect(window,&outer);
 CHECK(client.right==outer.right-outer.left);CHECK(client.bottom==outer.bottom-outer.top);
 BOOL nonclient_enabled=TRUE;
 HRESULT policy_result=DwmGetWindowAttribute(window,DWMWA_NCRENDERING_ENABLED,&nonclient_enabled,sizeof(nonclient_enabled));
 CHECK(SUCCEEDED(frame_config_result)&&SUCCEEDED(policy_result)&&!nonclient_enabled);
 command(L"defaults");command(L"clear");command(L"camera");
 CHECK(render_preview(L"preview.bmp"));CHECK(last_render_ok);
 wchar_t output[MAX_PATH];swprintf(output,MAX_PATH,L"%lsverification.json",app_folder);FILE *report=_wfopen(output,L"wb");
 if(report){fprintf(report,"{\"checks\":%d,\"failures\":%d,\"directwrite\":%s,\"nonclient_policy_disabled\":%s,\"frame_config_result\":%ld,\"frame_query_result\":%ld,\"startup_menu\":false,\"dpi\":%d,\"game_attachment_tested\":false}\n",checks,failures,last_render_ok?"true":"false",SUCCEEDED(frame_config_result)&&SUCCEEDED(policy_result)&&!nonclient_enabled?"true":"false",(long)frame_config_result,(long)policy_result,dpi);fclose(report);}else failures++;
 #undef CHECK
 return failures?1:0;
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,char *args,int show){
 (void)previous;self_test_mode=strstr(args,"--self-test")!=NULL;
 typedef BOOL (WINAPI *SetDpiContext)(HANDLE);SetDpiContext set_context=(SetDpiContext)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetProcessDpiAwarenessContext");
 if(!set_context||!set_context((HANDLE)(intptr_t)-4))SetProcessDPIAware();
 typedef UINT (WINAPI *GetSystemDpi)(void);GetSystemDpi get_system_dpi=(GetSystemDpi)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForSystem");
 if(get_system_dpi)dpi=(int)get_system_dpi();else {HDC screen=GetDC(NULL);dpi=GetDeviceCaps(screen,LOGPIXELSX);ReleaseDC(NULL,screen);}
 const char *test_dpi=strstr(args,"--dpi=");if(self_test_mode&&test_dpi){int n=atoi(test_dpi+6);if(n>=96&&n<=192)dpi=n;}
 create_fonts();edit_brush=CreateSolidBrush(bg);settings=camera_defaults();
 GetModuleFileNameW(NULL,app_folder,MAX_PATH);wchar_t *slash=wcsrchr(app_folder,L'\\');if(!slash)return 1;slash[1]=0;
 if(wcslen(app_folder)>MAX_PATH-55)return 1;
 if(self_test_mode){swprintf(folder,MAX_PATH,L"%lstest-artifacts",app_folder);CreateDirectoryW(folder,NULL);wcscat(folder,L"\\setups");CreateDirectoryW(folder,NULL);}else if(!setup_folder(folder))return 1;
 WNDCLASSEXW cls={0};cls.cbSize=sizeof(cls);cls.hInstance=instance;cls.lpfnWndProc=window_proc;cls.hCursor=LoadCursorW(NULL,(LPCWSTR)IDC_ARROW);cls.lpszClassName=L"FrontierHelperWindow";
 cls.hIcon=LoadIconW(NULL,(LPCWSTR)IDI_APPLICATION);if(!RegisterClassExW(&cls))return 1;
 int w=px(1120),h=px(800);if(w>GetSystemMetrics(SM_CXSCREEN)-40)w=GetSystemMetrics(SM_CXSCREEN)-40;if(h>GetSystemMetrics(SM_CYSCREEN)-70)h=GetSystemMetrics(SM_CYSCREEN)-70;
 window=CreateWindowExW(WS_EX_APPWINDOW,cls.lpszClassName,L"Frontier Helper — Rain 0.9.3 Test",WS_POPUP|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_SYSMENU,(GetSystemMetrics(SM_CXSCREEN)-w)/2,(GetSystemMetrics(SM_CYSCREEN)-h)/2,w,h,NULL,NULL,instance,NULL);if(!window)return 1;
 configure_frame(window);
 command_edit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,0,0,window,(HMENU)ID_COMMAND,instance,NULL);if(!command_edit)return 1;
 SetWindowTextW(command_edit,L"");SendMessageW(command_edit,WM_SETFONT,(WPARAM)font_mono,0);SendMessageW(command_edit,EM_SETLIMITTEXT,240,0);original_edit=(WNDPROC)SetWindowLongPtrW(command_edit,GWLP_WNDPROC,(LONG_PTR)edit_proc);
 if(self_test_mode){int result=run_self_test();DestroyWindow(window);return result;}
 refresh_setups();wchar_t core[MAX_PATH];swprintf(core,MAX_PATH,L"%lsFrontierOrbitRain08.dll",app_folder);if(!studio_backend_start(&backend,window,core))return 1;
 layout();SetTimer(window,1,100,NULL);ShowWindow(window,show==SW_HIDE?SW_SHOWNORMAL:show);UpdateWindow(window);SetFocus(command_edit);
 MSG message;while(GetMessageW(&message,NULL,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
 if(backend.thread){WaitForSingleObject(backend.thread,1000);CloseHandle(backend.thread);}if(setups)HeapFree(GetProcessHeap(),0,setups);
 helper_canvas_release();DeleteObject(font_mono);DeleteObject(font_meta);DeleteObject(font_title);DeleteObject(font_logo);DeleteObject(edit_brush);return 0;
}
