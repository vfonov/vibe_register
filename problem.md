# problem.md — Issue Log

Record new issues here, newest last, and link open ones from `AGENTS.md` → *Known open issues*.
The first entry (OverlapTest) is kept as the reference for the `voxelToWorld` / `worldToVoxel`
conventions and the diagnosis workflow.

---

# OverlapTest Resampling Bug (RESOLVED)

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

## What was broken

`renderOverlaySlice()` (`new_register/src/SliceRenderer.cpp`) resamples in world space: for each
output pixel in `sq1`'s grid → world coordinates (`refV2W`, from `ref.voxelToWorld`) → `sq2_tr` voxel
space (`PerVolInfo::worldToVox`, from `vol.worldToVoxel`) → nearest-neighbour sample. Its output
differed from the ground truth by hundreds of pixels per slice. `Overlap2Test` (identity direction
cosines) passed throughout, which isolated the bug to the coordinate mapping for rotated volumes with
different voxel sizes — not the LUT, blending or pixel format.

Candidate causes checked: matrix row/column convention, a missing MINC2 `start` translation, and
(Z,Y,X) storage vs (X,Y,Z) affine axis ordering.

---

## How it was diagnosed

Use the `dump_vol` debug tool (built in the test directory, not a ctest):

```bash
cd new_register/build
./tests/dump_vol ../tests/sq1.mnc ../tests/sq2_tr.mnc
```

This prints dims, step, start, direction cosines, the full `voxelToWorld` 4×4 matrix, and world coordinates of the volume corners. Cross-check the printed world corners against what an external MINC tool reports (e.g., `mincinfo`, or the resampling tool used to generate the references).

A mismatch in corner world coordinates between `dump_vol` and external tools pinpoints an affine construction bug in `Volume.cpp`.

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

# 2026-09-10 — `new_mincpik --label` ignored (RESOLVED)

**Symptom:** `--label` had no effect; label volumes rendered with a continuous colour map.
**Cause:** `Volume` had hand-written copy/move constructors and assignments that did not copy
`isLabelVolume_`, `labelDescriptionFile_` or `labelLUT_`. `mincpik_main.cpp` sets the flag and then
`push_back(std::move(vol))`, losing it. `AppState`'s LRU cache copy (`volumeCache_.put(path, vol)`) lost it too.
**Fix:** removed the special member functions (rule of zero), so every member is copied/moved.
**Test:** `VolumeCoreTest` (`tests/test_volume_core.cpp`).
