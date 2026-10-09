#include "studio_backend.h"
#include "profile_store.h"
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>
#include <stdarg.h>
#include <wchar.h>
#include <stdio.h>
#include <stddef.h>

#define MAX_LINES 500
#define MAX_HITS 128
#define ID_COMMAND 101
#define ID_NAME 102
#define ID_NUMBER 103
enum {HIT_START=1,HIT_RESET,HIT_DEFAULTS,HIT_PREVIEW,HIT_PAD,HIT_CHOOSE,
 HIT_OPEN_SETUPS,HIT_EXPORT_LOG,HIT_COPY,HIT_SAVE,HIT_NEW,HIT_IMPORT,HIT_EXPORT,HIT_OPEN_LOG,
 HIT_TAB=100,HIT_SLIDER=200,HIT_VALUE=300,HIT_SETUP=400};
typedef struct {wchar_t text[512];int level;} Line;
typedef struct {int id;RECT r;} Hit;
typedef struct {const wchar_t *label,*key;size_t offset;float lo,hi,step;const wchar_t *unit;} Slider;
static const Slider sliders[]={
 {L"DISTANCE",L"distance",offsetof(CameraSettings,distance_scale),.5f,1.5f,.01f,L"x"},
 {L"HEIGHT OFFSET",L"height",offsetof(CameraSettings,height_offset),-250,250,1,L"u"},
 {L"SHOULDER",L"shoulder",offsetof(CameraSettings,shoulder_offset),-300,300,1,L"u"},
 {L"RESPONSE",L"response",offsetof(CameraSettings,response),1,100,.5f,L""},
 {L"DEADZONE",L"deadzone",offsetof(CameraSettings,deadzone),.02f,.5f,.01f,L""},
 {L"YAW SPEED",L"yaw",offsetof(CameraSettings,yaw_speed),20,720,1,L"deg/s"},
 {L"PITCH SPEED",L"pitch",offsetof(CameraSettings,pitch_speed),10,360,1,L"deg/s"},
 {L"UP LIMIT",L"min",offsetof(CameraSettings,min_pitch),-85,0,1,L"deg"},
 {L"DOWN LIMIT",L"max",offsetof(CameraSettings,max_pitch),0,85,1,L"deg"},
 {L"RESET TILT",L"resetpitch",offsetof(CameraSettings,reset_pitch),-85,85,1,L"deg"}
};
static HWND window,command_edit,name_edit,number_edit;
static WNDPROC original_edit;
static StudioBackend backend;
static CameraSettings settings;
static ControlReply status;
static int connected,working,tab,drag=-1,focused_slider=-1,number_slider=-1;
static int width=1280,height=840,split=900,dpi=96,closing;
static HFONT font_ui,font_small,font_title,font_mono,font_mono_small,font_logo;
static HBRUSH edit_brush;
static Hit hits[MAX_HITS];static int hit_count;
static Line terminal[MAX_LINES],debug[MAX_LINES];static int terminal_count,debug_count,terminal_scroll,debug_scroll;
static SavedSetup *setups;static int setup_count,selected_setup=-1,setup_scroll;
static wchar_t folder[MAX_PATH],app_folder[MAX_PATH],client_path[MAX_PATH];
static wchar_t history[32][256];static int history_count,history_cursor;
static ULONGLONG last_frames_tick;static uint64_t observed_frames;
static const COLORREF bg=RGB(17,17,23),panel=RGB(23,23,31),raised=RGB(29,28,37),
 border=RGB(49,46,59),muted=RGB(139,132,155),ink=RGB(237,231,228),
 peach=RGB(232,169,143),purple=RGB(167,146,224),green=RGB(151,205,168),red=RGB(236,137,145);

static int px(int v){return MulDiv(v,dpi,96);}
static RECT rect(int x,int y,int w,int h){RECT r={px(x),px(y),px(x+w),px(y+h)};return r;}
static void fill(HDC dc,int x,int y,int w,int h,COLORREF color) {
 RECT r=rect(x,y,w,h);HBRUSH brush=CreateSolidBrush(color);FillRect(dc,&r,brush);DeleteObject(brush);
}
static void text(HDC dc,int x,int y,const wchar_t *s,HFONT font,COLORREF color) {
 SelectObject(dc,font);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);TextOutW(dc,px(x),px(y),s,(int)wcslen(s));
}
static void box(HDC dc,int x,int y,int w,int h,COLORREF color) {
 HBRUSH brush=CreateSolidBrush(color);HPEN pen=CreatePen(PS_SOLID,1,border);
 HGDIOBJ old_b=SelectObject(dc,brush),old_p=SelectObject(dc,pen);
 RoundRect(dc,px(x),px(y),px(x+w),px(y+h),px(12),px(12));
 SelectObject(dc,old_b);SelectObject(dc,old_p);DeleteObject(brush);DeleteObject(pen);
}
static COLORREF mix(COLORREF a,COLORREF b,double t) {
 return RGB((int)(GetRValue(a)+(GetRValue(b)-GetRValue(a))*t),
  (int)(GetGValue(a)+(GetGValue(b)-GetGValue(a))*t),
  (int)(GetBValue(a)+(GetBValue(b)-GetBValue(a))*t));
}
static void gradient(HDC dc,int x,int y,int w,int h,COLORREF left,COLORREF right) {
 for(int i=0;i<w;i++)fill(dc,x+i,y,1,h,mix(left,right,(double)i/fmax(1,w-1)));
}
static void hit(int id,int x,int y,int w,int h) {
 if(hit_count<MAX_HITS)hits[hit_count++]=(Hit){id,{x,y,x+w,y+h}};
}
static void button(HDC dc,int id,int x,int y,int w,int h,const wchar_t *label,int primary) {
 box(dc,x,y,w,h,primary?peach:raised);SelectObject(dc,font_small);
 SIZE size;GetTextExtentPoint32W(dc,label,(int)wcslen(label),&size);
 text(dc,x+(w-MulDiv(size.cx,96,dpi))/2,y+(h-14)/2,label,font_small,primary?bg:ink);hit(id,x,y,w,h);
}
static void append(Line *lines,int *count,int level,const wchar_t *message) {
 if(*count==MAX_LINES){memmove(lines,lines+1,(MAX_LINES-1)*sizeof(*lines));(*count)--;}
 lines[*count].level=level;wcsncpy(lines[*count].text,message,511);lines[*count].text[511]=0;(*count)++;
}
static void terminal_line(int level,const wchar_t *format,...) {
 wchar_t message[512];va_list args;va_start(args,format);vswprintf(message,512,format,args);va_end(args);
 append(terminal,&terminal_count,level,message);terminal_scroll=0;InvalidateRect(window,NULL,FALSE);
}
static void banner(void) {
 const wchar_t *z[]={
  L"   ZZZZZZZZZZZZZZZZZZZZZZZZ",
  L"   ZZZZZZZZZZZZZZZZZZZZZZZZ",
  L"                  ZZZZZZZZ",
  L"               ZZZZZZZZ",
  L"            ZZZZZZZZ",
  L"         ZZZZZZZZ",
  L"   ZZZZZZZZZZZZZZZZZZZZZZZZ",
  L"   ZZZZZZZZZZZZZZZZZZZZZZZZ"};
 terminal_line(0,L"");for(int i=0;i<8;i++)terminal_line(3+i,L"%ls",z[i]);
 terminal_line(1,L"   FRONTIER / ORBIT STUDIO 0.8.1   //   RAIN HD");
}
static float value(int i){float v;memcpy(&v,(BYTE*)&settings+sliders[i].offset,sizeof(v));return v;}
static float lower(int i){return i==9?settings.min_pitch:sliders[i].lo;}
static float upper(int i){return i==9?settings.max_pitch:sliders[i].hi;}
static void change(int i,float v) {
 v=fmaxf(lower(i),fminf(upper(i),v));memcpy((BYTE*)&settings+sliders[i].offset,&v,sizeof(v));
 settings.reset_pitch=fmaxf(settings.min_pitch,fminf(settings.max_pitch,settings.reset_pitch));
 studio_settings(&backend,&settings);InvalidateRect(window,NULL,FALSE);
}
static void apply_settings(const CameraSettings *s) {
 settings=*s;studio_settings(&backend,&settings);studio_flush(&backend);InvalidateRect(window,NULL,FALSE);
}
static const wchar_t *scene_name(void) {
 if(working)return L"CONNECTING";if(!connected)return L"DISCONNECTED";
 if(!status.enabled)return L"STOPPED";
 if(status.scene>=CAMERA_WAIT_SCENE&&status.scene<=CAMERA_GEOMETRY_UNAVAILABLE) {
  const wchar_t *waiting[]={L"",L"WAITING FOR SCENE",L"WAITING FOR FOCUS",L"SETTLING",L"REFERENCE PENDING",L"GEOMETRY FALLBACK"};
  return waiting[status.scene];
 }
 if(status.applied_revision!=status.accepted_revision)return L"WAITING FOR FRAME";
 if(status.scene==CAMERA_ACTIVE&&GetTickCount64()-last_frames_tick>1200)return L"WAITING FOR FRAME";
 const wchar_t *names[]={L"DISABLED",L"WAITING FOR SCENE",L"WAITING FOR FOCUS",
  L"SETTLING",L"REFERENCE PENDING",L"GEOMETRY FALLBACK",L"ACTIVE"};
 return status.scene<7?names[status.scene]:L"UNKNOWN";
}
static void card_row(HDC dc,int y,const wchar_t *label,const wchar_t *v,COLORREF color) {
 text(dc,split+22,y,label,font_mono_small,muted);text(dc,split+139,y,v,font_mono_small,color);
}
/* Wrap by measured glyph width, including long unbroken paths. Scroll is in visual rows. */
static void console(HDC dc,Line *lines,int count,int x,int y,int w,int h,int scroll,int small) {
 const int row=small?17:18;HFONT font=small?font_mono_small:font_mono;
 SelectObject(dc,font);int columns=w/(small?7:8);if(columns<8)columns=8;
 typedef struct {wchar_t s[512];int level;} Visual;
 Visual *visual=HeapAlloc(GetProcessHeap(),0,sizeof(*visual)*MAX_LINES*8);if(!visual)return;
 int total=0;
 for(int i=0;i<count&&total<MAX_LINES*8;i++) {
  const wchar_t *p=lines[i].text;
  do {
   int n=(int)wcslen(p);if(n>columns)n=columns;
   if(n==columns&&p[n]){int space=n;while(space>columns/2&&p[space]!=L' ')space--;if(space>columns/2)n=space;}
   wcsncpy(visual[total].s,p,(size_t)n);visual[total].s[n]=0;visual[total].level=lines[i].level;total++;
   p+=n;while(*p==L' ')p++;
  }while(*p&&total<MAX_LINES*8);
 }
 int visible=h/row,start=total-visible-scroll;if(start<0)start=0;
 RECT clip=rect(x,y,w,h);int saved=SaveDC(dc);IntersectClipRect(dc,clip.left,clip.top,clip.right,clip.bottom);
 for(int i=start;i<total&&i<start+visible;i++) {
  int level=visual[i].level;
  COLORREF color=level==1?green:level==2?red:level>=3?mix(peach,purple,(level-3)/7.0):muted;
  if(!small&&level>=3) {
   SIZE cell;GetTextExtentPoint32W(dc,L"Z",1,&cell);int step=MulDiv(cell.cx,96,dpi);
   for(int k=0;visual[i].s[k];k++)if(visual[i].s[k]!=L' '){wchar_t glyph[2]={visual[i].s[k],0};
    text(dc,x+k*step,y+(i-start)*row,glyph,font,color);}
  }else text(dc,x,y+(i-start)*row,visual[i].s,font,color);
 }
 RestoreDC(dc,saved);HeapFree(GetProcessHeap(),0,visual);
}
static void draw_sliders(HDC dc,int content_x,int content_w) {
 int x=content_x,y=187,w=content_w,h=306;
 box(dc,x,y,w,h,panel);
 text(dc,x+22,y+18,L"POSITION / FEEL",font_mono_small,peach);
 text(dc,x+w/2+14,y+18,L"ROTATION / LIMITS",font_mono_small,purple);
 int column=(w-66)/2;
 for(int i=0;i<10;i++) {
  int col=i/5,slot=i%5,sx=x+22+col*(column+22),sy=y+53+slot*44;
  text(dc,sx,sy,sliders[i].label,font_small,muted);
  wchar_t display[64];float v=value(i);
  if(i==0||i==4)swprintf(display,64,L"%.2f%ls",v,sliders[i].unit);
  else swprintf(display,64,L"%.0f%ls",v,sliders[i].unit);
  SelectObject(dc,font_mono_small);SIZE t;GetTextExtentPoint32W(dc,display,(int)wcslen(display),&t);
  text(dc,sx+column-MulDiv(t.cx,96,dpi),sy,display,font_mono_small,ink);
  hit(HIT_VALUE+i,sx+column-85,sy-2,85,20);
  int line_w=column-7,line_y=sy+25;
  fill(dc,sx,line_y,line_w,3,border);
  double f=(v-lower(i))/fmax(.01,upper(i)-lower(i));
  int knob=sx+(int)(line_w*f);
  gradient(dc,sx,line_y,knob-sx,3,col?purple:peach,col?peach:purple);
  HBRUSH b=CreateSolidBrush(focused_slider==i?ink:(col?purple:peach));
  HGDIOBJ old=SelectObject(dc,b);Ellipse(dc,px(knob-5),px(line_y-4),px(knob+5),px(line_y+6));SelectObject(dc,old);DeleteObject(b);
  hit(HIT_SLIDER+i,sx-5,line_y-8,line_w+10,19);
 }
}
static void draw_files(HDC dc,int x,int w) {
 box(dc,x,187,w,306,panel);text(dc,x+22,207,L"SESSION FILES",font_mono_small,peach);
 text(dc,x+22,244,L"Rain client",font_ui,ink);
 const wchar_t *client=*client_path?client_path:L"Auto-detect running client.exe on start";
 RECT r=rect(x+22,271,w-44,34);SelectObject(dc,font_mono_small);SetTextColor(dc,muted);
 DrawTextW(dc,client,-1,&r,DT_SINGLELINE|DT_PATH_ELLIPSIS);
 text(dc,x+22,319,L"Core  /  FrontierOrbitRain08.dll",font_mono,ink);
 text(dc,x+22,347,L"Setups  /  %LocalAppData%\\FrontierOrbitRain\\setups",font_mono_small,muted);
 text(dc,x+22,376,L"ATTACH LOG  /  click to open the persistent session journal",font_small,purple);
 hit(HIT_OPEN_LOG,x+22,368,w-44,30);
 button(dc,HIT_CHOOSE,x+22,421,140,35,L"CHOOSE CLIENT",0);
 button(dc,HIT_OPEN_SETUPS,x+171,421,135,35,L"OPEN SETUPS",0);
 button(dc,HIT_EXPORT_LOG,x+315,421,128,35,L"EXPORT LOG",0);
 button(dc,HIT_COPY,x+452,421,110,35,L"COPY DEBUG",0);
}
static void draw_setups(HDC dc,int x,int w) {
 box(dc,x,187,w,306,panel);text(dc,x+22,207,L"SAVED SETUPS",font_mono_small,peach);
 button(dc,HIT_NEW,x+w-114,202,91,29,L"NEW",0);
 int rows=4;
 if(!setup_count)text(dc,x+22,264,L"Your saved camera setups will appear here.",font_ui,muted);
 for(int slot=0;slot<rows&&slot+setup_scroll<setup_count;slot++) {
  int i=slot+setup_scroll,sy=247+slot*40;box(dc,x+22,sy,w-44,34,i==selected_setup?RGB(45,36,47):raised);
  RECT label=rect(x+35,sy+7,w-286,22);SelectObject(dc,font_ui);SetTextColor(dc,i==selected_setup?peach:ink);
  DrawTextW(dc,setups[i].name,-1,&label,DT_SINGLELINE|DT_END_ELLIPSIS);
  wchar_t detail[80];swprintf(detail,80,L"%.2fx  /  H%+.0f  /  S%+.0f",setups[i].data.camera.distance_scale,
   setups[i].data.camera.height_offset,setups[i].data.camera.shoulder_offset);
  text(dc,x+w-235,sy+9,detail,font_mono_small,muted);hit(HIT_SETUP+i,x+22,sy,w-44,34);
 }
 if(setup_count>rows){wchar_t count[80];swprintf(count,80,L"%d-%d / %d   scroll to browse",setup_scroll+1,
  setup_scroll+rows<setup_count?setup_scroll+rows:setup_count,setup_count);text(dc,x+180,210,count,font_mono_small,muted);}
 text(dc,x+22,423,L"NAME",font_mono_small,muted);
 button(dc,HIT_SAVE,x+w-232,416,83,34,L"SAVE",1);
 button(dc,HIT_IMPORT,x+w-141,416,56,34,L"IN",0);
 button(dc,HIT_EXPORT,x+w-77,416,55,34,L"OUT",0);
}
static void paint(HDC target) {
 RECT client;GetClientRect(window,&client);HDC dc=CreateCompatibleDC(target);
 HBITMAP bitmap=CreateCompatibleBitmap(target,client.right,client.bottom);
 HGDIOBJ old=SelectObject(dc,bitmap);fill(dc,0,0,width,height,bg);hit_count=0;
 /* Low-contrast terminal chrome with a continuous warm-to-violet accent. */
 gradient(dc,0,0,width,3,peach,purple);gradient(dc,72,3,width-72,62,RGB(35,27,32),RGB(24,22,36));
 fill(dc,0,3,72,height-3,RGB(21,20,27));fill(dc,71,65,1,height-65,border);
 text(dc,25,18,L"Z",font_logo,peach);
 text(dc,99,17,L"FRONTIER",font_small,muted);text(dc,99,34,L"ORBIT STUDIO",font_ui,ink);
 text(dc,310,30,L"RAIN EDITION  /  0.8.1",font_mono_small,purple);
 text(dc,width-111,21,L"_",font_ui,muted);hit(-1,width-132,4,42,54);
 text(dc,width-72,24,L"□",font_ui,muted);hit(-2,width-90,4,42,54);
 text(dc,width-31,24,L"×",font_ui,muted);hit(-3,width-48,4,45,54);
 const wchar_t *tabs[]={L"CAMERA",L"FILES",L"SETUPS"};
 for(int i=0;i<3;i++) {
  int tx=100+i*114;text(dc,tx,83,tabs[i],font_small,i==tab?ink:muted);hit(HIT_TAB+i,tx-5,70,101,41);
  if(i==tab)gradient(dc,tx,108,78,2,peach,purple);
  text(dc,28,140+i*64,i==0?L"◎":i==1?L"≡":L"◇",font_title,i==tab?peach:muted);
  hit(HIT_TAB+i,4,122+i*64,63,52);
 }
 text(dc,28,height-45,L"?",font_ui,muted);hit(-4,4,height-59,63,48);
 int x=100,w=split-124;
 text(dc,x,131,tab==0?L"Make room for the hunt.":tab==1?L"Everything in its place.":L"Keep your favorite view.",font_title,ink);
 text(dc,x,163,tab==0?L"Adjust live. Save a setup. Go hunt.":tab==1?
  L"Choose Rain, manage setups, export session diagnostics.":L"Save your camera settings. Import or share JSON setups.",font_small,muted);
 button(dc,HIT_START,split-145,130,121,42,working?L"WAIT...":connected&&status.enabled?L"STOP":L"START",1);
 if(tab==0)draw_sliders(dc,x,w);else if(tab==1)draw_files(dc,x,w);else draw_setups(dc,x,w);
 if(tab==0) {
  text(dc,x+2,509,settings.preview?L"[x] LIVE PREVIEW":L"[ ] LIVE PREVIEW",font_mono_small,settings.preview?purple:muted);
  hit(HIT_PREVIEW,x,501,160,26);
  wchar_t pad[40];swprintf(pad,40,L"PAD  %ls",settings.gamepad_index<0?L"AUTO":settings.gamepad_index==0?L"1":settings.gamepad_index==1?L"2":settings.gamepad_index==2?L"3":L"4");
  text(dc,x+180,509,pad,font_mono_small,muted);hit(HIT_PAD,x+175,501,95,26);
  button(dc,HIT_DEFAULTS,split-269,499,113,28,L"DEFAULTS",0);
  button(dc,HIT_RESET,split-148,499,124,28,L"RESET VIEW",0);
 }
 int terminal_y=tab==0?543:510;
 box(dc,x,terminal_y,w,height-terminal_y-70,RGB(19,19,27));
 text(dc,x+18,terminal_y+14,L"TERMINAL",font_mono_small,purple);
 text(dc,split-248,terminal_y+14,L"help  /  start  /  stop",font_mono_small,muted);
 console(dc,terminal,terminal_count,x+14,terminal_y+42,w-28,height-terminal_y-126,terminal_scroll,0);
 fill(dc,x,height-59,w,1,border);text(dc,x+14,height-43,L">",font_mono,peach);
 /* Right console is fixed; every displayed number comes from the real core. */
 fill(dc,split,65,1,height-65,border);
 text(dc,split+22,84,L"DEBUG CONSOLE",font_mono_small,ink);
 text(dc,split+22,121,L"SESSION / TELEMETRY",font_mono_small,muted);
 text(dc,split+22,148,scene_name(),font_ui,connected&&status.enabled?green:working?peach:muted);
 wchar_t v[100];swprintf(v,100,connected?L"%lu / x86":L"—",status.game_pid);card_row(dc,184,L"process",v,ink);
 card_row(dc,209,L"controller",connected?(status.pad_present?L"connected":L"not detected"):L"—",ink);
 swprintf(v,100,connected?L"%llu":L"—",(unsigned long long)status.frames);card_row(dc,234,L"applied frames",v,ink);
 swprintf(v,100,connected?L"%.0f / %.0f":L"—",status.actual_radius,status.requested_radius);card_row(dc,259,L"distance / req",v,peach);
 swprintf(v,100,connected?L"%.0f / %.0f":L"—",status.yaw,status.pitch);card_row(dc,284,L"yaw / pitch",v,ink);
 swprintf(v,100,connected?L"%llu / %llu":L"—",(unsigned long long)status.applied_revision,
  (unsigned long long)status.accepted_revision);card_row(dc,309,L"profile / req",v,purple);
 card_row(dc,334,L"geometry",!connected?L"—":status.scene==CAMERA_GEOMETRY_UNAVAILABLE?
  L"unavailable / native view":status.scene==CAMERA_WAIT_SCENE?L"waiting for map":L"native walls + floor",ink);
 fill(dc,split+22,367,width-split-44,1,border);text(dc,split+22,386,L"EVENT STREAM",font_mono_small,purple);
 console(dc,debug,debug_count,split+22,414,width-split-44,height-458,debug_scroll,1);
 fill(dc,72,height-24,width-72,24,RGB(21,20,28));
 text(dc,100,height-19,closing?L"Closing channel / restoring native camera...":
  connected?L"LOCAL SESSION   /   no server changes":L"READY   /   open Rain, enter the city, type start",font_mono_small,muted);
 text(dc,width-151,height-19,L"MANUAL ATTACH",font_mono_small,purple);
 BitBlt(target,0,0,client.right,client.bottom,dc,0,0,SRCCOPY);
 SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
static void layout(void) {
 RECT r;GetClientRect(window,&r);width=MulDiv(r.right,96,dpi);height=MulDiv(r.bottom,96,dpi);
 split=width-(width<1150?310:370);
 MoveWindow(command_edit,px(133),px(height-43),px(split-166),px(23),TRUE);
 MoveWindow(name_edit,px(169),px(417),px(split-425),px(28),TRUE);
 ShowWindow(name_edit,tab==2?SW_SHOW:SW_HIDE);
 if(number_slider>=0)ShowWindow(number_edit,SW_HIDE);number_slider=-1;
 InvalidateRect(window,NULL,FALSE);
}
static void refresh_setups(void) {
 wchar_t selected_path[MAX_PATH]=L"";
 if(selected_setup>=0&&selected_setup<setup_count)wcscpy(selected_path,setups[selected_setup].path);
 int count=setup_list(folder,NULL,0);size_t bytes=(size_t)(count+1)*sizeof(SavedSetup);
 SavedSetup *next=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,bytes);if(!next)return;
 if(setups)HeapFree(GetProcessHeap(),0,setups);setups=next;setup_count=setup_list(folder,setups,count+1);
 selected_setup=-1;
 if(*selected_path)for(int i=0;i<setup_count;i++)if(!_wcsicmp(selected_path,setups[i].path)){selected_setup=i;break;}
 if(setup_scroll>setup_count-4)setup_scroll=setup_count>4?setup_count-4:0;
}
static int file_dialog(wchar_t path[MAX_PATH],int save,const wchar_t *filter,const wchar_t *extension) {
 OPENFILENAMEW o={0};o.lStructSize=sizeof(o);o.hwndOwner=window;o.lpstrFile=path;o.nMaxFile=MAX_PATH;
 o.lpstrFilter=filter;o.lpstrDefExt=extension;o.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|
  (save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
 return save?GetSaveFileNameW(&o):GetOpenFileNameW(&o);
}
static void save_setup(const wchar_t *new_name) {
 wchar_t name[128];if(new_name){wcsncpy(name,new_name,127);name[127]=0;selected_setup=-1;}
 else GetWindowTextW(name_edit,name,128);
 if(new_name)for(int i=0;i<setup_count;i++)if(!_wcsicmp(name,setups[i].name)){selected_setup=i;break;}
 if(!*name)wcscpy(name,L"My setup");StudioProfile p={0};p.camera=settings;
 if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,name,-1,p.name,sizeof(p.name),NULL,NULL)) {
  terminal_line(2,L"Setup name is too long.");return;
 }
 wchar_t path[MAX_PATH];
 if(selected_setup>=0)wcscpy(path,setups[selected_setup].path);
 else {FILETIME ft;GetSystemTimeAsFileTime(&ft);swprintf(path,MAX_PATH,L"%ls\\setup-%08lx%08lx.json",folder,ft.dwHighDateTime,ft.dwLowDateTime);}
 if(!setup_write(path,&p)){terminal_line(2,L"Cannot save setup. Check the setup folder.");return;}
 refresh_setups();selected_setup=-1;
 for(int i=0;i<setup_count;i++)if(!_wcsicmp(setups[i].path,path))selected_setup=i;
 SetWindowTextW(name_edit,name);terminal_line(1,L"Saved setup: %ls",name);InvalidateRect(window,NULL,FALSE);
}
static void load_setup(int index) {
 if(index<0||index>=setup_count)return;selected_setup=index;
 apply_settings(&setups[index].data.camera);SetWindowTextW(name_edit,setups[index].name);
 terminal_line(1,L"Loaded setup: %ls",setups[index].name);
}
static void debug_summary(wchar_t out[2048]) {
 swprintf(out,2048,L"Frontier Orbit Studio 0.8.1 / Rain HD\r\nConnected: %d\r\nState: %ls\r\nPID: %lu\r\nEngine status: %ld\r\nFrames: %llu\r\nProfile accepted/applied: %llu/%llu\r\nDistance requested/actual/reference: %.2f/%.2f/%.2f\r\nController: %lu\r\nYaw/pitch: %.2f/%.2f\r\nReset accepted/applied: %llu/%llu\r\nAttach log: %ls\r\n",
  connected,scene_name(),status.game_pid,status.engine_status,(unsigned long long)status.frames,
  (unsigned long long)status.accepted_revision,(unsigned long long)status.applied_revision,
  status.requested_radius,status.actual_radius,status.reference_radius,status.pad_present,status.yaw,status.pitch,
  (unsigned long long)status.reset_accepted,(unsigned long long)status.reset_applied,backend.log_path);
}
static void export_log(void) {
 wchar_t path[MAX_PATH]=L"orbit-session.txt";
 if(!file_dialog(path,1,L"Text log\0*.txt\0\0",L"txt"))return;
 FILE *f=_wfopen(path,L"wb");if(!f){terminal_line(2,L"Cannot write log.");return;}
 wchar_t summary[2048];char utf8[8192];debug_summary(summary);
 int n=WideCharToMultiByte(CP_UTF8,0,summary,-1,utf8,sizeof(utf8),NULL,NULL);if(n>0)fwrite(utf8,1,(size_t)n-1,f);
 for(int i=0;i<debug_count;i++) {
  n=WideCharToMultiByte(CP_UTF8,0,debug[i].text,-1,utf8,sizeof(utf8),NULL,NULL);
  if(n>0){fwrite(utf8,1,(size_t)n-1,f);fwrite("\r\n",1,2,f);}
 }fclose(f);terminal_line(1,L"Session log exported.");
}
static void copy_debug(void) {
 wchar_t summary[2048];debug_summary(summary);
 if(OpenClipboard(window)) {
  HGLOBAL data=GlobalAlloc(GMEM_MOVEABLE,(wcslen(summary)+1)*sizeof(wchar_t));
  if(data){void *p=GlobalLock(data);if(p){memcpy(p,summary,(wcslen(summary)+1)*sizeof(wchar_t));GlobalUnlock(data);EmptyClipboard();if(!SetClipboardData(CF_UNICODETEXT,data))GlobalFree(data);}else GlobalFree(data);}
  CloseClipboard();terminal_line(1,L"Debug summary copied.");
 }
}
static void command(const wchar_t *line) {
 wchar_t buffer[256];wcsncpy(buffer,line,255);buffer[255]=0;
 wchar_t *c=buffer;while(*c==L' ')c++;if(!*c)return;
 terminal_line(0,L"> %ls",c);
 wchar_t *args=wcschr(c,L' ');if(args){*args++=0;while(*args==L' ')args++;}else args=L"";
 if(!_wcsicmp(c,L"start")) {
  DWORD pid=0;if(*args){wchar_t *end;unsigned long n=wcstoul(args,&end,10);if(*end||!n){terminal_line(2,L"Use start or start <PID>.");return;}pid=(DWORD)n;}
  banner();studio_action(&backend,ACTION_START,pid);
 }else if(!_wcsicmp(c,L"stop"))studio_action(&backend,ACTION_STOP,0);
 else if(!_wcsicmp(c,L"reset"))studio_action(&backend,ACTION_RESET,0);
 else if(!_wcsicmp(c,L"defaults")){CameraSettings s=camera_defaults();apply_settings(&s);terminal_line(1,L"Default settings restored; reference distance kept.");}
 else if(!_wcsicmp(c,L"camera")||!_wcsicmp(c,L"files")||!_wcsicmp(c,L"setups")){
  tab=!_wcsicmp(c,L"camera")?0:!_wcsicmp(c,L"files")?1:2;if(tab==2)refresh_setups();layout();}
 else if(!_wcsicmp(c,L"save"))save_setup(*args?args:NULL);
 else if(!_wcsicmp(c,L"load")) {
  int found=0;for(int i=0;i<setup_count;i++)if(!_wcsicmp(args,setups[i].name)){load_setup(i);found=1;break;}
  if(!found)terminal_line(2,L"Setup not found. Open SETUPS to choose one.");
 }else if(!_wcsicmp(c,L"set")) {
  wchar_t *p=wcschr(args,L' ');if(!p){terminal_line(2,L"Use set <parameter> <value>.");return;}*p++=0;
  wchar_t *end;double v=wcstod(p,&end);while(*end==L' ')end++;
  if(end==p||*end||!isfinite(v)){terminal_line(2,L"Enter a finite numeric value.");return;}
  int index=-1;for(int i=0;i<10;i++)if(!_wcsicmp(args,sliders[i].key))index=i;
  if(index<0||v<lower(index)||v>upper(index)){terminal_line(2,L"Unknown parameter or value outside its limits.");return;}
  change(index,(float)v);studio_flush(&backend);terminal_line(1,L"%ls = %.3g",args,v);
 }else if(!_wcsicmp(c,L"status")){terminal_line(1,L"%ls / PID %lu / profile %llu of %llu",scene_name(),
  connected?status.game_pid:0,(unsigned long long)status.applied_revision,(unsigned long long)status.accepted_revision);}
 else if(!_wcsicmp(c,L"clear")){terminal_count=0;terminal_scroll=0;}
 else if(!_wcsicmp(c,L"help")) {
  terminal_line(1,L"start [PID]  stop  reset  status  defaults  clear");
  terminal_line(0,L"files  setups  save <name>  load <name>");
  terminal_line(0,L"set distance|height|shoulder|yaw|pitch <value>");
  terminal_line(0,L"set response|deadzone|min|max|resetpitch <value>");
  terminal_line(0,L"L3: reset in game / F8: toggle / Ctrl+S: save");
 }else terminal_line(2,L"Unknown command. Type help.");
 InvalidateRect(window,NULL,FALSE);
}
static void run_command_edit(void) {
 wchar_t line[256];GetWindowTextW(command_edit,line,256);SetWindowTextW(command_edit,L"");
 if(*line){if(history_count==32){memmove(history,history+1,31*sizeof(history[0]));history_count--;}
  wcscpy(history[history_count++],line);history_cursor=history_count;command(line);}
}
static void number_commit(void) {
 if(number_slider<0)return;wchar_t s[80],*end;GetWindowTextW(number_edit,s,80);
 for(wchar_t *p=s;*p;p++)if(*p==L',')*p=L'.';double v=wcstod(s,&end);
 if(end!=s&&!*end&&isfinite(v)&&v>=lower(number_slider)&&v<=upper(number_slider)) {
  change(number_slider,(float)v);studio_flush(&backend);
 }else terminal_line(2,L"Value is outside this control's limits.");
 ShowWindow(number_edit,SW_HIDE);number_slider=-1;SetFocus(command_edit);
}
static LRESULT CALLBACK edit_proc(HWND h,UINT m,WPARAM w,LPARAM l) {
 if(m==WM_KEYDOWN&&w==VK_RETURN){if(h==number_edit)number_commit();else if(h==command_edit)run_command_edit();else save_setup(NULL);return 0;}
 if(m==WM_KEYDOWN&&w==VK_ESCAPE&&h==number_edit){number_slider=-1;ShowWindow(h,SW_HIDE);SetFocus(command_edit);return 0;}
 if(m==WM_KEYDOWN&&(w==VK_UP||w==VK_DOWN)&&h==command_edit) {
  history_cursor+=(w==VK_UP?-1:1);if(history_cursor<0)history_cursor=0;if(history_cursor>history_count)history_cursor=history_count;
  SetWindowTextW(h,history_cursor<history_count?history[history_cursor]:L"");SendMessageW(h,EM_SETSEL,256,256);return 0;
 }
 if(m==WM_KEYDOWN&&(GetKeyState(VK_CONTROL)&0x8000)&&w=='S'){save_setup(NULL);return 0;}
 return CallWindowProcW(original_edit,h,m,w,l);
}
static void activate(int id) {
 if(id==HIT_START){if(!working)command(connected&&status.enabled?L"stop":L"start");}
 else if(id==HIT_RESET)command(L"reset");else if(id==HIT_DEFAULTS)command(L"defaults");
 else if(id==HIT_PREVIEW){settings.preview=!settings.preview;studio_settings(&backend,&settings);studio_flush(&backend);}
 else if(id==HIT_PAD){settings.gamepad_index++;if(settings.gamepad_index>3)settings.gamepad_index=-1;studio_settings(&backend,&settings);studio_flush(&backend);}
 else if(id==HIT_CHOOSE) {
  wchar_t path[MAX_PATH]=L"";if(file_dialog(path,0,L"Rain client.exe\0client.exe\0Executable\0*.exe\0\0",L"exe")) {
   wcscpy(client_path,path);studio_select_exe(&backend,path);terminal_line(1,L"Rain client selected. Connect with start.");
  }
 }else if(id==HIT_OPEN_SETUPS)ShellExecuteW(window,L"open",folder,NULL,NULL,SW_SHOWNORMAL);
 else if(id==HIT_OPEN_LOG){if(*backend.log_path)ShellExecuteW(window,L"open",backend.log_path,NULL,NULL,SW_SHOWNORMAL);
  else terminal_line(2,L"Persistent attach journal is unavailable.");}
 else if(id==HIT_EXPORT_LOG)export_log();else if(id==HIT_COPY)copy_debug();
 else if(id==HIT_SAVE)save_setup(NULL);
 else if(id==HIT_NEW){selected_setup=-1;SetWindowTextW(name_edit,L"My setup");SetFocus(name_edit);}
 else if(id==HIT_IMPORT) {
  wchar_t path[MAX_PATH]=L"";StudioProfile p;
  if(file_dialog(path,0,L"Camera setup\0*.json\0\0",L"json")) {
   if(setup_read(path,&p)) {
    wchar_t name[128];MultiByteToWideChar(CP_UTF8,0,p.name,-1,name,128);selected_setup=-1;
    SetWindowTextW(name_edit,name);apply_settings(&p.camera);terminal_line(1,L"Imported setup: %ls. Save to keep a local copy.",name);
   }else terminal_line(2,L"Invalid setup file; settings were kept unchanged.");
  }
 }else if(id==HIT_EXPORT) {
  wchar_t path[MAX_PATH]=L"camera-setup.json",name[128];StudioProfile p={0};p.camera=settings;
  GetWindowTextW(name_edit,name,128);if(!*name)wcscpy(name,L"My setup");
  if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,name,-1,p.name,128,NULL,NULL)&&
   file_dialog(path,1,L"Camera setup\0*.json\0\0",L"json")){
    int ok=setup_write(path,&p);terminal_line(ok?1:2,ok?L"Setup export completed.":L"Cannot export setup. Check the destination folder.");
   }
 }else if(id>=HIT_TAB&&id<HIT_TAB+3){tab=id-HIT_TAB;if(tab==2)refresh_setups();layout();}
 else if(id>=HIT_SETUP&&id<HIT_SETUP+setup_count)load_setup(id-HIT_SETUP);
 else if(id==-1)ShowWindow(window,SW_MINIMIZE);
 else if(id==-2)ShowWindow(window,IsZoomed(window)?SW_RESTORE:SW_MAXIMIZE);
 else if(id==-3)PostMessageW(window,WM_CLOSE,0,0);else if(id==-4)command(L"help");
 InvalidateRect(window,NULL,FALSE);
}
static void begin_number(int i) {
 number_slider=i;focused_slider=i;
 for(int k=0;k<hit_count;k++)if(hits[k].id==HIT_VALUE+i) {
  RECT r=hits[k].r;MoveWindow(number_edit,px(r.left),px(r.top),px(r.right-r.left),px(23),TRUE);break;
 }
 wchar_t value_text[40];swprintf(value_text,40,L"%.3g",value(i));SetWindowTextW(number_edit,value_text);
 ShowWindow(number_edit,SW_SHOW);SetFocus(number_edit);SendMessageW(number_edit,EM_SETSEL,0,-1);
}
static void drag_slider(int i,int mouse_x) {
 for(int k=0;k<hit_count;k++)if(hits[k].id==HIT_SLIDER+i) {
  int left=hits[k].r.left+5,right=hits[k].r.right-5;
  double f=fmax(0,fmin(1,(double)(mouse_x-left)/(right-left)));
  float v=(float)(lower(i)+(upper(i)-lower(i))*f);
  v=roundf(v/sliders[i].step)*sliders[i].step;change(i,v);return;
 }
}
static LRESULT CALLBACK window_proc(HWND h,UINT message,WPARAM w,LPARAM l) {
 switch(message) {
 case WM_CREATE:return 0;
 case WM_NCHITTEST: {
  POINT p={GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&p);
  int x=MulDiv(p.x,96,dpi),y=MulDiv(p.y,96,dpi);
  if(!IsZoomed(h)) {
   if(x<6&&y<6)return HTTOPLEFT;if(x>width-6&&y<6)return HTTOPRIGHT;
   if(x<6&&y>height-6)return HTBOTTOMLEFT;if(x>width-6&&y>height-6)return HTBOTTOMRIGHT;
   if(x<6)return HTLEFT;if(x>width-6)return HTRIGHT;if(y<6)return HTTOP;if(y>height-6)return HTBOTTOM;
  }
  if(y<65&&x>72&&x<width-132)return HTCAPTION;return HTCLIENT;
 }
 case WM_GETMINMAXINFO:((MINMAXINFO*)l)->ptMinTrackSize=(POINT){px(1000),px(740)};return 0;
 case WM_SIZE:if(command_edit)layout();return 0;
 case WM_ERASEBKGND:return 1;
 case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);paint(dc);EndPaint(h,&ps);return 0;}
 case WM_CTLCOLOREDIT:SetTextColor((HDC)w,ink);SetBkColor((HDC)w,RGB(19,19,27));return (LRESULT)edit_brush;
 case WM_TIMER:
  if(TryAcquireSRWLockExclusive(&backend.lock)){status=backend.status;connected=backend.connected;working=backend.working;ReleaseSRWLockExclusive(&backend.lock);}
  if(status.frames!=observed_frames){observed_frames=status.frames;last_frames_tick=GetTickCount64();}
  if(connected||working||closing)InvalidateRect(h,NULL,FALSE);return 0;
 case STUDIO_LOG: {
  StudioLog *entry=(StudioLog*)l;SYSTEMTIME time;GetLocalTime(&time);wchar_t line[512];
  swprintf(line,512,L"%02u:%02u:%02u  %ls",time.wHour,time.wMinute,time.wSecond,entry->text);
  append(debug,&debug_count,entry->level,line);
  HeapFree(GetProcessHeap(),0,entry);InvalidateRect(h,NULL,FALSE);return 0;
 }
 case STUDIO_UPDATE:
  if(TryAcquireSRWLockExclusive(&backend.lock)){status=backend.status;connected=backend.connected;working=backend.working;ReleaseSRWLockExclusive(&backend.lock);}
  InvalidateRect(h,NULL,FALSE);return 0;
 case STUDIO_CLOSED:DestroyWindow(h);return 0;
 case WM_LBUTTONDOWN: {
  int x=MulDiv(GET_X_LPARAM(l),96,dpi),y=MulDiv(GET_Y_LPARAM(l),96,dpi);
  if(number_slider>=0)number_commit();
  for(int k=hit_count-1;k>=0;k--){if(PtInRect(&hits[k].r,(POINT){x,y})) {
   int id=hits[k].id;
   if(id>=HIT_SLIDER&&id<HIT_SLIDER+10){drag=id-HIT_SLIDER;focused_slider=drag;SetCapture(h);SetFocus(h);drag_slider(drag,x);}
   else if(id>=HIT_VALUE&&id<HIT_VALUE+10)begin_number(id-HIT_VALUE);
   else activate(id);break;
  }}return 0;
 }
 case WM_MOUSEMOVE:if(drag>=0)drag_slider(drag,MulDiv(GET_X_LPARAM(l),96,dpi));return 0;
 case WM_LBUTTONUP:if(drag>=0){drag=-1;ReleaseCapture();studio_flush(&backend);}return 0;
 case WM_MOUSEWHEEL: {
  POINT p={GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&p);int d=GET_WHEEL_DELTA_WPARAM(w)>0?3:-3;
  if(tab==2&&MulDiv(p.x,96,dpi)<split&&MulDiv(p.y,96,dpi)<493){setup_scroll-=d/3;
   if(setup_scroll<0)setup_scroll=0;if(setup_scroll>setup_count-4)setup_scroll=setup_count>4?setup_count-4:0;
   InvalidateRect(h,NULL,FALSE);return 0;}
  int *scroll=MulDiv(p.x,96,dpi)>split?&debug_scroll:&terminal_scroll;
  *scroll+=d;if(*scroll<0)*scroll=0;if(*scroll>MAX_LINES*8)*scroll=MAX_LINES*8;
  InvalidateRect(h,NULL,FALSE);return 0;
 }
 case WM_KEYDOWN:
  if((GetKeyState(VK_CONTROL)&0x8000)&&w=='S'){save_setup(NULL);return 0;}
  if(focused_slider>=0&&(w==VK_LEFT||w==VK_RIGHT)){change(focused_slider,value(focused_slider)+(w==VK_LEFT?-1:1)*sliders[focused_slider].step);studio_flush(&backend);return 0;}
  if(w==VK_RETURN){SetFocus(command_edit);return 0;}break;
 case WM_CLOSE:if(!closing){closing=1;studio_action(&backend,ACTION_QUIT,0);InvalidateRect(h,NULL,FALSE);}return 0;
 case WM_DESTROY:KillTimer(h,1);PostQuitMessage(0);return 0;
 }
 return DefWindowProcW(h,message,w,l);
}
static HFONT make_font(int size,int weight,const wchar_t *name) {
 return CreateFontW(-px(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
  OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,name);
}
static HFONT make_mono(int size,int weight) {
 HFONT font=make_font(size,weight,L"Consolas");HDC dc=GetDC(NULL);HGDIOBJ old=SelectObject(dc,font);
 SIZE letter,space;GetTextExtentPoint32W(dc,L"Z",1,&letter);GetTextExtentPoint32W(dc,L" ",1,&space);
 SelectObject(dc,old);ReleaseDC(NULL,dc);
 /* Keep ASCII art aligned on systems without Consolas (including the test VM). */
 if(letter.cx!=space.cx){DeleteObject(font);font=make_font(size,weight,L"Courier New");}
 return font;
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,char *args,int show) {
 (void)previous;(void)args;SetProcessDPIAware();HDC screen=GetDC(NULL);dpi=GetDeviceCaps(screen,LOGPIXELSX);ReleaseDC(NULL,screen);
 font_ui=make_font(15,FW_NORMAL,L"Segoe UI");font_small=make_font(12,FW_MEDIUM,L"Segoe UI");
 font_title=make_font(24,FW_SEMIBOLD,L"Segoe UI");font_logo=make_mono(29,FW_BOLD);
 font_mono=make_mono(14,FW_NORMAL);font_mono_small=make_mono(12,FW_NORMAL);
 edit_brush=CreateSolidBrush(RGB(19,19,27));settings=camera_defaults();
 WNDCLASSEXW cls={0};cls.cbSize=sizeof(cls);cls.hInstance=instance;cls.lpfnWndProc=window_proc;
 cls.hCursor=LoadCursorW(NULL,(LPCWSTR)IDC_ARROW);cls.lpszClassName=L"FrontierOrbitStudioWindow";
 if(!RegisterClassExW(&cls))return 1;
 int w=px(1280),h=px(840);if(w>GetSystemMetrics(SM_CXSCREEN)-30)w=GetSystemMetrics(SM_CXSCREEN)-30;
 if(h>GetSystemMetrics(SM_CYSCREEN)-40)h=GetSystemMetrics(SM_CYSCREEN)-40;
 window=CreateWindowExW(WS_EX_APPWINDOW,cls.lpszClassName,L"Frontier Orbit Studio — Rain 0.8.1",
  WS_POPUP|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_SYSMENU,
  (GetSystemMetrics(SM_CXSCREEN)-w)/2,(GetSystemMetrics(SM_CYSCREEN)-h)/2,w,h,NULL,NULL,instance,NULL);
 if(!window)return 1;
 command_edit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,0,0,window,(HMENU)ID_COMMAND,instance,NULL);
 name_edit=CreateWindowExW(0,L"EDIT",L"My setup",WS_CHILD|ES_AUTOHSCROLL,0,0,0,0,window,(HMENU)ID_NAME,instance,NULL);
 number_edit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|ES_AUTOHSCROLL,0,0,0,0,window,(HMENU)ID_NUMBER,instance,NULL);
 HWND edits[]={command_edit,name_edit,number_edit};
 for(int i=0;i<3;i++){SendMessageW(edits[i],WM_SETFONT,(WPARAM)font_mono,0);SendMessageW(edits[i],EM_SETLIMITTEXT,i==0?240:i==1?60:32,0);
  WNDPROC p=(WNDPROC)SetWindowLongPtrW(edits[i],GWLP_WNDPROC,(LONG_PTR)edit_proc);if(!original_edit)original_edit=p;}
 wchar_t core[MAX_PATH];GetModuleFileNameW(NULL,core,MAX_PATH);wchar_t *slash=wcsrchr(core,L'\\');
 if(!slash)return 1;slash[1]=0;wcscpy(app_folder,core);wcscat(core,L"FrontierOrbitRain08.dll");
 if(!setup_folder(folder)){MessageBoxW(window,L"Cannot create the setup folder in LocalAppData.",L"Orbit Studio",MB_ICONERROR);return 1;}
 refresh_setups();
 if(!studio_backend_start(&backend,window,core))return 1;
 terminal_line(1,L"Frontier Orbit Studio / Rain Edition");
 terminal_line(0,L"Open the game normally. Enter the city. Type start.");
 terminal_line(0,L"Type help for commands. Click values for precise input.");
 studio_log(&backend,0,L"[ready] Waiting for a manual start command.");
 studio_log(&backend,0,L"[core] Rain 20260929141936_cb31ac5 / HD only.");
 layout();SetTimer(window,1,100,NULL);ShowWindow(window,show);UpdateWindow(window);SetFocus(command_edit);
 MSG message;while(GetMessageW(&message,NULL,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
 if(backend.thread){WaitForSingleObject(backend.thread,1000);CloseHandle(backend.thread);}
 HFONT fonts[]={font_ui,font_small,font_title,font_mono,font_mono_small,font_logo};for(int i=0;i<6;i++)DeleteObject(fonts[i]);
 if(setups)HeapFree(GetProcessHeap(),0,setups);DeleteObject(edit_brush);return 0;
}
