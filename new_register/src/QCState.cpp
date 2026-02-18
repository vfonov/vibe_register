#include "QCState.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Internal helpers — lightweight RFC 4180 CSV parser/writer
// ---------------------------------------------------------------------------

namespace
{

/// Parse a single CSV line into fields, respecting double-quote escaping.
/// RFC 4180: fields may be enclosed in double quotes; a literal double quote
/// inside a quoted field is represented as two consecutive double quotes.
std::vector<std::string> parseCsvLine(const std::string& line)
{
    std::vector<std::string> fields;
    std::string field;
    bool inQuotes = false;

    for (size_t i = 0; i < line.size(); ++i)
    {
        char c = line[i];
        if (inQuotes)
        {
            if (c == '"')
            {
                // Peek ahead: doubled quote is an escaped literal quote
                if (i + 1 < line.size() && line[i + 1] == '"')
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
        }
        else
        {
            if (c == '"')
            {
                inQuotes = true;
            }
            else if (c == ',')
            {
                fields.push_back(std::move(field));
                field.clear();
            }
            else
            {
                field += c;
            }
        }
    }
    fields.push_back(std::move(field));
    return fields;
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

/// Read all non-empty lines from a file, stripping trailing \r.
std::vector<std::string> readLines(const std::string& path)
{
    std::ifstream ifs(path);
    if (!ifs)
        throw std::runtime_error("Cannot open file: " + path);

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(ifs, line))
    {
        // Strip trailing \r (Windows line endings)
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty())
            lines.push_back(std::move(line));
    }
    return lines;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// CSV I/O
// ---------------------------------------------------------------------------

void QCState::loadInputCsv(const std::string& path)
{
    auto lines = readLines(path);
    if (lines.empty())
        throw std::runtime_error("QC input CSV is empty: " + path);

    // Parse header
    auto header = parseCsvLine(lines[0]);
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

    for (size_t li = 1; li < lines.size(); ++li)
    {
        auto fields = parseCsvLine(lines[li]);
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

        // Initialise result to UNRATED
        QCRowResult result;
        result.id = rowIds.back();
        result.verdicts.resize(columnNames.size(), QCVerdict::UNRATED);
        result.comments.resize(columnNames.size());
        results.push_back(std::move(result));
    }
}

void QCState::loadOutputCsv(const std::string& path)
{
    if (!std::filesystem::exists(path))
        return;

    std::vector<std::string> lines;
    try
    {
        lines = readLines(path);
    }
    catch (...)
    {
        return; // Can't read output file — treat as absent
    }
    if (lines.empty())
        return;

    // Parse header
    auto outCols = parseCsvLine(lines[0]);
    if (outCols.empty())
        return;

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
    for (size_t li = 1; li < lines.size(); ++li)
    {
        auto fields = parseCsvLine(lines[li]);
        if (fields.empty())
            continue;

        std::string id = fields[0];
        auto it = idMap.find(id);
        if (it == idMap.end())
            continue; // Unknown ID, skip

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
                if (v == "PASS")
                    result.verdicts[ci] = QCVerdict::PASS;
                else if (v == "FAIL")
                    result.verdicts[ci] = QCVerdict::FAIL;
                else
                    result.verdicts[ci] = QCVerdict::UNRATED;
            }

            if (idx.hasComment && idx.commentIdx < fields.size())
                result.comments[ci] = fields[idx.commentIdx];
        }
    }
}

void QCState::saveOutputCsv() const
{
    if (outputCsvPath.empty())
        return;

    std::ofstream ofs(outputCsvPath, std::ios::trunc);
    if (!ofs)
        return;

    // Write header: ID, col1_verdict, col1_comment, ...
    std::vector<std::string> header;
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
                switch (results[i].verdicts[ci])
                {
                case QCVerdict::PASS: verdictStr = "PASS"; break;
                case QCVerdict::FAIL: verdictStr = "FAIL"; break;
                default: verdictStr = ""; break;
                }
            }
            row.push_back(verdictStr);

            std::string comment;
            if (ci < results[i].comments.size())
                comment = results[i].comments[ci];
            row.push_back(comment);
        }
        writeCsvRow(ofs, row);
    }

    ofs.flush();
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
            [](QCVerdict v) { return v != QCVerdict::UNRATED; });
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
            [](QCVerdict v) { return v == QCVerdict::UNRATED; });
        if (allUnrated)
            return static_cast<int>(i);
    }
    return -1;
}

const std::vector<std::string>& QCState::pathsForRow(int row) const
{
    return rowPaths[row];
}
