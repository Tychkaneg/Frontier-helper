#ifndef ORBIT_PROFILE_STORE_H
#define ORBIT_PROFILE_STORE_H
#include "studio_backend.h"
#include "profile_codec.h"
typedef struct {wchar_t path[MAX_PATH],name[128];StudioProfile data;} SavedSetup;
int setup_folder(wchar_t path[MAX_PATH]);
int setup_list(const wchar_t *folder,SavedSetup *setups,int capacity);
int setup_read(const wchar_t *path,StudioProfile *profile);
int setup_write(const wchar_t *path,const StudioProfile *profile);
#endif
