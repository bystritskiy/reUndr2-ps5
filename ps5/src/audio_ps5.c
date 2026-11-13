/* reUndr 2 PS5, MIT. 48 kHz multichannel PCM through libSceAudioOut.
 * The APU never blocks on the output device: a bounded queue joins the clocks.
 * The two clocks drift. Dropping a frame on a full queue and playing
 * silence on an empty one were each an audible click, so the worker resamples
 * by at most 0.8% (inaudible) to hold the queue near TARGET frames; drop and
 * silence are left for real stalls. */
#include "apu/apu_xaudio2.h"
#include "audio_native.h"
#include "audio_enhance.h"
#include <stdatomic.h>
#include <stdlib.h>
/* Set from Options -> Audio (recomp_manual.c): Multichannel Sound adds the front
 * pair to every speaker; Speaker Test 1..8 plays one channel, 9 walks them
 * (FL FR C LFE LS RS BL BR). Read by the output worker. */
static _Atomic int spread_audio = 1, speaker_test;
void nfsu2_audio_set_spread(int on){atomic_store(&spread_audio,on!=0);}
void nfsu2_audio_set_test(int channel){atomic_store(&speaker_test,channel>=0&&channel<=9?channel:0);}
static int native_audio;
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
extern int sceAudioOutInit(void);
extern int sceAudioOutOpen(int, int, int, unsigned, unsigned, unsigned);
extern int sceAudioOutOutput(int, const void *);
extern int sceAudioOutClose(int);
extern int sceAudioOutSetVolume(int, int, const int *);
extern int sceAudioOutGetPortState(int, void *);
extern int sceAudioOutSysGetHdmiMonitorInfo(int, void *, unsigned);
static void log_port_state(int port)
{
    if(native_audio){nfsu2_native_audio_log();return;}
    /* Public AudioOut v1 prefix; oversized storage also allows a longer
     * PS5 response. Log raw words instead of guessing undocumented fields. */
    uint64_t state[32] = {0};
    int rc = sceAudioOutGetPortState(port, state);
    unsigned char *b = (unsigned char *)state;
    fprintf(stderr, "[PS5 audio] port-state rc=%x output=%02x%02x channel-byte=%u raw=%016llx %016llx %016llx %016llx\n",
            (unsigned)rc,b[1],b[0],b[2],(unsigned long long)state[0],
            (unsigned long long)state[1],(unsigned long long)state[2],(unsigned long long)state[3]);
}
#define GRAIN 256
#define MAX_CHANNELS 8
#define S16_STEREO 1
#define S16_8CH 2
static int output_channels = 2;
#define PRIME 4
#define TARGET 8
#define CAPACITY 24
#define RING ((CAPACITY + 1) * GRAIN) /* room for a partly consumed frame */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t lifecycle = PTHREAD_MUTEX_INITIALIZER;
static pthread_t worker;
static int handle = -1, running, worker_created;
static int16_t ring[RING][MAX_CHANNELS];
static unsigned head, avail; /* PCM frames, independent of channel count */
static float phase, depth, ratio;
static int dropped, starved;
static void drift_update(void)
{
    float r;
    depth += ((float)avail / GRAIN - depth) * 0.02f; /* ~50-frame average */
    r = 1.0f + (depth - TARGET) * 0.0015f;
    if (r > 1.008f) r = 1.008f;
    if (r < 0.992f) r = 0.992f;
    if (r - ratio > 0.0005f || ratio - r > 0.0005f) ratio = r;
}
static int resample(int16_t *frame)
{
    unsigned need, used;
    float p = phase;
    drift_update();
    need = (unsigned)(phase + ratio * (GRAIN - 1)) + 1;
    if (avail < need) return 0;
    for (unsigned i = 0; i < GRAIN; i++, p += ratio) {
        unsigned k = (unsigned)p;
        float f = p - (float)k;
        const int16_t *a = ring[(head + k) % RING], *b = k + 1 < avail ? ring[(head + k + 1) % RING] : a;
        for (int c = 0; c < output_channels; c++) {
            float v = (float)a[c] + ((float)b[c] - (float)a[c]) * f;
            frame[i * output_channels + c] = (int16_t)(v < 0 ? v - 0.5f : v + 0.5f);
        }
    }
    used = (unsigned)p;
    phase = p - (float)used;
    head = (head + used) % RING; avail -= used;
    return 1;
}
static void *output_worker(void *unused)
{
    (void)unused;
    unsigned blocks = 0;
    AudioEnhance enhance={0};
    int last_test_channel=-1;
    int peaks[MAX_CHANNELS] = {0};
    for (;;) {
        int16_t frame[GRAIN * MAX_CHANNELS] = {0};
        pthread_mutex_lock(&lock);
        if (!running) { pthread_mutex_unlock(&lock); break; }
        if (!resample(frame)) ++starved;
        pthread_mutex_unlock(&lock);
        int test_channel=audio_enhance(frame,GRAIN,output_channels,
            atomic_load(&spread_audio),atomic_load(&speaker_test),&enhance);
        if(test_channel!=last_test_channel){
            fprintf(stderr,"[PS5 audio] speaker test channel=%d (FL FR C LFE LS RS BL BR)\n",test_channel);
            last_test_channel=test_channel;
        }
        for (unsigned i = 0; i < GRAIN; i++)
            for (int c = 0; c < output_channels; c++) {
                int v = abs((int)frame[i * output_channels + c]);
                if (v > peaks[c]) peaks[c] = v;
            }
        if (++blocks == 1875) { /* ten seconds: verify non-silent routing in a race */
            fprintf(stderr, "[PS5 audio] channel peaks FL/FR/C/LFE/LS/RS/extra: %d %d %d %d %d %d %d %d\n",
                    peaks[0], peaks[1], peaks[2], peaks[3], peaks[4], peaks[5], peaks[6], peaks[7]);
            log_port_state(handle);
            memset(peaks, 0, sizeof(peaks)); blocks = 0;
        }
        int rc = native_audio ? nfsu2_native_audio_write(frame) : sceAudioOutOutput(handle, frame); /* device clock, about 5.33 ms */
        if (rc < 0) {
            fprintf(stderr, "[PS5 audio] output failed: %x\n", (unsigned)rc);
            pthread_mutex_lock(&lock); running = 0; pthread_mutex_unlock(&lock);
            break;
        }
    }
    return NULL;
}
int xa2_init(void)
{
    pthread_mutex_lock(&lifecycle);
    if (worker_created) {
        int active = xa2_is_active(); pthread_mutex_unlock(&lifecycle); return active;
    }
    const char *volume=getenv("RECOMP_AUDIO_VOLUME");
    if(volume)xa2_set_master_volume((float)atof(volume)/100.0f);
    const char *channels = getenv("NFSU2_AUDIO_CHANNELS");
    output_channels = channels && atoi(channels) == 2 ? 2 : 8;
    const char *backend=getenv("NFSU2_AUDIO_BACKEND");
    native_audio=!(backend && !strcmp(backend,"legacy")) && nfsu2_native_audio_open(output_channels);
    handle=-1;
    if(!native_audio){
    sceAudioOutInit();
    handle = sceAudioOutOpen(255, 0, 0, GRAIN, 48000,
                            output_channels == 8 ? S16_8CH : S16_STEREO);
    if (handle < 0 && output_channels == 8) {
        fprintf(stderr, "[PS5 audio] 8ch open failed: %x; falling back to stereo\n", (unsigned)handle);
        output_channels = 2;
        handle = sceAudioOutOpen(255, 0, 0, GRAIN, 48000, S16_STEREO);
    }
    fprintf(stderr, "[PS5 audio] 48000 Hz S16, %d channels, port %d\n", output_channels, handle);
    if (handle < 0) { pthread_mutex_unlock(&lifecycle); return 0; }
    int volumes[8] = {32768,32768,32768,32768,32768,32768,32768,32768};
    int volume_rc = sceAudioOutSetVolume(handle, output_channels == 8 ? 0xff : 3, volumes);
    fprintf(stderr, "[PS5 audio] set all %d channel gains to 0dB: rc=%x\n", output_channels, (unsigned)volume_rc);
    log_port_state(handle);
    uint64_t hdmi[0x180 / 8] = {0};
    int hdmi_rc = sceAudioOutSysGetHdmiMonitorInfo(1, hdmi, sizeof(hdmi));
    fprintf(stderr, "[PS5 audio] HDMI monitor rc=%x\n", (unsigned)hdmi_rc);
    if (hdmi_rc == 0) {
        const unsigned char *b = (const unsigned char *)hdmi;
        for (unsigned off = 0x90; off < sizeof(hdmi); off += 8)
            fprintf(stderr, "[PS5 audio] HDMI audio-info[%03x]=%02x %02x %02x %02x %02x %02x %02x %02x\n",
                    off,b[off],b[off+1],b[off+2],b[off+3],b[off+4],b[off+5],b[off+6],b[off+7]);
    }
    }
    pthread_mutex_lock(&lock);
    head = 0; avail = PRIME * GRAIN; dropped = starved = 0;
    phase = 0; depth = TARGET; ratio = 1.0f;
    memset(ring, 0, sizeof(ring)); running = 1;
    pthread_mutex_unlock(&lock);
    if (pthread_create(&worker, NULL, output_worker, NULL) != 0) {
        pthread_mutex_lock(&lock); running = 0; pthread_mutex_unlock(&lock);
        if(native_audio)nfsu2_native_audio_close();else sceAudioOutClose(handle); handle = -1;native_audio=0;
        pthread_mutex_unlock(&lifecycle); return 0;
    }
    worker_created = 1;
    pthread_mutex_unlock(&lifecycle);
    return 1;
}
void xa2_shutdown(void)
{
    pthread_mutex_lock(&lifecycle);
    pthread_mutex_lock(&lock); running = 0; pthread_mutex_unlock(&lock);
    if (worker_created) pthread_join(worker, NULL);
    worker_created = 0;
    if(native_audio)nfsu2_native_audio_close();
    else if (handle >= 0) { sceAudioOutOutput(handle, NULL); sceAudioOutClose(handle); }
    native_audio=0;
    handle = -1;
    pthread_mutex_unlock(&lifecycle);
}
int xa2_is_active(void)
{
    pthread_mutex_lock(&lock); int active = running; pthread_mutex_unlock(&lock); return active;
}
static int submit_pcm(const int16_t *samples, int num_samples, int channels)
{
    if (!samples || num_samples <= 0 || num_samples > GRAIN) return 0;
    pthread_mutex_lock(&lock);
    if (!running || avail >= CAPACITY * GRAIN) {
        if (running) ++dropped;
        pthread_mutex_unlock(&lock); return 0;
    }
    for (int i = 0; i < GRAIN; i++) { /* a short submission keeps the frame cadence */
        int16_t *dst = ring[(head + avail + i) % RING];
        memset(dst, 0, MAX_CHANNELS * sizeof(*dst));
        if (i < num_samples) {
            if (channels == output_channels)
                apu_output_safety(samples + i * channels, dst, channels);
            else if (channels == 2)
                apu_output_safety(samples + i * 2, dst, 2); /* stereo stays at front */
            else {
                /* Defensive fallback for callers submitting 5.1 to a stereo port. */
                const int16_t *src = samples + i * channels;
                for (int c = 0; c < 2; c++) {
                    float v = src[c] + 0.70710678f * (src[2] + src[4+c] + src[6+c]);
                    int16_t v16 = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
                    apu_output_safety(&v16, dst+c, 1);
                }
            }
        }
    }
    avail += GRAIN;
    pthread_mutex_unlock(&lock);
    return 1;
}
int xa2_submit_samples(const int16_t *samples, int n) { return submit_pcm(samples, n, 2); }
int xa2_submit_surround(const int16_t *samples, int n) { return submit_pcm(samples, n, 8); }
int xa2_output_channels(void) { return output_channels; }
int xa2_get_buffer_size(void) { return GRAIN; }
int xa2_queued(void) { pthread_mutex_lock(&lock); int n=avail/GRAIN; pthread_mutex_unlock(&lock); return n; }
int xa2_queue_target(void) { return TARGET; }
void xa2_get_stats(Xa2Stats *out) { if (!out) return; pthread_mutex_lock(&lock); memset(out,0,sizeof(*out));out->dropped=dropped;out->underruns=starved;pthread_mutex_unlock(&lock); }
static float master_volume=0.5f;
void xa2_set_master_volume(float v) { master_volume = v<0?0:v>1?1:v; }
/* Silent while the port menu holds the title (its voices would loop). */
static volatile int muted;
void nfsu2_audio_mute(int on) { muted = on != 0; }
void apu_output_safety(const int16_t *in,int16_t *out,int n) {
    const float gain = muted ? 0.0f : master_volume;
    for(int i=0;i<n;i++) { float v=in[i]*gain; if(v>16422)v=16422;if(v< -16422)v= -16422;out[i]=(int16_t)v; }
}
