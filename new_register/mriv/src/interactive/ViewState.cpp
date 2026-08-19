#include "interactive/ViewState.hpp"

#include <algorithm>

namespace mriv::term
{

namespace
{

int clampIndex(int index, int count)
{
    return std::clamp(index, 0, count > 0 ? count - 1 : 0);
}

} // namespace

ViewState::ViewState(std::vector<glm::ivec3> dimensions,
                     std::vector<int> views,
                     char axis,
                     const glm::ivec3& cursor,
                     std::vector<VolumeDisplay> displays)
    : dimensions_(std::move(dimensions))
    , views_(std::move(views))
    , displays_(std::move(displays))
    , axis_(axis)
    , viewIndex_(viewIndexForAxis(axis).value_or(0))
{
    if (dimensions_.empty())
        dimensions_.push_back(glm::ivec3(1));
    if (views_.empty())
        views_.push_back(0);
    displays_.resize(dimensions_.size());

    // An active axis nobody can see would make j/k look broken, so it falls
    // back to the first displayed view.
    if (!isDisplayed(viewIndex_))
    {
        viewIndex_ = views_.front();
        axis_ = axisForViewIndex(viewIndex_);
    }

    const glm::ivec3& dims = dimensions_.front();
    cursor_ = glm::ivec3(clampIndex(cursor.x, dims.x),
                         clampIndex(cursor.y, dims.y),
                         clampIndex(cursor.z, dims.z));
}

bool ViewState::isDisplayed(int viewIndex) const
{
    return std::find(views_.begin(), views_.end(), viewIndex) != views_.end();
}

int& ViewState::cursorComponent(int viewIndex)
{
    switch (viewIndex)
    {
        case 1:  return cursor_.x;
        case 2:  return cursor_.y;
        default: return cursor_.z;
    }
}

const int& ViewState::cursorComponent(int viewIndex) const
{
    return const_cast<ViewState*>(this)->cursorComponent(viewIndex);
}

int ViewState::sliceCountFor(int volume, int viewIndex) const
{
    if (volume < 0 || volume >= volumeCount())
        return 0;
    return sliceCountForView(viewIndex, dimensions_[static_cast<size_t>(volume)]);
}

int ViewState::sliceIndexFor(int volume, int viewIndex) const
{
    if (volume < 0 || volume >= volumeCount())
        return 0;

    int index = cursorComponent(viewIndex);
    if (volume == 0)
        return index;

    // Every other column tracks the first one's cursor proportionally, so
    // identical volumes stay index-for-index aligned and mismatched ones
    // still move together.
    return mapSliceIndex(index, sliceCountFor(0, viewIndex), sliceCountFor(volume, viewIndex));
}

int ViewState::sliceIndex() const
{
    return cursorComponent(viewIndex_);
}

int ViewState::sliceCount() const
{
    return sliceCountFor(0, viewIndex_);
}

const VolumeDisplay& ViewState::display(int volume) const
{
    int index = std::clamp(volume, 0, volumeCount() - 1);
    return displays_[static_cast<size_t>(index)];
}

KeyResult ViewState::moveSlice(int delta)
{
    int& position = cursorComponent(viewIndex_);
    int wanted = clampIndex(position + delta, sliceCount());
    if (wanted == position)
        return KeyResult::Ignored;

    position = wanted;
    return KeyResult::Changed;
}

KeyResult ViewState::selectAxis(char axis)
{
    auto viewIndex = viewIndexForAxis(axis);
    if (!viewIndex.has_value() || *viewIndex == viewIndex_ || !isDisplayed(*viewIndex))
        return KeyResult::Ignored;

    axis_ = axis;
    viewIndex_ = *viewIndex;
    return KeyResult::Changed;
}

int ViewState::activeViewRow() const
{
    auto found = std::find(views_.begin(), views_.end(), viewIndex_);
    return found == views_.end() ? 0 : static_cast<int>(found - views_.begin());
}

KeyResult ViewState::selectViewRow(int delta)
{
    int rows = static_cast<int>(views_.size());
    if (rows < 2)
        return KeyResult::Ignored;

    // A ring in both directions: stopping dead at the end of a three-row
    // grid is more annoying than coming back round.
    int row = ((activeViewRow() + delta) % rows + rows) % rows;
    viewIndex_ = views_[static_cast<size_t>(row)];
    axis_ = axisForViewIndex(viewIndex_);
    return KeyResult::Changed;
}

KeyResult ViewState::selectVolume(int volume)
{
    if (volume < 0 || volume >= volumeCount() || volume == activeVolume_)
        return KeyResult::Ignored;

    activeVolume_ = volume;
    return KeyResult::Changed;
}

KeyResult ViewState::cycleColourMap(int delta)
{
    int count = colourMapCount();
    auto& display = displays_[static_cast<size_t>(activeVolume_)];
    int current = static_cast<int>(display.colourMap);
    // Wrapping in both directions: the list is a ring, and running off
    // either end is worse than coming back round.
    int next = ((current + delta) % count + count) % count;

    display.colourMap = static_cast<ColourMapType>(next);
    return KeyResult::Changed;
}

KeyResult ViewState::beginRangeEdit()
{
    const VolumeDisplay& display = displays_[static_cast<size_t>(activeVolume_)];
    editor_.begin(display.rangeLow, display.rangeHigh);
    editing_ = true;
    return KeyResult::Changed;
}

KeyResult ViewState::handleEditKey(char key)
{
    EditResult result = editor_.handleKey(key);

    if (result == EditResult::Committed)
    {
        auto value = editor_.value();
        auto& display = displays_[static_cast<size_t>(activeVolume_)];
        display.rangeLow  = value->first;
        display.rangeHigh = value->second;
        editing_ = false;
    }
    else if (result == EditResult::Cancelled)
    {
        editing_ = false;
    }

    // Every keystroke changes the prompt shown on the status row, so every
    // keystroke is a repaint. That costs a frame per character, the same as
    // holding j -- cheap enough not to justify a second, status-only path
    // through the render loop.
    return KeyResult::Changed;
}

KeyResult ViewState::handleKey(char key)
{
    // Ahead of the edit-prompt dispatch: a resize is not text, and must not
    // be typed into an open prompt, close it, or touch anything it holds --
    // it only means the terminal's geometry changed, so the caller redraws
    // exactly what is already here.
    if (key == kKeyResize)
        return KeyResult::Changed;

    if (editing_)
        return handleEditKey(key);

    switch (key)
    {
        case 'j': return moveSlice(1);
        case 'k': return moveSlice(-1);
        case 'x':
        case 'y':
        case 'z': return selectAxis(key);
        case '\t': return selectVolume((activeVolume_ + 1) % std::max(1, volumeCount()));
        // The arrows do what the letters and Tab do, for a user who has not
        // read the legend: vertical picks the view, horizontal the column.
        case kKeyUp:   return selectViewRow(-1);
        case kKeyDown: return selectViewRow(1);
        case kKeyLeft:
        case kKeyRight:
        {
            int count = std::max(1, volumeCount());
            int delta = (key == kKeyRight) ? 1 : count - 1;
            return selectVolume((activeVolume_ + delta) % count);
        }
        case 'c': return cycleColourMap(1);
        case 'C': return cycleColourMap(-1);
        case 'r': return beginRangeEdit();
        case 's': return KeyResult::Screenshot;
        // Esc quits, the same as 'q' -- it is the reflex for getting out of
        // a full-screen terminal application.
        case 'q':
        case '\x1b': return KeyResult::Quit;
        default: break;
    }

    // 1-9 pick a column directly, which beats pressing Tab four times to
    // reach the fifth volume.
    if (key >= '1' && key <= '9')
        return selectVolume(key - '1');

    return KeyResult::Ignored;
}

} // namespace mriv::term
