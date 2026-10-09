#ifdef NDEBUG
#error assertions must remain enabled
#endif
#include <assert.h>
#include <stdio.h>
#include "../src/profile_codec.h"
int main(void) {
 StudioProfile p={0},q;p.camera=camera_defaults();strcpy(p.name,"A \"wide\" \\ setup / Русский");
 p.camera.distance_scale=1.234567f;p.camera.height_offset=-71.25f;p.camera.preview=0;
 char text[4096];assert(profile_format(&p,text,sizeof(text))&&profile_parse(text,&q));
 assert(!strcmp(p.name,q.name)&&memcmp(&p.camera,&q.camera,sizeof(p.camera))==0);
 for(size_t i=0;i<strlen(text)-2;i++){char truncated[4096];memcpy(truncated,text,i);truncated[i]=0;assert(!profile_parse(truncated,&q));}
 char bad[4096];strcpy(bad,text);char *s=strstr(bad,"\"schema\": 1");assert(s);s[10]='2';assert(!profile_parse(bad,&q));
 strcpy(bad,text);s=strstr(bad,"\"preview\": 0");assert(s);s[11]='2';assert(!profile_parse(bad,&q));
 strcpy(bad,text);strcat(bad,"trailing");assert(!profile_parse(bad,&q));
 char escaped[4096];strcpy(escaped,text);s=strstr(escaped,"A ");assert(s);
 char *tail=strstr(s,"\",\n  \"camera\"");assert(tail);char rest[4096];strcpy(rest,tail);
 strcpy(s,"\\u042f\\uD83D\\uDE80");strcat(s,rest);assert(profile_parse(escaped,&q));
 assert(!strcmp(q.name,"Я🚀"));
 assert(!profile_format(&p,text,10));
 p.camera.distance_scale=NAN;assert(!profile_format(&p,text,sizeof(text)));
 puts("PASS: JSON profiles, exact float round-trip, UTF8/escaped Unicode, truncation, schema/range and trailing-data rejection.");
}
