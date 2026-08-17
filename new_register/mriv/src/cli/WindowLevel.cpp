#include "cli/WindowLevel.hpp"

namespace mriv::term
{

ValueRange windowLevelToRange(double window, double level)
{
    return ValueRange{level - window / 2.0, level + window / 2.0};
}

} // namespace mriv::term
