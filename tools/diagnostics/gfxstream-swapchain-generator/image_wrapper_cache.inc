// RME swapchain image wrapper cache begin
// Host-boxed image IDs are stable for one swapchain; each needs one Guest
// wrapper until that swapchain is destroyed. This does not own host images.
static std::mutex rmeSwapchainImagesMutex;
static std::unordered_map<VkSwapchainKHR, std::unordered_map<uint64_t, VkImage>> rmeSwapchainImages;

static bool rmeMapSwapchainImages(VkSwapchainKHR swapchain, const uint64_t* hostImages,
                                  uint32_t count, VkImage* output, VulkanHandleMapping* create) {
  for (uint32_t i = 0; i < count; ++i)
    if (!hostImages[i]) return false;
  std::lock_guard<std::mutex> lock(rmeSwapchainImagesMutex);
  auto& images = rmeSwapchainImages[swapchain];
  for (uint32_t i = 0; i < count; ++i) {
    auto it = images.find(hostImages[i]);
    if (it == images.end()) {
      VkImage image = VK_NULL_HANDLE;
      create->mapHandles_u64_VkImage(&hostImages[i], &image, 1);
      it = images.emplace(hostImages[i], image).first;
    }
    output[i] = it->second;
  }
  return true;
}

static void rmeReleaseSwapchainImages(VkSwapchainKHR swapchain, VulkanHandleMapping* destroy) {
  std::lock_guard<std::mutex> lock(rmeSwapchainImagesMutex);
  auto found = rmeSwapchainImages.find(swapchain);
  if (found == rmeSwapchainImages.end()) return;
  for (auto& entry : found->second) destroy->mapHandles_VkImage(&entry.second, 1);
  rmeSwapchainImages.erase(found);
}
// RME swapchain image wrapper cache end
