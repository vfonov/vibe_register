// mriv_qc key handling: verdicts, navigation, saving.

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "qc/QcState.hpp"

using namespace mriv::qc;
using mriv::term::KeyResult;

namespace
{

std::vector<QC::QCRecord> makeRecords(int n)
{
    std::vector<QC::QCRecord> records;
    for (int i = 0; i < n; ++i)
    {
        QC::QCRecord r;
        r.id = "sub" + std::to_string(i);
        r.visit = "-";
        r.picture_path = r.id + ".png";
        records.push_back(r);
    }
    return records;
}

/// Counts saves and can be told to fail.
struct FakeSaver
{
    int calls = 0;
    bool succeed = true;
    bool operator()()
    {
        ++calls;
        return succeed;
    }
};

} // namespace

TEST_CASE("p, w and f set the verdict, save and advance", "[qc][state]")
{
    auto records = makeRecords(4);
    FakeSaver saver;
    QcState state(records, std::ref(saver));

    CHECK(state.handleKey('p') == KeyResult::Changed);
    CHECK(records[0].qc_status == "Pass");
    CHECK(state.index() == 1);

    CHECK(state.handleKey('w') == KeyResult::Changed);
    CHECK(records[1].qc_status == "Warning");
    CHECK(state.index() == 2);

    CHECK(state.handleKey('f') == KeyResult::Changed);
    CHECK(records[2].qc_status == "Fail");
    CHECK(state.index() == 3);

    CHECK(saver.calls == 3);
    CHECK_FALSE(state.dirty());
}

TEST_CASE("verdict keys are case-insensitive", "[qc][state]")
{
    auto records = makeRecords(3);
    FakeSaver saver;
    QcState state(records, std::ref(saver));
    state.handleKey('P');
    state.handleKey('W');
    state.handleKey('F');
    CHECK(records[0].qc_status == "Pass");
    CHECK(records[1].qc_status == "Warning");
    CHECK(records[2].qc_status == "Fail");
}

TEST_CASE("a verdict on the last image stays there", "[qc][state]")
{
    auto records = makeRecords(2);
    FakeSaver saver;
    QcState state(records, std::ref(saver));
    state.handleKey('p');
    CHECK(state.handleKey('f') == KeyResult::Changed);
    CHECK(state.index() == 1);
    CHECK(records[1].qc_status == "Fail");
}

TEST_CASE("with auto-advance off a verdict stays on the image", "[qc][state]")
{
    auto records = makeRecords(3);
    FakeSaver saver;
    QcState state(records, std::ref(saver), false);
    state.handleKey('w');
    CHECK(state.index() == 0);
    CHECK(records[0].qc_status == "Warning");
}

TEST_CASE("u and Backspace clear a verdict; clearing nothing is ignored", "[qc][state]")
{
    auto records = makeRecords(2);
    FakeSaver saver;
    QcState state(records, std::ref(saver), false);

    CHECK(state.handleKey('u') == KeyResult::Ignored);
    CHECK(saver.calls == 0);

    state.handleKey('p');
    CHECK(state.handleKey('u') == KeyResult::Changed);
    CHECK(records[0].qc_status.empty());

    state.handleKey('f');
    CHECK(state.handleKey('\b') == KeyResult::Changed);
    CHECK(records[0].qc_status.empty());
    state.handleKey('f');
    CHECK(state.handleKey('\x7f') == KeyResult::Changed);
    CHECK(records[0].qc_status.empty());
}

TEST_CASE("next and previous keys move one image and clamp at the ends", "[qc][state]")
{
    using namespace mriv::term;
    auto records = makeRecords(3);
    FakeSaver saver;
    QcState state(records, std::ref(saver));

    for (char key : {kKeyLeft, 'h', 'b', 'k', kKeyUp, kKeyPageUp})
    {
        CHECK(state.handleKey(key) == KeyResult::Ignored);
        CHECK(state.index() == 0);
    }

    for (char key : {kKeyRight, 'l', 'n', ' ', 'j', kKeyDown, kKeyPageDown})
    {
        REQUIRE(state.handleKey('g') != KeyResult::Quit);
        CHECK(state.handleKey(key) == KeyResult::Changed);
        CHECK(state.index() == 1);
    }

    for (char key : {kKeyLeft, 'h', 'b', 'k', kKeyUp, kKeyPageUp})
    {
        state.handleKey('G');
        CHECK(state.handleKey(key) == KeyResult::Changed);
        CHECK(state.index() == 1);
    }

    state.handleKey('G');
    CHECK(state.index() == 2);
    CHECK(state.handleKey('l') == KeyResult::Ignored);
    CHECK(state.handleKey('G') == KeyResult::Ignored);
    CHECK(state.handleKey(kKeyHome) == KeyResult::Changed);
    CHECK(state.index() == 0);
    CHECK(state.handleKey('g') == KeyResult::Ignored);
    CHECK(state.handleKey(kKeyEnd) == KeyResult::Changed);
    CHECK(state.index() == 2);
    CHECK(saver.calls == 0);
}

TEST_CASE("] and [ jump to the next and previous unrated image, wrapping", "[qc][state]")
{
    auto records = makeRecords(5);
    records[1].qc_status = "Pass";
    records[3].qc_status = "Fail";
    FakeSaver saver;
    QcState state(records, std::ref(saver));

    CHECK(state.handleKey(']') == KeyResult::Changed);
    CHECK(state.index() == 2);
    CHECK(state.handleKey(']') == KeyResult::Changed);
    CHECK(state.index() == 4);
    CHECK(state.handleKey(']') == KeyResult::Changed);   // wraps
    CHECK(state.index() == 0);
    CHECK(state.handleKey('[') == KeyResult::Changed);   // wraps back
    CHECK(state.index() == 4);
    CHECK(state.handleKey('[') == KeyResult::Changed);
    CHECK(state.index() == 2);
}

TEST_CASE("] with nothing else unrated stays put and says so", "[qc][state]")
{
    auto records = makeRecords(3);
    for (auto& r : records)
        r.qc_status = "Pass";
    FakeSaver saver;
    QcState state(records, std::ref(saver));
    state.handleKey('l');

    CHECK(state.handleKey(']') == KeyResult::Changed);
    CHECK(state.index() == 1);
    CHECK(state.message() == "No unrated images");
}

TEST_CASE("rating the last unrated image announces completion", "[qc][state]")
{
    auto records = makeRecords(2);
    records[1].qc_status = "Pass";
    FakeSaver saver;
    QcState state(records, std::ref(saver));
    state.handleKey('w');
    CHECK(state.message() == "All 2 images rated");
}

TEST_CASE("s saves on demand; a failed save keeps the state dirty and says so", "[qc][state]")
{
    auto records = makeRecords(2);
    FakeSaver saver;
    QcState state(records, std::ref(saver));

    CHECK(state.handleKey('s') == KeyResult::Changed);
    CHECK(saver.calls == 1);
    CHECK(state.message() == "Saved");

    saver.succeed = false;
    state.handleKey('p');
    CHECK(state.dirty());
    CHECK(state.message() == "Save failed");
}

TEST_CASE("messages last for one key press", "[qc][state]")
{
    auto records = makeRecords(3);
    FakeSaver saver;
    QcState state(records, std::ref(saver));
    state.handleKey('s');
    CHECK_FALSE(state.message().empty());
    state.handleKey('l');
    CHECK(state.message().empty());
}

TEST_CASE("q and Escape quit, saving only unsaved changes", "[qc][state]")
{
    auto records = makeRecords(2);
    FakeSaver saver;
    QcState state(records, std::ref(saver));
    CHECK(state.handleKey('q') == KeyResult::Quit);
    CHECK(saver.calls == 0);

    saver.succeed = false;
    state.handleKey('p');
    CHECK(state.dirty());
    saver.succeed = true;
    CHECK(state.handleKey('\x1b') == KeyResult::Quit);
    CHECK(saver.calls == 2);
    CHECK_FALSE(state.dirty());
}

TEST_CASE("S asks for a screenshot and resize asks for a redraw", "[qc][state]")
{
    auto records = makeRecords(2);
    FakeSaver saver;
    QcState state(records, std::ref(saver));
    CHECK(state.handleKey('S') == KeyResult::Screenshot);
    CHECK(state.handleKey(mriv::term::kKeyResize) == KeyResult::Changed);
    CHECK(state.handleKey('?') == KeyResult::Ignored);
}

TEST_CASE("counts tallies each verdict and the unrated rest", "[qc][state]")
{
    auto records = makeRecords(6);
    records[0].qc_status = "Pass";
    records[1].qc_status = "Pass";
    records[2].qc_status = "Warning";
    records[3].qc_status = "Fail";
    FakeSaver saver;
    QcState state(records, std::ref(saver));

    auto c = state.counts();
    CHECK(c.pass == 2);
    CHECK(c.warning == 1);
    CHECK(c.fail == 1);
    CHECK(c.unrated == 2);
}

TEST_CASE("an empty list ignores everything but quit", "[qc][state]")
{
    std::vector<QC::QCRecord> records;
    FakeSaver saver;
    QcState state(records, std::ref(saver));
    CHECK(state.handleKey('p') == KeyResult::Ignored);
    CHECK(state.handleKey('l') == KeyResult::Ignored);
    CHECK(state.handleKey(']') == KeyResult::Ignored);
    CHECK(state.handleKey('q') == KeyResult::Quit);
}
