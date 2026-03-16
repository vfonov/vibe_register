#ifndef CSV_HANDLER_H
#define CSV_HANDLER_H

#include <string>
#include <vector>
#include <fstream>
#include <sstream>

namespace QC
{

struct QCRecord
{
    std::string id;
    std::string visit;
    std::string picture_path;
    std::string qc_status;      // "Pass", "Fail", or empty
    std::string notes;
};

class CSVHandler
{
public:
    CSVHandler() = default;
    ~CSVHandler() = default;

    // Load input CSV (id, visit, picture columns)
    bool loadInputCSV(const std::string& filename);

    // Load existing output CSV to resume work
    bool loadOutputCSV(const std::string& filename);

    // Save output CSV (id, visit, picture, QC, notes)
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
