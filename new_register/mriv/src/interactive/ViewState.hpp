#pragma once

#include <glm/glm.hpp>

#include "render/SliceGeometry.hpp"

namespace mriv::term
{

/// What a keypress did, so the caller knows whether a repaint is due.
enum class KeyResult
{
    /// The key is not bound, or it asked for something already true (the
    /// current axis, a slice past either end). Nothing changed; do not
    /// repaint.
    Ignored,
    /// State changed; the caller should render a new frame.
    Changed,
    /// The user asked to leave interactive mode.
    Quit,
};

/// The interactive session's model: where we are in the volume and how the
/// intensities are mapped. Deliberately free of notcurses, Volume and IO --
/// this is the part of M5 that can be tested without a TTY, so as much of
/// the behaviour as possible lives here rather than in the render loop.
///
/// Position is a 3D voxel cursor rather than one index per view. The three
/// axes are geometrically independent -- there is no meaningful mapping
/// from "60% along Z" onto X -- so each keeps its own position and leaving
/// an axis and returning to it lands exactly where you left off.
class ViewState
{
public:
    /// `sliceIndex` positions the cursor along `axis` only; the other two
    /// axes start at their midpoint, matching the CLI's default --slice.
    /// Out-of-range values are clamped rather than rejected.
    ViewState(const glm::ivec3& dimensions,
              char axis,
              int sliceIndex,
              double rangeLow,
              double rangeHigh);

    /// Apply one keypress: j/k slice, x/y/z axis, +/- window, q quit.
    /// There is no h/l -- the parent has no time dimension (PLAN.md,
    /// deferred work).
    KeyResult handleKey(char key);

    char axis() const { return axis_; }
    int viewIndex() const { return viewIndex_; }

    /// The cursor's position along the current axis, and the number of
    /// slices available there.
    int sliceIndex() const;
    int sliceCount() const;

    double rangeLow() const { return rangeLow_; }
    double rangeHigh() const { return rangeHigh_; }

private:
    /// The cursor component the current axis navigates.
    int& currentSlice();
    const int& currentSlice() const;

    KeyResult moveSlice(int delta);
    KeyResult selectAxis(char axis);
    KeyResult scaleWindow(double factor);

    glm::ivec3 dimensions_;
    glm::ivec3 cursor_;
    char axis_;
    int viewIndex_;
    double rangeLow_;
    double rangeHigh_;
};

} // namespace mriv::term
