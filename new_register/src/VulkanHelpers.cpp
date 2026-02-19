#include "VulkanHelpers.h"
#include <backends/imgui_impl_vulkan.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <memory>

static VkDevice g_Device = VK_NULL_HANDLE;
static VkPhysicalDevice g_PhysicalDevice = VK_NULL_HANDLE;
static uint32_t g_QueueFamily = (uint32_t)-1;
static VkQueue g_Queue = VK_NULL_HANDLE;
static VkDescriptorPool g_DescriptorPool = VK_NULL_HANDLE;
static VkCommandPool g_CommandPool = VK_NULL_HANDLE;

/// Persistent staging resources reused across all UpdateTexture calls.
/// The buffer grows to accommodate the largest texture and is never shrunk.
/// The command buffer is allocated once and reset before each use.
struct StagingResources {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    void* mappedPtr = nullptr;
    VkDeviceSize capacity = 0;

    /// Ensure the staging buffer can hold at least `requiredSize` bytes.
    /// Re-allocates (rounding up to the next power of 2) only when the
    /// current capacity is insufficient.
    void ensureCapacity(VkDeviceSize requiredSize);

    /// Release all Vulkan resources.  Called once at application shutdown.
    void destroy();
};

static StagingResources g_Staging;

static uint32_t findMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(g_PhysicalDevice, &mem_properties);
    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) && (mem_properties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    return 0xFFFFFFFF;
}

/// Round up to the next power of two (minimum 256 KB).
static VkDeviceSize nextPow2(VkDeviceSize v)
{
    constexpr VkDeviceSize minSize = 256 * 1024;
    if (v < minSize) v = minSize;
    --v;
    v |= v >> 1;  v |= v >> 2;  v |= v >> 4;
    v |= v >> 8;  v |= v >> 16; v |= v >> 32;
    return v + 1;
}

void StagingResources::ensureCapacity(VkDeviceSize requiredSize)
{
    if (requiredSize <= capacity)
        return;

    VkDeviceSize newCap = nextPow2(requiredSize);

    // Tear down old buffer/memory if present.
    if (mappedPtr)
    {
        vkUnmapMemory(g_Device, memory);
        mappedPtr = nullptr;
    }
    if (buffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(g_Device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
    }
    if (memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(g_Device, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }

    // Create new staging buffer.
    VkBufferCreateInfo bufInfo = {};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = newCap;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult err = vkCreateBuffer(g_Device, &bufInfo, nullptr, &buffer);
    if (err != VK_SUCCESS)
        throw std::runtime_error("StagingResources::ensureCapacity: vkCreateBuffer failed");

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(g_Device, buffer, &memReqs);

    uint32_t memType = findMemoryType(memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memType == 0xFFFFFFFF)
        throw std::runtime_error("StagingResources::ensureCapacity: no suitable memory type");

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = memType;
    err = vkAllocateMemory(g_Device, &allocInfo, nullptr, &memory);
    if (err != VK_SUCCESS)
        throw std::runtime_error("StagingResources::ensureCapacity: vkAllocateMemory failed");

    vkBindBufferMemory(g_Device, buffer, memory, 0);

    // Persistently map (HOST_COHERENT — no explicit flush needed).
    err = vkMapMemory(g_Device, memory, 0, newCap, 0, &mappedPtr);
    if (err != VK_SUCCESS)
        throw std::runtime_error("StagingResources::ensureCapacity: vkMapMemory failed");

    capacity = newCap;
}

void StagingResources::destroy()
{
    if (mappedPtr)
    {
        vkUnmapMemory(g_Device, memory);
        mappedPtr = nullptr;
    }
    if (commandBuffer != VK_NULL_HANDLE)
    {
        vkFreeCommandBuffers(g_Device, g_CommandPool, 1, &commandBuffer);
        commandBuffer = VK_NULL_HANDLE;
    }
    if (buffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(g_Device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
    }
    if (memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(g_Device, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }
    capacity = 0;
}

namespace VulkanHelpers {

void Init(VkDevice device, VkPhysicalDevice physical_device, uint32_t queue_family, VkQueue queue, VkDescriptorPool pool, VkCommandPool command_pool) {
    g_Device = device;
    g_PhysicalDevice = physical_device;
    g_QueueFamily = queue_family;
    g_Queue = queue;
    g_DescriptorPool = pool;
    g_CommandPool = command_pool;

    // Allocate the persistent upload command buffer.
    VkCommandBufferAllocateInfo cbAllocInfo = {};
    cbAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAllocInfo.commandPool = g_CommandPool;
    cbAllocInfo.commandBufferCount = 1;
    VkResult err = vkAllocateCommandBuffers(g_Device, &cbAllocInfo, &g_Staging.commandBuffer);
    if (err != VK_SUCCESS)
        throw std::runtime_error("VulkanHelpers::Init: failed to allocate upload command buffer");
}

std::unique_ptr<VulkanTexture> CreateTexture(int w, int h, const void* data) {
    auto tex = std::make_unique<VulkanTexture>();
    tex->width = w;
    tex->height = h;
    tex->size = w * h * 4;

    VkResult err;

    // Helper: clean up partially-created texture on failure, then throw.
    auto fail = [&](const char* msg) -> void {
        tex->cleanup(g_Device);
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
        UpdateTexture(tex.get(), data);
    }

    return tex;
}

void UpdateTexture(VulkanTexture* tex, const void* data) {
    if (!data || !tex)
        throw std::runtime_error("UpdateTexture: null texture or data pointer");

    VkDeviceSize image_size = tex->width * tex->height * 4;

    // Grow the persistent staging buffer if needed (no-op when large enough).
    g_Staging.ensureCapacity(image_size);

    // Copy pixel data into the persistently-mapped staging buffer.
    std::memcpy(g_Staging.mappedPtr, data, static_cast<size_t>(image_size));

    // Reset and re-record the persistent command buffer.
    vkResetCommandBuffer(g_Staging.commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(g_Staging.commandBuffer, &beginInfo);

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
            : static_cast<VkAccessFlags>(0);
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        VkPipelineStageFlags srcStage = tex->uploaded
            ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
            : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        vkCmdPipelineBarrier(g_Staging.commandBuffer, srcStage,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
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
    region.imageExtent = {static_cast<uint32_t>(tex->width),
                          static_cast<uint32_t>(tex->height), 1};

    vkCmdCopyBufferToImage(g_Staging.commandBuffer, g_Staging.buffer,
        tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

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

        vkCmdPipelineBarrier(g_Staging.commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    vkEndCommandBuffer(g_Staging.commandBuffer);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &g_Staging.commandBuffer;

    VkResult err = vkQueueSubmit(g_Queue, 1, &submitInfo, VK_NULL_HANDLE);
    if (err != VK_SUCCESS)
        throw std::runtime_error("UpdateTexture: vkQueueSubmit failed (err=" +
                                 std::to_string(err) + ")");
    vkQueueWaitIdle(g_Queue);

    tex->uploaded = true;
}

void Shutdown() {
    g_Staging.destroy();
}

void DestroyTexture(VulkanTexture* tex) {
    if (!tex) return;
    tex->cleanup(g_Device);
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

VulkanTexture::~VulkanTexture()
{
    if (image != VK_NULL_HANDLE || descriptor_set != VK_NULL_HANDLE)
    {
        cleanup(g_Device);
    }
}
