#include "../aod_runtime.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Pillow 13.0 bilinear resize, then RGB ImageNet normalization. */
struct expected { const unsigned char (*colors)[3]; };
static float worst_byte_error;
static int compare(void *opaque,const float *input,size_t n,float *logits,size_t cap,struct aod_shape *shape) {
    struct expected *e=opaque; assert(n==AOD_INPUT_FLOATS && cap==AOD_PIXELS);
    const unsigned px[]={0,1,255,511,256,300}, py[]={0,0,255,511,128,400};
    const float mean[]={.485f,.456f,.406f}, sd[]={.229f,.224f,.225f};
    for(unsigned i=0;i<6;i++) for(unsigned c=0;c<3;c++) {
        float got=(input[(size_t)c*AOD_PIXELS+(size_t)py[i]*512+px[i]]*sd[c]+mean[c])*255.0f;
        if(fabsf(got-e->colors[i][c])>worst_byte_error) worst_byte_error=fabsf(got-e->colors[i][c]);
        assert(fabsf(got-e->colors[i][c])<=1.01f);
    }
    for(size_t i=0;i<cap;i++) logits[i]=0;
    shape->rank=4; shape->dimensions[0]=1; shape->dimensions[1]=1;
    shape->dimensions[2]=512; shape->dimensions[3]=512; return 0;
}
static void run(unsigned w,unsigned h,const unsigned char colors[6][3]) {
    uint8_t *rgb=malloc((size_t)w*h*3); float *out=malloc((size_t)AOD_PIXELS*sizeof(*out)); assert(rgb && out);
    for(unsigned y=0;y<h;y++) for(unsigned x=0;x<w;x++) for(unsigned c=0;c<3;c++)
        rgb[((size_t)y*w+x)*3+c]=(uint8_t)((x*17+y*23+c*67)%256);
    struct expected e={colors};
    assert(aod_infer_rgb(rgb,(size_t)w*h*3,w,h,compare,&e,out,AOD_PIXELS)==AOD_OK);
    free(rgb); free(out);
}
static int compare_vertical(void *unused,const float *input,size_t n,float *logits,size_t cap,struct aod_shape *shape) {
    (void)unused; assert(n==AOD_INPUT_FLOATS && cap==AOD_PIXELS);
    const unsigned px[]={0,1,3,5,7,8};
    const unsigned char expected[][3]={{75,141,167},{112,164,150},{169,137,83},{113,89,150},{99,161,156},{137,168,113}};
    const float mean[]={.485f,.456f,.406f}, sd[]={.229f,.224f,.225f};
    for(unsigned i=0;i<6;i++) for(unsigned c=0;c<3;c++) {
        float got=(input[(size_t)c*AOD_PIXELS+px[i]]*sd[c]+mean[c])*255.0f;
        assert(fabsf(got-expected[i][c])<0.01f);
    }
    for(size_t i=0;i<cap;i++) logits[i]=0;
    shape->rank=4; shape->dimensions[0]=1; shape->dimensions[1]=1;
    shape->dimensions[2]=512; shape->dimensions[3]=512; return 0;
}
static void run_vertical(void) {
    const unsigned w=1216,h=2688; uint8_t *rgb=malloc((size_t)w*h*3);
    float *out=malloc((size_t)AOD_PIXELS*sizeof(*out)); assert(rgb && out);
    for(unsigned y=0;y<h;y++) for(unsigned x=0;x<w;x++) for(unsigned c=0;c<3;c++)
        rgb[((size_t)y*w+x)*3+c]=(uint8_t)((x*17+y*23+c*67)%256);
    assert(aod_infer_rgb(rgb,(size_t)w*h*3,w,h,compare_vertical,NULL,out,AOD_PIXELS)==AOD_OK);
    free(rgb); free(out);
}
static int compare_digest(void *opaque,const float *input,size_t n,float *logits,size_t cap,struct aod_shape *shape) {
    const uint64_t expected=*(const uint64_t *)opaque;
    const float mean[]={.485f,.456f,.406f}, sd[]={.229f,.224f,.225f};
    assert(n==AOD_INPUT_FLOATS && cap==AOD_PIXELS);
    uint64_t digest=UINT64_C(14695981039346656037);
    for(unsigned c=0;c<3;c++) for(size_t i=0;i<AOD_PIXELS;i++) {
        float recovered=(input[(size_t)c*AOD_PIXELS+i]*sd[c]+mean[c])*255.0f;
        long value=lroundf(recovered); assert(value>=0 && value<=255 && fabsf(recovered-(float)value)<0.01f);
        digest=(digest^(uint8_t)value)*UINT64_C(1099511628211);
    }
    assert(digest==expected);
    for(size_t i=0;i<cap;i++) logits[i]=0;
    shape->rank=4; shape->dimensions[0]=1; shape->dimensions[1]=1;
    shape->dimensions[2]=512; shape->dimensions[3]=512; return 0;
}
static void digest_case(unsigned w,unsigned h,uint64_t expected) {
    uint8_t *rgb=malloc((size_t)w*h*3); float *out=malloc((size_t)AOD_PIXELS*sizeof(*out)); assert(rgb && out);
    for(unsigned y=0;y<h;y++) for(unsigned x=0;x<w;x++) for(unsigned c=0;c<3;c++)
        rgb[((size_t)y*w+x)*3+c]=(uint8_t)((x*17+y*23+c*67)%256);
    assert(aod_infer_rgb(rgb,(size_t)w*h*3,w,h,compare_digest,&expected,out,AOD_PIXELS)==AOD_OK);
    free(rgb); free(out);
}
int main(void) {
    static const unsigned char up[6][3]={{0,67,134},{0,67,134},{51,118,185},{103,170,237},{29,96,163},{81,148,215}};
    static const unsigned char down[6][3]={{21,88,155},{52,119,186},{75,142,205},{195,84,73},{78,145,212},{206,60,84}};
    run(3,4,up); run(1024,768,down); run_vertical();
    /* FNV-1a of all channel-major bytes from Pillow 13.0 bilinear output. */
    digest_case(1,1,UINT64_C(0xd0874b8070362325));
    digest_case(3,4,UINT64_C(0x526e31b03b69966d));
    digest_case(512,512,UINT64_C(0x63c9234c4bd62325));
    digest_case(768,1024,UINT64_C(0xd3f144efd91a3880));
    digest_case(1024,768,UINT64_C(0xba66a2b18d840c21));
    digest_case(1216,2688,UINT64_C(0xda1fcd83797494d3));
    digest_case(512,2000,UINT64_C(0x3219e94fb30044f5));
    digest_case(2000,512,UINT64_C(0x3fde2a1b680f21a5));
    digest_case(2048,2048,UINT64_C(0x03a75416ec971d8d));
    digest_case(4096,1,UINT64_C(0xb4f8fcd99a15bf25));
    digest_case(1,4096,UINT64_C(0xdb89800fc8340125));
    printf("AOD_PREPROCESS_REFERENCE_OK max_byte_error=%g\n",worst_byte_error); return 0;
}
