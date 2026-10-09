#ifndef ORBIT_PROFILE_CODEC_H
#define ORBIT_PROFILE_CODEC_H
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include "../../control-core/src/camera_core.h"
typedef struct {char name[128];CameraSettings camera;} StudioProfile;
typedef struct {const char *p;int bad;} JsonReader;
static inline void json_space(JsonReader *j){while(isspace((unsigned char)*j->p))j->p++;}
static inline int json_take(JsonReader *j,char c){json_space(j);if(*j->p!=c){j->bad=1;return 0;}j->p++;return 1;}
static inline int json_hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
static inline unsigned json_code(JsonReader *j){unsigned v=0;for(int i=0;i<4;i++){int n=json_hex(*j->p);if(n<0){j->bad=1;return 0;}j->p++;v=v*16+(unsigned)n;}return v;}
static inline int json_string(JsonReader *j,char *out,size_t size) {
 if(!json_take(j,'"'))return 0;
 size_t n=0;
 while(*j->p&&*j->p!='"') {
  unsigned char bytes[4];unsigned length=1;unsigned c=(unsigned char)*j->p++;
  if(c<32){j->bad=1;return 0;}
  if(c=='\\') {
   c=(unsigned char)*j->p++;if(!c){j->bad=1;return 0;}
   switch(c) {
   case '"':case '\\':case '/':break;
   case 'n':c='\n';break;case 'r':c='\r';break;case 't':c='\t';break;
   case 'b':c='\b';break;case 'f':c='\f';break;
   case 'u': {
    c=json_code(j);if(j->bad)return 0;
    if(c>=0xd800&&c<=0xdbff) {
     if(j->p[0]!='\\'||j->p[1]!='u'){j->bad=1;return 0;}j->p+=2;
     unsigned low=json_code(j);if(low<0xdc00||low>0xdfff){j->bad=1;return 0;}
     c=0x10000+((c-0xd800)<<10)+(low-0xdc00);
    }else if(c>=0xdc00&&c<=0xdfff){j->bad=1;return 0;}
    if(c==0){j->bad=1;return 0;}
    if(c<0x80)bytes[0]=(unsigned char)c;
    else if(c<0x800){length=2;bytes[0]=0xc0|(c>>6);bytes[1]=0x80|(c&63);}
    else if(c<0x10000){length=3;bytes[0]=0xe0|(c>>12);bytes[1]=0x80|((c>>6)&63);bytes[2]=0x80|(c&63);}
    else {length=4;bytes[0]=0xf0|(c>>18);bytes[1]=0x80|((c>>12)&63);bytes[2]=0x80|((c>>6)&63);bytes[3]=0x80|(c&63);}
    goto append;
   }
   default:j->bad=1;return 0;
   }
  }
  bytes[0]=(unsigned char)c;
append:
  if(n+length>=size){j->bad=1;return 0;}memcpy(out+n,bytes,length);n+=length;
 }
 out[n]=0;return json_take(j,'"');
}
static inline int json_number(JsonReader *j,double *out) {
 json_space(j);const char *begin=j->p;if(*j->p=='-')j->p++;
 if(*j->p=='0')j->p++;
 else if(*j->p>='1'&&*j->p<='9'){while(isdigit((unsigned char)*j->p))j->p++;}
 else {j->bad=1;return 0;}
 if(*j->p=='.'){j->p++;if(!isdigit((unsigned char)*j->p)){j->bad=1;return 0;}while(isdigit((unsigned char)*j->p))j->p++;}
 if(*j->p=='e'||*j->p=='E'){j->p++;if(*j->p=='+'||*j->p=='-')j->p++;if(!isdigit((unsigned char)*j->p)){j->bad=1;return 0;}while(isdigit((unsigned char)*j->p))j->p++;}
 char *end;*out=strtod(begin,&end);if(end!=j->p||!isfinite(*out)){j->bad=1;return 0;}return 1;
}
static const char *const profile_keys[]={"yaw_speed","pitch_speed","min_pitch","max_pitch",
 "reset_pitch","response","deadzone","distance_scale","height_offset","shoulder_offset",
 "gamepad_index","preview"};
static inline int profile_camera(JsonReader *j,CameraSettings *s) {
 if(!json_take(j,'{'))return 0;
 unsigned mask=0;
 for(int count=0;count<12;count++) {
  char key[40];double value;
  if(count&&!json_take(j,','))return 0;
  if(!json_string(j,key,sizeof(key))||!json_take(j,':')||!json_number(j,&value))return 0;
  int field=-1;for(int i=0;i<12;i++)if(!strcmp(key,profile_keys[i]))field=i;
  if(field<0||(mask&(1u<<field))){j->bad=1;return 0;}mask|=1u<<field;
  if(field<10){float f=(float)value;memcpy((unsigned char*)s+field*sizeof(float),&f,sizeof(f));}
  else if(field==10){if(value< -1||value>3||floor(value)!=value)return 0;s->gamepad_index=(int32_t)value;}
  else {if(value!=0&&value!=1)return 0;s->preview=(uint32_t)value;}
 }
 return mask==4095&&json_take(j,'}')&&camera_settings_valid(s);
}
static inline int profile_parse(const char *text,StudioProfile *out) {
 JsonReader j={text,0};StudioProfile p={0};unsigned mask=0;
 if(!json_take(&j,'{'))return 0;
 for(int i=0;i<3;i++) {
  if(i&&!json_take(&j,','))return 0;
  char key[20];
  if(!json_string(&j,key,sizeof(key))||!json_take(&j,':'))return 0;
  unsigned bit=!strcmp(key,"schema")?1:!strcmp(key,"name")?2:!strcmp(key,"camera")?4:0;
  if(!bit||(mask&bit))return 0;
  mask|=bit;
  if(bit==1){double version;if(!json_number(&j,&version)||version!=1)return 0;}
  else if(bit==2){if(!json_string(&j,p.name,sizeof(p.name))||!*p.name)return 0;}
  else if(!profile_camera(&j,&p.camera))return 0;
 }
 if(!json_take(&j,'}'))return 0;
 json_space(&j);if(*j.p||j.bad)return 0;
 *out=p;return 1;
}
static inline int profile_format(const StudioProfile *p,char *out,size_t capacity) {
 if(!memchr(p->name,0,sizeof(p->name))||!camera_settings_valid(&p->camera)||!*p->name)return 0;
 char escaped[800];size_t n=0;
 for(const unsigned char *c=(const unsigned char*)p->name;*c;c++) {
  if(n+7>=sizeof(escaped))return 0;
  if(*c<32){int k=snprintf(escaped+n,sizeof(escaped)-n,"\\u%04x",*c);n+=(size_t)k;}
  else {if(*c=='"'||*c=='\\')escaped[n++]='\\';escaped[n++]=(char)*c;}
 }escaped[n]=0;
 int k=snprintf(out,capacity,"{\n  \"schema\": 1,\n  \"name\": \"%s\",\n  \"camera\": {\n",escaped);
 if(k<0||(size_t)k>=capacity)return 0;
 n=(size_t)k;
 for(int i=0;i<12;i++) {
  double v;if(i<10){float f;memcpy(&f,(const unsigned char*)&p->camera+i*sizeof(float),sizeof(f));v=f;}
  else v=i==10?(double)p->camera.gamepad_index:(double)p->camera.preview;
  k=snprintf(out+n,capacity-n,"    \"%s\": %.9g%s\n",profile_keys[i],v,i==11?"":",");
  if(k<0||(size_t)k>=capacity-n)return 0;
  n+=(size_t)k;
 }
 k=snprintf(out+n,capacity-n,"  }\n}\n");return k>=0&&(size_t)k<capacity-n;
}
#endif
