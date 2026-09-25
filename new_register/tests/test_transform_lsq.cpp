/// test_transform_lsq.cpp — computeTransform() recovers known transforms from
/// exact synthetic tag pairs, including large rotations.

#include <array>
#include <cmath>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Transform.h"

namespace
{

constexpr double kDeg = 3.14159265358979323846 / 180.0;

/// Non-coplanar source (vol2) points.
std::vector<glm::dvec3> sourcePoints()
{
    return {{0, 0, 0}, {40, 0, 0}, {0, 30, 0}, {0, 0, 50},
            {25, -10, 35}, {-30, 20, 10}, {15, 45, -20}, {-5, -35, -25}};
}

/// Rz * Ry * Rx (degrees), plus translation, as a 4x4 matrix.
glm::dmat4 rigid(double rxDeg, double ryDeg, double rzDeg, const glm::dvec3& t)
{
    glm::dmat4 m(1.0);
    m = glm::translate(m, t);
    m = glm::rotate(m, rzDeg * kDeg, glm::dvec3(0, 0, 1));
    m = glm::rotate(m, ryDeg * kDeg, glm::dvec3(0, 1, 0));
    m = glm::rotate(m, rxDeg * kDeg, glm::dvec3(1, 0, 0));
    return m;
}

std::vector<glm::dvec3> transformAll(const glm::dmat4& m, const std::vector<glm::dvec3>& pts)
{
    std::vector<glm::dvec3> out;
    for (const auto& p : pts)
        out.push_back(glm::dvec3(m * glm::dvec4(p, 1.0)));
    return out;
}

double maxAbsDiff(const glm::dmat4& a, const glm::dmat4& b)
{
    double d = 0.0;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            d = std::max(d, std::fabs(a[c][r] - b[c][r]));
    return d;
}

}  // namespace

TEST_CASE("LSQ6 recovers rigid transforms with large rotations", "[transform][lsq6]")
{
    const double rx = GENERATE(-150.0, -90.0, 0.0, 60.0, 170.0);
    const double ry = GENERATE(-80.0, -30.0, 0.0, 45.0, 89.0);
    const double rz = GENERATE(-120.0, 0.0, 90.0, 179.0);
    INFO("angles " << rx << " " << ry << " " << rz);

    const glm::dmat4 truth = rigid(rx, ry, rz, {12.0, -7.0, 3.5});
    const auto vol2 = sourcePoints();
    const auto vol1 = transformAll(truth, vol2);

    TransformResult res = computeTransform(vol1, vol2, TransformType::LSQ6);
    REQUIRE(res.valid);
    INFO("avgRMS " << res.avgRMS);
    CHECK(res.avgRMS < 1e-6);
    CHECK(maxAbsDiff(res.linearMatrix, truth) < 1e-6);
}

TEST_CASE("LSQ7 recovers a scaled rigid transform with a large rotation", "[transform][lsq7]")
{
    const double rz = GENERATE(-135.0, 100.0, 160.0);
    INFO("rz " << rz);
    glm::dmat4 truth = rigid(35.0, -50.0, rz, {-4.0, 9.0, 1.0});
    truth = truth * glm::scale(glm::dmat4(1.0), glm::dvec3(1.3));
    const auto vol2 = sourcePoints();
    const auto vol1 = transformAll(truth, vol2);

    TransformResult res = computeTransform(vol1, vol2, TransformType::LSQ7);
    REQUIRE(res.valid);
    INFO("avgRMS " << res.avgRMS);
    CHECK(res.avgRMS < 1e-6);
    CHECK(maxAbsDiff(res.linearMatrix, truth) < 1e-6);
}

TEST_CASE("LSQ9 recovers anisotropic scaling with a large rotation", "[transform][lsq9]")
{
    const double rx = GENERATE(-100.0, 20.0, 150.0);
    INFO("rx " << rx);
    glm::dmat4 truth = rigid(rx, 30.0, -110.0, {2.0, 3.0, -6.0});
    truth = truth * glm::scale(glm::dmat4(1.0), glm::dvec3(0.8, 1.2, 1.5));
    const auto vol2 = sourcePoints();
    const auto vol1 = transformAll(truth, vol2);

    TransformResult res = computeTransform(vol1, vol2, TransformType::LSQ9);
    REQUIRE(res.valid);
    INFO("avgRMS " << res.avgRMS);
    CHECK(res.avgRMS < 1e-6);
    CHECK(maxAbsDiff(res.linearMatrix, truth) < 1e-6);
}

TEST_CASE("LSQ10 recovers an X shear on top of rotation and scaling", "[transform][lsq10]")
{
    // Model used by the solver: M = SH * R * S, with SH(0,1) = shear.
    const double shear = GENERATE(-0.3, 0.15, 0.4);
    const double rz = GENERATE(-120.0, 25.0, 140.0);
    INFO("shear " << shear << " rz " << rz);
    glm::dmat4 sh(1.0);
    sh[1][0] = shear;  // column 1, row 0
    glm::dmat4 truth = rigid(20.0, -35.0, rz, {5.0, -2.0, 8.0});
    truth = truth * glm::scale(glm::dmat4(1.0), glm::dvec3(0.9, 1.1, 1.25));
    truth = glm::translate(glm::dmat4(1.0), glm::dvec3(truth[3])) * sh *
            glm::dmat4(glm::dmat3(truth));
    const auto vol2 = sourcePoints();
    const auto vol1 = transformAll(truth, vol2);

    TransformResult res = computeTransform(vol1, vol2, TransformType::LSQ10);
    REQUIRE(res.valid);
    INFO("avgRMS " << res.avgRMS);
    CHECK(res.avgRMS < 1e-6);
    CHECK(maxAbsDiff(res.linearMatrix, truth) < 1e-6);
}
