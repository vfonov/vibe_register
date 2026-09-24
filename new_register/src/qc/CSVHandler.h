#ifndef CSV_HANDLER_H
#define CSV_HANDLER_H

#include <string>
#include <vector>
#include <fstream>
#include <sstream>

namespace QC
{

// Verdict strings stored in the QC column (empty = not yet rated).
// Shared by new_qc and mriv_qc so both read and write the same CSV.
inline constexpr const char* kStatusPass = "Pass";
inline constexpr const char* kStatusWarning = "Warning";
inline constexpr const char* kStatusFail = "Fail";

struct QCRecord
{
    std::string id;
    std::string visit;
    std::string picture_path;
    std::string qc_status;      // "Pass", "Warning", "Fail", or empty
    std::string notes;
};

class CSVHandler
{
public:
    CSVHandler() = default;
    ~CSVHandler() = default;

    // Load input CSV (id, visit, picture columns)
    bool loadInputCSV(const std::string& filename);

    // Build records from a newline-separated list of image paths (stdin mode).
    // id = filename stem, visit = "-", picture = path as given. Blank lines skipped.
    bool loadImageList(std::istream& input);

    // Build records from image paths (e.g. positional command-line arguments),
    // with the same id/visit rules as loadImageList(). Blank paths skipped.
    bool loadImagePaths(const std::vector<std::string>& paths);

    // Load existing output CSV to resume work
    bool loadOutputCSV(const std::string& filename);

    // Save output CSV (id, visit, picture, QC, notes). Writes nothing to
    // stdout -- mriv_qc autosaves while notcurses owns the terminal.
    bool saveOutputCSV(const std::string& filename);

    // Get all records
    const std::vector<QCRecord>& getRecords() const { return records_; }
    std::vector<QCRecord>& getRecords() { return records_; }

    // Get record count
    size_t getRecordCount() const { return records_.size(); }

private:
    std::vector<QCRecord> records_;

    // Helper: trim whitespace
    static std::string trim(const std::string& str);

    // Helper: parse CSV line
    static std::vector<std::string> parseCSVLine(const std::string& line);

    // Helper: escape CSV field
    static std::string escapeCSVField(const std::string& field);
};

} // namespace QC

#endif // CSV_HANDLER_H
