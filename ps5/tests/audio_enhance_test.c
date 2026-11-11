#include "audio_enhance.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void){
 AudioEnhance s={0};int16_t pcm[256*8];
 for(int ch=0;ch<8;ch++){
  for(int i=0;i<2048;i++)pcm[i]=1000;
  assert(audio_enhance(pcm,256,8,1,ch+1,&s)==ch);
  int heard=0;
  for(int i=0;i<256;i++)for(int c=0;c<8;c++){
   if(c!=ch)assert(pcm[i*8+c]==0);else if(pcm[i*8+c])heard=1;
  }
  assert(heard);
 }
 memset(&s,0,sizeof(s));
 for(int b=0;b<1500;b++){
  memset(pcm,0,sizeof(pcm));int ch=audio_enhance(pcm,256,8,0,9,&s);
  assert(ch>=0&&ch<8);
  for(int i=0;i<256;i++){
   unsigned tick=(unsigned)b*256+i;int target=(tick/48000)%8;
   for(int c=0;c<8;c++)if(c!=target)assert(pcm[i*8+c]==0);
  }
 }
 memset(&s,0,sizeof(s));memset(pcm,0,sizeof(pcm));
 for(int i=0;i<256;i++){pcm[i*8]=1000;pcm[i*8+1]=-1000;pcm[i*8+4]=200;}
 audio_enhance(pcm,256,8,1,0,&s);
 assert(pcm[0]==1000&&pcm[1]==-1000&&pcm[2]==0&&pcm[3]==0);
 assert(pcm[4]==700&&pcm[5]==-500&&pcm[6]==350&&pcm[7]==-350);
 for(int i=0;i<2048;i++)pcm[i]=1000;
 audio_enhance(pcm,256,8,0,0,&s);for(int i=0;i<2048;i++)assert(pcm[i]==1000);
 for(int i=0;i<2048;i++)pcm[i]=16000;
 audio_enhance(pcm,256,8,1,0,&s);for(int i=0;i<2048;i++)assert(pcm[i]<=16422);
 puts("PASS: isolated 8-channel tones, walk transitions, reduced stereo spread, original effects, limiter");
}
