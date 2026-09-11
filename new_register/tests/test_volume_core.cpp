/// test_volume_core.cpp — core Volume behaviour that needs no external data files.
///
/// Tests:
///   testLabelStateSurvivesCopyAndMove — label flag / LUT / tags survive copy & move
///     (regression: hand-written special members dropped isLabelVolume_ etc.)
///   testUniqueLabelIdCache — getUniqueLabelIds() caching and invalidation
///   testQuantileMatchesSortedReference — computeQuantile() equals the sorted-copy order statistic
///   testQuantilesMatchReference — computeQuantiles() (unsorted, duplicate, clamped qs) matches
///     both the sorted reference and per-q computeQuantile(), and leaves data untouched
///   testQuantilesNonFinite — NaN / +-Inf voxels are ignored
///   testQuantilesEdgeCases — empty data, all-NaN data, empty qs, NaN q

#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <glm/glm.hpp>

#include "Volume.h"

static Volume makeLabelVolume()
{
    Volume v;
    v.dimensions = glm::ivec3(4, 3, 2);
    v.data.resize(24);
    for (size_t i = 0; i < v.data.size(); ++i)
        v.data[i] = static_cast<float>(i % 3);  // values 0, 1, 2

    v.setLabelVolume(true);
    v.tags.setPoints({glm::dvec3(1, 2, 3)});

    const std::filesystem::path labelPath =
        std::filesystem::temp_directory_path() / "nr_volume_core_labels.txt";
    {
        std::ofstream out(labelPath);
        out << "1 255 0 0 255 1 1 Left\n";
        out << "2 0 255 0 255 1 1 Right\n";
    }
    v.loadLabelDescriptionFile(labelPath.string());

    return v;
}

static void checkVolume(const Volume& c, const Volume& ref)
{
    assert(c.isLabelVolume());
    assert(c.getLabelLUT().size() == 2);
    assert(c.getLabelInfo(1) != nullptr);
    assert(c.getLabelInfo(1)->name == "Left");
    assert(c.getLabelInfo(2) != nullptr);
    assert(c.getLabelInfo(2)->g == 255);
    assert(c.dimensions == ref.dimensions);
    assert(c.data == ref.data);
    assert(c.getTagCount() == 1);
}

static void testLabelStateSurvivesCopyAndMove()
{
    const Volume v = makeLabelVolume();

    // Copy construction.
    Volume copyA(v);
    checkVolume(copyA, v);

    // Copy assignment.
    Volume copyB;
    copyB = v;
    checkVolume(copyB, v);

    Volume copyC(v);

    // Move construction (from copyA).
    Volume moved(std::move(copyA));
    checkVolume(moved, v);

    // Move assignment (from copyB).
    Volume movedAssigned;
    movedAssigned = std::move(copyB);
    checkVolume(movedAssigned, v);

    // Vector growth must move, not copy — and must keep the state.
    std::vector<Volume> vec;
    vec.push_back(std::move(copyC));
    checkVolume(vec.front(), v);

    static_assert(std::is_nothrow_move_constructible<Volume>::value,
                  "Volume must be nothrow-move-constructible so std::vector<Volume> "
                  "growth moves instead of copies");
}

static void testUniqueLabelIdCache()
{
    Volume v;
    v.dimensions = glm::ivec3(6, 1, 1);
    v.data = {0.0f, 3.0f, 1.0f, 3.0f, 2.4f, 0.0f};
    v.setLabelVolume(true);

    const std::vector<int> expected{1, 2, 3};
    assert(v.getUniqueLabelIds() == expected);
    // Second call must return the same cached vector, not recompute.
    assert(&v.getUniqueLabelIds() == &v.getUniqueLabelIds());

    // Non-label volume: empty result, even though the data hasn't changed.
    v.setLabelVolume(false);
    assert(v.getUniqueLabelIds().empty());

    // Flipping back on must recompute (proves setLabelVolume invalidated the cache).
    v.setLabelVolume(true);
    assert(v.getUniqueLabelIds() == expected);

    // Writing data directly does not auto-invalidate; the cache is stale until
    // invalidateLabelIdCache() is called.
    v.data[1] = 7.0f;
    v.invalidateLabelIdCache();
    const std::vector<int> withSeven{1, 2, 3, 7};
    assert(v.getUniqueLabelIds() == withSeven);

    // generate_test_data() and setLabelVolume() must both reset the cache.
    v.generate_test_data();
    v.setLabelVolume(true);
    assert(v.getUniqueLabelIds() != withSeven);
}

// 1000 integer-valued voxels in [0, 199], so there are plenty of duplicates.
static Volume makeQuantileVolume()
{
    Volume v;
    v.dimensions = glm::ivec3(10, 10, 10);
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 199);
    v.data.resize(1000);
    for (float& x : v.data)
        x = static_cast<float>(dist(rng));
    return v;
}

// Order statistic the quantile functions are specified to return.
static float sortedReference(const std::vector<float>& sorted, double q)
{
    return sorted[static_cast<size_t>(std::clamp(q, 0.0, 1.0) *
                                      static_cast<double>(sorted.size() - 1))];
}

static void testQuantileMatchesSortedReference()
{
    const Volume v = makeQuantileVolume();
    std::vector<float> sorted = v.data;
    std::sort(sorted.begin(), sorted.end());

    for (double q : {0.0, 0.02, 0.05, 0.5, 0.95, 0.98, 1.0, -1.0, 2.0})
        assert(v.computeQuantile(q) == sortedReference(sorted, q));
}

static void testQuantilesMatchReference()
{
    const Volume v = makeQuantileVolume();
    const std::vector<float> original = v.data;
    std::vector<float> sorted = v.data;
    std::sort(sorted.begin(), sorted.end());

    std::vector<double> deciles{0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0};
    std::shuffle(deciles.begin(), deciles.end(), std::mt19937(7));

    const std::vector<std::vector<double>> cases{
        {0.02, 0.98},
        {0.9, 0.1},    // reversed
        {0.5, 0.5},    // duplicate
        {-1.0, 2.0},   // clamped
        {0.25},        // single
        deciles,       // many, unsorted
    };
    for (const std::vector<double>& qs : cases)
    {
        const std::vector<float> result = v.computeQuantiles(qs);
        assert(result.size() == qs.size());
        for (size_t i = 0; i < qs.size(); ++i)
        {
            assert(result[i] == sortedReference(sorted, qs[i]));
            assert(result[i] == v.computeQuantile(qs[i]));
        }
    }
    assert(v.data == original);
}

static void testQuantilesNonFinite()
{
    Volume v;
    v.dimensions = glm::ivec3(6, 1, 1);
    v.data = {std::numeric_limits<float>::quiet_NaN(), 1.0f, 2.0f, 3.0f,
              std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};

    const std::vector<float> expected{1.0f, 2.0f, 3.0f};
    assert(v.computeQuantiles({0.0, 0.5, 1.0}) == expected);
    assert(v.computeQuantile(0.0) == 1.0f);
    assert(v.computeQuantile(1.0) == 3.0f);
}

static void testQuantilesEdgeCases()
{
    const std::vector<float> zeros{0.0f, 0.0f};

    Volume empty;
    assert(empty.computeQuantiles({0.1, 0.9}) == zeros);
    assert(empty.computeQuantile(0.5) == 0.0f);

    Volume allNan;
    allNan.dimensions = glm::ivec3(3, 1, 1);
    allNan.data.assign(3, std::numeric_limits<float>::quiet_NaN());
    assert(allNan.computeQuantiles({0.1, 0.9}) == zeros);
    assert(allNan.computeQuantile(0.5) == 0.0f);

    const Volume v = makeQuantileVolume();
    assert(v.computeQuantiles({}).empty());

    bool threw = false;
    try
    {
        v.computeQuantiles({0.5, std::numeric_limits<double>::quiet_NaN()});
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    assert(threw);
}

int main()
{
    testLabelStateSurvivesCopyAndMove();
    testUniqueLabelIdCache();
    testQuantileMatchesSortedReference();
    testQuantilesMatchReference();
    testQuantilesNonFinite();
    testQuantilesEdgeCases();

    std::cout << "All VolumeCore tests passed\n";
    return 0;
}
