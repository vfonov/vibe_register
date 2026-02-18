#include "QCState.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name) \
    static void test_##name(); \
    struct TestReg_##name { TestReg_##name() { \
        std::cout << "  Running " #name "... "; \
        try { test_##name(); testsPassed++; std::cout << "PASSED\n"; } \
        catch (const std::exception& e) { testsFailed++; std::cout << "FAILED: " << e.what() << "\n"; } \
        catch (...) { testsFailed++; std::cout << "FAILED (unknown exception)\n"; } \
    } } reg_##name; \
    static void test_##name()

#define ASSERT_EQ(a, b) \
    do { if ((a) != (b)) throw std::runtime_error( \
        std::string(#a " != " #b " at line ") + std::to_string(__LINE__)); } while(0)

#define ASSERT_TRUE(x) \
    do { if (!(x)) throw std::runtime_error( \
        std::string(#x " is false at line ") + std::to_string(__LINE__)); } while(0)

/// Helper to write a temporary file and clean up on destruction.
struct TmpFile
{
    std::string path;
    TmpFile(const std::string& name, const std::string& content)
        : path(std::filesystem::temp_directory_path() / name)
    {
        std::ofstream ofs(path, std::ios::trunc);
        ofs << content;
    }
    ~TmpFile() { std::filesystem::remove(path); }
};

// ---- Test 1: Parse input CSV ----
TEST(parse_input_csv)
{
    TmpFile f("qc_test_input.csv",
        "ID,T1,T2\n"
        "sub01,/data/sub01_t1.mnc,/data/sub01_t2.mnc\n"
        "sub02,/data/sub02_t1.mnc,/data/sub02_t2.mnc\n"
        "sub03,/data/sub03_t1.mnc,/data/sub03_t2.mnc\n");

    QCState qc;
    qc.loadInputCsv(f.path);

    ASSERT_EQ(qc.columnCount(), 2);
    ASSERT_EQ(qc.rowCount(), 3);
    ASSERT_EQ(qc.columnNames[0], "T1");
    ASSERT_EQ(qc.columnNames[1], "T2");
    ASSERT_EQ(qc.rowIds[0], "sub01");
    ASSERT_EQ(qc.rowIds[2], "sub03");
    ASSERT_EQ(qc.rowPaths[1][0], "/data/sub02_t1.mnc");
    ASSERT_EQ(qc.rowPaths[1][1], "/data/sub02_t2.mnc");

    // All results should be UNRATED
    for (const auto& r : qc.results)
    {
        for (auto v : r.verdicts)
            ASSERT_EQ(v, QCVerdict::UNRATED);
        for (const auto& c : r.comments)
            ASSERT_TRUE(c.empty());
    }
}

// ---- Test 2: Quoted fields in input CSV ----
TEST(quoted_fields)
{
    TmpFile f("qc_test_quoted.csv",
        "ID,Volume\n"
        "\"sub,01\",\"/path/with \"\"quotes\"\"\"\n"
        "sub02,/normal/path.mnc\n");

    QCState qc;
    qc.loadInputCsv(f.path);

    ASSERT_EQ(qc.rowCount(), 2);
    ASSERT_EQ(qc.rowIds[0], "sub,01");
    ASSERT_EQ(qc.rowPaths[0][0], "/path/with \"quotes\"");
    ASSERT_EQ(qc.rowIds[1], "sub02");
    ASSERT_EQ(qc.rowPaths[1][0], "/normal/path.mnc");
}

// ---- Test 3: Write + read round-trip ----
TEST(output_round_trip)
{
    // Create input
    TmpFile fin("qc_rt_input.csv",
        "ID,T1,T2\n"
        "sub01,a.mnc,b.mnc\n"
        "sub02,c.mnc,d.mnc\n"
        "sub03,e.mnc,f.mnc\n");

    std::string outPath = (std::filesystem::temp_directory_path() / "qc_rt_output.csv").string();

    // Load input and set some verdicts
    QCState qc1;
    qc1.loadInputCsv(fin.path);
    qc1.outputCsvPath = outPath;

    qc1.results[0].verdicts[0] = QCVerdict::PASS;
    qc1.results[0].verdicts[1] = QCVerdict::FAIL;
    qc1.results[0].comments[1] = "Bad quality";
    qc1.results[1].verdicts[0] = QCVerdict::PASS;
    qc1.results[1].verdicts[1] = QCVerdict::PASS;
    qc1.results[2].comments[0] = "Comment with, comma";

    qc1.saveOutputCsv();

    // Load into new QCState
    QCState qc2;
    qc2.loadInputCsv(fin.path);
    qc2.loadOutputCsv(outPath);

    ASSERT_EQ(qc2.results[0].verdicts[0], QCVerdict::PASS);
    ASSERT_EQ(qc2.results[0].verdicts[1], QCVerdict::FAIL);
    ASSERT_EQ(qc2.results[0].comments[1], "Bad quality");
    ASSERT_EQ(qc2.results[1].verdicts[0], QCVerdict::PASS);
    ASSERT_EQ(qc2.results[1].verdicts[1], QCVerdict::PASS);
    ASSERT_EQ(qc2.results[2].verdicts[0], QCVerdict::UNRATED);
    ASSERT_EQ(qc2.results[2].comments[0], "Comment with, comma");

    std::filesystem::remove(outPath);
}

// ---- Test 4: Missing output file ----
TEST(missing_output_file)
{
    TmpFile fin("qc_missing_input.csv",
        "ID,Vol\n"
        "sub01,a.mnc\n");

    QCState qc;
    qc.loadInputCsv(fin.path);

    // Should not throw
    qc.loadOutputCsv("/nonexistent/path/results.csv");

    // Results should remain UNRATED
    ASSERT_EQ(qc.results[0].verdicts[0], QCVerdict::UNRATED);
}

// ---- Test 5: Partial output (fewer rows) ----
TEST(partial_output)
{
    TmpFile fin("qc_partial_input.csv",
        "ID,Vol\n"
        "sub01,a.mnc\n"
        "sub02,b.mnc\n"
        "sub03,c.mnc\n");

    std::string outPath = (std::filesystem::temp_directory_path() / "qc_partial_output.csv").string();

    // Write output with only sub02 rated
    {
        std::ofstream ofs(outPath, std::ios::trunc);
        ofs << "ID,Vol_verdict,Vol_comment\n";
        ofs << "sub02,PASS,looks good\n";
    }

    QCState qc;
    qc.loadInputCsv(fin.path);
    qc.loadOutputCsv(outPath);

    ASSERT_EQ(qc.results[0].verdicts[0], QCVerdict::UNRATED); // sub01 not in output
    ASSERT_EQ(qc.results[1].verdicts[0], QCVerdict::PASS);    // sub02 matched
    ASSERT_EQ(qc.results[1].comments[0], "looks good");
    ASSERT_EQ(qc.results[2].verdicts[0], QCVerdict::UNRATED); // sub03 not in output

    std::filesystem::remove(outPath);
}

// ---- Test 6: ratedCount / firstUnratedRow ----
TEST(rated_count_and_first_unrated)
{
    TmpFile fin("qc_helpers_input.csv",
        "ID,T1,T2\n"
        "sub01,a.mnc,b.mnc\n"
        "sub02,c.mnc,d.mnc\n"
        "sub03,e.mnc,f.mnc\n");

    QCState qc;
    qc.loadInputCsv(fin.path);

    ASSERT_EQ(qc.ratedCount(), 0);
    ASSERT_EQ(qc.firstUnratedRow(), 0);

    // Rate sub01 partially (one column)
    qc.results[0].verdicts[0] = QCVerdict::PASS;
    ASSERT_EQ(qc.ratedCount(), 1);
    ASSERT_EQ(qc.firstUnratedRow(), 1); // sub02 is first fully unrated

    // Rate sub02
    qc.results[1].verdicts[1] = QCVerdict::FAIL;
    ASSERT_EQ(qc.ratedCount(), 2);
    ASSERT_EQ(qc.firstUnratedRow(), 2); // sub03

    // Rate sub03
    qc.results[2].verdicts[0] = QCVerdict::PASS;
    ASSERT_EQ(qc.ratedCount(), 3);
    ASSERT_EQ(qc.firstUnratedRow(), -1); // all rated
}

int main()
{
    std::cout << "QC CSV Tests:\n";
    // Tests are auto-registered by static constructors above
    std::cout << "\n" << testsPassed << " passed, " << testsFailed << " failed\n";
    return testsFailed > 0 ? 1 : 0;
}
