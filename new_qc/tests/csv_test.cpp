#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cassert>
#include <filesystem>
#include "../src/CSVHandler.h"

namespace fs = std::filesystem;

// Test counters
static int testsRun = 0;
static int testsPassed = 0;
static int testsFailed = 0;

#define TEST_ASSERT(condition, message) \
    do { \
        testsRun++; \
        if (condition) { \
            std::cout << "[PASS] " << message << std::endl; \
            testsPassed++; \
        } else { \
            std::cout << "[FAIL] " << message << std::endl; \
            testsFailed++; \
        } \
    } while (0)

// Test CSV field escaping through save/load round-trip
void testCSVFieldEscaping()
{
    std::cout << "\n=== Testing CSV Field Escaping ===" << std::endl;
    
    QC::CSVHandler handler;
    QC::QCRecord record;
    
    // Test field with comma
    record.id = "Subject, Jr.";
    record.visit = "baseline";
    record.picture_path = "/path/to/img.png";
    record.qc_status = "Pass";
    record.notes = "Test notes";
    handler.getRecords().push_back(record);
    
    std::string testFile = "test_escape.csv";
    handler.saveOutputCSV(testFile);
    
    // Reload and verify
    QC::CSVHandler handler2;
    handler2.loadOutputCSV(testFile);
    
    TEST_ASSERT(handler2.getRecords()[0].id == "Subject, Jr.", "Comma in field preserved");
    TEST_ASSERT(handler2.getRecords()[0].qc_status == "Pass", "QC status preserved");
    
    fs::remove(testFile);
}

// Test loading input CSV
void testLoadInputCSV()
{
    std::cout << "\n=== Testing Load Input CSV ===" << std::endl;
    
    // Create test file
    std::string testFile = "test_input.csv";
    std::ofstream out(testFile);
    out << "id,visit,picture\n";
    out << "subj001,baseline,/path/to/img1.png\n";
    out << "subj002,followup,/path/to/img2.jpg\n";
    out << "subj003,month3,/path/to/img3.png\n";
    out.close();
    
    QC::CSVHandler handler;
    bool loaded = handler.loadInputCSV(testFile);
    
    TEST_ASSERT(loaded, "Input CSV loaded successfully");
    TEST_ASSERT(handler.getRecordCount() == 3, "3 records loaded");
    
    auto& records = handler.getRecords();
    TEST_ASSERT(records[0].id == "subj001", "First record ID correct");
    TEST_ASSERT(records[0].visit == "baseline", "First record visit correct");
    TEST_ASSERT(records[0].picture_path == "/path/to/img1.png", "First record picture path correct");
    TEST_ASSERT(records[0].qc_status == "", "First record QC status empty initially");
    TEST_ASSERT(records[0].notes == "", "First record notes empty initially");
    
    TEST_ASSERT(records[1].id == "subj002", "Second record ID correct");
    TEST_ASSERT(records[2].id == "subj003", "Third record ID correct");
    
    // Cleanup
    fs::remove(testFile);
}

// Test loading output CSV (resume functionality)
void testLoadOutputCSV()
{
    std::cout << "\n=== Testing Load Output CSV (Resume) ===" << std::endl;
    
    // Create test output file with QC status
    std::string testFile = "test_output.csv";
    std::ofstream out(testFile);
    out << "id,visit,picture,QC,notes\n";
    out << "subj001,baseline,/path/to/img1.png,Pass,Good quality\n";
    out << "subj002,followup,/path/to/img2.jpg,Fail,Artifact present\n";
    out << "subj003,month3,/path/to/img3.png,,Needs review\n";
    out.close();
    
    QC::CSVHandler handler;
    bool loaded = handler.loadOutputCSV(testFile);
    
    TEST_ASSERT(loaded, "Output CSV loaded successfully");
    TEST_ASSERT(handler.getRecordCount() == 3, "3 records loaded");
    
    auto& records = handler.getRecords();
    TEST_ASSERT(records[0].qc_status == "Pass", "First record QC status loaded");
    TEST_ASSERT(records[0].notes == "Good quality", "First record notes loaded");
    
    TEST_ASSERT(records[1].qc_status == "Fail", "Second record QC status loaded");
    TEST_ASSERT(records[1].notes == "Artifact present", "Second record notes loaded");
    
    TEST_ASSERT(records[2].qc_status == "", "Third record QC status empty");
    TEST_ASSERT(records[2].notes == "Needs review", "Third record notes loaded");
    
    // Cleanup
    fs::remove(testFile);
}

// Test saving output CSV
void testSaveOutputCSV()
{
    std::cout << "\n=== Testing Save Output CSV ===" << std::endl;
    
    QC::CSVHandler handler;
    
    // Manually create records
    QC::QCRecord record1;
    record1.id = "subj001";
    record1.visit = "baseline";
    record1.picture_path = "/path/to/img1.png";
    record1.qc_status = "Pass";
    record1.notes = "Good quality image";
    
    QC::QCRecord record2;
    record2.id = "subj002";
    record2.visit = "followup";
    record2.picture_path = "/path/to/img2.jpg";
    record2.qc_status = "Fail";
    record2.notes = "Motion artifact";
    
    QC::QCRecord record3;
    record3.id = "subj003";
    record3.visit = "month3";
    record3.picture_path = "/path/to/img3.png";
    record3.qc_status = "";
    record3.notes = "";
    
    handler.getRecords().push_back(record1);
    handler.getRecords().push_back(record2);
    handler.getRecords().push_back(record3);
    
    std::string outputFile = "test_saved_output.csv";
    bool saved = handler.saveOutputCSV(outputFile);
    
    TEST_ASSERT(saved, "Output CSV saved successfully");
    
    // Verify file exists and can be read
    std::ifstream inFile(outputFile);
    TEST_ASSERT(inFile.is_open(), "Output file exists and is readable");
    
    std::string line;
    std::getline(inFile, line);
    TEST_ASSERT(line == "id,visit,picture,QC,notes", "Header line correct");
    
    std::getline(inFile, line);
    TEST_ASSERT(line.find("subj001") != std::string::npos, "First record saved");
    TEST_ASSERT(line.find("Pass") != std::string::npos, "QC status saved");
    TEST_ASSERT(line.find("Good quality image") != std::string::npos, "Notes saved");
    
    std::getline(inFile, line);
    TEST_ASSERT(line.find("subj002") != std::string::npos, "Second record saved");
    TEST_ASSERT(line.find("Fail") != std::string::npos, "Fail status saved");
    
    inFile.close();
    
    // Cleanup
    fs::remove(outputFile);
}

// Test CSV with special characters
void testSpecialCharacters()
{
    std::cout << "\n=== Testing Special Characters ===" << std::endl;
    
    std::string testFile = "test_special.csv";
    std::ofstream out(testFile);
    out << "id,visit,picture,QC,notes\n";
    out << "\"Subject, Jr.\",baseline,\"/path/with, comma.png\",Pass,\"Test notes\"\n";
    out << "subj002,\"Visit \"\"A\"\"\",/path.png,Fail,\"More notes\"\n";
    out.close();
    
    QC::CSVHandler handler;
    bool loaded = handler.loadOutputCSV(testFile);
    
    TEST_ASSERT(loaded, "CSV with special characters loaded");
    TEST_ASSERT(handler.getRecordCount() == 2, "2 records loaded");
    
    auto& records = handler.getRecords();
    TEST_ASSERT(records[0].id == "Subject, Jr.", "Comma in quoted field handled");
    TEST_ASSERT(records[0].qc_status == "Pass", "QC status loaded");
    TEST_ASSERT(records[1].visit == "Visit \"A\"", "Escaped quotes handled");
    TEST_ASSERT(records[1].qc_status == "Fail", "Second record QC status loaded");
    
    // Cleanup
    fs::remove(testFile);
}

// Test empty file handling
void testEmptyFileHandling()
{
    std::cout << "\n=== Testing Empty File Handling ===" << std::endl;
    
    // Test non-existent file
    QC::CSVHandler handler1;
    bool loaded1 = handler1.loadInputCSV("nonexistent_file.csv");
    TEST_ASSERT(!loaded1, "Non-existent file returns false");
    
    // Test empty file
    std::string emptyFile = "test_empty.csv";
    std::ofstream out(emptyFile);
    out.close();
    
    QC::CSVHandler handler2;
    bool loaded2 = handler2.loadInputCSV(emptyFile);
    TEST_ASSERT(!loaded2, "Empty file returns false");
    
    // Cleanup
    fs::remove(emptyFile);
}

// Test resume from existing output
void testResumeFromOutput()
{
    std::cout << "\n=== Testing Resume From Existing Output ===" << std::endl;
    
    // Create output file with partial QC work
    std::string outputFile = "test_resume.csv";
    std::ofstream out(outputFile);
    out << "id,visit,picture,QC,notes\n";
    out << "subj001,baseline,/path/img1.png,Pass,Reviewed\n";
    out << "subj002,followup,/path/img2.jpg,,Pending\n";
    out << "subj003,month3,/path/img3.png,Fail,Artifacts\n";
    out.close();
    
    QC::CSVHandler handler;
    bool loaded = handler.loadOutputCSV(outputFile);
    
    TEST_ASSERT(loaded, "Resume file loaded");
    TEST_ASSERT(handler.getRecordCount() == 3, "3 records in resume file");
    
    auto& records = handler.getRecords();
    TEST_ASSERT(records[0].qc_status == "Pass", "First record already marked Pass");
    TEST_ASSERT(records[1].qc_status == "", "Second record pending");
    TEST_ASSERT(records[2].qc_status == "Fail", "Third record marked Fail");
    
    // Modify a record and save
    records[1].qc_status = "Pass";
    records[1].notes = "Updated notes";
    
    handler.saveOutputCSV(outputFile);
    
    // Reload and verify
    QC::CSVHandler handler2;
    handler2.loadOutputCSV(outputFile);
    
    TEST_ASSERT(handler2.getRecords()[1].qc_status == "Pass", "Updated QC status saved");
    TEST_ASSERT(handler2.getRecords()[1].notes == "Updated notes", "Updated notes saved");
    
    // Cleanup
    fs::remove(outputFile);
}

int main(int argc, char* argv[])
{
    std::cout << "========================================" << std::endl;
    std::cout << "CSV Handler Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Run all tests
    testCSVFieldEscaping();
    testLoadInputCSV();
    testLoadOutputCSV();
    testSaveOutputCSV();
    testSpecialCharacters();
    testEmptyFileHandling();
    testResumeFromOutput();
    
    // Summary
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test Summary:" << std::endl;
    std::cout << "  Total:  " << testsRun << std::endl;
    std::cout << "  Passed: " << testsPassed << std::endl;
    std::cout << "  Failed: " << testsFailed << std::endl;
    std::cout << "========================================" << std::endl;
    
    if (testsFailed > 0)
    {
        std::cout << "\n[FAIL] Some tests failed!" << std::endl;
        return 1;
    }
    
    std::cout << "\n[PASS] All tests passed!" << std::endl;
    return 0;
}
