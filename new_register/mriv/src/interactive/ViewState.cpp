#include "interactive/ViewState.hpp"

#include <algorithm>

namespace mriv::term
{

namespace
{

/// One step of '+' / '-'. Multiplicative so the window scales the same way
/// at any magnitude and can never reach zero width or invert, however long
/// the user leans on '-'.
constexpr double kWindowStep = 1.1;

int clampIndex(int index, int count)
{
    return std::clamp(index, 0, count > 0 ? count - 1 : 0);
}

} // namespace

ViewState::ViewState(const glm::ivec3& dimensions,
                     char axis,
                     int sliceIndex,
                     double rangeLow,
                     double rangeHigh)
    : dimensions_(dimensions)
    , axis_(axis)
    , viewIndex_(viewIndexForAxis(axis).value_or(0))
    , rangeLow_(rangeLow)
    , rangeHigh_(rangeHigh)
{
    // The other two axes have no user-supplied position, so they start
    // where --slice would have put them by default.
    cursor_ = glm::ivec3(clampIndex(dimensions_.x / 2, dimensions_.x),
                         clampIndex(dimensions_.y / 2, dimensions_.y),
                         clampIndex(dimensions_.z / 2, dimensions_.z));
    currentSlice() = clampIndex(sliceIndex, sliceCount());
}

int& ViewState::currentSlice()
{
    switch (viewIndex_)
    {
        case 1:  return cursor_.x;
        case 2:  return cursor_.y;
        default: return cursor_.z;
    }
}

const int& ViewState::currentSlice() const
{
    return const_cast<ViewState*>(this)->currentSlice();
}

int ViewState::sliceIndex() const
{
    return currentSlice();
}

int ViewState::sliceCount() const
{
    return sliceCountForView(viewIndex_, dimensions_);
}

KeyResult ViewState::moveSlice(int delta)
{
    int wanted = clampIndex(currentSlice() + delta, sliceCount());
    if (wanted == currentSlice())
        return KeyResult::Ignored;

    currentSlice() = wanted;
    return KeyResult::Changed;
}

KeyResult ViewState::selectAxis(char axis)
{
    auto viewIndex = viewIndexForAxis(axis);
    if (!viewIndex.has_value() || *viewIndex == viewIndex_)
        return KeyResult::Ignored;

    axis_ = axis;
    viewIndex_ = *viewIndex;
    return KeyResult::Changed;
}

KeyResult ViewState::scaleWindow(double factor)
{
    double width = rangeHigh_ - rangeLow_;
    if (width <= 0.0)
        return KeyResult::Ignored;

    double centre = (rangeLow_ + rangeHigh_) / 2.0;
    double half   = width * factor / 2.0;
    rangeLow_  = centre - half;
    rangeHigh_ = centre + half;
    return KeyResult::Changed;
}

KeyResult ViewState::handleKey(char key)
{
    switch (key)
    {
        case 'j': return moveSlice(1);
        case 'k': return moveSlice(-1);
        case 'x':
        case 'y':
        case 'z': return selectAxis(key);
        // '+' widens the window (less contrast), '-' narrows it (more
        // contrast). '=' is accepted for '+' so the user need not hold
        // shift on a US layout.
        case '+':
        case '=': return scaleWindow(kWindowStep);
        case '-': return scaleWindow(1.0 / kWindowStep);
        case 'q': return KeyResult::Quit;
        default:  return KeyResult::Ignored;
    }
}

} // namespace mriv::term
