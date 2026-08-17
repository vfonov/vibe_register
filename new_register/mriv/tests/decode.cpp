#include "decode.hpp"

#include "util/Base64.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace mriv::term::test
{

DecodedImage decodeKittyEvent(const EscapeEvent& event)
{
    DecodedImage result;
    if (event.kind != EventKind::KittyGraphics || event.payload.empty())
        return result;

    std::vector<std::uint8_t> png = base64Decode(event.payload);
    if (png.empty())
        return result;

    int channels = 0;
    std::uint8_t* pixels = stbi_load_from_memory(
        png.data(), static_cast<int>(png.size()),
        &result.width, &result.height, &channels, 4);
    if (!pixels)
        return result;

    std::size_t count = static_cast<std::size_t>(result.width) *
                        static_cast<std::size_t>(result.height) * 4;
    result.rgba.assign(pixels, pixels + count);
    stbi_image_free(pixels);
    return result;
}

} // namespace mriv::term::test
