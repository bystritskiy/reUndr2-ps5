#include "xbe_check.h"
#include "sha256.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
/* Two sections: code that the port runs recompiled, and data. */
static unsigned char x[0x400];
static void put(unsigned at,unsigned v){x[at]=v;x[at+1]=v>>8;x[at+2]=v>>16;x[at+3]=v>>24;}
static int check(const char *path,const char *full,const Nfsu2XbeSection *ref,char *found){
 FILE *f=fopen(path,"wb");assert(f);fwrite(x,1,sizeof x,f);fclose(f);
 return nfsu2_xbe_check(path,full,ref,2,found);
}
int main(void){
 char path[]="/tmp/reundr2-xbe-XXXXXX",found[65],full[65],data[65];int fd=mkstemp(path);assert(fd>=0);close(fd);
 memcpy(x,"XBEH",4);put(0x104,0x10000);put(0x11C,2);put(0x120,0x10000+0x200);
 put(0x200+12,0x300);put(0x200+16,0x40);put(0x238+12,0x340);put(0x238+16,0x40);
 memset(x+0x200+36,0x11,20);memset(x+0x238+36,0x22,20);
 memset(x+0x300,0x90,0x40);memset(x+0x340,0x5a,0x40);
 Nfsu2Sha256 c;nfsu2_sha256_init(&c);nfsu2_sha256_update(&c,x+0x340,0x40);nfsu2_sha256_hex(&c,data);
 Nfsu2XbeSection ref[2]={{0x40,{0},NULL},{0x40,{0},NULL}};
 memset(ref[0].digest,0x11,20);memset(ref[1].digest,0x22,20);ref[1].data_sha256=data;
 assert(check(path,"",ref,full)&&strlen(full)==64);
 assert(check(path,full,ref,found)&&!strcmp(found,full));
 x[0x180]^=1;x[0x310]^=1;           /* certificate and recompiled code patched */
 assert(check(path,full,ref,found)&&strcmp(found,full));
 x[0x350]^=1;                       /* game data changed */
 assert(!check(path,full,ref,found));x[0x350]^=1;
 x[0x238+40]^=1;                    /* another build of the section */
 assert(!check(path,full,ref,found));x[0x238+40]^=1;
 put(0x238+16,0x400);               /* section past the end */
 assert(!check(path,full,ref,found));
 assert(!nfsu2_xbe_check("/nonexistent/default.xbe",full,ref,2,found)&&!found[0]);
 unlink(path);puts("XBE check: exact, patched header and code, changed data and digests, bounds passed");return 0;
}
