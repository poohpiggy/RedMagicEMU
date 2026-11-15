#include "../aod_runtime.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Public callers must be able to enforce the backend's dimension limit. */
#ifndef AOD_MAX_DIMENSION
#define AOD_MAX_DIMENSION 0u
#endif

struct fake { int calls; int mode; };
static int fake_infer(void *context, const float *input, size_t count, float *output,
                      size_t capacity, struct aod_shape *shape) {
    struct fake *f = context; f->calls++;
    assert(count == AOD_INPUT_FLOATS && capacity == AOD_PIXELS);
    assert(fabsf(input[0] - ((1.0f - .485f) / .229f)) < 1e-5f);
    assert(fabsf(input[AOD_PIXELS] - ((0.0f - .456f) / .224f)) < 1e-5f);
    assert(fabsf(input[2u*AOD_PIXELS] - (((128.0f/255.0f) - .406f) / .225f)) < 1e-5f);
    shape->rank=4; shape->dimensions[0]=1; shape->dimensions[1]=1;
    shape->dimensions[2]=512; shape->dimensions[3]=512;
    if(f->mode==6) output[0]=1.0f;
    else for (size_t i=0; i<capacity; ++i) output[i]=(float)(i%11)-5.0f;
    if (f->mode==1) return -7;
    if (f->mode==2) shape->dimensions[2]=511;
    if (f->mode==3) output[capacity-1]=NAN;
    if (f->mode==4) output[capacity-1]=INFINITY;
    if (f->mode==5) shape->rank=3;
    return 0;
}
int main(void) {
    assert(AOD_MAX_DIMENSION==4096u);
    uint8_t rgb[3]={255,0,128}; struct fake f={0,0};
    float *out=malloc(AOD_PIXELS*sizeof(*out)); assert(out);
    for(size_t i=0;i<AOD_PIXELS;i++) out[i]=123.0f;
    assert(aod_infer_rgb(NULL,3,1,1,fake_infer,&f,out,AOD_PIXELS)==AOD_INVALID_ARGUMENT);
    assert(aod_infer_rgb(rgb,2,1,1,fake_infer,&f,out,AOD_PIXELS)==AOD_INVALID_ARGUMENT);
    assert(aod_infer_rgb(rgb,3,0,1,fake_infer,&f,out,AOD_PIXELS)==AOD_INVALID_ARGUMENT);
    assert(aod_infer_rgb(rgb,3,1,1,NULL,&f,out,AOD_PIXELS)==AOD_INVALID_ARGUMENT);
    assert(aod_infer_rgb(rgb,3,1,1,fake_infer,&f,out,AOD_PIXELS-1)==AOD_INVALID_ARGUMENT);
    assert(aod_infer_rgb(rgb,3,UINT32_MAX,UINT32_MAX,fake_infer,&f,out,AOD_PIXELS)==AOD_INVALID_ARGUMENT);
    assert(f.calls==0 && out[0]==123.0f);
    assert(aod_infer_rgb(rgb,3,1,1,fake_infer,&f,out,AOD_PIXELS)==AOD_OK);
    assert(f.calls==1 && out[0]==-5.0f && out[10]==5.0f);
    for(int mode=1;mode<=6;mode++) {
        f.mode=mode; out[0]=123.0f; out[AOD_PIXELS-1]=321.0f;
        enum aod_result expected=mode==1?AOD_INFERENCE_FAILED:AOD_INVALID_OUTPUT;
        assert(aod_infer_rgb(rgb,3,1,1,fake_infer,&f,out,AOD_PIXELS)==expected);
        assert(out[0]==123.0f && out[AOD_PIXELS-1]==321.0f);
    }
    f.mode=0;
    assert(aod_infer_rgb(rgb,3,1,1,fake_infer,&f,out,AOD_PIXELS)==AOD_OK);
    free(out); puts("AOD_RUNTIME_OK"); return 0;
}
