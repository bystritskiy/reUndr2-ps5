/* reUndr 2 PS5, MIT. */
#include "xbe_check.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t u32(const unsigned char *p) { return p[0]|p[1]<<8|p[2]<<16|(uint32_t)p[3]<<24; }
static int same_build(const unsigned char *x,size_t size,const Nfsu2XbeSection *ref,unsigned count)
{
    if(size<0x124||memcmp(x,"XBEH",4))return 0;
    uint32_t base=u32(x+0x104),sections=u32(x+0x11C),at=u32(x+0x120)-base;
    if(sections!=count||at>size||(size-at)/56<count)return 0;
    for(unsigned i=0;i<count;i++) {
        const unsigned char *s=x+at+i*56;
        uint32_t raw=u32(s+12),raw_size=u32(s+16);
        if(raw_size!=ref[i].raw_size||memcmp(s+36,ref[i].digest,20)||raw>size||size-raw<raw_size)return 0;
        if(!ref[i].data_sha256)continue;
        Nfsu2Sha256 c;char hex[65];
        nfsu2_sha256_init(&c);nfsu2_sha256_update(&c,x+raw,raw_size);nfsu2_sha256_hex(&c,hex);
        if(strcmp(hex,ref[i].data_sha256))return 0;
    }
    return 1;
}
int nfsu2_xbe_check(const char *path,const char *full_sha256,const Nfsu2XbeSection *ref,unsigned count,char found[65])
{
    found[0]=0;
    FILE *f=fopen(path,"rb");
    if(!f)return 0;
    unsigned char *x=NULL;long size=-1;
    if(!fseek(f,0,SEEK_END))size=ftell(f);
    if(size>0&&size<=(64<<20)&&!fseek(f,0,SEEK_SET)&&(x=malloc((size_t)size))&&fread(x,1,(size_t)size,f)!=(size_t)size) { free(x);x=NULL; }
    fclose(f);
    if(!x)return 0;
    Nfsu2Sha256 c;nfsu2_sha256_init(&c);nfsu2_sha256_update(&c,x,(size_t)size);nfsu2_sha256_hex(&c,found);
    int ok=!strcmp(found,full_sha256)||same_build(x,(size_t)size,ref,count);
    free(x);return ok;
}
