/// test_qc_csv_io.cpp — QC CSV reading and writing edge cases.
///
/// Covers: a UTF-8 byte-order mark (Excel's "CSV UTF-8" export) before the
/// header; comments containing newlines, quotes and commas surviving a
/// save/load round trip; verdicts for IDs that are not in the current input
/// CSV being kept when the output file is rewritten; and a failed save being
/// reported instead of silently dropping the verdicts.

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "QCState.h"

namespace fs = std::filesystem;

namespace
{

/// A fresh, empty directory under the system temp dir, removed on exit.
struct TempDir
{
    fs::path path;
    TempDir()
    {
        path = fs::temp_directory_path() /
               ("nr_qc_csv_io_" + std::to_string(std::rand()));
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }
};

void writeFile(const fs::path& p, const std::string& text)
{
    std::ofstream ofs(p, std::ios::binary);
    ofs << text;
}

std::string readFile(const fs::path& p)
{
    std::ifstream ifs(p, std::ios::binary);
    std::stringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

}  // namespace

TEST_CASE("An input CSV with a UTF-8 BOM loads", "[qc][csv]")
{
    TempDir dir;
    const fs::path in = dir.path / "in.csv";
    writeFile(in, "\xEF\xBB\xBFID,T1\ns1,a.mnc\n");

    QCState qc;
    REQUIRE_NOTHROW(qc.loadInputCsv(in.string()));
    CHECK(qc.columnNames == std::vector<std::string>{"T1"});
    CHECK(qc.rowIds == std::vector<std::string>{"s1"});
}

TEST_CASE("Comments with newlines, quotes and commas round-trip", "[qc][csv]")
{
    TempDir dir;
    const fs::path in = dir.path / "in.csv";
    const fs::path out = dir.path / "out.csv";
    writeFile(in, "ID,T1\ns1,a.mnc\ns2,b.mnc\n");
    const std::string comment = "motion, \"ghosting\"\r\nsecond line\nthird";

    QCState qc;
    qc.loadInputCsv(in.string());
    qc.outputCsvPath = out.string();
    qc.results[0].verdicts[0] = 2;
    qc.results[0].comments[0] = comment;
    qc.results[1].verdicts[0] = 0;
    qc.saveOutputCsv();

    QCState reloaded;
    reloaded.loadInputCsv(in.string());
    reloaded.loadOutputCsv(out.string());
    CHECK(reloaded.results[0].verdicts[0] == 2);
    CHECK(reloaded.results[0].comments[0] == comment);
    CHECK(reloaded.results[1].verdicts[0] == 0);
}

TEST_CASE("Verdicts for IDs missing from the input are kept on save", "[qc][csv]")
{
    TempDir dir;
    const fs::path in = dir.path / "in.csv";
    const fs::path out = dir.path / "out.csv";
    writeFile(in, "ID,T1\ns1,a.mnc\n");
    writeFile(out, "ID,T1_verdict,T1_comment\ns1,PASS,\nold7,FAIL,kept note\n");

    QCState qc;
    qc.loadInputCsv(in.string());
    qc.outputCsvPath = out.string();
    qc.loadOutputCsv(out.string());
    qc.results[0].verdicts[0] = 1;
    qc.saveOutputCsv();

    const std::string text = readFile(out);
    CHECK(text.find("s1,WARN,") != std::string::npos);
    CHECK(text.find("old7,FAIL,kept note") != std::string::npos);
}

TEST_CASE("Single-verdict mode keeps unknown IDs too", "[qc][csv]")
{
    TempDir dir;
    const fs::path in = dir.path / "in.csv";
    const fs::path out = dir.path / "out.csv";
    writeFile(in, "ID,T1\ns1,a.mnc\n");
    writeFile(out, "ID,verdict,comment\nold7,FAIL,kept note\n");

    QCState qc;
    qc.singleVerdictMode = true;
    qc.loadInputCsv(in.string());
    qc.outputCsvPath = out.string();
    qc.loadOutputCsv(out.string());
    qc.saveOutputCsv();

    CHECK(readFile(out).find("old7,FAIL,kept note") != std::string::npos);
}

TEST_CASE("A failed save is reported and leaves no temp file", "[qc][csv]")
{
    TempDir dir;
    const fs::path in = dir.path / "in.csv";
    writeFile(in, "ID,T1\ns1,a.mnc\n");

    QCState qc;
    qc.loadInputCsv(in.string());

    qc.outputCsvPath = (dir.path / "no_such_dir" / "out.csv").string();
    CHECK_FALSE(qc.saveOutputCsv());

    qc.outputCsvPath = (dir.path / "out.csv").string();
    CHECK(qc.saveOutputCsv());
    CHECK(fs::exists(dir.path / "out.csv"));
    int entries = 0;
    for (const auto& e : fs::directory_iterator(dir.path))
    {
        (void)e;
        ++entries;
    }
    CHECK(entries == 2);  // in.csv and out.csv, nothing left behind
}
