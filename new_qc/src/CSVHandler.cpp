#include "CSVHandler.h"
#include <iostream>
#include <algorithm>
#include <cctype>

namespace QC
{

std::string CSVHandler::trim(const std::string& str)
{
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";
    
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::vector<std::string> CSVHandler::parseCSVLine(const std::string& line)
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
                // Check for escaped quote
                if (i + 1 < line.size() && line[i + 1] == '"')
                {
                    field += '"';
                    ++i;
                }
                else
                {
                    inQuotes = false;
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
                fields.push_back(trim(field));
                field.clear();
            }
            else
            {
                field += c;
            }
        }
    }
    
    // Add last field
    fields.push_back(trim(field));
    
    return fields;
}

std::string CSVHandler::escapeCSVField(const std::string& field)
{
    bool needsQuotes = field.find(',') != std::string::npos ||
                       field.find('"') != std::string::npos ||
                       field.find('\n') != std::string::npos;
    
    if (!needsQuotes)
        return field;
    
    std::string escaped = "\"";
    for (char c : field)
    {
        if (c == '"')
            escaped += "\"\"";
        else
            escaped += c;
    }
    escaped += "\"";
    
    return escaped;
}

bool CSVHandler::loadInputCSV(const std::string& filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Error: Cannot open input file: " << filename << std::endl;
        return false;
    }
    
    records_.clear();
    
    std::string line;
    bool headerSkipped = false;
    
    while (std::getline(file, line))
    {
        line = trim(line);
        if (line.empty())
            continue;
        
        auto fields = parseCSVLine(line);
        
        // Skip header row if it looks like a header
        if (!headerSkipped)
        {
            if (fields.size() >= 3)
            {
                std::string col0 = fields[0];
                std::transform(col0.begin(), col0.end(), col0.begin(), ::tolower);
                
                if (col0 == "id" || col0 == "subject" || col0 == "patient")
                {
                    headerSkipped = true;
                    continue;
                }
            }
            headerSkipped = true;
        }
        
        if (fields.size() < 3)
        {
            std::cerr << "Warning: Skipping malformed row: " << line << std::endl;
            continue;
        }
        
        QCRecord record;
        record.id = fields[0];
        record.visit = fields[1];
        record.picture_path = fields[2];
        record.qc_status = "";
        record.notes = "";
        
        records_.push_back(record);
    }
    
    std::cout << "Loaded " << records_.size() << " records from " << filename << std::endl;
    return !records_.empty();
}

bool CSVHandler::loadOutputCSV(const std::string& filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Info: Output file does not exist yet: " << filename << std::endl;
        return false;
    }
    
    std::vector<QCRecord> loadedRecords;
    
    std::string line;
    bool headerSkipped = false;
    
    while (std::getline(file, line))
    {
        line = trim(line);
        if (line.empty())
            continue;
        
        auto fields = parseCSVLine(line);
        
        // Skip header row
        if (!headerSkipped)
        {
            if (fields.size() >= 4)
            {
                std::string col0 = fields[0];
                std::transform(col0.begin(), col0.end(), col0.begin(), ::tolower);
                
                if (col0 == "id" || col0 == "subject")
                {
                    headerSkipped = true;
                    continue;
                }
            }
            headerSkipped = true;
        }
        
        if (fields.size() < 4)
        {
            std::cerr << "Warning: Skipping malformed row in output: " << line << std::endl;
            continue;
        }
        
        QCRecord record;
        record.id = fields[0];
        record.visit = fields[1];
        record.picture_path = fields[2];
        record.qc_status = fields[3];
        record.notes = (fields.size() >= 5) ? fields[4] : "";
        
        loadedRecords.push_back(record);
    }
    
    if (!loadedRecords.empty())
    {
        records_ = loadedRecords;
        std::cout << "Resumed from " << filename << " (" << records_.size() << " records)" << std::endl;
        return true;
    }
    
    return false;
}

bool CSVHandler::saveOutputCSV(const std::string& filename)
{
    std::ofstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Error: Cannot create output file: " << filename << std::endl;
        return false;
    }
    
    // Write header
    file << "id,visit,picture,QC,notes\n";
    
    // Write records
    for (const auto& record : records_)
    {
        file << escapeCSVField(record.id) << ","
             << escapeCSVField(record.visit) << ","
             << escapeCSVField(record.picture_path) << ","
             << escapeCSVField(record.qc_status) << ","
             << escapeCSVField(record.notes) << "\n";
    }
    
    std::cout << "Saved " << records_.size() << " records to " << filename << std::endl;
    return true;
}

} // namespace QC
