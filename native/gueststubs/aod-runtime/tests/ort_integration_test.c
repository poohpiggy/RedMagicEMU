#include "../aod_ort.h"
#include <assert.h>
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc,char **argv) {
    assert(argc==4); void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!lib) { fprintf(stderr,"dlopen: %s\n",dlerror()); return 1; }
    const OrtApiBase *(*get_base)(void)=dlsym(lib,"OrtGetApiBase"); assert(get_base);
    const OrtApi *api=get_base()->GetApi(ORT_API_VERSION); assert(api);
    struct aod_ort *ctx=(struct aod_ort *)(uintptr_t)1; char error[512]={0};
    assert(aod_ort_create(api,"/definitely/not/a/model.onnx",&ctx,error,sizeof(error))==AOD_INFERENCE_FAILED);
    assert(ctx==NULL && error[0]);
    assert(aod_ort_create(api,argv[2],&ctx,error,sizeof(error))==AOD_OK && ctx);
    float *out=malloc(AOD_PIXELS*sizeof(*out)); assert(out);
    for(size_t i=0;i<AOD_PIXELS;i++) out[i]=123.0f;
    uint8_t rgb[]={255,0,128};
    assert(aod_infer_rgb(rgb,sizeof(rgb),1,1,aod_ort_infer,ctx,out,AOD_PIXELS)==AOD_OK);
    float lo=out[0],hi=out[0];
    for(size_t i=0;i<AOD_PIXELS;i++) { assert(isfinite(out[i])); if(out[i]<lo) lo=out[i]; if(out[i]>hi) hi=out[i]; }
    assert(lo<hi && lo!=123.0f); printf("AOD_ORT_MODEL_OK min=%g max=%g\n",lo,hi);
    float x[1]={0}; struct aod_shape shape={0,{0,0,0,0}};
    assert(aod_ort_infer(ctx,x,1,out,AOD_PIXELS,&shape)!=0 && *aod_ort_error(ctx));
    float *invalid=calloc(AOD_INPUT_FLOATS,sizeof(*invalid)); assert(invalid);
    invalid[1]=NAN; out[0]=123.0f;
    assert(aod_ort_infer(ctx,invalid,AOD_INPUT_FLOATS,out,AOD_PIXELS,&shape)!=0);
    assert(strstr(aod_ort_error(ctx),"non-finite input") && out[0]==123.0f); free(invalid);
    assert(aod_infer_rgb(rgb,sizeof(rgb),1,1,aod_ort_infer,ctx,out,AOD_PIXELS)==AOD_OK);
    assert(!*aod_ort_error(ctx));
    /* Synthetic high-frequency pattern, reference: Pillow 13.0 + ORT 1.31 CPU. */
    const unsigned w=1024,h=768; uint8_t *pattern=malloc((size_t)w*h*3); assert(pattern);
    for(unsigned y=0;y<h;y++) for(unsigned xx=0;xx<w;xx++) for(unsigned c=0;c<3;c++)
        pattern[((size_t)y*w+xx)*3+c]=(uint8_t)((xx*17+y*23+c*67)%256);
    assert(aod_infer_rgb(pattern,(size_t)w*h*3,w,h,aod_ort_infer,ctx,out,AOD_PIXELS)==AOD_OK);
    const unsigned rows[]={0,128,255,400,511},cols[]={0,256,255,300,511};
    const float expected[]={-10.086398f,-13.187955f,-13.333802f,-11.822474f,-10.842399f};
    float worst=0;
    for(unsigned i=0;i<5;i++) { float error=fabsf(out[(size_t)rows[i]*512+cols[i]]-expected[i]); if(error>worst) worst=error; assert(error<0.35f); }
    printf("AOD_ORT_PATTERN_REFERENCE_OK max_logit_error=%g\n",worst); free(pattern);
    free(out); aod_ort_destroy(ctx); ctx=(struct aod_ort *)(uintptr_t)1; error[0]=0;
    assert(aod_ort_create(api,argv[3],&ctx,error,sizeof(error))==AOD_INFERENCE_FAILED);
    assert(ctx==NULL && strstr(error,"dimensions"));
    puts("AOD_ORT_BAD_MODEL_REJECTED"); assert(dlclose(lib)==0); return 0;
}
