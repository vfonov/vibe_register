// main_macos.mm — native macOS (Cocoa + Metal) entry point for new_register.
//
// IMPORTANT: compiled with ARC (-fobjc-arc); see the APPLE branch of
// new_register/CMakeLists.txt.
//
// This is the macOS counterpart to main.cpp.  Instead of GLFW it uses a native
// NSApplication / NSWindow / MTKView and ImGui's imgui_impl_osx (input) +
// imgui_impl_metal (rendering, via MetalBackend).  All platform-independent
// setup (CLI parsing, config, QC, volume loading) is shared with main.cpp:
// argument parsing lives in CliArgs.{h,cpp}; the object-graph wiring below
// mirrors main.cpp and must be kept in sync with it.

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_osx.h>

#include <memory>
#include <iostream>
#include <filesystem>
#include <vector>

#include "AppConfig.h"
#include "AppState.h"
#include "CliArgs.h"
#include "ColourMap.h"
#include "DebugFlag.h"
#include "Interface.h"
#include "MetalBackend.h"
#include "Prefetcher.h"
#include "QCState.h"
#include "ViewManager.h"
#include "Volume.h"

// ---------------------------------------------------------------------------
// AppContext — owns the whole C++ object graph for the lifetime of the app.
// Constructed after the Metal backend + window exist; referenced by the
// MTKView/NSApplication delegate each frame.
// ---------------------------------------------------------------------------

struct AppContext
{
    ParsedArgs                  args;
    AppConfig                   mergedCfg;
    QCState                     qcState;
    AppState                    state;

    std::unique_ptr<MetalBackend> backend;
    std::unique_ptr<ViewManager>  viewManager;
    std::unique_ptr<Interface>    interface;
    std::unique_ptr<Prefetcher>   prefetcher;

    int  initW = 0;
    int  initH = 0;
    bool cliSyncCursor = false;
    bool cliSyncZoom   = false;
    bool cliSyncPan    = false;
    bool cleanedUp     = false;
};

// ---------------------------------------------------------------------------
// Delegate: drives the per-frame render loop and app/window lifecycle.
// ---------------------------------------------------------------------------

@interface NRAppDelegate : NSObject <NSApplicationDelegate, MTKViewDelegate>
@property (nonatomic, assign) AppContext* ctx;
@property (nonatomic, strong) MTKView* view;
- (void)cleanup;
@end

@implementation NRAppDelegate

- (void)drawInMTKView:(MTKView*)view
{
    AppContext* ctx = self.ctx;
    if (ctx == nullptr) return;

    // Incrementally load one prefetched volume per frame (main thread only —
    // libminc/HDF5 are not thread-safe).
    if (ctx->prefetcher)
        ctx->prefetcher->loadPending();

    ctx->backend->imguiNewFrame();   // ImGui_ImplMetal_NewFrame + ImGui_ImplOSX_NewFrame
    ImGui::NewFrame();

    ctx->interface->render(*ctx->backend);

    ImGui::Render();
    ctx->backend->endFrame();        // encode draw data, present drawable, commit
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size
{
    AppContext* ctx = self.ctx;
    if (ctx && ctx->backend)
        ctx->backend->notifyResize((int)size.width, (int)size.height);
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender
{
    return YES;
}

- (void)applicationWillTerminate:(NSNotification*)notification
{
    [self cleanup];
}

// Tear down GPU + ImGui resources in the same order as main.cpp's shutdown.
- (void)cleanup
{
    AppContext* ctx = self.ctx;
    if (ctx == nullptr || ctx->cleanedUp) return;
    ctx->cleanedUp = true;

    ctx->backend->waitIdle();

    if (ctx->qcState.active)
        ctx->qcState.saveOutputCsv();

    if (ctx->viewManager)
        ctx->viewManager->destroyAllTextures();
    ctx->backend->shutdownTextureSystem();
    ctx->backend->shutdownImGui();
    ctx->backend->shutdown();
}

@end

// ---------------------------------------------------------------------------
// Platform-independent setup (mirrors main.cpp lines for config/QC/volumes).
// Returns 0 on success; a non-zero process exit code otherwise.
// ---------------------------------------------------------------------------

static int setupAppCore(AppContext& ctx)
{
    const ParsedArgs& args = ctx.args;

    std::string cliConfigPath  = args.configPath;
    std::string cliBackendName = args.backendName;
    std::string cliTagPath     = args.tagsPath;

    bool useTestData = args.test;
    debugLoggingEnabled().store(args.debug);

    std::string qcInputPath  = args.qcInputPath;
    std::string qcOutputPath = args.qcOutputPath;

    if (!qcInputPath.empty() && qcOutputPath.empty())
    {
        std::cerr << "Error: --qc requires --qc-output <path>\n";
        return 1;
    }

    // Backend selection: macOS only ships the Metal backend.
    if (!cliBackendName.empty() && cliBackendName != "auto")
    {
        auto parsed = GraphicsBackend::parseBackendName(cliBackendName);
        if (!parsed || *parsed != BackendType::Metal)
        {
            std::cerr << "Backend '" << cliBackendName
                      << "' is not available on macOS (only 'metal').\n";
            return 1;
        }
    }

    ctx.cliSyncCursor = args.syncCursor || args.syncAll;
    ctx.cliSyncZoom   = args.syncZoom   || args.syncAll;
    ctx.cliSyncPan    = args.syncPan    || args.syncAll;

    std::vector<std::string> volumeFiles = args.volumeFiles;

    std::string localConfigPath;
    if (!cliConfigPath.empty())
        localConfigPath = cliConfigPath;
    else if (std::filesystem::exists("config.json"))
        localConfigPath = "config.json";

    if (!localConfigPath.empty())
    {
        try { ctx.mergedCfg = loadConfig(localConfigPath); }
        catch (const std::exception& e) { std::cerr << "Warning: " << e.what() << "\n"; }
    }

    // --- QC mode initialization ---
    if (!qcInputPath.empty())
    {
        ctx.qcState.active = true;
        ctx.qcState.singleVerdictMode = args.qcSingleMode;
        ctx.qcState.inputCsvPath = qcInputPath;
        ctx.qcState.outputCsvPath = qcOutputPath;
        ctx.qcState.loadInputCsv(qcInputPath);
        if (std::filesystem::exists(qcOutputPath))
            ctx.qcState.loadOutputCsv(qcOutputPath);
        if (ctx.mergedCfg.qcColumns)
            ctx.qcState.columnConfigs = *ctx.mergedCfg.qcColumns;
        ctx.qcState.showOverlay = ctx.mergedCfg.global.showOverlay;

        int startRow = ctx.qcState.firstUnratedRow();
        if (startRow < 0) startRow = 0;
        ctx.qcState.currentRowIndex = startRow;
    }
    else if (volumeFiles.empty() && !ctx.mergedCfg.volumes.empty())
    {
        for (const auto& vc : ctx.mergedCfg.volumes)
            if (!vc.path.empty())
                volumeFiles.push_back(vc.path);
    }

    if (!ctx.qcState.active && !volumeFiles.empty())
    {
        for (const auto& path : volumeFiles)
        {
            try { ctx.state.loadVolume(path); }
            catch (const std::exception& e)
            {
                std::cerr << "Failed to load volume: " << e.what() << "\n";
            }
        }
        ctx.state.disambiguateVolumeNames();

        if (!cliTagPath.empty())
        {
            std::snprintf(ctx.state.combinedTagPath_,
                          sizeof(ctx.state.combinedTagPath_),
                          "%s", cliTagPath.c_str());
            ctx.state.loadCombinedTags(cliTagPath);
        }
        else
        {
            for (size_t volIdx = 0; volIdx < ctx.state.volumeCount(); ++volIdx)
                ctx.state.loadTagsForVolume(static_cast<int>(volIdx));
        }
    }
    else if (!ctx.qcState.active && useTestData)
    {
        Volume vol;
        vol.generate_test_data();
        ctx.state.volumes_.push_back(std::move(vol));
        ctx.state.volumePaths_.push_back("");
        ctx.state.volumeNames_.push_back("Test Data");
    }
    else if (!ctx.qcState.active)
    {
        std::cerr << "Error: no volume files specified.\n\n"
                  << "Usage: new_register [options] [volume1.mnc ...]\n"
                  << "\nRun 'new_register --help' for full option list.\n"
                  << "Run 'new_register --test' to launch with a generated test volume.\n";
        return 1;
    }

    ctx.state.localConfigPath_ = localConfigPath;

    // Initial window size: config override, else a default based on column count.
    int numVols = ctx.qcState.active ? ctx.qcState.columnCount() : (int)ctx.state.volumeCount();
    if (numVols < 1) numVols = 1;
    constexpr int colWidth = 300;
    constexpr int baseHeight = 480;
    int totalCols = numVols + (numVols > 1 ? 1 : 0);
    ctx.initW = colWidth * totalCols;
    ctx.initH = baseHeight;
    if (ctx.mergedCfg.global.windowWidth.has_value())
        ctx.initW = ctx.mergedCfg.global.windowWidth.value();
    if (ctx.mergedCfg.global.windowHeight.has_value())
        ctx.initH = ctx.mergedCfg.global.windowHeight.value();

    return 0;
}

// Wire ViewManager/Interface/Prefetcher and load initial textures.
// Mirrors main.cpp lines 541-646 (minus GLFW WindowManager).
static void setupViewsAndTextures(AppContext& ctx)
{
    ctx.viewManager = std::make_unique<ViewManager>(ctx.state, *ctx.backend);
    ctx.interface   = std::make_unique<Interface>(ctx.state, *ctx.viewManager, ctx.qcState);

    if (ctx.qcState.active)
    {
        ctx.prefetcher = std::make_unique<Prefetcher>(ctx.state.volumeCache_);
        ctx.interface->setPrefetcher(ctx.prefetcher.get());
    }

    if (ctx.qcState.active && ctx.qcState.rowCount() > 0)
    {
        const auto& paths = ctx.qcState.pathsForRow(ctx.qcState.currentRowIndex);
        ctx.state.loadVolumeSet(paths);
        ctx.state.applyConfig(ctx.mergedCfg, ctx.initW, ctx.initH);
        if (ctx.cliSyncCursor) ctx.state.syncCursors_ = true;
        if (ctx.cliSyncZoom)   ctx.state.syncZoom_ = true;
        if (ctx.cliSyncPan)    ctx.state.syncPan_ = true;

        for (int ci = 0; ci < ctx.qcState.columnCount() && ci < (int)ctx.state.volumeCount(); ++ci)
        {
            auto it = ctx.qcState.columnConfigs.find(ctx.qcState.columnNames[ci]);
            if (it != ctx.qcState.columnConfigs.end())
            {
                VolumeViewState& vs = ctx.state.viewStates_[ci];
                auto cmOpt = colourMapByName(it->second.colourMap);
                if (cmOpt) vs.colourMap = *cmOpt;
                if (it->second.valueMin) vs.valueRange[0] = *it->second.valueMin;
                if (it->second.valueMax) vs.valueRange[1] = *it->second.valueMax;
            }
        }
        ctx.viewManager->initializeAllTextures();

        if (ctx.prefetcher)
        {
            std::vector<std::string> prefetchPaths;
            int row = ctx.qcState.currentRowIndex;
            if (row > 0)
            {
                const auto& prev = ctx.qcState.pathsForRow(row - 1);
                prefetchPaths.insert(prefetchPaths.end(), prev.begin(), prev.end());
            }
            if (row + 1 < ctx.qcState.rowCount())
            {
                const auto& next = ctx.qcState.pathsForRow(row + 1);
                prefetchPaths.insert(prefetchPaths.end(), next.begin(), next.end());
            }
            if (!prefetchPaths.empty())
                ctx.prefetcher->requestPrefetch(prefetchPaths);
        }
    }
    else if (!ctx.state.volumes_.empty())
    {
        ctx.state.initializeViewStates();
        ctx.state.applyConfig(ctx.mergedCfg, ctx.initW, ctx.initH);
        if (ctx.cliSyncCursor) ctx.state.syncCursors_ = true;
        if (ctx.cliSyncZoom)   ctx.state.syncZoom_ = true;
        if (ctx.cliSyncPan)    ctx.state.syncPan_ = true;

        const auto& perVol = ctx.args.perVolOpts;
        for (size_t vi = 0; vi < perVol.size() && vi < ctx.state.volumeCount(); ++vi)
            if (perVol[vi].colourMap.has_value())
                ctx.state.viewStates_[vi].colourMap = *perVol[vi].colourMap;
        for (size_t vi = 0; vi < perVol.size() && vi < ctx.state.volumeCount(); ++vi)
            if (perVol[vi].range.has_value())
                ctx.state.viewStates_[vi].valueRange = *perVol[vi].range;
        for (size_t vi = 0; vi < perVol.size() && vi < ctx.state.volumeCount(); ++vi)
        {
            if (perVol[vi].isLabel)
            {
                ctx.state.volumes_[vi].setLabelVolume(true);
                if (!perVol[vi].colourMap.has_value())
                    ctx.state.viewStates_[vi].colourMap = ColourMapType::Viridis;
            }
            if (perVol[vi].labelDescFile.has_value())
                ctx.state.volumes_[vi].loadLabelDescriptionFile(*perVol[vi].labelDescFile);
        }

        ctx.viewManager->initializeAllTextures();
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    @autoreleasepool
    {
        auto parsed = parseArgs(argc, argv);
        if (!parsed)
            return 1;
        if (parsed->help)
        {
            printUsage();
            return 0;
        }

        auto ctx = std::make_unique<AppContext>();
        ctx->args = std::move(*parsed);

        try
        {
            int rc = setupAppCore(*ctx);
            if (rc != 0)
                return rc;

            // --- Native window + Metal view ---
            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

            // Clamp the requested size to the visible screen area.
            NSRect screen = NSScreen.mainScreen.visibleFrame;
            int w = ctx->initW, h = ctx->initH;
            if (w > (int)(screen.size.width  * 0.95)) w = (int)(screen.size.width  * 0.95);
            if (h > (int)(screen.size.height * 0.95)) h = (int)(screen.size.height * 0.95);

            NSRect frame = NSMakeRect(0, 0, w, h);
            NSWindow* window =
                [[NSWindow alloc] initWithContentRect:frame
                                            styleMask:(NSWindowStyleMaskTitled |
                                                       NSWindowStyleMaskClosable |
                                                       NSWindowStyleMaskMiniaturizable |
                                                       NSWindowStyleMaskResizable)
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
            window.title = @"New Register (metal)";
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
            ctx->backend = std::make_unique<MetalBackend>();
            ctx->backend->setNativeView((__bridge void*)view);
            ctx->backend->setWindowHints();        // no-op on Metal
            ctx->backend->initialize(nullptr);     // GLFW window unused on Metal
            ctx->backend->setFontConfig(ctx->mergedCfg.global.fontPath,
                                        ctx->mergedCfg.global.fontSize);
            ctx->backend->initImGui(nullptr);

            ctx->state.dpiScale_ = ctx->backend->imguiScale();

            setupViewsAndTextures(*ctx);

            // --- Delegate + run loop ---
            NRAppDelegate* delegate = [[NRAppDelegate alloc] init];
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
        catch (const std::exception& e)
        {
            std::cerr << "Fatal error: " << e.what() << "\n";
            return 1;
        }
    }
}
