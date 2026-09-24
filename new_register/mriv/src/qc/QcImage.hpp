#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "render/Resample.hpp"

namespace mriv::qc
{

/// A decoded picture as packed RGBA (0xAABBGGRR, R in the lowest byte) --
/// the same layout mriv_term's resampler, compositor and Screen take.
struct Image
{
    std::vector<uint32_t> pixels;
    int width = 0;
    int height = 0;
};

/// Decode a PNG or JPEG (anything stb_image reads) with alpha forced on.
/// Returns std::nullopt if the file is missing or cannot be decoded; the
/// caller shows that in the status row instead of failing the session.
std::optional<Image> loadImage(const std::string& path);

/// Fit `image` into a (boxW, boxH) pixel box, keeping its aspect ratio.
///
/// `scale` <= 0 means auto: a picture smaller than the box is magnified by
/// the largest whole factor that still fits (QC thumbnails are often tiny
/// next to a terminal window), and a larger one is shrunk to fit. A
/// positive `scale` is used as-is, the same as mriv's --scale. Returns an
/// empty image for an empty source or box.
mriv::term::ResampledImage fitImageToBox(const Image& image, int boxW, int boxH, int scale);

/// How many whole terminal cells to shift an `imagePx`-wide image to centre
/// it in a `boxPx`-wide box. A partly covered cell counts as covered. Zero
/// when the image fills the box or the cell size is unknown.
int centreOffsetCells(int boxPx, int imagePx, int cellPx);

} // namespace mriv::qc
