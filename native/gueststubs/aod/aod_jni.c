/* Experimental, not wired into a Guest.  Original ABI is from the target
 * com.zte.saliency.SaliencyApi; this implementation uses a software backend. */
#include <jni.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "aod_backend.h"
#include "aod_mask.h"
#include "aod_postprocess.h"
#include "../aod-runtime/aod_runtime.h"

#define AOD_N ((size_t)AOD_SIDE * AOD_SIDE)
#define AOD_MAX_PIXELS ((size_t)16777216)

static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static void *backend;
/* The observed target callers all use the same BackgroundThread; keep result
 * state per thread so unrelated callers cannot overwrite each other's result.
 * Reinitialization/release invalidates every thread through the generation. */
static uint64_t generation=1;
static _Thread_local uint64_t local_generation;
static _Thread_local struct aod_mask_state result={.top_x=-1};

static int infer_mask(void *context,const uint8_t *rgb,int width,int height,
                      uint8_t *alpha,size_t pixels) {
    float *logits=malloc(AOD_N*sizeof(*logits));
    uint8_t *soft=malloc(AOD_N), *binary=malloc(AOD_N);
    if (!logits||!soft||!binary) { free(logits);free(soft);free(binary);return -1; }
    if (aod_backend_run(context,rgb,pixels*3,width,height,logits,AOD_N)!=0) goto fail;
    for (size_t i=0;i<AOD_N;i++) {
        float z=logits[i];
        if (!isfinite(z)) goto fail;
        float e=expf(z<0?z:-z), p=z<0?e/(1.f+e):1.f/(1.f+e);
        soft[i]=(uint8_t)(p*255.f);
    }
    if (aod_prepare_mask(soft,AOD_N,AOD_SIDE,AOD_SIDE,binary,AOD_N)!=0 ||
        aod_finish_mask(binary,AOD_N,AOD_SIDE,AOD_SIDE,alpha,pixels,width,height)!=0) goto fail;
    free(logits);free(soft);free(binary);return 0;
fail:
    free(logits);free(soft);free(binary);return -1;
}

static void next_generation(void) {
    if (generation) generation=generation==UINT64_MAX?0:generation+1;
}

/* Called with lock held. Generation zero permanently disables the backend if
 * the counter is exhausted, instead of reviving stale thread-local results. */
static void current_result(void) {
    if (local_generation!=generation) {
        aod_mask_init(&result,backend?infer_mask:NULL,backend);
        local_generation=generation;
    }
}

JNIEXPORT void JNICALL Java_com_zte_saliency_SaliencyApi_saliencyMapInit(
    JNIEnv *env,jclass cls,jstring a,jstring b,jstring c) {
    (void)cls;
    /* The target returns without changing its state when any argument is null. */
    if (!a || !b || !c || (*env)->ExceptionCheck(env)) return;
    pthread_mutex_lock(&lock);
    if (backend) aod_backend_close(backend);
    backend=NULL;
    next_generation();
    void *new_backend=NULL;
    if (generation && aod_backend_open(&new_backend)==0 && new_backend) backend=new_backend;
    else if (new_backend) aod_backend_close(new_backend);
    current_result();
    pthread_mutex_unlock(&lock);
}

JNIEXPORT jint JNICALL Java_com_zte_saliency_SaliencyApi_saliencyInitStatus(JNIEnv *env,jclass cls) {
    (void)env;(void)cls;pthread_mutex_lock(&lock);jint status=backend?1:0;pthread_mutex_unlock(&lock);return status;
}

/* Clear output in bounded chunks without suppressing a pending JNI exception. */
static int zero_output(JNIEnv *env,jbyteArray out,jsize len) {
    static const jbyte zeros[4096]={0};
    for (jsize i=0;i<len;) {
        jsize n=len-i>(jsize)sizeof(zeros)?(jsize)sizeof(zeros):len-i;
        (*env)->SetByteArrayRegion(env,out,i,n,zeros);
        if ((*env)->ExceptionCheck(env)) return -1;
        i+=n;
    }
    return 0;
}

JNIEXPORT void JNICALL Java_com_zte_saliency_SaliencyApi_saliencyMapRunning(
    JNIEnv *env,jclass cls,jbyteArray rgb,jint width,jint height,jbyteArray out) {
    (void)cls;pthread_mutex_lock(&lock);current_result();
    aod_mask_init(&result,backend?infer_mask:NULL,backend);
    result.top_y=height>0?height:0;
    if ((*env)->ExceptionCheck(env) || !out) goto done;
    if (rgb && (*env)->IsSameObject(env,rgb,out)) goto done;
    if ((*env)->ExceptionCheck(env)) goto done;
    jsize out_len=(*env)->GetArrayLength(env,out);
    if ((*env)->ExceptionCheck(env) || out_len<0 || (size_t)out_len>AOD_MAX_PIXELS*4) goto done;
    if (zero_output(env,out,out_len)!=0 || !backend || !rgb || width<=0 || height<=0 ||
        (uint32_t)width>AOD_MAX_DIMENSION || (uint32_t)height>AOD_MAX_DIMENSION ||
        (size_t)width>AOD_MAX_PIXELS/(size_t)height) goto done;
    size_t pixels=(size_t)width*(size_t)height;
    if (pixels>AOD_MAX_PIXELS || (size_t)out_len!=pixels*4) goto done;
    jsize in_len=(*env)->GetArrayLength(env,rgb);
    if ((*env)->ExceptionCheck(env) || in_len<0 || (size_t)in_len!=pixels*3) goto done;
    uint8_t *in=malloc(pixels*3),*bgra=malloc(pixels*4);
    if (!in||!bgra) { free(in);free(bgra);goto done; }
    (*env)->GetByteArrayRegion(env,rgb,0,in_len,(jbyte*)in);
    if (!(*env)->ExceptionCheck(env) && aod_mask_run(&result,in,pixels*3,width,height,bgra,pixels*4)==0) {
        (*env)->SetByteArrayRegion(env,out,0,out_len,(const jbyte*)bgra);
        if ((*env)->ExceptionCheck(env)) aod_mask_init(&result,infer_mask,backend);
    }
    free(in);free(bgra);
done:
    pthread_mutex_unlock(&lock);
}

JNIEXPORT jboolean JNICALL Java_com_zte_saliency_SaliencyApi_saliencyIsAlphaNull(JNIEnv *env,jclass cls) {
    (void)env;(void)cls;pthread_mutex_lock(&lock);current_result();jboolean empty=result.has_alpha?JNI_FALSE:JNI_TRUE;pthread_mutex_unlock(&lock);return empty;
}

JNIEXPORT void JNICALL Java_com_zte_saliency_SaliencyApi_saliencyMapHighestPointGet(JNIEnv *env,jclass cls,jintArray point) {
    (void)cls;pthread_mutex_lock(&lock);current_result();
    if (point && !(*env)->ExceptionCheck(env)) {
        jsize n=(*env)->GetArrayLength(env,point);
        if (!(*env)->ExceptionCheck(env) && n>=2) {
            jint xy[2]={result.top_x,result.top_y};
            (*env)->SetIntArrayRegion(env,point,0,2,xy);
        }
    }
    pthread_mutex_unlock(&lock);
}

JNIEXPORT void JNICALL Java_com_zte_saliency_SaliencyApi_saliencyMapRelease(JNIEnv *env,jclass cls) {
    (void)env;(void)cls;pthread_mutex_lock(&lock);
    if (backend) aod_backend_close(backend);
    backend=NULL;next_generation();current_result();pthread_mutex_unlock(&lock);
}
