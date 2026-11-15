/* Experimental adapter. A build must explicitly define a validated model path;
 * an unspecified path fails initialization rather than reporting success. */
#include "aod_backend.h"
#include "../aod-runtime/aod_ort.h"
#include "../aod-runtime/aod_runtime.h"
#include <stdio.h>

int aod_backend_open(void **out) {
    if (!out) return -1;
    *out=NULL;
#ifdef RME_AOD_MODEL_PATH
    const OrtApiBase *base=OrtGetApiBase();
    if (!base) { fputs("RME AOD: ORT API base unavailable\n",stderr);return -1; }
    const OrtApi *api=base->GetApi(ORT_API_VERSION);
    if (!api) { fputs("RME AOD: ORT API version unavailable\n",stderr);return -1; }
    struct aod_ort *ctx=NULL;
    char error[256];
    enum aod_result status=aod_ort_create(api,RME_AOD_MODEL_PATH,&ctx,error,sizeof(error));
    if (status!=AOD_OK || !ctx) {
        fprintf(stderr,"RME AOD: model initialization failed (category %d)\n",(int)status);
        if (ctx) aod_ort_destroy(ctx);
        return -1;
    }
    *out=ctx;
    return 0;
#else
    fputs("RME AOD: model path not configured\n",stderr);
    return -1;
#endif
}

int aod_backend_run(void *context,const uint8_t *rgb,size_t length,int width,int height,
                    float *logits,size_t capacity) {
    if (!context || width<=0 || height<=0) return -1;
    enum aod_result status=aod_infer_rgb(rgb,length,(uint32_t)width,(uint32_t)height,
                                         aod_ort_infer,context,logits,capacity);
    if (status!=AOD_OK) fprintf(stderr,"RME AOD: inference failed (category %d)\n",(int)status);
    return status==AOD_OK?0:-1;
}

void aod_backend_close(void *context) {
    if (context) aod_ort_destroy(context);
}
