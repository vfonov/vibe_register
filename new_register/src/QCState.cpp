#include "QCState.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Internal helpers — lightweight RFC 4180 CSV parser/writer
// ---------------------------------------------------------------------------

namespace
{

/// Parse CSV text into records of fields (RFC 4180). Fields may be enclosed
/// in double quotes; a literal quote inside a quoted field is written as two
/// quotes, and a quoted field may span line breaks. A leading UTF-8 BOM is
/// skipped, CRLF line endings are accepted and blank lines are dropped.
std::vector<std::vector<std::string>> parseCsv(const std::string& text)
{
    std::vector<std::vector<std::string>> records;
    std::vector<std::string> fields;
    std::string field;
    bool inQuotes = false;
    bool recordHasContent = false;

    size_t i = 0;
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0)
        i = 3;

    auto endRecord = [&]()
    {
        fields.push_back(std::move(field));
        field.clear();
        if (recordHasContent)
            records.push_back(std::move(fields));
        fields.clear();
        recordHasContent = false;
    };

    for (; i < text.size(); ++i)
    {
        char c = text[i];
        if (inQuotes)
        {
            if (c == '"')
            {
                // Peek ahead: doubled quote is an escaped literal quote
                if (i + 1 < text.size() && text[i + 1] == '"')
                {
                    field += '"';
                    ++i; // skip the second quote
                }
                else
                {
                    inQuotes = false; // closing quote
                }
            }
            else
            {
                field += c;
            }
            continue;
        }

        if (c == '"')
        {
            inQuotes = true;
            recordHasContent = true;
        }
        else if (c == ',')
        {
            fields.push_back(std::move(field));
            field.clear();
            recordHasContent = true;
        }
        else if (c == '\n')
        {
            endRecord();
        }
        else if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
        {
            // CR of a CRLF line ending; the LF ends the record
        }
        else
        {
            field += c;
            recordHasContent = true;
        }
    }
    endRecord();
    return records;
}

/// Quote a CSV field if it contains commas, quotes, or newlines.
std::string quoteCsvField(const std::string& field)
{
    if (field.find_first_of(",\"\r\n") == std::string::npos)
        return field;

    std::string out;
    out.reserve(field.size() + 4);
    out += '"';
    for (char c : field)
    {
        if (c == '"')
            out += '"'; // double the quote
        out += c;
    }
    out += '"';
    return out;
}

/// Write a row of fields as a single CSV line to an output stream.
void writeCsvRow(std::ostream& os, const std::vector<std::string>& fields)
{
    for (size_t i = 0; i < fields.size(); ++i)
    {
        if (i > 0)
            os << ',';
        os << quoteCsvField(fields[i]);
    }
    os << '\n';
}

/// Read a whole CSV file into records.
std::vector<std::vector<std::string>> readCsvFile(const std::string& path)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs)
        throw std::runtime_error("Cannot open file: " + path);
    std::stringstream ss;
    ss << ifs.rdbuf();
    return parseCsv(ss.str());
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// CSV I/O
// ---------------------------------------------------------------------------

void QCState::loadInputCsv(const std::string& path)
{
    auto records = readCsvFile(path);
    if (records.empty())
        throw std::runtime_error("QC input CSV is empty: " + path);

    // Parse header
    const auto& header = records[0];
    if (header.empty())
        throw std::runtime_error("QC input CSV is empty: " + path);

    // First column must be "ID" (case-insensitive)
    std::string firstCol = header[0];
    std::transform(firstCol.begin(), firstCol.end(), firstCol.begin(), ::toupper);
    if (firstCol != "ID")
        throw std::runtime_error("QC input CSV first column must be 'ID', got: " +
                                 header[0]);

    columnNames.clear();
    for (size_t i = 1; i < header.size(); ++i)
        columnNames.push_back(header[i]);

    if (columnNames.empty())
        throw std::runtime_error("QC input CSV has no data columns: " + path);

    // Read data rows
    rowIds.clear();
    rowPaths.clear();
    results.clear();

    for (size_t li = 1; li < records.size(); ++li)
    {
        const auto& fields = records[li];
        if (fields.empty())
            continue;

        rowIds.push_back(fields[0]);

        std::vector<std::string> paths;
        for (size_t ci = 0; ci < columnNames.size(); ++ci)
        {
            if (ci + 1 < fields.size())
                paths.push_back(fields[ci + 1]);
            else
                paths.push_back(""); // missing field
        }
        rowPaths.push_back(std::move(paths));

        // Initialise result to unknown
        QCRowResult result;
        result.id = rowIds.back();
        if (singleVerdictMode)
        {
            result.verdicts.assign(1, QC_UNKNOWN);
            result.comments.assign(1, "");
        }
        else
        {
            result.verdicts.resize(columnNames.size(), QC_UNKNOWN);
            result.comments.resize(columnNames.size());
        }
        results.push_back(std::move(result));
    }
}

void QCState::loadOutputCsv(const std::string& path)
{
    otherOutputRows.clear();
    if (!std::filesystem::exists(path))
        return;

    std::vector<std::vector<std::string>> records;
    try
    {
        records = readCsvFile(path);
    }
    catch (...)
    {
        return; // Can't read output file — treat as absent
    }
    if (records.empty())
        return;

    // Parse header
    const auto& outCols = records[0];
    if (outCols.empty())
        return;

    // Keep rows for IDs that are not in the input CSV, so that saving
    // does not drop verdicts made with a different input list.
    auto keepOtherRow = [&](const std::vector<std::string>& fields)
    {
        std::map<std::string, std::string> row;
        for (size_t i = 0; i < outCols.size() && i < fields.size(); ++i)
            row[outCols[i]] = fields[i];
        row["ID"] = fields[0];
        otherOutputRows.push_back(std::move(row));
    };

    // Single-verdict mode: expect ID,verdict,comment
    if (singleVerdictMode)
    {
        // Build row ID -> index map for fast lookup
        std::map<std::string, int> idMap;
        for (size_t i = 0; i < rowIds.size(); ++i)
            idMap[rowIds[i]] = static_cast<int>(i);

        // Find column indices
        std::map<std::string, size_t> hdrIdx;
        for (size_t i = 0; i < outCols.size(); ++i)
            hdrIdx[outCols[i]] = i;

        auto verdictIt = hdrIdx.find("verdict");
        auto commentIt = hdrIdx.find("comment");
        if (verdictIt == hdrIdx.end())
            return; // unrecognised format, skip

        size_t vIdx = verdictIt->second;
        size_t cIdx = commentIt != hdrIdx.end() ? commentIt->second : SIZE_MAX;

        for (size_t li = 1; li < records.size(); ++li)
        {
            const auto& fields = records[li];
            if (fields.empty())
                continue;

            std::string id = fields[0];
            auto it = idMap.find(id);
            if (it == idMap.end())
            {
                keepOtherRow(fields);
                continue;
            }

            int rowIdx = it->second;
            auto& result = results[rowIdx];

            if (vIdx < fields.size())
            {
                const auto& v = fields[vIdx];
                auto it = std::find(verdictOptions.begin(), verdictOptions.end(), v);
                result.verdicts[0] = (it != verdictOptions.end())
                    ? static_cast<int>(it - verdictOptions.begin())
                    : QC_UNKNOWN;
            }
            if (cIdx != SIZE_MAX && cIdx < fields.size())
                result.comments[0] = fields[cIdx];
        }
        return;
    }

    // Build header-name -> column-index map for the output CSV
    std::map<std::string, size_t> outColIndex;
    for (size_t i = 0; i < outCols.size(); ++i)
        outColIndex[outCols[i]] = i;

    // Build map: input column name -> (verdict col idx, comment col idx) in output
    struct ColIndices { size_t verdictIdx; size_t commentIdx; bool hasComment; };
    std::map<std::string, ColIndices> colMap;
    for (const auto& colName : columnNames)
    {
        std::string vh = colName + "_verdict";
        auto vit = outColIndex.find(vh);
        if (vit == outColIndex.end())
            continue;

        std::string ch = colName + "_comment";
        auto cit = outColIndex.find(ch);
        colMap[colName] = {vit->second,
                           cit != outColIndex.end() ? cit->second : 0,
                           cit != outColIndex.end()};
    }

    // Build row ID -> index map for fast lookup
    std::map<std::string, int> idMap;
    for (size_t i = 0; i < rowIds.size(); ++i)
        idMap[rowIds[i]] = static_cast<int>(i);

    // Parse data rows
    for (size_t li = 1; li < records.size(); ++li)
    {
        const auto& fields = records[li];
        if (fields.empty())
            continue;

        std::string id = fields[0];
        auto it = idMap.find(id);
        if (it == idMap.end())
        {
            keepOtherRow(fields);
            continue;
        }

        int rowIdx = it->second;
        auto& result = results[rowIdx];

        for (size_t ci = 0; ci < columnNames.size(); ++ci)
        {
            auto mapIt = colMap.find(columnNames[ci]);
            if (mapIt == colMap.end())
                continue;

            const auto& idx = mapIt->second;

            if (idx.verdictIdx < fields.size())
            {
                const auto& v = fields[idx.verdictIdx];
                auto it = std::find(verdictOptions.begin(), verdictOptions.end(), v);
                result.verdicts[ci] = (it != verdictOptions.end())
                    ? static_cast<int>(it - verdictOptions.begin())
                    : QC_UNKNOWN;
            }

            if (idx.hasComment && idx.commentIdx < fields.size())
                result.comments[ci] = fields[idx.commentIdx];
        }
    }
}

bool QCState::saveOutputCsv() const
{
    if (outputCsvPath.empty())
        return true;

    const std::string tmpPath = outputCsvPath + ".tmp";
    std::ofstream ofs(tmpPath, std::ios::trunc | std::ios::binary);
    if (!ofs)
    {
        std::cerr << "Cannot write QC output CSV: " << tmpPath << "\n";
        return false;
    }

    std::vector<std::string> header;
    if (singleVerdictMode)
    {
        // Simplified output: ID,verdict,comment
        header = {"ID", "verdict", "comment"};
        writeCsvRow(ofs, header);
        for (size_t i = 0; i < results.size(); ++i)
        {
            const char* verdictStr = "";
            if (!results[i].verdicts.empty())
            {
                int v = results[i].verdicts[0];
                if (v >= 0 && v < static_cast<int>(verdictOptions.size()))
                    verdictStr = verdictOptions[v].c_str();
            }
            std::string comment = results[i].comments.empty() ? "" : results[i].comments[0];
            writeCsvRow(ofs, {rowIds[i], verdictStr, comment});
        }
    }
    else
    {
        // Write header: ID, col1_verdict, col1_comment, ...
        header.push_back("ID");
        for (const auto& col : columnNames)
        {
            header.push_back(col + "_verdict");
            header.push_back(col + "_comment");
        }
        writeCsvRow(ofs, header);

        // Write rows
        for (size_t i = 0; i < results.size(); ++i)
        {
            std::vector<std::string> row;
            row.push_back(rowIds[i]);
            for (size_t ci = 0; ci < columnNames.size(); ++ci)
            {
                const char* verdictStr = "";
                if (ci < results[i].verdicts.size())
                {
                    int v = results[i].verdicts[ci];
                    if (v >= 0 && v < static_cast<int>(verdictOptions.size()))
                        verdictStr = verdictOptions[v].c_str();
                }
                row.push_back(verdictStr);

                std::string comment;
                if (ci < results[i].comments.size())
                    comment = results[i].comments[ci];
                row.push_back(comment);
            }
            writeCsvRow(ofs, row);
        }
    }

    // Rows for IDs outside the current input list, mapped onto this header
    for (const auto& other : otherOutputRows)
    {
        std::vector<std::string> row;
        for (const auto& name : header)
        {
            auto it = other.find(name);
            row.push_back(it != other.end() ? it->second : "");
        }
        writeCsvRow(ofs, row);
    }

    ofs.close();
    std::error_code ec;
    if (!ofs)
    {
        std::cerr << "Failed writing QC output CSV: " << tmpPath << "\n";
        std::filesystem::remove(tmpPath, ec);
        return false;
    }
    std::filesystem::rename(tmpPath, outputCsvPath, ec);
    if (ec)
    {
        std::cerr << "Cannot replace QC output CSV " << outputCsvPath
                  << ": " << ec.message() << "\n";
        std::filesystem::remove(tmpPath, ec);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

int QCState::columnCount() const
{
    return static_cast<int>(columnNames.size());
}

int QCState::rowCount() const
{
    return static_cast<int>(rowIds.size());
}

int QCState::ratedCount() const
{
    int count = 0;
    for (const auto& r : results)
    {
        bool anyRated = std::any_of(r.verdicts.begin(), r.verdicts.end(),
            [](QCVerdict v) { return v != QC_UNKNOWN; });
        if (anyRated)
            ++count;
    }
    return count;
}

int QCState::firstUnratedRow() const
{
    for (size_t i = 0; i < results.size(); ++i)
    {
        bool allUnrated = std::all_of(results[i].verdicts.begin(),
            results[i].verdicts.end(),
            [](QCVerdict v) { return v == QC_UNKNOWN; });
        if (allUnrated)
            return static_cast<int>(i);
    }
    return -1;
}

const std::vector<std::string>& QCState::pathsForRow(int row) const
{
    return rowPaths[row];
}
