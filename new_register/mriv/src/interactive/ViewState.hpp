#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "interactive/Keys.hpp"
#include "interactive/RangeEditor.hpp"
#include "render/SliceGeometry.hpp"

#include "ColourMap.h"

namespace mriv::term
{

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

/// Zoom limits and the factor one press of + or - applies. The limits are
/// the parent's (kMinZoom / kMaxZoom in AppState.h, which is not part of
/// nr_core); --zoom is checked against them too.
constexpr double kMinZoom = 0.1;
constexpr double kMaxZoom = 50.0;
constexpr double kZoomStep = 1.25;

/// The interactive session's model: where the cursor is in the volumes and
/// how each one's intensities are mapped. Deliberately free of notcurses,
/// Volume and IO -- this is the part that can be tested without a TTY, so
/// as much behaviour as possible lives here rather than in the render loop
/// (mriv/HANDOFF.md sec 3.9).
///
/// Position is a single 3D voxel cursor, held in the *first* volume's index
/// space and mapped onto the others through world space by
/// mapVoxelThroughWorld(), as new_register does. One cursor is what
/// makes navigation synchronised: j/k moves every column at once, which is
/// the point of showing them side by side. Each axis keeps its own
/// component, so leaving an axis and returning to it lands exactly where it
/// was left -- the three are geometrically independent and there is no
/// meaningful way to carry a position between them.
class ViewState
{
public:
    /// `geometries` has one entry per volume, in column order; `views` is
    /// the list of viewIndex values to display, in row order; `cursor` is a
    /// voxel position in the first volume, clamped on entry to the range
    /// every volume together covers; `displays` has
    /// one entry per volume.
    ///
    /// `axis` is the axis j/k moves. If its view is not among `views` it
    /// falls back to the first displayed view, rather than leaving the
    /// user moving a slice they cannot see.
    ViewState(std::vector<VolumeGeometry> geometries,
              std::vector<int> views,
              char axis,
              const glm::ivec3& cursor,
              std::vector<VolumeDisplay> displays);

    /// Apply one keypress: j/k slice, J/K ten slices, g go-to prompt, m
    /// centre the cursor, x/y/z or up/down axis, Tab, 1-9 and
    /// left/right active volume, o overlay column, + / - zoom, 0 zoom
    /// reset, c/C colour map, r range prompt, s screenshot, i info block,
    /// ? or h key list, q or Esc quit.
    /// There is no
    /// h/l time navigation -- the parent has no time dimension (PLAN.md,
    /// deferred work) -- which is what leaves h free for the key list.
    ///
    /// While the range or go-to prompt is open every key belongs to it, including
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

    int volumeCount() const { return static_cast<int>(geometries_.size()); }
    int activeVolume() const { return activeVolume_; }

    /// The slice `volume` shows for `viewIndex`, and how many it has there.
    int sliceIndexFor(int volume, int viewIndex) const;
    int sliceCountFor(int volume, int viewIndex) const;

    /// The cursor's two in-plane voxel coordinates for `viewIndex`, in
    /// `volume`'s own index space -- what render/Crosshair.hpp needs to mark
    /// where the shared cursor sits within a rendered slice. Volume 0 reads
    /// straight off the cursor; every other volume shows the cursor's world
    /// position on both in-plane axes, the same way sliceIndexFor() does for
    /// the (single) out-of-plane one.
    struct CrosshairVoxel
    {
        int u;
        int v;
    };
    CrosshairVoxel crosshairFor(int volume, int viewIndex) const;

    /// The cursor's position along the active axis, counted from the lowest
    /// slice any volume has there, and the number of positions navigation
    /// covers -- every volume's extent, not only the first's. For a single
    /// volume these are its own slice index and count.
    int sliceIndex() const;
    int sliceCount() const;

    const VolumeDisplay& display(int volume) const;

    /// True while the go-to prompt ('g') is open: a slice number is being
    /// typed for the active axis, 1-based over sliceCount() like the n/N
    /// on the status row. Like the range prompt, it owns every key until
    /// Enter or Esc.
    bool isGoToOpen() const { return goToOpen_; }
    const std::string& goToText() const { return goToText_; }

    /// True when Enter was pressed on something that is not a slice number
    /// from 1 to sliceCount(). Cleared by any edit.
    bool goToHasError() const { return goToError_; }

    /// True while the key list opened by '?' is up. It replaces the whole
    /// screen, and the next key -- any key -- only closes it.
    bool isHelpVisible() const { return helpVisible_; }

    /// True while the info block -- the loaded files and where the cursor
    /// is in each -- is shown above the status row. On from the start;
    /// 'i' hides it, which gives its rows back to the image.
    bool isInfoVisible() const { return infoVisible_; }

    /// True while the overlay column is shown: one more column at the right
    /// of the grid with every volume blended in the first volume's grid, as
    /// new_register's Overlay panel does. Off at start; 'o' toggles it, and
    /// only when there are at least two volumes to blend.
    bool isOverlayVisible() const { return overlayVisible_; }

    /// How much larger than its fitted size every slice is drawn; 1 shows
    /// whole slices. '+' and '-' multiply and divide it by kZoomStep, '0'
    /// puts it back to 1. One factor for every view and column: zoomed
    /// panes stay centred on the shared cursor, so they keep showing the
    /// same place.
    double zoom() const { return zoom_; }

    /// Set the zoom directly (--zoom), clamped to kMinZoom..kMaxZoom.
    void setZoom(double zoom);

    /// The shared cursor in `volume`'s own voxels. Not clamped: outside the
    /// volume (see isCursorInside()) it is the index the cursor would have
    /// if the grid went on.
    glm::ivec3 cursorVoxel(int volume) const;

    /// False when the cursor has left `volume`'s grid, which navigation
    /// over the union of the volumes allows; its column shows background.
    bool isCursorInside(int volume) const;

    /// The cursor in world coordinates, the one position all volumes share.
    glm::dvec3 cursorWorld() const;

private:
    /// The cursor component a view's slice index lives in: viewIndex 1
    /// (sagittal) is X, 2 (coronal) is Y, anything else Z.
    static int cursorAxis(int viewIndex);
    int& cursorComponent(int viewIndex);
    const int& cursorComponent(int viewIndex) const;

    /// The shared cursor in `volume`'s own index space. Not clamped: a
    /// component outside the volume means its column shows background.
    glm::ivec3 cursorIn(int volume) const;

    /// Sets lowBound_/highBound_ from every volume's extent.
    void computeBounds();

    bool isDisplayed(int viewIndex) const;

    KeyResult moveSlice(int delta);
    KeyResult selectAxis(char axis);
    KeyResult selectViewRow(int delta);
    KeyResult selectVolume(int volume);
    KeyResult cycleColourMap(int delta);
    KeyResult centreCursor();
    KeyResult changeZoom(double zoom);
    KeyResult handleGoToKey(char key);
    KeyResult beginRangeEdit();
    KeyResult handleEditKey(char key);

    std::vector<VolumeGeometry> geometries_;
    std::vector<int> views_;
    std::vector<VolumeDisplay> displays_;
    glm::ivec3 cursor_;
    /// The range the cursor can move over, in the first volume's index
    /// space: the union of every volume's extent, so it can run past the
    /// ends of the first one (negative, or beyond its dimensions).
    glm::ivec3 lowBound_{0};
    glm::ivec3 highBound_{0};
    char axis_;
    int viewIndex_;
    int activeVolume_ = 0;
    RangeEditor editor_;
    bool editing_ = false;
    bool goToOpen_ = false;
    bool goToError_ = false;
    std::string goToText_;
    bool helpVisible_ = false;
    bool infoVisible_ = true;
    bool overlayVisible_ = false;
    double zoom_ = 1.0;
};

} // namespace mriv::term
