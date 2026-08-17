/// test_volume_info.cpp — --info metadata formatting. Modelled on
/// new_register/tests/dump_vol.cpp, but pure (returns a string, no I/O),
/// so it's exercised against Volume::generate_test_data() with no disk
/// access.

#include <cassert>

#include "cli/VolumeInfo.hpp"

#include "Volume.h"

using namespace mriv::term;

namespace
{

void testContainsPath()
{
    Volume vol;
    vol.generate_test_data();
    std::string info = formatVolumeInfo(vol, "some/path.mnc");
    assert(info.find("some/path.mnc") != std::string::npos);
}

void testContainsDimensions()
{
    Volume vol;
    vol.generate_test_data();
    std::string info = formatVolumeInfo(vol, "x.mnc");
    // generate_test_data() gives a 256^3 isotropic volume (Volume.cpp).
    assert(info.find("256") != std::string::npos);
}

void testContainsValueRange()
{
    Volume vol;
    vol.generate_test_data();
    std::string info = formatVolumeInfo(vol, "x.mnc");
    assert(info.find("0") != std::string::npos);
    assert(info.find("1") != std::string::npos);
}

void testContainsStep()
{
    Volume vol;
    vol.generate_test_data();
    std::string info = formatVolumeInfo(vol, "x.mnc");
    // step = (1,1,1) for the isotropic test volume.
    assert(info.find("step") != std::string::npos);
}

} // namespace

int main()
{
    testContainsPath();
    testContainsDimensions();
    testContainsValueRange();
    testContainsStep();
    return 0;
}
