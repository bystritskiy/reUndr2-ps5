#include "disc_image.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SECTOR 2048
#define VIDEO 0x18300000ull
static int calls,last;
static void progress(unsigned percent) { assert(percent>0&&percent<=100&&(int)percent>last);last=percent;calls++; }
static int accept_any(const char *xbe) { struct stat st;return !stat(xbe,&st)&&st.st_size==3; }
static int refuse(const char *xbe) { (void)xbe;return 0; }

static size_t entry(unsigned char *t,size_t at,uint16_t left,uint16_t right,uint32_t sector,uint32_t size,int dir,const char *name)
{
 size_t n=strlen(name);
 memcpy(t+at,&left,2);memcpy(t+at+2,&right,2);memcpy(t+at+4,&sector,4);memcpy(t+at+8,&size,4);
 t[at+12]=dir?0x10:0x20;t[at+13]=(unsigned char)n;memcpy(t+at+14,name,n);
 return (at+14+n+3)&~(size_t)3;
}

/* Root: default.xbe, with FRONTEND on its left and zebra.bin on its right;
 * FRONTEND holds a.bin, and an empty EMPTY sits beside it. */
static void make(const char *path,uint64_t base,int damaged,int wrong_magic)
{
 FILE *f=fopen(path,"wb");assert(f);
 unsigned char volume[SECTOR]={0};
 memcpy(volume,wrong_magic?"MICROSOFT*XBOX*MEDIX":"MICROSOFT*XBOX*MEDIA",20);
 uint32_t root=33,root_size=SECTOR,dir=34,dir_size=SECTOR;
 memcpy(volume+20,&root,4);memcpy(volume+24,&root_size,4);
 unsigned char t[SECTOR],d[SECTOR];
 memset(t,0xFF,sizeof t);memset(d,0xFF,sizeof d);
 size_t a=0,b=entry(t,a,0,0,0,0,0,"default.xbe");
 size_t c=entry(t,b,0,0,dir,dir_size,1,"FRONTEND");
 size_t e=entry(t,c,0,0,0,0,1,"EMPTY");
 entry(t,e,0,0,damaged?4000000:36,5,0,"zebra.bin");
 uint16_t left=(uint16_t)(b/4),right=(uint16_t)(e/4),next=(uint16_t)(c/4);
 entry(t,a,left,right,35,3,0,"default.xbe");
 entry(t,b,0,next,dir,dir_size,1,"FRONTEND");
 entry(d,0,0,0,37,4,0,"a.bin");
 assert(!fseeko(f,(off_t)(base+0x10000),SEEK_SET));fwrite(volume,1,SECTOR,f);
 assert(!fseeko(f,(off_t)(base+33*SECTOR),SEEK_SET));fwrite(t,1,SECTOR,f);fwrite(d,1,SECTOR,f);
 char data[3*SECTOR]={0};memcpy(data,"XBE",3);memcpy(data+SECTOR,"zebra",5);memcpy(data+2*SECTOR,"aaaa",4);
 fwrite(data,1,sizeof data,f);
 fclose(f);
}

static void expect(const char *path,const char *text)
{
 char got[32]={0};FILE *f=fopen(path,"rb");assert(f);size_t n=fread(got,1,sizeof got,f);fclose(f);
 assert(n==strlen(text)&&!memcmp(got,text,n));
}

static void clean(const char *t)
{
 char path[512];
 const char *leaves[]={"default.xbe","zebra.bin","FRONTEND/a.bin"};
 for(unsigned i=0;i<3;i++) { snprintf(path,sizeof path,"%s/%s",t,leaves[i]);unlink(path); }
 const char *dirs[]={"FRONTEND","EMPTY"};
 for(unsigned i=0;i<2;i++) { snprintf(path,sizeof path,"%s/%s",t,dirs[i]);rmdir(path); }
 rmdir(t);
}

int main(void)
{
 char temp[]="/tmp/nfsu2-image-XXXXXX";assert(mkdtemp(temp));
 char image[256],target[256],path[512];
 snprintf(image,sizeof image,"%s/disc.iso",temp);snprintf(target,sizeof target,"%s/game",temp);
 uint64_t bases[]={0,VIDEO};
 for(unsigned i=0;i<2;i++) {
  make(image,bases[i],0,0);last=0;calls=0;
  assert(nfsu2_image_extract(image,target,accept_any,progress)==NFSU2_IMAGE_OK);
  snprintf(path,sizeof path,"%s/default.xbe",target);expect(path,"XBE");
  snprintf(path,sizeof path,"%s/zebra.bin",target);expect(path,"zebra");
  snprintf(path,sizeof path,"%s/FRONTEND/a.bin",target);expect(path,"aaaa");
  snprintf(path,sizeof path,"%s/EMPTY",target);struct stat st;assert(!stat(path,&st)&&S_ISDIR(st.st_mode));
  assert(last==100&&calls);
  clean(target);unlink(image);
 }
 make(image,0,0,0);
 assert(nfsu2_image_extract(image,target,refuse,NULL)==NFSU2_IMAGE_WRONG_DISC);
 snprintf(path,sizeof path,"%s/zebra.bin",target);assert(access(path,F_OK));
 clean(target);unlink(image);
 make(image,0,1,0);
 assert(nfsu2_image_extract(image,target,accept_any,NULL)==NFSU2_IMAGE_DAMAGED);
 clean(target);unlink(image);
 make(image,0,0,1);
 assert(nfsu2_image_extract(image,target,accept_any,NULL)==NFSU2_IMAGE_NOT_A_DISC);
 assert(nfsu2_image_extract(temp,target,accept_any,NULL)==NFSU2_IMAGE_NOT_A_DISC);
 unlink(image);rmdir(temp);
 puts("Disc image: trimmed and full layouts, wrong disc, damaged and foreign images PASS");
}
