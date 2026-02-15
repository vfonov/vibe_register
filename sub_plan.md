# Sub-Plan: Migrate C-arrays to GLM vectors in new_register

## Phase 1: Volume.h (Class Members) — COMPLETED ✅

| # | Location | Current | Proposed |
|---|----------|---------|----------|
| 1.1 | `Volume::dimensions` (line 12) | `int dimensions[3]` | `glm::ivec3 dimensions` |
| 1.2 | `Volume::step` (line 16) | `double step[3]` | `glm::dvec3 step` |
| 1.3 | `Volume::start` (line 19) | `double start[3]` | `glm::dvec3 start` |
| 1.4 | `Volume::dirCos` (line 23) | `double dirCos[3][3]` | `glm::dmat3 dirCos` |

**Completed**: All 4 class members migrated to GLM types.

---

## Phase 2: Volume.h / Volume.cpp (Function Signatures) — 3 changes

| # | Location | Current | Proposed |
|---|----------|---------|----------|
| 1.1 | `Volume::dimensions` (line 12) | `int dimensions[3]` | `glm::ivec3 dimensions` |
| 1.2 | `Volume::step` (line 16) | `double step[3]` | `glm::dvec3 step` |
| 1.3 | `Volume::start` (line 19) | `double start[3]` | `glm::dvec3 start` |
| 1.4 | `Volume::dirCos` (line 23) | `double dirCos[3][3]` | `glm::dmat3 dirCos` |

---

## Phase 2: Volume.h / Volume.cpp (Function Signatures) — COMPLETED ✅

| # | Location | Current | Proposed |
|---|----------|---------|----------|
| 2.1 | `Volume::worldExtent()` (line 51) | `void worldExtent(double extent[3])` | `void worldExtent(glm::dvec3& extent)` |
| 2.2 | `Volume::transformVoxelToWorld()` (line 61) | `void transformVoxelToWorld(const int voxel[3], double world[3])` | `void transformVoxelToWorld(const glm::ivec3& voxel, glm::dvec3& world)` |
| 2.3 | `Volume::transformWorldToVoxel()` (line 66) | `void transformWorldToVoxel(const double world[3], int voxel[3])` | `void transformWorldToVoxel(const glm::dvec3& world, glm::ivec3& voxel)` |

**Completed**: All 3 function signatures updated.

---

## Phase 3: Volume.cpp (Local Variables + Implementation) — COMPLETED ✅

| # | Location | Current | Proposed |
|---|----------|---------|----------|
| 3.1 | `load()` function (line 126) | `int dim_indices[3]` | `glm::ivec3 dim_indices` |

**Completed**: Local variable migrated and all array subscript accesses changed to `.x`, `.y`, `.z`.

---

## Phase 4: main.cpp (Struct Members) — COMPLETED ✅

**VolumeViewState struct**:

| # | Location | Current | Proposed |
|---|----------|---------|----------|
| 4.1 | `sliceIndices` | `int sliceIndices[3]` | `glm::ivec3 sliceIndices` |
| 4.2 | `dragAccum` | `float dragAccum[3]` | `glm::dvec3 dragAccum` |
| 4.3 | `zoom` | `float zoom[3]` | `glm::dvec3 zoom` |
| 4.4 | `panU` | `float panU[3]` | `glm::dvec3 panU` |
| 4.5 | `panV` | `float panV[3]` | `glm::dvec3 panV` |
| 4.6 | `valueRange` | `float valueRange[2]` | Kept as `float[2]` for ImGui compatibility |

**OverlayState struct**:

| # | Location | Current | Proposed |
|---|----------|---------|----------|
| 4.7 | `zoom/panU/panV/dragAccum` | `float[3]` | `glm::dvec3` (each) |

**Completed**: All struct members migrated (valueRange kept as float[2] for ImGui compatibility).

---

## Phase 5: main.cpp (Helper Functions) — SKIPPED

Not needed - the helper functions were already using the Volume class methods directly.

---

## Phase 6: main.cpp (Call Sites) — COMPLETED ✅

Updated all transform function calls to use GLM types, fixed std::clamp type mismatches.

---

## Phase 7: Test Files — COMPLETED ✅

| # | File | Status |
|---|------|--------|
| 7.1 | `test_world_to_voxel.cpp` | ✅ Updated |
| 7.2 | `test_coordinate_sync.cpp` | ✅ Updated |
| 7.3 | `test_matrix_debug.cpp` | ✅ Updated |
| 7.4 | `test_volume_info.cpp` | ✅ Already works (uses array subscripts) |
| 7.5 | `test_thick_slices_com.cpp` | ✅ Updated |
| 7.6 | `test_center_of_mass.cpp` | ✅ Already works (direct calculation) |

---

## Phase 8: Config Serialization — COMPLETED ✅ (No changes needed)

`AppConfig.h` already uses `std::array<int, 3>` and `std::array<float, 3>` — kept as-is.

---

## Summary

| Phase | Status |
|-------|--------|
| 1-3 | ✅ COMPLETED |
| 4 | ✅ COMPLETED |
| 5 | ⏭️ SKIPPED |
| 6 | ✅ COMPLETED |
| 7 | ✅ COMPLETED |
| 8 | ✅ COMPLETED |

**All phases completed!** Build succeeds, all 10 tests pass.
