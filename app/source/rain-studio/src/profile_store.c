#include "profile_store.h"
#include <shlobj.h>
#include <wchar.h>
int setup_folder(wchar_t path[MAX_PATH]) {
 if(!GetModuleFileNameW(NULL,path,MAX_PATH))return 0;
 wchar_t *slash=wcsrchr(path,L'\\');if(!slash)return 0;slash[1]=0;
 if(wcslen(path)>MAX_PATH-10)return 0;
 wcscat(path,L"setups");return CreateDirectoryW(path,NULL)||GetLastError()==ERROR_ALREADY_EXISTS;
}
int setup_read(const wchar_t *path,StudioProfile *profile) {
 HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
 if(file==INVALID_HANDLE_VALUE)return 0;
 DWORD size=GetFileSize(file,NULL),read=0;char text[16385];
 int ok=size>0&&size<=16384&&ReadFile(file,text,size,&read,NULL)&&read==size;CloseHandle(file);
 if(!ok||memchr(text,0,size))return 0;text[size]=0;
 const char *p=text;if(size>=3&&(unsigned char)p[0]==0xef&&(unsigned char)p[1]==0xbb&&(unsigned char)p[2]==0xbf)p+=3;
 StudioProfile value;if(!profile_parse(p,&value))return 0;
 wchar_t name[128];if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.name,-1,name,128))return 0;
 *profile=value;return 1;
}
int setup_write(const wchar_t *path,const StudioProfile *profile) {
 char text[4096];if(!profile_format(profile,text,sizeof(text))||wcslen(path)>MAX_PATH-5)return 0;
 wchar_t temp[MAX_PATH];swprintf(temp,MAX_PATH,L"%ls.tmp",path);
 HANDLE file=CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
 if(file==INVALID_HANDLE_VALUE)return 0;
 DWORD written=0;DWORD bytes=(DWORD)strlen(text);
 int ok=WriteFile(file,text,bytes,&written,NULL)&&written==bytes&&FlushFileBuffers(file);CloseHandle(file);
 if(ok)ok=MoveFileExW(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
 if(!ok)DeleteFileW(temp);return ok;
}
static int compare_setup(const void *a,const void *b) {
 return _wcsicmp(((const SavedSetup*)a)->name,((const SavedSetup*)b)->name);
}
int setup_list(const wchar_t *folder,SavedSetup *setups,int capacity) {
 wchar_t pattern[MAX_PATH];if(wcslen(folder)>MAX_PATH-8)return 0;
 swprintf(pattern,MAX_PATH,L"%ls\\*.json",folder);WIN32_FIND_DATAW data;
 HANDLE find=FindFirstFileW(pattern,&data);if(find==INVALID_HANDLE_VALUE)return 0;int count=0;
 do {
  if((data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||(setups&&count>=capacity))continue;
  SavedSetup item={0};if(wcslen(folder)+wcslen(data.cFileName)+2>=MAX_PATH)continue;
  swprintf(item.path,MAX_PATH,L"%ls\\%ls",folder,data.cFileName);
  if(setup_read(item.path,&item.data)&&MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,
   item.data.name,-1,item.name,128)){if(setups)setups[count]=item;count++;}
 }while(FindNextFileW(find,&data));FindClose(find);
 if(setups)qsort(setups,(size_t)count,sizeof(*setups),compare_setup);return count;
}
