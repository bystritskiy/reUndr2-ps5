#include "audio_native.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static int fail_stage,step,ports,contexts,phase,users;
static int16_t *submitted;
static int stage(void){return ++step==fail_stage?-1:0;}
int sceAudioOut2Initialize(void){return 0;}
int sceAudioOut2UserCreate(uint32_t id,uintptr_t *u){assert(id==255);*u=33;users++;return 0;}
int sceAudioOut2UserDestroy(uintptr_t u){assert(u==33&&!ports&&!contexts);users--;return 0;}
int sceAudioOut2GetSpeakerInfo(AudioSpeakers *s,uint32_t flags){assert(!flags);s->available=0x3f;s->type=1;return 0;}
int sceAudioOut2PortGetState(AudioPort p,AudioPortState*s){assert(p==0);s->channels=6;return 0;}
int sceAudioOut2ContextResetParam(AudioContextParam*p){memset(p,0,sizeof(*p));return stage();}
int sceAudioOut2ContextQueryMemory(const AudioContextParam*p,size_t*n){assert(p->num_grains==256&&p->flags==1&&p->queue_depth==1);*n=17000;return stage();}
int sceAudioOut2ContextCreate(const AudioContextParam*p,void*b,size_t n,AudioContext*c){(void)p;assert(b&&((uintptr_t)b%16384)==0&&n==17000);int rc=stage();if(!rc){*c=0;contexts++;}return rc;}
int sceAudioOut2ContextSetAttributes(AudioContext c,const AudioAttribute*a,uint32_t n){assert(c==0&&n==1&&a->id==2&&a->size==4&&*(uint32_t*)a->value==1);return 0;}
int sceAudioOut2PortCreate(AudioContext c,const AudioPortParam*p,AudioPort*out){assert(c==0&&p->data_format==0x801&&p->sampling_freq==48000&&p->user_handle==33&&p->port_type==0);int rc=stage();if(!rc){*out=0;ports++;}return rc;}
int sceAudioOut2PortSetAttributes(AudioPort p,const AudioAttribute*a,uint32_t n){assert(p==0&&n==1);if(a->id==1){assert(a->size==32);for(int i=0;i<8;i++)assert(((float*)a->value)[i]==1);return stage();}assert(a->id==0&&a->size==8);submitted=*(int16_t**)a->value;phase=1;return 0;}
int sceAudioOut2ContextAdvance(AudioContext c){assert(c==0&&phase==1);phase=2;return 0;}
int sceAudioOut2ContextPush(AudioContext c,uint32_t b){assert(c==0&&b==1&&phase==2);phase=0;return 0;}
int sceAudioOut2PortDestroy(AudioPort p){assert(p==0);ports--;return 0;}
int sceAudioOut2ContextDestroy(AudioContext c){assert(c==0&&!ports);contexts--;return 0;}
int main(void){
 for(fail_stage=1;fail_stage<=5;fail_stage++){step=0;assert(!nfsu2_native_audio_open(8));assert(!ports&&!contexts&&!users);}
 fail_stage=0;step=0;assert(nfsu2_native_audio_open(8));int16_t pcm[2048];
 for(int i=0;i<2048;i++)pcm[i]=i;
 int16_t*first=NULL;
 for(int block=0;block<32;block++){
  assert(!nfsu2_native_audio_write(pcm));assert(submitted!=pcm);
  assert(!memcmp(submitted,pcm,sizeof(pcm)));
  if(!block)first=submitted;
  if(block>0&&block<16)assert(submitted!=first);
  if(block==16)assert(submitted==first);
 }
 nfsu2_native_audio_close();assert(!ports&&!contexts&&!users);nfsu2_native_audio_close();
 puts("PASS: AudioOut2 ABI, system-follow attributes, PCM lifetime ring, ordered push and failure cleanup");
}
