#include "hd_textures.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifndef NFS_HD_ROOT
#error Test root required
#endif
static void write_file(const char *name,const void *data,size_t size)
{
    char path[256];snprintf(path,sizeof path,NFS_HD_ROOT "/%s",name);
    FILE *f=fopen(path,"wb");assert(f);assert(fwrite(data,1,size,f)==size);fclose(f);
}
int main(int argc,char **argv)
{
    const unsigned char source[8]={1,2,3,4,5,6,7,8};
    uint64_t hash=UINT64_C(0xcbf29ce484222325);
    for (unsigned i=0;i<8;i++) hash=(hash^source[i])*UINT64_C(0x100000001b3);
    NfsHdTexture entry={.color=12,.source_w=4,.source_h=4,.source_bytes=8,
        .sample_hash=0x1234,.width=8,.height=8,.hash_lo=(uint32_t)hash,
        .hash_hi=(uint32_t)(hash>>32),.file="0123456789abcdef0123456789abcdef.rgba"};
    unsigned char manifest[16+sizeof entry];
    uint32_t header[4]={0,1,1,sizeof entry};memcpy(header,"NHDX",4);
    int invalid=argc>1 && !strcmp(argv[1],"invalid");
    if (invalid) strcpy(entry.file,"../outside.rgba");
    memcpy(manifest,header,16);memcpy(manifest+16,&entry,sizeof entry);
    write_file("manifest.bin",manifest,sizeof manifest);
    const NfsHdTexture *found=nfsu2_hd_find(source,12,4,4,8,0x1234);
    if (invalid) { assert(!found);puts("HD invalid manifest rejected");return 0; }
    assert(found && found->width==8);
    assert(!nfsu2_hd_find(source,12,8,4,8,0x1234));
    assert(!nfsu2_hd_find(source,12,4,4,8,0x1235));
    unsigned char different[8]={0};assert(!nfsu2_hd_find(different,12,4,4,8,0x1234));
    unsigned char image[16+256];uint32_t ih[4]={0,8,8,256};memcpy(ih,"NHD1",4);
    memcpy(image,ih,16);memset(image+16,0x5a,256);
    write_file(entry.file,image,sizeof image);
    void *pixels=nfsu2_hd_load(found);assert(pixels && !memcmp(pixels,image+16,256));free(pixels);
    write_file(entry.file,image,sizeof image-1);assert(!nfsu2_hd_load(found));
    image[4]=16;write_file(entry.file,image,sizeof image);assert(!nfsu2_hd_load(found));
    memset(image,0,16);write_file(entry.file,image,sizeof image);assert(!nfsu2_hd_load(found));
    puts("HD valid lookup, strong hash, dimensions, malformed/truncated file checks passed");
    return 0;
}
