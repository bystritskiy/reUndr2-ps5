/* reUndr 2 PS5, MIT. Optional stereo spread and isolated speaker identification. */
#ifndef NFSU2_AUDIO_ENHANCE_H
#define NFSU2_AUDIO_ENHANCE_H
#include <stdint.h>
#include <math.h>
typedef struct { float bass; unsigned tick; int previous_test; } AudioEnhance;
static inline int16_t audio_limit(float n)
{
    return (int16_t)(n>16422?16422:n< -16422?-16422:n);
}
/* test: 0 off, 1..8 individual channels, 9 sequential walk. */
static inline int audio_enhance(int16_t *pcm,unsigned frames,int channels,
                                int spread,int test,AudioEnhance *state)
{
    if(test!=state->previous_test){state->tick=0;state->previous_test=test;}
    int active=-1;
    for(unsigned i=0;i<frames;i++){
        int16_t *p=pcm+i*channels;
        if(test){
            unsigned slot=state->tick/48000;
            active=test==9?(int)(slot%channels):test-1;
            for(int c=0;c<channels;c++)p[c]=0;
            if(active<channels){
                unsigned t=state->tick%48000;
                /* 0.5 s tone, 0.5 s silence; gentle 10 ms fades. */
                if(t<24000){
                    float fade=fminf(1,fminf(t/480.0f,(24000-t)/480.0f));
                    float hz=active==3?60.0f:440.0f;
                    p[active]=(int16_t)(2400*fade*sinf(6.28318530718f*hz*t/48000.0f));
                }
            }
            state->tick++;continue;
        }
        if(spread && channels==8){
            float l=p[0],r=p[1];
            state->bass+=0.01293f*(0.5f*(l+r)-state->bass); /* 100 Hz LPF */
            p[2]=audio_limit(p[2]+0.25f*(l+r));
            p[3]=audio_limit(p[3]+0.35f*state->bass);
            p[4]=audio_limit(p[4]+0.5f*l);p[5]=audio_limit(p[5]+0.5f*r);
            p[6]=audio_limit(p[6]+0.35f*l);p[7]=audio_limit(p[7]+0.35f*r);
        }else state->bass=0;
    }
    return active;
}
#endif
