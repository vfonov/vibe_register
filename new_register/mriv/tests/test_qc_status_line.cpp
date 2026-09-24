// mriv_qc text rows: header, path/message row, key legend.

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "qc/QcState.hpp"
#include "qc/QcStatusLine.hpp"

using namespace mriv::qc;
using Catch::Matchers::ContainsSubstring;

namespace
{

std::vector<QC::QCRecord> makeRecords()
{
    std::vector<QC::QCRecord> records(4);
    records[0] = {"sub0", "baseline", "img/sub0.png", "Pass", ""};
    records[1] = {"sub1", "-", "img/sub1.png", "Warning", ""};
    records[2] = {"sub2", "-", "img/sub2.png", "Fail", ""};
    records[3] = {"sub3", "-", "img/sub3.png", "", ""};
    return records;
}

bool noSave() { return true; }

} // namespace

TEST_CASE("header shows position, id, visit, verdict and tallies", "[qc][status]")
{
    auto records = makeRecords();
    QcState state(records, noSave);

    std::string header = formatHeader(state);
    CHECK(header == "1/4  sub0  baseline  [Pass]  rated 3/4 (P1 W1 F1)");
}

TEST_CASE("header omits a '-' visit and marks an unrated image", "[qc][status]")
{
    auto records = makeRecords();
    QcState state(records, noSave);
    state.handleKey('G');
    CHECK(formatHeader(state) == "4/4  sub3  [--]  rated 3/4 (P1 W1 F1)");
}

TEST_CASE("the second row shows the path, a load failure, or the message", "[qc][status]")
{
    auto records = makeRecords();
    QcState state(records, noSave);

    CHECK(formatDetailLine(state, false) == "img/sub0.png");
    CHECK(formatDetailLine(state, true) == "cannot load img/sub0.png");

    state.handleKey('s');
    CHECK(formatDetailLine(state, true) == "Saved");
}

TEST_CASE("text rows never contain control characters", "[qc][status]")
{
    std::vector<QC::QCRecord> records(1);
    records[0] = {"a\nb", "v\r1", "dir\n/x.png", "", ""};
    QcState state(records, noSave);

    for (const std::string& line : {formatHeader(state), formatDetailLine(state, false),
                                    formatHotkeyLine()})
    {
        for (char c : line)
            CHECK(static_cast<unsigned char>(c) >= 0x20);
    }
}

TEST_CASE("the key legend names every binding", "[qc][status]")
{
    std::string keys = formatHotkeyLine();
    CHECK_THAT(keys, ContainsSubstring("p pass"));
    CHECK_THAT(keys, ContainsSubstring("w warn"));
    CHECK_THAT(keys, ContainsSubstring("f fail"));
    CHECK_THAT(keys, ContainsSubstring("u clear"));
    CHECK_THAT(keys, ContainsSubstring("[/] unrated"));
    CHECK_THAT(keys, ContainsSubstring("s save"));
    CHECK_THAT(keys, ContainsSubstring("q quit"));
}

TEST_CASE("the exit summary reports the tallies and where results went", "[qc][status]")
{
    auto records = makeRecords();
    QcState state(records, noSave);
    CHECK(formatSummary(state, "out.csv") == "rated 3/4 (P1 W1 F1) -> out.csv");
}
