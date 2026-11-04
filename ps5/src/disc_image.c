/* Unpacks an Xbox disc image (XDVDFS) on the console. MIT. */
#include "disc_image.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define SECTOR 2048
#define COPY_SIZE (4u<<20)
#define MAX_DEPTH 16
#define MAX_ENTRIES 65535
static const char MAGIC[]="MICROSOFT*XBOX*MEDIA";
/* A trimmed image starts with the file system; a full dump has the video partition first. */
static const uint64_t PARTITIONS[]={0,0x18300000,0xFD90000,0x2080000};

struct image {
 int fd;uint64_t base,size;char *buffer;
 uint64_t total,done;unsigned percent,entries;
 void (*progress)(unsigned);
};

static uint16_t u16(const unsigned char *p) { return (uint16_t)(p[0]|p[1]<<8); }
static uint32_t u32(const unsigned char *p) { return p[0]|p[1]<<8|p[2]<<16|(uint32_t)p[3]<<24; }

static int read_at(const struct image *m,void *out,size_t size,uint64_t offset)
{
 for(size_t got=0;got<size;) {
  ssize_t n=pread(m->fd,(char *)out+got,size-got,(off_t)(offset+got));
  if(n<0&&errno==EINTR)continue;
  if(n<=0)return 0;
  got+=(size_t)n;
 }
 return 1;
}

static int find_partition(struct image *m,uint32_t *sector,uint32_t *size)
{
 unsigned char volume[28];
 for(unsigned i=0;i<sizeof PARTITIONS/sizeof *PARTITIONS;i++) {
  if(PARTITIONS[i]+0x10000+sizeof volume>m->size||!read_at(m,volume,sizeof volume,PARTITIONS[i]+0x10000))continue;
  if(memcmp(volume,MAGIC,sizeof MAGIC-1))continue;
  m->base=PARTITIONS[i];*sector=u32(volume+20);*size=u32(volume+24);
  return 1;
 }
 return 0;
}

static int copy_file(struct image *m,uint32_t sector,uint32_t size,const char *path)
{
 uint64_t offset=m->base+(uint64_t)sector*SECTOR;
 if(offset+size>m->size)return NFSU2_IMAGE_DAMAGED;
 int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0666);
 if(fd<0)return NFSU2_IMAGE_WRITE;
 int result=NFSU2_IMAGE_OK;
 for(uint32_t left=size;left&&result==NFSU2_IMAGE_OK;) {
  size_t part=left<COPY_SIZE?left:COPY_SIZE;
  if(!read_at(m,m->buffer,part,offset)) { result=NFSU2_IMAGE_DAMAGED;break; }
  for(size_t wrote=0;wrote<part;) {
   ssize_t n=write(fd,m->buffer+wrote,part-wrote);
   if(n<0&&errno==EINTR)continue;
   if(n<=0) { result=NFSU2_IMAGE_WRITE;break; }
   wrote+=(size_t)n;
  }
  offset+=part;left-=(uint32_t)part;m->done+=part;
  unsigned percent=m->total?(unsigned)(m->done*100/m->total):100;
  if(m->progress&&percent!=m->percent)m->progress(percent);
  m->percent=percent;
 }
 if(close(fd)&&result==NFSU2_IMAGE_OK)result=NFSU2_IMAGE_WRITE;
 return result;
}

/* A directory is a binary tree of entries inside its own table. */
static int walk_table(struct image *m,const unsigned char *table,uint32_t table_size,uint32_t at,
                      const char *path,int depth,int copy,int skip_xbe);

static int walk_dir(struct image *m,uint32_t sector,uint32_t size,const char *path,int depth,int copy)
{
 if(!size)return NFSU2_IMAGE_OK;
 if(depth>MAX_DEPTH||size>(1u<<24)||m->base+(uint64_t)sector*SECTOR+size>m->size)return NFSU2_IMAGE_DAMAGED;
 unsigned char *table=malloc(size);
 if(!table)return NFSU2_IMAGE_WRITE;
 int result=read_at(m,table,size,m->base+(uint64_t)sector*SECTOR)
  ?walk_table(m,table,size,0,path,depth,copy,depth==0):NFSU2_IMAGE_DAMAGED;
 free(table);
 return result;
}

static int walk_table(struct image *m,const unsigned char *table,uint32_t table_size,uint32_t at,
                      const char *path,int depth,int copy,int skip_xbe)
{
 if(at>table_size||table_size-at<14)return NFSU2_IMAGE_DAMAGED;
 const unsigned char *e=table+at;
 uint16_t left=u16(e),right=u16(e+2);
 uint32_t sector=u32(e+4),size=u32(e+8);
 unsigned attributes=e[12],length=e[13];
 if(left==0xFFFF&&right==0xFFFF)return NFSU2_IMAGE_OK;
 if(++m->entries>MAX_ENTRIES||!length||length>table_size-at-14)return NFSU2_IMAGE_DAMAGED;
 char name[256];memcpy(name,e+14,length);name[length]=0;
 if(strchr(name,'/')||strchr(name,'\\')||!strcmp(name,".")||!strcmp(name,".."))return NFSU2_IMAGE_DAMAGED;
 int result=left&&left!=0xFFFF?walk_table(m,table,table_size,(uint32_t)left*4,path,depth,copy,skip_xbe):NFSU2_IMAGE_OK;
 if(result)return result;
 char child[PATH_MAX];
 if((size_t)snprintf(child,sizeof child,"%s/%s",path,name)>=sizeof child)return NFSU2_IMAGE_DAMAGED;
 if(attributes&0x10) {
  if(copy&&mkdir(child,0777)&&errno!=EEXIST)return NFSU2_IMAGE_WRITE;
  result=walk_dir(m,sector,size,child,depth+1,copy);
 }
 else if(!copy)m->total+=size;
 else if(!(skip_xbe&&!strcasecmp(name,"default.xbe")))result=copy_file(m,sector,size,child);
 if(result)return result;
 return right&&right!=0xFFFF?walk_table(m,table,table_size,(uint32_t)right*4,path,depth,copy,skip_xbe):NFSU2_IMAGE_OK;
}

/* Finds default.xbe in the root table, without its tree order mattering. */
static int find_xbe(const unsigned char *table,uint32_t size,uint32_t at,uint32_t *sector,uint32_t *bytes,int depth)
{
 if(depth>256||at>size||size-at<14)return 0;
 const unsigned char *e=table+at;
 uint16_t left=u16(e),right=u16(e+2);
 unsigned length=e[13];
 if((left==0xFFFF&&right==0xFFFF)||length>size-at-14)return 0;
 if(length==11&&!strncasecmp((const char *)e+14,"default.xbe",11)) { *sector=u32(e+4);*bytes=u32(e+8);return 1; }
 return (left&&left!=0xFFFF&&find_xbe(table,size,(uint32_t)left*4,sector,bytes,depth+1))
     ||(right&&right!=0xFFFF&&find_xbe(table,size,(uint32_t)right*4,sector,bytes,depth+1));
}

int nfsu2_image_extract(const char *image,const char *target,int (*accept)(const char *xbe),void (*progress)(unsigned))
{
 struct image m={.fd=open(image,O_RDONLY),.progress=progress};
 if(m.fd<0)return NFSU2_IMAGE_NOT_A_DISC;
 struct stat st;
 uint32_t root=0,root_size=0,xbe=0,xbe_size=0;
 int result=NFSU2_IMAGE_NOT_A_DISC;
 unsigned char *table=NULL;
 m.buffer=malloc(COPY_SIZE);
 if(!m.buffer||fstat(m.fd,&st)) { result=NFSU2_IMAGE_WRITE;goto done; }
 m.size=(uint64_t)st.st_size;
 if(!find_partition(&m,&root,&root_size)||!root_size||root_size>(1u<<24)
    ||m.base+(uint64_t)root*SECTOR+root_size>m.size)goto done;
 table=malloc(root_size);
 if(!table) { result=NFSU2_IMAGE_WRITE;goto done; }
 if(!read_at(&m,table,root_size,m.base+(uint64_t)root*SECTOR)||!find_xbe(table,root_size,0,&xbe,&xbe_size,0))goto done;
 if(mkdir(target,0777)&&errno!=EEXIST) { result=NFSU2_IMAGE_WRITE;goto done; }
 char path[PATH_MAX];
 if((size_t)snprintf(path,sizeof path,"%s/default.xbe",target)>=sizeof path)goto done;
 result=walk_dir(&m,root,root_size,target,0,0);
 if(result)goto done;
 m.entries=0;
 /* The executable goes first: a wrong disc is refused before gigabytes are copied. */
 result=copy_file(&m,xbe,xbe_size,path);
 if(result)goto done;
 if(accept&&!accept(path)) { result=NFSU2_IMAGE_WRONG_DISC;goto done; }
 result=walk_dir(&m,root,root_size,target,0,1);
done:
 free(table);free(m.buffer);close(m.fd);
 return result;
}
