// main_macos.mm — native macOS (Cocoa + Metal) entry point for new_qc.
//
// IMPORTANT: compiled with ARC (-fobjc-arc); see the APPLE branch of
// new_register/CMakeLists.txt.
//
// This is the macOS counterpart to main.cpp, mirroring the pattern used by
// new_register's own main_macos.mm: instead of GLFW it uses a native
// NSApplication / NSWindow / MTKView and ImGui's imgui_impl_osx (input) +
// imgui_impl_metal (rendering, via MetalBackend). new_qc's CLI is small
// enough (~30 lines) that it is duplicated here rather than factored into a
// shared header, consistent with this directory's existing convention of
// keeping its own local copies of Backend/BackendFactory/etc. rather than
// sharing code with new_register's top-level main.cpp/CliArgs.

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include <memory>
#include <iostream>
#include <string>
#include <optional>

#include "QCApp.h"
#include "MetalBackend.h"

static void printUsage(const char* programName)
{
    std::cout << "Usage: " << programName << " [OPTIONS] <input_csv> <output_csv>\n"
              << "\n"
              << "Quality Control tool for medical imaging datasets.\n"
              << "\n"
              << "Arguments:\n"
              << "  input_csv    CSV file with columns: id, visit, picture\n"
              << "  output_csv   CSV file for QC results (will be created/updated)\n"
              << "\n"
              << "Options:\n"
              << "  --help            Show this help message\n"
              << "  --version         Show version information\n"
              << "  --scale <factor>  Override screen content scale (HiDPI)\n"
              << "  --backend <name>  Graphics backend: metal (default; only option on macOS)\n"
              << "\n"
              << "Input CSV format:\n"
              << "  id,visit,picture\n"
              << "  subject001,baseline,/path/to/image1.png\n"
              << "  subject002,followup,/path/to/image2.jpg\n"
              << "\n"
              << "Output CSV format:\n"
              << "  id,visit,picture,QC,notes\n"
              << "  subject001,baseline,/path/to/image1.png,Pass,Good quality\n"
              << "  subject002,followup,/path/to/image2.jpg,Fail,Artifact present\n"
              << "\n"
              << "Controls:\n"
              << "  P            Mark current image as Pass\n"
              << "  F            Mark current image as Fail\n"
              << "  Left/Right   Navigate between images\n"
              << "  Page Up/Down Navigate between images\n"
              << "  Ctrl+S       Save progress manually\n"
              << "  Escape       Exit application\n"
              << "\n"
              << "HiDPI Support:\n"
              << "  The tool automatically detects the display's backing scale factor.\n"
              << "  Use --scale to override the detected value (e.g., --scale 1.5).\n"
              << "\n"
              << "The tool automatically saves progress after each QC decision.\n"
              << "Existing output files are loaded to resume interrupted work.\n";
}

static void printVersion()
{
    std::cout << "new_qc version 1.0.0\n"
              << "Quality Control tool for medical imaging\n";
}

// ---------------------------------------------------------------------------
// QCContext — owns the app object graph for the lifetime of the process.
// Referenced by the MTKView/NSApplication delegate each frame.
// ---------------------------------------------------------------------------

struct QCContext
{
    QC::QCApp qcApp;

    // Non-owning: the concrete backend is owned by qcApp (as a
    // std::unique_ptr<Backend>). Kept here, typed, so the delegate can call
    // Metal-specific notifyResize() without QCApp needing to expose it.
    MetalBackend* metalBackend = nullptr;
};

// ---------------------------------------------------------------------------
// Delegate: drives the per-frame render loop and app/window lifecycle.
// ---------------------------------------------------------------------------

@interface QCAppDelegate : NSObject <NSApplicationDelegate, MTKViewDelegate>
@property (nonatomic, assign) QCContext* ctx;
@property (nonatomic, strong) MTKView* view;
- (void)cleanup;
@end

@implementation QCAppDelegate

- (void)drawInMTKView:(MTKView*)view
{
    QCContext* ctx = self.ctx;
    if (ctx == nullptr) return;

    ctx->qcApp.renderFrame();

    if (ctx->qcApp.shouldExit())
        [NSApp terminate:nil];
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size
{
    QCContext* ctx = self.ctx;
    if (ctx && ctx->metalBackend)
        ctx->metalBackend->notifyResize((int)size.width, (int)size.height);
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender
{
    return YES;
}

- (void)applicationWillTerminate:(NSNotification*)notification
{
    [self cleanup];
}

- (void)cleanup
{
    QCContext* ctx = self.ctx;
    if (ctx == nullptr) return;
    ctx->qcApp.shutdown();   // idempotent — safe if already called
}

@end

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    @autoreleasepool
    {
        if (argc < 2)
        {
            printUsage(argv[0]);
            return 1;
        }

        std::string inputFile;
        std::string outputFile;
        std::optional<float> scaleFactor;

        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];

            if (arg == "--help" || arg == "-h")
            {
                printUsage(argv[0]);
                return 0;
            }
            else if (arg == "--version" || arg == "-v")
            {
                printVersion();
                return 0;
            }
            else if (arg == "--scale")
            {
                if (i + 1 >= argc)
                {
                    std::cerr << "Error: --scale requires a value\n";
                    return 1;
                }
                float scale = std::stof(argv[++i]);
                if (scale <= 0.0f)
                {
                    std::cerr << "Error: --scale must be positive\n";
                    return 1;
                }
                scaleFactor = scale;
            }
            else if (arg == "--backend")
            {
                if (i + 1 >= argc)
                {
                    std::cerr << "Error: --backend requires a value\n";
                    return 1;
                }
                std::string name = argv[++i];
                if (name != "metal" && name != "auto")
                {
                    std::cerr << "Backend '" << name
                              << "' is not available on macOS (only 'metal').\n";
                    return 1;
                }
            }
            else if (arg[0] == '-')
            {
                std::cerr << "Unknown option: " << arg << "\n";
                std::cerr << "Use --help for usage information\n";
                return 1;
            }
            else
            {
                if (inputFile.empty())
                    inputFile = arg;
                else if (outputFile.empty())
                    outputFile = arg;
                else
                {
                    std::cerr << "Error: Too many positional arguments\n";
                    std::cerr << "Use --help for usage information\n";
                    return 1;
                }
            }
        }

        if (inputFile.empty() || outputFile.empty())
        {
            std::cerr << "Error: Both input and output CSV files are required\n";
            std::cerr << "Use --help for usage information\n";
            return 1;
        }

        auto ctx = std::make_unique<QCContext>();

        // --- Native window + Metal view ---
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        int initW = 1280, initH = 720;
        NSRect screen = NSScreen.mainScreen.visibleFrame;
        if (initW > (int)(screen.size.width  * 0.95)) initW = (int)(screen.size.width  * 0.95);
        if (initH > (int)(screen.size.height * 0.95)) initH = (int)(screen.size.height * 0.95);

        NSRect frame = NSMakeRect(0, 0, initW, initH);
        NSWindow* window =
            [[NSWindow alloc] initWithContentRect:frame
                                        styleMask:(NSWindowStyleMaskTitled |
                                                   NSWindowStyleMaskClosable |
                                                   NSWindowStyleMaskMiniaturizable |
                                                   NSWindowStyleMaskResizable)
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
        window.title = @"new_qc - Quality Control";
        [window center];

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil)
        {
            std::cerr << "Fatal error: no Metal device available\n";
            return 1;
        }

        MTKView* view = [[MTKView alloc] initWithFrame:frame device:device];
        view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
        view.clearColor = MTLClearColorMake(0.1, 0.1, 0.1, 1.0);
        window.contentView = view;

        // --- Metal backend ---
        auto backend = std::make_unique<MetalBackend>();
        MetalBackend* metalBackendRaw = backend.get();
        backend->setNativeView((__bridge void*)view);
        backend->setWindowHints();   // no-op on Metal

        try
        {
            backend->initialize(nullptr);   // GLFW window unused on Metal
        }
        catch (const std::exception& e)
        {
            std::cerr << "Fatal error: " << e.what() << "\n";
            return 1;
        }

        if (!ctx->qcApp.initWithBackend(std::move(backend), inputFile, outputFile, scaleFactor))
            return 1;

        ctx->metalBackend = metalBackendRaw;

        // --- Delegate + run loop ---
        QCAppDelegate* delegate = [[QCAppDelegate alloc] init];
        delegate.ctx  = ctx.get();
        delegate.view = view;
        view.delegate = delegate;
        NSApp.delegate = delegate;

        [window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        [NSApp run];

        // Safety net in case applicationWillTerminate: did not run.
        [delegate cleanup];
        return 0;
    }
}
