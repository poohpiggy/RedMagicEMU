#include "aod_ort.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct aod_ort {
    const OrtApi *api;
    OrtEnv *env;
    OrtSession *session;
    OrtMemoryInfo *memory;
    char error[512];
};

static void set_error(struct aod_ort *ctx, const char *text) {
    snprintf(ctx->error,sizeof(ctx->error),"%s",text?text:"unknown error");
}
static int check(struct aod_ort *ctx, OrtStatus *status, const char *operation) {
    if(!status) return 1;
    const char *message=ctx->api->GetErrorMessage(status);
    snprintf(ctx->error,sizeof(ctx->error),"%s: %s",operation,message?message:"unknown error");
    ctx->api->ReleaseStatus(status); return 0;
}
void aod_ort_destroy(struct aod_ort *ctx) {
    if(!ctx) return;
    if(ctx->memory) ctx->api->ReleaseMemoryInfo(ctx->memory);
    if(ctx->session) ctx->api->ReleaseSession(ctx->session);
    if(ctx->env) ctx->api->ReleaseEnv(ctx->env);
    free(ctx);
}
const char *aod_ort_error(const struct aod_ort *ctx) { return ctx?ctx->error:"invalid context"; }

static int check_shape(struct aod_ort *ctx, const OrtTensorTypeAndShapeInfo *info, int channels) {
    size_t rank=0; int64_t dims[4]={0}; ONNXTensorElementDataType type;
    if(!info) { set_error(ctx,"model value is not a tensor"); return 0; }
    if(!check(ctx,ctx->api->GetTensorElementType(info,&type),"GetTensorElementType") ||
       !check(ctx,ctx->api->GetDimensionsCount(info,&rank),"GetDimensionsCount")) return 0;
    if(type!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || rank!=4) { set_error(ctx,"unexpected tensor type or rank"); return 0; }
    if(!check(ctx,ctx->api->GetDimensions(info,dims,4),"GetDimensions")) return 0;
    if(dims[0]!=1 || dims[1]!=channels || dims[2]!=AOD_SIDE || dims[3]!=AOD_SIDE) {
        set_error(ctx,"unexpected tensor dimensions"); return 0;
    }
    return 1;
}

static int check_endpoint(struct aod_ort *ctx, int is_input, OrtAllocator *allocator) {
    const OrtApi *api=ctx->api; char *name=NULL; OrtTypeInfo *type=NULL;
    const OrtTensorTypeAndShapeInfo *tensor=NULL; int ok=0;
    OrtStatus *status=is_input?api->SessionGetInputName(ctx->session,0,allocator,&name):
                              api->SessionGetOutputName(ctx->session,0,allocator,&name);
    if(!check(ctx,status,"SessionGetName")) goto done;
    if(!name || strcmp(name,is_input?"input_image":"output_image")!=0) {
        set_error(ctx,"unexpected model endpoint name"); goto done;
    }
    status=is_input?api->SessionGetInputTypeInfo(ctx->session,0,&type):
                    api->SessionGetOutputTypeInfo(ctx->session,0,&type);
    if(!check(ctx,status,"SessionGetTypeInfo")) goto done;
    if(!check(ctx,api->CastTypeInfoToTensorInfo(type,&tensor),"CastTypeInfoToTensorInfo")) goto done;
    ok=check_shape(ctx,tensor,is_input?3:1);
done:
    if(type) api->ReleaseTypeInfo(type);
    if(name && !check(ctx,api->AllocatorFree(allocator,name),"AllocatorFree")) ok=0;
    return ok;
}

enum aod_result aod_ort_create(const OrtApi *api,const char *path,struct aod_ort **out,char *error,size_t cap) {
    if(out) *out=NULL;
    if(error && cap) error[0]='\0';
    if(!api || !path || !*path || !out || (cap && !error)) return AOD_INVALID_ARGUMENT;
    struct aod_ort *ctx=calloc(1,sizeof(*ctx)); if(!ctx) return AOD_NO_MEMORY;
    ctx->api=api; OrtSessionOptions *opts=NULL; OrtAllocator *allocator=NULL; size_t inputs=0,outputs=0;
    if(!check(ctx,api->CreateEnv(ORT_LOGGING_LEVEL_WARNING,"aod-runtime",&ctx->env),"CreateEnv") ||
       !check(ctx,api->CreateSessionOptions(&opts),"CreateSessionOptions") ||
       !check(ctx,api->SetIntraOpNumThreads(opts,4),"SetIntraOpNumThreads") ||
       !check(ctx,api->SetInterOpNumThreads(opts,1),"SetInterOpNumThreads") ||
       !check(ctx,api->CreateSession(ctx->env,path,opts,&ctx->session),"CreateSession") ||
       !check(ctx,api->CreateCpuMemoryInfo(OrtArenaAllocator,OrtMemTypeDefault,&ctx->memory),"CreateCpuMemoryInfo") ||
       !check(ctx,api->SessionGetInputCount(ctx->session,&inputs),"SessionGetInputCount") ||
       !check(ctx,api->SessionGetOutputCount(ctx->session,&outputs),"SessionGetOutputCount")) goto fail;
    if(inputs!=1 || outputs!=1) { set_error(ctx,"unexpected model endpoint count"); goto fail; }
    if(!check(ctx,api->GetAllocatorWithDefaultOptions(&allocator),"GetAllocatorWithDefaultOptions") ||
       !check_endpoint(ctx,1,allocator) || !check_endpoint(ctx,0,allocator)) goto fail;
    api->ReleaseSessionOptions(opts); *out=ctx; return AOD_OK;
fail:
    if(error && cap) snprintf(error,cap,"%s",ctx->error);
    if(opts) api->ReleaseSessionOptions(opts);
    aod_ort_destroy(ctx); return AOD_INFERENCE_FAILED;
}
int aod_ort_infer(void *opaque,const float *in,size_t n,float *out,size_t cap,struct aod_shape *shape) {
    struct aod_ort *ctx=opaque; if(!ctx) return -1; ctx->error[0]='\0';
    if(!in || n!=AOD_INPUT_FLOATS || !out || cap<AOD_PIXELS || !shape) {
        set_error(ctx,"invalid inference arguments"); return -1;
    }
    for(size_t i=0;i<n;i++) if(!isfinite(in[i])) { set_error(ctx,"non-finite input"); return -1; }
    const OrtApi *api=ctx->api; OrtValue *input=NULL,*output=NULL;
    OrtTensorTypeAndShapeInfo *info=NULL; float *values=NULL; int ok=0;
    const int64_t dimensions[4]={1,3,AOD_SIDE,AOD_SIDE};
    const char *inputs[]={"input_image"}, *outputs[]={"output_image"};
    if(!check(ctx,api->CreateTensorWithDataAsOrtValue(ctx->memory,(void *)in,n*sizeof(*in),dimensions,4,
                                                      ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,&input),"CreateTensorWithDataAsOrtValue") ||
       !check(ctx,api->Run(ctx->session,NULL,inputs,(const OrtValue *const *)&input,1,outputs,1,&output),"Run") ||
       !check(ctx,api->GetTensorTypeAndShape(output,&info),"GetTensorTypeAndShape") ||
       !check_shape(ctx,info,1) ||
       !check(ctx,api->GetTensorMutableData(output,(void **)&values),"GetTensorMutableData")) goto done;
    if(!values) { set_error(ctx,"missing tensor data"); goto done; }
    for(size_t i=0;i<AOD_PIXELS;i++) if(!isfinite(values[i])) { set_error(ctx,"non-finite model output"); goto done; }
    memcpy(out,values,(size_t)AOD_PIXELS*sizeof(*out));
    shape->rank=4; shape->dimensions[0]=1; shape->dimensions[1]=1;
    shape->dimensions[2]=AOD_SIDE; shape->dimensions[3]=AOD_SIDE; ok=1;
done:
    if(info) api->ReleaseTensorTypeAndShapeInfo(info);
    if(output) api->ReleaseValue(output);
    if(input) api->ReleaseValue(input);
    return ok?0:-1;
}
