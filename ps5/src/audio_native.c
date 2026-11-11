/* reUndr 2 PS5, MIT. AudioOut2 application MAIN bed follows system settings.
 * ABI references and validation limits: docs/AUDIO.md. */
#include "audio_native.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static AudioContext context;
static AudioPort port;
static int context_live, port_live;
static uintptr_t user;
static void *memory;
static int channels;
/* Keep PCM alive for substantially longer than the context's depth-one queue. */
static int16_t blocks[16][256*8];
static unsigned next_block;
void nfsu2_native_audio_close(void)
{
    if(port_live) sceAudioOut2PortDestroy(port);
    if(context_live) sceAudioOut2ContextDestroy(context);
    if(user)sceAudioOut2UserDestroy(user);
    user=0;port=context=0;port_live=context_live=0;free(memory);memory=NULL;
}
void nfsu2_native_audio_log(void)
{
    AudioSpeakers speakers={0};
    int rc=sceAudioOut2GetSpeakerInfo(&speakers,0);
    fprintf(stderr,"[PS5 audio2] speakers rc=%x type=%u available=%04x flags=%x angles=%d/%d %d/%d\n",
            (unsigned)rc,speakers.type,speakers.available,speakers.flags,
            speakers.angles[4][0],speakers.angles[4][1],speakers.angles[5][0],speakers.angles[5][1]);
    if(port_live){
        AudioPortState state={0};rc=sceAudioOut2PortGetState(port,&state);
        fprintf(stderr,"[PS5 audio2] state rc=%x output=%x channels=%u volume=%d reroute=%u flags=%x\n",
                (unsigned)rc,state.output,state.channels,state.volume,state.reroute,state.flags);
    }
}
int nfsu2_native_audio_open(int count)
{
    AudioContextParam cp={0};AudioPortParam pp={0};size_t bytes=0;
    int rc=sceAudioOut2Initialize();
    fprintf(stderr,"[PS5 audio2] init rc=%x\n",(unsigned)rc);
    if(rc<0 && (uint32_t)rc!=0x80268004u)return 0;
    nfsu2_native_audio_log();
    const char *step="user-create";
    rc=sceAudioOut2UserCreate(255,&user);
    fprintf(stderr,"[PS5 audio2] user-create rc=%x handle=%llx\n",(unsigned)rc,(unsigned long long)user);
    if(rc<0)goto fail;
    step="context-reset";
    rc=sceAudioOut2ContextResetParam(&cp);if(rc<0)goto fail;
    cp.max_ports=1;cp.max_object_ports=cp.guarantee_object_ports=0;
    cp.queue_depth=1;cp.num_grains=256;cp.flags=1; /* application MAIN context */
    step="context-memory";
    rc=sceAudioOut2ContextQueryMemory(&cp,&bytes);if(rc<0)goto fail;
    if(!bytes||bytes>64*1024*1024){rc=-1;goto fail;}
    if(posix_memalign(&memory,16384,(bytes+16383)&~(size_t)16383)){rc=-1;goto fail;}
    memset(memory,0,bytes);
    step="context-create";
    rc=sceAudioOut2ContextCreate(&cp,memory,bytes,&context);if(rc<0)goto fail;
    context_live=1;
    uint32_t follow=1;
    AudioAttribute attr={2,0,&follow,sizeof(follow)};
    rc=sceAudioOut2ContextSetAttributes(context,&attr,1);
    fprintf(stderr,"[PS5 audio2] follow-system-speakers rc=%x\n",(unsigned)rc);
    /* Attribute support varies. The MAIN port still uses the system route. */
    pp.data_format=((uint32_t)count<<8)|1;pp.sampling_freq=48000;
    pp.port_type=0;pp.user_handle=user;
    step="port-create";
    rc=sceAudioOut2PortCreate(context,&pp,&port);
    fprintf(stderr,"[PS5 audio2] port-create rc=%x port=%llx\n",(unsigned)rc,(unsigned long long)port);
    if(rc<0)goto fail;
    port_live=1;
    float gains[8]={1,1,1,1,1,1,1,1};
    attr=(AudioAttribute){1,0,gains,(size_t)count*sizeof(float)};
    step="port-gains";
    rc=sceAudioOut2PortSetAttributes(port,&attr,1);if(rc<0)goto fail;
    channels=count;next_block=0;memset(blocks,0,sizeof(blocks));
    fprintf(stderr,"[PS5 audio2] MAIN S16 %dch 48000Hz context=%llx port=%llx memory=%zu\n",
            channels,(unsigned long long)context,(unsigned long long)port,bytes);
    nfsu2_native_audio_log();return 1;
fail:
    fprintf(stderr,"[PS5 audio2] open failed step=%s rc=%x; use legacy AudioOut\n",step,(unsigned)rc);
    nfsu2_native_audio_close();return 0;
}
int nfsu2_native_audio_write(const int16_t *pcm)
{
    int16_t *block=blocks[next_block++%16];memcpy(block,pcm,256*channels*sizeof(*pcm));
    const void *ptr=block;AudioAttribute attr={0,0,&ptr,sizeof(ptr)};
    int rc=sceAudioOut2PortSetAttributes(port,&attr,1);if(rc<0)return rc;
    rc=sceAudioOut2ContextAdvance(context);if(rc<0)return rc;
    return sceAudioOut2ContextPush(context,1);
}
