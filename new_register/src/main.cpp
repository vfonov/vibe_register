#include <imgui.h>
#include <imgui_internal.h>
#include <array>
#include <vector>
#include <iostream>
#include <string>
#include <string_view>
#include <algorithm>
#include <memory>
#include <optional>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "AppConfig.h"
#include "AppState.h"
#include "CliArgs.h"
#include "ColourMap.h"
#include "GraphicsBackend.h"
#include "Interface.h"
#include "Prefetcher.h"
#include "QCState.h"
#include "Volume.h"
#include "ViewManager.h"
#include "WindowManager.h"
#include "WaylandTouchInput.h"

#include <glm/glm.hpp>

extern "C" {
#include "minc2-simple.h"
}

// ---------------------------------------------------------------------------
// GLFW error callback — prints diagnostic info to stderr
// ---------------------------------------------------------------------------
static void glfwErrorCallback(int error, const char* description)
{
    if (debugLoggingEnabled())
        std::cerr << "[glfw] Error " << error << ": " << description << "\n";
}

// CLI argument parsing lives in CliArgs.h / CliArgs.cpp (shared with main_macos.mm).

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

  int main(int argc, char** argv)
    {
        bool glfwInitialized = false;

        try
        {
        auto parsed = parseArgs(argc, argv);
        if (!parsed)
            return 1;

        auto& args = *parsed;

        if (args.help)
        {
            printUsage();
            return 0;
        }

        std::string cliConfigPath = args.configPath;
        std::string cliBackendName = args.backendName;
        std::string cliTagPath = args.tagsPath;

        bool useTestData = args.test;
        debugLoggingEnabled().store(args.debug);

        std::string qcInputPath = args.qcInputPath;
        std::string qcOutputPath = args.qcOutputPath;

        if (!qcInputPath.empty() && qcOutputPath.empty())
        {
            std::cerr << "Error: --qc requires --qc-output <path>\n";
            return 1;
        }

        bool cliSyncCursor = args.syncCursor || args.syncAll;
        bool cliSyncZoom   = args.syncZoom   || args.syncAll;
        bool cliSyncPan    = args.syncPan    || args.syncAll;

        std::vector<std::string> volumeFiles = std::move(args.volumeFiles);

        // --- Backend selection ---
        BackendType backendType;
        if (!cliBackendName.empty())
        {
            if (cliBackendName == "auto")
            {
                backendType = GraphicsBackend::detectBest();
            }
            else
            {
                auto parsed = GraphicsBackend::parseBackendName(cliBackendName);
                if (!parsed)
                {
                    std::cerr << "Unknown backend: " << cliBackendName << "\n";
                    std::cerr << "Available:";
                    for (auto b : GraphicsBackend::availableBackends())
                        std::cerr << " " << GraphicsBackend::backendName(b);
                    std::cerr << "\n";
                    return 1;
                }
                backendType = *parsed;
            }
        }
        else
        {
            backendType = GraphicsBackend::detectBest();
        }

        if (debugLoggingEnabled())
        {
            std::cerr << "[backend] Using: " << GraphicsBackend::backendName(backendType) << "\n";
            std::cerr << "[backend] Available:";
            for (auto b : GraphicsBackend::availableBackends())
                std::cerr << " " << GraphicsBackend::backendName(b);
            std::cerr << "\n";
        }

        std::string localConfigPath;
        if (!cliConfigPath.empty())
        {
            localConfigPath = cliConfigPath;
        }
        else if (std::filesystem::exists("config.json"))
        {
            localConfigPath = "config.json";
        }

        AppConfig mergedCfg;
        if (!localConfigPath.empty())
        {
            try { mergedCfg = loadConfig(localConfigPath); }
            catch (const std::exception& e)
            {
                std::cerr << "Warning: " << e.what() << "\n";
            }
        }

        // --- QC mode initialization ---
        QCState qcState;
        if (!qcInputPath.empty())
        {
            qcState.active = true;
            qcState.singleVerdictMode = args.qcSingleMode;
            qcState.inputCsvPath = qcInputPath;
            qcState.outputCsvPath = qcOutputPath;
            qcState.loadInputCsv(qcInputPath);
            if (std::filesystem::exists(qcOutputPath))
                qcState.loadOutputCsv(qcOutputPath);
            if (mergedCfg.qcColumns)
                qcState.columnConfigs = *mergedCfg.qcColumns;
            qcState.showOverlay = mergedCfg.global.showOverlay;
        }

        AppState state;

        if (qcState.active)
        {
            // In QC mode, volumes are loaded after backend init (below).
            // Just determine the starting row.
            int startRow = qcState.firstUnratedRow();
            if (startRow < 0) startRow = 0;
            qcState.currentRowIndex = startRow;
        }
        else if (volumeFiles.empty() && !mergedCfg.volumes.empty())
        {
            for (const auto& vc : mergedCfg.volumes)
            {
                if (!vc.path.empty())
                    volumeFiles.push_back(vc.path);
            }
        }

        if (!qcState.active && !volumeFiles.empty())
        {
            for (const auto& path : volumeFiles)
            {
                try
                {
                    state.loadVolume(path);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Failed to load volume: " << e.what() << "\n";
                }
            }

            // Resolve duplicate basenames (e.g. 1/vol.mnc and 2/vol.mnc)
            // so that each ImGui window gets a unique title.
            state.disambiguateVolumeNames();

            // Load tags: if --tags was specified, use combined tag file;
            // otherwise fall back to per-volume auto-discovery.
            if (!cliTagPath.empty()) {
                std::snprintf(state.combinedTagPath_,
                              sizeof(state.combinedTagPath_),
                              "%s", cliTagPath.c_str());
                state.loadCombinedTags(cliTagPath);
            } else {
                for (size_t volIdx = 0; volIdx < state.volumeCount(); ++volIdx) {
                    state.loadTagsForVolume(static_cast<int>(volIdx));
                }
            }
        }
        else if (!qcState.active && useTestData)
        {
            Volume vol;
            vol.generate_test_data();
            state.volumes_.push_back(std::move(vol));
            state.volumePaths_.push_back("");
            state.volumeNames_.push_back("Test Data");
        }
        else if (!qcState.active)
        {
            std::cerr << "Error: no volume files specified.\n\n"
                      << "Usage: new_register [options] [volume1.mnc ...]\n"
                      << "\nRun 'new_register --help' for full option list.\n"
                      << "Run 'new_register --test' to launch with a generated test volume.\n";
            return 1;
        }

        // On Wayland sessions, force GLFW to use its native Wayland backend so that
        // wl_touch events (finger touch on touch screens) are delivered correctly.
        // Without this, GLFW may use XWayland where touch events are silently dropped.
        // glfwInitHint(GLFW_PLATFORM, …) requires GLFW 3.4+.
#if GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
        if (getenv("WAYLAND_DISPLAY") != nullptr)
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
#endif

        if (!glfwInit())
        {
            std::cerr << "Failed to initialize GLFW\n";
            return 1;
        }
        glfwInitialized = true;

        glfwSetErrorCallback(glfwErrorCallback);

        // Always enable GLFW_SCALE_TO_MONITOR for proper HiDPI framebuffer scaling
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

        // Determine scale factor: command-line override takes precedence
        float initScale = 1.0f;
        bool scaleOverride = args.scaleFactor.has_value();

        if (scaleOverride)
        {
            initScale = args.scaleFactor.value();
            if (debugLoggingEnabled())
                std::cerr << "[window] Using scale override: " << initScale << "\n";
        }

        // Create backend before window so it can set appropriate GLFW hints
        auto backend = GraphicsBackend::create(backendType);
        backend->setWindowHints();

        int monWorkX = 0, monWorkY = 0, monWorkW = 1280, monWorkH = 720;
        {
            float sx = 1.0f, sy = 1.0f;
            GLFWmonitor* primary = glfwGetPrimaryMonitor();
            if (primary)
            {
                glfwGetMonitorContentScale(primary, &sx, &sy);
                glfwGetMonitorWorkarea(primary, &monWorkX, &monWorkY,
                                      &monWorkW, &monWorkH);
                if (debugLoggingEnabled())
                {
                    std::cerr << "[window] Monitor content scale: " << sx << " x " << sy << "\n";
                    std::cerr << "[window] Monitor workarea: "
                              << monWorkX << "," << monWorkY << " "
                              << monWorkW << "x" << monWorkH << "\n";
                    const GLFWvidmode* vmode = glfwGetVideoMode(primary);
                    if (vmode)
                        std::cerr << "[window] Video mode: "
                                  << vmode->width << "x" << vmode->height
                                  << " @ " << vmode->refreshRate << "Hz\n";
                }
            }
            if (!scaleOverride)
            {
                initScale = (sx > sy) ? sx : sy;
                if (initScale < 1.0f) initScale = 1.0f;
            }
        }

        int numVols = qcState.active ? qcState.columnCount() : state.volumeCount();
        if (numVols < 1) numVols = 1;

        constexpr int colWidth  = 300;
        constexpr int baseHeight = 480;

        int totalCols = numVols + (numVols > 1 ? 1 : 0);
        int initW = colWidth * totalCols;
        int initH = baseHeight;

        if (mergedCfg.global.windowWidth.has_value())
            initW = mergedCfg.global.windowWidth.value();
        if (mergedCfg.global.windowHeight.has_value())
            initH = mergedCfg.global.windowHeight.value();

        int maxW = static_cast<int>(monWorkW * 0.9f);
        int maxH = static_cast<int>(monWorkH * 0.9f);
        if (initW > maxW) initW = maxW;
        if (initH > maxH) initH = maxH;

        if (debugLoggingEnabled())
        {
            std::cerr << "[window] Auto size: "
                      << static_cast<int>(colWidth * totalCols * initScale) << "x"
                      << static_cast<int>(baseHeight * initScale) << "\n";
            std::cerr << "[window] Config override: "
                      << (mergedCfg.global.windowWidth.has_value()
                          ? std::to_string(*mergedCfg.global.windowWidth) : "none")
                      << " x "
                      << (mergedCfg.global.windowHeight.has_value()
                          ? std::to_string(*mergedCfg.global.windowHeight) : "none")
                      << "\n";
            std::cerr << "[window] Clamped request: " << initW << "x" << initH
                      << " (max " << maxW << "x" << maxH << ")\n";
            std::cerr << "[window] GLFW_SCALE_TO_MONITOR: ON"
                         " (GLFW may multiply by content scale internally)\n";
        }

        std::string windowTitle = std::string("New Register (") +
            GraphicsBackend::backendName(backendType) + ")";
        GLFWwindow* window = glfwCreateWindow(initW, initH,
                                              windowTitle.c_str(),
                                              nullptr, nullptr);

        // Try to initialize the chosen backend.  If window creation or
        // backend init fails, fall through to the fallback loop below.
        bool initialized = false;
        if (window)
        {
            try
            {
                backend->initialize(window);
                initialized = true;
            }
            catch (const std::exception& e)
            {
                if (debugLoggingEnabled())
                    std::cerr << "[backend] " << GraphicsBackend::backendName(backendType)
                              << " init failed: " << e.what() << "\n";
            }
        }
        else
        {
            if (debugLoggingEnabled())
            {
                const char* errDesc = nullptr;
                int errCode = glfwGetError(&errDesc);
                std::cerr << "[backend] " << GraphicsBackend::backendName(backendType)
                          << " failed to create window"
                          << " (glfw error " << errCode
                          << ": " << (errDesc ? errDesc : "unknown") << ")\n";
            }
        }

        // If the chosen backend uses OpenGL and GLX failed, retry with EGL
        // before falling back to a completely different backend.  X2Go's
        // nxagent only provides GLX 1.2, but GLFW requires GLX 1.3; EGL
        // bypasses GLX entirely and works with Mesa's software renderer.
        if (!initialized && backendType == BackendType::OpenGL2)
        {
            if (debugLoggingEnabled())
                std::cerr << "[backend] Retrying opengl2 with EGL context\n";
            if (window)
            {
                glfwDestroyWindow(window);
                window = nullptr;
            }
            backend = GraphicsBackend::create(BackendType::OpenGL2);
            backend->setWindowHints();
            glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
            if (!scaleOverride)
                glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
            windowTitle = "New Register (opengl2-egl)";

            // Use conservative dimensions for the EGL retry.  Large windows
            // can cause fatal X I/O errors on X proxies like nxagent (the
            // Xlib error handler calls exit(), crashing the process).  We
            // create a small window first, then resize after success.
            constexpr int safeW = 800, safeH = 600;
            if (debugLoggingEnabled())
                std::cerr << "[window] EGL retry with safe size: "
                          << safeW << "x" << safeH
                          << " (will resize to " << initW << "x" << initH << ")\n";
            window = glfwCreateWindow(safeW, safeH,
                windowTitle.c_str(), nullptr, nullptr);
            if (window)
            {
                try
                {
                    backend->initialize(window);
                    initialized = true;
                    // Now resize to the desired dimensions.
                    glfwSetWindowSize(window, initW, initH);
                }
                catch (const std::exception& e)
                {
                    if (debugLoggingEnabled())
                        std::cerr << "[backend] opengl2-egl init failed: "
                                  << e.what() << "\n";
                }
            }
            else
            {
                if (debugLoggingEnabled())
                {
                    const char* errDesc = nullptr;
                    int errCode = glfwGetError(&errDesc);
                    std::cerr << "[backend] opengl2-egl failed to create window"
                              << " (glfw error " << errCode
                              << ": " << (errDesc ? errDesc : "unknown") << ")\n";
                }
            }
        }

        // Fallback: try every other compiled-in backend.
        // Use safe dimensions to avoid fatal X I/O errors on proxies,
        // then resize after successful initialization.
        if (!initialized)
        {
            constexpr int safeW = 800, safeH = 600;
            for (auto fallback : GraphicsBackend::availableBackends())
            {
                if (fallback == backendType)
                    continue;
                if (debugLoggingEnabled())
                    std::cerr << "[backend] Trying fallback: "
                              << GraphicsBackend::backendName(fallback)
                              << " (" << safeW << "x" << safeH << ")\n";
                try
                {
                    if (window)
                    {
                        glfwDestroyWindow(window);
                        window = nullptr;
                    }
                    backend = GraphicsBackend::create(fallback);
                    backend->setWindowHints();
                    if (!scaleOverride)
                        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
                    windowTitle = std::string("New Register (") +
                        GraphicsBackend::backendName(fallback) + ")";
                    window = glfwCreateWindow(safeW, safeH,
                        windowTitle.c_str(), nullptr, nullptr);
                    if (!window)
                    {
                        if (debugLoggingEnabled())
                        {
                            const char* errDesc = nullptr;
                            int errCode = glfwGetError(&errDesc);
                            std::cerr << "[backend] "
                                      << GraphicsBackend::backendName(fallback)
                                      << " window creation failed (glfw error "
                                      << errCode << ": "
                                      << (errDesc ? errDesc : "unknown") << ")\n";
                        }
                        continue;
                    }
                    backend->initialize(window);
                    backendType = fallback;
                    initialized = true;
                    glfwSetWindowSize(window, initW, initH);
                    break;
                }
                catch (const std::exception& e2)
                {
                    if (debugLoggingEnabled())
                        std::cerr << "[backend] " << GraphicsBackend::backendName(fallback)
                                  << " also failed: " << e2.what() << "\n";
                }
            }
        }

        if (!initialized)
        {
            std::cerr << "Error: No usable graphics backend found.\n";
            glfwTerminate();
            return 1;
        }

        // Update window title after potential fallback
        glfwSetWindowTitle(window, (std::string("New Register (") +
            GraphicsBackend::backendName(backendType) + ")").c_str());

        // On X11 HiDPI (no compositor scaling), resize the window to match the
        // effective ImGui scale (Wayland already rendered at the correct size).
        {
            float scale = backend->imguiScale();
            if (scale > 1.001f)
            {
                int winW, winH;
                glfwGetWindowSize(window, &winW, &winH);
                int baseW = colWidth * totalCols;
                if (winW <= baseW)  // GLFW_SCALE_TO_MONITOR didn't fire
                {
                    int newW = std::min(static_cast<int>(winW * scale), maxW);
                    int newH = std::min(static_cast<int>(winH * scale), maxH);
                    glfwSetWindowSize(window, newW, newH);
                }
            }
        }

        // Apply scale override BEFORE initImGui so ImGui configuration uses correct scale
        if (scaleOverride)
        {
            backend->setContentScale(args.scaleFactor.value());
            if (debugLoggingEnabled())
                std::cerr << "[window] Scale override applied: " << args.scaleFactor.value() << "\n";
        }

        // Apply font configuration from config file (must be before initImGui)
        backend->setFontConfig(mergedCfg.global.fontPath, mergedCfg.global.fontSize);

        backend->initImGui(window);

#ifdef HAS_WAYLAND_TOUCH
        WaylandTouch::install(window);
#endif

        state.dpiScale_ = backend->imguiScale();
        state.localConfigPath_ = localConfigPath;

        ViewManager viewManager(state, *backend);
        Interface interface(state, viewManager, qcState);

        // Create window manager for handling resize events
        WindowManager windowManager;
        windowManager.setFramebufferCallback(window, backend.get());

        // Create prefetcher for QC mode — queues adjacent rows for
        // main-thread loading (libminc/HDF5 are not thread-safe).
        std::unique_ptr<Prefetcher> prefetcher;
        if (qcState.active)
        {
            prefetcher = std::make_unique<Prefetcher>(state.volumeCache_);
            interface.setPrefetcher(prefetcher.get());
        }

        if (qcState.active && qcState.rowCount() > 0)
        {
            const auto& paths = qcState.pathsForRow(qcState.currentRowIndex);
            state.loadVolumeSet(paths);
            // Apply global config (sync flags, overlays, colour maps, etc.)
            state.applyConfig(mergedCfg, initW, initH);
            // CLI sync flags override config values.
            if (cliSyncCursor) state.syncCursors_ = true;
            if (cliSyncZoom)   state.syncZoom_ = true;
            if (cliSyncPan)    state.syncPan_ = true;
            // Apply per-column configs (colour map, value range)
            for (int ci = 0; ci < qcState.columnCount() && ci < state.volumeCount(); ++ci)
            {
                auto it = qcState.columnConfigs.find(qcState.columnNames[ci]);
                if (it != qcState.columnConfigs.end())
                {
                    VolumeViewState& vs = state.viewStates_[ci];
                    auto cmOpt = colourMapByName(it->second.colourMap);
                    if (cmOpt) vs.colourMap = *cmOpt;
                    if (it->second.valueMin) vs.valueRange[0] = *it->second.valueMin;
                    if (it->second.valueMax) vs.valueRange[1] = *it->second.valueMax;
                }
            }
            viewManager.initializeAllTextures();

            // Queue adjacent rows for prefetching (loaded incrementally each frame).
            if (prefetcher)
            {
                std::vector<std::string> prefetchPaths;
                int row = qcState.currentRowIndex;
                if (row > 0)
                {
                    const auto& prev = qcState.pathsForRow(row - 1);
                    prefetchPaths.insert(prefetchPaths.end(), prev.begin(), prev.end());
                }
                if (row + 1 < qcState.rowCount())
                {
                    const auto& next = qcState.pathsForRow(row + 1);
                    prefetchPaths.insert(prefetchPaths.end(), next.begin(), next.end());
                }
                if (!prefetchPaths.empty())
                    prefetcher->requestPrefetch(prefetchPaths);
            }
        }
        else if (!state.volumes_.empty())
        {
            state.initializeViewStates();
            state.applyConfig(mergedCfg, initW, initH);

            // CLI sync flags override config values.
            if (cliSyncCursor) state.syncCursors_ = true;
            if (cliSyncZoom)   state.syncZoom_ = true;
            if (cliSyncPan)    state.syncPan_ = true;

            // CLI LUT flags override config colour maps.
            for (size_t vi = 0; vi < args.perVolOpts.size() && vi < static_cast<size_t>(state.volumeCount()); ++vi)
            {
                if (args.perVolOpts[vi].colourMap.has_value())
                {
                    state.viewStates_[vi].colourMap = *args.perVolOpts[vi].colourMap;
                }
            }

            // CLI range flags: override value range.
            for (size_t vi = 0; vi < args.perVolOpts.size() && vi < static_cast<size_t>(state.volumeCount()); ++vi)
            {
                if (args.perVolOpts[vi].range.has_value())
                {
                    state.viewStates_[vi].valueRange = *args.perVolOpts[vi].range;
                }
            }

            // CLI label flags: mark volumes as label volumes and load LUTs.
            for (size_t vi = 0; vi < args.perVolOpts.size() && vi < static_cast<size_t>(state.volumeCount()); ++vi)
            {
                if (args.perVolOpts[vi].isLabel)
                {
                    state.volumes_[vi].setLabelVolume(true);
                    // Default to Viridis for label volumes unless an explicit LUT was given.
                    if (!args.perVolOpts[vi].colourMap.has_value())
                        state.viewStates_[vi].colourMap = ColourMapType::Viridis;
                }
                if (args.perVolOpts[vi].labelDescFile.has_value())
                {
                    state.volumes_[vi].loadLabelDescriptionFile(*args.perVolOpts[vi].labelDescFile);
                }
            }

            viewManager.initializeAllTextures();
        }

        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();

            // Incrementally load one prefetched volume per frame (main thread
            // only — libminc/HDF5 are not thread-safe).
            if (prefetcher)
                prefetcher->loadPending();

            // Handle deferred swapchain rebuild (triggered by framebuffer resize callback)
            if (windowManager.needsSwapchainRebuild())
            {
                int width, height;
                windowManager.getFramebufferSize(width, height);
                if (width > 0 && height > 0)
                {
                    backend->rebuildSwapchain(width, height);
                    windowManager.resetRebuildFlag();
                }
            }

            backend->imguiNewFrame();
            ImGui::NewFrame();

            interface.render(*backend);

            ImGui::Render();
            backend->endFrame();
        }

        backend->waitIdle();

        if (qcState.active)
            qcState.saveOutputCsv();

        viewManager.destroyAllTextures();
        backend->shutdownTextureSystem();

        backend->shutdownImGui();
        backend->shutdown();

        // Clear framebuffer callback before destroying window
        windowManager.clearCallback();

#ifdef HAS_WAYLAND_TOUCH
        WaylandTouch::shutdown();
#endif

        glfwDestroyWindow(window);
        glfwTerminate();

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Fatal error: " << e.what() << "\n";
        if (glfwInitialized)
            glfwTerminate();
        return 1;
    }
}
