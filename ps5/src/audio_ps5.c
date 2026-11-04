/* reUndr 2 PS5, MIT. 48 kHz stereo PCM through libSceAudioOut.
 * The APU never blocks on the output device: a bounded queue joins the clocks.
 * The two clocks drift. Dropping a frame on a full queue and playing
 * silence on an empty one were each an audible click, so the worker resamples
 * by at most 0.8% (inaudible) to hold the queue near TARGET frames; drop and
 * silence are left for real stalls. */
#include "apu/apu_xaudio2.h"
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
extern int sceAudioOutInit(void);
extern int sceAudioOutOpen(int, int, int, unsigned, unsigned, unsigned);
extern int sceAudioOutOutput(int, const void *);
extern int sceAudioOutClose(int);
#define GRAIN 256
#define PRIME 4
#define TARGET 8
#define CAPACITY 24
#define RING ((CAPACITY + 1) * GRAIN) /* room for a partly consumed frame */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t lifecycle = PTHREAD_MUTEX_INITIALIZER;
static pthread_t worker;
static int handle = -1, running, worker_created;
static int16_t ring[RING][2];
static unsigned head, avail; /* stereo samples */
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
        for (int c = 0; c < 2; c++) {
            float v = (float)a[c] + ((float)b[c] - (float)a[c]) * f;
            frame[i * 2 + c] = (int16_t)(v < 0 ? v - 0.5f : v + 0.5f);
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
    for (;;) {
        int16_t frame[GRAIN * 2] = {0};
        pthread_mutex_lock(&lock);
        if (!running) { pthread_mutex_unlock(&lock); break; }
        if (!resample(frame)) ++starved;
        pthread_mutex_unlock(&lock);
        int rc = sceAudioOutOutput(handle, frame); /* device clock, about 5.33 ms */
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
    sceAudioOutInit();
    handle = sceAudioOutOpen(255, 0, 0, GRAIN, 48000, 1); /* S16_STEREO */
    if (handle < 0) { pthread_mutex_unlock(&lifecycle); return 0; }
    pthread_mutex_lock(&lock);
    head = 0; avail = PRIME * GRAIN; dropped = starved = 0;
    phase = 0; depth = TARGET; ratio = 1.0f;
    memset(ring, 0, sizeof(ring)); running = 1;
    pthread_mutex_unlock(&lock);
    if (pthread_create(&worker, NULL, output_worker, NULL) != 0) {
        pthread_mutex_lock(&lock); running = 0; pthread_mutex_unlock(&lock);
        sceAudioOutClose(handle); handle = -1;
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
    if (handle >= 0) { sceAudioOutOutput(handle, NULL); sceAudioOutClose(handle); }
    handle = -1;
    pthread_mutex_unlock(&lifecycle);
}
int xa2_is_active(void)
{
    pthread_mutex_lock(&lock); int active = running; pthread_mutex_unlock(&lock); return active;
}
int xa2_submit_samples(const int16_t *samples, int num_samples)
{
    if (!samples || num_samples <= 0 || num_samples > GRAIN) return 0;
    pthread_mutex_lock(&lock);
    if (!running || avail >= CAPACITY * GRAIN) {
        if (running) ++dropped;
        pthread_mutex_unlock(&lock); return 0;
    }
    for (int i = 0; i < GRAIN; i++) { /* a short submission keeps the frame cadence */
        int16_t *dst = ring[(head + avail + i) % RING];
        if (i < num_samples) apu_output_safety(samples+i*2,dst,2); else dst[0]=dst[1]=0;
    }
    avail += GRAIN;
    pthread_mutex_unlock(&lock);
    return 1;
}
int xa2_get_buffer_size(void) { return GRAIN; }
int xa2_queued(void) { pthread_mutex_lock(&lock); int n=avail/GRAIN; pthread_mutex_unlock(&lock); return n; }
int xa2_queue_target(void) { return TARGET; }
void xa2_get_stats(Xa2Stats *out) { if (!out) return; pthread_mutex_lock(&lock); memset(out,0,sizeof(*out));out->dropped=dropped;out->underruns=starved;pthread_mutex_unlock(&lock); }
static float master_volume=0.5f;
void xa2_set_master_volume(float v) { master_volume = v<0?0:v>1?1:v; }
void apu_output_safety(const int16_t *in,int16_t *out,int n) {
    for(int i=0;i<n;i++) { float v=in[i]*master_volume; if(v>16422)v=16422;if(v< -16422)v= -16422;out[i]=(int16_t)v; }
}
