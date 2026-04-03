#pragma once

#include <string>

class Volume;

/// Check if a filename indicates a NIfTI file (.nii or .nii.gz)
bool isNiftiFile(const std::string& filename);

/// Load a NIfTI file into a Volume object
/// @param filename Path to .nii or .nii.gz file
/// @param vol Volume object to populate
/// @throws std::runtime_error on file read error or unsupported format
void loadNiftiFile(const std::string& filename, Volume& vol);
