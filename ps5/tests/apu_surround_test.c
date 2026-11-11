/* Drive the production DSP bypass through every 32-sample slice of a block. */
#include "apu/apu_state.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct McpxApuDebug g_dbg;
int main(void)
{
    MCPXAPUState *d = calloc(1,sizeof(*d));assert(d);
    float bins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = {{0}};
    /* Production init configures the legacy stereo reference too. */
    mcpx_apu_dsp_init(d);
    for(int slice=0;slice<8;slice++){
        d->ep_frame_div=slice;
        memset(bins,0,sizeof bins);bins[8][3]=0.25f;
        mcpx_apu_dsp_frame(d,bins);
        for(int i=0;i<NUM_SAMPLES_PER_FRAME;i++)for(int c=0;c<8;c++)
            assert((i==3&&c==4) ? d->monitor.surround_buf[slice*32+i][c]>0 : d->monitor.surround_buf[slice*32+i][c]==0);
        assert(d->monitor.frame_buf[slice*32+3][0]>0); /* fallback keeps rear audible */
        assert(d->monitor.frame_buf[slice*32+3][1]==0);
    }
    /* No stale rear signal when a new stereo-only frame replaces it. */
    d->ep_frame_div=0;memset(bins,0,sizeof bins);bins[0][0]=0.25f;bins[1][1]=0.125f;
    mcpx_apu_dsp_frame(d,bins);
    for(int i=0;i<32;i++)for(int c=2;c<8;c++)assert(d->monitor.surround_buf[i][c]==0);
    free(d->gp.dsp);free(d->ep.dsp);free(d);
    puts("PASS: production APU DSP output, all eight block slices, rear separation and overwrite");
}
