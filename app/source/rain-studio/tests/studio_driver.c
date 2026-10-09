#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void command(HWND window,const wchar_t *input) {
 HWND edit=GetDlgItem(window,101);assert(edit);SetWindowTextW(edit,input);
 SendMessageW(edit,WM_KEYDOWN,VK_RETURN,0);
}
static void screenshot(HWND window,const char *path) {
 RECT r;GetWindowRect(window,&r);int width=r.right-r.left,height=r.bottom-r.top;
 HDC screen=GetWindowDC(window),dc=CreateCompatibleDC(screen);
 BITMAPINFO info={0};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
 info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
 info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
 void *pixels=NULL;HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,NULL,0);assert(bitmap&&pixels);
 HGDIOBJ old=SelectObject(dc,bitmap);assert(BitBlt(dc,0,0,width,height,screen,0,0,SRCCOPY));
 BITMAPFILEHEADER file={0};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info.bmiHeader);
 file.bfSize=file.bfOffBits+(DWORD)(width*height*4);
 FILE *out=fopen(path,"wb");assert(out);
 assert(fwrite(&file,1,sizeof(file),out)==sizeof(file));
 assert(fwrite(&info.bmiHeader,1,sizeof(info.bmiHeader),out)==sizeof(info.bmiHeader));
 assert(fwrite(pixels,1,(size_t)(width*height*4),out)==(size_t)(width*height*4));fclose(out);
 SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(window,screen);
 printf("Screenshot %dx%d: %s\n",width,height,path);
}
int main(int argc,char **argv) {
 int wide_count=0;wchar_t **wide_args=CommandLineToArgvW(GetCommandLineW(),&wide_count);assert(wide_args&&wide_count==argc);
 if(argc==2&&!strcmp(argv[1],"cleanup")) {
  const wchar_t *classes[]={L"FrontierOrbitStudioWindow",L"FakeRainWindow"};
  for(int k=0;k<2;k++)for(int n=0;n<10;n++) {
   HWND h=FindWindowW(classes[k],NULL);if(!h)break;PostMessageW(h,WM_CLOSE,0,0);
   for(int i=0;i<400&&IsWindow(h);i++)Sleep(20);assert(!IsWindow(h));
  }
  LocalFree(wide_args);return 0;
 }
 if(argc==2&&!strncmp(argv[1],"fixture-",8)) {
  HWND fixture=FindWindowW(L"FakeRainWindow",NULL);assert(fixture);
  if(!strcmp(argv[1],"fixture-map"))SendMessageW(fixture,WM_APP+10,0,0);
  else if(!strcmp(argv[1],"fixture-state"))SendMessageW(fixture,WM_APP+11,0,0);
  else if(!strcmp(argv[1],"fixture-close")){PostMessageW(fixture,WM_CLOSE,0,0);
   for(int i=0;i<100&&IsWindow(fixture);i++)Sleep(20);assert(!IsWindow(fixture));}
  else return 2;return 0;
 }
 HWND window=NULL;for(int i=0;i<100&&!window;i++){window=FindWindowW(L"FrontierOrbitStudioWindow",NULL);if(!window)Sleep(20);}assert(window);
 if(argc==3&&!strcmp(argv[1],"command"))command(window,wide_args[2]);
 else if(argc==3&&!strcmp(argv[1],"command-file")) {
  FILE *f=fopen(argv[2],"rb");assert(f);char utf8[1024];size_t n=fread(utf8,1,sizeof(utf8)-1,f);fclose(f);utf8[n]=0;
  wchar_t wide[256];assert(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8,-1,wide,256));command(window,wide);
 }
 else if(argc==3&&!strcmp(argv[1],"summary")) {
  command(window,L"files");Sleep(150);
  SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(610,438));
  SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(610,438));
  assert(OpenClipboard(NULL));HANDLE h=GetClipboardData(CF_UNICODETEXT);assert(h);
  const wchar_t *wide=GlobalLock(h);assert(wide);char utf8[8192];
  int count=WideCharToMultiByte(CP_UTF8,0,wide,-1,utf8,sizeof(utf8),NULL,NULL);assert(count);
  FILE *f=fopen(argv[2],"wb");assert(f);fwrite(utf8,1,(size_t)count-1,f);fclose(f);
  GlobalUnlock(h);CloseClipboard();puts(utf8);
 }
 else if(argc==3&&!strcmp(argv[1],"screenshot")){SetForegroundWindow(window);Sleep(150);screenshot(window,argv[2]);}
 else if(argc==4&&!strcmp(argv[1],"click")) {
  int x=atoi(argv[2]),y=atoi(argv[3]);SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(x,y));
 }else if(argc==6&&!strcmp(argv[1],"drag")) {
  int x=atoi(argv[2]),y=atoi(argv[3]),end=atoi(argv[4]),ey=atoi(argv[5]);
  SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
  for(int i=1;i<=20;i++){SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(x+(end-x)*i/20,y+(ey-y)*i/20));Sleep(5);}
  SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(end,ey));
 }else if(argc==2&&!strcmp(argv[1],"close")){PostMessageW(window,WM_CLOSE,0,0);
  for(int i=0;i<350&&IsWindow(window);i++)Sleep(20);assert(!IsWindow(window));}
 else return 2;LocalFree(wide_args);return 0;
}
