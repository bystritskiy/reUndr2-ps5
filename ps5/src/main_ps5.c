/* Native PS5 entry for reUndr 2. MIT. */
#include "platform/win32_compat.h"
#include "sha256.h"
#include "game_revision.h"
#include "xbe_check.h"
#include "disc_image.h"
#include "screen_ps5.h"
#include <SDL3/SDL.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <strings.h>
extern int nfsu2_game_main(int,char **);
extern int sceSystemServiceHideSplashScreen(void);
extern int sceKernelDebugOutText(int,const char *);
extern int scePthreadSetaffinity(unsigned long,unsigned long);
extern int sceKernelSendNotificationRequest(int,void *,size_t,int);
extern int sceKernelUsleep(unsigned);
extern int nfsu2_movie_skip_down(void);
static void notify(const char *text) {
 struct { char reserved[45];char message[3075]; } note;memset(&note,0,sizeof note);
 snprintf(note.message,sizeof note.message,"reUndr 2: %s",text);sceKernelSendNotificationRequest(0,&note,sizeof note,0);
}
static void *game(void *unused) { (void)unused;return (void *)(intptr_t)nfsu2_game_main(0,NULL); }
/* Any default.xbe is run; the log says whether it is the build the port was made from. */
static void log_xbe(const char *path) {
 static const Nfsu2XbeSection ref[]=NFSU2_XBE_SECTIONS;char sha[65];
 int known=nfsu2_xbe_check(path,NFSU2_XBE_SHA256,ref,sizeof ref/sizeof *ref,sha);
 fprintf(stderr,"[XBE] %s SHA-256 %s: %s\n",path,sha[0]?sha:"unreadable",known?"supported NTSC-U build":"other build, may not work");
}
static int valid(const char *dir) {
 char path[1024];struct stat st;
 snprintf(path,sizeof path,"%s/default.xbe",dir);
 if(stat(path,&st)||!S_ISREG(st.st_mode))return 0;
 const char *parts[]={"B3","NFSUNDER"};
 for(unsigned i=0;i<sizeof parts/sizeof *parts;i++) { snprintf(path,sizeof path,"%s/%s",dir,parts[i]); if(stat(path,&st)||!S_ISDIR(st.st_mode))return 0; }
 return 1;
}
static void remove_tree(const char *path) {
 DIR *d=opendir(path);
 if(!d) { unlink(path);return; }
 for(struct dirent *e;(e=readdir(d));) {
  if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
  char child[1024];snprintf(child,sizeof child,"%s/%s",path,e->d_name);remove_tree(child);
 }
 closedir(d);rmdir(path);
}
#define ACCENT 0xff8ed6f0
static const Nfsu2ScreenLine UNPACKING[]={
 {260,7,ACCENT,"Unpacking your disc image"},
 {380,4,0,"This happens once. The game needs about 2.5 GB on the console."},
 {440,4,0,"When it is done, the game starts and you can delete the .iso."}};
static void unpack_progress(unsigned percent) {
 nfsu2_screen_show(UNPACKING,sizeof UNPACKING/sizeof *UNPACKING,(int)percent);
}
static int image_error;
/* 0 when dir holds no image of the supported disc, 1 once it is unpacked. */
static int unpack_image_in(const char *dir,const char *target) {
 DIR *d=opendir(dir);
 if(!d)return 0;
 char part[1024],image[1024];int unpacked=0;
 snprintf(part,sizeof part,"%s.part",target);
 for(struct dirent *e;!unpacked&&(e=readdir(d));) {
  size_t length=strlen(e->d_name);
  if(length<5||strcasecmp(e->d_name+length-4,".iso")||e->d_name[0]=='.')continue;
  snprintf(image,sizeof image,"%s/%s",dir,e->d_name);
  remove_tree(part);
  int result=nfsu2_image_extract(image,part,NULL,unpack_progress);
  if(result==NFSU2_IMAGE_OK) {
   if(rename(part,target))result=NFSU2_IMAGE_WRITE;
   else unpacked=1;
  }
  if(result==NFSU2_IMAGE_DAMAGED||result==NFSU2_IMAGE_WRITE)image_error=result;
  fprintf(stderr,"[IMAGE] %s: %d\n",image,result);
 }
 closedir(d);remove_tree(part);
 return unpacked;
}
static int unpack_image(const char *target) {
 char dir[64];
 if(unpack_image_in("/data/nfsu2",target))return 1;
 for(int i=0;i<8;i++) {
  snprintf(dir,sizeof dir,"/mnt/usb%d/nfsu2",i);if(unpack_image_in(dir,target))return 1;
  snprintf(dir,sizeof dir,"/mnt/usb%d",i);if(unpack_image_in(dir,target))return 1;
 }
 return 0;
}
/* Says what is wrong and where things go, then waits for Cross. */
__attribute__((noreturn)) static void no_game(void) {
 static const Nfsu2ScreenLine damaged[]={
  {260,7,ACCENT,"The disc image is damaged"},
  {380,4,0,"Make it again from your disc, or copy it to the console again."},
  {860,4,0,"Press Cross to close."}};
 static const Nfsu2ScreenLine no_room[]={
  {260,7,ACCENT,"Not enough free space"},
  {380,4,0,"The game needs about 2.5 GB free on the console."},
  {860,4,0,"Press Cross to close."}};
 static const Nfsu2ScreenLine missing[]={
  {260,7,ACCENT,"Game not found"},
  {380,4,0,"reUndr 2 runs your own copy of the original Xbox game."},
  {450,4,0,"Copy your disc image (.iso) to one of these places:"},
  {520,4,ACCENT,"/data/nfsu2/"},
  {570,4,ACCENT,"/mnt/usb0/nfsu2/  or  /mnt/usb0/   (usb0 to usb7)"},
  {650,4,0,"Or copy the unpacked disc to this folder:"},
  {700,4,ACCENT,"/data/nfsu2/game/"},
  {860,4,0,"Then start reUndr 2 again. Press Cross to close."}};
 const Nfsu2ScreenLine *lines=missing;unsigned count=sizeof missing/sizeof *missing;
 const char *note="game files not found. Copy your disc image (.iso) to /data/nfsu2. See README.txt.";
 if(image_error==NFSU2_IMAGE_DAMAGED) { lines=damaged;count=sizeof damaged/sizeof *damaged;note="the disc image is damaged. See README.txt."; }
 else if(image_error==NFSU2_IMAGE_WRITE) { lines=no_room;count=sizeof no_room/sizeof *no_room;note="no room for the game: it needs 2.5 GB. See README.txt."; }
 notify(note);
 sceKernelDebugOutText(0,"[NFSU2] Missing game. Expected /app0/game or /data/nfsu2/game\n");
 fprintf(stderr,"Missing Xbox disc: no default.xbe with the game folders\n");fflush(stderr);
 if(nfsu2_screen_show(lines,count,-1)==0) {
  while(!nfsu2_movie_skip_down())sceKernelUsleep(50000);
 }
 else sceKernelUsleep(200000);
 _exit(0);
}
struct log_sink { int from,to; };
static void *log_writer(void *arg) {
 struct log_sink *s=arg; char buf[16384];
 for(;;) { ssize_t n=read(s->from,buf,sizeof buf);if(n<0&&errno==EINTR)continue;if(n<=0)break;
   for(ssize_t done=0;done<n;) { ssize_t w=write(s->to,buf+done,n-done);if(w<0&&errno==EINTR)continue;if(w<=0)break;done+=w; }
 }
 close(s->from);close(s->to);return NULL;
}
static FILE *open_log(const char *path) {
 static struct log_sink sinks[2];static unsigned used;
 int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0666),ends[2];if(fd<0)return NULL;
 if(used<2&&pipe(ends)==0) {
   FILE *stream=fdopen(ends[1],"w");pthread_t th;
   struct log_sink *sink=&sinks[used];*sink=(struct log_sink){ends[0],fd};
   if(stream&&pthread_create(&th,NULL,log_writer,sink)==0) { used++;pthread_detach(th);setvbuf(stream,NULL,_IOLBF,0);return stream; }
   if(stream)fclose(stream);else close(ends[1]);close(ends[0]);
 }
 return fdopen(fd,"w");
}
static void load_settings(void) {
 FILE *f=fopen("/data/nfsu2/PPSA99027/nfsu2_env.txt","r");if(!f)return;
 char line[512];
 while(fgets(line,sizeof line,f)) { char *key=line;while(*key==' '||*key=='\t')key++;
   if(*key=='#'||*key=='\n')continue;char *eq=strchr(key,'=');if(!eq)continue;*eq++=0;
   char *end=key+strlen(key);while(end>key&&(end[-1]==' '||end[-1]=='\t'))*--end=0;
   while(*eq==' '||*eq=='\t')eq++;eq[strcspn(eq,"\r\n")]=0;if(*key)setenv(key,eq,1);
 }fclose(f);
}
int main(int argc,char **argv) {
 (void)argc;(void)argv;
 scePthreadSetaffinity((unsigned long)pthread_self(),0x1fff);
 mkdir("/data/nfsu2",0777);mkdir("/data/nfsu2/PPSA99027",0777);
 mkdir("/data/nfsu2/PPSA99027/save",0777);
 chmod("/data/nfsu2",0777);chmod("/data/nfsu2/PPSA99027",0777);
 FILE *out=open_log("/data/nfsu2/PPSA99027/stdout.log"),*err=open_log("/data/nfsu2/PPSA99027/stderr.log");
 if(!out||!err)return 1;stdout=out;stderr=err;load_settings();
 sceKernelDebugOutText(0,"[NFSU2] PS5 native boot 01.000.000\n");
 SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"offscreen");
 setenv("RECOMP_GIL_EAGER","1",0);setenv("RECOMP_CULL","0",0);
 setenv("RECOMP_GL_SCALE","4.5",0);setenv("RECOMP_VK_HEADLESS","0",1);
 setenv("RECOMP_QUIET","1",0);setenv("NFSU2_FPS_CAP","60",0);
 char dir[1024]="/app0/game";
 if(!valid(dir))snprintf(dir,sizeof dir,"/data/nfsu2/game");
 if(!valid(dir)) {
   int found=0;
   for(int i=0;i<8&&!found;i++) { snprintf(dir,sizeof dir,"/mnt/usb%d/nfsu2/game",i);found=valid(dir); }
   if(!found&&unpack_image("/data/nfsu2/game")) { snprintf(dir,sizeof dir,"/data/nfsu2/game");found=valid(dir); }
   if(!found)no_game();
   nfsu2_screen_close();
 }
 { char xbe[1100];snprintf(xbe,sizeof xbe,"%s/default.xbe",dir);log_xbe(xbe); }
 setenv("NFSU2_GAME_DIR",dir,1);
 sceSystemServiceHideSplashScreen();
 pthread_attr_t attr;pthread_t th;void *rc=NULL;
 pthread_attr_init(&attr);pthread_attr_setstacksize(&attr,16*1024*1024);
 int thread_error=pthread_create(&th,&attr,game,NULL);pthread_attr_destroy(&attr);
 if(thread_error)return 1;
 pthread_join(th,&rc);return (int)(intptr_t)rc;
}
