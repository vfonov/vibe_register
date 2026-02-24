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

#include "cxxopts.hpp"

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
    if (debugLoggingEnabled())
        std::cerr << "[glfw] Error " << error << ": " << description << "\n";
}

int main(int argc, char** argv)
{
    try
    {
        cxxopts::Options opts("new_register", "Medical imaging volume viewer");

        opts.add_options()
            ("c,config", "Load config from <path>", cxxopts::value<std::string>())
            ("B,backend", "Graphics backend: auto, vulkan, opengl2", cxxopts::value<std::string>())
            ("t,tags", "Load combined two-volume .tag file", cxxopts::value<std::string>())
            ("h,help", "Show this help message")
            ("d,debug", "Enable debug output")
            ("test", "Launch with a generated test volume")
            ("lut", "Set colour map for the next volume", cxxopts::value<std::string>())
            ("r,red", "Set Red colour map for the next volume")
            ("g,green", "Set Green colour map for the next volume")
            ("b,blue", "Set Blue colour map for the next volume")
            ("G,gray", "Set GrayScale colour map for the next volume")
            ("H,hot", "Set HotMetal colour map for the next volume")
            ("S,spectral", "Set Spectral colour map for the next volume")
            ("l,label", "Mark next volume as label/segmentation volume")
            ("L,labels", "Load label description file for next volume", cxxopts::value<std::string>())
            ("range", "Set value range for next volume as <min>,<max> (e.g., --range 0,100)", cxxopts::value<std::string>())
            ("qc", "Enable QC mode with input CSV", cxxopts::value<std::string>())
            ("qc-output", "Output CSV for QC verdicts (required with --qc)", cxxopts::value<std::string>())
            ("positional", "Volume files", cxxopts::value<std::vector<std::string>>());

        opts.parse_positional({"positional"});

        auto result = opts.parse(argc, argv);

        if (result.count("help"))
        {
            std::cout << opts.help() << "\n"
                      << "Backends:\n"
                      << "  vulkan   Vulkan (default where available, best performance)\n"
                      << "  opengl2  OpenGL 2.1 (legacy, works over SSH/X11)\n"
                      << "  auto     Auto-detect best available (default)\n"
                      << "\nLUT and range flags apply to the next volume file on the command line.\n"
                      << "Example: new_register --gray --range 0,100 vol1.mnc -r vol2.mnc\n";
            return 0;
        }

        std::string cliConfigPath;
        if (result.count("config"))
            cliConfigPath = result["config"].as<std::string>();

        std::string cliBackendName;
        if (result.count("backend"))
            cliBackendName = result["backend"].as<std::string>();

        std::string cliTagPath;
        if (result.count("tags"))
            cliTagPath = result["tags"].as<std::string>();

        bool useTestData = result.count("test") > 0;
        debugLoggingEnabled().store(result.count("debug") > 0);

        std::string qcInputPath;
        if (result.count("qc"))
            qcInputPath = result["qc"].as<std::string>();

        std::string qcOutputPath;
        if (result.count("qc-output"))
            qcOutputPath = result["qc-output"].as<std::string>();

        if (!qcInputPath.empty() && qcOutputPath.empty())
        {
            std::cerr << "Error: --qc requires --qc-output <path>\n";
            return 1;
        }

        std::vector<std::string> volumeFiles;
        std::vector<std::optional<std::string>> cliLutPerVolume;
        std::vector<bool> cliLabelVolumePerVolume;
        std::vector<std::optional<std::string>> cliLabelDescPerVolume;
        std::vector<std::optional<std::array<double, 2>>> cliRangePerVolume;

        std::optional<std::string> pendingLut;
        bool pendingLabelVolume = false;
        std::optional<std::string> pendingLabelDesc;
        std::optional<double> pendingMin;
        std::optional<double> pendingMax;

        if (result.count("positional"))
            volumeFiles = result["positional"].as<std::vector<std::string>>();

        for (size_t i = 0; i < volumeFiles.size(); ++i)
        {
            cliLutPerVolume.push_back(std::nullopt);
            cliLabelVolumePerVolume.push_back(false);
            cliLabelDescPerVolume.push_back(std::nullopt);
            cliRangePerVolume.push_back(std::nullopt);
        }

        for (int i = 1; i < argc; ++i)
        {
            std::string_view arg = argv[i];
            bool isLutFlag = false;

            if (arg == "--lut" || arg == "-lut")
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

            if (arg == "-r" || arg == "--red" || arg == "-g" || arg == "--green" ||
                arg == "-b" || arg == "--blue" || arg == "-G" || arg == "--gray" ||
                arg == "-H" || arg == "--hot" || arg == "-S" || arg == "--spectral")
            {
                std::string lutName;
                if (arg == "-r" || arg == "--red") lutName = "Red";
                else if (arg == "-g" || arg == "--green") lutName = "Green";
                else if (arg == "-b" || arg == "--blue") lutName = "Blue";
                else if (arg == "-G" || arg == "--gray") lutName = "GrayScale";
                else if (arg == "-H" || arg == "--hot") lutName = "HotMetal";
                else if (arg == "-S" || arg == "--spectral") lutName = "Spectral";
                pendingLut = lutName;
                isLutFlag = true;
            }

            if (arg == "-l" || arg == "--label")
            {
                pendingLabelVolume = true;
                continue;
            }

            if (arg == "-L" || arg == "--labels")
            {
                pendingLabelDesc = argv[++i];
                continue;
            }

            if (arg == "--range")
            {
                std::string rangeStr = argv[++i];
                auto commaPos = rangeStr.find(',');
                if (commaPos == std::string::npos)
                {
                    std::cerr << "Error: --range must be in format <min>,<max> (e.g., --range 0,100)\n";
                    return 1;
                }
                pendingMin = std::stod(rangeStr.substr(0, commaPos));
                pendingMax = std::stod(rangeStr.substr(commaPos + 1));
                continue;
            }

            if (isLutFlag || pendingLabelVolume || pendingLabelDesc || pendingMin || pendingMax)
            {
                for (size_t vi = 0; vi < volumeFiles.size(); ++vi)
                {
                    if (cliLutPerVolume[vi].has_value()) continue;
                    if (pendingLut)
                    {
                        cliLutPerVolume[vi] = pendingLut;
                        pendingLut.reset();
                    }
                    if (pendingLabelVolume)
                    {
                        cliLabelVolumePerVolume[vi] = true;
                        pendingLabelVolume = false;
                    }
                    if (pendingLabelDesc)
                    {
                        cliLabelDescPerVolume[vi] = pendingLabelDesc;
                        pendingLabelDesc.reset();
                    }
                    if (pendingMin || pendingMax)
                    {
                        if (!cliRangePerVolume[vi].has_value())
                            cliRangePerVolume[vi] = std::array<double, 2>{0.0, 1.0};
                        if (pendingMin)
                        {
                            (*cliRangePerVolume[vi])[0] = *pendingMin;
                            pendingMin.reset();
                        }
                        if (pendingMax)
                        {
                            (*cliRangePerVolume[vi])[1] = *pendingMax;
                            pendingMax.reset();
                        }
                    }
                    break;
                }
            }
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

        if (pendingMin || pendingMax)
        {
            std::cerr << "Warning: --range " << (pendingMin ? std::to_string(*pendingMin) : "?") 
                      << "," << (pendingMax ? std::to_string(*pendingMax) : "?")
                      << " at end of arguments has no volume to apply to\n";
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
                if (debugLoggingEnabled())
                    std::cerr << "[backend] " << GraphicsBackend::backendName(backendType)
                              << " init failed: " << e.what() << "\n";
            }
        }
        else
        {
            if (debugLoggingEnabled())
                std::cerr << "[backend] " << GraphicsBackend::backendName(backendType)
                          << " failed to create window\n";
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
                    if (debugLoggingEnabled())
                        std::cerr << "[backend] opengl2-egl init failed: "
                                  << e.what() << "\n";
                }
            }
            else
            {
                if (debugLoggingEnabled())
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
                if (debugLoggingEnabled())
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

        backend->initImGui(window);

        state.dpiScale_ = backend->contentScale();
        state.localConfigPath_ = localConfigPath;

        ViewManager viewManager(state, *backend);
        Interface interface(state, viewManager, qcState);

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

            // CLI range flags: override value range.
            for (size_t vi = 0; vi < cliRangePerVolume.size() && vi < static_cast<size_t>(state.volumeCount()); ++vi)
            {
                if (cliRangePerVolume[vi].has_value())
                {
                    state.viewStates_[vi].valueRange = *cliRangePerVolume[vi];
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

            // Incrementally load one prefetched volume per frame (main thread
            // only — libminc/HDF5 are not thread-safe).
            if (prefetcher)
                prefetcher->loadPending();

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
