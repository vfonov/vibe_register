#pragma once

#include <string>

class Volume;

/// Check if a filename indicates a NIfTI file (.nii, .nii.gz, or a .hdr/.img pair,
/// optionally gzipped as .hdr.gz/.img.gz; case-insensitive)
bool isNiftiFile(const std::string& filename);

/// Load a NIfTI file into a Volume object
/// @param filename Path to a .nii / .nii.gz file, or to either half of a .hdr/.img pair
/// @param vol Volume object to populate
/// @throws std::runtime_error on file read error or unsupported format
void loadNiftiFile(const std::string& filename, Volume& vol);
