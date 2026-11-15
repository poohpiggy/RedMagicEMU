#include "../aod_runtime.h"
#include "../aod_ort.h"
#include "../../aod/aod_mask.h"
#include "../../aod/aod_postprocess.h"
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct backend { struct aod_ort *ort; int fail_next; };
static int infer(void *opaque,const uint8_t *rgb,int width,int height,uint8_t *alpha,size_t pixels) {
    struct backend *b=opaque;
    if(b->fail_next) { b->fail_next=0; return -1; }
    float *logits=malloc((size_t)AOD_PIXELS*sizeof(*logits));
    uint8_t *soft=malloc(AOD_PIXELS),*binary=malloc(AOD_PIXELS); int status=-1;
    if(!logits || !soft || !binary) goto done;
    if(aod_infer_rgb(rgb,pixels*3,(uint32_t)width,(uint32_t)height,aod_ort_infer,b->ort,logits,AOD_PIXELS)!=AOD_OK) goto done;
    for(size_t i=0;i<AOD_PIXELS;i++) {
        float z=fmaxf(-60.0f,fminf(60.0f,logits[i]));
        soft[i]=(uint8_t)(255.0f/(1.0f+expf(-z)));
    }
    if(aod_prepare_mask(soft,AOD_PIXELS,AOD_SIDE,AOD_SIDE,binary,AOD_PIXELS)!=0) goto done;
    if(aod_finish_mask(binary,AOD_PIXELS,AOD_SIDE,AOD_SIDE,alpha,pixels,width,height)!=0) goto done;
    status=0;
done:
    free(binary); free(soft); free(logits); return status;
}
static unsigned dimension(const char *s) {
    char *end=NULL; errno=0; unsigned long n=strtoul(s,&end,10);
    assert(!errno && end && !*end && n>0 && n<=4096); return (unsigned)n;
}
static void expect_zero(const uint8_t *p,size_t n) { for(size_t i=0;i<n;i++) assert(p[i]==0); }
int main(int argc,char **argv) {
    if(argc!=7) { fprintf(stderr,"usage: e2e ORT_SO MODEL INPUT_RGB WIDTH HEIGHT OUTPUT_BGRA\n"); return 2; }
    unsigned w=dimension(argv[4]),h=dimension(argv[5]); size_t pixels=(size_t)w*h;
    assert(pixels<=16777216u);
    uint8_t *rgb=malloc(pixels*3),*bgra=malloc(pixels*4),*first=malloc(pixels*4); assert(rgb&&bgra&&first);
    FILE *f=fopen(argv[3],"rb"); assert(f); assert(fread(rgb,1,pixels*3,f)==pixels*3); assert(fgetc(f)==EOF && !ferror(f)); assert(fclose(f)==0);
    void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); if(!lib) { fprintf(stderr,"dlopen: %s\n",dlerror()); return 1; }
    const OrtApiBase *(*get_base)(void)=dlsym(lib,"OrtGetApiBase"); assert(get_base);
    const OrtApi *api=get_base()->GetApi(ORT_API_VERSION); assert(api);
    struct aod_ort *ort=NULL; char error[512]={0};
    enum aod_result init=aod_ort_create(api,argv[2],&ort,error,sizeof(error));
    if(init!=AOD_OK) { fprintf(stderr,"ORT init %d: %s\n",init,error); return 1; }
    struct backend backend={ort,0}; struct aod_mask_state state; aod_mask_init(&state,infer,&backend);
    assert(aod_mask_run(&state,rgb,pixels*3,(int)w,(int)h,bgra,pixels*4)==0 && state.has_alpha);
    size_t top=0; while(top<pixels && !bgra[top*4+3]) top++; assert(top<pixels);
    assert(state.top_x==(int)(top%w) && state.top_y==(int)(top/w));
    for(size_t i=0;i<pixels;i++) assert(bgra[i*4]==rgb[i*3+2] && bgra[i*4+1]==rgb[i*3+1] && bgra[i*4+2]==rgb[i*3]);
    int top_x=state.top_x,top_y=state.top_y; memcpy(first,bgra,pixels*4);
    backend.fail_next=1; assert(aod_mask_run(&state,rgb,pixels*3,(int)w,(int)h,bgra,pixels*4)!=0);
    assert(!state.has_alpha && state.top_x==-1 && state.top_y==(int)h); expect_zero(bgra,pixels*4);
    assert(aod_mask_run(&state,rgb,pixels*3-1,(int)w,(int)h,bgra,pixels*4)!=0);
    assert(!state.has_alpha && state.top_x==-1); expect_zero(bgra,pixels*4);
    assert(aod_mask_run(&state,rgb,pixels*3,(int)w,(int)h,bgra,pixels*4)==0 && state.has_alpha);
    assert(state.top_x==top_x && state.top_y==top_y && memcmp(first,bgra,pixels*4)==0);
    /* Fail if the destination already exists; image data stays outside the repository. */
    f=fopen(argv[6],"wbx"); assert(f); assert(fwrite(bgra,1,pixels*4,f)==pixels*4 && fclose(f)==0);
    aod_mask_release(&state); assert(!state.has_alpha);
    assert(aod_mask_run(&state,rgb,pixels*3,(int)w,(int)h,bgra,pixels*4)!=0); expect_zero(bgra,pixels*4);
    printf("AOD_E2E_MODEL_OK width=%u height=%u top_x=%d top_y=%d\n",w,h,top_x,top_y);
    aod_ort_destroy(ort); assert(dlclose(lib)==0); free(first); free(bgra); free(rgb); return 0;
}
