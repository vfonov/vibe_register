#pragma once

#include <string>

class Volume;

namespace mriv::term
{

/// Format volume metadata for `--info`: dimensions, step, start, dirCos,
/// value range, and the world coordinates of the (0,0,0) and
/// (max,max,max) corner voxels. Modelled on
/// new_register/tests/dump_vol.cpp (see mriv/PLAN.md's --info section).
/// Pure formatting -- no I/O -- so it's testable against
/// Volume::generate_test_data() without touching disk.
std::string formatVolumeInfo(const Volume& vol, const std::string& path);

} // namespace mriv::term
