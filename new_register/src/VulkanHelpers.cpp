#include "VulkanHelpers.h"
#include <backends/imgui_impl_vulkan.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

static VkDevice g_Device = VK_NULL_HANDLE;
static VkPhysicalDevice g_PhysicalDevice = VK_NULL_HANDLE;
static uint32_t g_QueueFamily = (uint32_t)-1;
static VkQueue g_Queue = VK_NULL_HANDLE;
static VkDescriptorPool g_DescriptorPool = VK_NULL_HANDLE;
static VkCommandPool g_CommandPool = VK_NULL_HANDLE;

static uint32_t findMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(g_PhysicalDevice, &mem_properties);
    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) && (mem_properties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    return 0xFFFFFFFF;
}

namespace VulkanHelpers {

void Init(VkDevice device, VkPhysicalDevice physical_device, uint32_t queue_family, VkQueue queue, VkDescriptorPool pool, VkCommandPool command_pool) {
    g_Device = device;
    g_PhysicalDevice = physical_device;
    g_QueueFamily = queue_family;
    g_Queue = queue;
    g_DescriptorPool = pool;
    g_CommandPool = command_pool;
}

VulkanTexture* CreateTexture(int w, int h, const void* data) {
    VulkanTexture* tex = new VulkanTexture();
    tex->width = w;
    tex->height = h;
    tex->size = w * h * 4;

    VkResult err;

    // Helper: clean up partially-created texture on failure, then throw.
    auto fail = [&](const char* msg) -> void {
        tex->cleanup(g_Device);
        delete tex;
        throw std::runtime_error(std::string("CreateTexture: ") + msg);
    };

    // Create Image
    {
        VkImageCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = VK_FORMAT_R8G8B8A8_UNORM;
        info.extent.width = w;
        info.extent.height = h;
        info.extent.depth = 1;
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        err = vkCreateImage(g_Device, &info, nullptr, &tex->image);
        if (err != VK_SUCCESS) fail("vkCreateImage failed");

        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(g_Device, tex->image, &req);
        VkMemoryAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = req.size;
        alloc_info.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (alloc_info.memoryTypeIndex == 0xFFFFFFFF) fail("no suitable memory type for image");
        err = vkAllocateMemory(g_Device, &alloc_info, nullptr, &tex->image_memory);
        if (err != VK_SUCCESS) fail("vkAllocateMemory failed for image");
        
        vkBindImageMemory(g_Device, tex->image, tex->image_memory, 0);
    }

    // Create Sampler
    {
        VkSamplerCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = VK_FILTER_LINEAR;
        info.minFilter = VK_FILTER_LINEAR;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        info.maxAnisotropy = 1.0f;
        info.minLod = -1000;
        info.maxLod = 1000;
        err = vkCreateSampler(g_Device, &info, nullptr, &tex->sampler);
        if (err != VK_SUCCESS) fail("vkCreateSampler failed");
    }

    // Create Image View
    {
        VkImageViewCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = tex->image;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = VK_FORMAT_R8G8B8A8_UNORM;
        info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.layerCount = 1;
        err = vkCreateImageView(g_Device, &info, nullptr, &tex->image_view);
        if (err != VK_SUCCESS) fail("vkCreateImageView failed");
    }

    // Descriptor Set: Create one for ImGui to use
    tex->descriptor_set = ImGui_ImplVulkan_AddTexture(tex->sampler, tex->image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Upload Data (using staging buffer)
    if (data) {
        UpdateTexture(tex, data);
    }

    return tex;
}

void UpdateTexture(VulkanTexture* tex, const void* data) {
    if (!data || !tex)
        throw std::runtime_error("UpdateTexture: null texture or data pointer");
    
    VkDeviceSize image_size = tex->width * tex->height * 4;
    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_buffer_memory = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkResult err;

    // Helper: clean up staging resources on failure, then throw.
    auto fail_staging = [&](const std::string& msg) -> void {
        if (commandBuffer)
            vkFreeCommandBuffers(g_Device, g_CommandPool, 1, &commandBuffer);
        if (staging_buffer)
            vkDestroyBuffer(g_Device, staging_buffer, nullptr);
        if (staging_buffer_memory)
            vkFreeMemory(g_Device, staging_buffer_memory, nullptr);
        throw std::runtime_error(msg);
    };

    // Create Staging Buffer
    {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = image_size;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        err = vkCreateBuffer(g_Device, &bufferInfo, nullptr, &staging_buffer);
        if (err != VK_SUCCESS)
        {
            fail_staging("UpdateTexture: vkCreateBuffer failed (err=" +
                         std::to_string(err) + ")");
        }

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(g_Device, staging_buffer, &memReqs);

        uint32_t memType = findMemoryType(memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (memType == 0xFFFFFFFF)
        {
            fail_staging("UpdateTexture: no suitable memory type for staging buffer");
        }

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = memType;
        err = vkAllocateMemory(g_Device, &allocInfo, nullptr, &staging_buffer_memory);
        if (err != VK_SUCCESS)
        {
            fail_staging("UpdateTexture: vkAllocateMemory failed (err=" +
                         std::to_string(err) + ")");
        }
        vkBindBufferMemory(g_Device, staging_buffer, staging_buffer_memory, 0);

        void* mapped;
        err = vkMapMemory(g_Device, staging_buffer_memory, 0, image_size, 0, &mapped);
        if (err != VK_SUCCESS)
        {
            fail_staging("UpdateTexture: vkMapMemory failed (err=" +
                         std::to_string(err) + ")");
        }
        memcpy(mapped, data, (size_t)image_size);
        vkUnmapMemory(g_Device, staging_buffer_memory);
    }

    // Copy buffer to image
    {
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = g_CommandPool;
        allocInfo.commandBufferCount = 1;

        err = vkAllocateCommandBuffers(g_Device, &allocInfo, &commandBuffer);
        if (err != VK_SUCCESS)
        {
            fail_staging("UpdateTexture: vkAllocateCommandBuffers failed (err=" +
                         std::to_string(err) + ")");
        }

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        // Transition to TRANSFER_DST.
        // On first upload the image is UNDEFINED; on subsequent uploads
        // it is SHADER_READ_ONLY_OPTIMAL.
        {
            VkImageMemoryBarrier barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = tex->uploaded
                ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                : VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = tex->image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = tex->uploaded
                ? VK_ACCESS_SHADER_READ_BIT
                : (VkAccessFlags)0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            VkPipelineStageFlags srcStage = tex->uploaded
                ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            vkCmdPipelineBarrier(commandBuffer, srcStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        VkBufferImageCopy region = {};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {(uint32_t)tex->width, (uint32_t)tex->height, 1};

        vkCmdCopyBufferToImage(commandBuffer, staging_buffer, tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // Transition to SHADER_READ_ONLY
        {
            VkImageMemoryBarrier barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = tex->image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        err = vkQueueSubmit(g_Queue, 1, &submitInfo, VK_NULL_HANDLE);
        if (err != VK_SUCCESS)
        {
            fail_staging("UpdateTexture: vkQueueSubmit failed (err=" +
                         std::to_string(err) + ")");
        }
        vkQueueWaitIdle(g_Queue);
    }

    tex->uploaded = true;

    // Clean up staging resources (success path).
    if (commandBuffer)
        vkFreeCommandBuffers(g_Device, g_CommandPool, 1, &commandBuffer);
    if (staging_buffer)
        vkDestroyBuffer(g_Device, staging_buffer, nullptr);
    if (staging_buffer_memory)
        vkFreeMemory(g_Device, staging_buffer_memory, nullptr);
}

void DestroyTexture(VulkanTexture* tex) {
    if (!tex) return;
    tex->cleanup(g_Device);
    delete tex;
}

} // namespace

void VulkanTexture::cleanup(VkDevice device) {
    if (descriptor_set)
    {
        ImGui_ImplVulkan_RemoveTexture(descriptor_set);
        descriptor_set = VK_NULL_HANDLE;
    }
    if (image_view) { vkDestroyImageView(device, image_view, nullptr); image_view = VK_NULL_HANDLE; }
    if (sampler)    { vkDestroySampler(device, sampler, nullptr); sampler = VK_NULL_HANDLE; }
    if (image)      { vkDestroyImage(device, image, nullptr); image = VK_NULL_HANDLE; }
    if (image_memory) { vkFreeMemory(device, image_memory, nullptr); image_memory = VK_NULL_HANDLE; }
}
