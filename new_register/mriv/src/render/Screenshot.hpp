#pragma once

#include <cstdint>
#include <string>

namespace mriv::term
{

/// The lowest-numbered "screenshotNNNNNN.png" (%06d, 1-based) path in `dir`
/// that does not already exist -- the same convention new_register uses on
/// P (Interface::saveScreenshot, new_register/src/Interface.cpp), so a
/// script or habit built around one viewer's screenshots carries over to
/// the other unchanged. `dir` defaults to the current directory; passed
/// explicitly so tests can probe the numbering without touching the repo
/// tree or the caller's cwd.
std::string nextScreenshotFilename(const std::string& dir = ".");

/// Writes a packed RGBA (0xAABBGGRR) buffer of size (width,height) as a PNG
/// to nextScreenshotFilename(dir). Returns the path written on success; on
/// failure (no frame, or the PNG encoder rejecting it) returns an empty
/// string and writes a diagnostic to std::cerr.
std::string saveScreenshot(const std::uint32_t* rgba, int width, int height,
                           const std::string& dir = ".");

} // namespace mriv::term
