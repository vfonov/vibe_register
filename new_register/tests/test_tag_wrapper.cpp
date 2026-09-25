/// test_tag_wrapper.cpp — TagWrapper save/load round trips.
///
/// Covers: repeated saves on one wrapper (leaks are caught by the
/// TagWrapperMemcheck valgrind run), saving an empty tag set (removes the
/// file), and saving two-volume tags whose point lists differ in length.

#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>

#include "TagWrapper.hpp"
#include "nr_test.h"

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
                ("nr_tag_test_" + std::to_string(rd()) + std::to_string(rd()));
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

}  // namespace

TEST_CASE("TagWrapper can load and save repeatedly on one instance", "[tagwrapper][memcheck]")
{
    REQUIRE_FALSE(nrtest::srcDir().empty());
    TempDir tmp;

    TagWrapper tw;
    tw.load(nrtest::srcDir() + "/test_two_vol.tag");
    REQUIRE(tw.tagCount() == 3);
    REQUIRE(tw.hasTwoVolumes());

    tw.save(tmp.file("a.tag"));
    tw.setPoints2({});  // now a single-volume set
    tw.save(tmp.file("b.tag"));
    tw.save(tmp.file("c.tag"));

    TagWrapper back;
    back.load(tmp.file("c.tag"));
    CHECK(back.tagCount() == 3);
    CHECK_FALSE(back.hasTwoVolumes());
    CHECK(back.points() == tw.points());
    CHECK(back.labels() == tw.labels());
}

TEST_CASE("Saving an empty tag set removes the tag file", "[tagwrapper][memcheck]")
{
    // libminc cannot read a zero-point tag file back, so no tags == no file.
    TempDir tmp;
    const std::string path = tmp.file("empty.tag");

    TagWrapper tw;
    tw.setPoints({{1.0, 2.0, 3.0}});
    tw.save(path);
    REQUIRE(fs::exists(path));

    // The user deletes the last tag and saves again: the file must follow.
    tw.removeTag(0);
    REQUIRE_NOTHROW(tw.save(path));
    CHECK_FALSE(fs::exists(path));

    // Saving an empty set when there is no file is fine too.
    REQUIRE_NOTHROW(tw.save(path));
}

TEST_CASE("Two-volume save writes only complete pairs", "[tagwrapper][memcheck]")
{
    TempDir tmp;
    const std::string path = tmp.file("pairs.tag");

    TagWrapper tw;
    tw.setPoints({{1, 2, 3}, {4, 5, 6}, {7, 8, 9}});
    tw.setPoints2({{11, 12, 13}, {14, 15, 16}});
    tw.setLabels({"a", "b", "c"});
    tw.save(path);

    TagWrapper back;
    back.load(path);
    REQUIRE(back.hasTwoVolumes());
    REQUIRE(back.tagCount() == 2);
    CHECK(back.points() == std::vector<glm::dvec3>{{1, 2, 3}, {4, 5, 6}});
    CHECK(back.points2() == std::vector<glm::dvec3>{{11, 12, 13}, {14, 15, 16}});
    CHECK(back.labels() == std::vector<std::string>{"a", "b"});
}
