#pragma once

#include <vector>
#include <string>
#include <array>
#include <unordered_map>
#include <stdexcept>

#include <glm/glm.hpp>

#include "TagWrapper.hpp"

struct LabelInfo {
    uint8_t r = 0, g = 0, b = 0, a = 255;
    bool visible = true;
    std::string name;
};

/// Linear index of voxel (x, y, z) in Volume::data (x varies fastest).
/// Computed in size_t: int arithmetic overflows past INT_MAX voxels.
inline size_t voxelIndex(const glm::ivec3& dims, int x, int y, int z)
{
    return (static_cast<size_t>(z) * static_cast<size_t>(dims.y) + static_cast<size_t>(y)) *
               static_cast<size_t>(dims.x) +
           static_cast<size_t>(x);
}

class Volume {
public:
    glm::ivec3 dimensions{0, 0, 0};  // X, Y, Z voxel counts

    /// Voxel spacing in mm along each axis (always positive: negative file
    /// steps are normalised at load time).  Index 0 = X, 1 = Y, 2 = Z.
    glm::dvec3 step{1.0, 1.0, 1.0};

    /// World coordinate of the first voxel along each axis.
    glm::dvec3 start{0.0, 0.0, 0.0};

    /// Direction cosines per axis (unit vectors in world space).
    /// dirCos[i] is a 3-element vector for axis i.
    glm::dmat3 dirCos{1.0, 0.0, 0.0,
                      0.0, 1.0, 0.0,
                      0.0, 0.0, 1.0};

    std::vector<float> data;
    float min_value = 0.0f;
    float max_value = 1.0f;

    /// 4x4 transformation matrix from voxel coordinates to world coordinates.
    /// Integer voxel indices are voxel centres (MINC convention):
    /// world = dirCos * (start + (i,j,k) * step), with no half-voxel offset.
    glm::dmat4 voxelToWorld{1.0};  // Identity by default

    /// Inverse matrix: world coordinates to voxel coordinates.
    glm::dmat4 worldToVoxel{1.0};  // Identity by default

    /// Load a MINC2 volume from disk.
    /// @throws std::runtime_error on any failure (file not found, bad format, etc.)
    void load(const std::string& filename);
    float get(int x, int y, int z) const;

    /// Single quantile; same as computeQuantiles({q})[0].
    float computeQuantile(double q) const;

    /// Several quantiles from one copy of the data. result[i] corresponds to
    /// qs[i] (input order kept; duplicates and unsorted input allowed). q is
    /// clamped to [0,1]; a NaN q throws std::invalid_argument. Non-finite
    /// voxels are ignored; if there are no finite voxels every result is 0.
    /// An empty qs returns an empty vector without touching the data.
    std::vector<float> computeQuantiles(const std::vector<double>& qs) const;

    /// Unchecked voxel access — caller guarantees x,y,z are in bounds.
    /// Avoids the branch overhead of get() in hot loops.
    float getUnchecked(int x, int y, int z) const
    {
        return data[z * dimensions.y * dimensions.x + y * dimensions.x + x];
    }
    void generate_test_data();

    /// Return the physical (world-space) size of the volume along each
    /// axis:  |step[i]| * dimensions[i].
    void worldExtent(glm::dvec3& extent) const;

    /// Compute the pixel aspect ratio for a 2D slice view whose two
    /// in-plane axes are axisU and axisV.  Returns |step[axisU]| /
    /// |step[axisV]|, i.e. the width of one pixel relative to its height.
    double slicePixelAspect(int axisU, int axisV) const;
    
    /// Transform voxel coordinates (integers) to world coordinates using the
    /// precomputed voxel-to-world matrix.
    /// Both voxel and world use MINC order: .x = X, .y = Y, .z = Z.
    void transformVoxelToWorld(const glm::ivec3& voxel, glm::dvec3& world) const;
    
    /// Transform world coordinates to voxel indices using the precomputed
    /// world-to-voxel matrix. Result is rounded to nearest integer.
    /// Both world and voxel use MINC order: .x = X, .y = Y, .z = Z.
    /// Returns true if the point is within volume bounds, false otherwise.
    bool transformWorldToVoxel(const glm::dvec3& world, glm::ivec3& voxel) const;

    /// Tag management methods
    void loadTags(const std::string& path);
    void saveTags(const std::string& path);
    void clearTags();
    const std::vector<glm::dvec3>& getTagPoints() const { return tags.points(); }
    const std::vector<std::string>& getTagLabels() const { return tags.labels(); }
    int getTagCount() const { return tags.tagCount(); }
    bool hasTags() const { return tags.hasTags(); }

    TagWrapper tags;

    bool isLabelVolume() const { return isLabelVolume_; }
    void setLabelVolume(bool val) { isLabelVolume_ = val; invalidateLabelIdCache(); }
    void setLabelDescriptionFile(const std::string& path);
    const std::string& labelDescriptionFile() const { return labelDescriptionFile_; }
    const std::unordered_map<int, LabelInfo>& getLabelLUT() const { return labelLUT_; }
    const LabelInfo* getLabelInfo(int labelId) const;

    /// Sorted, non-zero label IDs present in `data` (empty unless isLabelVolume()).
    /// Computed on first call and cached. load(), generate_test_data() and
    /// setLabelVolume() reset the cache; call invalidateLabelIdCache() after
    /// writing `data` directly. Not thread-safe — call from the main thread.
    const std::vector<int>& getUniqueLabelIds() const;
    /// Label ID -> its position in getUniqueLabelIds(); cached with it.
    const std::unordered_map<int, int>& getLabelIndexMap() const;
    void invalidateLabelIdCache()
    {
        uniqueLabelIds_.clear();
        labelIndexMap_.clear();
        uniqueLabelIdsValid_ = false;
    }
    std::string getLabelNameAtVoxel(int x, int y, int z) const;

private:
    bool isLabelVolume_ = false;
    std::string labelDescriptionFile_;
    std::unordered_map<int, LabelInfo> labelLUT_;
    mutable std::vector<int> uniqueLabelIds_;
    mutable std::unordered_map<int, int> labelIndexMap_;
    mutable bool uniqueLabelIdsValid_ = false;

public:
    void loadLabelDescriptionFile(const std::string& path);
};
