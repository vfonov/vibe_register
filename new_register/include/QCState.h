#pragma once

#include <map>
#include <string>
#include <vector>

#include "AppConfig.h"

/// Verdict for a single volume column within a QC row.
enum class QCVerdict { UNRATED, PASS, FAIL };

/// Per-row QC result: one verdict + comment per column.
struct QCRowResult
{
    std::string id;
    std::vector<QCVerdict> verdicts;   // parallel with columnNames
    std::vector<std::string> comments; // parallel with columnNames
};

/// Full QC session state: input CSV data, output results, runtime navigation.
class QCState
{
public:
    bool active = false;

    std::string inputCsvPath;
    std::string outputCsvPath;

    /// Column names parsed from the input CSV header (excluding "ID").
    std::vector<std::string> columnNames;

    /// Per-row data from the input CSV.
    std::vector<std::string> rowIds;
    std::vector<std::vector<std::string>> rowPaths; // rowPaths[row][col]

    /// Per-row results (parallel with rowIds).
    std::vector<QCRowResult> results;

    /// Per-column display config (from JSON config, keyed by column name).
    std::map<std::string, QCColumnConfig> columnConfigs;

    /// Currently displayed row index (-1 if none).
    int currentRowIndex = -1;

    /// Whether the overlay panel is visible in QC mode.
    bool showOverlay = true;

    // --- CSV I/O ---

    /// Parse the input CSV file. Populates columnNames, rowIds, rowPaths,
    /// and initialises results to UNRATED/empty.
    /// Throws std::runtime_error on missing ID column or empty file.
    void loadInputCsv(const std::string& path);

    /// Load previously saved verdicts from the output CSV.
    /// If the file does not exist, returns silently (results stay UNRATED).
    void loadOutputCsv(const std::string& path);

    /// Write all results to outputCsvPath (truncate mode, immediate flush).
    void saveOutputCsv() const;

    // --- Accessors ---

    int columnCount() const;
    int rowCount() const;

    /// Number of rows with at least one non-UNRATED verdict.
    int ratedCount() const;

    /// Index of first row where ALL verdicts are UNRATED, or -1 if all rated.
    int firstUnratedRow() const;

    /// Get file paths for a specific row.
    const std::vector<std::string>& pathsForRow(int row) const;
};
