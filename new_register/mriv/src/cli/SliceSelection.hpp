#pragma once

#include <optional>
#include <string>

namespace mriv::term
{

/// The three forms `-s/--slice` accepts (PLAN.md CLI surface): an absolute
/// voxel index, a percentage of the slicing axis, or the literal "mid".
enum class SliceSelectionKind
{
    Absolute,
    Percent,
    Mid
};

struct SliceSelection
{
    SliceSelectionKind kind = SliceSelectionKind::Mid;

    /// Meaning depends on kind: the absolute index for Absolute, or the
    /// percentage (0-100, may be out of that range from user input) for
    /// Percent. Unused for Mid.
    double value = 0.0;
};

/// Parse a `--slice` argument of the form "n", "p%", or "mid".
/// Returns std::nullopt if the text does not match any of those forms so
/// the CLI layer can report a clean error.
std::optional<SliceSelection> parseSliceArg(const std::string& arg);

/// Resolve a parsed SliceSelection to a concrete voxel index along an axis
/// of size dimSize, clamped to [0, dimSize - 1]. Behaviour is undefined
/// for dimSize <= 0 (renderSlice() already clamps internally, but the CLI
/// clamps too so --info-style feedback can report what was rendered --
/// see mriv/HANDOFF.md sec 6).
int resolveSliceIndex(const SliceSelection& selection, int dimSize);

/// A full `--slice` argument, in one of two mutually exclusive shapes:
///
///  - the classic bare form ("n", "p%", "mid") -- stored in `active`,
///    which the caller resolves against whatever axis `--axis` names, the
///    same as it always has.
///  - "x=<sel>,y=<sel>,z=<sel>" -- any subset of the three, any order, each
///    `<sel>` in the same "n|p%|mid" grammar -- stored directly in `x`/
///    `y`/`z`, positioning each axis independently of `--axis`.
///
/// Exactly one of `active` or at least one of `x`/`y`/`z` is set; the two
/// forms never mix.
struct SliceSpec
{
    std::optional<SliceSelection> active;
    std::optional<SliceSelection> x;
    std::optional<SliceSelection> y;
    std::optional<SliceSelection> z;
};

/// Parse a `--slice` argument into a SliceSpec. Returns std::nullopt for
/// anything that fails to match either shape above: a malformed selection,
/// an axis letter other than x/y/z, a repeated axis, or a mix of the bare
/// and "axis=" forms in the same argument -- so the CLI layer can report a
/// clean error.
std::optional<SliceSpec> parseSliceSpec(const std::string& arg);

} // namespace mriv::term
