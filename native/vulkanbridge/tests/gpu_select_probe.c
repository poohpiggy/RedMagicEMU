/* Manual diagnostic: uses the system Vulkan loader, never the test doubles. */
#include "../gpu_buffer.h"
#include "../gpu_submission.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    VkApplicationInfo app = { .sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName="rme-vulkan-gpu-select", .apiVersion=VK_API_VERSION_1_2 };
    VkInstanceCreateInfo ci = { .sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo=&app };
    VkInstance instance=VK_NULL_HANDLE;
    VkResult vr=vkCreateInstance(&ci,NULL,&instance);
    if (vr!=VK_SUCCESS || !instance) { fprintf(stderr,"FAIL vkCreateInstance result=%d\n",(int)vr); return 1; }
    uint32_t count=0;
    vr=vkEnumeratePhysicalDevices(instance,&count,NULL);
    if (vr != VK_SUCCESS || count > 256) {
        fprintf(stderr,"FAIL enumerate result=%d count=%u\n",(int)vr,count); vkDestroyInstance(instance,NULL); return 1;
    }
    VkPhysicalDevice devices[256];
    if (count) {
        uint32_t capacity=count;
        vr=vkEnumeratePhysicalDevices(instance,&count,devices);
        if (vr!=VK_SUCCESS || count>capacity) { fprintf(stderr,"FAIL enumerate values result=%d\n",(int)vr); vkDestroyInstance(instance,NULL); return 1; }
    }
    for (uint32_t i=0;i<count;++i) {
        VkPhysicalDeviceProperties p; memset(&p,0,sizeof(p)); vkGetPhysicalDeviceProperties(devices[i],&p);
        p.deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE-1]=0;
        printf("REPORTED_DEVICE name=%s type=%d api=%u.%u.%u\n",p.deviceName,(int)p.deviceType,
            VK_API_VERSION_MAJOR(p.apiVersion),VK_API_VERSION_MINOR(p.apiVersion),VK_API_VERSION_PATCH(p.apiVersion));
    }
    struct rme_vk_gpu_device gpu={0};
    enum rme_vk_device_status status=rme_vk_gpu_open(instance,vkGetInstanceProcAddr,&gpu);
    if (status != RME_VK_DEVICE_OK) {
        if (status == RME_VK_DEVICE_SELECT_FAILED)
            fprintf(stderr,"SELECT_FAILED status=%d vk_result=%d\n",(int)gpu.select_status,(int)gpu.vk_result);
        else fprintf(stderr,"DEVICE_OPEN_FAILED status=%d vk_result=%d\n",(int)status,(int)gpu.vk_result);
        vkDestroyInstance(instance,NULL); return 1;
    }
    gpu.selected.properties.deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE-1]=0;
    printf("SELECTED_REPORTED_GPU name=%s type=%d api=%u.%u.%u queue_family=%u\n",
        gpu.selected.properties.deviceName,(int)gpu.selected.properties.deviceType,
        VK_API_VERSION_MAJOR(gpu.selected.properties.apiVersion),VK_API_VERSION_MINOR(gpu.selected.properties.apiVersion),
        VK_API_VERSION_PATCH(gpu.selected.properties.apiVersion),gpu.selected.queue_family);
    struct rme_vk_gpu_buffer buffer={0};
    enum rme_vk_buffer_status buffer_status=rme_vk_gpu_buffer_create(instance,vkGetInstanceProcAddr,&gpu,
        65536,4194304,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,0,&buffer);
    if (buffer_status!=RME_VK_BUFFER_OK) {
        fprintf(stderr,"BUFFER_CREATE_FAILED status=%d vk_result=%d\n",(int)buffer_status,(int)buffer.vk_result);
        if (rme_vk_gpu_close(&gpu)!=RME_VK_DEVICE_OK) { fprintf(stderr,"DEVICE_CLOSE_FAILED: resources retained\n"); return 1; }
        vkDestroyInstance(instance,NULL); return 1;
    }
    printf("BUFFER_BOUND size=%llu allocation=%llu memory_type=%u heap=%u dedicated=%u\n",
        (unsigned long long)buffer.size,(unsigned long long)buffer.allocation_size,
        buffer.memory_type_index,buffer.memory_heap_index,(unsigned)buffer.dedicated);
    if (rme_vk_gpu_buffer_destroy(&buffer)!=RME_VK_BUFFER_OK) {
        fprintf(stderr,"BUFFER_DESTROY_FAILED (leaving device and instance for process exit)\n"); return 1;
    }
    puts("VULKAN_BUFFER_LIFECYCLE_OK");
    struct rme_vk_gpu_submission submission={0};
    enum rme_vk_submission_status submit_status=rme_vk_gpu_submit(instance,vkGetInstanceProcAddr,&gpu,VK_NULL_HANDLE,&submission);
    if (submit_status!=RME_VK_SUBMISSION_OK) {
        fprintf(stderr,"SUBMIT_FAILED status=%d vk_result=%d\n",(int)submit_status,(int)submission.vk_result);
        if (gpu.active_submissions) { fprintf(stderr,"SUBMISSION_OUTCOME_UNKNOWN: resources retained\n"); return 1; }
        if (rme_vk_gpu_close(&gpu)!=RME_VK_DEVICE_OK) { fprintf(stderr,"DEVICE_CLOSE_FAILED\n"); return 1; }
        vkDestroyInstance(instance,NULL); return 1;
    }
    submit_status=rme_vk_gpu_submission_wait(&submission,10000000000ULL);
    if (submit_status!=RME_VK_SUBMISSION_OK) {
        if (submission.phase==RME_VK_SUBMISSION_LOST_COMPLETE) {
            fprintf(stderr,"SUBMISSION_DEVICE_LOST vk_result=%d: execution failed\n",(int)submission.vk_result);
            if (rme_vk_gpu_submission_dispose(&submission)!=RME_VK_SUBMISSION_OK || rme_vk_gpu_close(&gpu)!=RME_VK_DEVICE_OK) {
                fprintf(stderr,"LOST_DEVICE_CLEANUP_FAILED: resources retained\n"); return 1;
            }
            vkDestroyInstance(instance,NULL); return 1;
        }
        fprintf(stderr,"SUBMISSION_WAIT_FAILED status=%d vk_result=%d: resources retained\n",(int)submit_status,(int)submission.vk_result); return 1;
    }
    if (rme_vk_gpu_submission_dispose(&submission)!=RME_VK_SUBMISSION_OK) {
        fprintf(stderr,"SUBMISSION_DISPOSE_FAILED: resources retained\n"); return 1;
    }
    puts("VULKAN_EMPTY_SUBMISSION_OK");
    if (rme_vk_gpu_close(&gpu)!=RME_VK_DEVICE_OK) {
        fprintf(stderr,"DEVICE_CLOSE_FAILED: resources retained\n"); return 1;
    }
    puts("VULKAN_DEVICE_LIFECYCLE_OK");
    vkDestroyInstance(instance,NULL);
    return 0;
}
