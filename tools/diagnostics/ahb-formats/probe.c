#include <android/hardware_buffer.h>
#include <android/log.h>
#include <jni.h>
#include <stdint.h>
#include <stdio.h>
#define TAG "RmeAHBProbe"
static void event(const char *s){__android_log_write(ANDROID_LOG_INFO,TAG,s);}
JNIEXPORT jstring JNICALL Java_app_rme_probe_BufferActivity_probe(JNIEnv *env,jclass cls,jint mode){
 (void)cls;const char *name=mode==0?"YUV420_CPU_RW":mode==1?"YUV420_CPU_RW_GPU":mode==2?"YV12_CPU_RW":"YV12_CPU_RW_GPU";char text[256];
 AHardwareBuffer_Desc d={0};d.width=640;d.height=360;d.layers=1;d.format=(mode>=2)?(uint32_t)0x32315659:AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420;
 uint64_t cpu=AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;d.usage=cpu|((mode&1)?AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE:0);
 AHardwareBuffer *buffer=0;snprintf(text,sizeof(text),"%s allocate begin",name);event(text);int a=AHardwareBuffer_allocate(&d,&buffer);snprintf(text,sizeof(text),"%s allocate result=%d",name,a);event(text);
 if(a!=0||!buffer){snprintf(text,sizeof(text),"allocate=%d buffer_present=%d",a,buffer!=0);return (*env)->NewStringUTF(env,text);}
 AHardwareBuffer_Desc actual={0};AHardwareBuffer_describe(buffer,&actual);snprintf(text,sizeof(text),"%s described stride=%u",name,actual.stride);event(text);
 AHardwareBuffer_Planes planes={0};snprintf(text,sizeof(text),"%s lockPlanes begin",name);event(text);int l=AHardwareBuffer_lockPlanes(buffer,cpu,-1,0,&planes);
 snprintf(text,sizeof(text),"%s lockPlanes result=%d planes=%u",name,l,l==0?planes.planeCount:0);event(text);int u=0;
 if(l==0){for(uint32_t i=0;i<planes.planeCount&&i<4;i++){snprintf(text,sizeof(text),"%s plane=%u rowStride=%u pixelStride=%u",name,i,planes.planes[i].rowStride,planes.planes[i].pixelStride);event(text);}snprintf(text,sizeof(text),"%s unlock begin",name);event(text);u=AHardwareBuffer_unlock(buffer,0);snprintf(text,sizeof(text),"%s unlock result=%d",name,u);event(text);}
 AHardwareBuffer_release(buffer);if(l==0)snprintf(text,sizeof(text),"allocate=%d lockPlanes=%d unlock=%d",a,l,u);else snprintf(text,sizeof(text),"allocate=%d lockPlanes=%d unlock=not_called",a,l);return (*env)->NewStringUTF(env,text);
}
