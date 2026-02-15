# Sub-Plan: Standardize to MINC Spatial Index Order

## Current State

The codebase uses a **custom "app convention"** for `sliceIndices`:
- **App convention**: `[0]=Z, [1]=X, [2]=Y` (i.e., `.z=Z, .x=X, .y=Y`)
- **MINC convention**: `[0]=X, [1]=Y, [2]=Z` (i.e., `.x=X, .y=Y, .z=Z`)

## Goal

Standardize **all** spatial coordinate arrays to use **MINC order** (index 0 = X, index 1 = Y, index 2 = Z).

---

## Phase 1: Change Struct Definition — 1 change

| # | Location | Current | Proposed |
|---|----------|---------|----------|
| 1.1 | `VolumeViewState::sliceIndices` (main.cpp:50) | `glm::ivec3 sliceIndices{0, 0, 0}; // .z=Z, .x=X, .y=Y (app convention)` | `glm::ivec3 sliceIndices{0, 0, 0}; // .x=X, .y=Y, .z=Z (MINC order)` |

---

## Phase 2: Update ResetViews — 1 change

| # | Location | Current | Proposed |
|---|----------|---------|----------|
| 2.1 | ResetViews (main.cpp:609-611) | `sliceIndices[0]=dimZ/2, [1]=dimX/2, [2]=dimY/2` | `sliceIndices.x = dimensions.x/2; sliceIndices.y = dimensions.y/2; sliceIndices.z = dimensions.z/2;` |

---

## Phase 3: Update Crosshair Drawing — 6 changes

| # | Location | Current | Proposed |
|---|----------|---------|----------|
| 3.1 | Transverse crosshair U (main.cpp:707) | `sliceIndices[1]` | `sliceIndices.x` |
| 3.2 | Transverse crosshair V (main.cpp:709) | `sliceIndices[2]` | `sliceIndices.y` |
| 3.3 | Sagittal crosshair U (main.cpp:715) | `sliceIndices[2]` | `sliceIndices.y` |
| 3.4 | Sagittal crosshair V (main.cpp:717) | `sliceIndices[0]` | `sliceIndices.z` |
| 3.5 | Coronal crosshair U (main.cpp:723) | `sliceIndices[1]` | `sliceIndices.x` |
| 3.6 | Coronal crosshair V (main.cpp:725) | `sliceIndices[0]` | `sliceIndices.z` |

---

## Phase 4: Update Mouse Click Handling — 6 changes

| # | Location | Current | Proposed |
|---|----------|---------|----------|
| 4.1 | Transverse click X (main.cpp:787) | `sliceIndices[1] = voxX` | `sliceIndices.x = voxX` |
| 4.2 | Transverse click Y (main.cpp:788) | `sliceIndices[2] = voxY` | `sliceIndices.y = voxY` |
| 4.3 | Sagittal click Y (main.cpp:800) | `sliceIndices[2] = voxY` | `sliceIndices.y = voxY` |
| 4.4 | Sagittal click Z (main.cpp:801) | `sliceIndices[0] = voxZ` | `sliceIndices.z = voxZ` |
| 4.5 | Coronal click X (main.cpp:813) | `sliceIndices[1] = voxX` | `sliceIndices.x = voxX` |
| 4.6 | Coronal click Z (main.cpp:814) | `sliceIndices[0] = voxZ` | `sliceIndices.z = voxZ` |

---

## Phase 5: Update Overlay Crosshair — 6 changes

Same pattern as Phase 3 (lines 1044-1064)

---

## Phase 6: Update Overlay Mouse Click — 6 changes

Same pattern as Phase 4 (lines 1122-1147)

---

## Phase 7: Update Info Display — 1 change

| # | Location | Current | Proposed |
|---|----------|---------|----------|
| 7.1 | Info voxel display (main.cpp:1967-1968) | `sliceIndices[1], sliceIndices[2], sliceIndices[0]` | `sliceIndices.x, sliceIndices.y, sliceIndices.z` |

---

## Phase 8: Remove Sync Conversion Code — 2 changes

| # | Location | Current | Proposed |
|---|----------|---------|----------|
| 8.1 | SyncCursors (main.cpp:558-585) | Conversion code: `voxelMINC(refState.sliceIndices.y, ...)` | Direct: `refState.sliceIndices` |
| 8.2 | Sync checkbox (main.cpp:1783-1801) | Same conversion pattern | Remove conversion |

---

## Phase 9: Update Config Serialization — 2 changes

| # | Location | Current | Proposed |
|---|----------|---------|----------|
| 9.1 | Config save (main.cpp:1757-1759) | `{sliceIndices[0], [1], [2]}` | Direct copy |
| 9.2 | Config load clamp (main.cpp:1593-1598) | `v==0?dim[2] : v==1?dim[0] : dim[1]` | `v==0?dim.z : v==1?dim.x : dim.y` |

---

## Phase 10: Cleanup Comments — 4 changes

| # | Location | Current | Proposed |
|---|----------|---------|----------|
| 10.1 | Struct comment (main.cpp:50) | `.z=Z, .x=X, .y=Y (app convention)` | `.x=X, .y=Y, .z=Z (MINC order)` |
| 10.2 | Crosshair comment (main.cpp:702) | `[0]=Z, [1]=X, [2]=Y (app convention)` | Remove |
| 10.3 | Volume.cpp comment (line 241) | "Callers must convert..." | Remove |
| 10.4 | Volume.cpp comment (line 252) | "Callers must convert..." | Remove |

---

## Summary

| Phase | Changes |
|-------|---------|
| 1 | 1 |
| 2 | 1 |
| 3 | 6 |
| 4 | 6 |
| 5 | 6 |
| 6 | 6 |
| 7 | 1 |
| 8 | 2 |
| 9 | 2 |
| 10 | 4 |

**Total: 35 changes**
