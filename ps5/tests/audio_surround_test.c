/* Eight-lane isolation and output resampling, with a deterministic mock device. */
#include "apu/apu_xaudio2.h"
#include "audio_mix.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static int entered, credits, release_all, closed, fallback, opens, channels;
static int16_t frames[10][256*8];
void nfsu2_audio_set_spread(int on);
int nfsu2_native_audio_open(int c){(void)c;return 0;}
int nfsu2_native_audio_write(const int16_t *p){(void)p;abort();}
void nfsu2_native_audio_log(void){}
void nfsu2_native_audio_close(void){}
int sceAudioOutInit(void) { return 0; }
int sceAudioOutOpen(int user,int type,int index,unsigned grain,unsigned rate,unsigned format)
{
    assert(user==255 && type==0 && index==0 && grain==256 && rate==48000);
    opens++;
    if (format==2) { if (fallback) return -1; channels=8; }
    else { assert(format==1 && fallback); channels=2; }
    return 9;
}
int sceAudioOutSetVolume(int h,int mask,const int *volumes)
{
    assert(h==9 && (mask==3 || mask==255));
    for(int i=0;i<8;i++)assert(volumes[i]==32768);
    return 0;
}
int sceAudioOutGetPortState(int h,void *state)
{
    assert(h==9);memset(state,0,32);return 0;
}
int sceAudioOutSysGetHdmiMonitorInfo(int type,void *buf,unsigned size)
{
    assert(type==1 && size==0x180);memset(buf,0,size);return -1;
}
int sceAudioOutClose(int h) { assert(h==9); closed++; return 0; }
int sceAudioOutOutput(int h,const void *pcm)
{
    assert(h==9); if (!pcm) return 0;
    pthread_mutex_lock(&gate);
    if (entered<10) memcpy(frames[entered],pcm,256*channels*sizeof(int16_t));
    entered++; pthread_cond_broadcast(&changed);
    while (!credits&&!release_all) pthread_cond_wait(&changed,&gate);
    if (credits) credits--;
    pthread_mutex_unlock(&gate); return 0;
}
static void wait_calls(int n)
{
    pthread_mutex_lock(&gate);
    while(entered<n) pthread_cond_wait(&changed,&gate);
    pthread_mutex_unlock(&gate);
}
static void advance(int n)
{
    pthread_mutex_lock(&gate); credits++;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&gate);
    wait_calls(n);
}
static void test_routing(void)
{
    static const int target[10]={0,1,2,3,4,5,0,1,4,5};
    float bins[32][32]={{0}};int16_t out[8];
    for(int b=0;b<10;b++){
        memset(bins,0,sizeof bins);bins[b][17]=0.25f;
        nfsu2_audio_route(&bins[0][0],32,17,out);
        for(int c=0;c<8;c++) assert((c==target[b]) ? out[c]>0 : out[c]==0);
        nfsu2_audio_route(&bins[0][0],32,16,out);
        for(int c=0;c<8;c++)assert(out[c]==0);
    }
    for(int b=10;b<32;b++){
        memset(bins,0,sizeof bins);bins[b][0]=0.25f;
        nfsu2_audio_route(&bins[0][0],32,0,out);
        assert(out[0]||out[1]);for(int c=2;c<8;c++)assert(out[c]==0);
    }
    assert(nfsu2_audio_add(30000,30000)==32767);
    assert(nfsu2_audio_add(-30000,-30000)==-32768);
    assert(nfsu2_audio_s16(NAN)==0);
}
int main(int argc,char **argv)
{
    (void)argv;fallback=argc>1;unsetenv("NFSU2_AUDIO_CHANNELS");unsetenv("RECOMP_AUDIO_VOLUME");
    nfsu2_audio_set_spread(0); /* exact lanes; the spread has its own test */
    test_routing();xa2_set_master_volume(1);assert(xa2_init());wait_calls(1);
    assert(xa2_output_channels()==(fallback?2:8));assert(opens==(fallback?2:1));
    int16_t pcm[256*8],stereo[256*2];
    for(int i=0;i<256;i++){
        for(int c=0;c<8;c++)pcm[i*8+c]=(c+1)*100;
        stereo[i*2]=1200;stereo[i*2+1]=-1300;
    }
    assert(!xa2_submit_surround(NULL,256));assert(!xa2_submit_surround(pcm,257));
    assert(!xa2_submit_surround(pcm,0));assert(xa2_submit_surround(pcm,256));
    assert(xa2_submit_samples(stereo,256));
    for(int i=0;i<5;i++)advance(i+2);
    for(int i=0;i<4;i++)for(int j=0;j<256*channels;j++)assert(frames[i][j]==0);
    if(!fallback){
        for(int i=2;i<254;i++)for(int c=0;c<8;c++)assert(abs(frames[4][i*8+c]-(c+1)*100)<=1);
        for(int i=2;i<254;i++){
            assert(abs(frames[5][i*8]-1200)<=1);assert(abs(frames[5][i*8+1]+1300)<=1);
            for(int c=2;c<8;c++)assert(frames[5][i*8+c]==0);
        }
    }else{
        assert(frames[4][20]>100 && frames[4][21]>200);
        assert(abs(frames[5][20]-1200)<=1 && abs(frames[5][21]+1300)<=1);
    }
    pthread_mutex_lock(&gate);release_all=1;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&gate);
    xa2_shutdown();assert(!xa2_is_active()&&closed==1);
    puts("PASS: isolated Xbox speaker/3D bins, FX preservation, 8ch queue/resampling, stereo movies, fallback");
}
