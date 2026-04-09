#include "Backend.h"
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

std::unique_ptr<Backend> Backend::create(BackendType type)
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

BackendType Backend::detectBest()
{
#ifdef HAS_VULKAN
    if (!isRemoteX11Display())
        return BackendType::Vulkan;
#endif
#ifdef HAS_OPENGL2
    return BackendType::OpenGL2;
#else
    throw std::runtime_error("No graphics backends compiled in");
#endif
}

const char* Backend::backendName(BackendType type)
{
    switch (type)
    {
    case BackendType::Vulkan:  return "vulkan";
    case BackendType::OpenGL2: return "opengl2";
    }
    return "unknown";
}

std::optional<BackendType> Backend::parseBackendName(const std::string& name)
{
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return std::tolower(c); });

    if (lower == "vulkan" || lower == "vk")    return BackendType::Vulkan;
    if (lower == "opengl2" || lower == "gl2"
        || lower == "opengl" || lower == "gl") return BackendType::OpenGL2;
    return std::nullopt;
}
