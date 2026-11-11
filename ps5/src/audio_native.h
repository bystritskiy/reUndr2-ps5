/* reUndr 2 PS5, MIT. Public AudioOut2 ABI subset; no system route overrides. */
#ifndef NFSU2_AUDIO_NATIVE_H
#define NFSU2_AUDIO_NATIVE_H
#include <stdint.h>
#include <stddef.h>
typedef uint64_t AudioContext;
typedef uint64_t AudioPort;
typedef struct {
    uint32_t max_ports, max_object_ports, guarantee_object_ports;
    uint32_t queue_depth, num_grains, flags, reserved[10];
} AudioContextParam;
typedef struct {
    uint16_t port_type, pad;
    uint32_t data_format, sampling_freq, flags;
    uintptr_t user_handle;
    uint32_t reserved[10];
} AudioPortParam;
typedef struct { uint32_t id, reserved; const void *value; size_t size; } AudioAttribute;
typedef struct {
    uint16_t output; uint8_t channels, pad;
    int16_t volume; uint16_t reroute;
    uint32_t flags, pad2; uint64_t reserved[6];
} AudioPortState;
typedef struct {
    uint8_t type,pad; uint16_t pad2; uint32_t available,flags,pad3;
    int16_t angles[16][2];
} AudioSpeakers;
_Static_assert(sizeof(AudioContextParam)==64,"AudioOut2 context ABI");
_Static_assert(sizeof(AudioPortParam)==64,"AudioOut2 port ABI");
_Static_assert(sizeof(AudioAttribute)==24,"AudioOut2 attribute ABI");
_Static_assert(sizeof(AudioPortState)==64,"AudioOut2 state ABI");
_Static_assert(sizeof(AudioSpeakers)==80,"AudioOut2 speaker ABI");
extern int sceAudioOut2Initialize(void);
extern int sceAudioOut2UserCreate(uint32_t,uintptr_t *);
extern int sceAudioOut2UserDestroy(uintptr_t);
extern int sceAudioOut2ContextResetParam(AudioContextParam *);
extern int sceAudioOut2ContextQueryMemory(const AudioContextParam *,size_t *);
extern int sceAudioOut2ContextCreate(const AudioContextParam *,void *,size_t,AudioContext *);
extern int sceAudioOut2ContextDestroy(AudioContext);
extern int sceAudioOut2ContextAdvance(AudioContext);
extern int sceAudioOut2ContextPush(AudioContext,uint32_t);
extern int sceAudioOut2ContextSetAttributes(AudioContext,const AudioAttribute *,uint32_t);
extern int sceAudioOut2PortCreate(AudioContext,const AudioPortParam *,AudioPort *);
extern int sceAudioOut2PortDestroy(AudioPort);
extern int sceAudioOut2PortSetAttributes(AudioPort,const AudioAttribute *,uint32_t);
extern int sceAudioOut2PortGetState(AudioPort,AudioPortState *);
extern int sceAudioOut2GetSpeakerInfo(AudioSpeakers *,uint32_t);
int nfsu2_native_audio_open(int channels);
int nfsu2_native_audio_write(const int16_t *pcm);
void nfsu2_native_audio_log(void);
void nfsu2_native_audio_close(void);
#endif
