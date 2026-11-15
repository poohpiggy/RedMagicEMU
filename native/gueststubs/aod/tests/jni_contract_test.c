#define _POSIX_C_SOURCE 200809L
#include <jni.h>
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

JNIEXPORT void JNICALL Java_com_zte_saliency_SaliencyApi_saliencyMapInit(JNIEnv*,jclass,jstring,jstring,jstring);
JNIEXPORT jint JNICALL Java_com_zte_saliency_SaliencyApi_saliencyInitStatus(JNIEnv*,jclass);
JNIEXPORT void JNICALL Java_com_zte_saliency_SaliencyApi_saliencyMapRunning(JNIEnv*,jclass,jbyteArray,jint,jint,jbyteArray);
JNIEXPORT jboolean JNICALL Java_com_zte_saliency_SaliencyApi_saliencyIsAlphaNull(JNIEnv*,jclass);
JNIEXPORT void JNICALL Java_com_zte_saliency_SaliencyApi_saliencyMapHighestPointGet(JNIEnv*,jclass,jintArray);
JNIEXPORT void JNICALL Java_com_zte_saliency_SaliencyApi_saliencyMapRelease(JNIEnv*,jclass);

struct array { jsize n; void *data; };
static int pending;
static int fail_get;
static jsize size_of(JNIEnv *e,jarray a) { (void)e;return ((struct array *)a)->n; }
static jboolean same(JNIEnv *e,jobject a,jobject b) { (void)e;return a==b; }
static jboolean exception(JNIEnv *e) { (void)e;return pending?JNI_TRUE:JNI_FALSE; }
static void get_bytes(JNIEnv *e,jbyteArray a,jsize start,jsize n,jbyte *out) { (void)e;if(fail_get)pending=1;else if(!pending)memcpy(out,(char*)((struct array*)a)->data+start,(size_t)n); }
static void set_bytes(JNIEnv *e,jbyteArray a,jsize start,jsize n,const jbyte *in) { (void)e;if(!pending)memcpy((char*)((struct array*)a)->data+start,in,(size_t)n); }
static void set_ints(JNIEnv *e,jintArray a,jsize start,jsize n,const jint *in) { (void)e;if(!pending)memcpy((jint*)((struct array*)a)->data+start,in,(size_t)n*sizeof(jint)); }
static const struct JNINativeInterface table={.GetArrayLength=size_of,.IsSameObject=same,.ExceptionCheck=exception,.GetByteArrayRegion=get_bytes,.SetByteArrayRegion=set_bytes,.SetIntArrayRegion=set_ints};
static JNIEnv env=&table;
extern atomic_int fake_max_active;
extern atomic_int fake_fail_open;
int fake_active_count(void);
static atomic_int phase;

static void *first_request(void *unused) {
    (void)unused; jbyte src[]={1,2,3,4,5,6,7,8,9,10,11,12},dst[16]; jint xy[2];
    struct array a={12,src},b={16,dst},p={2,xy};
    Java_com_zte_saliency_SaliencyApi_saliencyMapRunning(&env,NULL,(jbyteArray)&a,2,2,(jbyteArray)&b);
    atomic_store(&phase,1);
    struct timespec delay={0,1000000};
    for (int i=0;i<5000 && atomic_load(&phase)<2;i++) nanosleep(&delay,NULL);
    assert(atomic_load(&phase)==2);
    assert(Java_com_zte_saliency_SaliencyApi_saliencyIsAlphaNull(&env,NULL)==JNI_FALSE);
    Java_com_zte_saliency_SaliencyApi_saliencyMapHighestPointGet(&env,NULL,(jintArray)&p);
    assert(xy[0]==0 && xy[1]==0);
    return NULL;
}
static void *second_request(void *unused) {
    (void)unused; jbyte src[]={42,2,3,4,5,6,7,8,9,10,11,12},dst[16];
    struct array a={12,src},b={16,dst}; struct timespec delay={0,1000000};
    for(int i=0;i<5000 && atomic_load(&phase)<1;i++) nanosleep(&delay,NULL);
    assert(atomic_load(&phase)==1);
    Java_com_zte_saliency_SaliencyApi_saliencyMapRunning(&env,NULL,(jbyteArray)&a,2,2,(jbyteArray)&b);
    assert(Java_com_zte_saliency_SaliencyApi_saliencyIsAlphaNull(&env,NULL)==JNI_TRUE);
    atomic_store(&phase,2);return NULL;
}
static void *generation_request(void *unused) {
    (void)unused; jbyte src[]={1,2,3,4,5,6,7,8,9,10,11,12},dst[16]; jint xy[2]={99,99};
    struct array a={12,src},b={16,dst},p={2,xy};
    Java_com_zte_saliency_SaliencyApi_saliencyMapRunning(&env,NULL,(jbyteArray)&a,2,2,(jbyteArray)&b);
    assert(Java_com_zte_saliency_SaliencyApi_saliencyIsAlphaNull(&env,NULL)==JNI_FALSE);
    atomic_store(&phase,1);struct timespec delay={0,1000000};
    for(int i=0;i<5000 && atomic_load(&phase)<2;i++)nanosleep(&delay,NULL);
    assert(atomic_load(&phase)==2);
    assert(Java_com_zte_saliency_SaliencyApi_saliencyIsAlphaNull(&env,NULL)==JNI_TRUE);
    Java_com_zte_saliency_SaliencyApi_saliencyMapHighestPointGet(&env,NULL,(jintArray)&p);
    assert(xy[0]==-1 && xy[1]==0);return NULL;
}

static void *worker(void *unused) {
    (void)unused; jbyte src[]={1,2,3,4,5,6,7,8,9,10,11,12},dst[16];
    struct array a={12,src},b={16,dst};
    Java_com_zte_saliency_SaliencyApi_saliencyMapRunning(&env,NULL,(jbyteArray)&a,2,2,(jbyteArray)&b);
    assert((unsigned char)dst[0]==3 && (unsigned char)dst[3]==255);
    return NULL;
}
int main(void) {
    jbyte src[]={1,2,3,4,5,6,7,8,9,10,11,12},dst[16]; jint point[2]={99,99};
    struct array a={12,src},b={16,dst},p={2,point};
    jstring valid=(jstring)&a;
    assert(Java_com_zte_saliency_SaliencyApi_saliencyInitStatus(&env,NULL)==0);
    assert(Java_com_zte_saliency_SaliencyApi_saliencyIsAlphaNull(&env,NULL)==JNI_TRUE);
    Java_com_zte_saliency_SaliencyApi_saliencyMapInit(&env,NULL,NULL,valid,valid);
    assert(Java_com_zte_saliency_SaliencyApi_saliencyInitStatus(&env,NULL)==0);
    atomic_store(&fake_fail_open,1);
    Java_com_zte_saliency_SaliencyApi_saliencyMapInit(&env,NULL,valid,valid,valid);
    assert(Java_com_zte_saliency_SaliencyApi_saliencyInitStatus(&env,NULL)==0);
    atomic_store(&fake_fail_open,0);
    Java_com_zte_saliency_SaliencyApi_saliencyMapInit(&env,NULL,valid,valid,valid);
    assert(Java_com_zte_saliency_SaliencyApi_saliencyInitStatus(&env,NULL)==1);
    Java_com_zte_saliency_SaliencyApi_saliencyMapInit(&env,NULL,valid,NULL,valid);
    assert(Java_com_zte_saliency_SaliencyApi_saliencyInitStatus(&env,NULL)==1);
    Java_com_zte_saliency_SaliencyApi_saliencyMapInit(&env,NULL,valid,valid,NULL);
    assert(Java_com_zte_saliency_SaliencyApi_saliencyInitStatus(&env,NULL)==1);
    pending=1;
    Java_com_zte_saliency_SaliencyApi_saliencyMapInit(&env,NULL,valid,valid,valid);
    assert(pending==1 && Java_com_zte_saliency_SaliencyApi_saliencyInitStatus(&env,NULL)==1);
    pending=0;
    Java_com_zte_saliency_SaliencyApi_saliencyMapRunning(&env,NULL,(jbyteArray)&a,2,2,(jbyteArray)&b);
    assert(Java_com_zte_saliency_SaliencyApi_saliencyIsAlphaNull(&env,NULL)==JNI_FALSE);
    assert((unsigned char)dst[0]==3 && (unsigned char)dst[1]==2 && (unsigned char)dst[2]==1 && (unsigned char)dst[3]==255);
    Java_com_zte_saliency_SaliencyApi_saliencyMapHighestPointGet(&env,NULL,(jintArray)&p);
    assert(point[0]==0 && point[1]==0);
    src[0]=42; memset(dst,0x77,sizeof(dst));
    Java_com_zte_saliency_SaliencyApi_saliencyMapRunning(&env,NULL,(jbyteArray)&a,2,2,(jbyteArray)&b);
    assert(Java_com_zte_saliency_SaliencyApi_saliencyIsAlphaNull(&env,NULL)==JNI_TRUE && dst[0]==0 && dst[15]==0);
    Java_com_zte_saliency_SaliencyApi_saliencyMapHighestPointGet(&env,NULL,(jintArray)&p);
    assert(point[0]==-1 && point[1]==2);
    src[0]=1;
    pthread_t t1,t2; assert(!pthread_create(&t1,NULL,worker,NULL));assert(!pthread_create(&t2,NULL,worker,NULL));
    pthread_join(t1,NULL);pthread_join(t2,NULL);assert(atomic_load(&fake_max_active)==1);
    assert(!pthread_create(&t1,NULL,first_request,NULL));assert(!pthread_create(&t2,NULL,second_request,NULL));
    pthread_join(t1,NULL);pthread_join(t2,NULL);
    memset(dst,0x77,sizeof(dst));
    Java_com_zte_saliency_SaliencyApi_saliencyMapRunning(&env,NULL,(jbyteArray)&a,INT32_MAX,INT32_MAX,(jbyteArray)&b);
    assert(Java_com_zte_saliency_SaliencyApi_saliencyIsAlphaNull(&env,NULL)==JNI_TRUE && dst[0]==0);
    jbyte *wide_rgb=calloc(5000u*3u,1),*wide_out=calloc(5000u*4u,1);
    assert(wide_rgb && wide_out);
    struct array wide_in={5000*3,wide_rgb},wide_dst={5000*4,wide_out};
    Java_com_zte_saliency_SaliencyApi_saliencyMapRunning(&env,NULL,(jbyteArray)&wide_in,5000,1,(jbyteArray)&wide_dst);
    assert(Java_com_zte_saliency_SaliencyApi_saliencyIsAlphaNull(&env,NULL)==JNI_TRUE);
    free(wide_rgb);free(wide_out);
    pending=1;
    Java_com_zte_saliency_SaliencyApi_saliencyMapRunning(&env,NULL,(jbyteArray)&a,2,2,(jbyteArray)&b);
    assert(pending==1); pending=0;
    fail_get=1;memset(dst,0x77,sizeof(dst));
    Java_com_zte_saliency_SaliencyApi_saliencyMapRunning(&env,NULL,(jbyteArray)&a,2,2,(jbyteArray)&b);
    assert(pending==1 && dst[0]==0); pending=0;fail_get=0;
    assert(Java_com_zte_saliency_SaliencyApi_saliencyIsAlphaNull(&env,NULL)==JNI_TRUE);
    atomic_store(&phase,0);assert(!pthread_create(&t1,NULL,generation_request,NULL));
    struct timespec delay={0,1000000};
    int attempts=0;while(atomic_load(&phase)<1 && attempts++<5000)nanosleep(&delay,NULL);
    assert(atomic_load(&phase)==1);
    Java_com_zte_saliency_SaliencyApi_saliencyMapRelease(&env,NULL);
    Java_com_zte_saliency_SaliencyApi_saliencyMapInit(&env,NULL,valid,valid,valid);
    atomic_store(&phase,2);pthread_join(t1,NULL);
    assert(!pthread_create(&t1,NULL,worker,NULL));
    attempts=0;while(!fake_active_count() && attempts++<5000)nanosleep(&delay,NULL);
    assert(fake_active_count());
    Java_com_zte_saliency_SaliencyApi_saliencyMapRelease(&env,NULL);pthread_join(t1,NULL);
    assert(Java_com_zte_saliency_SaliencyApi_saliencyInitStatus(&env,NULL)==0);
    assert(Java_com_zte_saliency_SaliencyApi_saliencyIsAlphaNull(&env,NULL)==JNI_TRUE);
    puts("AOD_JNI_CONTRACT_OK");return 0;
}
