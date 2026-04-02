# problem.md — Known Open Issue: OverlapTest Resampling Bug

## Status

**RESOLVED** — OverlapTest now passes. The coordinate conversion bug in `renderOverlaySlice()` was fixed by rewriting the world-space transform chain in `Volume.cpp` and `SliceRenderer.cpp`. All reference PNGs now match.

---

## What the test does

`new_register/tests/test_overlap.cpp` loads two volumes:

| Volume | Dims | Voxel size | Direction cosines |
|--------|------|------------|-------------------|
| `sq1.mnc` | 100×100×100 | 2 mm isotropic | Identity (axis-aligned) |
| `sq2_tr.mnc` | 50×50×50 | 4 mm isotropic | ~20° rotated |

It calls `renderOverlaySlice()` (in `new_register/src/SliceRenderer.cpp`) to composite both volumes in `sq1`'s voxel grid, then pixel-compares against three reference PNGs:

```
new_register/tests/correct_overlap_ax.png   — axial   midslice
new_register/tests/correct_overlap_sa.png   — sagittal midslice
new_register/tests/correct_overlap_co.png   — coronal  midslice
```

---

## How the reference PNGs were generated (the ground truth)

The reference PNGs are the **gold standard**. They were produced by:

1. Resampling `sq2_tr.mnc` into the same voxel space as `sq1.mnc` using a **proven external tool** (not our code), with **nearest-neighbor interpolation**.
2. Rendering each volume independently with the current **`new_mincpik`** (GrayScale LUT, alpha = 0.5).
3. Compositing the two renders into one image.

This approach is verified correct by an independent tool chain. The resulting PNGs represent what a pixel-perfect nearest-neighbor world-space resampling of `sq2_tr.mnc` into `sq1.mnc`'s grid should look like.

---

## What is broken

`renderOverlaySlice()` performs its own world-space resampling:
for each output pixel in `sq1`'s grid → compute world coordinates → map to `sq2_tr` voxel space → nearest-neighbor sample.

The current implementation produces output that differs from the ground truth by **hundreds of pixels** per slice. The mismatch count was **unchanged** before and after the rendering was rewritten from a scanline-delta approach to a per-pixel world-space transform approach, which suggests the bug is systematic — likely in the coordinate transform chain itself, not the blending or LUT logic.

---

## Where to look

The relevant code is entirely in:

- **`new_register/src/SliceRenderer.cpp`** — function `renderOverlaySlice()`, specifically:
  - The construction of `PerVolInfo::worldToVox` (populated from `vol.worldToVoxel`)
  - The `refV2W` matrix (from `ref.voxelToWorld`)
  - The per-pixel transform: `ref voxel → world → sq2_tr voxel`
- **`new_register/src/Volume.cpp`** — how `voxelToWorld` and `worldToVoxel` are built from MINC2 metadata (step, start, direction cosines)

Candidate causes:

1. **Wrong matrix convention** — `voxelToWorld` or `worldToVoxel` may have incorrect column/row ordering, or may not account for MINC2's start offset correctly.
2. **Origin/start mismatch** — MINC2 `start` encodes the world coordinate of voxel (0,0,0). If the affine is built as `R × diag(step)` without the start translation, all coordinates will be shifted.
3. **Axis ordering** — MINC2 stores data as (Z, Y, X) in memory. If the affine maps (X,Y,Z) but the data is indexed as (Z,Y,X), the wrong voxels are sampled.

---

## How to diagnose

Use the `dump_vol` debug tool (built in the test directory, not a ctest):

```bash
cd new_register/build
./tests/dump_vol ../tests/sq1.mnc ../tests/sq2_tr.mnc
```

This prints dims, step, start, direction cosines, the full `voxelToWorld` 4×4 matrix, and world coordinates of the volume corners. Cross-check the printed world corners against what an external MINC tool reports (e.g., `mincinfo`, `minc_modify_header`, or the resampling tool used to generate the references).

A mismatch in corner world coordinates between `dump_vol` and external tools would pinpoint the affine construction bug in `Volume.cpp`.

---

## Resolution

The bug was in the coordinate transform chain:

1. **`Volume.cpp`** — `voxelToWorld` and `worldToVoxel` matrices were constructed incorrectly from MINC2 metadata. The fix ensures proper handling of step, start, and direction cosines.
2. **`SliceRenderer.cpp`** — `renderOverlaySlice()` used the corrected matrices to compute per-pixel world-space transforms.

The fix was verified by:
- `dump_vol` debug tool confirming corner world coordinates match external MINC tools
- All three reference PNGs (axial, sagittal, coronal) now matching pixel-perfect
- `Overlap2Test` continues to pass (identity direction cosines)

---

## What is NOT broken (historical)

- `Overlap2Test` (sq1 + sq2.mnc, identity dirCos) **passes** — both volumes are axis-aligned, so coordinate mapping errors cancel out or are too small to cause mismatches at nearest-neighbor resolution.
- The LUT, blending, and output pixel format are all correct (verified by Overlap2Test passing and by `generate_overlap_refs` + test round-trip).
- ~~The bug is **specifically in the world-space coordinate mapping** for volumes with non-identity direction cosines and different voxel sizes.~~ (FIXED)
