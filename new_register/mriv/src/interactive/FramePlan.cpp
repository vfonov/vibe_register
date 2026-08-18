#include "interactive/FramePlan.hpp"

namespace mriv::term
{

FrameRequest planFrame(const ViewState& state,
                       const std::vector<const Volume*>& volumes,
                       int boxWidth,
                       int boxHeight,
                       int scale)
{
    FrameRequest request;
    request.views     = state.views();
    request.boxWidth  = boxWidth;
    request.boxHeight = boxHeight;
    request.scale     = scale;

    request.panes.reserve(volumes.size());
    for (size_t i = 0; i < volumes.size(); ++i)
    {
        int volume = static_cast<int>(i);
        const VolumeDisplay& display = state.display(volume);

        FramePane pane;
        pane.volume          = volumes[i];
        pane.valueMin        = display.rangeLow;
        pane.valueMax        = display.rangeHigh;
        pane.colourMap       = display.colourMap;
        pane.invertColourMap = display.invertColourMap;

        // One slice index per displayed view, already mapped out of the
        // first volume's index space by the state.
        pane.sliceIndices.reserve(state.views().size());
        for (int viewIndex : state.views())
            pane.sliceIndices.push_back(state.sliceIndexFor(volume, viewIndex));

        request.panes.push_back(std::move(pane));
    }

    return request;
}

} // namespace mriv::term
