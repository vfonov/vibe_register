#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "interactive/RangeEditor.hpp"
#include "render/SliceGeometry.hpp"

#include "ColourMap.h"

namespace mriv::term
{

/// What a keypress did, so the caller knows whether a repaint is due.
enum class KeyResult
{
    /// The key is not bound, or it asked for something already true (the
    /// current axis, a slice past either end, a volume already active).
    /// Nothing changed; do not repaint.
    Ignored,
    /// State changed; the caller should render a new frame.
    Changed,
    /// The user asked to leave interactive mode.
    Quit,
    /// The user asked to save the frame already on screen. Deliberately
    /// distinct from Changed: nothing in ViewState changed, so redrawing
    /// would cost a slice render and a bitmap upload for no visible
    /// difference. The caller saves whatever it last drew instead.
    Screenshot,
};

/// The arrow keys, carried through the session as the control codes
/// readline binds to the same movements (Ctrl-P/N/B/F). Screen translates
/// notcurses' synthetic key ids into these, so ViewState, Session and the
/// tests can all stay on plain chars instead of gaining a key type for
/// four bindings. Nothing else in the key map uses the control range, and
/// a user who types Ctrl-B on purpose gets the movement it names anyway.
constexpr char kKeyUp    = '\x10';
constexpr char kKeyDown  = '\x0e';
constexpr char kKeyLeft  = '\x02';
constexpr char kKeyRight = '\x06';

/// A terminal resize, or a request to just redraw. Carried the same way as
/// the arrows: Screen::readKey() maps notcurses' NCKEY_RESIZE to this rather
/// than handling the resize itself, since recomputing the display box and
/// rebuilding the frame needs the render pipeline, which Screen deliberately
/// does not have (mriv/HANDOFF.md sec 3.9). It changes nothing in ViewState
/// -- the picture is unchanged, only the terminal's geometry is -- so
/// handleKey() reports Changed unconditionally and lets the caller redo the
/// layout against whatever ViewState already holds. Bound to Ctrl-L, the
/// traditional Unix "redraw the screen" keystroke, which is deliberate: a
/// user who mashes it when a repaint looks stuck gets exactly the recovery
/// they expect, for free.
constexpr char kKeyResize = '\x0c';

/// How one volume's intensities are mapped. Per volume, not per session:
/// the reason to put two volumes side by side is usually that they need
/// different ranges and different colour maps.
struct VolumeDisplay
{
    double rangeLow = 0.0;
    double rangeHigh = 1.0;
    ColourMapType colourMap = ColourMapType::GrayScale;
    bool invertColourMap = false;
};

/// The interactive session's model: where the cursor is in the volumes and
/// how each one's intensities are mapped. Deliberately free of notcurses,
/// Volume and IO -- this is the part that can be tested without a TTY, so
/// as much behaviour as possible lives here rather than in the render loop
/// (mriv/HANDOFF.md sec 3.9).
///
/// Position is a single 3D voxel cursor, held in the *first* volume's index
/// space and mapped onto the others by mapSliceIndex(). One cursor is what
/// makes navigation synchronised: j/k moves every column at once, which is
/// the point of showing them side by side. Each axis keeps its own
/// component, so leaving an axis and returning to it lands exactly where it
/// was left -- the three are geometrically independent and there is no
/// meaningful way to carry a position between them.
class ViewState
{
public:
    /// `dimensions` has one entry per volume, in column order; `views` is
    /// the list of viewIndex values to display, in row order; `cursor` is a
    /// voxel position in the first volume, clamped on entry; `displays` has
    /// one entry per volume.
    ///
    /// `axis` is the axis j/k moves. If its view is not among `views` it
    /// falls back to the first displayed view, rather than leaving the
    /// user moving a slice they cannot see.
    ViewState(std::vector<glm::ivec3> dimensions,
              std::vector<int> views,
              char axis,
              const glm::ivec3& cursor,
              std::vector<VolumeDisplay> displays);

    /// Apply one keypress: j/k slice, x/y/z or up/down axis, Tab, 1-9 and
    /// left/right active volume, c/C colour map, r range prompt, s
    /// screenshot, q or Esc quit. There is no
    /// h/l -- the parent has no time dimension (PLAN.md, deferred work).
    ///
    /// While the range prompt is open every key belongs to it, including
    /// the navigation letters: typing a number must not also move a slice,
    /// and Esc must close the prompt rather than leave the application.
    KeyResult handleKey(char key);

    /// True while the range prompt is open. The status line renders the
    /// prompt instead of the usual summary when it is.
    bool isEditing() const { return editing_; }
    const RangeEditor& editor() const { return editor_; }

    char axis() const { return axis_; }

    /// The viewIndex of the active axis -- what j/k moves, not the only
    /// thing on screen.
    int viewIndex() const { return viewIndex_; }

    /// The active view's position in views() -- its row in the grid. The
    /// marker drawn beside it is placed by row, and --views can put the
    /// rows in any order, so a viewIndex is not enough.
    int activeViewRow() const;

    /// The displayed views, in row order.
    const std::vector<int>& views() const { return views_; }

    int volumeCount() const { return static_cast<int>(dimensions_.size()); }
    int activeVolume() const { return activeVolume_; }

    /// The slice `volume` shows for `viewIndex`, and how many it has there.
    int sliceIndexFor(int volume, int viewIndex) const;
    int sliceCountFor(int volume, int viewIndex) const;

    /// The cursor's position along the active axis in the first volume --
    /// the index space navigation happens in.
    int sliceIndex() const;
    int sliceCount() const;

    const VolumeDisplay& display(int volume) const;

private:
    int& cursorComponent(int viewIndex);
    const int& cursorComponent(int viewIndex) const;

    bool isDisplayed(int viewIndex) const;

    KeyResult moveSlice(int delta);
    KeyResult selectAxis(char axis);
    KeyResult selectViewRow(int delta);
    KeyResult selectVolume(int volume);
    KeyResult cycleColourMap(int delta);
    KeyResult beginRangeEdit();
    KeyResult handleEditKey(char key);

    std::vector<glm::ivec3> dimensions_;
    std::vector<int> views_;
    std::vector<VolumeDisplay> displays_;
    glm::ivec3 cursor_;
    char axis_;
    int viewIndex_;
    int activeVolume_ = 0;
    RangeEditor editor_;
    bool editing_ = false;
};

} // namespace mriv::term
