#pragma once
#include <vulkan/vulkan.h>
#include <imgui.h>
#include <memory>

class VulkanTexture {
public:
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory image_memory = VK_NULL_HANDLE;
    VkImageView image_view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    
    int width = 0;
    int height = 0;
    VkDeviceSize size = 0;

    /// True after the first successful upload (layout is
    /// VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL).
    bool uploaded = false;

    ~VulkanTexture();

    void cleanup(VkDevice device);
};

namespace VulkanHelpers {
    void Init(VkDevice device, VkPhysicalDevice physical_device, uint32_t queue_family, VkQueue queue, VkDescriptorPool pool, VkCommandPool command_pool);
    std::unique_ptr<VulkanTexture> CreateTexture(int w, int h, const void* data);
    void UpdateTexture(VulkanTexture* texture, const void* data);
    void DestroyTexture(VulkanTexture* texture);
}
