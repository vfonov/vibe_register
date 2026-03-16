#include <iostream>
#include <string>
#include <cstring>
#include "QCApp.h"

void printUsage(const char* programName)
{
    std::cout << "Usage: " << programName << " [OPTIONS] <input_csv> <output_csv>\n"
              << "\n"
              << "Quality Control tool for medical imaging datasets.\n"
              << "\n"
              << "Arguments:\n"
              << "  input_csv    CSV file with columns: id, visit, picture\n"
              << "  output_csv   CSV file for QC results (will be created/updated)\n"
              << "\n"
              << "Options:\n"
              << "  --help       Show this help message\n"
              << "  --version    Show version information\n"
              << "  --scale <factor>  Override screen content scale (HiDPI)\n"
              << "\n"
              << "Input CSV format:\n"
              << "  id,visit,picture\n"
              << "  subject001,baseline,/path/to/image1.png\n"
              << "  subject002,followup,/path/to/image2.jpg\n"
              << "\n"
              << "Output CSV format:\n"
              << "  id,visit,picture,QC,notes\n"
              << "  subject001,baseline,/path/to/image1.png,Pass,Good quality\n"
              << "  subject002,followup,/path/to/image2.jpg,Fail,Artifact present\n"
              << "\n"
              << "Controls:\n"
              << "  P            Mark current image as Pass\n"
              << "  F            Mark current image as Fail\n"
              << "  Left/Right   Navigate between images\n"
              << "  Page Up/Down Navigate between images\n"
              << "  Ctrl+S       Save progress manually\n"
              << "  Escape       Exit application\n"
              << "\n"
              << "HiDPI Support:\n"
              << "  The tool automatically detects monitor content scale.\n"
              << "  Use --scale to override the detected value (e.g., --scale 1.5).\n"
              << "\n"
              << "The tool automatically saves progress after each QC decision.\n"
              << "Existing output files are loaded to resume interrupted work.\n";
}

void printVersion()
{
    std::cout << "new_qc version 1.0.0\n"
              << "Quality Control tool for medical imaging\n";
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        printUsage(argv[0]);
        return 1;
    }
    
    std::string inputFile;
    std::string outputFile;
    std::optional<float> scaleFactor;
    
    // Parse arguments
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h")
        {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "--version" || arg == "-v")
        {
            printVersion();
            return 0;
        }
        else if (arg == "--scale")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Error: --scale requires a value\n";
                return 1;
            }
            
            float scale = std::stof(argv[++i]);
            if (scale <= 0.0f)
            {
                std::cerr << "Error: --scale must be positive\n";
                return 1;
            }
            
            scaleFactor = scale;
        }
        else if (arg[0] == '-')
        {
            std::cerr << "Unknown option: " << arg << "\n";
            std::cerr << "Use --help for usage information\n";
            return 1;
        }
        else
        {
            // Positional arguments
            if (inputFile.empty())
            {
                inputFile = arg;
            }
            else if (outputFile.empty())
            {
                outputFile = arg;
            }
            else
            {
                std::cerr << "Error: Too many positional arguments\n";
                std::cerr << "Use --help for usage information\n";
                return 1;
            }
        }
    }
    
    if (inputFile.empty() || outputFile.empty())
    {
        std::cerr << "Error: Both input and output CSV files are required\n";
        std::cerr << "Use --help for usage information\n";
        return 1;
    }
    
   // Create and run QC application
    QC::QCApp app;
    
    if (!app.init(inputFile, outputFile, scaleFactor))
    {
        return 1;
    }
    
    app.run();
    app.shutdown();
    
    return 0;
}
