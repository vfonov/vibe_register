#include "imgui_impl_osmesa.h"

#include <GL/osmesa.h>
#include <GL/gl.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cstdint>
#include <cstdio>
#include <vector>

static OSMesaContext      s_ctx    = nullptr;
static std::vector<uint8_t> s_buffer;
static int                s_width  = 0;
static int                s_height = 0;

bool osmesa_init(int width, int height)
{
    s_ctx = OSMesaCreateContextExt(OSMESA_RGBA, 24, 8, 0, nullptr);
    if (!s_ctx)
    {
        fprintf(stderr, "osmesa_init: OSMesaCreateContextExt failed\n");
        return false;
    }

    s_width  = width;
    s_height = height;
    s_buffer.assign((size_t)width * height * 4, 0);

    if (!OSMesaMakeCurrent(s_ctx, s_buffer.data(), GL_UNSIGNED_BYTE, width, height))
    {
        fprintf(stderr, "osmesa_init: OSMesaMakeCurrent failed\n");
        OSMesaDestroyContext(s_ctx);
        s_ctx = nullptr;
        return false;
    }

    // pixel (0,0) is top-left — matches normal image conventions
    OSMesaPixelStore(OSMESA_Y_UP, 0);
    return true;
}

void osmesa_shutdown()
{
    if (s_ctx)
    {
        OSMesaDestroyContext(s_ctx);
        s_ctx = nullptr;
    }
    s_buffer.clear();
    s_width  = 0;
    s_height = 0;
}

void osmesa_save_png(const std::string& path)
{
    int ok = stbi_write_png(path.c_str(), s_width, s_height, 4,
                            s_buffer.data(), s_width * 4);
    if (!ok)
        fprintf(stderr, "osmesa_save_png: stbi_write_png failed for %s\n",
                path.c_str());
}
