/* Android AHB BLOB <-> Vulkan transfer, single process, no external handles.
 * Requires a Vulkan 1.1 device with Android AHB and foreign queue extensions.
 * All reported capabilities and results are from the local driver. */
#define VK_USE_PLATFORM_ANDROID_KHR 1
#include <vulkan/vulkan.h>
#include <android/hardware_buffer.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "verify.h"

enum { WORDS=64, BYTES=WORDS*4, MAX_DEVICES=256, MAX_EXTENSIONS=4096, MAX_QUEUES=1024 };
#define VK_CHECK(call) do { VkResult vr_=(call); if(vr_!=VK_SUCCESS) {     fprintf(stderr,"FAIL %s VkResult=%d\n",#call,(int)vr_); goto cleanup; } } while(0)

static int has_ext(const VkExtensionProperties *ext,uint32_t n,const char *wanted) {
    for(uint32_t i=0;i<n;i++) if(!strcmp(ext[i].extensionName,wanted)) return 1;
    return 0;
}
static int rank_type(VkPhysicalDeviceType t) {
    if(t==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU||t==VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) return 3;
    if(t==VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU) return 2;
    return 1;
}
int main(int argc,char **argv) {
    int result=1, wrong=0, cpu_locked=0, unsafe=0, mapped=0;
    if(argc==2 && !strcmp(argv[1],"--expect-mismatch")) wrong=1;
    else if(argc!=1) { fprintf(stderr,"usage: %s [--expect-mismatch]\n",argv[0]); return 2; }
    VkInstance instance=VK_NULL_HANDLE; VkPhysicalDevice *all=NULL,physical=VK_NULL_HANDLE;
    VkDevice device=VK_NULL_HANDLE; VkQueue queue=VK_NULL_HANDLE;
    VkBuffer ahb_buffer=VK_NULL_HANDLE,stage_buffer=VK_NULL_HANDLE;
    VkDeviceMemory ahb_memory=VK_NULL_HANDLE,stage_memory=VK_NULL_HANDLE;
    VkCommandPool pool=VK_NULL_HANDLE; VkFence fence=VK_NULL_HANDLE;
    AHardwareBuffer *ahb=NULL; void *cpu_ptr=NULL,*stage_ptr=NULL;
    uint32_t family=0, count=0; int best=0, stage_coherent=0;
    VkPhysicalDeviceProperties selected={0};
    uint32_t loader=VK_API_VERSION_1_0;
    PFN_vkEnumerateInstanceVersion vers=(PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(VK_NULL_HANDLE,"vkEnumerateInstanceVersion");
    if(vers) VK_CHECK(vers(&loader));
    if(loader<VK_API_VERSION_1_1) { fputs("UNSUPPORTED Vulkan 1.1 loader required\n",stderr); goto cleanup; }
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="rme-ahb-vulkan-probe",.apiVersion=VK_API_VERSION_1_1};
    VkInstanceCreateInfo ii={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
    VK_CHECK(vkCreateInstance(&ii,NULL,&instance));
    VK_CHECK(vkEnumeratePhysicalDevices(instance,&count,NULL));
    if(!count || count>MAX_DEVICES) { fprintf(stderr,"FAIL device_count=%u\n",count); goto cleanup; }
    all=calloc(count,sizeof(*all)); if(!all) { fputs("FAIL alloc devices\n",stderr); goto cleanup; }
    VK_CHECK(vkEnumeratePhysicalDevices(instance,&count,all));
    for(uint32_t i=0;i<count;i++) {
        VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(all[i],&p);
        printf("CANDIDATE name=%s type=%u api=%u.%u.%u\n",p.deviceName,p.deviceType,
               VK_API_VERSION_MAJOR(p.apiVersion),VK_API_VERSION_MINOR(p.apiVersion),VK_API_VERSION_PATCH(p.apiVersion));
        if(p.apiVersion<VK_API_VERSION_1_1 || rank_type(p.deviceType)<=best) continue;
        uint32_t ne=0; VkResult r=vkEnumerateDeviceExtensionProperties(all[i],NULL,&ne,NULL);
        if(r!=VK_SUCCESS || ne>MAX_EXTENSIONS) { fprintf(stderr,"FAIL extension_count=%d/%u\n",(int)r,ne); goto cleanup; }
        VkExtensionProperties *e=calloc(ne?ne:1,sizeof(*e)); if(!e) goto cleanup;
        r=vkEnumerateDeviceExtensionProperties(all[i],NULL,&ne,e);
        if(r!=VK_SUCCESS) { free(e); fprintf(stderr,"FAIL extensions=%d\n",(int)r); goto cleanup; }
        int ok=has_ext(e,ne,VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME) &&
               has_ext(e,ne,VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME);
        free(e);
        if(!ok) continue;
        uint32_t nq=0; vkGetPhysicalDeviceQueueFamilyProperties(all[i],&nq,NULL);
        if(!nq || nq>MAX_QUEUES) continue;
        VkQueueFamilyProperties *q=calloc(nq,sizeof(*q)); if(!q) goto cleanup;
        vkGetPhysicalDeviceQueueFamilyProperties(all[i],&nq,q);
        for(uint32_t j=0;j<nq;j++) {
            if(q[j].queueCount && (q[j].queueFlags&(VK_QUEUE_TRANSFER_BIT|VK_QUEUE_COMPUTE_BIT|VK_QUEUE_GRAPHICS_BIT))) {
                physical=all[i]; family=j; best=rank_type(p.deviceType); selected=p; break;
            }
        }
        free(q);
    }
    if(!physical) { fputs("UNSUPPORTED no Vulkan 1.1 device with AHB, FOREIGN and transfer queue\n",stderr); goto cleanup; }
    printf("DEVICE name=%s type=%u api=%u.%u.%u queue=%u\n",selected.deviceName,selected.deviceType,
           VK_API_VERSION_MAJOR(selected.apiVersion),VK_API_VERSION_MINOR(selected.apiVersion),VK_API_VERSION_PATCH(selected.apiVersion),family);
    PFN_vkGetPhysicalDeviceExternalBufferProperties ext_buffer=(PFN_vkGetPhysicalDeviceExternalBufferProperties)
        vkGetInstanceProcAddr(instance,"vkGetPhysicalDeviceExternalBufferProperties");
    if(!ext_buffer) { fputs("FAIL no external buffer query function\n",stderr); goto cleanup; }
    VkPhysicalDeviceExternalBufferInfo eb={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO,
        .usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID};
    VkExternalBufferProperties eprops={.sType=VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};
    ext_buffer(physical,&eb,&eprops);
    VkExternalMemoryProperties ext=eprops.externalMemoryProperties;
    printf("AHB_BUFFER features=0x%x compatible=0x%x\n",ext.externalMemoryFeatures,ext.compatibleHandleTypes);
    if(!(ext.externalMemoryFeatures&VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) ||
       !(ext.compatibleHandleTypes&VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID)) {
        fputs("UNSUPPORTED AHB BLOB import for transfer buffer\n",stderr); goto cleanup;
    }
    if(ext.externalMemoryFeatures&VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT) {
        fputs("FAIL AHB buffer incorrectly reports DEDICATED_ONLY\n",stderr); goto cleanup;
    }
    VkPhysicalDeviceSamplerYcbcrConversionFeatures ycbcr={
        .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES};
    VkPhysicalDeviceFeatures2 features={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&ycbcr};
    vkGetPhysicalDeviceFeatures2(physical,&features);
    if(!ycbcr.samplerYcbcrConversion) {
        fputs("UNSUPPORTED probe requires samplerYcbcrConversion feature\n",stderr); goto cleanup;
    }
    ycbcr.samplerYcbcrConversion=VK_TRUE;
    const float priority=1.0f;
    VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    const char *devext[]={VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME,VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME};
    VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&ycbcr,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi,
        .enabledExtensionCount=2,.ppEnabledExtensionNames=devext};
    VK_CHECK(vkCreateDevice(physical,&di,NULL,&device));
    vkGetDeviceQueue(device,family,0,&queue);
    if(!queue) { fputs("FAIL null queue\n",stderr); goto cleanup; }
    PFN_vkGetAndroidHardwareBufferPropertiesANDROID ahb_props=(PFN_vkGetAndroidHardwareBufferPropertiesANDROID)
        vkGetDeviceProcAddr(device,"vkGetAndroidHardwareBufferPropertiesANDROID");
    if(!ahb_props) { fputs("FAIL no vkGetAndroidHardwareBufferPropertiesANDROID\n",stderr); goto cleanup; }
    AHardwareBuffer_Desc desc={.width=BYTES,.height=1,.layers=1,.format=AHARDWAREBUFFER_FORMAT_BLOB,
        .usage=AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN|AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN};
    if(!AHardwareBuffer_isSupported(&desc)) { fputs("UNSUPPORTED AHB BLOB CPU+GPU usage\n",stderr); goto cleanup; }
    int ae=AHardwareBuffer_allocate(&desc,&ahb);
    if(ae || !ahb) { fprintf(stderr,"FAIL AHardwareBuffer_allocate=%d\n",ae); goto cleanup; }
    AHardwareBuffer_Desc got={0}; AHardwareBuffer_describe(ahb,&got);
    if(got.width<BYTES || got.height!=1 || got.layers!=1 || got.format!=AHARDWAREBUFFER_FORMAT_BLOB ||
       (got.usage&desc.usage)!=desc.usage) {
        fputs("FAIL allocated AHB description mismatch\n",stderr); goto cleanup;
    }
    ae=AHardwareBuffer_lock(ahb,AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,-1,NULL,&cpu_ptr);
    if(ae) { fprintf(stderr,"FAIL AHardwareBuffer_lock(write)=%d\n",ae); goto cleanup; }
    cpu_locked=1;
    if(!cpu_ptr) { fputs("FAIL AHB null CPU write pointer\n",stderr); goto cleanup; }
    uint32_t expected[WORDS];
    for(uint32_t i=0;i<WORDS;i++) expected[i]=input_word(i);
    memcpy(cpu_ptr,expected,BYTES); cpu_ptr=NULL;
    ae=AHardwareBuffer_unlock(ahb,NULL); cpu_locked=0;
    if(ae) { unsafe=1; fprintf(stderr,"FAIL AHardwareBuffer_unlock(write)=%d\n",ae); goto cleanup; }

    VkExternalMemoryBufferCreateInfo ext_create={.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
        .handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID};
    VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.pNext=&ext_create,.size=BYTES,
        .usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    VK_CHECK(vkCreateBuffer(device,&bi,NULL,&ahb_buffer));
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(device,ahb_buffer,&req);
    VkAndroidHardwareBufferPropertiesANDROID ap={.sType=VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID};
    VK_CHECK(ahb_props(device,ahb,&ap));
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(physical,&mp);
    uint32_t index=UINT32_MAX;
    for(uint32_t i=0;i<mp.memoryTypeCount;i++)
        if((req.memoryTypeBits & ap.memoryTypeBits & (1u<<i)) && !(mp.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_PROTECTED_BIT)) { index=i; break; }
    if(index==UINT32_MAX || ap.allocationSize<req.size) { fputs("UNSUPPORTED no compatible AHB memory type or size\n",stderr); goto cleanup; }
    VkImportAndroidHardwareBufferInfoANDROID imp={.sType=VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,.buffer=ahb};
    VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.pNext=&imp,.allocationSize=ap.allocationSize,.memoryTypeIndex=index};
    VK_CHECK(vkAllocateMemory(device,&ai,NULL,&ahb_memory));
    VK_CHECK(vkBindBufferMemory(device,ahb_buffer,ahb_memory,0));

    VkBufferCreateInfo sb={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=BYTES,
        .usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    VK_CHECK(vkCreateBuffer(device,&sb,NULL,&stage_buffer));
    vkGetBufferMemoryRequirements(device,stage_buffer,&req);
    index=UINT32_MAX;
    for(uint32_t i=0;i<mp.memoryTypeCount;i++) {
        VkMemoryPropertyFlags flags=mp.memoryTypes[i].propertyFlags;
        if(!(req.memoryTypeBits&(1u<<i)) || !(flags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) || (flags&VK_MEMORY_PROPERTY_PROTECTED_BIT)) continue;
        if(index==UINT32_MAX || (flags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) index=i;
        if(flags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) break;
    }
    if(index==UINT32_MAX) { fputs("UNSUPPORTED no host-visible staging memory\n",stderr); goto cleanup; }
    stage_coherent=!!(mp.memoryTypes[index].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo si={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=index};
    VK_CHECK(vkAllocateMemory(device,&si,NULL,&stage_memory));
    VK_CHECK(vkBindBufferMemory(device,stage_buffer,stage_memory,0));
    VK_CHECK(vkMapMemory(device,stage_memory,0,VK_WHOLE_SIZE,0,&stage_ptr)); mapped=1;
    if(!stage_ptr) { fputs("FAIL null staging pointer\n",stderr); goto cleanup; }
    VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};
    VK_CHECK(vkCreateCommandPool(device,&pi,NULL,&pool));
    VkCommandBufferAllocateInfo cai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer command=VK_NULL_HANDLE; VK_CHECK(vkAllocateCommandBuffers(device,&cai,&command));
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    VK_CHECK(vkBeginCommandBuffer(command,&begin));
    VkBufferMemoryBarrier acquire={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask=0,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_FOREIGN_EXT,.dstQueueFamilyIndex=family,
        .buffer=ahb_buffer,.offset=0,.size=BYTES};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,1,&acquire,0,NULL);
    VkBufferCopy copy={.srcOffset=0,.dstOffset=0,.size=BYTES};
    vkCmdCopyBuffer(command,ahb_buffer,stage_buffer,1,&copy);
    VkBufferMemoryBarrier read_before_write={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .buffer=ahb_buffer,.offset=0,.size=BYTES};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,1,&read_before_write,0,NULL);
    vkCmdFillBuffer(command,ahb_buffer,0,BYTES,FILL_WORD);
    VkBufferMemoryBarrier barriers[2]={
      {.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=0,
       .srcQueueFamilyIndex=family,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_FOREIGN_EXT,.buffer=ahb_buffer,.offset=0,.size=BYTES},
      {.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
       .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=stage_buffer,.offset=0,.size=BYTES}
    };
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,NULL,2,barriers,0,NULL);
    VK_CHECK(vkEndCommandBuffer(command));
    VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VK_CHECK(vkCreateFence(device,&fi,NULL,&fence));
    VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
    unsafe=1;
    VK_CHECK(vkQueueSubmit(queue,1,&submit,fence));
    VK_CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_C(10000000000)));
    unsafe=0;
    if(!stage_coherent) {
        VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=stage_memory,.offset=0,.size=VK_WHOLE_SIZE};
        VK_CHECK(vkInvalidateMappedMemoryRanges(device,1,&range));
    }
    uint32_t observed[WORDS]; memcpy(observed,stage_ptr,BYTES);
    size_t errors=mismatches(observed,WORDS,0,0);
    if(errors) { fprintf(stderr,"FAIL CPU_TO_VK mismatches=%zu\n",errors); goto cleanup; }
    puts("AHB_CPU_TO_VK_OK words=64");
    ae=AHardwareBuffer_lock(ahb,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,-1,NULL,&cpu_ptr);
    if(ae) { fprintf(stderr,"FAIL AHardwareBuffer_lock(read)=%d\n",ae); goto cleanup; }
    cpu_locked=1;
    if(!cpu_ptr) { fputs("FAIL AHB null CPU read pointer\n",stderr); goto cleanup; }
    memcpy(observed,cpu_ptr,BYTES); cpu_ptr=NULL;
    ae=AHardwareBuffer_unlock(ahb,NULL); cpu_locked=0;
    if(ae) { unsafe=1; fprintf(stderr,"FAIL AHardwareBuffer_unlock(read)=%d\n",ae); goto cleanup; }
    errors=mismatches(observed,WORDS,1,wrong);
    if(errors) { fprintf(stderr,"FAIL VK_TO_CPU mismatches=%zu%s\n",errors,wrong?" intentional expectation mismatch":""); goto cleanup; }
    if(wrong) { fputs("FAIL intentional expectation unexpectedly matched\n",stderr); goto cleanup; }
    puts("AHB_VK_TO_CPU_OK words=64");
    result=0;
cleanup:
    free(all);
    if(cpu_locked) {
        int u=AHardwareBuffer_unlock(ahb,NULL);
        if(u) { fprintf(stderr,"FAIL cleanup AHardwareBuffer_unlock=%d\n",u); unsafe=1; result=1; }
    }
    if(unsafe) { fputs("RESOURCE_STATE_UNKNOWN: leave Vulkan/AHB resources for process teardown\n",stderr); return 1; }
    if(device) {
        if(mapped) vkUnmapMemory(device,stage_memory);
        if(fence) vkDestroyFence(device,fence,NULL);
        if(pool) vkDestroyCommandPool(device,pool,NULL);
        if(stage_buffer) vkDestroyBuffer(device,stage_buffer,NULL);
        if(stage_memory) vkFreeMemory(device,stage_memory,NULL);
        if(ahb_buffer) vkDestroyBuffer(device,ahb_buffer,NULL);
        if(ahb_memory) vkFreeMemory(device,ahb_memory,NULL);
        vkDestroyDevice(device,NULL);
    }
    if(ahb) AHardwareBuffer_release(ahb);
    if(instance) vkDestroyInstance(instance,NULL);
    if(!result) puts("AHB_VULKAN_ROUNDTRIP_OK");
    return result;
}