# new_mincpik — Performance Review

Identified bottlenecks in the CPU-only headless mosaic pipeline, ranked by
impact. No changes have been made yet; this document is a reference for
future optimization work.

---

## Tier 1 — High Impact

### 1. Sagittal view: severe cache thrashing
**File:** `new_register/src/SliceRenderer.cpp` ~line 231–245

The sagittal inner loop accesses `vdata[zOff + y * dimX]` — stride = `dimX`
floats per pixel (2 KB per step on a 512-wide volume). Every pixel fetch is a
cache miss at L1. Axial and coronal views traverse memory sequentially and are
not affected.

**Proposed fix:** Gather the column into a contiguous temp buffer in one
sequential pass, then colour-map the buffer:
```cpp
std::vector<float> col_buf(w * h);
for (int z = 0; z < h; ++z)
    for (int y = 0; y < w; ++y)
        col_buf[z * w + y] = vdata[z * dimY * dimX + xOff + y * dimX];
// colour-map col_buf sequentially — now cache-friendly
```

---

### 2. Overlay rendering: per-pixel double-precision matrix multiply
**File:** `new_register/src/SliceRenderer.cpp` ~line 560–703

`renderOverlaySlice` computes a `glm::dvec3` coordinate transform per pixel
per volume:
```cpp
glm::dvec3 worldPt = sc.base + px * sc.dpx + py * sc.dpy;
```
~36 double FLOPs per pixel per overlay volume. For 512×512 with 2 volumes
≈ 19 M FLOPs just for coordinate transforms.

**Proposed fix:** Downcast pre-computed `dpx`/`dpy`/`base` increments to
`glm::vec3` (single precision) — sub-voxel accuracy is unnecessary for display.
The TPS inverse transform path cannot be simplified this way.

---

### 3. Per-pixel `std::log10()` call
**File:** `new_register/src/SliceRenderer.cpp` ~line 156, ~609

When `useLogTransform` is set, every pixel calls `std::log10(v)` (~20–50
cycles). For a 512×512 slice = 5–13 M cycles per slice.

**Proposed fix:** Pre-compute a 256-entry log LUT spanning `[rangeMin, rangeMax]`
at render setup time; replace the per-pixel call with a table lookup. The output
is already quantised to 8 bits so 256 entries are sufficient.

---

## Tier 2 — Medium Impact

### 4. `resampleToPhysicalAspect`: per-pixel boundary checks
**File:** `new_register/src/mincpik/mosaic.cpp` ~line 202–232

```cpp
int srcX = static_cast<int>(x * scaleX);
if (srcX >= slice.width) srcX = slice.width - 1;  // executed per pixel
```
For a 2 K-wide output this is 2 M branches. Both bounds checks can be
eliminated by clamping `outW`/`outH` so that the guard condition is provably
false inside the loop, or by pre-building `srcXmap[outW]` / `srcYmap[outH]`
index arrays once before the loop.

---

### 5. Allocation chain: 3 heap allocations per slice
**File:** `new_register/src/mincpik/mincpik_main.cpp` ~line 268–295

Each slice triggers three separate `std::vector` allocations:
1. `renderSlice` / `renderOverlaySlice` → `pixels.resize(w*h)`
2. `applyCrop` → `pixels.assign(outW*outH, ...)`
3. `resampleToPhysicalAspect` → `pixels.resize(outW*outH)`

With 50 slices across three planes = 150 allocations and 3× temporary buffer
traffic per slice.

**Proposed fix:** Pass a reusable scratch `RenderedSlice` into the render
functions and resize in place; or confirm NRVO applies to all return paths
(currently returns by value).

---

### 6. `getUniqueLabelIds()` scans full volume per render
**File:** `new_register/src/SliceRenderer.cpp` ~line 136 / `new_register/src/Volume.cpp` ~line 464

For label volumes, `vol.getUniqueLabelIds()` iterates all voxels (O(N)) on
every `renderSlice` call to build a label→colour-index map.

**Proposed fix:** Cache the unique label list in `Volume` (populate on first
call, invalidate via a `labelCacheDirty` flag). The `Volume` class already
knows when data changes.

---

## Tier 3 — Low Impact

### 7. `computeQuantile()` copies full volume data
**File:** `new_register/src/Volume.cpp` ~line 306–314

```cpp
std::vector<float> tmp = data;  // copies entire volume
std::nth_element(tmp.begin(), tmp.begin() + idx, tmp.end());
```
Called once per volume at startup; not a steady-state bottleneck, but doubles
memory pressure briefly for large volumes (512³ ≈ 128 MB).

---

### 8. Mosaic final scale: division instead of reciprocal multiply
**File:** `new_register/src/mincpik/mincpik_main.cpp` ~line 573–596

```cpp
int srcX = static_cast<int>(x / scale);  // division per pixel
```
Pre-compute `double invScale = 1.0 / scale` and use `x * invScale`.

---

### 9. LUT inversion done per render
**File:** `new_register/src/SliceRenderer.cpp` ~line 31, 395–397

When `invertColourMap` is set, a 256-entry vector is copied and reversed on
every render call. Cache the inverted copy in `ColourMap`.

---

### 10. `applyCrop`: allocates blank tile before checking slice range
**File:** `new_register/src/mincpik/mosaic.cpp` ~line 157–161

The function allocates and zero-fills `outW × outH` pixels, then immediately
returns the blank buffer when the slice index is outside the crop range. Move
the range check before the allocation.

---

## Summary Table

| # | Bottleneck | File | Severity |
|---|-----------|------|----------|
| 1 | Sagittal cache thrashing (stride = dimX) | `SliceRenderer.cpp:231` | **High** |
| 2 | Per-pixel double-precision matrix multiply | `SliceRenderer.cpp:560` | **High** |
| 3 | Per-pixel `std::log10()` | `SliceRenderer.cpp:156` | **High** |
| 4 | `resampleToPhysicalAspect` per-pixel bounds checks | `mosaic.cpp:202` | Medium |
| 5 | 3 heap allocations per slice | `mincpik_main.cpp:268` | Medium |
| 6 | `getUniqueLabelIds()` full-volume scan per render | `Volume.cpp:464` | Medium |
| 7 | `computeQuantile()` full-volume copy | `Volume.cpp:306` | Low |
| 8 | Final scale: division instead of multiply | `mincpik_main.cpp:573` | Low |
| 9 | LUT inversion per render | `SliceRenderer.cpp:31` | Low |
| 10 | `applyCrop` allocates before range check | `mosaic.cpp:157` | Low |
