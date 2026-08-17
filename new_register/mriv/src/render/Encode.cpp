#include "render/Encode.hpp"

#include <stdexcept>

#include "util/Base64.hpp"

// stb_image_write requires the implementation macro in exactly one .cpp.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace mriv::term
{

namespace
{

void stbiWriteCallback(void* context, void* data, int size)
{
    std::vector<uint8_t>* out = static_cast<std::vector<uint8_t>*>(context);
    auto* bytes = static_cast<const uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

std::vector<uint8_t> writePngToMemory(const std::uint8_t* rgba,
                                      std::size_t width,
                                      std::size_t height)
{
    std::vector<uint8_t> png;

    // stbi_write_png_to_func expects top-down RGBA. renderSlice() already
    // produces top-down; the caller must flip before calling if they have
    // bottom-up data.
    int ok = stbi_write_png_to_func(stbiWriteCallback, &png,
                                    static_cast<int>(width),
                                    static_cast<int>(height),
                                    4,                    // RGBA
                                    rgba,
                                    static_cast<int>(width * 4));
    if (!ok)
        throw std::runtime_error("mriv: PNG encoding failed");

    return png;
}

} // namespace

std::string encodePixels(PixelProtocol protocol,
                         const std::uint8_t* rgba,
                         std::size_t width,
                         std::size_t height)
{
    switch (protocol)
    {
    case PixelProtocol::Kitty:
        return encodeKittyPng(rgba, width, height);
    case PixelProtocol::None:
    case PixelProtocol::Sixel:
    case PixelProtocol::ITerm2:
    default:
        return {};
    }
}

std::string encodeKittyPng(const std::uint8_t* rgba,
                           std::size_t width,
                           std::size_t height)
{
    if (!rgba || width == 0 || height == 0)
        return {};

    std::vector<uint8_t> png = writePngToMemory(rgba, width, height);
    std::string b64          = base64Encode(png.data(), png.size());

    // Kitty graphics protocol, direct/complete PNG image (f=100, a=T).
    // ESC _ G <keyvals> ; <data> ESC backslash
    std::string out;
    out.reserve(2 + 16 + 1 + b64.size() + 2);
    out += "\x1b_Ga=T,f=100,s=";
    out += std::to_string(width);
    out += ",v=";
    out += std::to_string(height);
    out += ";";
    out += b64;
    out += "\x1b\\";
    return out;
}

} // namespace mriv::term
