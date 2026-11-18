/* Host-only integration diagnostic; never connects to an EMU Guest. */
#include "../../../native/vulkanbridge/gpu_buffer.h"
#include "../../../native/vulkanbridge/gpu_compute.h"
#include "../vulkan-compute/verify.h"
#include "shader_spv.h"
#include <stdio.h>
#include <string.h>

#define CHECK(call) do { VkResult r_=(call); if(r_!=VK_SUCCESS) { fprintf(stderr,"FAIL %s result=%d\n",#call,(int)r_); goto cleanup; } } while(0)
enum { WORD_COUNT=64 };

int main(int argc, char **argv) {
    int mismatch=0, result=1, mapped=0;
    if(argc==2 && !strcmp(argv[1],"--expect-mismatch")) mismatch=1;
    else if(argc!=1) { fprintf(stderr,"usage: %s [--expect-mismatch]\n",argv[0]); return 2; }
    VkInstance instance=VK_NULL_HANDLE;
    struct rme_vk_gpu_device gpu={0};
    struct rme_vk_gpu_buffer buffer={0};
    struct rme_vk_compute_dispatch compute={0};
    uint32_t *words=NULL;
    const VkDeviceSize bytes=WORD_COUNT*sizeof(uint32_t);
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="rme-vulkanbridge-compute",.apiVersion=VK_API_VERSION_1_2};
    VkInstanceCreateInfo ci={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
    CHECK(vkCreateInstance(&ci,NULL,&instance));
    if(rme_vk_gpu_open(instance,vkGetInstanceProcAddr,&gpu)!=RME_VK_DEVICE_OK) {
        fprintf(stderr,"DEVICE_OPEN_FAILED status=%d select=%d vk=%d\n",(int)gpu.status,(int)gpu.select_status,(int)gpu.vk_result); goto cleanup;
    }
    gpu.selected.properties.deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE-1]=0;
    printf("SELECTED_REPORTED_GPU name=%s type=%d api=%u.%u.%u queue=%u\n",gpu.selected.properties.deviceName,
        (int)gpu.selected.properties.deviceType,VK_API_VERSION_MAJOR(gpu.selected.properties.apiVersion),
        VK_API_VERSION_MINOR(gpu.selected.properties.apiVersion),VK_API_VERSION_PATCH(gpu.selected.properties.apiVersion),gpu.selected.queue_family);
    const VkPhysicalDeviceLimits *limits=&gpu.selected.properties.limits;
    if(limits->maxComputeWorkGroupSize[0]<16 || !limits->maxComputeWorkGroupSize[1] || !limits->maxComputeWorkGroupSize[2] ||
       limits->maxComputeWorkGroupInvocations<16) { fprintf(stderr,"FAIL shader local size exceeds device limits\n"); goto cleanup; }
    if(rme_vk_gpu_buffer_create(instance,vkGetInstanceProcAddr,&gpu,bytes,4194304,
       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,&buffer)!=RME_VK_BUFFER_OK) {
        fprintf(stderr,"BUFFER_CREATE_FAILED status=%d vk=%d\n",(int)buffer.status,(int)buffer.vk_result); goto cleanup;
    }
    int coherent=!!(buffer.memory_properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    printf("MEMORY host_visible=yes host_coherent=%s\n",coherent?"yes":"no");
    void *address=NULL;
    CHECK(vkMapMemory(gpu.device,buffer.memory,0,VK_WHOLE_SIZE,0,&address));
    mapped=1;
    if(!address) { fprintf(stderr,"FAIL null mapping\n"); goto cleanup; }
    words=address;
    for(uint32_t i=0;i<WORD_COUNT;i++) words[i]=i;
    VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=buffer.memory,.offset=0,.size=VK_WHOLE_SIZE};
    if(!coherent) CHECK(vkFlushMappedMemoryRanges(gpu.device,1,&range));
    if(rme_vk_compute_start(instance,vkGetInstanceProcAddr,&gpu,&buffer,shader_spv,
       sizeof(shader_spv)/sizeof(shader_spv[0]),"main",WORD_COUNT/16,1,1,&compute)!=RME_VK_COMPUTE_OK) {
        fprintf(stderr,"COMPUTE_START_FAILED status=%d vk=%d\n",(int)compute.status,(int)compute.vk_result); goto cleanup;
    }
    if(rme_vk_compute_wait(&compute,10000000000ULL)!=RME_VK_COMPUTE_OK) {
        fprintf(stderr,"WAIT_FAILED status=%d vk=%d\n",(int)compute.status,(int)compute.vk_result); goto cleanup;
    }
    if(rme_vk_compute_dispose(&compute)!=RME_VK_COMPUTE_OK) { fprintf(stderr,"DISPOSE_FAILED\n"); goto cleanup; }
    if(!coherent) CHECK(vkInvalidateMappedMemoryRanges(gpu.device,1,&range));
    size_t errors=verify_words(words,WORD_COUNT,mismatch);
    if(errors) { fprintf(stderr,"OUTPUT_MISMATCH count=%zu%s\n",errors,mismatch?" (intentional expectation mismatch)":""); goto cleanup; }
    result=0;
cleanup:
    if(rme_vk_compute_dispose(&compute)!=RME_VK_COMPUTE_OK) { fprintf(stderr,"COMPLETION_UNRESOLVED: resources retained\n"); return 1; }
    if(gpu.device) {
        if(mapped) vkUnmapMemory(gpu.device,buffer.memory);
        if(rme_vk_gpu_buffer_destroy(&buffer)!=RME_VK_BUFFER_OK || rme_vk_gpu_close(&gpu)!=RME_VK_DEVICE_OK) {
            fprintf(stderr,"CLEANUP_FAILED: resources retained\n"); return 1;
        }
    }
    if(instance) vkDestroyInstance(instance,NULL);
    if(result==0) puts("VULKAN_BRIDGE_COMPUTE_OK words=64");
    return result;
}
