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
#include <stdexcept>
#include <format>

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "AppConfig.h"
#include "ColourMap.h"
#include "GraphicsBackend.h"
#include "Volume.h"
#include "VulkanHelpers.h"

// --- Clamp colour mode for under/over range voxels ---
// -2 = "Current" (use volume's own colour map endpoint)
// -1 = "Transparent"
//  0..N = specific ColourMapType index (use that LUT's endpoint)
constexpr int kClampCurrent     = -2;
constexpr int kClampTransparent = -1;

// --- Per-volume view state ---
struct VolumeViewState
{
    VulkanTexture* sliceTextures[3] = { nullptr, nullptr, nullptr };
    int sliceIndices[3] = { 0, 0, 0 };  // Current slice for each view
    float valueRange[2] = { 0.0f, 1.0f };  // Min, Max
    float dragAccum[3] = { 0.0f, 0.0f, 0.0f };  // Middle-drag accumulator
    ColourMapType colourMap = ColourMapType::GrayScale;

    // Per-view zoom & pan
    float zoom[3] = { 1.0f, 1.0f, 1.0f };        // Zoom factor (1 = fit)
    float panU[3] = { 0.0f, 0.0f, 0.0f };         // Pan offset in normalised UV [0..1]
    float panV[3] = { 0.0f, 0.0f, 0.0f };

    // Alpha for overlay blending (0 = invisible, 1 = opaque)
    float overlayAlpha = 1.0f;

    // Colour for voxels below min / above max intensity range
    int underColourMode = kClampCurrent;  // default: LUT[0]
    int overColourMode  = kClampCurrent;  // default: LUT[255]
};

// --- Overlay panel state (uses first volume's grid as reference) ---
struct OverlayState
{
    VulkanTexture* textures[3] = { nullptr, nullptr, nullptr };
    float zoom[3] = { 1.0f, 1.0f, 1.0f };
    float panU[3] = { 0.5f, 0.5f, 0.5f };
    float panV[3] = { 0.5f, 0.5f, 0.5f };
    float dragAccum[3] = { 0.0f, 0.0f, 0.0f };
};

// --- Application State ---
std::vector<Volume> g_Volumes;
std::vector<std::string> g_VolumeNames;  // Display names (file basenames)
std::vector<std::string> g_VolumePaths;  // Full file paths (for config saving)
std::vector<VolumeViewState> g_ViewStates;
OverlayState g_Overlay;
bool g_LayoutInitialized = false;
bool g_CleanMode = false;  // Hide UI controls, show only slice views
bool g_SyncCursors = false;  // Synchronize cursor position across all views
int g_LastSyncSource = 0;  // Index of volume last interacted with (for sync reference)
float g_DpiScale = 1.0f;  // Set after backend initialisation
std::string g_LocalConfigPath;  // Path for "Save Local Config"

// --- Forward Declarations ---
void UpdateSliceTexture(int volumeIndex, int viewIndex);
void UpdateOverlayTexture(int viewIndex);
void UpdateAllOverlayTextures();
void ResetViews();
int RenderSliceView(int vi, int viewIndex, const ImVec2& childSize,
                    const Volume& vol, VolumeViewState& state);
int RenderOverlayView(int viewIndex, const ImVec2& childSize);

// --- Screenshot helper ---
// Finds the next available filename (screenshot000001.png, screenshot000002.png, ...)
// and saves the provided RGBA pixel data as a PNG file.
// Returns the filename on success, or an empty string on failure.
static std::string saveScreenshot(GraphicsBackend& backend)
{
    int width = 0, height = 0;
    auto pixels = backend.captureScreenshot(width, height);
    if (pixels.empty() || width <= 0 || height <= 0)
    {
        std::cerr << "Screenshot: failed to capture framebuffer\n";
        return {};
    }

    // Find the next available filename
    int index = 1;
    std::string filename;
    while (true)
    {
        filename = std::format("screenshot{:06d}.png", index);
        if (!std::filesystem::exists(filename))
            break;
        ++index;
    }

    int ok = stbi_write_png(filename.c_str(), width, height, 4,
                            pixels.data(), width * 4);
    if (!ok)
    {
        std::cerr << "Screenshot: failed to write " << filename << "\n";
        return {};
    }

    std::cout << "Screenshot saved: " << filename << "\n";
    return filename;
}

// --- Resolve clamp colour for under/over range voxels ---
// Returns a packed 0xAABBGGRR colour.
// mode: kClampCurrent, kClampTransparent, or a ColourMapType index.
// currentMap: the volume's active colour map (used when mode == kClampCurrent).
// isOver: true = use LUT[255], false = use LUT[0].
static uint32_t resolveClampColour(int mode, ColourMapType currentMap, bool isOver)
{
    if (mode == kClampTransparent)
        return 0x00000000;

    ColourMapType mapToUse = currentMap;
    if (mode >= 0 && mode < colourMapCount())
        mapToUse = static_cast<ColourMapType>(mode);

    const ColourLut& lut = colourMapLut(mapToUse);
    return isOver ? lut.table[255] : lut.table[0];
}

// --- Display name for a clamp colour mode (for UI combo) ---
static const char* clampColourLabel(int mode)
{
    if (mode == kClampCurrent)     return "Current";
    if (mode == kClampTransparent) return "Transparent";
    if (mode >= 0 && mode < colourMapCount())
        return colourMapName(static_cast<ColourMapType>(mode)).data();
    return "Unknown";
}

// --- Volume Rendering Helpers ---
void UpdateSliceTexture(int volumeIndex, int viewIndex)
{
    if (volumeIndex < 0 ||
        volumeIndex >= static_cast<int>(g_Volumes.size())) return;
    if (volumeIndex >= static_cast<int>(g_ViewStates.size())) return;

    const Volume& vol = g_Volumes[volumeIndex];
    if (vol.data.empty()) return;

    VolumeViewState& state = g_ViewStates[volumeIndex];
    const ColourLut& lut = colourMapLut(state.colourMap);

    int w, h;
    std::vector<uint32_t> pixels;

    int dimX = vol.dimensions[0];
    int dimY = vol.dimensions[1];
    int dimZ = vol.dimensions[2];

    float rangeMin = state.valueRange[0];
    float rangeMax = state.valueRange[1];
    float rangeSpan = rangeMax - rangeMin;
    if (rangeSpan < 1e-12f) rangeSpan = 1e-12f;

    // Lambda: map a raw voxel value through min/max range to a LUT colour.
    // Values below rangeMin or above rangeMax use the configured under/over colour.
    auto voxelToColour = [&](float val) -> uint32_t
    {
        if (val < rangeMin)
            return resolveClampColour(state.underColourMode, state.colourMap, false);
        if (val > rangeMax)
            return resolveClampColour(state.overColourMode, state.colourMap, true);
        float norm = (val - rangeMin) / rangeSpan;
        int idx = static_cast<int>(norm * 255.0f + 0.5f);
        if (idx > 255) idx = 255;
        return lut.table[idx];
    };

    if (viewIndex == 0)  // Transverse (Z-slice)
    {
        w = dimX; h = dimY;
        int z = state.sliceIndices[0];
        if (z >= dimZ) z = dimZ - 1;

        pixels.resize(w * h);
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                pixels[(h - 1 - y) * w + x] = voxelToColour(vol.get(x, y, z));
            }
        }
    }
    else if (viewIndex == 1)  // Sagittal (X-slice)
    {
        w = dimY; h = dimZ;
        int x = state.sliceIndices[1];
        if (x >= dimX) x = dimX - 1;

        pixels.resize(w * h);
        for (int z = 0; z < h; ++z)
        {
            for (int y = 0; y < w; ++y)
            {
                pixels[(h - 1 - z) * w + y] = voxelToColour(vol.get(x, y, z));
            }
        }
    }
    else  // Coronal (Y-slice)
    {
        w = dimX; h = dimZ;
        int y = state.sliceIndices[2];
        if (y >= dimY) y = dimY - 1;

        pixels.resize(w * h);
        for (int z = 0; z < h; ++z)
        {
            for (int x = 0; x < w; ++x)
            {
                pixels[(h - 1 - z) * w + x] = voxelToColour(vol.get(x, y, z));
            }
        }
    }

    VulkanTexture*& tex = state.sliceTextures[viewIndex];
    if (!tex)
    {
        tex = VulkanHelpers::CreateTexture(w, h, pixels.data());
    }
    else
    {
        if (tex->width != w || tex->height != h)
        {
            VulkanHelpers::DestroyTexture(tex);
            tex = VulkanHelpers::CreateTexture(w, h, pixels.data());
        }
        else
        {
            VulkanHelpers::UpdateTexture(tex, pixels.data());
        }
    }
}

// --- Composite all volumes into the overlay texture for one view ---
// Uses the first volume's voxel grid as the reference frame.
// For each reference voxel, we find the corresponding world coordinate,
// then sample from every volume (nearest-neighbour in their own grid).
// Colours are alpha-composited using "over" blending.
void UpdateOverlayTexture(int viewIndex)
{
    int numVols = static_cast<int>(g_Volumes.size());
    if (numVols < 2) return;

    const Volume& ref = g_Volumes[0];
    const VolumeViewState& refState = g_ViewStates[0];
    if (ref.data.empty()) return;

    int w, h;
    // The overlay grid dimensions match the reference volume's slice
    if (viewIndex == 0)      { w = ref.dimensions[0]; h = ref.dimensions[1]; }
    else if (viewIndex == 1) { w = ref.dimensions[1]; h = ref.dimensions[2]; }
    else                     { w = ref.dimensions[0]; h = ref.dimensions[2]; }

    int sliceIdx = refState.sliceIndices[viewIndex];

    std::vector<uint32_t> pixels(w * h);

    // For each pixel in the reference grid
    for (int py = 0; py < h; ++py)
    {
        for (int px = 0; px < w; ++px)
        {
            // Determine reference voxel indices for this pixel
            int refX, refY, refZ;
            if (viewIndex == 0)       { refX = px; refY = py; refZ = std::clamp(sliceIdx, 0, ref.dimensions[2] - 1); }
            else if (viewIndex == 1)  { refX = std::clamp(sliceIdx, 0, ref.dimensions[0] - 1); refY = px; refZ = py; }
            else                      { refX = px; refY = std::clamp(sliceIdx, 0, ref.dimensions[1] - 1); refZ = py; }

            // World coordinate of this reference voxel
            double wx = ref.start[0] + refX * ref.step[0];
            double wy = ref.start[1] + refY * ref.step[1];
            double wz = ref.start[2] + refZ * ref.step[2];

            // Composite: weighted average blend (each volume
            // contributes proportionally to its alpha weight)
            float accR = 0.0f, accG = 0.0f, accB = 0.0f;
            float totalWeight = 0.0f;

            for (int vi = 0; vi < numVols; ++vi)
            {
                const Volume& vol = g_Volumes[vi];
                const VolumeViewState& st = g_ViewStates[vi];
                if (vol.data.empty()) continue;
                if (st.overlayAlpha <= 0.0f) continue;

                // Map world coord to this volume's voxel space
                // (nearest-neighbour: round to nearest integer)
                double vx = (wx - vol.start[0]) / vol.step[0];
                double vy = (wy - vol.start[1]) / vol.step[1];
                double vz = (wz - vol.start[2]) / vol.step[2];

                int ix = static_cast<int>(std::round(vx));
                int iy = static_cast<int>(std::round(vy));
                int iz = static_cast<int>(std::round(vz));

                // Skip if outside this volume's bounds
                if (ix < 0 || ix >= vol.dimensions[0] ||
                    iy < 0 || iy >= vol.dimensions[1] ||
                    iz < 0 || iz >= vol.dimensions[2])
                    continue;

                // Get raw voxel value and map through colour LUT
                float raw = vol.get(ix, iy, iz);
                float rangeMin = st.valueRange[0];
                float rangeMax = st.valueRange[1];
                float rangeSpan = rangeMax - rangeMin;
                if (rangeSpan < 1e-12f) rangeSpan = 1e-12f;

                uint32_t packed;
                if (raw < rangeMin)
                    packed = resolveClampColour(st.underColourMode, st.colourMap, false);
                else if (raw > rangeMax)
                    packed = resolveClampColour(st.overColourMode, st.colourMap, true);
                else
                {
                    float norm = (raw - rangeMin) / rangeSpan;
                    norm = std::clamp(norm, 0.0f, 1.0f);
                    int lutIdx = std::min(static_cast<int>(norm * 255.0f + 0.5f), 255);
                    const ColourLut& lut = colourMapLut(st.colourMap);
                    packed = lut.table[lutIdx];
                }

                // Skip transparent voxels (alpha == 0)
                if ((packed >> 24) == 0) continue;

                // Unpack 0xAABBGGRR
                float srcR = static_cast<float>((packed >>  0) & 0xFF) / 255.0f;
                float srcG = static_cast<float>((packed >>  8) & 0xFF) / 255.0f;
                float srcB = static_cast<float>((packed >> 16) & 0xFF) / 255.0f;
                float w_alpha = st.overlayAlpha;

                accR += srcR * w_alpha;
                accG += srcG * w_alpha;
                accB += srcB * w_alpha;
                totalWeight += w_alpha;
            }

            // Normalise by total weight
            if (totalWeight > 0.0f)
            {
                accR /= totalWeight;
                accG /= totalWeight;
                accB /= totalWeight;
            }

            // Clamp and pack to 0xAABBGGRR
            auto toByte = [](float v) -> uint8_t {
                return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
            };

            uint32_t result = static_cast<uint32_t>(toByte(accR))
                            | (static_cast<uint32_t>(toByte(accG)) << 8)
                            | (static_cast<uint32_t>(toByte(accB)) << 16)
                            | (0xFFu << 24);

            // V-flip: image row 0 = top = max voxel index
            pixels[(h - 1 - py) * w + px] = result;
        }
    }

    VulkanTexture*& tex = g_Overlay.textures[viewIndex];
    if (!tex)
    {
        tex = VulkanHelpers::CreateTexture(w, h, pixels.data());
    }
    else
    {
        if (tex->width != w || tex->height != h)
        {
            VulkanHelpers::DestroyTexture(tex);
            tex = VulkanHelpers::CreateTexture(w, h, pixels.data());
        }
        else
        {
            VulkanHelpers::UpdateTexture(tex, pixels.data());
        }
    }
}

void UpdateAllOverlayTextures()
{
    for (int v = 0; v < 3; ++v)
        UpdateOverlayTexture(v);
}

// --- Convert slice indices to physical (world) coordinates ---
// Given a volume and slice indices, compute the world space position.
static void sliceIndicesToWorld(const Volume& vol, const int indices[3], double world[3])
{
    // Use precomputed voxel-to-world matrix
    // The matrix includes the +0.5 offset already
    glm::dvec4 voxel(indices[0], indices[1], indices[2], 1.0);
    glm::dvec4 worldH = vol.voxelToWorld * voxel;
    
    world[0] = worldH.x;
    world[1] = worldH.y;
    world[2] = worldH.z;
}

// --- Convert physical coordinates to slice indices ---
// Given a world position, find the voxel indices that contain this position.
static void worldToSliceIndices(const Volume& vol, const double world[3], int indices[3])
{
    // Use precomputed world-to-voxel matrix
    glm::dvec4 worldH(world[0], world[1], world[2], 1.0);
    glm::dvec4 voxelH = vol.worldToVoxel * worldH;
    
    indices[0] = static_cast<int>(std::round(voxelH.x));
    indices[1] = static_cast<int>(std::round(voxelH.y));
    indices[2] = static_cast<int>(std::round(voxelH.z));
    
    // Clamp to valid range
    indices[0] = std::clamp(indices[0], 0, vol.dimensions[0] - 1);
    indices[1] = std::clamp(indices[1], 0, vol.dimensions[1] - 1);
    indices[2] = std::clamp(indices[2], 0, vol.dimensions[2] - 1);
}

// --- Synchronize cursor position across all volumes based on physical coordinates ---
// When sync is enabled, moving cursor in any volume syncs all others to the same
// physical world position (not the same slice index).
// This is called AFTER all volumes are rendered to sync their slice indices.
static void SyncCursors()
{
    if (!g_SyncCursors || g_Volumes.size() < 2)
        return;

    // Use the last interacted volume as reference for sync
    const Volume& refVol = g_Volumes[g_LastSyncSource];
    const VolumeViewState& refState = g_ViewStates[g_LastSyncSource];
    
    // Get world position from reference volume
    double worldPos[3];
    sliceIndicesToWorld(refVol, refState.sliceIndices, worldPos);
    
    // Find corresponding slice indices in all other volumes
    for (int i = 0; i < static_cast<int>(g_Volumes.size()); ++i)
    {
        if (i == g_LastSyncSource)
            continue;
            
        Volume& otherVol = g_Volumes[i];
        VolumeViewState& otherState = g_ViewStates[i];
        int indices[3];
        worldToSliceIndices(otherVol, worldPos, indices);

        // Clamp to valid range
        indices[0] = std::clamp(indices[0], 0, otherVol.dimensions[0] - 1);
        indices[1] = std::clamp(indices[1], 0, otherVol.dimensions[1] - 1);
        indices[2] = std::clamp(indices[2], 0, otherVol.dimensions[2] - 1);

        otherState.sliceIndices[0] = indices[0];
        otherState.sliceIndices[1] = indices[1];
        otherState.sliceIndices[2] = indices[2];
    }
    
    // Update textures for all volumes
    for (int i = 0; i < static_cast<int>(g_Volumes.size()); ++i)
    {
        for (int v = 0; v < 3; ++v)
            UpdateSliceTexture(i, v);
    }
    UpdateAllOverlayTextures();
}

// --- Initialize state for all loaded volumes ---
void ResetViews()
{
    g_ViewStates.resize(g_Volumes.size());

    for (int vi = 0; vi < static_cast<int>(g_Volumes.size()); ++vi)
    {
        const Volume& vol = g_Volumes[vi];
        if (vol.data.empty()) continue;

        VolumeViewState& state = g_ViewStates[vi];

        state.sliceIndices[0] = vol.dimensions[2] / 2;
        state.sliceIndices[1] = vol.dimensions[0] / 2;
        state.sliceIndices[2] = vol.dimensions[1] / 2;

        state.valueRange[0] = vol.min_value;
        state.valueRange[1] = vol.max_value;

        for (int v = 0; v < 3; ++v)
        {
            state.zoom[v] = 1.0f;
            state.panU[v] = 0.5f;
            state.panV[v] = 0.5f;
        }

        UpdateSliceTexture(vi, 0);
        UpdateSliceTexture(vi, 1);
        UpdateSliceTexture(vi, 2);
    }
}

// --- Render a single slice view within a child region ---
// Returns a bitmask of view indices that need texture updates.
int RenderSliceView(int vi, int viewIndex, const ImVec2& childSize,
                            const Volume& vol, VolumeViewState& state)
{
    int dirtyMask = 0;
    char childId[64];
    std::snprintf(childId, sizeof(childId), "##view_%d_%d", vi, viewIndex);

    ImGui::BeginChild(childId, childSize, ImGuiChildFlags_Borders);
    {

        if (state.sliceTextures[viewIndex])
        {
            VulkanTexture* tex = state.sliceTextures[viewIndex];
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float sliderHeight = 30.0f * g_DpiScale;
            avail.y -= sliderHeight;

            // Image position and size for mouse hit-testing
            ImVec2 imgPos(0, 0);
            ImVec2 imgSize(0, 0);

            if (avail.x > 0 && avail.y > 0)
            {
                // Compute world-space aspect ratio, accounting for
                // non-uniform voxel spacing.
                //   Transverse (0): X horizontal, Y vertical
                //   Sagittal   (1): Y horizontal, Z vertical
                //   Coronal    (2): X horizontal, Z vertical
                int axisU, axisV;
                if (viewIndex == 0)      { axisU = 0; axisV = 1; }
                else if (viewIndex == 1) { axisU = 1; axisV = 2; }
                else                     { axisU = 0; axisV = 2; }

                double pixelAspect = vol.slicePixelAspect(axisU, axisV);
                float aspect = static_cast<float>(tex->width) /
                               static_cast<float>(tex->height) *
                               static_cast<float>(pixelAspect);

                imgSize = avail;
                if (imgSize.x / imgSize.y > aspect)
                    imgSize.x = imgSize.y * aspect;
                else
                    imgSize.y = imgSize.x / aspect;

                // Center the image horizontally
                float padX = (avail.x - imgSize.x) * 0.5f;
                if (padX > 0)
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padX);

                imgPos = ImGui::GetCursorScreenPos();

                // --- Compute visible UV region from zoom/pan ---
                float zf = state.zoom[viewIndex];
                float halfU = 0.5f / zf;
                float halfV = 0.5f / zf;
                float centerU = state.panU[viewIndex];
                float centerV = state.panV[viewIndex];
                ImVec2 uv0(centerU - halfU, centerV - halfV);
                ImVec2 uv1(centerU + halfU, centerV + halfV);

                ImGui::Image(
                    reinterpret_cast<ImTextureID>(tex->descriptor_set),
                    imgSize, uv0, uv1);

                // --- Draw crosshair showing other panels' slice positions ---
                {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    const ImU32 crossCol = IM_COL32(255, 255, 0, 100);
                    const float crossThick = 1.0f * g_DpiScale;

                    float normCrossU = 0.0f, normCrossV = 0.0f;
                    if (viewIndex == 0)
                    {
                        normCrossU = static_cast<float>(state.sliceIndices[1]) /
                                     static_cast<float>(std::max(vol.dimensions[0] - 1, 1));
                        normCrossV = static_cast<float>(state.sliceIndices[2]) /
                                     static_cast<float>(std::max(vol.dimensions[1] - 1, 1));
                    }
                    else if (viewIndex == 1)
                    {
                        normCrossU = static_cast<float>(state.sliceIndices[2]) /
                                     static_cast<float>(std::max(vol.dimensions[1] - 1, 1));
                        normCrossV = static_cast<float>(state.sliceIndices[0]) /
                                     static_cast<float>(std::max(vol.dimensions[2] - 1, 1));
                    }
                    else
                    {
                        normCrossU = static_cast<float>(state.sliceIndices[1]) /
                                     static_cast<float>(std::max(vol.dimensions[0] - 1, 1));
                        normCrossV = static_cast<float>(state.sliceIndices[0]) /
                                     static_cast<float>(std::max(vol.dimensions[2] - 1, 1));
                    }

                    // V is flipped (image top = max voxel)
                    normCrossV = 1.0f - normCrossV;

                    // Map from full UV to screen coords accounting for zoom/pan
                    float uvSpanU = uv1.x - uv0.x;
                    float uvSpanV = uv1.y - uv0.y;
                    float screenX = imgPos.x + (normCrossU - uv0.x) / uvSpanU * imgSize.x;
                    float screenY = imgPos.y + (normCrossV - uv0.y) / uvSpanV * imgSize.y;

                    // Clip crosshair lines to the image rectangle
                    ImVec2 clipMin = imgPos;
                    ImVec2 clipMax(imgPos.x + imgSize.x, imgPos.y + imgSize.y);
                    dl->PushClipRect(clipMin, clipMax, true);

                    dl->AddLine(
                        ImVec2(screenX, imgPos.y),
                        ImVec2(screenX, imgPos.y + imgSize.y),
                        crossCol, crossThick);
                    dl->AddLine(
                        ImVec2(imgPos.x, screenY),
                        ImVec2(imgPos.x + imgSize.x, screenY),
                        crossCol, crossThick);

                    dl->PopClipRect();
                }

                // --- Mouse interaction on the image ---
                bool imageHovered = ImGui::IsItemHovered();
                bool shiftHeld = ImGui::GetIO().KeyShift;

                // Shift + Left drag: pan the view
                if (imageHovered && shiftHeld &&
                    ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
                {
                    ImVec2 delta = ImGui::GetIO().MouseDelta;
                    float uvSpanU = uv1.x - uv0.x;
                    float uvSpanV = uv1.y - uv0.y;
                    state.panU[viewIndex] -= delta.x / imgSize.x * uvSpanU;
                    state.panV[viewIndex] -= delta.y / imgSize.y * uvSpanV;
                }
                // Left click / drag (no Shift): set cross-slice positions
                else if (imageHovered && !shiftHeld &&
                         ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    ImVec2 mouse = ImGui::GetMousePos();
                    // Map screen position to full UV through zoom/pan
                    float normU = uv0.x + (mouse.x - imgPos.x) / imgSize.x * (uv1.x - uv0.x);
                    float normV = uv0.y + (mouse.y - imgPos.y) / imgSize.y * (uv1.y - uv0.y);
                    normU = std::clamp(normU, 0.0f, 1.0f);
                    normV = std::clamp(normV, 0.0f, 1.0f);
                    // V is flipped
                    normV = 1.0f - normV;

                    if (viewIndex == 0)
                    {
                        int voxX = static_cast<int>(normU * (vol.dimensions[0] - 1) + 0.5f);
                        int voxY = static_cast<int>(normV * (vol.dimensions[1] - 1) + 0.5f);
                        state.sliceIndices[1] = std::clamp(voxX, 0, vol.dimensions[0] - 1);
                        state.sliceIndices[2] = std::clamp(voxY, 0, vol.dimensions[1] - 1);
                        dirtyMask |= (1 << 1) | (1 << 2);
                        if (g_SyncCursors)
                            g_LastSyncSource = vi;
                    }
                    else if (viewIndex == 1)
                    {
                        int voxY = static_cast<int>(normU * (vol.dimensions[1] - 1) + 0.5f);
                        int voxZ = static_cast<int>(normV * (vol.dimensions[2] - 1) + 0.5f);
                        state.sliceIndices[2] = std::clamp(voxY, 0, vol.dimensions[1] - 1);
                        state.sliceIndices[0] = std::clamp(voxZ, 0, vol.dimensions[2] - 1);
                        dirtyMask |= (1 << 0) | (1 << 2);
                        if (g_SyncCursors)
                            g_LastSyncSource = vi;
                    }
                    else
                    {
                        int voxX = static_cast<int>(normU * (vol.dimensions[0] - 1) + 0.5f);
                        int voxZ = static_cast<int>(normV * (vol.dimensions[2] - 1) + 0.5f);
                        state.sliceIndices[1] = std::clamp(voxX, 0, vol.dimensions[0] - 1);
                        state.sliceIndices[0] = std::clamp(voxZ, 0, vol.dimensions[2] - 1);
                        dirtyMask |= (1 << 0) | (1 << 1);
                        if (g_SyncCursors)
                            g_LastSyncSource = vi;
                    }
                }

                // Shift + Middle drag: zoom (drag up = zoom in, down = out)
                if (imageHovered && shiftHeld &&
                    ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))
                {
                    float dragY = ImGui::GetIO().MouseDelta.y;
                    if (dragY != 0.0f)
                    {
                        float factor = 1.0f - dragY * 0.005f;
                        state.zoom[viewIndex] = std::clamp(
                            state.zoom[viewIndex] * factor, 0.1f, 50.0f);
                    }
                }
                // Middle button drag (no Shift): scroll current slice
                else if (imageHovered && !shiftHeld &&
                         ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))
                {
                    float dragY = ImGui::GetIO().MouseDelta.y;
                    if (dragY != 0.0f)
                    {
                        int maxSliceVal = (viewIndex == 0) ? vol.dimensions[2]
                                        : (viewIndex == 1) ? vol.dimensions[0]
                                                           : vol.dimensions[1];
                        float sliceDelta = -dragY / imgSize.y *
                                           static_cast<float>(maxSliceVal);
                        state.dragAccum[viewIndex] += sliceDelta;
                        int steps = static_cast<int>(state.dragAccum[viewIndex]);
                        if (steps != 0)
                        {
                            state.dragAccum[viewIndex] -= static_cast<float>(steps);
                            state.sliceIndices[viewIndex] = std::clamp(
                                state.sliceIndices[viewIndex] + steps,
                                0, maxSliceVal - 1);
                            dirtyMask |= (1 << viewIndex);
                            if (g_SyncCursors)
                                g_LastSyncSource = vi;
                        }
                    }
                }
                else if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
                {
                    state.dragAccum[viewIndex] = 0.0f;
                }

                // Mouse wheel: zoom centered on cursor position
                if (imageHovered)
                {
                    float wheel = ImGui::GetIO().MouseWheel;
                    if (wheel != 0.0f)
                    {
                        ImVec2 mouse = ImGui::GetMousePos();
                        float cursorU = uv0.x + (mouse.x - imgPos.x) / imgSize.x * (uv1.x - uv0.x);
                        float cursorV = uv0.y + (mouse.y - imgPos.y) / imgSize.y * (uv1.y - uv0.y);

                        float factor = (wheel > 0) ? 1.1f : 1.0f / 1.1f;
                        float newZoom = std::clamp(
                            state.zoom[viewIndex] * factor, 0.1f, 50.0f);

                        // Adjust pan so the point under the cursor stays fixed
                        float zOld = state.zoom[viewIndex];
                        state.panU[viewIndex] = cursorU +
                            (state.panU[viewIndex] - cursorU) * (zOld / newZoom);
                        state.panV[viewIndex] = cursorV +
                            (state.panV[viewIndex] - cursorV) * (zOld / newZoom);
                        state.zoom[viewIndex] = newZoom;
                    }
                }
            }

            // Slice navigation slider (hidden in clean mode)
            if (!g_CleanMode)
            {
                int maxSlice = (viewIndex == 0) ? vol.dimensions[2]
                             : (viewIndex == 1) ? vol.dimensions[0]
                                                : vol.dimensions[1];

                ImGui::PushID(vi * 3 + viewIndex);
                {
                    if (ImGui::Button("-"))
                    {
                        if (state.sliceIndices[viewIndex] > 0)
                        {
                            state.sliceIndices[viewIndex]--;
                            dirtyMask |= (1 << viewIndex);
                        }
                    }
                    ImGui::SameLine();

                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30.0f * g_DpiScale);
                    if (ImGui::SliderInt("##slice", &state.sliceIndices[viewIndex],
                                         0, maxSlice - 1, "Slice %d"))
                    {
                        dirtyMask |= (1 << viewIndex);
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("+"))
                    {
                        if (state.sliceIndices[viewIndex] < maxSlice - 1)
                        {
                            state.sliceIndices[viewIndex]++;
                            dirtyMask |= (1 << viewIndex);
                        }
                    }
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();
    return dirtyMask;
}

// --- Render one overlay slice view (composited from all volumes) ---
// Returns a bitmask of view indices that need overlay texture updates.
// Mouse interaction syncs slice positions across all individual volumes.
int RenderOverlayView(int viewIndex, const ImVec2& childSize)
{
    int dirtyMask = 0;
    const Volume& ref = g_Volumes[0];
    VolumeViewState& refState = g_ViewStates[0];

    char childId[64];
    std::snprintf(childId, sizeof(childId), "##overlay_%d", viewIndex);

    ImGui::BeginChild(childId, childSize, ImGuiChildFlags_Borders);
    {
        if (g_Overlay.textures[viewIndex])
        {
            VulkanTexture* tex = g_Overlay.textures[viewIndex];
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float sliderHeight = 30.0f * g_DpiScale;
            avail.y -= sliderHeight;

            ImVec2 imgPos(0, 0);
            ImVec2 imgSize(0, 0);

            if (avail.x > 0 && avail.y > 0)
            {
                int axisU, axisV;
                if (viewIndex == 0)      { axisU = 0; axisV = 1; }
                else if (viewIndex == 1) { axisU = 1; axisV = 2; }
                else                     { axisU = 0; axisV = 2; }

                double pixelAspect = ref.slicePixelAspect(axisU, axisV);
                float aspect = static_cast<float>(tex->width) /
                               static_cast<float>(tex->height) *
                               static_cast<float>(pixelAspect);

                imgSize = avail;
                if (imgSize.x / imgSize.y > aspect)
                    imgSize.x = imgSize.y * aspect;
                else
                    imgSize.y = imgSize.x / aspect;

                float padX = (avail.x - imgSize.x) * 0.5f;
                if (padX > 0)
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padX);

                imgPos = ImGui::GetCursorScreenPos();

                float zf = g_Overlay.zoom[viewIndex];
                float halfU = 0.5f / zf;
                float halfV = 0.5f / zf;
                float centerU = g_Overlay.panU[viewIndex];
                float centerV = g_Overlay.panV[viewIndex];
                ImVec2 uv0(centerU - halfU, centerV - halfV);
                ImVec2 uv1(centerU + halfU, centerV + halfV);

                ImGui::Image(
                    reinterpret_cast<ImTextureID>(tex->descriptor_set),
                    imgSize, uv0, uv1);

                // --- Draw crosshair ---
                {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    const ImU32 crossCol = IM_COL32(255, 255, 0, 100);
                    const float crossThick = 1.0f * g_DpiScale;

                    float normCrossU = 0.0f, normCrossV = 0.0f;
                    if (viewIndex == 0)
                    {
                        normCrossU = static_cast<float>(refState.sliceIndices[1]) /
                                     static_cast<float>(std::max(ref.dimensions[0] - 1, 1));
                        normCrossV = static_cast<float>(refState.sliceIndices[2]) /
                                     static_cast<float>(std::max(ref.dimensions[1] - 1, 1));
                    }
                    else if (viewIndex == 1)
                    {
                        normCrossU = static_cast<float>(refState.sliceIndices[2]) /
                                     static_cast<float>(std::max(ref.dimensions[1] - 1, 1));
                        normCrossV = static_cast<float>(refState.sliceIndices[0]) /
                                     static_cast<float>(std::max(ref.dimensions[2] - 1, 1));
                    }
                    else
                    {
                        normCrossU = static_cast<float>(refState.sliceIndices[1]) /
                                     static_cast<float>(std::max(ref.dimensions[0] - 1, 1));
                        normCrossV = static_cast<float>(refState.sliceIndices[0]) /
                                     static_cast<float>(std::max(ref.dimensions[2] - 1, 1));
                    }

                    normCrossV = 1.0f - normCrossV;

                    float uvSpanU = uv1.x - uv0.x;
                    float uvSpanV = uv1.y - uv0.y;
                    float screenX = imgPos.x + (normCrossU - uv0.x) / uvSpanU * imgSize.x;
                    float screenY = imgPos.y + (normCrossV - uv0.y) / uvSpanV * imgSize.y;

                    ImVec2 clipMin = imgPos;
                    ImVec2 clipMax(imgPos.x + imgSize.x, imgPos.y + imgSize.y);
                    dl->PushClipRect(clipMin, clipMax, true);

                    dl->AddLine(
                        ImVec2(screenX, imgPos.y),
                        ImVec2(screenX, imgPos.y + imgSize.y),
                        crossCol, crossThick);
                    dl->AddLine(
                        ImVec2(imgPos.x, screenY),
                        ImVec2(imgPos.x + imgSize.x, screenY),
                        crossCol, crossThick);

                    dl->PopClipRect();
                }

                // --- Mouse interaction (synced to all volumes) ---
                bool imageHovered = ImGui::IsItemHovered();
                bool shiftHeld = ImGui::GetIO().KeyShift;

                // Shift + Left drag: pan
                if (imageHovered && shiftHeld &&
                    ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
                {
                    ImVec2 delta = ImGui::GetIO().MouseDelta;
                    float uvSpanU = uv1.x - uv0.x;
                    float uvSpanV = uv1.y - uv0.y;
                    g_Overlay.panU[viewIndex] -= delta.x / imgSize.x * uvSpanU;
                    g_Overlay.panV[viewIndex] -= delta.y / imgSize.y * uvSpanV;
                }
                // Left click/drag: set cross-slice positions (synced to all volumes)
                else if (imageHovered && !shiftHeld &&
                         ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    ImVec2 mouse = ImGui::GetMousePos();
                    float normU = uv0.x + (mouse.x - imgPos.x) / imgSize.x * (uv1.x - uv0.x);
                    float normV = uv0.y + (mouse.y - imgPos.y) / imgSize.y * (uv1.y - uv0.y);
                    normU = std::clamp(normU, 0.0f, 1.0f);
                    normV = std::clamp(normV, 0.0f, 1.0f);
                    normV = 1.0f - normV;

                    if (viewIndex == 0)
                    {
                        int voxX = static_cast<int>(normU * (ref.dimensions[0] - 1) + 0.5f);
                        int voxY = static_cast<int>(normV * (ref.dimensions[1] - 1) + 0.5f);
                        int newSag = std::clamp(voxX, 0, ref.dimensions[0] - 1);
                        int newCor = std::clamp(voxY, 0, ref.dimensions[1] - 1);
                        for (auto& st : g_ViewStates)
                        {
                            st.sliceIndices[1] = newSag;
                            st.sliceIndices[2] = newCor;
                        }
                        dirtyMask |= (1 << 1) | (1 << 2);
                    }
                    else if (viewIndex == 1)
                    {
                        int voxY = static_cast<int>(normU * (ref.dimensions[1] - 1) + 0.5f);
                        int voxZ = static_cast<int>(normV * (ref.dimensions[2] - 1) + 0.5f);
                        int newCor = std::clamp(voxY, 0, ref.dimensions[1] - 1);
                        int newTra = std::clamp(voxZ, 0, ref.dimensions[2] - 1);
                        for (auto& st : g_ViewStates)
                        {
                            st.sliceIndices[2] = newCor;
                            st.sliceIndices[0] = newTra;
                        }
                        dirtyMask |= (1 << 0) | (1 << 2);
                    }
                    else
                    {
                        int voxX = static_cast<int>(normU * (ref.dimensions[0] - 1) + 0.5f);
                        int voxZ = static_cast<int>(normV * (ref.dimensions[2] - 1) + 0.5f);
                        int newSag = std::clamp(voxX, 0, ref.dimensions[0] - 1);
                        int newTra = std::clamp(voxZ, 0, ref.dimensions[2] - 1);
                        for (auto& st : g_ViewStates)
                        {
                            st.sliceIndices[1] = newSag;
                            st.sliceIndices[0] = newTra;
                        }
                        dirtyMask |= (1 << 0) | (1 << 1);
                    }
                }

                // Shift + Middle drag: zoom
                if (imageHovered && shiftHeld &&
                    ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))
                {
                    float dragY = ImGui::GetIO().MouseDelta.y;
                    if (dragY != 0.0f)
                    {
                        float factor = 1.0f - dragY * 0.005f;
                        g_Overlay.zoom[viewIndex] = std::clamp(
                            g_Overlay.zoom[viewIndex] * factor, 0.1f, 50.0f);
                    }
                }
                // Middle drag: scroll slice (synced)
                else if (imageHovered && !shiftHeld &&
                         ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))
                {
                    float dragY = ImGui::GetIO().MouseDelta.y;
                    if (dragY != 0.0f)
                    {
                        int maxSliceVal = (viewIndex == 0) ? ref.dimensions[2]
                                        : (viewIndex == 1) ? ref.dimensions[0]
                                                           : ref.dimensions[1];
                        float sliceDelta = -dragY / imgSize.y *
                                           static_cast<float>(maxSliceVal);
                        g_Overlay.dragAccum[viewIndex] += sliceDelta;
                        int steps = static_cast<int>(g_Overlay.dragAccum[viewIndex]);
                        if (steps != 0)
                        {
                            g_Overlay.dragAccum[viewIndex] -= static_cast<float>(steps);
                            int newSlice = std::clamp(
                                refState.sliceIndices[viewIndex] + steps,
                                0, maxSliceVal - 1);
                            for (auto& st : g_ViewStates)
                                st.sliceIndices[viewIndex] = newSlice;
                            dirtyMask |= (1 << viewIndex);
                        }
                    }
                }
                else if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
                {
                    g_Overlay.dragAccum[viewIndex] = 0.0f;
                }

                // Mouse wheel: zoom centered on cursor
                if (imageHovered)
                {
                    float wheel = ImGui::GetIO().MouseWheel;
                    if (wheel != 0.0f)
                    {
                        ImVec2 mouse = ImGui::GetMousePos();
                        float cursorU = uv0.x + (mouse.x - imgPos.x) / imgSize.x * (uv1.x - uv0.x);
                        float cursorV = uv0.y + (mouse.y - imgPos.y) / imgSize.y * (uv1.y - uv0.y);

                        float factor = (wheel > 0) ? 1.1f : 1.0f / 1.1f;
                        float newZoom = std::clamp(
                            g_Overlay.zoom[viewIndex] * factor, 0.1f, 50.0f);

                        float zOld = g_Overlay.zoom[viewIndex];
                        g_Overlay.panU[viewIndex] = cursorU +
                            (g_Overlay.panU[viewIndex] - cursorU) * (zOld / newZoom);
                        g_Overlay.panV[viewIndex] = cursorV +
                            (g_Overlay.panV[viewIndex] - cursorV) * (zOld / newZoom);
                        g_Overlay.zoom[viewIndex] = newZoom;
                    }
                }
            }

            // Slice navigation slider (synced to all volumes, hidden in clean mode)
            if (!g_CleanMode)
            {
                int maxSlice = (viewIndex == 0) ? ref.dimensions[2]
                             : (viewIndex == 1) ? ref.dimensions[0]
                                                : ref.dimensions[1];

                ImGui::PushID(100 + viewIndex);
                {
                    if (ImGui::Button("-"))
                    {
                        if (refState.sliceIndices[viewIndex] > 0)
                        {
                            int newSlice = refState.sliceIndices[viewIndex] - 1;
                            for (auto& st : g_ViewStates)
                                st.sliceIndices[viewIndex] = newSlice;
                            dirtyMask |= (1 << viewIndex);
                        }
                    }
                    ImGui::SameLine();

                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30.0f * g_DpiScale);
                    int sliceVal = refState.sliceIndices[viewIndex];
                    if (ImGui::SliderInt("##slice", &sliceVal,
                                         0, maxSlice - 1, "Slice %d"))
                    {
                        for (auto& st : g_ViewStates)
                            st.sliceIndices[viewIndex] = sliceVal;
                        dirtyMask |= (1 << viewIndex);
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("+"))
                    {
                        if (refState.sliceIndices[viewIndex] < maxSlice - 1)
                        {
                            int newSlice = refState.sliceIndices[viewIndex] + 1;
                            for (auto& st : g_ViewStates)
                                st.sliceIndices[viewIndex] = newSlice;
                            dirtyMask |= (1 << viewIndex);
                        }
                    }
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();
    return dirtyMask;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    try
    {

    // --- Parse CLI arguments ---
    // LUT flags (--gray, --hot, etc.) set a pending colour map that is applied
    // to the *next* volume file on the command line.  --lut <name> accepts
    // any colour map name.  The per-volume CLI LUT overrides both global and
    // local config values.
    std::string cliConfigPath;  // -c / --config <path>
    std::vector<std::string> volumeFiles;
    std::vector<std::optional<std::string>> cliLutPerVolume;  // parallel to volumeFiles
    std::optional<std::string> pendingLut;  // set by a LUT flag, consumed by next volume

    // Map of LUT flags to colour map names (long and short forms)
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

        // Check LUT flags (long and short forms)
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

        // Not a recognised option — treat as a volume file path
        volumeFiles.push_back(std::string(arg));
        cliLutPerVolume.push_back(pendingLut);
        pendingLut.reset();
    }

    if (pendingLut.has_value())
    {
        std::cerr << "Warning: LUT flag at end of arguments has no volume to apply to\n";
    }

    // --- Load and merge configs ---
    AppConfig globalCfg;
    try { globalCfg = loadConfig(globalConfigPath()); }
    catch (const std::exception& e)
    {
        std::cerr << "Warning: " << e.what() << "\n";
    }

    // Determine local config path: --config flag takes priority, else ./config.json
    if (!cliConfigPath.empty())
    {
        g_LocalConfigPath = cliConfigPath;
    }
    else if (std::filesystem::exists("config.json"))
    {
        g_LocalConfigPath = "config.json";
    }

    AppConfig localCfg;
    if (!g_LocalConfigPath.empty())
    {
        try { localCfg = loadConfig(g_LocalConfigPath); }
        catch (const std::exception& e)
        {
            std::cerr << "Warning: " << e.what() << "\n";
        }
    }

    AppConfig mergedCfg = mergeConfigs(globalCfg, localCfg);

    // --- Determine which volumes to load ---
    // CLI filenames take priority; if none given, use config's volume list
    if (volumeFiles.empty() && !mergedCfg.volumes.empty())
    {
        for (const auto& vc : mergedCfg.volumes)
        {
            if (!vc.path.empty())
                volumeFiles.push_back(vc.path);
        }
    }

    // Load volumes
    if (!volumeFiles.empty())
    {
        for (const auto& path : volumeFiles)
        {
            try
            {
                Volume vol;
                vol.load(path);
                g_Volumes.push_back(std::move(vol));
                g_VolumePaths.push_back(path);
                g_VolumeNames.push_back(
                    std::filesystem::path(path).filename().string());
            }
            catch (const std::exception& e)
            {
                std::cerr << "Failed to load volume: " << e.what() << "\n";
            }
        }
    }
    else
    {
        Volume vol;
        vol.generate_test_data();
        g_Volumes.push_back(std::move(vol));
        g_VolumePaths.push_back("");
        g_VolumeNames.push_back("Test Data");
    }

    if (g_Volumes.empty())
    {
        std::cerr << "No volumes loaded.\n";
    }

    // Setup GLFW
    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    // Query primary monitor content scale and work area for window sizing
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

    // Size the window based on the number of loaded volumes.
    // Each volume column gets ~400 logical pixels wide; height is 720
    // or 75% of the monitor work area, whichever is smaller.
    int numVols = static_cast<int>(g_Volumes.size());
    if (numVols < 1) numVols = 1;

    constexpr int colWidth  = 200;  // logical pixels per volume column
    constexpr int baseHeight = 480;

    // Add one extra column for overlay when multiple volumes loaded
    int totalCols = numVols + (numVols > 1 ? 1 : 0);
    int initW = static_cast<int>(colWidth * totalCols * initScale);
    int initH = static_cast<int>(baseHeight * initScale);

    // Apply window size from config if available
    if (mergedCfg.global.windowWidth.has_value())
        initW = mergedCfg.global.windowWidth.value();
    if (mergedCfg.global.windowHeight.has_value())
        initH = mergedCfg.global.windowHeight.value();

    // Clamp to 90% of the monitor work area
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

    // Create and initialize graphics backend
    auto backend = GraphicsBackend::createDefault();
    backend->initialize(window);

    backend->initImGui(window);

    // Store DPI scale for all UI size computations
    g_DpiScale = backend->contentScale();

    // Initialize slice views for all volumes
    if (!g_Volumes.empty())
    {
        ResetViews();

        // Apply per-volume config settings
        for (int vi = 0; vi < static_cast<int>(g_Volumes.size()); ++vi)
        {
            VolumeViewState& state = g_ViewStates[vi];
            const Volume& vol = g_Volumes[vi];

            // Find matching VolumeConfig by path
            const VolumeConfig* vc = nullptr;
            for (const auto& v : mergedCfg.volumes)
            {
                if (v.path == g_VolumePaths[vi])
                {
                    vc = &v;
                    break;
                }
            }

            // Apply default colour map from global config
            auto defaultCm = colourMapByName(mergedCfg.global.defaultColourMap);
            if (defaultCm.has_value())
                state.colourMap = defaultCm.value();

            if (vc)
            {
                // Colour map
                auto cm = colourMapByName(vc->colourMap);
                if (cm.has_value())
                    state.colourMap = cm.value();

                // Value range
                if (vc->valueMin.has_value())
                    state.valueRange[0] = vc->valueMin.value();
                if (vc->valueMax.has_value())
                    state.valueRange[1] = vc->valueMax.value();

                // Slice indices (-1 means keep the midpoint default)
                for (int v = 0; v < 3; ++v)
                {
                    if (vc->sliceIndices[v] >= 0)
                    {
                        int maxSlice = (v == 0) ? vol.dimensions[2]
                                     : (v == 1) ? vol.dimensions[0]
                                                 : vol.dimensions[1];
                        state.sliceIndices[v] = std::clamp(
                            vc->sliceIndices[v], 0, maxSlice - 1);
                    }
                }

                // Zoom & pan
                state.zoom[0] = vc->zoom[0];
                state.zoom[1] = vc->zoom[1];
                state.zoom[2] = vc->zoom[2];
                state.panU[0] = vc->panU[0];
                state.panU[1] = vc->panU[1];
                state.panU[2] = vc->panU[2];
                state.panV[0] = vc->panV[0];
                state.panV[1] = vc->panV[1];
                state.panV[2] = vc->panV[2];
            }

            // CLI LUT override takes highest priority
            if (vi < static_cast<int>(cliLutPerVolume.size()) &&
                cliLutPerVolume[vi].has_value())
            {
                auto cm = colourMapByName(cliLutPerVolume[vi].value());
                if (cm.has_value())
                    state.colourMap = cm.value();
            }

            // Re-render textures with applied settings
            UpdateSliceTexture(vi, 0);
            UpdateSliceTexture(vi, 1);
            UpdateSliceTexture(vi, 2);
        }

        // Initialize overlay textures if multiple volumes
        if (g_Volumes.size() > 1)
            UpdateAllOverlayTextures();
    }

    int numVolumes = static_cast<int>(g_Volumes.size());
    bool hasOverlay = numVolumes > 1;

    // Pre-generate window names — one per volume column (file basenames)
    std::vector<std::string> columnNames;
    for (int vi = 0; vi < numVolumes; ++vi)
    {
        columnNames.push_back(g_VolumeNames[vi]);
    }

    // Fixed height for the controls section at the bottom of each column
    const float controlsHeightBase = 160.0f * g_DpiScale;

    // Main loop
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // Resize swap chain if needed
        if (backend->needsSwapchainRebuild())
        {
            int width, height;
            glfwGetFramebufferSize(window, &width, &height);
            if (width > 0 && height > 0)
            {
                backend->rebuildSwapchain(width, height);
            }
        }

        // Start ImGui frame
        backend->imguiNewFrame();
        ImGui::NewFrame();

        // DockSpace with default layout
        ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        // In clean mode, controls are hidden so views use full height
        const float controlsHeight = g_CleanMode ? 0.0f : controlsHeightBase;

        if (!g_LayoutInitialized && numVolumes > 0)
        {
            g_LayoutInitialized = true;

            ImVec2 vpSize = ImGui::GetMainViewport()->Size;

            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId,
                                      ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, vpSize);

            // Split off a narrow left column for the Tools panel
            ImGuiID toolsId, contentId;
            ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left,
                                        0.08f, &toolsId, &contentId);
            ImGui::DockBuilderDockWindow("Tools", toolsId);

            // Layout: split remaining space into columns — one per volume
            // plus an overlay column on the right when multiple volumes.
            int totalColumns = numVolumes + (hasOverlay ? 1 : 0);
            std::vector<ImGuiID> columnIds(totalColumns);
            if (totalColumns == 1)
            {
                columnIds[0] = contentId;
            }
            else
            {
                ImGuiID remaining = contentId;
                for (int ci = 0; ci < totalColumns - 1; ++ci)
                {
                    float fraction = 1.0f /
                        static_cast<float>(totalColumns - ci);
                    ImGuiID leftId, rightId;
                    ImGui::DockBuilderSplitNode(remaining, ImGuiDir_Left,
                                                fraction,
                                                &leftId, &rightId);
                    columnIds[ci] = leftId;
                    remaining = rightId;
                }
                columnIds[totalColumns - 1] = remaining;
            }

            for (int vi = 0; vi < numVolumes; ++vi)
            {
                ImGui::DockBuilderDockWindow(
                    columnNames[vi].c_str(), columnIds[vi]);
            }
            if (hasOverlay)
            {
                ImGui::DockBuilderDockWindow(
                    "Overlay", columnIds[totalColumns - 1]);
            }

            ImGui::DockBuilderFinish(dockspaceId);
        }

        // --- Tools panel (left side) ---
        if (!g_CleanMode)
        {
        ImGui::Begin("Tools");
        {
            float btnWidth = ImGui::GetContentRegionAvail().x;

            if (ImGui::Button("Save Global", ImVec2(btnWidth, 0)))
            {
                try
                {
                    AppConfig cfg;
                    cfg.global.defaultColourMap = "GrayScale";
                    int winW, winH;
                    glfwGetWindowSize(window, &winW, &winH);
                    cfg.global.windowWidth = winW;
                    cfg.global.windowHeight = winH;

                    for (int vi = 0; vi < numVolumes; ++vi)
                    {
                        const VolumeViewState& st = g_ViewStates[vi];
                        VolumeConfig vc;
                        vc.path = g_VolumePaths[vi];
                        vc.colourMap = std::string(
                            colourMapName(st.colourMap));
                        vc.valueMin = st.valueRange[0];
                        vc.valueMax = st.valueRange[1];
                        vc.sliceIndices = {st.sliceIndices[0],
                                           st.sliceIndices[1],
                                           st.sliceIndices[2]};
                        vc.zoom = {st.zoom[0], st.zoom[1], st.zoom[2]};
                        vc.panU = {st.panU[0], st.panU[1], st.panU[2]};
                        vc.panV = {st.panV[0], st.panV[1], st.panV[2]};
                        cfg.volumes.push_back(std::move(vc));
                    }
                    saveConfig(cfg, globalConfigPath());
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Failed to save global config: "
                              << e.what() << "\n";
                }
            }

            if (ImGui::Checkbox("Sync All", &g_SyncCursors))
            {
                // When sync is enabled, set all cursors to match first volume
                if (g_SyncCursors && numVolumes > 1)
                {
                    for (int vi = 1; vi < numVolumes; ++vi)
                    {
                        for (int v = 0; v < 3; ++v)
                        {
                            g_ViewStates[vi].sliceIndices[v] = 
                                g_ViewStates[0].sliceIndices[v];
                        }
                    }
                    UpdateAllOverlayTextures();
                }
                else
                {
                    // When sync is disabled, reset the sync source
                    g_LastSyncSource = 0;
                }
            }

            if (ImGui::Button("Save Local", ImVec2(btnWidth, 0)))
            {
                try
                {
                    AppConfig cfg;
                    cfg.global.defaultColourMap = "GrayScale";
                    int winW, winH;
                    glfwGetWindowSize(window, &winW, &winH);
                    cfg.global.windowWidth = winW;
                    cfg.global.windowHeight = winH;

                    for (int vi = 0; vi < numVolumes; ++vi)
                    {
                        const VolumeViewState& st = g_ViewStates[vi];
                        VolumeConfig vc;
                        vc.path = g_VolumePaths[vi];
                        vc.colourMap = std::string(
                            colourMapName(st.colourMap));
                        vc.valueMin = st.valueRange[0];
                        vc.valueMax = st.valueRange[1];
                        vc.sliceIndices = {st.sliceIndices[0],
                                           st.sliceIndices[1],
                                           st.sliceIndices[2]};
                        vc.zoom = {st.zoom[0], st.zoom[1], st.zoom[2]};
                        vc.panU = {st.panU[0], st.panU[1], st.panU[2]};
                        vc.panV = {st.panV[0], st.panV[1], st.panV[2]};
                        cfg.volumes.push_back(std::move(vc));
                    }

                    std::string savePath = g_LocalConfigPath.empty()
                        ? "config.json" : g_LocalConfigPath;
                    saveConfig(cfg, savePath);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Failed to save local config: "
                              << e.what() << "\n";
                }
            }

            ImGui::Separator();

            if (ImGui::Button("[R] Reset All Views", ImVec2(btnWidth, 0)))
            {
                ResetViews();
                if (hasOverlay)
                    UpdateAllOverlayTextures();
            }

            if (ImGui::Button("[P] Screenshot", ImVec2(btnWidth, 0)))
            {
                saveScreenshot(*backend);
            }

            ImGui::Separator();

            if (ImGui::Button("[C] Clean Mode", ImVec2(btnWidth, 0)))
            {
                g_CleanMode = true;
            }

            ImGui::Separator();

            if (ImGui::Button("[Q] Quit", ImVec2(btnWidth, 0)))
            {
                glfwSetWindowShouldClose(window, true);
            }
        }
        ImGui::End();
        } // !g_CleanMode

        // --- Global keyboard shortcuts ---
        // Only fire when no text input is active
        if (!ImGui::GetIO().WantTextInput)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_R))
            {
                ResetViews();
                if (hasOverlay)
                    UpdateAllOverlayTextures();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Q))
            {
                glfwSetWindowShouldClose(window, true);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_C))
            {
                g_CleanMode = !g_CleanMode;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_P))
            {
                saveScreenshot(*backend);
            }
        }

        // --- Render each volume's column window ---
        for (int vi = 0; vi < numVolumes; ++vi)
        {
            VolumeViewState& state = g_ViewStates[vi];
            const Volume& vol = g_Volumes[vi];

            ImGui::Begin(columnNames[vi].c_str());
            {
                ImVec2 avail = ImGui::GetContentRegionAvail();

                // Divide available height: 3 equal view rows + fixed
                // controls height at the bottom.  The spacing between
                // children is accounted for by ImGui automatically.
                float viewAreaHeight = avail.y - controlsHeight;
                float viewRowHeight  = viewAreaHeight / 3.0f;
                float viewWidth      = avail.x;

                if (viewRowHeight < 40.0f * g_DpiScale)
                    viewRowHeight = 40.0f * g_DpiScale;

                // Three slice views — equal height
                int viewDirtyMask = 0;
                for (int v = 0; v < 3; ++v)
                {
                    viewDirtyMask |= RenderSliceView(vi, v,
                                        ImVec2(viewWidth, viewRowHeight),
                                        vol, state);
                }

                // Apply deferred texture updates from mouse interaction
                for (int v = 0; v < 3; ++v)
                {
                    if (viewDirtyMask & (1 << v))
                    {
                        UpdateSliceTexture(vi, v);
                        if (hasOverlay)
                            UpdateOverlayTexture(v);
                    }
                }

                // Controls section (hidden in clean mode)
                if (!g_CleanMode)
                {
                ImGui::BeginChild("##controls", ImVec2(viewWidth, 0),
                                  ImGuiChildFlags_Borders);
                {
                    ImGui::Text("Dimensions: %d x %d x %d",
                                vol.dimensions[0], vol.dimensions[1],
                                vol.dimensions[2]);
                    ImGui::Text("Voxel size: %.3f x %.3f x %.3f mm",
                                vol.step[0], vol.step[1], vol.step[2]);
                    ImGui::Separator();

                    // Colour map selector: quick-access buttons + dropdown
                    {
                        ImGui::PushID(vi + 1000);

                        // Quick-access colour maps shown as colour swatches
                        static const ColourMapType quickMaps[] = {
                            ColourMapType::GrayScale,
                            ColourMapType::Red,
                            ColourMapType::Green,
                            ColourMapType::Blue,
                            ColourMapType::Spectral,
                        };
                        constexpr int nQuick = sizeof(quickMaps) / sizeof(quickMaps[0]);

                        auto applyColourMap = [&](ColourMapType cmType)
                        {
                            state.colourMap = cmType;
                            UpdateSliceTexture(vi, 0);
                            UpdateSliceTexture(vi, 1);
                            UpdateSliceTexture(vi, 2);
                            if (hasOverlay)
                                UpdateAllOverlayTextures();
                        };

                        const float swatchSize = 24.0f * g_DpiScale;
                        const float borderThickness = 2.0f * g_DpiScale;

                        for (int qi = 0; qi < nQuick; ++qi)
                        {
                            if (qi > 0) ImGui::SameLine();

                            ColourMapType cmType = quickMaps[qi];
                            bool isActive = (state.colourMap == cmType);

                            ImGui::PushID(qi);

                            ImVec2 cursor = ImGui::GetCursorScreenPos();
                            if (ImGui::InvisibleButton("##swatch",
                                    ImVec2(swatchSize, swatchSize)))
                            {
                                applyColourMap(cmType);
                            }

                            ImDrawList* dl = ImGui::GetWindowDrawList();
                            ImVec2 pMin = cursor;
                            ImVec2 pMax(cursor.x + swatchSize,
                                        cursor.y + swatchSize);

                            if (cmType == ColourMapType::Spectral)
                            {
                                // Draw miniature gradient strip
                                const ColourLut& lut =
                                    colourMapLut(ColourMapType::Spectral);
                                int nStrips = static_cast<int>(swatchSize);
                                for (int s = 0; s < nStrips; ++s)
                                {
                                    float t = static_cast<float>(s) /
                                              static_cast<float>(nStrips - 1);
                                    int idx = static_cast<int>(
                                        t * 255.0f + 0.5f);
                                    if (idx > 255) idx = 255;
                                    uint32_t packed = lut.table[idx];
                                    // Convert 0xAABBGGRR to ImGui's ImU32
                                    // (same layout)
                                    float x0 = pMin.x + static_cast<float>(s);
                                    float x1 = x0 + 1.0f;
                                    dl->AddRectFilled(
                                        ImVec2(x0, pMin.y),
                                        ImVec2(x1, pMax.y),
                                        packed);
                                }
                            }
                            else
                            {
                                // Solid colour swatch
                                ColourMapRGBA rep =
                                    colourMapRepresentative(cmType);
                                ImU32 col = ImGui::ColorConvertFloat4ToU32(
                                    ImVec4(rep.r, rep.g, rep.b, 1.0f));
                                dl->AddRectFilled(pMin, pMax, col);
                            }

                            // Border: white for active, dark for inactive
                            if (isActive)
                            {
                                dl->AddRect(
                                    ImVec2(pMin.x - 1, pMin.y - 1),
                                    ImVec2(pMax.x + 1, pMax.y + 1),
                                    IM_COL32(255, 255, 255, 255),
                                    0.0f, 0, borderThickness);
                            }
                            else
                            {
                                dl->AddRect(pMin, pMax,
                                    IM_COL32(80, 80, 80, 255));
                            }

                            // Tooltip with colour map name on hover
                            if (ImGui::IsItemHovered())
                            {
                                ImGui::SetTooltip("%s",
                                    colourMapName(cmType).data());
                            }

                            ImGui::PopID();
                        }

                        // "More..." dropdown for the remaining colour maps
                        ImGui::SameLine();

                        // Check if current map is one not in quickMaps
                        bool currentInQuick = false;
                        for (int qi = 0; qi < nQuick; ++qi)
                        {
                            if (quickMaps[qi] == state.colourMap)
                            {
                                currentInQuick = true;
                                break;
                            }
                        }

                        const char* moreLabel = currentInQuick
                            ? "More..."
                            : colourMapName(state.colourMap).data();

                        if (ImGui::BeginCombo("##more_maps", moreLabel,
                                              ImGuiComboFlags_NoPreview))
                        {
                            for (int cm = 0; cm < colourMapCount(); ++cm)
                            {
                                auto cmType = static_cast<ColourMapType>(cm);

                                // Skip maps already shown as buttons
                                bool isQuick = false;
                                for (int qi = 0; qi < nQuick; ++qi)
                                {
                                    if (quickMaps[qi] == cmType)
                                    {
                                        isQuick = true;
                                        break;
                                    }
                                }
                                if (isQuick) continue;

                                bool selected = (cmType == state.colourMap);
                                if (ImGui::Selectable(
                                        colourMapName(cmType).data(),
                                        selected))
                                {
                                    applyColourMap(cmType);
                                }
                                if (selected)
                                    ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }

                        ImGui::PopID();
                    }
                    ImGui::Separator();

                    bool changed = false;
                    ImGui::PushID(vi);
                    {
                        // Shared lambda for under/over combo.
                        // isUnder: true puts NegR/G/B at the very top.
                        // Both combos put R/G/B right after (or at the top
                        // for "over"), then Current/Transparent, then the rest.
                        auto clampCombo = [&](const char* tooltip,
                                              const char* id,
                                              int& mode,
                                              bool isUnder) -> bool
                        {
                            bool ret = false;
                            if (ImGui::BeginCombo(id,
                                                  clampColourLabel(mode),
                                                  ImGuiComboFlags_None))
                            {
                                // Helper to emit one selectable
                                auto item = [&](const char* label,
                                                int value) -> void
                                {
                                    if (ImGui::Selectable(label, mode == value))
                                    {
                                        mode = value;
                                        ret = true;
                                    }
                                };

                                // Helper to emit a colour map selectable
                                auto cmItem = [&](ColourMapType cm) -> void
                                {
                                    int idx = static_cast<int>(cm);
                                    item(colourMapName(cm).data(), idx);
                                };

                                // Under combo: NegR/G/B first
                                if (isUnder)
                                {
                                    cmItem(ColourMapType::NegRed);
                                    cmItem(ColourMapType::NegGreen);
                                    cmItem(ColourMapType::NegBlue);
                                    ImGui::Separator();
                                }

                                // Both: R/G/B next
                                cmItem(ColourMapType::Red);
                                cmItem(ColourMapType::Green);
                                cmItem(ColourMapType::Blue);
                                ImGui::Separator();

                                // Current / Transparent
                                item("Current", kClampCurrent);
                                item("Transparent", kClampTransparent);
                                ImGui::Separator();

                                // Remaining colour maps (skip those already shown)
                                for (int cm = 0; cm < colourMapCount(); ++cm)
                                {
                                    auto cmt = static_cast<ColourMapType>(cm);
                                    if (cmt == ColourMapType::Red ||
                                        cmt == ColourMapType::Green ||
                                        cmt == ColourMapType::Blue ||
                                        cmt == ColourMapType::NegRed ||
                                        cmt == ColourMapType::NegGreen ||
                                        cmt == ColourMapType::NegBlue)
                                        continue;
                                    cmItem(cmt);
                                }
                                ImGui::EndCombo();
                            }
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("%s", tooltip);
                            return ret;
                        };

                        float avail = ImGui::GetContentRegionAvail().x;
                        float spacing = ImGui::GetStyle().ItemSpacing.x;
                        float autoW = ImGui::CalcTextSize("Auto").x +
                                      ImGui::GetStyle().FramePadding.x * 2.0f;
                        // Reserve width for the two clamp combos (equal size)
                        float clampW = ImGui::CalcTextSize("Current__").x +
                                       ImGui::GetStyle().FramePadding.x * 2.0f;
                        float inputTotal = avail - autoW - clampW * 2.0f
                                           - spacing * 4.0f;
                        float inputW = inputTotal * 0.5f;
                        if (inputW < 30.0f) inputW = 30.0f;

                        // Layout: [under] [min] [Auto] [max] [over]
                        ImGui::SetNextItemWidth(clampW);
                        if (clampCombo("Under colour", "##under",
                                       state.underColourMode, true))
                            changed = true;
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(inputW);
                        if (ImGui::InputFloat("##min", &state.valueRange[0],
                                              0.0f, 0.0f, "%.4g"))
                            changed = true;
                        ImGui::SameLine();
                        if (ImGui::Button("Auto"))
                        {
                            state.valueRange[0] = vol.min_value;
                            state.valueRange[1] = vol.max_value;
                            changed = true;
                        }
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(inputW);
                        if (ImGui::InputFloat("##max", &state.valueRange[1],
                                              0.0f, 0.0f, "%.4g"))
                            changed = true;
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(clampW);
                        if (clampCombo("Over colour", "##over",
                                       state.overColourMode, false))
                            changed = true;
                    }
                    ImGui::PopID();

                    if (changed)
                    {
                        UpdateSliceTexture(vi, 0);
                        UpdateSliceTexture(vi, 1);
                        UpdateSliceTexture(vi, 2);
                        if (hasOverlay)
                            UpdateAllOverlayTextures();
                    }

                    // Reset View button — restores zoom/pan to defaults
                    if (ImGui::Button("Reset View"))
                    {
                        for (int v = 0; v < 3; ++v)
                        {
                            state.zoom[v] = 1.0f;
                            state.panU[v] = 0.5f;
                            state.panV[v] = 0.5f;
                        }
                    }
                }
                ImGui::EndChild();
                } // !g_CleanMode
            }
            ImGui::End();
        }

        // --- Overlay panel (visible when more than one volume loaded) ---
        if (hasOverlay)
        {
            ImGui::Begin("Overlay");
            {
                ImVec2 avail = ImGui::GetContentRegionAvail();
                // Use same controlsHeight as volume columns for alignment
                float viewAreaHeight = avail.y - controlsHeight;
                float viewRowHeight = viewAreaHeight / 3.0f;
                if (viewRowHeight < 40.0f * g_DpiScale)
                    viewRowHeight = 40.0f * g_DpiScale;

                int overlayDirtyMask = 0;
                for (int v = 0; v < 3; ++v)
                    overlayDirtyMask |= RenderOverlayView(v,
                        ImVec2(avail.x, viewRowHeight));

                // When overlay interaction changes slices, update all
                // individual volume textures AND overlay textures
                if (overlayDirtyMask)
                {
                    for (int vi = 0; vi < numVolumes; ++vi)
                        for (int v = 0; v < 3; ++v)
                            if (overlayDirtyMask & (1 << v))
                                UpdateSliceTexture(vi, v);
                    for (int v = 0; v < 3; ++v)
                        if (overlayDirtyMask & (1 << v))
                            UpdateOverlayTexture(v);
                }

                // Controls: blending and per-volume alpha (hidden in clean mode)
                if (!g_CleanMode)
                {
                ImGui::BeginChild("##overlay_controls",
                                  ImVec2(avail.x, 0),
                                  ImGuiChildFlags_Borders);
                {
                    bool alphaChanged = false;

                    if (numVolumes == 2)
                    {
                        // Special case: single blend slider between two volumes.
                        // blendT=0 means 100% volume 0, blendT=1 means 100% volume 1.
                        // We derive blendT from the current alpha values and
                        // write back complementary alphas when the slider moves.
                        float a0 = g_ViewStates[0].overlayAlpha;
                        float a1 = g_ViewStates[1].overlayAlpha;
                        float blendT = (a0 + a1 > 0.0f)
                                       ? a1 / (a0 + a1)
                                       : 0.5f;

                        ImGui::Text("%s", g_VolumeNames[0].c_str());
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(
                            ImGui::GetContentRegionAvail().x
                            - ImGui::CalcTextSize(g_VolumeNames[1].c_str()).x
                            - ImGui::GetStyle().ItemSpacing.x);
                        if (ImGui::SliderFloat("##blend", &blendT,
                                               0.0f, 1.0f, "%.2f"))
                        {
                            g_ViewStates[0].overlayAlpha = 1.0f - blendT;
                            g_ViewStates[1].overlayAlpha = blendT;
                            alphaChanged = true;
                        }
                        ImGui::SameLine();
                        ImGui::Text("%s", g_VolumeNames[1].c_str());
                    }
                    else
                    {
                        // General case: independent alpha slider per volume
                        for (int vi = 0; vi < numVolumes; ++vi)
                        {
                            ImGui::PushID(vi + 2000);
                            ImGui::Text("%s", g_VolumeNames[vi].c_str());
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(
                                ImGui::GetContentRegionAvail().x);
                            if (ImGui::SliderFloat("##alpha",
                                    &g_ViewStates[vi].overlayAlpha,
                                    0.0f, 1.0f, "%.2f"))
                                alphaChanged = true;
                            ImGui::PopID();
                        }
                    }

                    if (alphaChanged)
                        UpdateAllOverlayTextures();

                    if (ImGui::Button("Reset View"))
                    {
                        for (int v = 0; v < 3; ++v)
                        {
                            g_Overlay.zoom[v] = 1.0f;
                            g_Overlay.panU[v] = 0.5f;
                            g_Overlay.panV[v] = 0.5f;
                        }
                    }
                }
                ImGui::EndChild();
                } // !g_CleanMode
            }
            ImGui::End();
        }

        // Sync cursor positions across volumes if enabled
        if (g_SyncCursors)
            SyncCursors();

        // Rendering
        ImGui::Render();
        backend->endFrame();
    }

    // Cleanup: wait for GPU, destroy textures (while ImGui is still alive),
    // then shut down ImGui, then shut down the backend.
    backend->waitIdle();

    // Destroy overlay textures
    for (int i = 0; i < 3; ++i)
    {
        if (g_Overlay.textures[i])
        {
            VulkanHelpers::DestroyTexture(g_Overlay.textures[i]);
            g_Overlay.textures[i] = nullptr;
        }
    }

    for (auto& state : g_ViewStates)
    {
        for (int i = 0; i < 3; ++i)
        {
            if (state.sliceTextures[i])
            {
                VulkanHelpers::DestroyTexture(state.sliceTextures[i]);
                state.sliceTextures[i] = nullptr;
            }
        }
    }

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
