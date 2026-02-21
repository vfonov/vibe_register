// Test loading of single-volume and two-volume .tag files using TagWrapper
#include "TagWrapper.hpp"
#include <iostream>
#include <glm/vec3.hpp>
#include <cmath>
#include <string>

static const double kTol = 1e-6;

static bool approxEq(double a, double b)
{
    return std::fabs(a - b) < kTol;
}

static bool checkPoint(const glm::dvec3& got, double ex, double ey, double ez,
                       const char* label)
{
    if (!approxEq(got.x, ex) || !approxEq(got.y, ey) || !approxEq(got.z, ez))
    {
        std::cerr << label << " mismatch: ("
                  << got.x << "," << got.y << "," << got.z
                  << ") expected (" << ex << "," << ey << "," << ez << ")\n";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 1: Single-volume .tag file
// ---------------------------------------------------------------------------
static int testSingleVolume(const std::string& dir)
{
    std::string path = dir + "/test_single_vol.tag";
    TagWrapper tw;
    try {
        tw.load(path);
    } catch (const std::exception& e) {
        std::cerr << "FAIL [single-vol load]: " << e.what() << "\n";
        return 1;
    }

    if (tw.volumeCount() != 1) {
        std::cerr << "FAIL [single-vol]: volumeCount=" << tw.volumeCount()
                  << " expected 1\n";
        return 1;
    }

    if (tw.tagCount() != 3) {
        std::cerr << "FAIL [single-vol]: tagCount=" << tw.tagCount()
                  << " expected 3\n";
        return 1;
    }

    if (tw.hasTwoVolumes()) {
        std::cerr << "FAIL [single-vol]: hasTwoVolumes() should be false\n";
        return 1;
    }

    const auto& pts = tw.points();
    if (!checkPoint(pts[0], -14, 19, 18, "single-vol pt0")) return 1;
    if (!checkPoint(pts[1],  14, 23, 18, "single-vol pt1")) return 1;
    if (!checkPoint(pts[2], -25, -44, 18, "single-vol pt2")) return 1;

    // Check labels
    const auto& labels = tw.labels();
    if (labels.size() != 3) {
        std::cerr << "FAIL [single-vol]: label count=" << labels.size()
                  << " expected 3\n";
        return 1;
    }
    if (labels[0] != "Point1" || labels[1] != "Point2" || labels[2] != "Point3") {
        std::cerr << "FAIL [single-vol]: labels mismatch: \""
                  << labels[0] << "\",\"" << labels[1] << "\",\"" << labels[2] << "\"\n";
        return 1;
    }

    std::cerr << "PASS [single-vol]: 3 points, 1 volume\n";
    return 0;
}

// ---------------------------------------------------------------------------
// Test 2: Two-volume .tag file
// ---------------------------------------------------------------------------
static int testTwoVolume(const std::string& dir)
{
    std::string path = dir + "/test_two_vol.tag";
    TagWrapper tw;
    try {
        tw.load(path);
    } catch (const std::exception& e) {
        std::cerr << "FAIL [two-vol load]: " << e.what() << "\n";
        return 1;
    }

    if (tw.volumeCount() != 2) {
        std::cerr << "FAIL [two-vol]: volumeCount=" << tw.volumeCount()
                  << " expected 2\n";
        return 1;
    }

    if (tw.tagCount() != 3) {
        std::cerr << "FAIL [two-vol]: tagCount=" << tw.tagCount()
                  << " expected 3\n";
        return 1;
    }

    if (!tw.hasTwoVolumes()) {
        std::cerr << "FAIL [two-vol]: hasTwoVolumes() should be true\n";
        return 1;
    }

    // Volume 1 points
    const auto& pts1 = tw.points();
    if (!checkPoint(pts1[0], -14, 19, 18, "two-vol v1 pt0")) return 1;
    if (!checkPoint(pts1[1],  14, 23, 18, "two-vol v1 pt1")) return 1;
    if (!checkPoint(pts1[2], -25, -44, 18, "two-vol v1 pt2")) return 1;

    // Volume 2 points
    const auto& pts2 = tw.points2();
    if (pts2.size() != 3) {
        std::cerr << "FAIL [two-vol]: points2 count=" << pts2.size()
                  << " expected 3\n";
        return 1;
    }
    if (!checkPoint(pts2[0], -15, 20, 19, "two-vol v2 pt0")) return 1;
    if (!checkPoint(pts2[1],  13, 22, 17, "two-vol v2 pt1")) return 1;
    if (!checkPoint(pts2[2], -26, -45, 19, "two-vol v2 pt2")) return 1;

    // Labels (shared between volumes)
    const auto& labels = tw.labels();
    if (labels[0] != "Point1" || labels[1] != "Point2" || labels[2] != "Point3") {
        std::cerr << "FAIL [two-vol]: labels mismatch\n";
        return 1;
    }

    std::cerr << "PASS [two-vol]: 3 points, 2 volumes\n";
    return 0;
}

// ---------------------------------------------------------------------------
// Test 3: Round-trip save/load for two-volume file
// ---------------------------------------------------------------------------
static int testRoundTrip(const std::string& dir)
{
    // Create a TagWrapper with two volumes of data, save, reload, verify
    TagWrapper tw;
    std::vector<glm::dvec3> v1 = {{1.5, 2.5, 3.5}, {4.0, 5.0, 6.0}};
    std::vector<glm::dvec3> v2 = {{10.1, 20.2, 30.3}, {40.4, 50.5, 60.6}};
    std::vector<std::string> labels = {"TagA", "TagB"};

    tw.setPoints(v1);
    tw.setPoints2(v2);
    tw.setLabels(labels);

    std::string outPath = dir + "/test_roundtrip_tmp.tag";
    try {
        tw.save(outPath);
    } catch (const std::exception& e) {
        std::cerr << "FAIL [roundtrip save]: " << e.what() << "\n";
        return 1;
    }

    // Reload
    TagWrapper tw2;
    try {
        tw2.load(outPath);
    } catch (const std::exception& e) {
        std::cerr << "FAIL [roundtrip load]: " << e.what() << "\n";
        return 1;
    }

    if (tw2.volumeCount() != 2) {
        std::cerr << "FAIL [roundtrip]: volumeCount=" << tw2.volumeCount()
                  << " expected 2\n";
        return 1;
    }
    if (tw2.tagCount() != 2) {
        std::cerr << "FAIL [roundtrip]: tagCount=" << tw2.tagCount()
                  << " expected 2\n";
        return 1;
    }
    if (!tw2.hasTwoVolumes()) {
        std::cerr << "FAIL [roundtrip]: hasTwoVolumes() should be true\n";
        return 1;
    }

    const auto& rp1 = tw2.points();
    const auto& rp2 = tw2.points2();
    if (!checkPoint(rp1[0], 1.5, 2.5, 3.5, "roundtrip v1 pt0")) return 1;
    if (!checkPoint(rp1[1], 4.0, 5.0, 6.0, "roundtrip v1 pt1")) return 1;
    if (!checkPoint(rp2[0], 10.1, 20.2, 30.3, "roundtrip v2 pt0")) return 1;
    if (!checkPoint(rp2[1], 40.4, 50.5, 60.6, "roundtrip v2 pt1")) return 1;

    const auto& rl = tw2.labels();
    if (rl[0] != "TagA" || rl[1] != "TagB") {
        std::cerr << "FAIL [roundtrip]: labels mismatch: \""
                  << rl[0] << "\",\"" << rl[1] << "\"\n";
        return 1;
    }

    // Clean up temp file
    std::remove(outPath.c_str());

    std::cerr << "PASS [roundtrip]: save + load two-volume file\n";
    return 0;
}

// ---------------------------------------------------------------------------
// Test 4: Single-volume round-trip (backward compat)
// ---------------------------------------------------------------------------
static int testSingleVolumeRoundTrip(const std::string& dir)
{
    TagWrapper tw;
    std::vector<glm::dvec3> v1 = {{-1.0, 0.0, 1.0}, {2.0, 3.0, 4.0}, {5.5, 6.5, 7.5}};
    std::vector<std::string> labels = {"A", "B", "C"};

    tw.setPoints(v1);
    tw.setLabels(labels);
    // No setPoints2 — should save as Volumes=1

    std::string outPath = dir + "/test_single_rt_tmp.tag";
    try {
        tw.save(outPath);
    } catch (const std::exception& e) {
        std::cerr << "FAIL [single-rt save]: " << e.what() << "\n";
        return 1;
    }

    TagWrapper tw2;
    try {
        tw2.load(outPath);
    } catch (const std::exception& e) {
        std::cerr << "FAIL [single-rt load]: " << e.what() << "\n";
        return 1;
    }

    if (tw2.volumeCount() != 1) {
        std::cerr << "FAIL [single-rt]: volumeCount=" << tw2.volumeCount()
                  << " expected 1\n";
        return 1;
    }
    if (tw2.hasTwoVolumes()) {
        std::cerr << "FAIL [single-rt]: hasTwoVolumes() should be false\n";
        return 1;
    }
    if (tw2.tagCount() != 3) {
        std::cerr << "FAIL [single-rt]: tagCount=" << tw2.tagCount()
                  << " expected 3\n";
        return 1;
    }

    const auto& rp = tw2.points();
    if (!checkPoint(rp[0], -1.0, 0.0, 1.0, "single-rt pt0")) return 1;
    if (!checkPoint(rp[1],  2.0, 3.0, 4.0, "single-rt pt1")) return 1;
    if (!checkPoint(rp[2],  5.5, 6.5, 7.5, "single-rt pt2")) return 1;

    std::remove(outPath.c_str());

    std::cerr << "PASS [single-rt]: save + load single-volume file\n";
    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <test_dir>\n"
                  << "  test_dir should contain test_single_vol.tag and test_two_vol.tag\n";
        return 1;
    }
    std::string dir = argv[1];

    int failures = 0;
    failures += testSingleVolume(dir);
    failures += testTwoVolume(dir);
    failures += testRoundTrip(dir);
    failures += testSingleVolumeRoundTrip(dir);

    if (failures > 0) {
        std::cerr << "\n" << failures << " test(s) FAILED\n";
        return 1;
    }
    std::cerr << "\nAll tag tests passed.\n";
    return 0;
}
