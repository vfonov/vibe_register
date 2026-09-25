/// test_minc_nonspatial.cpp — MINC files with non-spatial dimensions.
///
/// Fixtures are written at run time with minc2-simple into a temporary
/// directory. Covers: 4-D (time) volumes keep frame 0 only, whatever the file
/// order of the time axis, and vector-valued volumes are rejected clearly.

#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

extern "C" {
#include <minc2-simple.h>
}

#include "Volume.h"

namespace
{

namespace fs = std::filesystem;

/// Fresh per-test scratch directory, removed on destruction.
class TempDir
{
public:
    TempDir()
    {
        std::random_device rd;
        path_ = fs::temp_directory_path() /
                ("nr_minc_test_" + std::to_string(rd()) + std::to_string(rd()));
        fs::create_directories(path_);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    std::string file(const std::string& name) const { return (path_ / name).string(); }

private:
    fs::path path_;
};

minc2_dimension dim(int id, int length)
{
    minc2_dimension d{};
    d.id = id;
    d.length = length;
    d.step = 1.0;
    d.start = 0.0;
    return d;
}

/// Write a float MINC file. `dims` is in file order, fastest-varying first;
/// `data` is laid out in that order.
void writeMinc(const std::string& path, std::vector<minc2_dimension> dims,
               const std::vector<float>& data)
{
    minc2_dimension end{};
    end.id = MINC2_DIM_END;
    dims.push_back(end);

    minc2_file_handle h = minc2_allocate0();
    REQUIRE(h != nullptr);
    REQUIRE(minc2_define(h, dims.data(), MINC2_FLOAT, MINC2_FLOAT) == MINC2_SUCCESS);
    REQUIRE(minc2_create(h, path.c_str()) == MINC2_SUCCESS);
    REQUIRE(minc2_save_complete_volume(h, data.data(), MINC2_FLOAT) == MINC2_SUCCESS);
    minc2_close(h);
    minc2_free(h);
}

constexpr int kNx = 4, kNy = 3, kNz = 2, kNt = 3;

/// Value of voxel (x,y,z) in frame t: frames 1.. are far outside frame 0's range.
float voxelValue(int x, int y, int z, int t)
{
    return static_cast<float>(t * 1000 + (z * kNy + y) * kNx + x);
}

void requireFrameZero(const Volume& vol)
{
    REQUIRE(vol.dimensions == glm::ivec3(kNx, kNy, kNz));
    REQUIRE(vol.data.size() == static_cast<size_t>(kNx * kNy * kNz));
    for (int z = 0; z < kNz; ++z)
        for (int y = 0; y < kNy; ++y)
            for (int x = 0; x < kNx; ++x)
                REQUIRE(vol.get(x, y, z) == voxelValue(x, y, z, 0));
    CHECK(vol.min_value == 0.0f);
    CHECK(vol.max_value == static_cast<float>(kNx * kNy * kNz - 1));
}

}  // namespace

TEST_CASE("4-D MINC with time slowest keeps frame 0", "[minc][4d]")
{
    TempDir tmp;
    const std::string path = tmp.file("xyzt.mnc");
    std::vector<float> data;
    for (int t = 0; t < kNt; ++t)
        for (int z = 0; z < kNz; ++z)
            for (int y = 0; y < kNy; ++y)
                for (int x = 0; x < kNx; ++x)
                    data.push_back(voxelValue(x, y, z, t));
    writeMinc(path, {dim(MINC2_DIM_X, kNx), dim(MINC2_DIM_Y, kNy), dim(MINC2_DIM_Z, kNz),
                     dim(MINC2_DIM_TIME, kNt)}, data);

    Volume vol;
    vol.load(path);
    requireFrameZero(vol);
}

TEST_CASE("4-D MINC with time fastest keeps frame 0", "[minc][4d]")
{
    TempDir tmp;
    const std::string path = tmp.file("txyz.mnc");
    std::vector<float> data;
    for (int z = 0; z < kNz; ++z)
        for (int y = 0; y < kNy; ++y)
            for (int x = 0; x < kNx; ++x)
                for (int t = 0; t < kNt; ++t)
                    data.push_back(voxelValue(x, y, z, t));
    writeMinc(path, {dim(MINC2_DIM_TIME, kNt), dim(MINC2_DIM_X, kNx), dim(MINC2_DIM_Y, kNy),
                     dim(MINC2_DIM_Z, kNz)}, data);

    Volume vol;
    vol.load(path);
    requireFrameZero(vol);
}

TEST_CASE("Vector-valued MINC is rejected with a clear error", "[minc][vector]")
{
    TempDir tmp;
    const std::string path = tmp.file("rgb.mnc");
    std::vector<float> data(static_cast<size_t>(3 * kNx * kNy * kNz), 1.0f);
    writeMinc(path, {dim(MINC2_DIM_VEC, 3), dim(MINC2_DIM_X, kNx), dim(MINC2_DIM_Y, kNy),
                     dim(MINC2_DIM_Z, kNz)}, data);

    Volume vol;
    REQUIRE_THROWS_WITH(vol.load(path),
                        Catch::Matchers::ContainsSubstring("vector dimension"));
}
