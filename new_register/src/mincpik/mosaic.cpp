/// mosaic.cpp — Mosaic layout and slice manipulation utilities for new_mincpik.

#include "mosaic.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <string>

std::vector<double> parseDoubleList(const std::string& str)
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

std::vector<float> parseFloatList(const std::string& str)
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

int worldToSliceVoxel(const Volume& vol, int viewIndex, double worldCoord)
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

std::vector<int> evenlySpacedSlices(const Volume& vol, int viewIndex, int count)
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

void viewAxes(int viewIndex, int& axisU, int& axisV)
{
    if (viewIndex == 0)      { axisU = 0; axisV = 1; }
    else if (viewIndex == 1) { axisU = 1; axisV = 2; }
    else                     { axisU = 0; axisV = 2; }
}

RenderedSlice resampleToPhysicalAspect(
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

void blitSlice(
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
