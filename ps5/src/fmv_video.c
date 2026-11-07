/* Decodes one AV1 movie ahead of the game on its own threads, into a small ring
 * of BGRA frames for fmv_replace.c. The game asks for frames in order, so a frame
 * it did not ask for is dropped and the last one stays on screen if the decoder
 * falls behind. Only one movie plays at a time. */
#include "fmv_video.h"
#include <dav1d/dav1d.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define RING 6
#define THREADS 4
#define IDLE_SECONDS 10
#define FIRST_WAIT_MS 300
#define NEXT_WAIT_MS 4
#define MAX_WIDTH 4096
#define IVF_FRAME_HEADER 12

typedef struct { uint32_t frame; uint8_t *pixels; } Slot;

static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static pthread_t worker;
static int started;
static int stop,done;
static int fd;
static uint64_t offset;
static uint32_t width,height,next_frame;
static Slot slots[RING];
static unsigned head,count;
static int row_u[MAX_WIDTH],row_v[MAX_WIDTH];
static uint32_t late;
static uint64_t convert_us;

static struct timespec after(long ms)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME,&t);
    t.tv_sec+=ms/1000;
    t.tv_nsec+=(ms%1000)*1000000;
    if (t.tv_nsec>=1000000000) { t.tv_sec++; t.tv_nsec-=1000000000; }
    return t;
}

/* The movies carry no colour tags, so they stay limited range BT.601. Plain int
 * arithmetic on whole rows lets the compiler vectorize this: at 4K it runs for
 * every frame, on the thread that has to stay ahead of the game. */
static void to_bgra(const Dav1dPicture *pic,uint8_t *out)
{
    const ptrdiff_t luma=pic->stride[0],chroma=pic->stride[1];
    for (uint32_t y=0;y<height;y++) {
        const uint8_t *restrict py=(const uint8_t *)pic->data[0]+y*luma;
        const uint8_t *pu=(const uint8_t *)pic->data[1]+(y/2)*chroma;
        const uint8_t *pv=(const uint8_t *)pic->data[2]+(y/2)*chroma;
        int *restrict u=row_u,*restrict v=row_v;
        for (uint32_t x=0;x<width/2;x++) {
            u[x*2]=u[x*2+1]=pu[x]-128;
            v[x*2]=v[x*2+1]=pv[x]-128;
        }
        /* The game's VP6 pictures, and so its movie texture, are stored bottom-up. */
        uint32_t *restrict bgra=(uint32_t *)(void *)(out+(size_t)(height-1-y)*width*4);
        for (uint32_t x=0;x<width;x++) {
            const int c=298*(py[x]-16)+128;
            int b=(c+516*u[x])>>8,g=(c-100*u[x]-208*v[x])>>8,r=(c+409*v[x])>>8;
            b=b<0 ? 0 : b>255 ? 255 : b;
            g=g<0 ? 0 : g>255 ? 255 : g;
            r=r<0 ? 0 : r>255 ? 255 : r;
            bgra[x]=0xFF000000u|(uint32_t)r<<16|(uint32_t)g<<8|(uint32_t)b;
        }
    }
}

/* Holds the lock on return. Nonzero means the movie is no longer wanted. */
static int wait_for_room(void)
{
    pthread_mutex_lock(&lock);
    while (!stop && count==RING) {
        const struct timespec until=after(IDLE_SECONDS*1000);
        if (pthread_cond_timedwait(&changed,&lock,&until)==ETIMEDOUT) stop=1;
    }
    return stop;
}

static int emit(const Dav1dPicture *pic)
{
    if (pic->p.w!=(int)width || pic->p.h!=(int)height || pic->p.layout!=DAV1D_PIXEL_LAYOUT_I420
        || pic->p.bpc!=8) {
        fprintf(stderr,"[FMV] picture %dx%d does not fit the pack\n",pic->p.w,pic->p.h);
        return -1;
    }
    const int unwanted=wait_for_room();
    Slot *slot=&slots[(head+count)%RING];
    pthread_mutex_unlock(&lock);
    if (unwanted) return -1;
    struct timespec before,after_convert;
    clock_gettime(CLOCK_MONOTONIC,&before);
    to_bgra(pic,slot->pixels);
    clock_gettime(CLOCK_MONOTONIC,&after_convert);
    convert_us+=(uint64_t)(after_convert.tv_sec-before.tv_sec)*1000000
        +(after_convert.tv_nsec-before.tv_nsec)/1000;
    pthread_mutex_lock(&lock);
    slot->frame=next_frame++;
    count++;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
    return 0;
}

static int drain(Dav1dContext *ctx)
{
    for (;;) {
        Dav1dPicture pic;
        memset(&pic,0,sizeof pic);
        const int result=dav1d_get_picture(ctx,&pic);
        if (result==DAV1D_ERR(EAGAIN)) return 0;
        if (result<0) return result;
        const int failed=emit(&pic);
        dav1d_picture_unref(&pic);
        if (failed) return -1;
    }
}

static int decode(Dav1dContext *ctx)
{
    uint64_t at=offset;
    for (;;) {
        uint8_t header[IVF_FRAME_HEADER];
        const ssize_t got=pread(fd,header,sizeof header,(off_t)at);
        if (!got) return 0;
        if (got!=(ssize_t)sizeof header) return -1;
        const uint32_t size=header[0]|header[1]<<8|header[2]<<16|(uint32_t)header[3]<<24;
        at+=sizeof header;
        Dav1dData data;
        memset(&data,0,sizeof data);
        uint8_t *bytes=dav1d_data_create(&data,size);
        if (!bytes) return -1;
        if (pread(fd,bytes,size,(off_t)at)!=(ssize_t)size) { dav1d_data_unref(&data); return -1; }
        at+=size;
        while (data.sz) {
            const int result=dav1d_send_data(ctx,&data);
            if (result<0 && result!=DAV1D_ERR(EAGAIN)) { dav1d_data_unref(&data); return result; }
            const int failed=drain(ctx);
            if (failed) { dav1d_data_unref(&data); return failed; }
        }
    }
}

static void *run(void *unused)
{
    (void)unused;
    Dav1dSettings settings;
    dav1d_default_settings(&settings);
    settings.n_threads=THREADS;
    settings.apply_grain=0;
    Dav1dContext *ctx=NULL;
    int result=dav1d_open(&ctx,&settings);
    if (!result) {
        result=decode(ctx);
        /* Pictures held back by frame threads come out once the input ends. */
        if (!result) result=drain(ctx);
        dav1d_close(&ctx);
    }
    if (result>0 || result<-1) fprintf(stderr,"[FMV] decoder failed: %d\n",result);
    pthread_mutex_lock(&lock);
    done=1;
    fprintf(stderr,"[FMV] %ux%u: %u frames decoded, %u late, colour %llu us each\n",width,height,
            next_frame,late,next_frame ? (unsigned long long)(convert_us/next_frame) : 0);
    pthread_cond_broadcast(&changed);
    while (!stop) {
        const struct timespec until=after(IDLE_SECONDS*1000);
        if (pthread_cond_timedwait(&changed,&lock,&until)==ETIMEDOUT) stop=1;
    }
    for (unsigned i=0;i<RING;i++) { free(slots[i].pixels); slots[i].pixels=NULL; }
    count=0;
    pthread_mutex_unlock(&lock);
    return NULL;
}

void nfsu2_fmv_video_stop(void)
{
    if (!started) return;
    pthread_mutex_lock(&lock);
    stop=1;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
    pthread_join(worker,NULL);
    started=0;
}

static int start(int movie,uint64_t data_offset,uint32_t w,uint32_t h)
{
    nfsu2_fmv_video_stop();
    fd=movie; offset=data_offset; width=w; height=h;
    next_frame=head=count=late=0;
    convert_us=0;
    stop=done=0;
    for (unsigned i=0;i<RING;i++) {
        slots[i].pixels=malloc((size_t)w*h*4);
        if (!slots[i].pixels) {
            for (unsigned n=0;n<=i;n++) { free(slots[n].pixels); slots[n].pixels=NULL; }
            return 0;
        }
    }
    if (pthread_create(&worker,NULL,run,NULL)) {
        for (unsigned i=0;i<RING;i++) { free(slots[i].pixels); slots[i].pixels=NULL; }
        return 0;
    }
    started=1;
    return 1;
}

const void *nfsu2_fmv_video_frame(int movie,uint64_t data_offset,uint32_t w,uint32_t h,
                                  uint32_t frame,int restart)
{
    if (restart || !started || fd!=movie) {
        if (!start(movie,data_offset,w,h)) return NULL;
    } else {
        pthread_mutex_lock(&lock);
        const int gone=stop && !count;
        pthread_mutex_unlock(&lock);
        if (gone && !start(movie,data_offset,w,h)) return NULL;
    }
    pthread_mutex_lock(&lock);
    struct timespec until=after(count ? NEXT_WAIT_MS : FIRST_WAIT_MS);
    for (;;) {
        while (count>1 && slots[head].frame<frame) {
            head=(head+1)%RING;
            count--;
            pthread_cond_broadcast(&changed);
        }
        if ((count && slots[head].frame>=frame) || done || stop) break;
        if (pthread_cond_timedwait(&changed,&lock,&until)==ETIMEDOUT) { late++; break; }
    }
    const void *pixels=count ? slots[head].pixels : NULL;
    pthread_mutex_unlock(&lock);
    return pixels;
}
