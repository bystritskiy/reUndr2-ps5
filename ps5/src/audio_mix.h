/* reUndr 2 PS5, MIT. Xbox DSMIXBIN -> AudioOut S16_8CH speaker bed.
 * First six lanes: FL FR C LFE LS RS. Extra lanes remain silent (Xbox 5.1).
 * No upmix: 3D front/back gains come from the guest voice processor.
 * GP/EP effects are still stubbed. Unknown FX sends retain the legacy
 * front-pair fold, not a made-up direction or an emulated reverb. */
#ifndef NFSU2_AUDIO_MIX_H
#define NFSU2_AUDIO_MIX_H
#include <stdint.h>
#include <math.h>
static inline int16_t nfsu2_audio_s16(float v)
{
    if (!isfinite(v)) return 0;
    return (int16_t)(tanhf(v) * 32767.0f);
}
static inline int16_t nfsu2_audio_add(int16_t a, int16_t b)
{
    int n = (int)a + b;
    return (int16_t)(n > 32767 ? 32767 : n < -32768 ? -32768 : n);
}
static inline void nfsu2_audio_route(const float *bins, unsigned stride,
                                    unsigned sample, int16_t out[8])
{
    /* 0..5: explicit speakers; 6..9: XTLK/3D front L/R, back L/R.
     * HRTF is disabled in this runtime; preserve the guest's four gains. */
#define BIN(n) bins[(n) * stride + sample]
    float fl = BIN(0) + BIN(6), fr = BIN(1) + BIN(7);
    fl += 0.70710678f * BIN(10);
    fr += 0.70710678f * BIN(10);
    for (unsigned b = 11; b < 32; b++) {
        if (b & 1) fr += BIN(b); else fl += BIN(b);
    }
    out[0] = nfsu2_audio_s16(fl);
    out[1] = nfsu2_audio_s16(fr);
    out[2] = nfsu2_audio_s16(BIN(2));
    out[3] = nfsu2_audio_s16(BIN(3));
    out[4] = nfsu2_audio_s16(BIN(4) + BIN(8));
    out[5] = nfsu2_audio_s16(BIN(5) + BIN(9));
    out[6] = out[7] = 0;
#undef BIN
}
#endif
