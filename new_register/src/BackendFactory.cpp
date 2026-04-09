#include "GraphicsBackend.h"
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <cstdlib>

#ifdef HAS_VULKAN
#include "VulkanBackend.h"
#endif
#ifdef HAS_OPENGL2
#include "OpenGL2Backend.h"
#endif

std::unique_ptr<GraphicsBackend> GraphicsBackend::create(BackendType type)
{
    switch (type)
    {
#ifdef HAS_VULKAN
    case BackendType::Vulkan:
        return std::make_unique<VulkanBackend>();
#endif
#ifdef HAS_OPENGL2
    case BackendType::OpenGL2:
        return std::make_unique<OpenGL2Backend>();
#endif
    default:
        throw std::runtime_error(
            std::string("Backend not available: ") + backendName(type));
    }
}

static bool isRemoteX11Display()
{
    // x2go sets X2GO_SESSION; general SSH X11 forwarding sets SSH_CONNECTION/SSH_CLIENT
    if (std::getenv("X2GO_SESSION"))
        return true;
    if (std::getenv("SSH_CONNECTION") || std::getenv("SSH_CLIENT"))
    {
        const char* disp = std::getenv("DISPLAY");
        return disp && disp[0] != '\0';
    }
    return false;
}

BackendType GraphicsBackend::detectBest()
{
    auto avail = availableBackends();
    if (avail.empty())
        throw std::runtime_error("No graphics backends compiled in");

    if (isRemoteX11Display())
    {
        // Vulkan crashes with XIO fatal errors over x2go / SSH X11 forwarding;
        // skip it and use the next available backend.
        for (auto b : avail)
            if (b != BackendType::Vulkan)
                return b;
    }
    return avail.front();
}

std::vector<BackendType> GraphicsBackend::availableBackends()
{
    std::vector<BackendType> result;
#ifdef HAS_VULKAN
    result.push_back(BackendType::Vulkan);
#endif
#ifdef HAS_OPENGL2
    result.push_back(BackendType::OpenGL2);
#endif
#ifdef HAS_METAL
    result.push_back(BackendType::Metal);
#endif
    return result;
}

const char* GraphicsBackend::backendName(BackendType type)
{
    switch (type)
    {
    case BackendType::Vulkan:  return "vulkan";
    case BackendType::OpenGL2: return "opengl2";
    case BackendType::Metal:   return "metal";
    }
    return "unknown";
}

std::optional<BackendType> GraphicsBackend::parseBackendName(const std::string& name)
{
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return std::tolower(c); });

    if (lower == "vulkan" || lower == "vk")    return BackendType::Vulkan;
    if (lower == "opengl2" || lower == "gl2"
        || lower == "opengl" || lower == "gl") return BackendType::OpenGL2;
    if (lower == "metal" || lower == "mtl")    return BackendType::Metal;
    return std::nullopt;
}
