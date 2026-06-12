// MetalBackend.mm — native macOS Metal renderer for new_register.
//
// IMPORTANT: this translation unit is compiled with ARC (-fobjc-arc); see the
// APPLE branch of new_register/CMakeLists.txt.  All Objective-C objects below
// are managed automatically — do NOT add manual retain/release.
//
// Mirrors ImGui's example_apple_metal: a CAMetalLayer-backed MTKView, an
// imgui_impl_metal renderer, and a per-frame command buffer + render pass.
// Window/input is driven natively by main_macos.mm via imgui_impl_osx.

#include "MetalBackend.h"

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/QuartzCore.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_metal.h>
#include <backends/imgui_impl_osx.h>

#include <iostream>
#include <cstring>

#include "DebugFlag.h"   // debugLoggingEnabled()

// ---------------------------------------------------------------------------
// PIMPL: holds all Objective-C / Metal state out of the C++ header.
// ---------------------------------------------------------------------------

struct MetalBackend::Impl
{
    MTKView*                     view              = nil;   // native view (not owned; held by NSWindow)
    id<MTLDevice>                device            = nil;
    id<MTLCommandQueue>          queue             = nil;

    // Per-frame transient state (valid between imguiNewFrame() and endFrame()).
    id<MTLCommandBuffer>         commandBuffer     = nil;
    MTLRenderPassDescriptor*     renderPassDescriptor = nil;

    // Most recently committed command buffer, used by waitIdle().
    id<MTLCommandBuffer>         lastCommitted     = nil;

    // Persistent RGBA8 copy of the last presented frame, for captureScreenshot().
    id<MTLTexture>               captureTexture    = nil;

    // App-owned slice/overlay textures, keyed by their ImTextureID pointer value.
    NSMutableDictionary<NSNumber*, id<MTLTexture>>* textures = nil;
};

// Bridge helpers between ImTextureID and id<MTLTexture>.
static inline ImTextureID texToImID(id<MTLTexture> t)
{
    return (ImTextureID)(uintptr_t)(__bridge void*)t;
}
static inline NSNumber* imIDKey(ImTextureID id_)
{
    return [NSNumber numberWithUnsignedLongLong:(unsigned long long)id_];
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

MetalBackend::MetalBackend()
    : impl_(std::make_unique<Impl>())
{
    impl_->textures = [NSMutableDictionary dictionary];
}

MetalBackend::~MetalBackend() = default;

void MetalBackend::setNativeView(void* mtkView)
{
    impl_->view = (__bridge MTKView*)mtkView;
}

void MetalBackend::notifyResize(int /*width*/, int /*height*/)
{
    // The MTKView resizes its drawable automatically; we only need to flag a
    // rebuild so the main loop can react (e.g. re-layout) on the next frame.
    swapchainRebuild_ = true;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void MetalBackend::setWindowHints()
{
    // No GLFW window on macOS — nothing to hint.
}

void MetalBackend::initialize(GLFWwindow* /*window*/)
{
    if (impl_->view == nil)
        throw std::runtime_error("MetalBackend: setNativeView() must be called before initialize()");

    impl_->device = impl_->view.device ?: MTLCreateSystemDefaultDevice();
    if (impl_->device == nil)
        throw std::runtime_error("MetalBackend: no Metal device available");

    impl_->view.device = impl_->device;
    impl_->queue = [impl_->device newCommandQueue];

    // Allow reading the drawable back for screenshots.
    impl_->view.framebufferOnly = NO;

    // HiDPI: backingScaleFactor (2.0 on Retina).  We set framebufferScale_ ==
    // contentScale_ so imguiScale() == 1.0 by default; imgui_impl_osx supplies
    // DisplayFramebufferScale, giving crisp Retina rendering without manually
    // upscaling the style/fonts (which would double-scale).
    float scale = 1.0f;
    if (impl_->view.window != nil)
        scale = (float)impl_->view.window.backingScaleFactor;
    else if (NSScreen.mainScreen != nil)
        scale = (float)NSScreen.mainScreen.backingScaleFactor;
    if (scale < 1.0f) scale = 1.0f;
    contentScale_     = scale;
    framebufferScale_ = scale;

    if (debugLoggingEnabled())
        std::cerr << "[metal] Initialized: " << impl_->device.name.UTF8String
                  << " (backingScale " << scale << ")\n";
}

void MetalBackend::shutdown()
{
    impl_->captureTexture = nil;
    impl_->queue          = nil;
    impl_->device         = nil;
    impl_->view           = nil;
}

void MetalBackend::waitIdle()
{
    if (impl_->lastCommitted != nil)
        [impl_->lastCommitted waitUntilCompleted];
}

// ---------------------------------------------------------------------------
// Frame cycle
// ---------------------------------------------------------------------------

bool MetalBackend::needsSwapchainRebuild() const
{
    return swapchainRebuild_;
}

void MetalBackend::rebuildSwapchain(int /*width*/, int /*height*/)
{
    // MTKView manages drawable sizing; nothing to rebuild explicitly.
    swapchainRebuild_ = false;
}

void MetalBackend::beginFrame()
{
    // All per-frame GPU work happens in imguiNewFrame()/endFrame().
}

void MetalBackend::endFrame()
{
    ImDrawData* drawData = ImGui::GetDrawData();

    if (impl_->commandBuffer == nil)
        return;

    if (impl_->renderPassDescriptor == nil)
    {
        // No drawable this frame (e.g. window minimized) — commit empty buffer.
        [impl_->commandBuffer commit];
        impl_->lastCommitted = impl_->commandBuffer;
        impl_->commandBuffer = nil;
        return;
    }

    id<MTLRenderCommandEncoder> encoder =
        [impl_->commandBuffer renderCommandEncoderWithDescriptor:impl_->renderPassDescriptor];
    [encoder pushDebugGroup:@"new_register ImGui"];
    if (drawData != nullptr)
        ImGui_ImplMetal_RenderDrawData(drawData, impl_->commandBuffer, encoder);
    [encoder popDebugGroup];
    [encoder endEncoding];

    id<CAMetalDrawable> drawable = impl_->view.currentDrawable;
    if (drawable != nil)
    {
        // Keep an RGBA8 copy of the frame for captureScreenshot().
        id<MTLTexture> src = drawable.texture;
        if (impl_->captureTexture == nil ||
            impl_->captureTexture.width != src.width ||
            impl_->captureTexture.height != src.height)
        {
            MTLTextureDescriptor* desc =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:src.pixelFormat
                                                                   width:src.width
                                                                  height:src.height
                                                               mipmapped:NO];
            desc.usage = MTLTextureUsageShaderRead;
            desc.storageMode = MTLStorageModeManaged;
            impl_->captureTexture = [impl_->device newTextureWithDescriptor:desc];
        }
        id<MTLBlitCommandEncoder> blit = [impl_->commandBuffer blitCommandEncoder];
        [blit copyFromTexture:src toTexture:impl_->captureTexture];
        [blit endEncoding];

        [impl_->commandBuffer presentDrawable:drawable];
    }

    [impl_->commandBuffer commit];
    impl_->lastCommitted        = impl_->commandBuffer;
    impl_->commandBuffer        = nil;
    impl_->renderPassDescriptor = nil;
}

// ---------------------------------------------------------------------------
// ImGui integration
// ---------------------------------------------------------------------------

void MetalBackend::initImGui(GLFWwindow* /*window*/)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(imguiScale());

    ImGuiStyle& style = ImGui::GetStyle();
    style.SeparatorSize = ImMax(1.0f, style.SeparatorSize);
    style.DockingSeparatorSize = ImMax(1.0f, style.DockingSeparatorSize);

    // Load font at scaled size (matches Vulkan/OpenGL2 backends).
    {
        float scaledSize = fontSize_ * imguiScale();
        ImFontConfig fontCfg;
        fontCfg.SizePixels = scaledSize;
        bool loaded = false;
        if (!fontPath_.empty())
        {
            loaded = (io.Fonts->AddFontFromFileTTF(fontPath_.c_str(), scaledSize, &fontCfg) != nullptr);
            if (!loaded)
                std::cerr << "[font] Failed to load font: " << fontPath_
                          << ", falling back to ProggyForever\n";
        }
        if (!loaded)
            io.Fonts->AddFontDefaultVector(&fontCfg);
    }

    ImGui_ImplMetal_Init(impl_->device);
    ImGui_ImplOSX_Init(impl_->view);
}

void MetalBackend::shutdownImGui()
{
    ImGui_ImplMetal_Shutdown();
    ImGui_ImplOSX_Shutdown();
    ImGui::DestroyContext();
}

void MetalBackend::imguiNewFrame()
{
    impl_->commandBuffer = [impl_->queue commandBuffer];
    impl_->renderPassDescriptor = impl_->view.currentRenderPassDescriptor;

    if (impl_->renderPassDescriptor != nil)
    {
        impl_->renderPassDescriptor.colorAttachments[0].clearColor =
            MTLClearColorMake(0.1, 0.1, 0.1, 1.0);
        ImGui_ImplMetal_NewFrame(impl_->renderPassDescriptor);
    }
    ImGui_ImplOSX_NewFrame(impl_->view);
}

void MetalBackend::imguiRenderDrawData()
{
    endFrame();
}

// ---------------------------------------------------------------------------
// DPI
// ---------------------------------------------------------------------------

float MetalBackend::contentScale() const
{
    return contentScale_;
}

void MetalBackend::setContentScale(float scale)
{
    contentScale_ = scale;
    manualScale_  = true;
}

float MetalBackend::imguiScale() const
{
    if (manualScale_) return contentScale_;
    return (framebufferScale_ > 0.0f) ? contentScale_ / framebufferScale_ : 1.0f;
}

void MetalBackend::setFontConfig(const std::string& fontPath, float fontSize)
{
    fontPath_ = fontPath;
    fontSize_ = fontSize;
}

// ---------------------------------------------------------------------------
// Window control
// ---------------------------------------------------------------------------

void MetalBackend::requestClose()
{
    if (impl_->view != nil && impl_->view.window != nil)
        [impl_->view.window performSelectorOnMainThread:@selector(close)
                                             withObject:nil
                                          waitUntilDone:NO];
}

void MetalBackend::windowSize(int& w, int& h) const
{
    w = 0;
    h = 0;
    if (impl_->view != nil)
    {
        // Window size in points (screen coordinates), matching GLFW semantics.
        NSSize sz = impl_->view.bounds.size;
        w = (int)sz.width;
        h = (int)sz.height;
    }
}

// ---------------------------------------------------------------------------
// Screenshot
// ---------------------------------------------------------------------------

std::vector<uint8_t> MetalBackend::captureScreenshot(int& width, int& height)
{
    id<MTLTexture> tex = impl_->captureTexture;
    if (tex == nil)
    {
        width = 0;
        height = 0;
        return {};
    }

    width  = (int)tex.width;
    height = (int)tex.height;

    // Synchronize the managed texture so the CPU can read the GPU-written copy.
    {
        id<MTLCommandBuffer> cb = [impl_->queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        [blit synchronizeResource:tex];
        [blit endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
    }

    std::vector<uint8_t> pixels((size_t)width * height * 4);
    [tex getBytes:pixels.data()
      bytesPerRow:(NSUInteger)width * 4
       fromRegion:MTLRegionMake2D(0, 0, width, height)
      mipmapLevel:0];

    // MTKView drawables are typically BGRA8 — swizzle to RGBA8 to match the
    // contract used by the Vulkan/OpenGL2 backends and stb_image_write.
    if (tex.pixelFormat == MTLPixelFormatBGRA8Unorm ||
        tex.pixelFormat == MTLPixelFormatBGRA8Unorm_sRGB)
    {
        for (size_t i = 0; i + 3 < pixels.size(); i += 4)
            std::swap(pixels[i], pixels[i + 2]);
    }

    // Metal texture origin is top-left (same as the expected output), so no
    // vertical flip is needed (unlike OpenGL's bottom-up readback).
    return pixels;
}

// ---------------------------------------------------------------------------
// Texture management
// ---------------------------------------------------------------------------

std::unique_ptr<Texture> MetalBackend::createTexture(int w, int h, const void* data)
{
    MTLTextureDescriptor* desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:(NSUInteger)w
                                                          height:(NSUInteger)h
                                                       mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModeManaged;

    id<MTLTexture> mtlTex = [impl_->device newTextureWithDescriptor:desc];
    if (mtlTex == nil)
        throw std::runtime_error("MetalBackend: failed to create texture");

    if (data != nullptr)
        [mtlTex replaceRegion:MTLRegionMake2D(0, 0, w, h)
                  mipmapLevel:0
                    withBytes:data
                  bytesPerRow:(NSUInteger)w * 4];

    auto tex = std::make_unique<Texture>();
    tex->id     = texToImID(mtlTex);
    tex->width  = w;
    tex->height = h;

    impl_->textures[imIDKey(tex->id)] = mtlTex;   // retain (ARC)
    return tex;
}

void MetalBackend::updateTexture(Texture* tex, const void* data)
{
    if (!tex || data == nullptr) return;
    id<MTLTexture> mtlTex = impl_->textures[imIDKey(tex->id)];
    if (mtlTex == nil) return;

    [mtlTex replaceRegion:MTLRegionMake2D(0, 0, tex->width, tex->height)
              mipmapLevel:0
                withBytes:data
              bytesPerRow:(NSUInteger)tex->width * 4];
}

void MetalBackend::destroyTexture(Texture* tex)
{
    if (!tex) return;
    [impl_->textures removeObjectForKey:imIDKey(tex->id)];   // release (ARC)
    tex->id = 0;
}

void MetalBackend::shutdownTextureSystem()
{
    [impl_->textures removeAllObjects];
}
