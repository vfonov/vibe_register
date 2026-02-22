#include <imgui.h>
#include <imgui_internal.h>
#include <vector>
#include <iostream>
#include <string>
#include <algorithm>
#include <memory>
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
#include "ColourMap.h"
#include "GraphicsBackend.h"
#include "Interface.h"
#include "Prefetcher.h"
#include "QCState.h"
#include "Volume.h"
#include "ViewManager.h"

#include <glm/glm.hpp>

extern "C" {
#include "minc2-simple.h"
}

// ---------------------------------------------------------------------------
// GLFW error callback — prints diagnostic info to stderr
// ---------------------------------------------------------------------------
static void glfwErrorCallback(int error, const char* description)
{
    std::cerr << "[glfw] Error " << error << ": " << description << "\n";
}

int main(int argc, char** argv)
{
    try
    {
        std::string cliConfigPath;
        std::string cliBackendName;
        std::vector<std::string> volumeFiles;
        std::vector<std::optional<std::string>> cliLutPerVolume;
        std::optional<std::string> pendingLut;
        std::string qcInputPath;
        std::string qcOutputPath;
        std::string cliTagPath;
        bool useTestData = false;
        std::vector<bool> cliLabelVolumePerVolume;
        bool pendingLabelVolume = false;
        std::vector<std::optional<std::string>> cliLabelDescPerVolume;
        std::optional<std::string> pendingLabelDesc;

        static const std::array<std::pair<std::string_view, std::string_view>, 12> lutFlags = {{
            {"--gray",     "GrayScale"},
            {"--hot",      "HotMetal"},
            {"--spectral", "Spectral"},
            {"--red",      "Red"},
            {"--green",    "Green"},
            {"--blue",     "Blue"},
            {"-r",         "Red"},
            {"-b",         "Blue"},
            {"-g",         "Green"},
            {"-G",         "GrayScale"},
            {"-H",         "HotMetal"},
            {"-S",         "Spectral"},
        }};

        for (int i = 1; i < argc; ++i)
        {
            std::string_view arg = argv[i];

            if ((arg == "--config" || arg == "-c") && i + 1 < argc)
            {
                cliConfigPath = argv[++i];
                continue;
            }

            if (arg == "--help" || arg == "-h")
            {
                std::cerr << "Usage: new_register [options] [volume1.mnc ...]\n"
                          << "\nOptions:\n"
                          << "  -c, --config <path>   Load config from <path>\n"
                          << "  -B, --backend <name>  Graphics backend: auto, vulkan, opengl2 (default: auto)\n"
                          << "  -t, --tags <path>     Load combined two-volume .tag file\n"
                          << "  -h, --help            Show this help message\n"
                          << "      --test            Launch with a generated test volume\n"
                          << "      --lut <name>      Set colour map for the next volume\n"
                          << "  -r, --red             Set Red colour map for the next volume\n"
                          << "  -g, --green           Set Green colour map for the next volume\n"
                          << "  -b, --blue            Set Blue colour map for the next volume\n"
                          << "  -G, --gray            Set GrayScale colour map for the next volume\n"
                          << "  -H, --hot             Set HotMetal colour map for the next volume\n"
                          << "  -S, --spectral        Set Spectral colour map for the next volume\n"
                          << "  -l, --label           Mark next volume as label/segmentation volume\n"
                          << "  -L, --labels <file>   Load label description file for next volume\n"
                          << "      --qc <input.csv>  Enable QC mode with input CSV\n"
                          << "      --qc-output <out> Output CSV for QC verdicts (required with --qc)\n"
                          << "\nBackends:\n"
                          << "  vulkan   Vulkan (default where available, best performance)\n"
                          << "  opengl2  OpenGL 2.1 (legacy, works over SSH/X11)\n"
                          << "  auto     Auto-detect best available (default)\n"
                          << "\nLUT flags apply to the next volume file on the command line.\n"
                          << "Example: new_register --gray vol1.mnc -r vol2.mnc\n";
                return 0;
            }

            if (arg == "--test")
            {
                useTestData = true;
                continue;
            }

            if ((arg == "--backend" || arg == "-B") && i + 1 < argc)
            {
                cliBackendName = argv[++i];
                continue;
            }

            if (arg == "--lut" && i + 1 < argc)
            {
                std::string lutName = argv[++i];
                if (!colourMapByName(lutName).has_value())
                {
                    std::cerr << "Unknown colour map: " << lutName << "\n"
                              << "Available maps:";
                    for (int cm = 0; cm < colourMapCount(); ++cm)
                        std::cerr << " " << colourMapName(static_cast<ColourMapType>(cm));
                    std::cerr << "\n";
                    return 1;
                }
                pendingLut = std::move(lutName);
                continue;
            }

            if ((arg == "--tags" || arg == "-t") && i + 1 < argc)
            {
                cliTagPath = argv[++i];
                continue;
            }

            if (arg == "--label" || arg == "-l")
            {
                pendingLabelVolume = true;
                continue;
            }

            if ((arg == "--labels" || arg == "-L") && i + 1 < argc)
            {
                pendingLabelDesc = argv[++i];
                continue;
            }

            if (arg == "--qc" && i + 1 < argc)
            {
                qcInputPath = argv[++i];
                continue;
            }

            if (arg == "--qc-output" && i + 1 < argc)
            {
                qcOutputPath = argv[++i];
                continue;
            }

            bool isLutFlag = false;
            for (const auto& [flag, name] : lutFlags)
            {
                if (arg == flag)
                {
                    pendingLut = std::string(name);
                    isLutFlag = true;
                    break;
                }
            }
            if (isLutFlag) continue;

            volumeFiles.push_back(std::string(arg));
            cliLutPerVolume.push_back(pendingLut);
            cliLabelVolumePerVolume.push_back(pendingLabelVolume);
            cliLabelDescPerVolume.push_back(pendingLabelDesc);
            pendingLut.reset();
            pendingLabelVolume = false;
            pendingLabelDesc.reset();
        }

        if (pendingLut.has_value())
        {
            std::cerr << "Warning: LUT flag at end of arguments has no volume to apply to\n";
        }

        if (pendingLabelVolume)
        {
            std::cerr << "Warning: --label flag at end of arguments has no volume to apply to\n";
        }

        if (pendingLabelDesc)
        {
            std::cerr << "Warning: --labels flag at end of arguments has no volume to apply to\n";
        }

        if (!qcInputPath.empty() && qcOutputPath.empty())
        {
            std::cerr << "Error: --qc requires --qc-output <path>\n";
            return 1;
        }

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

        std::cerr << "[backend] Using: " << GraphicsBackend::backendName(backendType) << "\n";
        std::cerr << "[backend] Available:";
        for (auto b : GraphicsBackend::availableBackends())
            std::cerr << " " << GraphicsBackend::backendName(b);
        std::cerr << "\n";

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

        if (!glfwInit())
        {
            std::cerr << "Failed to initialize GLFW\n";
            return 1;
        }

        glfwSetErrorCallback(glfwErrorCallback);
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

        // Create backend before window so it can set appropriate GLFW hints
        auto backend = GraphicsBackend::create(backendType);
        backend->setWindowHints();

        float initScale = 1.0f;
        int monWorkX = 0, monWorkY = 0, monWorkW = 1280, monWorkH = 720;
        {
            float sx = 1.0f, sy = 1.0f;
            GLFWmonitor* primary = glfwGetPrimaryMonitor();
            if (primary)
            {
                glfwGetMonitorContentScale(primary, &sx, &sy);
                glfwGetMonitorWorkarea(primary, &monWorkX, &monWorkY,
                                      &monWorkW, &monWorkH);
            }
            initScale = (sx > sy) ? sx : sy;
            if (initScale < 1.0f) initScale = 1.0f;
        }

        int numVols = qcState.active ? qcState.columnCount() : state.volumeCount();
        if (numVols < 1) numVols = 1;

        constexpr int colWidth  = 200;
        constexpr int baseHeight = 480;

        int totalCols = numVols + (numVols > 1 ? 1 : 0);
        int initW = static_cast<int>(colWidth * totalCols * initScale);
        int initH = static_cast<int>(baseHeight * initScale);

        if (mergedCfg.global.windowWidth.has_value())
            initW = mergedCfg.global.windowWidth.value();
        if (mergedCfg.global.windowHeight.has_value())
            initH = mergedCfg.global.windowHeight.value();

        int maxW = static_cast<int>(monWorkW * 0.9f);
        int maxH = static_cast<int>(monWorkH * 0.9f);
        if (initW > maxW) initW = maxW;
        if (initH > maxH) initH = maxH;

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
                std::cerr << "[backend] " << GraphicsBackend::backendName(backendType)
                          << " init failed: " << e.what() << "\n";
            }
        }
        else
        {
            std::cerr << "[backend] " << GraphicsBackend::backendName(backendType)
                      << " failed to create window\n";
        }

        // If the chosen backend uses OpenGL and GLX failed, retry with EGL
        // before falling back to a completely different backend.  X2Go's
        // nxagent only provides GLX 1.2, but GLFW requires GLX 1.3; EGL
        // bypasses GLX entirely and works with Mesa's software renderer.
        if (!initialized && backendType == BackendType::OpenGL2)
        {
            std::cerr << "[backend] Retrying opengl2 with EGL context\n";
            if (window)
            {
                glfwDestroyWindow(window);
                window = nullptr;
            }
            backend = GraphicsBackend::create(BackendType::OpenGL2);
            backend->setWindowHints();
            glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
            glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
            windowTitle = "New Register (opengl2-egl)";
            window = glfwCreateWindow(initW, initH,
                windowTitle.c_str(), nullptr, nullptr);
            if (window)
            {
                try
                {
                    backend->initialize(window);
                    initialized = true;
                }
                catch (const std::exception& e)
                {
                    std::cerr << "[backend] opengl2-egl init failed: "
                              << e.what() << "\n";
                }
            }
            else
            {
                std::cerr << "[backend] opengl2-egl failed to create window\n";
            }
        }

        // Fallback: try every other compiled-in backend
        if (!initialized)
        {
            for (auto fallback : GraphicsBackend::availableBackends())
            {
                if (fallback == backendType)
                    continue;
                std::cerr << "[backend] Trying fallback: "
                          << GraphicsBackend::backendName(fallback) << "\n";
                try
                {
                    if (window)
                    {
                        glfwDestroyWindow(window);
                        window = nullptr;
                    }
                    backend = GraphicsBackend::create(fallback);
                    backend->setWindowHints();
                    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
                    windowTitle = std::string("New Register (") +
                        GraphicsBackend::backendName(fallback) + ")";
                    window = glfwCreateWindow(initW, initH,
                        windowTitle.c_str(), nullptr, nullptr);
                    if (!window)
                        continue;
                    backend->initialize(window);
                    backendType = fallback;
                    initialized = true;
                    break;
                }
                catch (const std::exception& e2)
                {
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

        backend->initImGui(window);

        state.dpiScale_ = backend->contentScale();
        state.localConfigPath_ = localConfigPath;

        ViewManager viewManager(state, *backend);
        Interface interface(state, viewManager, qcState);

        // Create background prefetcher for QC mode (optional).
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

            // Kick off initial prefetch of adjacent rows.
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

            // CLI LUT flags override config colour maps.
            for (size_t vi = 0; vi < cliLutPerVolume.size() && vi < static_cast<size_t>(state.volumeCount()); ++vi)
            {
                if (cliLutPerVolume[vi].has_value())
                {
                    auto cmOpt = colourMapByName(*cliLutPerVolume[vi]);
                    if (cmOpt)
                        state.viewStates_[vi].colourMap = *cmOpt;
                }
            }

            // CLI label flags: mark volumes as label volumes and load LUTs.
            for (size_t vi = 0; vi < cliLabelVolumePerVolume.size() && vi < static_cast<size_t>(state.volumeCount()); ++vi)
            {
                if (cliLabelVolumePerVolume[vi])
                {
                    state.volumes_[vi].setLabelVolume(true);
                }
                if (cliLabelDescPerVolume[vi].has_value())
                {
                    state.volumes_[vi].loadLabelDescriptionFile(*cliLabelDescPerVolume[vi]);
                }
            }

            viewManager.initializeAllTextures();
        }

        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();

            if (backend->needsSwapchainRebuild())
            {
                int width, height;
                glfwGetFramebufferSize(window, &width, &height);
                if (width > 0 && height > 0)
                {
                    backend->rebuildSwapchain(width, height);
                }
            }

            backend->imguiNewFrame();
            ImGui::NewFrame();

            interface.render(*backend, window);

            ImGui::Render();
            backend->endFrame();
        }

        backend->waitIdle();

        // Shut down prefetcher before destroying GPU resources.
        if (prefetcher)
            prefetcher->shutdown();

        if (qcState.active)
            qcState.saveOutputCsv();

        viewManager.destroyAllTextures();
        backend->shutdownTextureSystem();

        backend->shutdownImGui();
        backend->shutdown();

        glfwDestroyWindow(window);
        glfwTerminate();

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Fatal error: " << e.what() << "\n";
        glfwTerminate();
        return 1;
    }
}
