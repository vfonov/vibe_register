#include "interactive/ViewState.hpp"

#include <algorithm>

namespace mriv::term
{

ViewState::ViewState(std::vector<VolumeGeometry> geometries,
                     std::vector<int> views,
                     char axis,
                     const glm::ivec3& cursor,
                     std::vector<VolumeDisplay> displays)
    : geometries_(std::move(geometries))
    , views_(std::move(views))
    , displays_(std::move(displays))
    , axis_(axis)
    , viewIndex_(viewIndexForAxis(axis).value_or(0))
{
    if (geometries_.empty())
        geometries_.push_back(VolumeGeometry{});
    if (views_.empty())
        views_.push_back(0);
    displays_.resize(geometries_.size());

    // An active axis nobody can see would make j/k look broken, so it falls
    // back to the first displayed view.
    if (!isDisplayed(viewIndex_))
    {
        viewIndex_ = views_.front();
        axis_ = axisForViewIndex(viewIndex_);
    }

    computeBounds();
    cursor_ = glm::clamp(cursor, lowBound_, highBound_);
}

void ViewState::computeBounds()
{
    // Every volume's corners, in the first volume's index space: navigation
    // covers all of them, so j/k keeps going while any column still has a
    // slice to show, and the ones that have run out show background.
    const VolumeGeometry& first = geometries_.front();
    lowBound_  = glm::ivec3(0);
    highBound_ = glm::max(first.dimensions - 1, glm::ivec3(0));

    for (size_t volume = 1; volume < geometries_.size(); ++volume)
    {
        const VolumeGeometry& geometry = geometries_[volume];
        glm::ivec3 last = glm::max(geometry.dimensions - 1, glm::ivec3(0));
        for (int corner = 0; corner < 8; ++corner)
        {
            glm::ivec3 voxel((corner & 1) ? last.x : 0,
                             (corner & 2) ? last.y : 0,
                             (corner & 4) ? last.z : 0);
            glm::ivec3 mapped = mapVoxelThroughWorld(voxel, geometry, first);
            lowBound_  = glm::min(lowBound_, mapped);
            highBound_ = glm::max(highBound_, mapped);
        }
    }
}

bool ViewState::isDisplayed(int viewIndex) const
{
    return std::find(views_.begin(), views_.end(), viewIndex) != views_.end();
}

int ViewState::cursorAxis(int viewIndex)
{
    switch (viewIndex)
    {
        case 1:  return 0;
        case 2:  return 1;
        default: return 2;
    }
}

int& ViewState::cursorComponent(int viewIndex)
{
    return cursor_[cursorAxis(viewIndex)];
}

const int& ViewState::cursorComponent(int viewIndex) const
{
    return const_cast<ViewState*>(this)->cursorComponent(viewIndex);
}

int ViewState::sliceCountFor(int volume, int viewIndex) const
{
    if (volume < 0 || volume >= volumeCount())
        return 0;
    return sliceCountForView(viewIndex, geometries_[static_cast<size_t>(volume)].dimensions);
}

glm::ivec3 ViewState::cursorIn(int volume) const
{
    if (volume == 0)
        return cursor_;

    // Every other column shows the world position the first one's cursor
    // is at, the same rule as new_register's ViewManager::syncCursors(): a
    // position off the edge of a smaller volume pins it to the edge slice.
    return mapVoxelThroughWorld(cursor_, geometries_[0],
                                geometries_[static_cast<size_t>(volume)]);
}

int ViewState::sliceIndexFor(int volume, int viewIndex) const
{
    if (volume < 0 || volume >= volumeCount())
        return 0;

    return cursorIn(volume)[cursorAxis(viewIndex)];
}

ViewState::CrosshairVoxel ViewState::crosshairFor(int volume, int viewIndex) const
{
    if (volume < 0 || volume >= volumeCount())
        return {0, 0};

    AspectAxes axes = aspectAxesForView(viewIndex);
    glm::ivec3 voxel = cursorIn(volume);
    return {voxel[axes.u], voxel[axes.v]};
}

int ViewState::sliceIndex() const
{
    return cursorComponent(viewIndex_) - lowBound_[cursorAxis(viewIndex_)];
}

int ViewState::sliceCount() const
{
    int axis = cursorAxis(viewIndex_);
    return highBound_[axis] - lowBound_[axis] + 1;
}

const VolumeDisplay& ViewState::display(int volume) const
{
    int index = std::clamp(volume, 0, volumeCount() - 1);
    return displays_[static_cast<size_t>(index)];
}

KeyResult ViewState::moveSlice(int delta)
{
    int axis = cursorAxis(viewIndex_);
    int& position = cursor_[axis];
    int wanted = std::clamp(position + delta, lowBound_[axis], highBound_[axis]);
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
