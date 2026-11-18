#!/usr/bin/env python3
"""Recreate the isolated d7a host-instance extension candidate patch."""
import argparse
import difflib
from pathlib import Path


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError("unexpected source layout: " + old[:80])
    return text.replace(old, new, 1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    relative = "host/vulkan/VkDecoderGlobalState.cpp"
    original = (args.source / relative).read_text()
    text = original
    anchor = '''        std::vector<const char*> finalExts = filteredInstanceExtensionNames(
            pCreateInfo->enabledExtensionCount, pCreateInfo->ppEnabledExtensionNames);
'''
    extra = '''#if defined(__ANDROID__)
        VkResult surfaceResult = prepareAndroidSurfaceExtensions(finalExts);
        if (surfaceResult != VK_SUCCESS) return surfaceResult;
#endif
'''
    text = replace_once(text, anchor, anchor + extra)
    start = text.index("    std::vector<const char*> filteredInstanceExtensionNames(")
    end = text.index("    bool getDefaultQueueForDeviceLocked(", start)
    method = text[start:end]
    method = replace_once(method, '''        std::vector<const char*> res;
''', '''        std::vector<const char*> res;
        auto appendUnique = [&res](const char* name) {
            auto matches = [name](const char* current) { return !strcmp(name, current); };
            if (std::find_if(res.begin(), res.end(), matches) == res.end()) res.push_back(name);
        };
''')
    method = method.replace("            res.push_back(", "            appendUnique(")
    helper = '''#if defined(__ANDROID__)
    VkResult prepareAndroidSurfaceExtensions(std::vector<const char*>& extensions) {
        constexpr const char* androidSurface = "VK_KHR_android_surface";
        auto contains = [](const std::vector<const char*>& names, const char* target) {
            return std::find_if(names.begin(), names.end(), [target](const char* name) {
                return !strcmp(name, target);
            }) != names.end();
        };
        if (!contains(extensions, androidSurface)) return VK_SUCCESS;
        if (!m_vk->vkEnumerateInstanceExtensionProperties) return VK_ERROR_INITIALIZATION_FAILED;
        uint32_t count = 0;
        VkResult result = m_vk->vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
        if (result != VK_SUCCESS) return result == VK_INCOMPLETE ? VK_ERROR_INITIALIZATION_FAILED : result;
        if (!count) return VK_ERROR_EXTENSION_NOT_PRESENT;
        std::vector<VkExtensionProperties> available(count);
        result = m_vk->vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());
        if (result != VK_SUCCESS) return result == VK_INCOMPLETE ? VK_ERROR_INITIALIZATION_FAILED : result;
        available.resize(count);
        auto supported = [&available](const char* name) {
            return std::find_if(available.begin(), available.end(), [name](const auto& property) {
                return !strcmp(name, property.extensionName);
            }) != available.end();
        };
        if (!supported(androidSurface) || !supported(VK_KHR_SURFACE_EXTENSION_NAME)) {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
        if (!contains(extensions, VK_KHR_SURFACE_EXTENSION_NAME)) {
            extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        }
        return VK_SUCCESS;
    }
#endif

'''
    text = text[:start] + method + helper + text[end:]
    patch = difflib.unified_diff(original.splitlines(keepends=True), text.splitlines(keepends=True),
                                 fromfile="a/" + relative, tofile="b/" + relative)
    args.output.write_text("".join(patch))


if __name__ == "__main__":
    main()
