/// test_xfm_direction.cpp — .xfm files written by writeXfmFile() map vol2 -> vol1
/// when evaluated by libminc (via minc2-simple), for linear and TPS transforms,
/// and readXfmFile() handles concatenated files.

#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <glm/glm.hpp>

extern "C" {
#include "minc2-simple.h"
}

#include "Transform.h"

namespace
{

namespace fs = std::filesystem;

std::string tempXfm(const std::string& stem)
{
    std::random_device rd;
    return (fs::temp_directory_path() /
            ("nr_xfm_" + stem + std::to_string(rd()) + std::to_string(rd()) + ".xfm")).string();
}

/// Non-coplanar vol2 tags and vol1 = rotation + translation + a smooth bend.
void makeTags(std::vector<glm::dvec3>& vol1, std::vector<glm::dvec3>& vol2)
{
    vol2 = {{0, 0, 0}, {40, 0, 0}, {0, 30, 0}, {0, 0, 50}, {25, -10, 35},
            {-30, 20, 10}, {15, 45, -20}, {-5, -35, -25}, {33, 28, 12}};
    const double a = 0.5;  // ~29 degrees about z
    for (const auto& p : vol2)
    {
        glm::dvec3 q(std::cos(a) * p.x - std::sin(a) * p.y,
                     std::sin(a) * p.x + std::cos(a) * p.y, p.z);
        q += glm::dvec3(10.0, -6.0, 4.0);
        q.z += 0.002 * p.x * p.x;  // non-linear part
        vol1.push_back(q);
    }
}

/// Evaluate `path` with libminc at `in`.
glm::dvec3 minctransform(const std::string& path, const glm::dvec3& in)
{
    minc2_xfm_file_handle h = minc2_xfm_allocate0();
    REQUIRE(h != nullptr);
    REQUIRE(minc2_xfm_open(h, path.c_str()) == MINC2_SUCCESS);
    double src[3] = {in.x, in.y, in.z};
    double dst[3] = {0, 0, 0};
    REQUIRE(minc2_xfm_transform_point(h, src, dst) == MINC2_SUCCESS);
    minc2_xfm_destroy(h);
    return {dst[0], dst[1], dst[2]};
}

}  // namespace

TEST_CASE("written .xfm maps vol2 tags onto vol1 tags", "[xfm][direction]")
{
    const TransformType type = GENERATE(TransformType::LSQ12, TransformType::TPS);
    INFO("type " << transformTypeName(type));

    std::vector<glm::dvec3> vol1, vol2;
    makeTags(vol1, vol2);
    TransformResult res = computeTransform(vol1, vol2, type);
    REQUIRE(res.valid);

    const std::string path = tempXfm("dir");
    REQUIRE(writeXfmFile(path, res));

    // TPS interpolates exactly; LSQ12 only fits, so compare against its own mapping.
    for (size_t i = 0; i < vol2.size(); ++i)
    {
        const glm::dvec3 expected = res.transformPoint(vol2[i]);
        const glm::dvec3 got = minctransform(path, vol2[i]);
        INFO("tag " << i << " expected " << expected.x << "," << expected.y << "," << expected.z
                    << " got " << got.x << "," << got.y << "," << got.z);
        CHECK(glm::length(got - expected) < 1e-3);
        if (type == TransformType::TPS)
            CHECK(glm::length(got - vol1[i]) < 1e-3);
    }
    fs::remove(path);
}

TEST_CASE("readXfmFile composes a concatenated linear .xfm", "[xfm][read]")
{
    const std::string path = tempXfm("concat");
    {
        std::ofstream out(path);
        out << "MNI Transform File\n\n"
               "Transform_Type = Linear;\n"
               "Linear_Transform =\n"
               " 1 0 0 10\n 0 1 0 0\n 0 0 1 0;\n"
               "Transform_Type = Linear;\n"
               "Linear_Transform =\n"
               " 2 0 0 0\n 0 2 0 0\n 0 0 2 0;\n";
    }
    glm::dmat4 m(1.0);
    REQUIRE(readXfmFile(path, m));
    // libminc applies the first transform first: p -> 2 * (p + (10,0,0)).
    const glm::dvec3 p(1.0, 2.0, 3.0);
    const glm::dvec3 got(m * glm::dvec4(p, 1.0));
    CHECK(glm::length(got - glm::dvec3(22.0, 4.0, 6.0)) < 1e-9);
    CHECK(glm::length(got - minctransform(path, p)) < 1e-9);
    fs::remove(path);
}
