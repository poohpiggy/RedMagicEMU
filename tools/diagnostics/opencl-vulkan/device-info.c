/* Standalone OpenCL capability enumeration. No context, kernel or enqueue. */
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>
#include <CL/cl_ext.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_PLATFORMS = 128, MAX_DEVICES = 256, MAX_TEXT = 1048576 };
static int checked(cl_int code, const char *name) {
    if (code == CL_SUCCESS) return 1;
    fprintf(stderr, "FAIL %s: OpenCL error %d\n", name, (int)code);
    return 0;
}
static char *platform_text(cl_platform_id p, cl_platform_info key) {
    size_t n = 0;
    if (!checked(clGetPlatformInfo(p, key, 0, NULL, &n), "clGetPlatformInfo(size)") || !n || n > MAX_TEXT) return NULL;
    char *s = calloc(n + 1, 1);
    if (!s) return NULL;
    if (!checked(clGetPlatformInfo(p, key, n, s, NULL), "clGetPlatformInfo")) { free(s); return NULL; }
    return s;
}
static char *device_text(cl_device_id d, cl_device_info key) {
    size_t n = 0;
    if (!checked(clGetDeviceInfo(d, key, 0, NULL, &n), "clGetDeviceInfo(size)") || !n || n > MAX_TEXT) return NULL;
    char *s = calloc(n + 1, 1);
    if (!s) return NULL;
    if (!checked(clGetDeviceInfo(d, key, n, s, NULL), "clGetDeviceInfo(text)")) { free(s); return NULL; }
    return s;
}
static int has_token(const char *s, const char *token) {
    size_t n = strlen(token);
    for (const char *p = s; *p;) {
        while (*p == ' ') ++p;
        const char *end = strchr(p, ' ');
        if (!end) end = p + strlen(p);
        if ((size_t)(end - p) == n && !memcmp(p, token, n)) return 1;
        p = end;
    }
    return 0;
}
static int show_device(cl_device_id d) {
    char *name=NULL, *vendor=NULL, *driver=NULL, *version=NULL, *cversion=NULL, *extensions=NULL;
    size_t *sizes=NULL, max_group=0;
    cl_uint dimensions=0;
    cl_bool image=CL_FALSE, compiler=CL_FALSE;
    cl_ulong local=0;
    cl_device_type type=0;
    cl_device_fp_config half_config=0;
    int result=0;
    name=device_text(d,CL_DEVICE_NAME); vendor=device_text(d,CL_DEVICE_VENDOR);
    driver=device_text(d,CL_DRIVER_VERSION); version=device_text(d,CL_DEVICE_VERSION);
    cversion=device_text(d,CL_DEVICE_OPENCL_C_VERSION); extensions=device_text(d,CL_DEVICE_EXTENSIONS);
    if (!name || !vendor || !driver || !version || !cversion || !extensions) goto done;
#define INFO(key, var) if (!checked(clGetDeviceInfo(d, key, sizeof(var), &(var), NULL), #key)) goto done
    INFO(CL_DEVICE_TYPE,type);
    INFO(CL_DEVICE_IMAGE_SUPPORT,image);
    INFO(CL_DEVICE_COMPILER_AVAILABLE,compiler);
    INFO(CL_DEVICE_LOCAL_MEM_SIZE,local);
    INFO(CL_DEVICE_MAX_WORK_GROUP_SIZE,max_group);
    INFO(CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS,dimensions);
    if (!dimensions || dimensions > 64) { fprintf(stderr,"FAIL invalid work-item dimensions=%u\n",dimensions); goto done; }
    sizes=calloc(dimensions,sizeof(*sizes));
    if (!sizes) goto done;
    if (!checked(clGetDeviceInfo(d,CL_DEVICE_MAX_WORK_ITEM_SIZES,dimensions*sizeof(*sizes),sizes,NULL),"CL_DEVICE_MAX_WORK_ITEM_SIZES")) goto done;
    int fp16=has_token(extensions,"cl_khr_fp16");
    if (fp16) INFO(CL_DEVICE_HALF_FP_CONFIG,half_config);
    printf("DEVICE name=%s vendor=%s reported_type=%s type_flags=0x%llx\n",name,vendor,(type & CL_DEVICE_TYPE_GPU) ? "GPU" : (type & CL_DEVICE_TYPE_CPU) ? "CPU" : (type & CL_DEVICE_TYPE_ACCELERATOR) ? "accelerator" : "other",(unsigned long long)type);
    printf("DRIVER version=%s\nDEVICE_VERSION %s\nOPENCL_C_VERSION %s\n",driver,version,cversion);
    printf("CAPS image=%s compiler=%s fp16_extension=%s", image?"yes":"no", compiler?"yes":"no", fp16?"yes":"no");
    if (fp16) printf(" fp16_config=0x%llx", (unsigned long long)half_config);
    else printf(" fp16_config=not_queried");
    printf(" local_mem_bytes=%llu workgroup_max=%zu dimensions=%u item_sizes=", (unsigned long long)local,max_group,dimensions);
    for(cl_uint i=0;i<dimensions;i++) printf("%s%zu",i?",":"",sizes[i]);
    putchar('\n');
    static const char *const interop[] = {
        "cl_khr_external_memory",
        "cl_khr_external_memory_android_hardware_buffer",
        "cl_qcom_ext_host_ptr",
        "cl_qcom_android_native_buffer_host_ptr",
        "cl_arm_import_memory",
    };
    for(size_t i=0;i<sizeof(interop)/sizeof(interop[0]);i++)
        printf("INTEROP %s=%s\n",interop[i],has_token(extensions,interop[i])?"yes":"no");
    result=1;
done:
    free(name);free(vendor);free(driver);free(version);free(cversion);free(extensions);free(sizes);
    return result;
}
int main(int argc, char **argv) {
    if (argc != 1) { fprintf(stderr,"usage: %s\n",argv[0]); return 2; }
    cl_uint count=0, reported=0;
    cl_platform_id *platforms=NULL;
    int result=1;
    if (!checked(clGetPlatformIDs(0,NULL,&count),"clGetPlatformIDs(count)")) goto done;
    if (!count || count>MAX_PLATFORMS) { fprintf(stderr,"FAIL platform count=%u\n",count); goto done; }
    platforms=calloc(count,sizeof(*platforms)); if(!platforms) goto done;
    if (!checked(clGetPlatformIDs(count,platforms,NULL),"clGetPlatformIDs(list)")) goto done;
    for(cl_uint i=0;i<count;i++) {
        char *pname=platform_text(platforms[i],CL_PLATFORM_NAME);
        char *pversion=platform_text(platforms[i],CL_PLATFORM_VERSION);
        if(!pname || !pversion) { free(pname);free(pversion);goto done; }
        printf("PLATFORM name=%s version=%s\n",pname,pversion);
        free(pname);free(pversion);
        cl_uint n=0;
        cl_int err=clGetDeviceIDs(platforms[i],CL_DEVICE_TYPE_ALL,0,NULL,&n);
        if(err==CL_DEVICE_NOT_FOUND) { puts("PLATFORM_DEVICES none"); continue; }
        if(!checked(err,"clGetDeviceIDs(count)") || !n || n>MAX_DEVICES) goto done;
        cl_device_id *devices=calloc(n,sizeof(*devices)); if(!devices) goto done;
        err=clGetDeviceIDs(platforms[i],CL_DEVICE_TYPE_ALL,n,devices,NULL);
        if(!checked(err,"clGetDeviceIDs(list)")) { free(devices); goto done; }
        for(cl_uint j=0;j<n;j++) {
            if(!show_device(devices[j])) { free(devices); goto done; }
            reported++;
        }
        free(devices);
    }
    if(!reported) { fputs("FAIL no OpenCL devices\n",stderr); goto done; }
    printf("OPENCL_DEVICE_INFO_OK count=%u\n",reported);
    result=0;
done:
    free(platforms);
    return result;
}
