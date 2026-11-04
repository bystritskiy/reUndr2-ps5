#include "kernel/kernel.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
void xbox_log(int level,const char *system,const char *format,...) { (void)level;(void)system;(void)format; }
const char *xbox_LookupSymbolicLink(const char *name) {
 return !strcmp(name,"\\??\\U:")?"\\Device\\Harddisk0\\Partition1\\UDATA\\4541005A":NULL;
}
int main(void) {
 char temp[]="/tmp/nfsu2-path-XXXXXX";assert(mkdtemp(temp));char game[512],save[512],path[1024],want[1024];
 snprintf(game,sizeof game,"%s/game",temp);snprintf(save,sizeof save,"%s/save",temp);
 xbox_path_init(game,save);
 assert(xbox_translate_path("\\Device\\Harddisk0\\Partition1\\UDATA\\4541005A\\profile.nfs",path,sizeof path));
 snprintf(want,sizeof want,"%s/UserData/4541005A/profile.nfs",save);assert(!strcmp(path,want));
 assert(xbox_translate_path("\\Device\\Harddisk0\\Partition1\\TDATA\\4541005A\\foo",path,sizeof path));
 snprintf(want,sizeof want,"%s/TitleData/4541005A/foo",save);assert(!strcmp(path,want));
 assert(xbox_translate_path("U:\\profile.nfs",path,sizeof path));
 snprintf(want,sizeof want,"%s/UserData/4541005A/profile.nfs",save);assert(!strcmp(path,want));
 assert(xbox_translate_path("\\Device\\Harddisk0\\Partition1\\UDATA",path,sizeof path));
 snprintf(want,sizeof want,"%s/UserData",save);assert(!strcmp(path,want));
 assert(xbox_translate_path("D:\\FRONTEND\\test.bin",path,sizeof path));
 snprintf(want,sizeof want,"%s/FRONTEND/test.bin",game);assert(!strcmp(path,want));
 const char *files[]={"Partition0.img","Partition1.img","Partition2.img","Partition3.img","Partition4.img","Partition5.img"};
 for(unsigned i=0;i<6;i++){snprintf(path,sizeof path,"%s/%s",save,files[i]);unlink(path);}
 const char *dirs[]={"TitleData","UserData","Cache","SystemData"};
 for(unsigned i=0;i<4;i++){snprintf(path,sizeof path,"%s/%s",save,dirs[i]);rmdir(path);}rmdir(save);rmdir(temp);
 puts("Read-only disc / writable saves and mounted U: path routing PASS");
}
