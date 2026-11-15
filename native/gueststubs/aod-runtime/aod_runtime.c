#include "aod_runtime.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define AOD_MAX_TAPS 18
#define AOD_WEIGHT_BITS 22
#define AOD_WEIGHT_SCALE (1u << AOD_WEIGHT_BITS)

struct weights {
    unsigned start;
    unsigned count;
    uint32_t value[AOD_MAX_TAPS];
};

/* Pillow 13.0.0 Resample.c: bilinear coefficients rounded to 22-bit fixed point.
 * See PILLOW_RESIZE_LICENSE.txt for attribution and license. */
static int weights_init(struct weights *all, unsigned source) {
    const double scale=(double)source/AOD_SIDE;
    const double support=scale>1.0?scale:1.0;
    for(unsigned i=0;i<AOD_SIDE;i++) {
        const double center=(i+.5)*scale;
        int first=(int)(center-support+.5), last=(int)(center+support+.5);
        if(first<0) first=0;
        if(last>(int)source) last=(int)source;
        if(last<=first || last-first>AOD_MAX_TAPS) return 0;
        all[i].start=(unsigned)first; all[i].count=(unsigned)(last-first);
        double sum=0, weights[AOD_MAX_TAPS];
        for(int j=first;j<last;j++) {
            double distance=fabs(((double)j-center+.5)*(1.0/support));
            double weight=distance<1.0?1.0-distance:0.0;
            weights[j-first]=weight; sum+=weight;
        }
        if(sum<=0.0) return 0;
        for(unsigned j=0;j<all[i].count;j++)
            all[i].value[j]=(uint32_t)(.5+(weights[j]/sum)*AOD_WEIGHT_SCALE);
    }
    return 1;
}

static uint8_t rounded_byte(uint64_t value) {
    value=(value+(AOD_WEIGHT_SCALE/2))>>AOD_WEIGHT_BITS;
    return (uint8_t)(value>255?255:value);
}

static void preprocess(const uint8_t *rgb, unsigned width, unsigned height,
                       const struct weights *xw, const struct weights *yw,
                       uint8_t *temporary, int vertical_first, float *input) {
    const float mean[3]={.485f,.456f,.406f}, deviation[3]={.229f,.224f,.225f};
    if(vertical_first) {
        for(unsigned y=0;y<AOD_SIDE;y++) for(unsigned x=0;x<width;x++) for(unsigned c=0;c<3;c++) {
            const struct weights *p=&yw[y]; uint64_t value=0;
            for(unsigned k=0;k<p->count;k++) value+=(uint64_t)rgb[((size_t)(p->start+k)*width+x)*3+c]*p->value[k];
            temporary[((size_t)y*width+x)*3+c]=rounded_byte(value);
        }
    } else {
        for(unsigned y=0;y<height;y++) for(unsigned x=0;x<AOD_SIDE;x++) for(unsigned c=0;c<3;c++) {
            const struct weights *p=&xw[x]; uint64_t value=0;
            for(unsigned k=0;k<p->count;k++) value+=(uint64_t)rgb[((size_t)y*width+p->start+k)*3+c]*p->value[k];
            temporary[((size_t)y*AOD_SIDE+x)*3+c]=rounded_byte(value);
        }
    }
    for(unsigned y=0;y<AOD_SIDE;y++) for(unsigned x=0;x<AOD_SIDE;x++) for(unsigned c=0;c<3;c++) {
        const struct weights *p=vertical_first?&xw[x]:&yw[y]; uint64_t value=0;
        for(unsigned k=0;k<p->count;k++) {
            size_t index=vertical_first?((size_t)y*width+p->start+k)*3+c:
                                        ((size_t)(p->start+k)*AOD_SIDE+x)*3+c;
            value+=(uint64_t)temporary[index]*p->value[k];
        }
        float pixel=rounded_byte(value)/255.0f;
        input[(size_t)c*AOD_PIXELS+(size_t)y*AOD_SIDE+x]=(pixel-mean[c])/deviation[c];
    }
}

enum aod_result aod_infer_rgb(const uint8_t *rgb, size_t rgb_bytes, uint32_t width,
                              uint32_t height, aod_infer_fn infer, void *context,
                              float *logits, size_t logits_count) {
    if(!rgb || !width || !height || width>AOD_MAX_DIMENSION || height>AOD_MAX_DIMENSION ||
       rgb_bytes!=(size_t)width*height*3u || !infer || !logits || logits_count<AOD_PIXELS)
        return AOD_INVALID_ARGUMENT;
    struct weights *xw=malloc(AOD_SIDE*sizeof(*xw)), *yw=malloc(AOD_SIDE*sizeof(*yw));
    /* Pillow 13 runs the vertical pass first for a sufficiently tall input. */
    int vertical_first=(height>AOD_SIDE && (int64_t)height-AOD_SIDE>2*((int64_t)width-AOD_SIDE));
    uint8_t *temporary=malloc((size_t)(vertical_first?width:height)*AOD_SIDE*3u);
    float *input=malloc((size_t)AOD_INPUT_FLOATS*sizeof(*input));
    float *result=malloc((size_t)AOD_PIXELS*sizeof(*result));
    enum aod_result status=AOD_NO_MEMORY;
    if(!xw || !yw || !temporary || !input || !result) goto done;
    status=AOD_INVALID_ARGUMENT;
    if(!weights_init(xw,width) || !weights_init(yw,height)) goto done;
    preprocess(rgb,width,height,xw,yw,temporary,vertical_first,input);
    struct aod_shape shape={0,{0,0,0,0}};
    for(size_t i=0;i<AOD_PIXELS;i++) result[i]=NAN;
    status=AOD_INFERENCE_FAILED;
    if(infer(context,input,AOD_INPUT_FLOATS,result,AOD_PIXELS,&shape)!=0) goto done;
    status=AOD_INVALID_OUTPUT;
    if(shape.rank!=4 || shape.dimensions[0]!=1 || shape.dimensions[1]!=1 ||
       shape.dimensions[2]!=AOD_SIDE || shape.dimensions[3]!=AOD_SIDE) goto done;
    for(size_t i=0;i<AOD_PIXELS;i++) if(!isfinite(result[i])) goto done;
    memcpy(logits,result,(size_t)AOD_PIXELS*sizeof(*result)); status=AOD_OK;
done:
    free(result); free(input); free(temporary); free(yw); free(xw); return status;
}
