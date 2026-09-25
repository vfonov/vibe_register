#include "Volume.h"
#include "DebugFlag.h"
#include "NiftiVolume.h"
#include "VolumeReorient.h"
#include <array>
#include <cmath>
#include <vector>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <unordered_set>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

// Include minc2-simple header
#include <minc2-simple.h>

// RAII wrapper for minc2 file handles — ensures close+free on scope exit.
class Minc2Handle
{
public:
    Minc2Handle()
    {
        if (minc2_allocate(&h_) != MINC2_SUCCESS)
            throw std::runtime_error("Failed to allocate minc2 handle");
    }

    ~Minc2Handle()
    {
        if (opened_) minc2_close(h_);
        minc2_free(h_);
    }

    void open(const std::string& filename)
    {
        if (minc2_open(h_, filename.c_str()) != MINC2_SUCCESS)
            throw std::runtime_error("Failed to open file: " + filename);
        opened_ = true;
    }

    minc2_file_handle get() const { return h_; }

    Minc2Handle(const Minc2Handle&) = delete;
    Minc2Handle& operator=(const Minc2Handle&) = delete;
    Minc2Handle(Minc2Handle&& other) noexcept : h_(other.h_), opened_(other.opened_) {
        other.h_ = nullptr;
        other.opened_ = false;
    }
    Minc2Handle& operator=(Minc2Handle&& other) noexcept {
        if (this != &other) {
            if (opened_) minc2_close(h_);
            minc2_free(h_);
            h_ = other.h_;
            opened_ = other.opened_;
            other.h_ = nullptr;
            other.opened_ = false;
        }
        return *this;
    }

private:
    minc2_file_handle h_ = nullptr;
    bool opened_ = false;
};

namespace
{

// Build voxelToWorld / worldToVoxel from vol.dirCos, vol.step and vol.start.
// MINC: world = dirCos * diag(step) * voxel + dirCos * start
// start[i] is along dimension i's axis, so the world translation = dirCos * start.
void buildVoxelToWorld(Volume& vol)
{
    glm::dmat3 dirCos3(vol.dirCos[0][0], vol.dirCos[0][1], vol.dirCos[0][2],
                       vol.dirCos[1][0], vol.dirCos[1][1], vol.dirCos[1][2],
                       vol.dirCos[2][0], vol.dirCos[2][1], vol.dirCos[2][2]);

    glm::dvec3 scale(vol.step.x, vol.step.y, vol.step.z);
    // start[i] is a coordinate along dimension i's own axis; rotate into world space.
    // Formula from minc2-simple geo.py: aff[0:3,3] = dir_cos @ start
    glm::dvec3 trans = dirCos3 * glm::dvec3(vol.start.x, vol.start.y, vol.start.z);

    // dirCos * diag(scale): scale column i by scale[i] (scalar), not element-wise by scale vector.
    glm::dmat3 affine = dirCos3;
    for (int i = 0; i < 3; ++i)
        affine[i] *= scale[i];

    // For 4x4 matrix: world = affine * voxel + start
    vol.voxelToWorld = glm::dmat4(
        glm::dvec4(affine[0], 0.0),
        glm::dvec4(affine[1], 0.0),
        glm::dvec4(affine[2], 0.0),
        glm::dvec4(trans, 1.0)
    );

    // Compute inverse for world-to-voxel
    // world = affine * voxel + start
    // world - start = affine * voxel
    // voxel = affine^-1 * (world - start)
    vol.worldToVoxel = glm::inverse(vol.voxelToWorld);
}

// Calculate min/max for visualization, ignoring non-finite voxels
// (NaN, +inf, -inf) so they don't distort the displayed range.
// The renderer maps non-finite values to under/over colours instead.
void computeValueRange(Volume& vol, const std::string& filename)
{
    vol.min_value =  std::numeric_limits<float>::infinity();
    vol.max_value = -std::numeric_limits<float>::infinity();
    size_t nonFiniteCount = 0;
    for (float v : vol.data)
    {
        if (!std::isfinite(v))
        {
            ++nonFiniteCount;
            continue;
        }
        if (v < vol.min_value) vol.min_value = v;
        if (v > vol.max_value) vol.max_value = v;
    }

    if (nonFiniteCount > 0 && debugLoggingEnabled())
        std::cerr << "Volume " << filename << ": " << nonFiniteCount
                  << " non-finite voxel(s) ignored when computing range; "
                  << "they will render as under/over colour\n";

    if (!std::isfinite(vol.min_value) || !std::isfinite(vol.max_value) ||
        vol.min_value >= vol.max_value)
    {
        vol.min_value = 0.0f;
        vol.max_value = 1.0f;
    }
}

// Copy of the finite voxels (NaN / +-inf dropped: nth_element on NaN is undefined).
std::vector<float> finiteValues(const std::vector<float>& data)
{
    std::vector<float> out(data.size());
    size_t count = 0;
    for (float v : data)
    {
        if (std::isfinite(v))
            out[count++] = v;
    }
    out.resize(count);
    return out;
}

// Place the order statistic for every target in [first, last) at its index in v,
// using one nth_element per target over a shrinking range: O(n log m).
// Precondition: targets sorted, unique, and all within [lo, hi).
void selectOrderStatistics(std::vector<float>& v, size_t lo, size_t hi,
                           const size_t* first, const size_t* last)
{
    if (first == last)
        return;
    using Diff = std::vector<float>::difference_type;
    const size_t* mid = first + (last - first) / 2;
    std::nth_element(v.begin() + static_cast<Diff>(lo), v.begin() + static_cast<Diff>(*mid),
                     v.begin() + static_cast<Diff>(hi));
    // Everything before *mid is <= v[*mid], everything after is >=.
    selectOrderStatistics(v, lo, *mid, first, mid);
    selectOrderStatistics(v, *mid + 1, hi, mid + 1, last);
}

}  // namespace

void Volume::generate_test_data()
{
    invalidateLabelIdCache();

    dimensions = glm::ivec3(256, 256, 256);

    step = glm::dvec3(1.0, 1.0, 1.0);

    start = glm::dvec3(-128.0, -128.0, -128.0);

    dirCos = glm::dmat3(1.0, 0.0, 0.0,
                        0.0, 1.0, 0.0,
                        0.0, 0.0, 1.0);

    data.resize(256 * 256 * 256);

    min_value = 0.0f;
    max_value = 1.0f;

    for (int z = 0; z < 256; ++z)
    {
        for (int y = 0; y < 256; ++y)
        {
            for (int x = 0; x < 256; ++x)
            {
                float val = static_cast<float>(x) / 256.0f;
                // Grid lines
                if (z % 32 == 0 || y % 32 == 0 || x % 32 == 0)
                    val = 0.8f;

                // Sphere in center
                float dx = x - 128.0f;
                float dy = y - 128.0f;
                float dz = z - 128.0f;
                if (std::sqrt(dx * dx + dy * dy + dz * dz) < 60.0f)
                    val = 1.0f;

                data[z * 256 * 256 + y * 256 + x] = val;
            }
        }
    }

}

void Volume::load(const std::string& filename)
{
    if (filename.empty())
        throw std::runtime_error("Empty filename provided");

    invalidateLabelIdCache();

    // Detect NIfTI files by extension
    if (isNiftiFile(filename)) {
        loadNiftiFile(filename, *this);
        computeValueRange(*this, filename);
        return;
    }

    Minc2Handle h;
    h.open(filename);

    int ndim = 0;
    if (minc2_ndim(h.get(), &ndim) != MINC2_SUCCESS)
        throw std::runtime_error("Failed to get number of dimensions: " + filename);

    struct minc2_dimension *storeDims = nullptr;
    if (minc2_get_store_dimensions(h.get(), &storeDims) != MINC2_SUCCESS)
        throw std::runtime_error("Failed to get dimension info: " + filename);

    // Fast path: plain 3-D x/y/z volume. Read in file storage order and reorient
    // ourselves; libminc's apparent-order read (setup_standard_order) permutes
    // through a slow generic path.
    std::array<int, 3> fileAxisToSpatial{-1, -1, -1};
    bool fileOrderPath = (ndim == 3);
    for (int j = 0; fileOrderPath && j < 3; ++j)
    {
        switch (storeDims[j].id)
        {
        case MINC2_DIM_X: fileAxisToSpatial[j] = 0; break;
        case MINC2_DIM_Y: fileAxisToSpatial[j] = 1; break;
        case MINC2_DIM_Z: fileAxisToSpatial[j] = 2; break;
        default: fileOrderPath = false; break;
        }
    }
    if (fileOrderPath)
    {
        std::array<bool, 3> seen{false, false, false};
        for (int j = 0; j < 3; ++j)
        {
            if (seen[fileAxisToSpatial[j]]) fileOrderPath = false;
            else seen[fileAxisToSpatial[j]] = true;
        }
    }

    if (fileOrderPath)
    {
        // storeDims[j] is file axis j, with j = 0 the fastest-varying
        // (minc2-simple stores store_dims[ndims-i-1] = file_dims[i]).
        std::array<int, 3> fileDims{0, 0, 0};
        std::array<bool, 3> flip{false, false, false};
        for (int j = 0; j < 3; ++j)
        {
            const minc2_dimension& fd = storeDims[j];
            int axis = fileAxisToSpatial[j];
            fileDims[j]     = fd.length;
            dimensions[axis] = fd.length;
            step[axis]       = fd.step;
            start[axis]      = fd.start;

            if (fd.have_dir_cos)
            {
                dirCos[axis][0] = fd.dir_cos[0];
                dirCos[axis][1] = fd.dir_cos[1];
                dirCos[axis][2] = fd.dir_cos[2];
            }
            else
            {
                // Default direction cosines (identity)
                dirCos[axis][0] = (axis == 0) ? 1.0 : 0.0;
                dirCos[axis][1] = (axis == 1) ? 1.0 : 0.0;
                dirCos[axis][2] = (axis == 2) ? 1.0 : 0.0;
            }

            if (step[axis] < 0.0)
            {
                // Same normalisation minc2_setup_standard_order applies (MI_POSITIVE):
                // start moves to the far end, step becomes positive, data is mirrored.
                start[axis] += step[axis] * (fd.length - 1);
                step[axis]   = -step[axis];
                flip[axis]   = true;
            }
        }
        buildVoxelToWorld(*this);

        size_t total_voxels = static_cast<size_t>(fileDims[0]) * fileDims[1] * fileDims[2];
        if (total_voxels == 0)
            throw std::runtime_error("Volume has 0 voxels: " + filename);

        if (isIdentityReorient(fileAxisToSpatial, flip))
        {
            data.resize(total_voxels);
            if (minc2_load_complete_volume(h.get(), data.data(), MINC2_FLOAT) != MINC2_SUCCESS)
                throw std::runtime_error("Failed to load volume data: " + filename);
        }
        else
        {
            std::vector<float> fileOrder(total_voxels);
            if (minc2_load_complete_volume(h.get(), fileOrder.data(), MINC2_FLOAT) != MINC2_SUCCESS)
                throw std::runtime_error("Failed to load volume data: " + filename);
            data = reorientToStandard(fileOrder.data(), fileDims, fileAxisToSpatial, flip);
        }
    }
    else
    {
        // Vector-valued (e.g. RGB) volumes would be interleaved in `data`.
        for (int i = 0; i < ndim; ++i)
        {
            if (storeDims[i].id == MINC2_DIM_VEC)
                throw std::runtime_error("Volumes with a vector dimension (" +
                                         std::to_string(storeDims[i].length) +
                                         " components) are not supported: " + filename);
        }

        if (minc2_setup_standard_order(h.get()) != MINC2_SUCCESS)
            throw std::runtime_error("Failed to setup standard dimension order: " + filename);

        struct minc2_dimension *dims = nullptr;
        if (minc2_get_representation_dimensions(h.get(), &dims) != MINC2_SUCCESS)
            throw std::runtime_error("Failed to get dimension info: " + filename);

        // Find X, Y, Z dimension indices
        glm::ivec3 dim_indices{-1, -1, -1};

        for (int i = 0; i < ndim; ++i)
        {
            if (dims[i].id == MINC2_DIM_X) dim_indices[0] = i;
            else if (dims[i].id == MINC2_DIM_Y) dim_indices[1] = i;
            else if (dims[i].id == MINC2_DIM_Z) dim_indices[2] = i;
        }

        if (dim_indices[0] == -1 || dim_indices[1] == -1 || dim_indices[2] == -1)
            throw std::runtime_error("Could not find X, Y, and Z dimensions: " + filename);

        // Extract dimension sizes and spatial metadata
        for (int axis = 0; axis < 3; ++axis)
        {
            int di = dim_indices[axis];
            dimensions[axis] = dims[di].length;
            step[axis]       = dims[di].step;
            start[axis]      = dims[di].start;

            if (dims[di].have_dir_cos)
            {
                dirCos[axis][0] = dims[di].dir_cos[0];
                dirCos[axis][1] = dims[di].dir_cos[1];
                dirCos[axis][2] = dims[di].dir_cos[2];
            }
            else
            {
                // Default direction cosines (identity)
                dirCos[axis][0] = (axis == 0) ? 1.0 : 0.0;
                dirCos[axis][1] = (axis == 1) ? 1.0 : 0.0;
                dirCos[axis][2] = (axis == 2) ? 1.0 : 0.0;
            }
        }

        buildVoxelToWorld(*this);

        size_t total_voxels = 1;
        for (int i = 0; i < ndim; ++i)
        {
            total_voxels *= dims[i].length;
        }

        if (total_voxels == 0)
            throw std::runtime_error("Volume has 0 voxels: " + filename);

        data.resize(total_voxels);  // std::bad_alloc propagates naturally

        if (minc2_load_complete_volume(h.get(), data.data(), MINC2_FLOAT) != MINC2_SUCCESS)
            throw std::runtime_error("Failed to load volume data: " + filename);

        // Standard order puts time slowest, so frame 0 comes first; keep
        // only that frame (as the NIfTI loader does).
        size_t frameVoxels = static_cast<size_t>(dimensions.x) * dimensions.y * dimensions.z;
        if (data.size() > frameVoxels)
        {
            data.resize(frameVoxels);
            data.shrink_to_fit();
        }
    }

    computeValueRange(*this, filename);
}

float Volume::get(int x, int y, int z) const
{
    if (x < 0 || x >= dimensions.x ||
        y < 0 || y >= dimensions.y ||
        z < 0 || z >= dimensions.z) return 0.0f;

    return data[voxelIndex(dimensions, x, y, z)];
}

float Volume::computeQuantile(double q) const
{
    return computeQuantiles({q})[0];
}

std::vector<float> Volume::computeQuantiles(const std::vector<double>& qs) const
{
    for (double q : qs)
    {
        if (std::isnan(q))
            throw std::invalid_argument("Volume::computeQuantiles: quantile is NaN");
    }
    if (qs.empty())
        return {};

    std::vector<float> tmp = finiteValues(data);
    if (tmp.empty())
        return std::vector<float>(qs.size(), 0.0f);

    std::vector<size_t> idx(qs.size());
    for (size_t i = 0; i < qs.size(); ++i)
        idx[i] = static_cast<size_t>(std::clamp(qs[i], 0.0, 1.0) *
                                     static_cast<double>(tmp.size() - 1));

    std::vector<size_t> targets = idx;
    std::sort(targets.begin(), targets.end());
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    selectOrderStatistics(tmp, 0, tmp.size(),
                          targets.data(), targets.data() + targets.size());

    std::vector<float> result(qs.size());
    for (size_t i = 0; i < qs.size(); ++i)
        result[i] = tmp[idx[i]];
    return result;
}

void Volume::worldExtent(glm::dvec3& extent) const
{
    extent.x = std::abs(step.x) * dimensions.x;
    extent.y = std::abs(step.y) * dimensions.y;
    extent.z = std::abs(step.z) * dimensions.z;
}

double Volume::slicePixelAspect(int axisU, int axisV) const
{
    double su = std::abs(step[axisU]);
    double sv = std::abs(step[axisV]);
    if (sv < 1e-12) return 1.0;
    return su / sv;
}

void Volume::transformVoxelToWorld(const glm::ivec3& voxel, glm::dvec3& world) const
{
    glm::dvec4 v(static_cast<double>(voxel.x), static_cast<double>(voxel.y), static_cast<double>(voxel.z), 1.0);
    glm::dvec4 w = voxelToWorld * v;
    world.x = w.x;
    world.y = w.y;
    world.z = w.z;
}

bool Volume::transformWorldToVoxel(const glm::dvec3& world, glm::ivec3& voxel) const
{
    glm::dvec4 w(world.x, world.y, world.z, 1.0);
    glm::dvec4 v = worldToVoxel * w;
    voxel.x = static_cast<int>(std::round(v.x));
    voxel.y = static_cast<int>(std::round(v.y));
    voxel.z = static_cast<int>(std::round(v.z));
    
    bool inBounds = voxel.x >= 0 && voxel.x < dimensions.x &&
                    voxel.y >= 0 && voxel.y < dimensions.y &&
                    voxel.z >= 0 && voxel.z < dimensions.z;
    
    voxel.x = std::clamp(voxel.x, 0, dimensions.x - 1);
    voxel.y = std::clamp(voxel.y, 0, dimensions.y - 1);
    voxel.z = std::clamp(voxel.z, 0, dimensions.z - 1);
    
    return inBounds;
}

void Volume::loadTags(const std::string& path) {
    tags.load(path);
}

void Volume::saveTags(const std::string& path) {
    tags.save(path);
}

void Volume::clearTags() {
    tags.clear();
}

void Volume::loadLabelDescriptionFile(const std::string& path) {
    labelDescriptionFile_ = path;
    labelLUT_.clear();

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Warning: Could not open label description file: " << path << "\n";
        return;
    }

    std::string line;
    int lineNum = 0;
    while (std::getline(file, line)) {
        ++lineNum;
        if (line.empty()) continue;

        // Skip comment lines
        size_t firstNonSpace = line.find_first_not_of(" \t");
        if (firstNonSpace == std::string::npos) continue;
        if (line[firstNonSpace] == '#') continue;

        // ITK-SNAP format: IDX R G B A VIS MSH "LABEL" (tabs or spaces).
        // The label name is quoted and may contain spaces.
        std::istringstream iss(line);
        std::string token;
        std::vector<std::string> tokens;
        while (tokens.size() < 7 && iss >> token) {
            tokens.push_back(token);
        }
        std::string name;
        if (tokens.size() == 7) {
            iss >> std::ws;
            if (iss.peek() == '"')
                iss >> std::quoted(name);
            else
                iss >> name;
        }

        if (tokens.size() < 5) {
            std::cerr << "Warning: Line " << lineNum << " in " << path << " has fewer than 5 columns, skipping\n";
            continue;
        }

        try {
            auto toByte = [](double v) {
                return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0, 255.0)));
            };
            int labelId = static_cast<int>(std::lround(std::stod(tokens[0])));
            LabelInfo info;
            info.r = toByte(std::stod(tokens[1]));
            info.g = toByte(std::stod(tokens[2]));
            info.b = toByte(std::stod(tokens[3]));
            // ITK-SNAP stores opacity as 0..1; values above 1 are 0..255.
            double alpha = std::stod(tokens[4]);
            info.a = toByte(alpha <= 1.0 ? alpha * 255.0 : alpha);

            if (tokens.size() >= 6) {
                info.visible = (std::stoi(tokens[5]) != 0);
            }

            // Column 7 (mesh visibility) is not used.
            info.name = name;

            labelLUT_[labelId] = info;
        } catch (const std::exception& e) {
            std::cerr << "Warning: Line " << lineNum << " in " << path << " parse error: " << e.what() << "\n";
        }
    }

    file.close();
    std::cout << "Loaded " << labelLUT_.size() << " label definitions from " << path << "\n";
}

void Volume::setLabelDescriptionFile(const std::string& path) {
    if (!path.empty()) {
        loadLabelDescriptionFile(path);
    }
}

const LabelInfo* Volume::getLabelInfo(int labelId) const {
    auto it = labelLUT_.find(labelId);
    if (it != labelLUT_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::string Volume::getLabelNameAtVoxel(int x, int y, int z) const {
    if (!isLabelVolume_ || dimensions.x == 0) {
        return "";
    }

    if (x < 0 || x >= dimensions.x || y < 0 || y >= dimensions.y || z < 0 || z >= dimensions.z) {
        return "";
    }

    int labelId = static_cast<int>(getUnchecked(x, y, z) + 0.5f);
    const LabelInfo* info = getLabelInfo(labelId);
    if (info && !info->name.empty()) {
        return info->name;
    }
    return "";
}

const std::vector<int>& Volume::getUniqueLabelIds() const {
    if (uniqueLabelIdsValid_) {
        return uniqueLabelIds_;
    }
    uniqueLabelIds_.clear();
    if (isLabelVolume_ && !data.empty()) {
        std::unordered_set<int> uniqueIds;
        for (float val : data) {
            int labelId = static_cast<int>(val + 0.5f);
            if (labelId != 0) {
                uniqueIds.insert(labelId);
            }
        }
        uniqueLabelIds_.assign(uniqueIds.begin(), uniqueIds.end());
        std::sort(uniqueLabelIds_.begin(), uniqueLabelIds_.end());
    }
    labelIndexMap_.clear();
    for (size_t i = 0; i < uniqueLabelIds_.size(); ++i)
        labelIndexMap_[uniqueLabelIds_[i]] = static_cast<int>(i);
    uniqueLabelIdsValid_ = true;
    return uniqueLabelIds_;
}

const std::unordered_map<int, int>& Volume::getLabelIndexMap() const {
    getUniqueLabelIds();
    return labelIndexMap_;
}
