/// new_mincpik — headless mosaic image generator for MINC2 volumes.
///
/// Loads one or more MINC2 volumes, renders slices using the same CPU
/// compositing algorithm as new_register's overlay panel, arranges them
/// in a grid (rows = planes, columns = slices), and writes a PNG file.
///
/// Zero GPU / X11 / Wayland / GLFW / ImGui dependencies.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "stb_easy_font.h"

#include "AppConfig.h"
#include "ColourMap.h"
#include "SliceRenderer.h"
#include "Transform.h"
#include "Volume.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Parse a comma-separated list of doubles.
static std::vector<double> parseDoubleList(const std::string& str)
{
    std::vector<double> out;
    std::istringstream ss(str);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        if (!token.empty())
            out.push_back(std::stod(token));
    }
    return out;
}

/// Parse a comma-separated list of floats.
static std::vector<float> parseFloatList(const std::string& str)
{
    std::vector<float> out;
    std::istringstream ss(str);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        if (!token.empty())
            out.push_back(std::stof(token));
    }
    return out;
}

/// Convert world coordinate to voxel index along a given axis.
/// viewIndex: 0=axial(Z), 1=sagittal(X), 2=coronal(Y).
static int worldToSliceVoxel(const Volume& vol, int viewIndex, double worldCoord)
{
    // Build a world point with the coordinate on the relevant axis
    glm::dvec3 worldPt(0.0, 0.0, 0.0);
    if (viewIndex == 0)
        worldPt.z = worldCoord;
    else if (viewIndex == 1)
        worldPt.x = worldCoord;
    else
        worldPt.y = worldCoord;

    glm::ivec3 voxel;
    vol.transformWorldToVoxel(worldPt, voxel);

    int maxDim;
    int idx;
    if (viewIndex == 0)
    {
        idx = voxel.z;
        maxDim = vol.dimensions.z;
    }
    else if (viewIndex == 1)
    {
        idx = voxel.x;
        maxDim = vol.dimensions.x;
    }
    else
    {
        idx = voxel.y;
        maxDim = vol.dimensions.y;
    }
    return std::clamp(idx, 0, maxDim - 1);
}

/// Compute evenly spaced voxel indices for a given number of slices.
/// Distributes N slices evenly across the volume extent, avoiding the
/// very first and last slices (which are typically blank).
static std::vector<int> evenlySpacedSlices(const Volume& vol, int viewIndex, int count)
{
    int maxDim;
    if (viewIndex == 0)
        maxDim = vol.dimensions.z;
    else if (viewIndex == 1)
        maxDim = vol.dimensions.x;
    else
        maxDim = vol.dimensions.y;

    std::vector<int> result;
    if (count <= 0)
        return result;
    if (count == 1)
    {
        result.push_back(maxDim / 2);
        return result;
    }

    // Skip 10% at each edge to avoid blank slices
    int lo = std::max(1, static_cast<int>(maxDim * 0.1));
    int hi = std::min(maxDim - 2, static_cast<int>(maxDim * 0.9));
    if (hi <= lo)
    {
        lo = 0;
        hi = maxDim - 1;
    }
    double step = (count > 1) ? static_cast<double>(hi - lo) / (count - 1) : 0.0;
    for (int i = 0; i < count; ++i)
        result.push_back(lo + static_cast<int>(std::round(i * step)));

    return result;
}

// ---------------------------------------------------------------------------
// Aspect-ratio correction
// ---------------------------------------------------------------------------

/// Determine which two volume axes correspond to the in-plane (U, V)
/// directions for a given view.
///   viewIndex 0 (axial):    U=X(0), V=Y(1)
///   viewIndex 1 (sagittal): U=Y(1), V=Z(2)
///   viewIndex 2 (coronal):  U=X(0), V=Z(2)
static void viewAxes(int viewIndex, int& axisU, int& axisV)
{
    if (viewIndex == 0)      { axisU = 0; axisV = 1; }
    else if (viewIndex == 1) { axisU = 1; axisV = 2; }
    else                     { axisU = 0; axisV = 2; }
}

/// Resample a rendered slice so that its pixel dimensions reflect the
/// physical (world-space) voxel spacing at the finest resolution present
/// in the volume.  When voxels are non-uniform (e.g. 3 mm X, 1 mm Y,
/// 2 mm Z), the raw pixel buffer has an incorrect aspect ratio *and*
/// views involving coarse axes appear smaller than views with fine axes.
///
/// By using the smallest step across all three volume axes as the
/// target pixel size, every axis is scaled to the same resolution and
/// all slice planes have consistent visual sizes — matching
/// new_register's display.
///
/// @param slice       The raw rendered slice (1 voxel = 1 pixel).
/// @param vol         The reference volume (provides step sizes).
/// @param viewIndex   0=axial, 1=sagittal, 2=coronal.
/// @return A new RenderedSlice with corrected dimensions (or the original
///         if no correction is needed).
static RenderedSlice resampleToPhysicalAspect(
    const RenderedSlice& slice,
    const Volume& vol,
    int viewIndex)
{
    int axisU, axisV;
    viewAxes(viewIndex, axisU, axisV);

    double stepU = std::abs(vol.step[axisU]);
    double stepV = std::abs(vol.step[axisV]);
    if (stepU < 1e-12 || stepV < 1e-12)
        return slice;

    // Use the finest voxel step across all three axes as the target
    // pixel size.  This ensures that all slice planes are rendered at
    // the same resolution and have consistent visual sizes.
    double minStep = std::min({std::abs(vol.step[0]),
                               std::abs(vol.step[1]),
                               std::abs(vol.step[2])});
    if (minStep < 1e-12)
        minStep = std::min(stepU, stepV);

    // Scale each axis: nVoxels * (voxelSize / targetPixelSize)
    int outW = static_cast<int>(std::round(slice.width  * (stepU / minStep)));
    int outH = static_cast<int>(std::round(slice.height * (stepV / minStep)));

    if (outW < 1) outW = 1;
    if (outH < 1) outH = 1;
    if (outW == slice.width && outH == slice.height)
        return slice;

    RenderedSlice out;
    out.width  = outW;
    out.height = outH;
    out.pixels.resize(outW * outH);

    double scaleX = static_cast<double>(slice.width)  / outW;
    double scaleY = static_cast<double>(slice.height) / outH;

    for (int y = 0; y < outH; ++y)
    {
        int srcY = static_cast<int>(y * scaleY);
        if (srcY >= slice.height) srcY = slice.height - 1;
        for (int x = 0; x < outW; ++x)
        {
            int srcX = static_cast<int>(x * scaleX);
            if (srcX >= slice.width) srcX = slice.width - 1;
            out.pixels[y * outW + x] = slice.pixels[srcY * slice.width + srcX];
        }
    }

    return out;
}

// ---------------------------------------------------------------------------
// Mosaic assembly
// ---------------------------------------------------------------------------

/// Blit a RenderedSlice into a larger pixel buffer at (destX, destY).
static void blitSlice(
    const RenderedSlice& slice,
    std::vector<uint32_t>& dest,
    int destWidth,
    int destX,
    int destY)
{
    for (int y = 0; y < slice.height; ++y)
    {
        int srcOff = y * slice.width;
        int dstOff = (destY + y) * destWidth + destX;
        std::memcpy(&dest[dstOff], &slice.pixels[srcOff],
                     slice.width * sizeof(uint32_t));
    }
}

// ---------------------------------------------------------------------------
// Title text rendering
// ---------------------------------------------------------------------------

/// Parse a hex digit (0-9, a-f, A-F).  Returns -1 on invalid input.
static int hexDigit(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
    if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
    return -1;
}

/// Parse a foreground colour from a string.
/// Supports:
///   - Hex: #RRGGBB, #RGB, RRGGBB, RGB (with or without '#' prefix)
///   - Named colours: white, black, red, green, blue, yellow, cyan, magenta,
///                    gray/grey, orange
/// Returns packed 0xAABBGGRR (little-endian RGBA) matching the mosaic format.
/// Returns 0xFFFFFFFF (white) on parse failure.
static uint32_t parseFgColour(const std::string& str)
{
    auto pack = [](uint8_t r, uint8_t g, uint8_t b) -> uint32_t
    {
        return static_cast<uint32_t>(r)
             | (static_cast<uint32_t>(g) << 8)
             | (static_cast<uint32_t>(b) << 16)
             | (0xFFu << 24);
    };

    // Named colours (case-insensitive)
    std::string lower = str;
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (lower == "white")                    return pack(255, 255, 255);
    if (lower == "black")                    return pack(0, 0, 0);
    if (lower == "red")                      return pack(255, 0, 0);
    if (lower == "green")                    return pack(0, 255, 0);
    if (lower == "blue")                     return pack(0, 0, 255);
    if (lower == "yellow")                   return pack(255, 255, 0);
    if (lower == "cyan")                     return pack(0, 255, 255);
    if (lower == "magenta")                  return pack(255, 0, 255);
    if (lower == "gray" || lower == "grey")  return pack(128, 128, 128);
    if (lower == "orange")                   return pack(255, 165, 0);

    // Hex parsing
    std::string_view hex = str;
    if (!hex.empty() && hex[0] == '#')
        hex.remove_prefix(1);

    if (hex.size() == 6)
    {
        int r1 = hexDigit(hex[0]), r2 = hexDigit(hex[1]);
        int g1 = hexDigit(hex[2]), g2 = hexDigit(hex[3]);
        int b1 = hexDigit(hex[4]), b2 = hexDigit(hex[5]);
        if (r1 >= 0 && r2 >= 0 && g1 >= 0 && g2 >= 0 && b1 >= 0 && b2 >= 0)
        {
            return pack(static_cast<uint8_t>(r1 * 16 + r2),
                        static_cast<uint8_t>(g1 * 16 + g2),
                        static_cast<uint8_t>(b1 * 16 + b2));
        }
    }
    else if (hex.size() == 3)
    {
        int r = hexDigit(hex[0]);
        int g = hexDigit(hex[1]);
        int b = hexDigit(hex[2]);
        if (r >= 0 && g >= 0 && b >= 0)
        {
            return pack(static_cast<uint8_t>(r * 17),
                        static_cast<uint8_t>(g * 17),
                        static_cast<uint8_t>(b * 17));
        }
    }

    std::cerr << "Warning: could not parse colour '" << str
              << "', defaulting to white.\n";
    return pack(255, 255, 255);
}

/// Render a line of text into a RenderedSlice using stb_easy_font.
///
/// 1. Calls stb_easy_font_print() to generate axis-aligned quads at the
///    native 12px line height.
/// 2. Rasterizes those quads into a small pixel buffer (each quad is a
///    filled axis-aligned rectangle).
/// 3. Scales the buffer up by @p scale using nearest-neighbour.
///
/// @param text       The ASCII text to render.
/// @param fgColour   Packed 0xAABBGGRR foreground colour.
/// @param scale      Integer scale factor (1 = native 12px height).
/// @return A RenderedSlice containing the rasterized text on a transparent
///         background (alpha = 0 where no glyph).
static RenderedSlice renderTextRow(
    const std::string& text,
    uint32_t fgColour,
    int scale)
{
    RenderedSlice result;
    if (text.empty() || scale < 1)
        return result;

    // Measure text at native size
    // stb_easy_font functions take char* (not const), so cast away const.
    int nativeW = stb_easy_font_width(const_cast<char*>(text.c_str()));
    int nativeH = stb_easy_font_height(const_cast<char*>(text.c_str()));
    if (nativeW <= 0 || nativeH <= 0)
        return result;

    // Add 1px padding on each side to avoid clipping
    nativeW += 2;
    nativeH += 2;

    // Generate quads
    // Budget ~270 bytes per character; allocate generously.
    std::vector<char> vbuf(text.size() * 300 + 1024);
    unsigned char color[4] = {255, 255, 255, 255};  // placeholder; we use fgColour when writing pixels
    int numQuads = stb_easy_font_print(
        1.0f, 1.0f,  // 1px offset for padding
        const_cast<char*>(text.c_str()),
        color,
        vbuf.data(),
        static_cast<int>(vbuf.size()));

    if (numQuads <= 0)
        return result;

    // Rasterize quads into a native-size buffer.
    // Each quad = 4 vertices, each vertex = 16 bytes: {float x, float y, float z, uint8[4] color}
    // Quads are axis-aligned rectangles — just find min/max x/y and fill.
    std::vector<uint32_t> nativeBuf(nativeW * nativeH, 0x00000000);  // transparent

    for (int q = 0; q < numQuads; ++q)
    {
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        for (int v = 0; v < 4; ++v)
        {
            int off = (q * 4 + v) * 16;
            float vx, vy;
            std::memcpy(&vx, &vbuf[off + 0], sizeof(float));
            std::memcpy(&vy, &vbuf[off + 4], sizeof(float));
            if (vx < minX) minX = vx;
            if (vx > maxX) maxX = vx;
            if (vy < minY) minY = vy;
            if (vy > maxY) maxY = vy;
        }

        int x0 = static_cast<int>(std::floor(minX));
        int y0 = static_cast<int>(std::floor(minY));
        int x1 = static_cast<int>(std::ceil(maxX));
        int y1 = static_cast<int>(std::ceil(maxY));

        x0 = std::clamp(x0, 0, nativeW);
        x1 = std::clamp(x1, 0, nativeW);
        y0 = std::clamp(y0, 0, nativeH);
        y1 = std::clamp(y1, 0, nativeH);

        for (int py = y0; py < y1; ++py)
            for (int px = x0; px < x1; ++px)
                nativeBuf[py * nativeW + px] = fgColour;
    }

    // Scale up by nearest-neighbour
    int outW = nativeW * scale;
    int outH = nativeH * scale;

    result.width  = outW;
    result.height = outH;
    result.pixels.resize(outW * outH);

    for (int y = 0; y < outH; ++y)
    {
        int srcY = y / scale;
        for (int x = 0; x < outW; ++x)
        {
            int srcX = x / scale;
            result.pixels[y * outW + x] = nativeBuf[srcY * nativeW + srcX];
        }
    }

    return result;
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
    // Global flags
    bool help  = false;
    bool debug = false;

    // Output
    std::string outputPath;
    std::optional<int> width;
    int gap = 2;

    // Slice counts
    int nAxial    = 1;
    int nSagittal = 1;
    int nCoronal  = 1;

    // Slice-at world coordinates
    std::string axialAt;
    std::string sagittalAt;
    std::string coronalAt;

    // Layout
    int rows = 1;

    // Config / transform
    std::string configPath;
    std::string tagsPath;
    std::string xfmPath;

    // Per-volume alpha
    std::string alphaStr;

    // Title annotation
    std::string title;
    std::string fgColourStr = "white";
    std::optional<int> fontScale;

    // Volumes and their per-volume options
    std::vector<std::string> volumeFiles;
    std::vector<PerVolOpts>  perVolOpts;
};

/// Print usage / help text to stdout.
static void printUsage()
{
    std::cout <<
        "Usage: new_mincpik [OPTIONS] [VOLUMES...]\n"
        "\n"
        "Headless mosaic image generator for MINC2 volumes.\n"
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
        "Slice selection:\n"
        "      --axial <N>      Number of axial slices (default: 1)\n"
        "      --sagittal <N>   Number of sagittal slices (default: 1)\n"
        "      --coronal <N>    Number of coronal slices (default: 1)\n"
        "      --axial-at <Z,...>    World Z coordinates (comma-separated)\n"
        "      --sagittal-at <X,...> World X coordinates (comma-separated)\n"
        "      --coronal-at <Y,...>  World Y coordinates (comma-separated)\n"
        "\n"
        "Layout:\n"
        "      --rows <N>       Arrange each plane's slices across N rows\n"
        "\n"
        "Output:\n"
        "  -o, --output <path>  Output PNG path (required)\n"
        "      --width <px>     Scale output to this width in pixels\n"
        "      --gap <px>       Cell gap in pixels (default: 2)\n"
        "\n"
        "Annotation:\n"
        "      --title <text>   Title text rendered above the mosaic\n"
        "      --fg <colour>    Foreground colour for title (default: white)\n"
        "                       Hex: #RRGGBB, #RGB, RRGGBB, RGB\n"
        "                       Named: white, black, red, green, blue,\n"
        "                              yellow, cyan, magenta, gray, orange\n"
        "      --font-scale <N> Integer scale for 12px font (default: auto)\n"
        "\n"
        "Transform:\n"
        "  -t, --tags <file>    Load .tag file for registration\n"
        "      --xfm <file>     Load .xfm linear transform file\n"
        "\n"
        "Misc:\n"
        "  -c, --config <file>  Load config.json\n"
        "      --alpha <a,...>  Per-volume overlay alpha (comma-separated)\n"
        "  -d, --debug          Enable debug output\n"
        "  -h, --help           Show this help message\n"
        "\n";

    std::cout << "Available colour maps (for --lut):\n";
    for (int cm = 0; cm < colourMapCount(); ++cm)
        std::cout << "  " << colourMapName(static_cast<ColourMapType>(cm)) << "\n";

    std::cout << "\nExamples:\n"
              << "  new_mincpik --gray vol1.mnc -r vol2.mnc --coronal 5 -o mosaic.png\n"
              << "  new_mincpik vol.mnc --coronal 12 --rows 3 -o mosaic.png\n";
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

        if (arg == "-h" || arg == "--help")
        {
            args.help = true;
            continue;
        }
        if (arg == "-d" || arg == "--debug")
        {
            args.debug = true;
            continue;
        }

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
            auto vals = parseDoubleList(argv[i]);
            if (vals.size() >= 2)
            {
                pendingMin = vals[0];
                pendingMax = vals[1];
            }
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

        if (arg == "-o" || arg == "--output")
        {
            ++i;
            if (!requireValue(i, argc, "--output"))
                return std::nullopt;
            args.outputPath = argv[i];
            continue;
        }

        if (arg == "--width")
        {
            ++i;
            if (!requireValue(i, argc, "--width"))
                return std::nullopt;
            args.width = std::stoi(argv[i]);
            continue;
        }

        if (arg == "--gap")
        {
            ++i;
            if (!requireValue(i, argc, "--gap"))
                return std::nullopt;
            args.gap = std::stoi(argv[i]);
            continue;
        }

        if (arg == "--axial")
        {
            ++i;
            if (!requireValue(i, argc, "--axial"))
                return std::nullopt;
            args.nAxial = std::stoi(argv[i]);
            continue;
        }

        if (arg == "--sagittal")
        {
            ++i;
            if (!requireValue(i, argc, "--sagittal"))
                return std::nullopt;
            args.nSagittal = std::stoi(argv[i]);
            continue;
        }

        if (arg == "--coronal")
        {
            ++i;
            if (!requireValue(i, argc, "--coronal"))
                return std::nullopt;
            args.nCoronal = std::stoi(argv[i]);
            continue;
        }

        if (arg == "--axial-at")
        {
            ++i;
            if (!requireValue(i, argc, "--axial-at"))
                return std::nullopt;
            args.axialAt = argv[i];
            continue;
        }

        if (arg == "--sagittal-at")
        {
            ++i;
            if (!requireValue(i, argc, "--sagittal-at"))
                return std::nullopt;
            args.sagittalAt = argv[i];
            continue;
        }

        if (arg == "--coronal-at")
        {
            ++i;
            if (!requireValue(i, argc, "--coronal-at"))
                return std::nullopt;
            args.coronalAt = argv[i];
            continue;
        }

        if (arg == "--rows")
        {
            ++i;
            if (!requireValue(i, argc, "--rows"))
                return std::nullopt;
            args.rows = std::stoi(argv[i]);
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

        if (arg == "--xfm")
        {
            ++i;
            if (!requireValue(i, argc, "--xfm"))
                return std::nullopt;
            args.xfmPath = argv[i];
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

        if (arg == "--alpha")
        {
            ++i;
            if (!requireValue(i, argc, "--alpha"))
                return std::nullopt;
            args.alphaStr = argv[i];
            continue;
        }

        if (arg == "--title")
        {
            ++i;
            if (!requireValue(i, argc, "--title"))
                return std::nullopt;
            args.title = argv[i];
            continue;
        }

        if (arg == "--fg")
        {
            ++i;
            if (!requireValue(i, argc, "--fg"))
                return std::nullopt;
            args.fgColourStr = argv[i];
            continue;
        }

        if (arg == "--font-scale")
        {
            ++i;
            if (!requireValue(i, argc, "--font-scale"))
                return std::nullopt;
            args.fontScale = std::stoi(argv[i]);
            continue;
        }

        // -- Unknown flag --

        if (arg.size() > 1 && arg[0] == '-')
        {
            std::cerr << "Error: unknown option: " << arg << "\n"
                      << "Run 'new_mincpik --help' for usage.\n";
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

        bool debug = args.debug;

        if (args.outputPath.empty())
        {
            std::cerr << "Error: --output (-o) is required.\n"
                      << "Run 'new_mincpik --help' for usage.\n";
            return 1;
        }

        if (args.volumeFiles.empty())
        {
            std::cerr << "Error: no volume files specified.\n"
                      << "Run 'new_mincpik --help' for usage.\n";
            return 1;
        }

        // --- Load volumes ---
        std::vector<Volume> volumes;
        volumes.reserve(args.volumeFiles.size());

        for (size_t i = 0; i < args.volumeFiles.size(); ++i)
        {
            Volume vol;
            if (debug)
                std::cerr << "[mincpik] Loading " << args.volumeFiles[i] << "...\n";
            vol.load(args.volumeFiles[i]);

            if (args.perVolOpts[i].isLabel)
                vol.setLabelVolume(true);
            if (args.perVolOpts[i].labelDescFile)
                vol.loadLabelDescriptionFile(*args.perVolOpts[i].labelDescFile);

            volumes.push_back(std::move(vol));
        }

        // --- Build per-volume render params ---
        std::vector<VolumeRenderParams> params(volumes.size());

        // Per-volume alpha from --alpha flag
        std::vector<float> alphas;
        if (!args.alphaStr.empty())
            alphas = parseFloatList(args.alphaStr);

        for (size_t i = 0; i < volumes.size(); ++i)
        {
            params[i].valueMin = volumes[i].min_value;
            params[i].valueMax = volumes[i].max_value;
            params[i].colourMap = ColourMapType::GrayScale;
            params[i].overlayAlpha = 1.0f;

            if (args.perVolOpts[i].colourMap)
                params[i].colourMap = *args.perVolOpts[i].colourMap;
            else if (args.perVolOpts[i].isLabel)
                params[i].colourMap = ColourMapType::Viridis;
            if (args.perVolOpts[i].range)
            {
                params[i].valueMin = (*args.perVolOpts[i].range)[0];
                params[i].valueMax = (*args.perVolOpts[i].range)[1];
                // Below-range voxels should be transparent, not clamped to the
                // lowest LUT colour.  This matches new_register's behaviour.
                params[i].underColourMode = kSliceClampTransparent;
                params[i].overColourMode  = kSliceClampTransparent;
            }
            if (i < alphas.size())
                params[i].overlayAlpha = alphas[i];
        }

        // --- Config overrides ---
        if (!args.configPath.empty())
        {
            try
            {
                AppConfig cfg = loadConfig(args.configPath);
                for (size_t i = 0; i < cfg.volumes.size() && i < volumes.size(); ++i)
                {
                    // Config provides defaults; CLI flags above override
                    if (!args.perVolOpts[i].colourMap)
                    {
                        auto cmOpt = colourMapByName(cfg.volumes[i].colourMap);
                        if (cmOpt)
                            params[i].colourMap = *cmOpt;
                    }
                    if (!args.perVolOpts[i].range)
                    {
                        if (cfg.volumes[i].valueMin)
                            params[i].valueMin = *cfg.volumes[i].valueMin;
                        if (cfg.volumes[i].valueMax)
                            params[i].valueMax = *cfg.volumes[i].valueMax;
                    }
                }
            }
            catch (const std::exception& e)
            {
                std::cerr << "Warning: config load failed: " << e.what() << "\n";
            }
        }

        // --- Transform (optional) ---
        TransformResult xfmResult;
        if (!args.tagsPath.empty() && volumes.size() >= 2)
        {
            // Load tag file, compute transform
            TagWrapper tags;
            tags.load(args.tagsPath);
            if (tags.tagCount() >= kMinPointsLinear)
            {
                auto vol1Tags = tags.points();
                auto vol2Tags = tags.points2();
                if (!vol2Tags.empty())
                {
                    xfmResult = computeTransform(vol1Tags, vol2Tags, TransformType::LSQ6);
                    if (debug && xfmResult.valid)
                        std::cerr << "[mincpik] Transform computed (LSQ6, "
                                  << tags.tagCount() << " tags, RMS="
                                  << xfmResult.avgRMS << ")\n";
                }
            }
        }
        if (!args.xfmPath.empty() && volumes.size() >= 2)
        {
            glm::dmat4 mat;
            if (readXfmFile(args.xfmPath, mat))
            {
                xfmResult.valid = true;
                xfmResult.type = TransformType::LSQ12;
                xfmResult.linearMatrix = mat;
                if (debug)
                    std::cerr << "[mincpik] Loaded .xfm transform\n";
            }
            else
            {
                std::cerr << "Warning: failed to read .xfm file\n";
            }
        }

        // --- Determine slice coordinates ---
        // viewIndex: 0=axial(Z), 1=sagittal(X), 2=coronal(Y)
        // Each entry in sliceCoords[view] is a list of voxel indices.
        std::vector<int> sliceCoords[3];

        const Volume& refVol = volumes[0];

        // User-specified world coordinates take priority
        if (!args.axialAt.empty())
        {
            auto coords = parseDoubleList(args.axialAt);
            for (double c : coords)
                sliceCoords[0].push_back(worldToSliceVoxel(refVol, 0, c));
        }
        if (!args.sagittalAt.empty())
        {
            auto coords = parseDoubleList(args.sagittalAt);
            for (double c : coords)
                sliceCoords[1].push_back(worldToSliceVoxel(refVol, 1, c));
        }
        if (!args.coronalAt.empty())
        {
            auto coords = parseDoubleList(args.coronalAt);
            for (double c : coords)
                sliceCoords[2].push_back(worldToSliceVoxel(refVol, 2, c));
        }

        // Fall back to evenly spaced slices
        if (sliceCoords[0].empty())
            sliceCoords[0] = evenlySpacedSlices(refVol, 0, args.nAxial);
        if (sliceCoords[1].empty())
            sliceCoords[1] = evenlySpacedSlices(refVol, 1, args.nSagittal);
        if (sliceCoords[2].empty())
            sliceCoords[2] = evenlySpacedSlices(refVol, 2, args.nCoronal);

        int gap = args.gap;
        int nRows = std::max(1, args.rows);

        // --- Render all slices ---
        // Layout: rows = planes (axial, sagittal, coronal)
        //         columns = slices within each plane
        // When --rows N is given, each plane's slices are split across N
        // sub-rows (ceiling division for uneven counts).
        // We only include rows that have at least one slice.

        struct SliceRow
        {
            int viewIndex;
            std::vector<RenderedSlice> slices;
        };
        std::vector<SliceRow> rows;

        // Order: coronal (view 2), sagittal (view 1), axial (view 0)
        // This matches the visual convention in PLAN.md
        int viewOrder[] = {2, 1, 0};

        bool useOverlay = (volumes.size() >= 2);

        for (int vi : viewOrder)
        {
            if (sliceCoords[vi].empty())
                continue;

            // Render all slices for this plane first
            std::vector<RenderedSlice> allSlices;

            for (int sliceIdx : sliceCoords[vi])
            {
                RenderedSlice raw;
                if (useOverlay)
                {
                    std::vector<const Volume*> volPtrs;
                    for (auto& v : volumes)
                        volPtrs.push_back(&v);

                    const TransformResult* xfm = xfmResult.valid ? &xfmResult : nullptr;
                    raw = renderOverlaySlice(volPtrs, params, vi, sliceIdx, xfm);
                }
                else
                {
                    raw = renderSlice(volumes[0], params[0], vi, sliceIdx);
                }

                // Correct for non-uniform voxel spacing so that output
                // pixels are square in world space (matches new_register).
                allSlices.push_back(
                    resampleToPhysicalAspect(raw, refVol, vi));
            }

            // Split into nRows sub-rows
            int total = static_cast<int>(allSlices.size());
            int perRow = (total + nRows - 1) / nRows;  // ceiling division

            for (int r = 0; r < nRows; ++r)
            {
                int start = r * perRow;
                if (start >= total)
                    break;
                int end = std::min(start + perRow, total);

                SliceRow row;
                row.viewIndex = vi;
                for (int s = start; s < end; ++s)
                    row.slices.push_back(std::move(allSlices[s]));

                rows.push_back(std::move(row));
            }
        }

        if (rows.empty())
        {
            std::cerr << "Error: no slices to render.\n";
            return 1;
        }

        // --- Compute mosaic dimensions ---
        // Find the max number of columns across all rows
        int maxCols = 0;
        for (const auto& row : rows)
            maxCols = std::max(maxCols, static_cast<int>(row.slices.size()));

        // Compute total width and height
        // Each row's height is the max slice height in that row
        int totalWidth = 0;
        int totalHeight = 0;

        // First, find per-row dimensions
        struct RowLayout
        {
            int maxSliceWidth = 0;
            int maxSliceHeight = 0;
        };
        std::vector<RowLayout> rowLayouts(rows.size());

        for (size_t r = 0; r < rows.size(); ++r)
        {
            for (const auto& slice : rows[r].slices)
            {
                rowLayouts[r].maxSliceWidth = std::max(rowLayouts[r].maxSliceWidth, slice.width);
                rowLayouts[r].maxSliceHeight = std::max(rowLayouts[r].maxSliceHeight, slice.height);
            }
        }

        // Compute global max cell width (so columns are aligned)
        int cellWidth = 0;
        for (const auto& rl : rowLayouts)
            cellWidth = std::max(cellWidth, rl.maxSliceWidth);

        totalWidth = cellWidth * maxCols + gap * (maxCols - 1);
        for (size_t r = 0; r < rows.size(); ++r)
        {
            totalHeight += rowLayouts[r].maxSliceHeight;
            if (r + 1 < rows.size())
                totalHeight += gap;
        }

        if (debug)
        {
            std::cerr << "[mincpik] Mosaic: " << totalWidth << "x" << totalHeight
                      << " (" << rows.size() << " rows, " << maxCols << " cols)\n";
            for (size_t r = 0; r < rows.size(); ++r)
            {
                const char* viewNames[] = {"axial", "sagittal", "coronal"};
                std::cerr << "  Row " << r << ": " << viewNames[rows[r].viewIndex]
                          << " (" << rows[r].slices.size() << " slices, "
                          << rowLayouts[r].maxSliceWidth << "x"
                          << rowLayouts[r].maxSliceHeight << ")\n";
            }
        }

        // --- Assemble mosaic ---
        std::vector<uint32_t> mosaic(totalWidth * totalHeight, 0xFF000000);  // opaque black

        int curY = 0;
        for (size_t r = 0; r < rows.size(); ++r)
        {
            int curX = 0;
            for (size_t c = 0; c < rows[r].slices.size(); ++c)
            {
                const auto& slice = rows[r].slices[c];
                // Center slice within its cell
                int offsetX = curX + (cellWidth - slice.width) / 2;
                int offsetY = curY + (rowLayouts[r].maxSliceHeight - slice.height) / 2;
                blitSlice(slice, mosaic, totalWidth, offsetX, offsetY);
                curX += cellWidth + gap;
            }
            curY += rowLayouts[r].maxSliceHeight + gap;
        }

        // --- Optional title annotation ---
        // Render the title text and prepend it to the mosaic, expanding
        // the buffer vertically at the top.
        if (!args.title.empty())
        {
            uint32_t fgColour = parseFgColour(args.fgColourStr);

            // Determine font scale: explicit --font-scale, or auto-size
            // to ~4% of mosaic height (clamped 1x-8x).
            int fontSc = 1;
            if (args.fontScale.has_value())
            {
                fontSc = std::max(1, *args.fontScale);
            }
            else
            {
                fontSc = std::max(1, std::min(8,
                    static_cast<int>(std::round(totalHeight * 0.04 / 12.0))));
            }

            RenderedSlice titleSlice = renderTextRow(args.title, fgColour, fontSc);

            if (titleSlice.width > 0 && titleSlice.height > 0)
            {
                int titleRowHeight = titleSlice.height + gap;
                int newHeight = totalHeight + titleRowHeight;

                std::vector<uint32_t> newMosaic(totalWidth * newHeight, 0xFF000000);

                // Blit title, centered horizontally
                int titleX = std::max(0, (totalWidth - titleSlice.width) / 2);
                for (int y = 0; y < titleSlice.height; ++y)
                {
                    for (int x = 0; x < titleSlice.width && (titleX + x) < totalWidth; ++x)
                    {
                        uint32_t px = titleSlice.pixels[y * titleSlice.width + x];
                        // Only write non-transparent pixels (title bg is transparent)
                        if ((px >> 24) != 0)
                            newMosaic[y * totalWidth + titleX + x] = px;
                    }
                }

                // Blit original mosaic below the title row
                for (int y = 0; y < totalHeight; ++y)
                {
                    std::memcpy(
                        &newMosaic[(titleRowHeight + y) * totalWidth],
                        &mosaic[y * totalWidth],
                        totalWidth * sizeof(uint32_t));
                }

                mosaic = std::move(newMosaic);
                totalHeight = newHeight;

                if (debug)
                    std::cerr << "[mincpik] Title added: scale=" << fontSc
                              << " titleH=" << titleSlice.height
                              << " newMosaic=" << totalWidth << "x" << totalHeight << "\n";
            }
        }

        // --- Optional width scaling ---
        std::vector<uint32_t> finalPixels;
        int finalW = totalWidth;
        int finalH = totalHeight;

        if (args.width.has_value())
        {
            int targetW = *args.width;
            if (targetW > 0 && targetW != totalWidth)
            {
                double scale = static_cast<double>(targetW) / totalWidth;
                finalW = targetW;
                finalH = static_cast<int>(std::round(totalHeight * scale));
                if (finalH < 1) finalH = 1;

                finalPixels.resize(finalW * finalH);
                // Nearest-neighbour downscale
                for (int y = 0; y < finalH; ++y)
                {
                    int srcY = static_cast<int>(y / scale);
                    if (srcY >= totalHeight) srcY = totalHeight - 1;
                    for (int x = 0; x < finalW; ++x)
                    {
                        int srcX = static_cast<int>(x / scale);
                        if (srcX >= totalWidth) srcX = totalWidth - 1;
                        finalPixels[y * finalW + x] = mosaic[srcY * totalWidth + srcX];
                    }
                }
            }
        }

        const uint32_t* outPixels = finalPixels.empty() ? mosaic.data() : finalPixels.data();

        // --- Write PNG ---
        int stride = finalW * 4;  // 4 bytes per pixel (RGBA)
        int ok = stbi_write_png(args.outputPath.c_str(), finalW, finalH, 4,
                                outPixels, stride);
        if (!ok)
        {
            std::cerr << "Error: failed to write " << args.outputPath << "\n";
            return 1;
        }

        if (debug)
            std::cerr << "[mincpik] Wrote " << args.outputPath << " ("
                      << finalW << "x" << finalH << ")\n";

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
}
