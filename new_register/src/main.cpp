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
#include "ColourMap.h"
#include "GraphicsBackend.h"
#include "Interface.h"
#include "Prefetcher.h"
#include "QCState.h"
#include "Volume.h"
#include "ViewManager.h"
#include "WindowManager.h"

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

// ---------------------------------------------------------------------------
// CLI argument parsing (replaces cxxopts)
// ---------------------------------------------------------------------------

/// Per-volume display options.  Accumulated while walking argv and flushed
/// to the per-volume vector each time a positional volume file is seen.
struct PerVolOpts
{
    std::optional<ColourMapType> colourMap;
    std::optional<std::array<double, 2>> range;
    bool isLabel = false;
    std::optional<std::string> labelDescFile;
};

/// All parsed command-line arguments.
struct ParsedArgs
{
    bool help  = false;
    bool debug = false;
    bool test  = false;

    std::string configPath;
    std::string backendName;
    std::string tagsPath;
    std::string qcInputPath;
    std::string qcOutputPath;

    bool syncAll    = false;
    bool syncCursor = false;
    bool syncZoom   = false;
    bool syncPan    = false;

    std::optional<float> scaleFactor;

    std::vector<std::string> volumeFiles;
    std::vector<PerVolOpts>  perVolOpts;
};

/// Print usage / help text to stdout.
static void printUsage()
{
    std::cout <<
        "Usage: new_register [OPTIONS] [VOLUMES...]\n"
        "\n"
        "Medical imaging volume viewer.\n"
        "\n"
        "Volume display options (apply to the NEXT volume file):\n"
        "  -G, --gray           GrayScale colour map\n"
        "  -H, --hot            HotMetal colour map\n"
        "  -S, --spectral       Spectral colour map\n"
        "  -r, --red            Red colour map\n"
        "  -g, --green          Green colour map\n"
        "  -b, --blue           Blue colour map\n"
        "      --lut <name>     Named colour map (see list below)\n"
        "      --range <min,max>  Value range for next volume\n"
        "  -l, --label          Mark next volume as label volume\n"
        "  -L, --labels <file>  Label description file for next volume\n"
        "\n"
        "General:\n"
        "  -c, --config <path>  Load config from <path>\n"
        "  -B, --backend <name> Graphics backend: auto, vulkan, opengl2\n"
        "  -t, --tags <file>    Load combined two-volume .tag file\n"
        "  -d, --debug          Enable debug output\n"
        "  -h, --help           Show this help message\n"
        "      --test           Launch with a generated test volume\n"
        "      --scale <factor> Override screen content scale (HiDPI)\n"
        "\n"
        "QC mode:\n"
        "      --qc <csv>       Enable QC mode with input CSV\n"
        "      --qc-output <csv>  Output CSV for QC verdicts (required with --qc)\n"
        "\n"
        "Synchronization:\n"
        "      --sync           Synchronize all (cursor, zoom, pan)\n"
        "      --sync-cursor    Synchronize cursor position across volumes\n"
        "      --sync-zoom      Synchronize zoom level across volumes\n"
        "      --sync-pan       Synchronize pan position across volumes\n"
        "\n"
        "Backends:\n"
        "  vulkan   Vulkan (default where available, best performance)\n"
        "  opengl2  OpenGL 2.1 (legacy, works over SSH/X11)\n"
        "  auto     Auto-detect best available (default)\n"
        "\n";

    std::cout << "Available colour maps (for --lut):\n";
    for (int cm = 0; cm < colourMapCount(); ++cm)
        std::cout << "  " << colourMapName(static_cast<ColourMapType>(cm)) << "\n";

    std::cout << "\nLUT and range flags apply to the next volume file on the command line.\n"
              << "Example: new_register --gray --range 0,100 vol1.mnc -r vol2.mnc\n";
}

/// Require a value argument after a flag, printing an error and returning
/// false if we've run past the end of argv.
static bool requireValue(int i, int argc, const char* flag)
{
    if (i >= argc)
    {
        std::cerr << "Error: " << flag << " requires a value.\n";
        return false;
    }
    return true;
}

/// Parse all command-line arguments in a single pass.
/// Returns ParsedArgs on success.  On error, prints a message to stderr
/// and returns std::nullopt.
static std::optional<ParsedArgs> parseArgs(int argc, char** argv)
{
    ParsedArgs args;

    // Pending per-volume state, flushed when a positional arg is seen.
    std::optional<ColourMapType> pendingLut;
    bool pendingLabel = false;
    std::optional<std::string> pendingLabelDesc;
    std::optional<double> pendingMin, pendingMax;

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg = argv[i];

        // -- Boolean flags (no value) --

        if (arg == "-h" || arg == "--help")    { args.help = true;  continue; }
        if (arg == "-d" || arg == "--debug")   { args.debug = true; continue; }
        if (arg == "--test")                   { args.test = true;  continue; }

        if (arg == "--sync")        { args.syncAll = true;    continue; }
        if (arg == "--sync-cursor") { args.syncCursor = true; continue; }
        if (arg == "--sync-zoom")   { args.syncZoom = true;   continue; }
        if (arg == "--sync-pan")    { args.syncPan = true;    continue; }

        // -- Per-volume colour map shorthands (no value) --

        if (arg == "-G" || arg == "--gray")     { pendingLut = ColourMapType::GrayScale; continue; }
        if (arg == "-H" || arg == "--hot")      { pendingLut = ColourMapType::HotMetal;  continue; }
        if (arg == "-S" || arg == "--spectral") { pendingLut = ColourMapType::Spectral;  continue; }
        if (arg == "-r" || arg == "--red")      { pendingLut = ColourMapType::Red;       continue; }
        if (arg == "-g" || arg == "--green")    { pendingLut = ColourMapType::Green;     continue; }
        if (arg == "-b" || arg == "--blue")     { pendingLut = ColourMapType::Blue;      continue; }
        if (arg == "-l" || arg == "--label")    { pendingLabel = true;                   continue; }

        // -- Valued flags (consume next arg) --

        if (arg == "--lut")
        {
            ++i;
            if (!requireValue(i, argc, "--lut"))
                return std::nullopt;
            auto cmOpt = colourMapByName(argv[i]);
            if (cmOpt)
            {
                pendingLut = *cmOpt;
            }
            else
            {
                std::cerr << "Unknown colour map: " << argv[i] << "\n"
                          << "Available maps:";
                for (int cm = 0; cm < colourMapCount(); ++cm)
                    std::cerr << " " << colourMapName(static_cast<ColourMapType>(cm));
                std::cerr << "\n";
                return std::nullopt;
            }
            continue;
        }

        if (arg == "--range")
        {
            ++i;
            if (!requireValue(i, argc, "--range"))
                return std::nullopt;
            std::string rangeStr = argv[i];
            auto commaPos = rangeStr.find(',');
            if (commaPos == std::string::npos)
            {
                std::cerr << "Error: --range must be in format <min>,<max> (e.g., --range 0,100)\n";
                return std::nullopt;
            }
            pendingMin = std::stod(rangeStr.substr(0, commaPos));
            pendingMax = std::stod(rangeStr.substr(commaPos + 1));
            continue;
        }

        if (arg == "-L" || arg == "--labels")
        {
            ++i;
            if (!requireValue(i, argc, "--labels"))
                return std::nullopt;
            pendingLabelDesc = argv[i];
            continue;
        }

        if (arg == "-c" || arg == "--config")
        {
            ++i;
            if (!requireValue(i, argc, "--config"))
                return std::nullopt;
            args.configPath = argv[i];
            continue;
        }

        if (arg == "-B" || arg == "--backend")
        {
            ++i;
            if (!requireValue(i, argc, "--backend"))
                return std::nullopt;
            args.backendName = argv[i];
            continue;
        }

        if (arg == "-t" || arg == "--tags")
        {
            ++i;
            if (!requireValue(i, argc, "--tags"))
                return std::nullopt;
            args.tagsPath = argv[i];
            continue;
        }

        if (arg == "--qc")
        {
            ++i;
            if (!requireValue(i, argc, "--qc"))
                return std::nullopt;
            args.qcInputPath = argv[i];
            continue;
        }

        if (arg == "--qc-output")
        {
            ++i;
            if (!requireValue(i, argc, "--qc-output"))
                return std::nullopt;
            args.qcOutputPath = argv[i];
            continue;
        }

        if (arg == "--scale")
        {
            ++i;
            if (!requireValue(i, argc, "--scale"))
                return std::nullopt;
            args.scaleFactor = std::stof(argv[i]);
            continue;
        }

        // -- Unknown flag --

        if (arg.size() > 1 && arg[0] == '-')
        {
            std::cerr << "Error: unknown option: " << arg << "\n"
                      << "Run 'new_register --help' for usage.\n";
            return std::nullopt;
        }

        // -- Positional: volume file --
        // Flush any pending per-volume options.

        PerVolOpts pvo;
        if (pendingLut)
        {
            pvo.colourMap = *pendingLut;
            pendingLut.reset();
        }
        if (pendingLabel)
        {
            pvo.isLabel = true;
            pendingLabel = false;
        }
        if (pendingLabelDesc)
        {
            pvo.labelDescFile = *pendingLabelDesc;
            pendingLabelDesc.reset();
        }
        if (pendingMin && pendingMax)
        {
            pvo.range = std::array<double, 2>{*pendingMin, *pendingMax};
            pendingMin.reset();
            pendingMax.reset();
        }

        args.volumeFiles.push_back(std::string(arg));
        args.perVolOpts.push_back(std::move(pvo));
    }

    // Warn about unused pending per-volume state
    if (pendingLut.has_value())
        std::cerr << "Warning: LUT flag at end of arguments has no volume to apply to\n";
    if (pendingLabel)
        std::cerr << "Warning: --label flag at end of arguments has no volume to apply to\n";
    if (pendingLabelDesc)
        std::cerr << "Warning: --labels flag at end of arguments has no volume to apply to\n";
    if (pendingMin || pendingMax)
        std::cerr << "Warning: --range "
                  << (pendingMin ? std::to_string(*pendingMin) : "?")
                  << "," << (pendingMax ? std::to_string(*pendingMax) : "?")
                  << " at end of arguments has no volume to apply to\n";

    return args;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
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

        constexpr int colWidth  = 200;
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

        // Clear framebuffer callback before destroying window
        windowManager.clearCallback();

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
