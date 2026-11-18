#include <hardware/hwvulkan.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "gfxstream_vk_entrypoints.h"

extern struct hwvulkan_module_t HAL_MODULE_INFO_SYM;
VKAPI_ATTR VkResult VKAPI_CALL gfxstream_vk_EnumerateInstanceExtensionProperties(const char *layer, uint32_t *count, VkExtensionProperties *props) { (void)layer; (void)count; (void)props; return VK_ERROR_INITIALIZATION_FAILED; }
VKAPI_ATTR VkResult VKAPI_CALL gfxstream_vk_CreateInstance(const VkInstanceCreateInfo *i, const VkAllocationCallbacks *a, VkInstance *o) { (void)i; (void)a; (void)o; return VK_ERROR_INITIALIZATION_FAILED; }
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL gfxstream_vk_GetInstanceProcAddr(VkInstance i, const char *name) { (void)i; (void)name; return NULL; }

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    struct hw_module_t *module = &HAL_MODULE_INFO_SYM.common;
    if (strcmp(module->id, "vulkan") || module->tag != HARDWARE_MODULE_TAG || !module->methods || !module->methods->open) return 1;
    if (!strcmp(argv[1], "invalid")) {
        pid_t child = fork(); if (child < 0) return 2;
        if (!child) { struct hw_device_t *bad = NULL; int r = module->methods->open(module, "not-vk0", &bad); _exit(r != 0 && !bad ? 0 : 1); }
        int status; if (waitpid(child, &status, 0) != child) return 2;
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return 0;
        fprintf(stderr, "FAIL invalid name: open did not return a normal error (signal=%d)\n", WIFSIGNALED(status) ? WTERMSIG(status) : 0); return 1;
    }
    struct hw_device_t *raw = NULL;
    if (module->methods->open(module, HWVULKAN_DEVICE_0, &raw) || !raw) return 1;
    struct hwvulkan_device_t *device = (struct hwvulkan_device_t *)raw;
    if (raw->tag != HARDWARE_DEVICE_TAG || raw->module != module || !raw->close ||
        device->EnumerateInstanceExtensionProperties != gfxstream_vk_EnumerateInstanceExtensionProperties ||
        device->CreateInstance != gfxstream_vk_CreateInstance || device->GetInstanceProcAddr != gfxstream_vk_GetInstanceProcAddr) return 1;
    if (!strcmp(argv[1], "open")) return 0;
    if (!strcmp(argv[1], "close")) {
        int status = raw->close(raw);
        if (status) { fprintf(stderr, "FAIL close: returned %d; upstream close does not free the device\n", status); return 1; }
        return 0;
    }
    return 2;
}
