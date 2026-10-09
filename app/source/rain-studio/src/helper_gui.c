#include "studio_backend.h"
#include "profile_store.h"
#include "helper_renderer.h"
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <wchar.h>
#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <math.h>

/* Compact is the production view. Logical coordinates are always 96 DPI. */
enum {PAGE_CAMERA,PAGE_PROFILES,PAGE_FILES};
enum {MINIMIZE=10,MAXIMIZE,CLOSE,NAV_CAMERA=100,NAV_PROFILES,NAV_FILES,
 PROFILE=110,ADVANCED=120,LIVE,DEFAULTS,SAVE,START,IMPORT,EXPORT,FOLDER,CLIENT,
 JOURNAL,EXPORT_LOG,AUTO_CLIENT,GAMEPAD,RECENTER,SCROLLBAR=140,
 SLIDER=200,NUMBER=220,LOAD=300,MODAL_CANCEL=900,MODAL_OK};
enum {MODAL_NONE,MODAL_SAVE,MODAL_DISCARD};
typedef struct {int x,y,w,h;} Box;
typedef struct {int id;Box box;int enabled,content;} Hit;
typedef struct {const wchar_t *label,*unit;size_t offset;float lo,hi,step;int decimals;} Parameter;
static const Parameter parameters[]={
 {L"Distance",L"\x00d7",offsetof(CameraSettings,distance_scale),.5f,1.5f,.01f,2},
 {L"Height",L"u",offsetof(CameraSettings,height_offset),-250,250,1,0},
 {L"Shoulder offset",L"u",offsetof(CameraSettings,shoulder_offset),-300,300,1,0},
 {L"Smoothness",L"",offsetof(CameraSettings,response),1,100,.5f,1},
 {L"Yaw speed",L"\x00b0/s",offsetof(CameraSettings,yaw_speed),20,720,1,0},
 {L"Pitch speed",L"\x00b0/s",offsetof(CameraSettings,pitch_speed),10,360,1,0},
 {L"Deadzone",L"",offsetof(CameraSettings,deadzone),.02f,.5f,.01f,2},
 {L"Up limit",L"\x00b0",offsetof(CameraSettings,min_pitch),-85,0,1,0},
 {L"Down limit",L"\x00b0",offsetof(CameraSettings,max_pitch),0,85,1,0},
 {L"Reset tilt",L"\x00b0",offsetof(CameraSettings,reset_pitch),-85,85,1,0}
};
static const COLORREF bg=RGB(14,16,19),panel=RGB(22,25,30),high=RGB(32,37,45),
 border=RGB(44,50,59),ink=RGB(243,243,245),muted=RGB(165,173,186),
 accent=RGB(255,139,44),active=RGB(51,37,27),danger=RGB(240,92,70);
static HWND window,editor;
static WNDPROC editor_original;
static StudioBackend backend;
static CameraSettings settings,baseline;
static ControlReply status;
static HFONT font_body,font_small,font_heading,font_bold,font_number,font_brand,font_logo;
static HBRUSH edit_brush;
static HICON app_icon;
static int dpi=96,width=1040,height=780,page,advanced,scroll_y,scroll_max;
static int connected,working,pending,closing,self_test,dirty,imported,hover=-1,focus_id=-1,keyboard_focus;
static int edit_index=-1,edit_busy,drag_index=-1,drag_scroll,scroll_anchor,scroll_origin,popup,popup_choice,popup_scroll,modal,pending_load;
static int hit_count,last_render_ok,notice_error;
static Hit hits[600];
static Box number_boxes[10],slider_boxes[10],viewport,profile_box,dialog_box,scroll_thumb;
static SavedSetup *setups;static int setup_count;
static wchar_t app_folder[MAX_PATH],folder[MAX_PATH],client_path[MAX_PATH],current_path[MAX_PATH];
static wchar_t current_name[128]=L"Default",notice_text[512],modal_error[512];
static ULONGLONG last_frames_tick,notice_until;static uint64_t observed_frames;
static HRESULT frame_result=E_FAIL;
static void redraw(void){if(window)InvalidateRect(window,NULL,FALSE);}
static int px(int n){return MulDiv(n,dpi,96);}
static Box box(int x,int y,int w,int h){Box b={x,y,w,h};return b;}
static int inside(Box b,int x,int y){return x>=b.x&&x<b.x+b.w&&y>=b.y&&y<b.y+b.h;}
static float value(int i){float v;memcpy(&v,(BYTE*)&settings+parameters[i].offset,sizeof(v));return v;}
static float lower(int i){return i==9?settings.min_pitch:parameters[i].lo;}
static float upper(int i){return i==9?settings.max_pitch:parameters[i].hi;}
static void paint(HDC dc);
static void update_editor(void);
static void activate(int id);
static void navigate_focus(int backwards);
static void notify(int error,const wchar_t *format,...){
 va_list args;va_start(args,format);vswprintf(notice_text,512,format,args);va_end(args);
 notice_error=error;notice_until=GetTickCount64()+(error?14000:6000);redraw();
 if(error&&modal==MODAL_SAVE)wcscpy(modal_error,notice_text);
}
static void changed(int flush){
 dirty=imported||memcmp(&settings,&baseline,sizeof(settings))!=0;
 if(!self_test&&!closing){studio_settings(&backend,&settings);if(flush)studio_flush(&backend);}
 redraw();
}
static int change(int i,float v,int flush){
 if(i<0||i>=10||!isfinite(v))return 0;
 v=fmaxf(lower(i),fminf(upper(i),v));
 memcpy((BYTE*)&settings+parameters[i].offset,&v,sizeof(v));
 settings.reset_pitch=fmaxf(settings.min_pitch,fminf(settings.max_pitch,settings.reset_pitch));
 changed(flush);return 1;
}
static int parse_value(int i,const wchar_t *input,float *out){
 wchar_t s[96];wcsncpy(s,input,95);s[95]=0;
 for(wchar_t *p=s;*p;p++)if(*p==L',')*p=L'.';
 wchar_t *end;double n=wcstod(s,&end);if(end==s)return 0;while(*end==L' ')end++;
 if(*end||!isfinite(n)||n<lower(i)-.000001||n>upper(i)+.000001)return 0;
 *out=(float)n;return 1;
}
static void format_value(int i,wchar_t *s,int capacity){swprintf(s,capacity,L"%.*f",parameters[i].decimals,value(i));}
static const wchar_t *scene_name(void){
 if(closing)return L"Closing";
 if(pending||working)return pending==ACTION_STOP?L"Stopping...":L"Connecting...";
 if(!connected)return L"Disconnected";
 if(!status.enabled)return L"Camera stopped";
 if(status.scene==CAMERA_ACTIVE&&(status.applied_revision!=status.accepted_revision||GetTickCount64()-last_frames_tick>1200))return L"Waiting for frame";
 const wchar_t *names[]={L"Camera stopped",L"Waiting for gameplay",L"Waiting for focus",L"Settling",L"Waiting for camera",L"Geometry fallback",L"Camera active"};
 return status.scene<7?names[status.scene]:L"Connected";
}
static void refresh_setups(void){
 int count=setup_list(folder,NULL,0);SavedSetup *next=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(size_t)(count+1)*sizeof(SavedSetup));
 if(!next){notify(1,L"Cannot read the profiles folder.");return;}
 int got=setup_list(folder,next,count+1);if(setups)HeapFree(GetProcessHeap(),0,setups);setups=next;setup_count=got;
}
static int selected_profile(void){for(int i=0;i<setup_count;i++)if(*current_path&&!_wcsicmp(current_path,setups[i].path))return i+1;return 0;}
static void load_profile(int index){
 if(index<0||index>setup_count)return;
 CameraSettings next=camera_defaults();wchar_t name[128]=L"Default",path[MAX_PATH]=L"";
 if(index){StudioProfile p;if(!setup_read(setups[index-1].path,&p)){notify(1,L"This profile could not be read. Current values were kept.");refresh_setups();return;}
  next=p.camera;MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,p.name,-1,name,128);wcscpy(path,setups[index-1].path);}
 settings=next;baseline=next;imported=0;wcscpy(current_name,name);wcscpy(current_path,path);changed(1);
 notify(0,L"Loaded %ls",current_name);
}
static void request_load(int index){
 popup=0;if(dirty){modal=MODAL_DISCARD;pending_load=index;focus_id=MODAL_CANCEL;keyboard_focus=1;redraw();}
 else load_profile(index);
}
static int save_profile(const wchar_t *input){
 wchar_t name[128];wcsncpy(name,input,127);name[127]=0;size_t len=wcslen(name);
 while(len&&name[len-1]==L' ')name[--len]=0;wchar_t *begin=name;while(*begin==L' ')begin++;
 if(!*begin){notify(1,L"Enter a profile name.");return 0;}
 for(const wchar_t *p=begin;*p;p++)if(*p<32){notify(1,L"Use a name without control characters.");return 0;}
 StudioProfile p={0};p.camera=settings;
 if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,begin,-1,p.name,sizeof(p.name),NULL,NULL)){notify(1,L"Profile name is too long (maximum 127 UTF-8 bytes).");return 0;}
 refresh_setups();wchar_t path[MAX_PATH]=L"";
 for(int i=0;i<setup_count;i++)if(!_wcsicmp(begin,setups[i].name)){wcscpy(path,setups[i].path);break;}
 if(!*path){FILETIME ft;GetSystemTimeAsFileTime(&ft);swprintf(path,MAX_PATH,L"%ls\\setup-%08lx%08lx.json",folder,ft.dwHighDateTime,ft.dwLowDateTime);}
 if(!setup_write(path,&p)){notify(1,L"Cannot save the profile. Check the profiles folder.");return 0;}
 wcscpy(current_name,begin);wcscpy(current_path,path);baseline=settings;dirty=0;imported=0;refresh_setups();notify(0,L"Saved %ls",current_name);return 1;
}
static int file_dialog(wchar_t path[MAX_PATH],int save,const wchar_t *filter,const wchar_t *extension){
 OPENFILENAMEW o={0};o.lStructSize=sizeof(o);o.hwndOwner=window;o.lpstrFile=path;o.nMaxFile=MAX_PATH;
 o.lpstrFilter=filter;o.lpstrDefExt=extension;o.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
 return save?GetSaveFileNameW(&o):GetOpenFileNameW(&o);
}
static int import_path(const wchar_t *path){
 StudioProfile p;if(!setup_read(path,&p)){notify(1,L"Invalid profile. Current camera values were kept.");return 0;}
 settings=p.camera;baseline=camera_defaults();imported=1;current_path[0]=0;MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,p.name,-1,current_name,128);
 changed(1);dirty=1;notify(0,L"Imported %ls. Save profile to keep a local copy.",current_name);return 1;
}
static void export_profile(void){
 wchar_t path[MAX_PATH]=L"camera-profile.json";if(!file_dialog(path,1,L"Camera profile\0*.json\0\0",L"json"))return;
 StudioProfile p={0};p.camera=settings;
 if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,current_name,-1,p.name,sizeof(p.name),NULL,NULL)){notify(1,L"Profile name is too long.");return;}
 int ok=setup_write(path,&p);notify(!ok,ok?L"Profile exported.":L"Cannot export the profile.");
}
static void open_path(const wchar_t *path){
 if(!*path){notify(1,L"No session log is available yet.");return;}
 if((INT_PTR)ShellExecuteW(window,L"open",path,NULL,NULL,SW_SHOWNORMAL)<=32)notify(1,L"Windows could not open this file or folder.");
}
static int finish_edit(int commit){
 if(edit_index<0||edit_busy)return 1;int i=edit_index;edit_index=-1;edit_busy=1;
 wchar_t s[96];GetWindowTextW(editor,s,96);float n;int ok=!commit||parse_value(i,s,&n);
 ShowWindow(editor,SW_HIDE);if(commit&&ok)change(i,n,1);
 if(!ok)notify(1,L"%ls: enter a value between %.3g and %.3g.",parameters[i].label,lower(i),upper(i));
 edit_busy=0;redraw();return ok;
}
static void begin_edit(int i){
 finish_edit(1);edit_index=i;LONG_PTR style=GetWindowLongPtrW(editor,GWL_STYLE);
 SetWindowLongPtrW(editor,GWL_STYLE,(style&~ES_CENTER)|ES_RIGHT);SendMessageW(editor,WM_SETFONT,(WPARAM)font_number,TRUE);
 wchar_t s[64];format_value(i,s,64);SetWindowTextW(editor,s);SendMessageW(editor,EM_SETLIMITTEXT,64,0);
 SetWindowTextW(editor,s);update_editor();ShowWindow(editor,SW_SHOW);SetFocus(editor);SendMessageW(editor,EM_SETSEL,0,-1);redraw();
}
static void begin_save(void){
 if(!finish_edit(1))return;popup=0;modal=MODAL_SAVE;modal_error[0]=0;focus_id=MODAL_OK;
 LONG_PTR style=GetWindowLongPtrW(editor,GWL_STYLE);SetWindowLongPtrW(editor,GWL_STYLE,style&~(ES_RIGHT|ES_CENTER));
 SendMessageW(editor,WM_SETFONT,(WPARAM)font_body,TRUE);SendMessageW(editor,EM_SETLIMITTEXT,127,0);
 SetWindowTextW(editor,!*current_path&&!wcscmp(current_name,L"Default")?L"My profile":current_name);
 update_editor();ShowWindow(editor,SW_SHOW);SetFocus(editor);SendMessageW(editor,EM_SETSEL,0,-1);redraw();
}
static void close_modal(void){modal=0;modal_error[0]=0;ShowWindow(editor,SW_HIDE);SetFocus(window);keyboard_focus=0;focus_id=SAVE;redraw();}
static void submit_modal(void){
 if(modal==MODAL_SAVE){wchar_t s[128];GetWindowTextW(editor,s,128);if(save_profile(s))close_modal();}
 else if(modal==MODAL_DISCARD){int index=pending_load;close_modal();load_profile(index);}
}
static void start_stop(void){
 if(pending||working||closing)return;
 pending=connected&&status.enabled?ACTION_STOP:ACTION_START;notice_text[0]=0;
 if(!self_test){studio_settings(&backend,&settings);studio_flush(&backend);studio_action(&backend,(enum StudioAction)pending,0);}
 redraw();
}
static void activate(int id){
 if(closing)return;
 if(id==MINIMIZE){ShowWindow(window,SW_MINIMIZE);return;}
 if(id==MAXIMIZE){ShowWindow(window,IsZoomed(window)?SW_RESTORE:SW_MAXIMIZE);return;}
 if(id==CLOSE){PostMessageW(window,WM_CLOSE,0,0);return;}
 if(modal){if(id==MODAL_CANCEL)close_modal();else if(id==MODAL_OK)submit_modal();return;}
 if(id>=NAV_CAMERA&&id<=NAV_FILES){finish_edit(1);popup=0;page=id-NAV_CAMERA;scroll_y=0;notice_text[0]=0;if(page==PAGE_PROFILES)refresh_setups();redraw();return;}
 if(id>=NUMBER&&id<NUMBER+10){begin_edit(id-NUMBER);return;}
 if(id>=LOAD&&id<LOAD+setup_count+1){request_load(id-LOAD);return;}
 if(id==PROFILE){finish_edit(1);refresh_setups();popup=!popup;popup_choice=selected_profile();popup_scroll=popup_choice>7?popup_choice-7:0;redraw();return;}
 if(id==ADVANCED){finish_edit(1);advanced=!advanced;scroll_y=0;redraw();return;}
 if(id==LIVE){settings.preview=!settings.preview;changed(1);return;}
 if(id==DEFAULTS){finish_edit(0);settings=camera_defaults();changed(1);notify(0,L"Default camera values restored");return;}
 if(id==SAVE){begin_save();return;}
 if(id==START){if(finish_edit(1))start_stop();return;}
 if(id==IMPORT){wchar_t path[MAX_PATH]=L"";if(file_dialog(path,0,L"Camera profile\0*.json\0\0",L"json"))import_path(path);return;}
 if(id==EXPORT){export_profile();return;}
 if(id==FOLDER){open_path(folder);return;}
 if(id==JOURNAL){open_path(backend.log_path);return;}
 if(id==EXPORT_LOG){wchar_t path[MAX_PATH]=L"helper-session.txt";if(!file_dialog(path,1,L"Text log\0*.txt\0\0",L"txt"))return;
  int ok=*backend.log_path&&CopyFileW(backend.log_path,path,FALSE);notify(!ok,ok?L"Session log exported":L"Cannot export the session log");return;}
 if(id==CLIENT){wchar_t path[MAX_PATH]=L"";if(file_dialog(path,0,L"Rain executable\0*.exe\0\0",L"exe")){
  wcscpy(client_path,path);if(!self_test)studio_select_exe(&backend,path);notify(0,L"Rain client selected");}return;}
 if(id==AUTO_CLIENT){client_path[0]=0;if(!self_test)studio_select_exe(&backend,L"");notify(0,L"Rain client: automatic detection");return;}
 if(id==GAMEPAD){settings.gamepad_index++;if(settings.gamepad_index>3)settings.gamepad_index=-1;changed(1);return;}
 if(id==RECENTER){if(connected&&!working&&!pending&&!self_test)studio_action(&backend,ACTION_RESET,0);return;}
}

static void fill(Box b,COLORREF c){helper_canvas_fill((float)b.x,(float)b.y,(float)b.w,(float)b.h,c);}
static void round_box(Box b,int radius,COLORREF c,COLORREF edge,int stroke){helper_canvas_round((float)b.x,(float)b.y,(float)b.w,(float)b.h,(float)radius,c,edge,(float)stroke);}
static void txt(Box b,const wchar_t *s,HFONT font,COLORREF c,int align){helper_canvas_text((float)b.x,(float)b.y,(float)b.w,(float)b.h,s,font,c,align,1);}
static void stroke(float x1,float y1,float x2,float y2,COLORREF c){helper_canvas_stroke(x1,y1,x2,y2,c,1.5f);}
static void circle(float x,float y,float r,COLORREF c){helper_canvas_circle(x,y,r,c);}
/* A small, consistent vector set. No raster scaling at fractional DPI. */
static void icon(int kind,int x,int y,COLORREF c){
 float a=(float)x,b=(float)y;
 if(kind==0){for(int i=0;i<3;i++){float yy=b+3+i*6;stroke(a,yy,a+18,yy,c);round_box(box(x+(i==1?4:10),y+i*6,4,6),1,panel,c,1);}}
 else if(kind==1){stroke(a,b+6,a+9,b+1,c);stroke(a+9,b+1,a+18,b+6,c);stroke(a+18,b+6,a+9,b+11,c);stroke(a+9,b+11,a,b+6,c);stroke(a,b+11,a+9,b+16,c);stroke(a+9,b+16,a+18,b+11,c);}
 else if(kind==2){stroke(a,b+4,a+6,b+4,c);stroke(a+6,b+4,a+8,b+7,c);stroke(a+8,b+7,a+18,b+7,c);stroke(a+18,b+7,a+18,b+17,c);stroke(a+18,b+17,a,b+17,c);stroke(a,b+17,a,b+4,c);}
 else if(kind==3){stroke(a+9,b+1,a+9,b+17,c);stroke(a+1,b+9,a+17,b+9,c);stroke(a+5,b+5,a+9,b+1,c);stroke(a+9,b+1,a+13,b+5,c);stroke(a+13,b+13,a+17,b+9,c);stroke(a+17,b+9,a+13,b+5,c);}
 else if(kind==4){stroke(a+2,b+3,a+14,b+3,c);stroke(a+14,b+3,a+18,b+7,c);stroke(a+18,b+7,a+18,b+14,c);stroke(a+18,b+14,a+6,b+14,c);stroke(a+6,b+14,a+2,b+10,c);stroke(a+2,b+10,a+2,b+3,c);circle(a+7,b+8,2,c);}
 else if(kind==5){stroke(a+3,b+1,a+15,b+1,c);stroke(a+15,b+1,a+15,b+17,c);stroke(a+15,b+17,a+9,b+13,c);stroke(a+9,b+13,a+3,b+17,c);stroke(a+3,b+17,a+3,b+1,c);}
 else if(kind==6){stroke(a+5,b+3,a+15,b+9,c);stroke(a+15,b+9,a+5,b+15,c);stroke(a+5,b+15,a+5,b+3,c);}
 else if(kind==7)round_box(box(x+4,y+4,11,11),1,c,c,0);
 else if(kind==8){stroke(a+4,b+6,a+9,b+11,c);stroke(a+9,b+11,a+14,b+6,c);}
 else if(kind==9){stroke(a+4,b+11,a+9,b+6,c);stroke(a+9,b+6,a+14,b+11,c);}
}
static int add_hit(int id,Box b,int enabled,int content){
 if(hit_count>=600)return -1;
 hits[hit_count]=(Hit){id,b,enabled,content};return hit_count++;
}
static void focus_ring(int id,Box b,int radius){
 if(keyboard_focus&&focus_id==id)helper_canvas_round((float)b.x-2,(float)b.y-2,(float)b.w+4,(float)b.h+4,(float)radius,RGB(0,0,0),accent,1);
}
static void button(int id,Box b,const wchar_t *label,int primary,int glyph,int enabled,int content){
 int hot=hover==id&&enabled;COLORREF c=primary?(enabled?(hot?RGB(255,154,67):accent):active):(hot?border:high);
 round_box(b,primary?9:7,c,keyboard_focus&&focus_id==id?accent:primary?c:border,1);
 COLORREF tc=enabled?(primary?bg:ink):muted;int shift=glyph>=0?20:0;
 if(glyph>=0)icon(glyph,b.x+12,b.y+(b.h-18)/2,tc);
 txt(box(b.x+8+shift,b.y,b.w-16-shift,b.h),label,font_small,tc,2);
 add_hit(id,b,enabled,content);
}
static void field(int i,int x,int y,int w){
 int unit=!*parameters[i].unit?0:!wcscmp(parameters[i].unit,L"\x00b0/s")?27:14;
 Box n=box(x+w-78,y,78,28);number_boxes[i]=n;
 round_box(n,6,bg,edit_index==i||(keyboard_focus&&focus_id==NUMBER+i)?accent:border,1);
 txt(box(x,y,w-90,28),parameters[i].label,font_small,muted,0);
 wchar_t s[64];format_value(i,s,64);
 if(edit_index!=i)txt(box(n.x+7,n.y,n.w-14-unit,n.h),s,font_number,ink,1);
 if(unit)txt(box(n.x+n.w-unit-5,n.y,unit,n.h),parameters[i].unit,font_brand,muted,0);
 add_hit(NUMBER+i,n,1,1);
 Box rail=box(x,y+41,w,24);slider_boxes[i]=rail;
 float t=upper(i)>lower(i)?(value(i)-lower(i))/(upper(i)-lower(i)):0;
 float thumb=x+6+t*(w-12);int cy=rail.y+12;
 round_box(box(x+6,cy-2,w-12,4),2,border,border,0);
 if(thumb>x+6)helper_canvas_round((float)x+6,(float)cy-2,thumb-x-6,4,2,accent,accent,0);
 if((keyboard_focus&&focus_id==SLIDER+i)||hover==SLIDER+i)circle(thumb,(float)cy,9,active);
 circle(thumb,(float)cy,6,accent);add_hit(SLIDER+i,rail,1,1);
}
static int page_length(void){
 if(page==PAGE_CAMERA)return advanced?596:408;
 if(page==PAGE_PROFILES)return 56+(setup_count+1)*76;
 return 484;
}
static void scene_geometry(void){
 RECT r;GetClientRect(window,&r);width=MulDiv(r.right,96,dpi);height=MulDiv(r.bottom,96,dpi);
 viewport=box(106,154,width-132,height-294);
 if(viewport.h<100)viewport.h=100;
 scroll_max=page_length()-viewport.h;if(scroll_max<0)scroll_max=0;
 if(scroll_y>scroll_max)scroll_y=scroll_max;if(scroll_y<0)scroll_y=0;
 profile_box=box(width-226,101,200,36);
 dialog_box=box((width-440)/2,(height-244)/2,440,244);
}
static void update_editor(void){
 if(!editor)return;scene_geometry();Box b;
 if(modal==MODAL_SAVE)b=box(dialog_box.x+36,dialog_box.y+106,dialog_box.w-72,18);
 else if(edit_index>=0){b=number_boxes[edit_index];int unit=!*parameters[edit_index].unit?0:!wcscmp(parameters[edit_index].unit,L"\x00b0/s")?27:14;b.x+=7;b.w-=14+unit;b.y+=5;b.h=18;}
 else return;
 MoveWindow(editor,px(b.x),px(b.y),px(b.w),px(b.h),TRUE);
 /* The input baseline uses the same Segoe UI / Consolas metric as the label. */
 SendMessageW(editor,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,0);InvalidateRect(editor,NULL,TRUE);
}
static void paint_camera(void){
 int x=viewport.x,y=viewport.y-scroll_y,w=viewport.w,col=(w-16)/2;
 round_box(box(x,y,col,294),12,panel,border,1);round_box(box(x+col+16,y,w-col-16,294),12,panel,border,1);
 icon(3,x+20,y+20,accent);txt(box(x+49,y+14,col-69,30),L"Position",font_bold,ink,0);
 icon(4,x+col+36,y+20,accent);txt(box(x+col+65,y+14,col-69,30),L"Movement",font_bold,ink,0);
 for(int i=0;i<3;i++){field(i,x+20,y+52+i*80,col-40);field(i+3,x+col+36,y+52+i*80,w-col-56);}
 int ay=y+310,ah=advanced?234:46;
 Box a=box(x,ay,w,ah);round_box(a,12,panel,border,1);
 txt(box(x+18,ay,w-60,46),L"Advanced settings",font_small,hover==ADVANCED?ink:muted,0);icon(advanced?9:8,x+w-36,ay+14,muted);
 if(keyboard_focus&&focus_id==ADVANCED)helper_canvas_round((float)x,(float)ay,(float)w,46,12,panel,accent,1);
 add_hit(ADVANCED,box(x,ay,w,46),1,1);
 if(advanced){int aw=(w-62)/2;field(6,x+18,ay+52,aw);field(7,x+44+aw,ay+52,aw);field(8,x+18,ay+132,aw);field(9,x+44+aw,ay+132,aw);}
 int ty=ay+ah+16;
 Box check=box(x,ty+10,16,16);round_box(check,4,settings.preview?accent:bg,keyboard_focus&&focus_id==LIVE?accent:border,1);
 if(settings.preview){stroke(x+4,ty+18,x+7,ty+21,bg);stroke(x+7,ty+21,x+12,ty+15,bg);}
 txt(box(x+24,ty,130,36),L"Live preview",font_small,muted,0);add_hit(LIVE,box(x,ty,160,36),1,1);
 button(DEFAULTS,box(x+w-234,ty,80,36),L"Reset",0,-1,1,1);
 button(SAVE,box(x+w-146,ty,146,36),L"Save profile",0,5,1,1);
}
static void paint_profiles(void){
 int x=viewport.x,y=viewport.y-scroll_y,w=viewport.w;
 button(IMPORT,box(x,y,92,36),L"Import",0,-1,1,1);button(EXPORT,box(x+100,y,92,36),L"Export",0,-1,1,1);
 button(SAVE,box(x+w-146,y,146,36),L"Save profile",0,5,1,1);
 y+=56;int current=selected_profile();
 for(int i=0;i<=setup_count;i++){
  int ry=y+i*76;if(ry+68<viewport.y||ry>viewport.y+viewport.h)continue;
  const wchar_t *name=i?setups[i-1].name:L"Default";CameraSettings s=i?setups[i-1].data.camera:camera_defaults();
  int selected=i==current&&!dirty;round_box(box(x,ry,w,68),10,selected?active:panel,selected?RGB(94,61,34):border,1);
  txt(box(x+18,ry+10,w-140,22),name,font_bold,ink,0);
  wchar_t details[96];swprintf(details,96,L"%.2f\x00d7  \x00b7  height %+.0f  \x00b7  shoulder %+.0f",s.distance_scale,s.height_offset,s.shoulder_offset);
  txt(box(x+18,ry+34,w-140,22),details,font_small,muted,0);
  button(LOAD+i,box(x+w-112,ry+16,94,36),selected?L"Selected":L"Load",0,-1,!selected,1);
 }
}
static void file_row(int y,const wchar_t *title,const wchar_t *detail,int id,const wchar_t *action){
 int x=viewport.x,w=viewport.w;round_box(box(x,y,w,78),10,panel,border,1);
 txt(box(x+18,y+13,w-160,24),title,font_bold,ink,0);txt(box(x+18,y+40,w-160,22),detail,font_small,muted,0);
 button(id,box(x+w-122,y+21,104,36),action,0,-1,!working&&!pending,1);
}
static void paint_files(void){
 int x=viewport.x,y=viewport.y-scroll_y,w=viewport.w;
 file_row(y,L"Rain client",*client_path?client_path:L"Automatic detection",CLIENT,L"Browse");
 if(*client_path)button(AUTO_CLIENT,box(x+w-218,y+21,88,36),L"Auto",0,-1,!working&&!pending,1);
 file_row(y+94,L"Camera profiles",folder,FOLDER,L"Open folder");
 file_row(y+188,L"Connection log",*backend.log_path?backend.log_path:L"Available after the application starts",JOURNAL,L"View");
 button(EXPORT_LOG,box(x+w-122,y+278,122,36),L"Export log",0,-1,*backend.log_path!=0,1);
 round_box(box(x,y+336,w,148),12,panel,border,1);
 txt(box(x+18,y+346,w-36,28),L"Session",font_bold,ink,0);
 txt(box(x+18,y+384,120,28),L"Controller",font_small,muted,0);
 wchar_t pad[32];swprintf(pad,32,settings.gamepad_index<0?L"Auto":L"Gamepad %d",settings.gamepad_index+1);
 button(GAMEPAD,box(x+144,y+384,132,32),pad,0,-1,1,1);
 button(RECENTER,box(x+w-148,y+384,130,32),L"Recenter",0,-1,connected&&status.enabled&&!pending&&!working,1);
 wchar_t telemetry[256];if(connected)swprintf(telemetry,256,L"PID %lu  \x00b7  %llu frames  \x00b7  controller %ls",status.game_pid,(unsigned long long)status.frames,status.pad_present?L"connected":L"not detected");
 else wcscpy(telemetry,L"Open Rain HD and enter the city before pressing Start.");
 txt(box(x+18,y+426,w-36,24),telemetry,font_small,muted,0);
 txt(box(x+18,y+454,w-36,24),L"F8 toggles camera  \x00b7  L3 recenters in game",font_small,muted,0);
}
static void paint_popup(void){
 int rows=setup_count+1;if(rows>8)rows=8;
 Box b=box(profile_box.x,profile_box.y+42,profile_box.w,rows*36+8);
 round_box(box(b.x+2,b.y+4,b.w,b.h),9,RGB(7,8,10),RGB(7,8,10),0);round_box(b,9,high,border,1);
 for(int n=0;n<rows;n++){int i=popup_scroll+n;if(i>setup_count)break;
  Box row=box(b.x+4,b.y+4+n*36,b.w-8,36);if(i==popup_choice)round_box(row,5,active,active,0);
  txt(box(row.x+9,row.y,row.w-18,row.h),i?setups[i-1].name:L"Default",font_small,i==popup_choice?accent:ink,0);
  add_hit(LOAD+i,row,1,0);
 }
 if(setup_count+1>8){txt(box(b.x-40,b.y,30,24),popup_scroll?L"\x2191":L"",font_small,muted,2);txt(box(b.x-40,b.y+b.h-24,30,24),popup_scroll+rows<=setup_count?L"\x2193":L"",font_small,muted,2);}
}
static void paint_modal(void){
 helper_canvas_dim((float)width,(float)height,.65f);
 helper_canvas_round((float)dialog_box.x-3,(float)dialog_box.y+5,(float)dialog_box.w+6,(float)dialog_box.h+6,14,RGB(7,8,10),RGB(7,8,10),0);
 Box b=dialog_box;round_box(b,12,panel,border,1);
 txt(box(b.x+24,b.y+18,b.w-48,32),modal==MODAL_SAVE?L"Save profile":L"Load another profile?",font_bold,ink,0);
 if(modal==MODAL_SAVE){
  txt(box(b.x+24,b.y+61,b.w-48,24),L"Profile name",font_small,muted,0);
  round_box(box(b.x+24,b.y+96,b.w-48,38),7,bg,accent,1);
  txt(box(b.x+24,b.y+146,b.w-48,24),*modal_error?modal_error:L"An existing name updates that saved profile.",font_small,*modal_error?danger:muted,0);
 }else{
  txt(box(b.x+24,b.y+64,b.w-48,30),L"Your current changes have not been saved.",font_body,muted,0);
  txt(box(b.x+24,b.y+100,b.w-48,30),L"Load the selected profile and discard these changes?",font_small,muted,0);
 }
 button(MODAL_CANCEL,box(b.x+b.w-230,b.y+b.h-60,94,36),L"Cancel",0,-1,1,0);
 button(MODAL_OK,box(b.x+b.w-124,b.y+b.h-60,100,36),modal==MODAL_SAVE?L"Save":L"Load",1,-1,1,0);
}
static void paint(HDC dc){
 scene_geometry();RECT client;GetClientRect(window,&client);last_render_ok=0;hit_count=0;
 if(!helper_canvas_begin(dc,client.right,client.bottom,dpi)){
  HBRUSH b=CreateSolidBrush(bg);FillRect(dc,&client,b);DeleteObject(b);SetTextColor(dc,ink);SetBkMode(dc,TRANSPARENT);TextOutW(dc,24,24,L"Graphics initialization failed",30);return;
 }
 fill(box(0,0,width,height),bg);fill(box(0,0,width,56),panel);fill(box(0,56,80,height-56),panel);
 helper_canvas_line(0,55.5f,(float)width,55.5f,border);helper_canvas_line(79.5f,56,79.5f,(float)height,border);
 txt(box(24,6,40,44),L"Z",font_logo,accent,0);
 txt(box(width-330,0,178,56),L"RAIN EDITION  /  0.10.0",font_brand,muted,1);
 for(int i=0;i<3;i++){Box b=box(width-138+i*46,0,46,56);if(hover==MINIMIZE+i)fill(b,i==2?RGB(104,36,31):high);
  int cx=b.x+23,cy=28;
  if(i==0)stroke((float)cx-5,(float)cy,(float)cx+5,(float)cy,muted);
  else if(i==1){if(IsZoomed(window)){helper_canvas_line((float)cx-2,(float)cy-6,(float)cx+6,(float)cy-6,muted);helper_canvas_line((float)cx+6,(float)cy-6,(float)cx+6,(float)cy+2,muted);}round_box(box(cx-5,cy-4,9,9),0,panel,muted,1);}
  else {stroke((float)cx-5,(float)cy-5,(float)cx+5,(float)cy+5,ink);stroke((float)cx+5,(float)cy-5,(float)cx-5,(float)cy+5,ink);}
  add_hit(MINIMIZE+i,b,1,0);
 }
 const wchar_t *nav[]={L"Camera",L"Profiles",L"Files"};
 for(int i=0;i<3;i++){Box b=box(9,74+i*68,62,60);int selected=page==i;
  if(selected||hover==NAV_CAMERA+i||focus_id==NAV_CAMERA+i)round_box(b,8,selected?active:high,keyboard_focus&&focus_id==NAV_CAMERA+i?accent:selected?active:high,keyboard_focus&&focus_id==NAV_CAMERA+i);
  icon(i,b.x+22,b.y+10,selected?accent:muted);txt(box(b.x,b.y+34,b.w,20),nav[i],font_brand,selected?accent:muted,2);add_hit(NAV_CAMERA+i,b,1,0);
 }
 txt(box(106,80,width-358,22),L"FRONTIER HELPER",font_brand,muted,0);
 const wchar_t *head[]={L"Camera settings",L"Camera profiles",L"Application files"};txt(box(106,104,width-358,36),head[page],font_heading,ink,0);
 txt(box(profile_box.x,78,profile_box.w,20),dirty?L"Profile \x00b7 modified":L"Current profile",font_brand,dirty?accent:muted,0);
 round_box(profile_box,8,panel,popup||(keyboard_focus&&focus_id==PROFILE)?accent:border,1);
 txt(box(profile_box.x+12,profile_box.y,profile_box.w-44,36),current_name,font_small,ink,0);icon(8,profile_box.x+profile_box.w-30,profile_box.y+10,muted);add_hit(PROFILE,profile_box,1,0);
 helper_canvas_clip((float)viewport.x-3,(float)viewport.y,(float)viewport.w+6,(float)viewport.h);
 if(page==PAGE_CAMERA)paint_camera();else if(page==PAGE_PROFILES)paint_profiles();else paint_files();helper_canvas_unclip();
 if(scroll_max){int track=viewport.h;int thumb=track*track/page_length();if(thumb<28)thumb=28;
  int yy=viewport.y+(track-thumb)*scroll_y/scroll_max;scroll_thumb=box(width-18,yy,11,thumb);
  round_box(box(width-14,yy,3,thumb),2,hover==SCROLLBAR||drag_scroll?muted:border,border,0);
  add_hit(SCROLLBAR,box(width-22,viewport.y,18,track),1,0);}
 txt(box(106,height-128,width-132,28),notice_text,font_small,notice_error?danger:accent,0);
 helper_canvas_line(106,(float)height-85,(float)width-26,(float)height-85,border);
 circle(110,(float)height-48,3,connected&&status.enabled?accent:muted);
 txt(box(124,height-70,width-330,44),scene_name(),font_small,ink,0);
 button(START,box(width-171,height-70,145,44),pending||working?L"Wait...":connected&&status.enabled?L"STOP":L"START",1,connected&&status.enabled?7:6,!pending&&!working&&!closing,0);
 if(popup)paint_popup();if(modal)paint_modal();last_render_ok=helper_canvas_end();
}

static int hit_at(int x,int y){
 for(int i=hit_count-1;i>=0;i--){Hit *h=&hits[i];
  if(modal&&h->id!=MODAL_OK&&h->id!=MODAL_CANCEL&&(h->id<MINIMIZE||h->id>CLOSE))continue;
  if(popup&&(h->id<LOAD||h->id>=LOAD+setup_count+1))continue;
  if(h->enabled&&(!h->content||inside(viewport,x,y))&&inside(h->box,x,y))return h->id;
 }return -1;
}
static void slider_from_x(int i,int x,int flush){
 Box b=slider_boxes[i];float t=(x-b.x-6)/(float)(b.w-12);t=fmaxf(0,fminf(1,t));
 float lo=lower(i),hi=upper(i),v=lo+t*(hi-lo);v=lo+roundf((v-lo)/parameters[i].step)*parameters[i].step;
 change(i,v,flush);
}
static void navigate_focus(int backwards){
 if(modal){
  if(GetFocus()==editor){SetFocus(window);focus_id=backwards?MODAL_OK:MODAL_CANCEL;}
  else if(modal==MODAL_SAVE&&((focus_id==MODAL_OK&&!backwards)||(focus_id==MODAL_CANCEL&&backwards))){SetFocus(editor);return;}
  else focus_id=focus_id==MODAL_CANCEL?MODAL_OK:MODAL_CANCEL;
  keyboard_focus=1;redraw();return;
 }
 popup=0;finish_edit(1);SetFocus(window);keyboard_focus=1;int index=-1;
 for(int i=0;i<hit_count;i++)if(hits[i].id==focus_id){index=i;break;}
 int direction=backwards?-1:1;
 for(int n=0;n<hit_count;n++){index=(index+direction+hit_count)%hit_count;Hit *h=&hits[index];
  if(h->enabled&&h->id>=NAV_CAMERA&&h->id<LOAD+setup_count+1&&h->id!=SCROLLBAR){focus_id=h->id;
   if(h->content){if(h->box.y<viewport.y)scroll_y-=viewport.y-h->box.y;
    else if(h->box.y+h->box.h>viewport.y+viewport.h)scroll_y+=h->box.y+h->box.h-viewport.y-viewport.h;
    scene_geometry();}redraw();return;}
 }
}
static LRESULT CALLBACK editor_proc(HWND h,UINT message,WPARAM w,LPARAM l){
 if(message==WM_KEYDOWN){
  if(w==VK_RETURN){if(modal==MODAL_SAVE)submit_modal();else {finish_edit(1);SetFocus(window);}return 0;}
  if(w==VK_ESCAPE){if(modal)close_modal();else {finish_edit(0);SetFocus(window);}return 0;}
  if(w==VK_TAB){navigate_focus(GetKeyState(VK_SHIFT)<0);return 0;}
 }
 if(message==WM_CHAR&&(w==VK_RETURN||w==VK_ESCAPE||w==VK_TAB))return 0;
 if(message==WM_KILLFOCUS&&edit_index>=0)finish_edit(1);
 return CallWindowProcW(editor_original,h,message,w,l);
}
static void create_fonts(void){
 helper_canvas_release();HFONT *fonts[]={&font_body,&font_small,&font_heading,&font_bold,&font_number,&font_brand,&font_logo};
 int sizes[]={14,13,25,14,13,11,30},weights[]={400,400,600,600,400,400,900};
 for(int i=0;i<7;i++){if(*fonts[i])DeleteObject(*fonts[i]);*fonts[i]=CreateFontW(-px(sizes[i]),0,0,0,weights[i],0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,i==4||i==5?L"Consolas":L"Segoe UI");}
 if(editor)SendMessageW(editor,WM_SETFONT,(WPARAM)(modal==MODAL_SAVE?font_body:font_number),TRUE);
}
static void configure_frame(HWND h){
 enum DWMNCRENDERINGPOLICY p=DWMNCRP_DISABLED;frame_result=DwmSetWindowAttribute(h,DWMWA_NCRENDERING_POLICY,&p,sizeof(p));
 COLORREF c=DWMWA_COLOR_NONE;DwmSetWindowAttribute(h,DWMWA_BORDER_COLOR,&c,sizeof(c));
 DWORD corner=2;DwmSetWindowAttribute(h,33,&corner,sizeof(corner));
 SetWindowPos(h,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
}
static HICON create_app_icon(void){
 int size=32;HDC screen=GetDC(NULL),dc=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,size,size);
 HGDIOBJ old=SelectObject(dc,bitmap);RECT r={0,0,size,size};HBRUSH b=CreateSolidBrush(panel);FillRect(dc,&r,b);DeleteObject(b);
 HFONT f=CreateFontW(-28,0,0,0,900,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Segoe UI");HGDIOBJ of=SelectObject(dc,f);
 SetTextColor(dc,accent);SetBkMode(dc,TRANSPARENT);DrawTextW(dc,L"Z",1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
 SelectObject(dc,of);DeleteObject(f);SelectObject(dc,old);BYTE mask[128]={0};HBITMAP mono=CreateBitmap(size,size,1,1,mask);
 ICONINFO ii={TRUE,0,0,mono,bitmap};HICON icon=CreateIconIndirect(&ii);DeleteObject(mono);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(NULL,screen);return icon;
}
static LRESULT CALLBACK window_proc(HWND h,UINT message,WPARAM w,LPARAM l){
 switch(message){
 case WM_NCCALCSIZE:return 0;
 case WM_NCPAINT:return 0;
 case WM_NCACTIVATE:return TRUE;
 case WM_DWMCOMPOSITIONCHANGED:configure_frame(h);return 0;
 case WM_NCHITTEST:{POINT p={GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&p);RECT r;GetClientRect(h,&r);
  int edge=px(5);if(!IsZoomed(h)){
   int left=p.x<edge,right=p.x>=r.right-edge,top=p.y<edge,bottom=p.y>=r.bottom-edge;
   if(top&&left)return HTTOPLEFT;if(top&&right)return HTTOPRIGHT;if(bottom&&left)return HTBOTTOMLEFT;if(bottom&&right)return HTBOTTOMRIGHT;
   if(left)return HTLEFT;if(right)return HTRIGHT;if(top)return HTTOP;if(bottom)return HTBOTTOM;
  }
  if(p.y<px(56)&&p.x<r.right-px(138))return HTCAPTION;return HTCLIENT;
 }
 case WM_GETMINMAXINFO:{MINMAXINFO *m=(MINMAXINFO*)l;m->ptMinTrackSize=(POINT){px(860),px(620)};
  MONITORINFO mi={0};mi.cbSize=sizeof(mi);if(GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&mi)){
   m->ptMaxPosition=(POINT){mi.rcWork.left-mi.rcMonitor.left,mi.rcWork.top-mi.rcMonitor.top};m->ptMaxSize=(POINT){mi.rcWork.right-mi.rcWork.left,mi.rcWork.bottom-mi.rcWork.top};}return 0;
 }
 case WM_SIZE:if(window){finish_edit(1);popup=0;scene_geometry();if(modal==MODAL_SAVE)update_editor();redraw();}return 0;
 case WM_DPICHANGED:{finish_edit(1);dpi=LOWORD(w);create_fonts();RECT *r=(RECT*)l;SetWindowPos(h,NULL,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);update_editor();redraw();return 0;}
 case WM_ERASEBKGND:return 1;
 case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT r;GetClientRect(h,&r);HDC buffer=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,r.right,r.bottom);HGDIOBJ old=SelectObject(buffer,bitmap);
  paint(buffer);BitBlt(dc,0,0,r.right,r.bottom,buffer,0,0,SRCCOPY);SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);EndPaint(h,&ps);return 0;}
 case WM_CTLCOLOREDIT:SetTextColor((HDC)w,ink);SetBkColor((HDC)w,bg);return (LRESULT)edit_brush;
 case WM_MOUSEMOVE:{int x=MulDiv(GET_X_LPARAM(l),96,dpi),y=MulDiv(GET_Y_LPARAM(l),96,dpi);
  if(drag_scroll){int travel=viewport.h-scroll_thumb.h;if(travel>0)scroll_y=scroll_origin+(y-scroll_anchor)*scroll_max/travel;scene_geometry();redraw();return 0;}
  if(drag_index>=0){slider_from_x(drag_index,x,0);return 0;}int id=hit_at(x,y);if(hover!=id){hover=id;redraw();}
  TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);return 0;}
 case WM_MOUSELEAVE:hover=-1;redraw();return 0;
 case WM_LBUTTONDOWN:{int x=MulDiv(GET_X_LPARAM(l),96,dpi),y=MulDiv(GET_Y_LPARAM(l),96,dpi);int id=hit_at(x,y);
  if(modal==MODAL_SAVE&&inside(box(dialog_box.x+24,dialog_box.y+96,dialog_box.w-48,38),x,y)){SetFocus(editor);return 0;}
  if(popup&&id<0){popup=0;redraw();return 0;}
  int valid=finish_edit(1);SetFocus(h);keyboard_focus=0;focus_id=id;if(!valid&&(id==START||id==SAVE))return 0;
  if(id>=SLIDER&&id<SLIDER+10){drag_index=id-SLIDER;SetCapture(h);slider_from_x(drag_index,x,0);}
  else if(id==SCROLLBAR){if(inside(scroll_thumb,x,y)){drag_scroll=1;scroll_anchor=y;scroll_origin=scroll_y;SetCapture(h);}
   else {scroll_y+=y<scroll_thumb.y?40-viewport.h:viewport.h-40;scene_geometry();}}
  else if(id>=0)activate(id);redraw();return 0;}
 case WM_LBUTTONUP:if(drag_index>=0){drag_index=-1;ReleaseCapture();changed(1);}if(drag_scroll){drag_scroll=0;ReleaseCapture();redraw();}return 0;
 case WM_CAPTURECHANGED:drag_scroll=0;if(drag_index>=0){drag_index=-1;changed(1);}return 0;
 case WM_MOUSEWHEEL:{if(modal)return 0;int delta=GET_WHEEL_DELTA_WPARAM(w);
  if(popup){popup_scroll-=delta/120;if(popup_scroll<0)popup_scroll=0;int max=setup_count-7;if(max<0)max=0;if(popup_scroll>max)popup_scroll=max;}
  else {finish_edit(1);scroll_y-=delta/120*60;scene_geometry();}redraw();return 0;}
 case WM_KEYDOWN:{
  if(w==VK_TAB){navigate_focus(GetKeyState(VK_SHIFT)<0);return 0;}
  if(w==VK_ESCAPE){if(modal)close_modal();else if(popup){popup=0;redraw();}return 0;}
  if(modal){if(w==VK_RETURN||w==VK_SPACE)activate(focus_id);return 0;}
  if(popup){if(w==VK_UP||w==VK_DOWN||w==VK_HOME||w==VK_END){if(w==VK_HOME)popup_choice=0;else if(w==VK_END)popup_choice=setup_count;else popup_choice+=w==VK_UP?-1:1;
    if(popup_choice<0)popup_choice=0;if(popup_choice>setup_count)popup_choice=setup_count;
    if(popup_choice<popup_scroll)popup_scroll=popup_choice;if(popup_choice>popup_scroll+7)popup_scroll=popup_choice-7;redraw();}
   else if(w==VK_RETURN||w==VK_SPACE)request_load(popup_choice);return 0;}
  if(focus_id>=SLIDER&&focus_id<SLIDER+10){int i=focus_id-SLIDER;float n=value(i);
   if(w==VK_LEFT||w==VK_DOWN)n-=parameters[i].step*(GetKeyState(VK_SHIFT)<0?10:1);
   else if(w==VK_RIGHT||w==VK_UP)n+=parameters[i].step*(GetKeyState(VK_SHIFT)<0?10:1);
   else if(w==VK_HOME)n=lower(i);else if(w==VK_END)n=upper(i);else if(w==VK_RETURN){begin_edit(i);return 0;}else return 0;
   change(i,n,1);return 0;}
  if(w==VK_RETURN||w==VK_SPACE){if(focus_id<0)focus_id=NAV_CAMERA;activate(focus_id);return 0;}
  if((w==VK_DOWN||w==VK_UP)&&focus_id==PROFILE){activate(PROFILE);return 0;}
  if(w==VK_NEXT||w==VK_PRIOR){scroll_y+=w==VK_NEXT?viewport.h-40:40-viewport.h;scene_geometry();redraw();return 0;}
  if(GetKeyState(VK_CONTROL)<0&&w=='S'){begin_save();return 0;}
  if(GetKeyState(VK_CONTROL)<0&&w=='R'){activate(DEFAULTS);return 0;}
  return 0;
 }
 case WM_CHAR:if(focus_id>=NUMBER&&focus_id<NUMBER+10&&(w==L'-'||w==L'.'||(w>=L'0'&&w<=L'9'))){begin_edit(focus_id-NUMBER);SendMessageW(editor,WM_CHAR,w,l);}return 0;
 case WM_COMMAND:if((HWND)l==editor&&HIWORD(w)==EN_CHANGE&&modal==MODAL_SAVE){modal_error[0]=0;redraw();}return 0;
 case WM_SETCURSOR:if(LOWORD(l)==HTCLIENT){SetCursor(LoadCursorW(NULL,(LPCWSTR)(hover>=NUMBER&&hover<NUMBER+10?IDC_IBEAM:hover>=100?IDC_HAND:IDC_ARROW)));return TRUE;}break;
 case STUDIO_LOG:{StudioLog *entry=(StudioLog*)l;if(entry){
  if(entry->level>=2&&wcsstr(entry->text,L"[start]")==NULL){const wchar_t *text=wcschr(entry->text,L']');if(text){text++;while(*text==L' ')text++;}else text=entry->text;notify(1,L"%ls",text);}
  else if(wcsstr(entry->text,L"[start]")&&entry->level==1)notify(0,L"Camera connected");
  HeapFree(GetProcessHeap(),0,entry);}return 0;}
 case STUDIO_UPDATE:AcquireSRWLockShared(&backend.lock);status=backend.status;connected=backend.connected;working=backend.working;
  if(!backend.working&&backend.action==ACTION_NONE&&(pending!=ACTION_STOP||!connected||!status.enabled))pending=0;ReleaseSRWLockShared(&backend.lock);
  if(status.frames!=observed_frames){observed_frames=status.frames;last_frames_tick=GetTickCount64();}redraw();return 0;
 case WM_TIMER:if(*notice_text&&GetTickCount64()>notice_until){notice_text[0]=0;redraw();}return 0;
 case WM_CLOSE:if(closing)return 0;finish_edit(1);close_modal();closing=1;popup=0;
  if(backend.thread){studio_action(&backend,ACTION_QUIT,0);redraw();}else DestroyWindow(h);return 0;
 case STUDIO_CLOSED:DestroyWindow(h);return 0;
 case WM_DESTROY:KillTimer(h,1);PostQuitMessage(0);return 0;
 }return DefWindowProcW(h,message,w,l);
}

/* Offscreen snapshots exercise this application's real scene at the requested
   DPI. They are rendering QA, not screenshots of a desktop or a Rain session. */
static int render_preview(const wchar_t *name){
 RECT r;GetClientRect(window,&r);HDC dc=CreateCompatibleDC(NULL);BITMAPINFO info={0};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
 info.bmiHeader.biWidth=r.right;info.bmiHeader.biHeight=-r.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void *pixels=NULL;
 HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,NULL,0);if(!bitmap){DeleteDC(dc);return 0;}HGDIOBJ old=SelectObject(dc,bitmap);paint(dc);
 if(edit_index>=0||modal==MODAL_SAVE){update_editor();RECT edit,fmt;GetWindowRect(editor,&edit);MapWindowPoints(NULL,window,(POINT*)&edit,2);SendMessageW(editor,EM_GETRECT,0,(LPARAM)&fmt);
  wchar_t input[128];GetWindowTextW(editor,input,128);LRESULT pos=SendMessageW(editor,EM_POSFROMCHAR,0,0);
  int saved=SaveDC(dc);IntersectClipRect(dc,edit.left,edit.top,edit.right,edit.bottom);SelectObject(dc,(HFONT)SendMessageW(editor,WM_GETFONT,0,0));SetTextColor(dc,ink);SetBkMode(dc,TRANSPARENT);
  if(pos!=-1)TextOutW(dc,edit.left+(SHORT)LOWORD(pos),edit.top+(SHORT)HIWORD(pos),input,(int)wcslen(input));RestoreDC(dc,saved);
 }
 BITMAPFILEHEADER file={0};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info.bmiHeader);file.bfSize=file.bfOffBits+(DWORD)(r.right*r.bottom*4);
 wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,L"%ls%ls",app_folder,name);FILE *out=_wfopen(path,L"wb");int ok=0;
 if(out){ok=fwrite(&file,1,sizeof(file),out)==sizeof(file)&&fwrite(&info.bmiHeader,1,sizeof(info.bmiHeader),out)==sizeof(info.bmiHeader)&&fwrite(pixels,1,(size_t)(r.right*r.bottom*4),out)==(size_t)(r.right*r.bottom*4);fclose(out);}
 SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return ok&&last_render_ok;
}
static int run_self_test(void){
 int checks=0,failures=0;
 #define CHECK(c) do{checks++;if(!(c)){failures++;fprintf(report,"FAIL line %d: %s\n",__LINE__,#c);}}while(0)
 wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,L"%lsqa-%d.txt",app_folder,dpi);FILE *report=_wfopen(path,L"wb");if(!report)return 1;
 CHECK(page==PAGE_CAMERA&&!advanced&&!dirty&&!connected&&!pending);
 CHECK(camera_settings_valid(&settings));CHECK(setup_count==0);
 CHECK(render_preview(L"preview-camera.bmp"));
 CHECK(number_boxes[0].y==number_boxes[3].y&&slider_boxes[0].y==slider_boxes[3].y);
 CHECK(number_boxes[0].w==number_boxes[3].w);
 float parsed=0;CHECK(parse_value(0,L"1.20",&parsed)&&fabsf(parsed-1.2f)<.00001f);
 CHECK(parse_value(0,L"1,20",&parsed));CHECK(!parse_value(0,L"nan",&parsed));CHECK(!parse_value(0,L"inf",&parsed));CHECK(!parse_value(0,L"9",&parsed));CHECK(!parse_value(0,L"",&parsed));CHECK(!parse_value(0,L"1.2garbage",&parsed));
 Box b=slider_boxes[0];SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(px(b.x+6+(b.w-12)*7/10),px(b.y+12)));
 CHECK(drag_index==0&&fabsf(value(0)-1.2f)<.011f);SendMessageW(window,WM_LBUTTONUP,0,0);CHECK(drag_index<0&&dirty);
 focus_id=SLIDER;SendMessageW(window,WM_KEYDOWN,VK_HOME,0);CHECK(value(0)==.5f);SendMessageW(window,WM_KEYDOWN,VK_END,0);CHECK(value(0)==1.5f);
 change(0,1.2f,1);begin_edit(0);SetWindowTextW(editor,L"1.25");SendMessageW(editor,WM_KEYDOWN,VK_RETURN,0);CHECK(fabsf(value(0)-1.25f)<.00001f&&edit_index<0);
 begin_edit(0);SetWindowTextW(editor,L"9");CHECK(!finish_edit(1)&&fabsf(value(0)-1.25f)<.00001f);
 begin_edit(0);SetWindowTextW(editor,L"1.4");SendMessageW(editor,WM_KEYDOWN,VK_ESCAPE,0);CHECK(fabsf(value(0)-1.25f)<.00001f);
 CHECK(change(7,-10,1)&&value(9)==-10);CHECK(camera_settings_valid(&settings));CHECK(!change(0,NAN,1));
 CHECK(save_profile(L"QA profile"));CHECK(setup_count==1&&!dirty);CameraSettings saved=settings;
 change(1,25,1);request_load(1);CHECK(modal==MODAL_DISCARD&&value(1)==25);close_modal();CHECK(value(1)==25);
 request_load(1);submit_modal();CHECK(memcmp(&settings,&saved,sizeof(settings))==0&&!dirty);
 change(2,40,1);CHECK(save_profile(L"QA profile"));CHECK(setup_count==1&&value(2)==40);
 activate(DEFAULTS);CHECK(value(0)==1&&value(2)==0&&dirty);load_profile(1);CHECK(value(2)==40&&!dirty);
 StudioProfile p;CHECK(setup_read(current_path,&p)&&memcmp(&p.camera,&settings,sizeof(settings))==0);
 CameraSettings before=settings;CHECK(!import_path(L"Z:\\no-such-file.json"));CHECK(memcmp(&before,&settings,sizeof(settings))==0);
 CHECK(import_path(current_path));CHECK(imported&&dirty);CHECK(save_profile(L"QA imported"));CHECK(setup_count==2&&!dirty&&!imported);
 activate(LIVE);CHECK(!settings.preview&&dirty);activate(GAMEPAD);CHECK(settings.gamepad_index==0);
 for(int i=0;i<4;i++)activate(GAMEPAD);CHECK(settings.gamepad_index==-1);
 activate(PROFILE);CHECK(popup);CHECK(render_preview(L"preview-profile-menu.bmp"));popup=0;
 activate(ADVANCED);CHECK(advanced);CHECK(render_preview(L"preview-advanced.bmp"));CHECK(scroll_max>0);
 SendMessageW(window,WM_MOUSEWHEEL,MAKEWPARAM(0,(WORD)-120),0);CHECK(scroll_y>0);CHECK(render_preview(L"preview-advanced-scrolled.bmp"));
 activate(NAV_PROFILES);CHECK(page==PAGE_PROFILES&&scroll_y==0);CHECK(render_preview(L"preview-profiles.bmp"));
 activate(NAV_FILES);CHECK(page==PAGE_FILES);CHECK(render_preview(L"preview-files.bmp"));CHECK(scroll_max==0);
 start_stop();CHECK(pending==ACTION_START&&!connected);start_stop();CHECK(pending==ACTION_START);pending=0;
 connected=1;status.enabled=1;status.scene=CAMERA_ACTIVE;status.applied_revision=status.accepted_revision=7;last_frames_tick=GetTickCount64();
 CHECK(!wcscmp(scene_name(),L"Camera active"));start_stop();CHECK(pending==ACTION_STOP);pending=0;status.enabled=0;
 CHECK(!wcscmp(scene_name(),L"Camera stopped"));start_stop();CHECK(pending==ACTION_START);pending=0;connected=0;memset(&status,0,sizeof(status));
 activate(NAV_CAMERA);advanced=0;load_profile(0);notice_text[0]=0;CHECK(render_preview(L"preview-camera.bmp"));
 begin_edit(4);CHECK(render_preview(L"preview-exact-input.bmp"));CHECK((HFONT)SendMessageW(editor,WM_GETFONT,0,0)==font_number);
 RECT format;SendMessageW(editor,EM_GETRECT,0,(LPARAM)&format);TEXTMETRICW metrics;HDC editdc=GetDC(editor);HGDIOBJ old=SelectObject(editdc,font_number);GetTextMetricsW(editdc,&metrics);SelectObject(editdc,old);ReleaseDC(editor,editdc);
 CHECK(format.bottom-format.top>=metrics.tmHeight);finish_edit(0);
 begin_save();CHECK(modal==MODAL_SAVE);CHECK(render_preview(L"preview-save.bmp"));close_modal();
 begin_save();SendMessageW(editor,WM_KEYDOWN,VK_TAB,0);CHECK(GetFocus()==window&&focus_id==MODAL_CANCEL);
 navigate_focus(0);CHECK(focus_id==MODAL_OK);navigate_focus(0);CHECK(GetFocus()==editor);close_modal();
 begin_save();SetWindowTextW(editor,L" ");submit_modal();CHECK(modal==MODAL_SAVE&&*modal_error);close_modal();
 begin_edit(0);SetWindowTextW(editor,L"9");activate(START);CHECK(!pending&&value(0)==1);
 SetWindowPos(window,NULL,0,0,px(860),px(620),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);CHECK(render_preview(L"preview-small.bmp"));
 CHECK(viewport.w>=700&&viewport.y+viewport.h<=height-140);CHECK(scroll_max>=0);
 advanced=1;CHECK(render_preview(L"preview-small-advanced.bmp"));scroll_y=scroll_max;CHECK(render_preview(L"preview-small-advanced-bottom.bmp"));
 CHECK(scroll_thumb.h<viewport.h);CHECK(hit_at(scroll_thumb.x+5,scroll_thumb.y+5)==SCROLLBAR);
 /* Exercise the same publishing functions as the visible controls, without
    creating a worker or attaching to a game process. */
 InitializeSRWLock(&backend.lock);self_test=0;change(0,1.17f,1);
 CHECK(backend.settings_serial==1&&backend.flush_settings&&memcmp(&backend.settings,&settings,sizeof(settings))==0);
 pending=0;start_stop();CHECK(backend.action==ACTION_START&&pending==ACTION_START);
 pending=0;connected=1;status.enabled=1;start_stop();CHECK(backend.action==ACTION_STOP&&pending==ACTION_STOP);
 studio_action(&backend,ACTION_QUIT,0);studio_action(&backend,ACTION_START,0);CHECK(backend.action==ACTION_QUIT);
 self_test=1;pending=connected=0;memset(&status,0,sizeof(status));
 RECT client,outer;GetClientRect(window,&client);GetWindowRect(window,&outer);CHECK(client.right==outer.right-outer.left&&client.bottom==outer.bottom-outer.top);
 BOOL nc=TRUE;HRESULT frame_query=DwmGetWindowAttribute(window,DWMWA_NCRENDERING_ENABLED,&nc,sizeof(nc));CHECK(SUCCEEDED(frame_result)&&SUCCEEDED(frame_query)&&!nc);
 fprintf(report,"Checks: %d\nFailures: %d\nDPI: %d\nDirectWrite: %d\nGame attachment tested: no\n",checks,failures,dpi,last_render_ok);fclose(report);
 swprintf(path,MAX_PATH,L"%lsverification-%d.json",app_folder,dpi);FILE *json=_wfopen(path,L"wb");
 if(json){fprintf(json,"{\"checks\":%d,\"failures\":%d,\"dpi\":%d,\"directwrite\":%s,\"nonclient_disabled\":%s,\"game_attachment_tested\":false}\n",checks,failures,dpi,last_render_ok?"true":"false",SUCCEEDED(frame_result)&&SUCCEEDED(frame_query)&&!nc?"true":"false");fclose(json);}else failures++;
 #undef CHECK
 return failures?1:0;
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,char *args,int show){
 (void)previous;self_test=strstr(args,"--self-test")!=NULL;
 typedef BOOL (WINAPI *SetContext)(HANDLE);SetContext set_context=(SetContext)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetProcessDpiAwarenessContext");
 if(!set_context||!set_context((HANDLE)(intptr_t)-4))SetProcessDPIAware();
 typedef UINT (WINAPI *SystemDpi)(void);SystemDpi system_dpi=(SystemDpi)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForSystem");
 if(system_dpi)dpi=(int)system_dpi();else {HDC dc=GetDC(NULL);dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(NULL,dc);}
 const char *arg_dpi=strstr(args,"--dpi=");if(self_test&&arg_dpi){int n=atoi(arg_dpi+6);if(n>=96&&n<=192)dpi=n;}
 GetModuleFileNameW(NULL,app_folder,MAX_PATH);wchar_t *slash=wcsrchr(app_folder,L'\\');if(!slash)return 1;slash[1]=0;
 if(wcslen(app_folder)>MAX_PATH-85)return 1;
 settings=baseline=camera_defaults();create_fonts();edit_brush=CreateSolidBrush(bg);app_icon=create_app_icon();
 if(self_test){swprintf(folder,MAX_PATH,L"%lstest-artifacts",app_folder);CreateDirectoryW(folder,NULL);wchar_t isolated[MAX_PATH];
  swprintf(isolated,MAX_PATH,L"%ls\\run-%lu-%d",folder,GetCurrentProcessId(),dpi);wcscpy(folder,isolated);CreateDirectoryW(folder,NULL);}
 else if(!setup_folder(folder))return 1;
 refresh_setups();WNDCLASSEXW cls={0};cls.cbSize=sizeof(cls);cls.hInstance=instance;cls.lpfnWndProc=window_proc;
 cls.hCursor=LoadCursorW(NULL,(LPCWSTR)IDC_ARROW);cls.lpszClassName=L"FrontierHelperCompactWindow";cls.hIcon=cls.hIconSm=app_icon;if(!RegisterClassExW(&cls))return 1;
 int w=px(1040),h=px(780);if(!self_test){MONITORINFO mi={0};mi.cbSize=sizeof(mi);if(GetMonitorInfoW(MonitorFromPoint((POINT){0,0},MONITOR_DEFAULTTOPRIMARY),&mi)){
  if(w>mi.rcWork.right-mi.rcWork.left-32)w=mi.rcWork.right-mi.rcWork.left-32;if(h>mi.rcWork.bottom-mi.rcWork.top-32)h=mi.rcWork.bottom-mi.rcWork.top-32;}}
 window=CreateWindowExW(WS_EX_APPWINDOW,cls.lpszClassName,L"Frontier Helper — Rain 0.10.0",WS_POPUP|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_SYSMENU|WS_CLIPCHILDREN,
  (GetSystemMetrics(SM_CXSCREEN)-w)/2,(GetSystemMetrics(SM_CYSCREEN)-h)/2,w,h,NULL,NULL,instance,NULL);if(!window)return 1;
 configure_frame(window);editor=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|ES_AUTOHSCROLL|ES_RIGHT,0,0,0,0,window,(HMENU)101,instance,NULL);if(!editor)return 1;
 editor_original=(WNDPROC)SetWindowLongPtrW(editor,GWLP_WNDPROC,(LONG_PTR)editor_proc);
 if(self_test){int result=run_self_test();DestroyWindow(window);return result;}
 wchar_t core[MAX_PATH];swprintf(core,MAX_PATH,L"%lsFrontierOrbitRain08.dll",app_folder);
 if(!studio_backend_start(&backend,window,core)){MessageBoxW(window,L"The camera module could not start.",L"Frontier Helper",MB_OK|MB_ICONERROR);DestroyWindow(window);return 1;}
 scene_geometry();SetTimer(window,1,250,NULL);ShowWindow(window,show==SW_HIDE?SW_SHOWNORMAL:show);UpdateWindow(window);SetFocus(window);
 MSG message;while(GetMessageW(&message,NULL,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
 if(backend.thread){WaitForSingleObject(backend.thread,1000);CloseHandle(backend.thread);}if(setups)HeapFree(GetProcessHeap(),0,setups);
 helper_canvas_release();DeleteObject(font_body);DeleteObject(font_small);DeleteObject(font_heading);DeleteObject(font_bold);DeleteObject(font_number);DeleteObject(font_brand);DeleteObject(font_logo);DeleteObject(edit_brush);if(app_icon)DestroyIcon(app_icon);return 0;
}
