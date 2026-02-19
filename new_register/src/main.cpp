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
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "AppConfig.h"
#include "AppState.h"
#include "ColourMap.h"
#include "GraphicsBackend.h"
#include "Interface.h"
#include "QCState.h"
#include "Volume.h"
#include "VulkanHelpers.h"
#include "ViewManager.h"

#include <glm/glm.hpp>

extern "C" {
#include "minc2-simple.h"
}

int main(int argc, char** argv)
{
    try
    {
        std::string cliConfigPath;
        std::vector<std::string> volumeFiles;
        std::vector<std::optional<std::string>> cliLutPerVolume;
        std::optional<std::string> pendingLut;
        std::string qcInputPath;
        std::string qcOutputPath;

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
                          << "  -h, --help            Show this help message\n"
                          << "      --lut <name>      Set colour map for the next volume\n"
                          << "  -r, --red             Set Red colour map for the next volume\n"
                          << "  -g, --green           Set Green colour map for the next volume\n"
                          << "  -b, --blue            Set Blue colour map for the next volume\n"
                          << "  -G, --gray            Set GrayScale colour map for the next volume\n"
                          << "  -H, --hot             Set HotMetal colour map for the next volume\n"
                          << "  -S, --spectral        Set Spectral colour map for the next volume\n"
                          << "      --qc <input.csv>  Enable QC mode with input CSV\n"
                          << "      --qc-output <out> Output CSV for QC verdicts (required with --qc)\n"
                          << "\nLUT flags apply to the next volume file on the command line.\n"
                          << "Example: new_register --gray vol1.mnc -r vol2.mnc\n";
                return 0;
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
            pendingLut.reset();
        }

        if (pendingLut.has_value())
        {
            std::cerr << "Warning: LUT flag at end of arguments has no volume to apply to\n";
        }

        if (!qcInputPath.empty() && qcOutputPath.empty())
        {
            std::cerr << "Error: --qc requires --qc-output <path>\n";
            return 1;
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

            for (size_t volIdx = 0; volIdx < state.volumeCount(); ++volIdx) {
                state.loadTagsForVolume(static_cast<int>(volIdx));
            }
        }
        else if (!qcState.active)
        {
            Volume vol;
            vol.generate_test_data();
            state.volumes_.push_back(std::move(vol));
            state.volumePaths_.push_back("");
            state.volumeNames_.push_back("Test Data");
        }

        if (!qcState.active && state.volumes_.empty())
        {
            std::cerr << "No volumes loaded.\n";
        }

        if (!glfwInit())
        {
            std::cerr << "Failed to initialize GLFW\n";
            return 1;
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

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

        GLFWwindow* window = glfwCreateWindow(initW, initH,
                                              "New Register (ImGui + Vulkan)",
                                              nullptr, nullptr);
        if (!window)
        {
            std::cerr << "Failed to create GLFW window.\n";
            glfwTerminate();
            return 1;
        }

        auto backend = GraphicsBackend::createDefault();
        backend->initialize(window);

        backend->initImGui(window);

        state.dpiScale_ = backend->contentScale();
        state.localConfigPath_ = localConfigPath;

        ViewManager viewManager(state);
        Interface interface(state, viewManager, qcState);

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

        if (qcState.active)
            qcState.saveOutputCsv();

        viewManager.destroyAllTextures();
        VulkanHelpers::Shutdown();

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
