/* Lossless host-side UI replacements. Guest texture geometry stays unchanged. */
#include "hd_textures.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

_Static_assert(sizeof(NfsHdTexture) == 84, "HD manifest record ABI");
#ifndef NFS_HD_ROOT
#define NFS_HD_ROOT "/app0/hd"
#endif
#define DUMP_MIN_TEXELS (128*128)
static NfsHdTexture *entries;
static uint32_t count;
static int initialized;
static char root[256];

static FILE *open_manifest(const char *dir)
{
    char path[320];
    snprintf(root,sizeof root,"%s",dir);
    snprintf(path,sizeof path,"%s/manifest.bin",root);
    return fopen(path,"rb");
}

static void init(void)
{
    uint32_t header[4];
    FILE *f=NULL;
    initialized = 1;
    const char *enabled = getenv("NFSU2_HD");
    if (enabled && !strcmp(enabled,"0")) return;
    /* NFSU2_HD_DIR holds a pack kept outside the app; it comes first. */
    const char *outside=getenv("NFSU2_HD_DIR");
    if (outside && *outside) f=open_manifest(outside);
    if (!f) f=open_manifest(NFS_HD_ROOT);
    if (!f) return;
    if (fread(header,sizeof header,1,f)!=1 || memcmp(header,"NHDX",4)
        || header[1]!=1 || !header[2] || header[2]>4096 || header[3]!=sizeof(NfsHdTexture)) {
        fclose(f); return;
    }
    NfsHdTexture *data = calloc(header[2],sizeof *data);
    if (!data) { fclose(f); return; }
    if (fread(data,sizeof *data,header[2],f)!=header[2] || fgetc(f)!=EOF) {
        free(data); fclose(f); return;
    }
    fclose(f);
    for (uint32_t i=0;i<header[2];i++) {
        NfsHdTexture *t=&data[i];
        if (!t->width || !t->height || t->width>4096 || t->height>4096
            || (uint64_t)t->width*t->height>8u*1024u*1024u
            || !memchr(t->file,0,sizeof t->file) || !t->file[0]
            || strlen(t->file)!=37 || strcmp(t->file+32,".rgba")
            || (i && t->sample_hash<data[i-1].sample_hash)) {
            free(data); return;
        }
        for (unsigned k=0;k<32;k++) {
            unsigned char c=(unsigned char)t->file[k];
            if (!((c>='0' && c<='9') || (c>='a' && c<='f'))) { free(data); return; }
        }
    }
    entries=data; count=header[2];
    fprintf(stderr,"[HD] Loaded %u texture fingerprints\n",count);
}

/* NFSU2_HD_DUMP=<folder>: writes each large texture without a replacement once,
 * to find out what a screen is made of. */
static void dump(const void *source,uint32_t color,uint32_t w,uint32_t h,uint32_t bytes,
                 uint32_t sample_hash)
{
    static const char *folder;
    static int checked;
    if (!checked) { folder=getenv("NFSU2_HD_DUMP"); checked=1; }
    if (!folder || !*folder || w*h<DUMP_MIN_TEXELS) return;
    char path[320];
    snprintf(path,sizeof path,"%s/%08x_%ux%u_%02x.dxt",folder,sample_hash,w,h,color);
    FILE *f=fopen(path,"rb");
    if (f) { fclose(f); return; }
    f=fopen(path,"wb");
    if (!f) return;
    fwrite(source,1,bytes,f);
    fclose(f);
}

const NfsHdTexture *nfsu2_hd_find(const void *source,uint32_t color,
                                uint32_t w,uint32_t h,uint32_t bytes,uint32_t sample_hash)
{
    if (!initialized) init();
    uint32_t lo=0,hi=count;
    while (lo<hi) {
        uint32_t mid=lo+(hi-lo)/2;
        if (entries[mid].sample_hash<sample_hash) lo=mid+1; else hi=mid;
    }
    uint64_t hash=0;
    int hashed=0;
    for (uint32_t i=lo;i<count && entries[i].sample_hash==sample_hash;i++) {
        const NfsHdTexture *t=&entries[i];
        if (t->color!=color || t->source_w!=w || t->source_h!=h || t->source_bytes!=bytes) continue;
        if (!hashed) {
            hash=UINT64_C(0xcbf29ce484222325);
            const unsigned char *p=source;
            for (uint32_t k=0;k<bytes;k++) hash=(hash^p[k])*UINT64_C(0x100000001b3);
            hashed=1;
        }
        if (hash==((uint64_t)t->hash_hi<<32 | t->hash_lo)) return t;
    }
    dump(source,color,w,h,bytes,sample_hash);
    return NULL;
}

void *nfsu2_hd_load(const NfsHdTexture *t)
{
    char path[320];
    uint32_t header[4];
    if (!t) return NULL;
    snprintf(path,sizeof path,"%s/%s",root,t->file);
    FILE *f=fopen(path,"rb");
    if (!f) return NULL;
    const uint32_t bytes=t->width*t->height*4;
    if (fread(header,sizeof header,1,f)!=1 || memcmp(header,"NHD1",4)
        || header[1]!=t->width || header[2]!=t->height || header[3]!=bytes) {
        fclose(f); return NULL;
    }
    void *pixels=malloc(bytes);
    if (!pixels) { fclose(f); return NULL; }
    if (fread(pixels,1,bytes,f)!=bytes || fgetc(f)!=EOF) {
        free(pixels); fclose(f); return NULL;
    }
    fclose(f);
    fprintf(stderr,"[HD] %ux%u -> %ux%u %s\n",t->source_w,t->source_h,t->width,t->height,t->file);
    return pixels;
}

/* hd/wide.txt (NFSU2_HD_DIR first): one sampled hash per line, in hex, the same
 * hash as the texture's in manifest.bin. */
#define MAX_WIDE 32
static uint32_t wide[MAX_WIDE];
static unsigned wide_count;

static void load_wide(const char *dir)
{
    char path[320], line[64];
    snprintf(path,sizeof path,"%s/wide.txt",dir);
    FILE *f=fopen(path,"r");
    if (!f) return;
    while (wide_count<MAX_WIDE && fgets(line,sizeof line,f)) {
        char *end;
        unsigned long v=strtoul(line,&end,16);
        if (end!=line) wide[wide_count++]=(uint32_t)v;
    }
    fclose(f);
    fprintf(stderr,"[HD] %u wide backdrops\n",wide_count);
}

static uint32_t sampled(const unsigned char *mem,uint32_t bytes)
{
    uint32_t h=2166136261u^bytes,step=bytes/256,i,w;
    if (step<4) step=4;
    step&=~3u;
    for (i=0;i+4<=bytes;i+=step) { memcpy(&w,mem+i,4); h=(h^w)*16777619u; }
    memcpy(&w,mem+bytes-4,4);
    return (h^w)*16777619u;
}

int nfsu2_hd_wide(const void *source,uint32_t bytes)
{
    static int loaded;
    if (!loaded) {
        loaded=1;
        const char *enabled=getenv("NFSU2_HD");
        if (enabled && !strcmp(enabled,"0")) return 0;
        const char *outside=getenv("NFSU2_HD_DIR");
        if (outside && *outside) load_wide(outside);
        if (!wide_count) load_wide(NFS_HD_ROOT);
    }
    if (!wide_count || bytes<4) return 0;
    const uint32_t h=sampled(source,bytes);
    for (unsigned i=0;i<wide_count;i++) if (wide[i]==h) return 1;
    return 0;
}
