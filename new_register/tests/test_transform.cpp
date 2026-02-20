// test_transform.cpp — Tests for transform computation from tag point pairs.
//
// Verifies LSQ6 (rigid), LSQ7 (similarity), LSQ9/10 (arbitrary param),
// LSQ12 (full affine), TPS (thin-plate spline), and XFM file I/O.

#include "Transform.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

static int failures = 0;

static void check(bool cond, const char* msg, int line)
{
    if (!cond)
    {
        std::cerr << "FAIL (line " << line << "): " << msg << "\n";
        ++failures;
    }
}

#define CHECK(cond, msg) check((cond), (msg), __LINE__)

static bool approxEqual(double a, double b, double tol = 1e-6)
{
    return std::abs(a - b) < tol;
}

static bool vecApprox(const glm::dvec3& a, const glm::dvec3& b, double tol = 1e-6)
{
    return approxEqual(a.x, b.x, tol) &&
           approxEqual(a.y, b.y, tol) &&
           approxEqual(a.z, b.z, tol);
}

// ---------------------------------------------------------------------------
// Test helpers: generate tag point pairs from a known transform
// ---------------------------------------------------------------------------

/// Generate vol2 tags by applying a known 4x4 matrix to vol1 tags.
static std::vector<glm::dvec3> applyMatrix(
    const std::vector<glm::dvec3>& vol1Tags,
    const glm::dmat4& mat)
{
    std::vector<glm::dvec3> vol2Tags;
    vol2Tags.reserve(vol1Tags.size());
    for (const auto& p : vol1Tags)
    {
        glm::dvec4 h(p, 1.0);
        glm::dvec4 r = mat * h;
        vol2Tags.emplace_back(r.x, r.y, r.z);
    }
    return vol2Tags;
}

/// A set of well-distributed 3D points for testing.
static std::vector<glm::dvec3> makeTestPoints()
{
    return {
        { 10.0,  20.0,  30.0},
        {-15.0,  25.0,  10.0},
        { 30.0, -10.0,  45.0},
        {  5.0,  40.0, -20.0},
        {-25.0, -30.0,  15.0},
        { 20.0,  15.0, -35.0},
        {-10.0,  -5.0,  50.0},
        { 35.0,  30.0,  25.0},
    };
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

/// Test that too-few points returns invalid result.
static void testMinPoints()
{
    std::cout << "  testMinPoints...";

    std::vector<glm::dvec3> pts3 = {
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1}
    };

    auto r = computeTransform(pts3, pts3, TransformType::LSQ6);
    CHECK(!r.valid, "LSQ6 with 3 points should be invalid");

    auto r2 = computeTransform(pts3, pts3, TransformType::TPS);
    CHECK(!r2.valid, "TPS with 3 points should be invalid");

    // 4 points should be enough for linear
    std::vector<glm::dvec3> pts4 = {
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 1}
    };
    auto r3 = computeTransform(pts4, pts4, TransformType::LSQ6);
    CHECK(r3.valid, "LSQ6 with 4 points should be valid");

    // But TPS needs 5
    auto r4 = computeTransform(pts4, pts4, TransformType::TPS);
    CHECK(!r4.valid, "TPS with 4 points should be invalid");

    std::cout << " done\n";
}

/// Test LSQ6: pure translation.
static void testLSQ6Translation()
{
    std::cout << "  testLSQ6Translation...";

    auto vol1 = makeTestPoints();

    // Create vol2 by shifting vol1 by a known offset.
    // The transform should recover: T(vol2[i]) = vol1[i]
    // So if vol2[i] = vol1[i] + offset, then T is "subtract offset" = translate(-offset).
    // But we want T(vol2) = vol1, meaning T takes a vol2 point and maps it to vol1.
    // vol2[i] = vol1[i] - (5, -10, 15)  =>  vol1[i] = vol2[i] + (5, -10, 15)
    // So T is a translation by (5, -10, 15).
    glm::dvec3 offset(5.0, -10.0, 15.0);
    std::vector<glm::dvec3> vol2;
    for (const auto& p : vol1)
        vol2.push_back(p - offset);

    auto result = computeTransform(vol1, vol2, TransformType::LSQ6);
    CHECK(result.valid, "LSQ6 translation should be valid");
    CHECK(result.avgRMS < 1e-6, "LSQ6 translation should have zero RMS");

    // Check that applying the transform to vol2 recovers vol1
    for (size_t i = 0; i < vol1.size(); ++i)
    {
        glm::dvec3 recovered = result.transformPoint(vol2[i]);
        if (!vecApprox(recovered, vol1[i], 1e-4))
        {
            std::cerr << "\n    point " << i
                      << ": expected (" << vol1[i].x << "," << vol1[i].y << "," << vol1[i].z << ")"
                      << " got (" << recovered.x << "," << recovered.y << "," << recovered.z << ")\n";
        }
        CHECK(vecApprox(recovered, vol1[i], 1e-4),
              "LSQ6 translation: recovered point should match vol1");
    }

    std::cout << " done\n";
}

/// Test LSQ6: pure rotation (90 degrees about Z axis).
static void testLSQ6Rotation()
{
    std::cout << "  testLSQ6Rotation...";

    auto vol1 = makeTestPoints();

    // Known forward transform: vol1 = mat * vol2
    // So vol2 = inv(mat) * vol1
    // Rotate vol2 points by 45 degrees about Z relative to vol1
    double angle = glm::radians(45.0);
    double c = std::cos(angle), s = std::sin(angle);

    // vol2[i] is vol1[i] rotated by -45 degrees about Z
    // (i.e., the "original" unregistered positions)
    std::vector<glm::dvec3> vol2;
    for (const auto& p : vol1)
    {
        // Rotate by -45 deg about Z: x' = cx + sy, y' = -sx + cy
        vol2.push_back(glm::dvec3(
            c * p.x + s * p.y,
            -s * p.x + c * p.y,
            p.z));
    }

    auto result = computeTransform(vol1, vol2, TransformType::LSQ6);
    CHECK(result.valid, "LSQ6 rotation should be valid");
    CHECK(result.avgRMS < 1e-4, "LSQ6 rotation RMS should be near zero");

    for (size_t i = 0; i < vol1.size(); ++i)
    {
        glm::dvec3 recovered = result.transformPoint(vol2[i]);
        CHECK(vecApprox(recovered, vol1[i], 1e-3),
              "LSQ6 rotation: recovered point should match vol1");
    }

    std::cout << " done\n";
}

/// Test LSQ7: rotation + translation + uniform scale.
static void testLSQ7Scale()
{
    std::cout << "  testLSQ7Scale...";

    auto vol1 = makeTestPoints();

    // Build a known transform mat that maps vol2 -> vol1.
    // We'll construct vol2 as the inverse: vol2[i] = inv(mat) * vol1[i].
    //
    // mat = translate(3, -7, 11) * rotateY(30deg) * scale(2)
    // This means: T(p) = translate(3,-7,11) * rotY(30) * scale(2) * p
    //
    // Build the forward matrix directly using Eigen-style math to avoid
    // any GLM column/row confusion:
    double angle = glm::radians(30.0);
    double cs = std::cos(angle), sn = std::sin(angle);
    double sc = 2.0;

    // Rotation about Y: [[c 0 s],[0 1 0],[-s 0 c]]
    // Scale: multiply all by sc
    // Then add translation
    glm::dmat4 mat(1.0);
    mat[0][0] = sc * cs;   mat[1][0] = 0.0;     mat[2][0] = sc * sn;   mat[3][0] = 3.0;
    mat[0][1] = 0.0;       mat[1][1] = sc;       mat[2][1] = 0.0;       mat[3][1] = -7.0;
    mat[0][2] = sc * (-sn); mat[1][2] = 0.0;     mat[2][2] = sc * cs;   mat[3][2] = 11.0;
    mat[0][3] = 0.0;       mat[1][3] = 0.0;      mat[2][3] = 0.0;       mat[3][3] = 1.0;

    glm::dmat4 invMat = glm::inverse(mat);
    auto vol2 = applyMatrix(vol1, invMat);

    auto result = computeTransform(vol1, vol2, TransformType::LSQ7);
    CHECK(result.valid, "LSQ7 should be valid");

    if (result.avgRMS > 1e-3)
        std::cerr << "\n    LSQ7 avgRMS = " << result.avgRMS << "\n";
    CHECK(result.avgRMS < 1e-3, "LSQ7 RMS should be near zero");

    for (size_t i = 0; i < vol1.size(); ++i)
    {
        glm::dvec3 recovered = result.transformPoint(vol2[i]);
        if (!vecApprox(recovered, vol1[i], 1e-2))
        {
            std::cerr << "\n    LSQ7 point " << i
                      << ": expected (" << vol1[i].x << "," << vol1[i].y << "," << vol1[i].z << ")"
                      << " got (" << recovered.x << "," << recovered.y << "," << recovered.z << ")\n";
        }
        CHECK(vecApprox(recovered, vol1[i], 1e-2),
              "LSQ7: recovered point should match vol1");
    }

    std::cout << " done\n";
}

/// Test LSQ12: full affine.
static void testLSQ12()
{
    std::cout << "  testLSQ12...";

    auto vol1 = makeTestPoints();

    // Arbitrary affine: mat maps vol2 -> vol1.
    // Build directly in column-major (GLM convention).
    glm::dmat4 mat(1.0);
    mat[0][0] = 1.5;   mat[1][0] = 0.2;   mat[2][0] = -0.1;  mat[3][0] = 10.0;
    mat[0][1] = 0.3;   mat[1][1] = 0.8;   mat[2][1] = 0.1;   mat[3][1] = -5.0;
    mat[0][2] = -0.1;  mat[1][2] = 0.4;   mat[2][2] = 1.2;   mat[3][2] = 3.0;
    mat[0][3] = 0.0;   mat[1][3] = 0.0;   mat[2][3] = 0.0;   mat[3][3] = 1.0;

    glm::dmat4 invMat = glm::inverse(mat);
    auto vol2 = applyMatrix(vol1, invMat);

    auto result = computeTransform(vol1, vol2, TransformType::LSQ12);
    CHECK(result.valid, "LSQ12 should be valid");
    if (result.avgRMS > 1e-6)
        std::cerr << "\n    LSQ12 avgRMS = " << result.avgRMS << "\n";
    CHECK(result.avgRMS < 1e-6, "LSQ12 RMS should be near zero");

    for (size_t i = 0; i < vol1.size(); ++i)
    {
        glm::dvec3 recovered = result.transformPoint(vol2[i]);
        CHECK(vecApprox(recovered, vol1[i], 1e-4),
              "LSQ12: recovered point should match vol1");
    }

    std::cout << " done\n";
}

/// Test LSQ9: non-uniform scale.
static void testLSQ9()
{
    std::cout << "  testLSQ9...";

    auto vol1 = makeTestPoints();

    // Non-uniform scale (1.5, 0.8, 1.2) + rotation about X (20 deg) + translation
    double angle = glm::radians(20.0);
    double cx = std::cos(angle), sx = std::sin(angle);

    // Build mat = T * Rx * S
    glm::dmat4 mat(1.0);
    // Rotation about X: [[1,0,0],[0,c,-s],[0,s,c]]
    // Scale: diag(1.5, 0.8, 1.2)
    // Combined R*S:
    mat[0][0] = 1.5;         mat[1][0] = 0.0;          mat[2][0] = 0.0;            mat[3][0] = 5.0;
    mat[0][1] = 0.0;         mat[1][1] = 0.8 * cx;     mat[2][1] = 1.2 * (-sx);    mat[3][1] = -3.0;
    mat[0][2] = 0.0;         mat[1][2] = 0.8 * sx;     mat[2][2] = 1.2 * cx;       mat[3][2] = 8.0;
    mat[0][3] = 0.0;         mat[1][3] = 0.0;           mat[2][3] = 0.0;            mat[3][3] = 1.0;

    glm::dmat4 invMat = glm::inverse(mat);
    auto vol2 = applyMatrix(vol1, invMat);

    auto result = computeTransform(vol1, vol2, TransformType::LSQ9);
    CHECK(result.valid, "LSQ9 should be valid");

    // LM should converge to near-exact solution (with R*S parameterization)
    if (result.avgRMS > 1e-6)
        std::cerr << "\n    LSQ9 avgRMS = " << result.avgRMS << "\n";
    CHECK(result.avgRMS < 1e-4, "LSQ9 RMS should be near zero");

    for (size_t i = 0; i < vol1.size(); ++i)
    {
        glm::dvec3 recovered = result.transformPoint(vol2[i]);
        CHECK(vecApprox(recovered, vol1[i], 1e-3),
              "LSQ9: recovered point should match vol1");
    }

    std::cout << " done (avgRMS=" << result.avgRMS << ")\n";
}

/// Test TPS: identity (when vol1 == vol2, TPS should be near-identity).
static void testTPSIdentity()
{
    std::cout << "  testTPSIdentity...";

    auto pts = makeTestPoints();

    auto result = computeTransform(pts, pts, TransformType::TPS);
    CHECK(result.valid, "TPS identity should be valid");
    CHECK(result.avgRMS < 1e-6, "TPS identity RMS should be near zero");

    // Transform should recover the same points
    for (size_t i = 0; i < pts.size(); ++i)
    {
        glm::dvec3 recovered = result.transformPoint(pts[i]);
        CHECK(vecApprox(recovered, pts[i], 1e-4),
              "TPS identity: recovered point should match input");
    }

    std::cout << " done\n";
}

/// Test TPS: known non-linear deformation (should interpolate perfectly at tag points).
static void testTPSDeformation()
{
    std::cout << "  testTPSDeformation...";

    auto vol1 = makeTestPoints();

    // Apply a non-linear deformation to create vol2
    // f(p) = p + (sin(p.x/10), cos(p.y/10), sin(p.z/10)) * 5
    std::vector<glm::dvec3> vol2;
    for (const auto& p : vol1)
    {
        glm::dvec3 deformed = p + glm::dvec3(
            std::sin(p.x / 10.0) * 5.0,
            std::cos(p.y / 10.0) * 5.0,
            std::sin(p.z / 10.0) * 5.0);
        vol2.push_back(deformed);
    }

    // TPS should perfectly interpolate the tag points
    auto result = computeTransform(vol1, vol2, TransformType::TPS);
    CHECK(result.valid, "TPS deformation should be valid");

    // At the source points (vol2), TPS should evaluate to vol1
    for (size_t i = 0; i < vol1.size(); ++i)
    {
        glm::dvec3 recovered = result.transformPoint(vol2[i]);
        CHECK(vecApprox(recovered, vol1[i], 1e-3),
              "TPS deformation: should interpolate exactly at tag points");
    }

    std::cout << " done (avgRMS=" << result.avgRMS << ")\n";
}

/// Test XFM file I/O for linear transforms.
static void testXfmIO()
{
    std::cout << "  testXfmIO...";

    auto vol1 = makeTestPoints();

    // Create a known transform
    glm::dmat4 mat(1.0);
    mat = glm::translate(mat, glm::dvec3(5.0, -10.0, 15.0));
    mat = glm::rotate(mat, glm::radians(45.0), glm::dvec3(0.0, 0.0, 1.0));

    glm::dmat4 invMat = glm::inverse(mat);
    auto vol2 = applyMatrix(vol1, invMat);

    auto result = computeTransform(vol1, vol2, TransformType::LSQ6);
    CHECK(result.valid, "XFM IO: transform should be valid");

    // Write to file
    std::string path = "test_output.xfm";
    bool wrote = writeXfmFile(path, result);
    CHECK(wrote, "XFM IO: write should succeed");

    // Read back
    glm::dmat4 readMat;
    bool readOk = readXfmFile(path, readMat);
    CHECK(readOk, "XFM IO: read should succeed");

    // Compare matrices
    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            CHECK(approxEqual(readMat[col][row],
                              result.linearMatrix[col][row], 1e-8),
                  "XFM IO: read matrix should match written");
        }
    }

    // Clean up
    std::remove(path.c_str());

    std::cout << " done\n";
}

/// Test XFM file I/O for TPS transforms.
static void testXfmTPS()
{
    std::cout << "  testXfmTPS...";

    auto vol1 = makeTestPoints();

    // Simple deformation for TPS
    std::vector<glm::dvec3> vol2;
    for (const auto& p : vol1)
    {
        vol2.push_back(p + glm::dvec3(2.0, -3.0, 1.0));
    }

    auto result = computeTransform(vol1, vol2, TransformType::TPS);
    CHECK(result.valid, "XFM TPS IO: transform should be valid");

    std::string path = "test_tps_output.xfm";
    bool wrote = writeXfmFile(path, result);
    CHECK(wrote, "XFM TPS IO: write should succeed");

    // Verify file exists and has TPS content
    std::ifstream in(path);
    CHECK(in.is_open(), "XFM TPS IO: file should be readable");
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    CHECK(content.find("Thin_Plate_Spline_Transform") != std::string::npos,
          "XFM TPS IO: file should contain TPS header");
    CHECK(content.find("Points") != std::string::npos,
          "XFM TPS IO: file should contain Points section");
    CHECK(content.find("Displacements") != std::string::npos,
          "XFM TPS IO: file should contain Displacements section");

    std::remove(path.c_str());

    std::cout << " done\n";
}

/// Test transformTypeName.
static void testTransformNames()
{
    std::cout << "  testTransformNames...";

    CHECK(std::string(transformTypeName(TransformType::LSQ6)).find("Rigid") != std::string::npos,
          "LSQ6 name should contain Rigid");
    CHECK(std::string(transformTypeName(TransformType::TPS)).find("Thin") != std::string::npos,
          "TPS name should contain Thin");

    std::cout << " done\n";
}

int main()
{
    std::cout << "=== Transform Computation Tests ===\n";

    testMinPoints();
    testLSQ6Translation();
    testLSQ6Rotation();
    testLSQ7Scale();
    testLSQ9();
    testLSQ12();
    testTPSIdentity();
    testTPSDeformation();
    testXfmIO();
    testXfmTPS();
    testTransformNames();

    std::cout << "\n";
    if (failures == 0)
    {
        std::cout << "All transform tests PASSED.\n";
        return 0;
    }
    else
    {
        std::cout << failures << " transform test(s) FAILED.\n";
        return 1;
    }
}
