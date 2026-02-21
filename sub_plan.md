# Sub-Plan: HIGH Priority Unit Tests

## Overview

Create 4 new test files + extend 1 existing test to cover all HIGH priority test gaps.
Target: go from 12 to 16 test suites.

---

## Steps

### Step 1: Create `test_app_config.cpp` ✅ DONE

**Links:** `AppConfig.cpp`, `nlohmann_json`
**Tests for:** `loadConfig()`, `saveConfig()`, JSON `to_json`/`from_json` round-trips

| Subtest | What it verifies |
|---|---|
| `testMissingFileReturnsDefault` | `loadConfig("/nonexistent")` returns default `AppConfig` (empty volumes, default GlobalConfig) |
| `testSaveAndReloadRoundTrip` | Fully populated `AppConfig` with 2 VolumeConfigs (all fields), GlobalConfig (all fields), save → reload → verify every field |
| `testOptionalFieldsOmitted` | Save config with `valueMin`/`valueMax`/`windowWidth`/`windowHeight` unset (nullopt), reload, verify nullopt |
| `testVolumeConfigMinimalFields` | Save VolumeConfig with only `path` set, reload, verify defaults for colourMap, sliceIndices, zoom, pan |
| `testQCColumnsRoundTrip` | Save config with `qcColumns` map, reload, verify column names and values |
| `testQCColumnsAbsent` | Save config without `qcColumns`, reload, verify `qcColumns` is nullopt |
| `testMalformedJsonThrows` | Write garbage to temp file, verify `loadConfig` throws `std::runtime_error` |
| `testInvalidStructureThrows` | Write valid JSON with wrong types (e.g. `"global": 42`), verify throws |
| `testSaveCreatesParentDir` | Save to nested temp path, verify file is created |

9 subtests, ~260 lines. No MINC/Eigen deps. 13/13 tests pass.

---

### Step 2: Add `colourMapByName` subtests to existing `test_colour_map.cpp` ✅ DONE

**No new file.** Extend existing test.

| Subtest | What it verifies |
|---|---|
| `colourMapByName("Gray")` returns `GrayScale` | Valid name lookup |
| `colourMapByName("Hot Metal")` returns `HotMetal` | Valid name with space |
| `colourMapByName("Spectral")` returns `Spectral` | Another valid name |
| `colourMapByName("NonExistent")` returns `std::nullopt` | Invalid name |
| `colourMapByName("")` returns `std::nullopt` | Empty string |
| Round-trip all types | `colourMapByName(colourMapName(type)) == type` for all 21 types |

3 new test groups (tests 12–14) added: valid lookups, invalid names, full round-trip for all 21 types. 13/13 tests pass.

---

### Step 3: Create `test_tag_wrapper_ops.cpp`

**Links:** `TagWrapper.cpp`, `minc2-simple-static`, `minc2`, `glm`
**Tests for:** `removeTag()`, `updateTag()`, `clear()` including two-volume data

| Subtest | What it verifies |
|---|---|
| `testRemoveTagSingleVolume` | 3 tags, remove index 1, verify 2 remain with correct coords and labels |
| `testRemoveTagTwoVolume` | 3 tags with points2, remove index 1, verify both points_ and points2_ updated |
| `testRemoveFirstTag` | Remove index 0, verify remaining shifted correctly |
| `testRemoveLastTag` | Remove last index, verify no shift needed |
| `testRemoveOutOfRange` | Remove index -1 and index >= count, verify no-op |
| `testUpdateTag` | Update position and label at index 1, verify only that entry changes |
| `testUpdateTagEmptyLabel` | Update with empty label, verify position changes but label preserved |
| `testUpdateOutOfRange` | Update index -1 and index >= count, verify no-op |
| `testClearSingleVolume` | Load single-vol file, `clear()`, verify tagCount==0, hasTags==false, volumeCount==0 |
| `testClearTwoVolume` | Load two-vol file, `clear()`, verify points, points2, labels all empty |

Takes test dir as argv[1]. Uses existing `test_single_vol.tag` / `test_two_vol.tag` fixtures.
~130 lines.

---

### Step 4: Create `test_transform_extra.cpp`

**Links:** `Transform.cpp`, `glm`, `minc2-simple-static`, `minc2`, Eigen
**Tests for:** `inverseTransformPoint()` (linear + TPS), LSQ10

| Subtest | What it verifies |
|---|---|
| `testInverseLinearTranslation` | LSQ6 translation: `inverseTransformPoint(vol1_pt)` ≈ vol2_pt |
| `testInverseLinearRotation` | LSQ6 with 45° rotation: forward + inverse round-trip |
| `testInverseLinearAffine` | LSQ12 arbitrary affine: `transformPoint(inverseTransformPoint(p)) ≈ p` |
| `testInverseTPSIdentity` | TPS with vol1==vol2: inverse returns same point |
| `testInverseTPSDeformation` | TPS with non-linear deformation: `inverseTransformPoint(vol1[i])` ≈ vol2[i] for each tag |
| `testInverseTPSInterpolated` | TPS: inverse at non-tag point, verify round-trip `transformPoint(inverseTransformPoint(p)) ≈ p` |
| `testLSQ10` | Rotation + translation + non-uniform scale + 1 shear: valid result, low RMS, recovered points |
| `testInverseInvalidReturnsInput` | Invalid TransformResult: `inverseTransformPoint(pt)` returns `pt` unchanged |

~200 lines.

---

### Step 5: Create `test_volume_cache.cpp`

**Links:** `AppState.cpp`, `Volume.cpp`, `TagWrapper.cpp`, `AppConfig.cpp`, `ColourMap.cpp`, `Transform.cpp`, `minc2-simple-static`, `glm`, `nlohmann_json`, Eigen
**Tests for:** `VolumeCache::get()`, `put()`, `clear()`, LRU eviction

| Subtest | What it verifies |
|---|---|
| `testEmptyCache` | New cache: `size()` == 0, `get("any")` returns nullptr |
| `testPutAndGet` | `put("path", vol)` then `get("path")` returns non-null with correct dimensions |
| `testCacheMiss` | `put("a", vol)` then `get("b")` returns nullptr |
| `testLRUEviction` | Capacity=2: put "a", "b", "c" → "a" evicted, "b" and "c" remain |
| `testAccessRefreshesLRU` | Capacity=2: put "a", "b", get "a", put "c" → "b" evicted (not "a") |
| `testUpdateExisting` | `put("x", vol1)` then `put("x", vol2)` → `get("x")` returns vol2's data, size still 1 |
| `testClear` | Put 3 items, `clear()`, size == 0, all `get()` return nullptr |

Uses `generate_test_data()` for lightweight volumes (no MINC I/O).
~100 lines.

---

### Step 6: Update `tests/CMakeLists.txt`

`test_app_config` already added in Step 1. Add 3 remaining test targets:

```cmake
# VolumeCache LRU test
add_nr_test(test_volume_cache
    SOURCES   AppState.cpp Volume.cpp TagWrapper.cpp AppConfig.cpp ColourMap.cpp Transform.cpp
    INCLUDES  ${INC_DIR} ${glm_SOURCE_DIR} ${eigen_SOURCE_DIR} ${minc2-simple-static_SOURCE_DIR}/src
    LINKS     minc2-simple-static minc2 glm nlohmann_json::nlohmann_json
)
add_test(NAME VolumeCacheTest COMMAND test_volume_cache)

# TagWrapper operations test (remove, update, clear)
add_nr_test(test_tag_wrapper_ops
    SOURCES  TagWrapper.cpp
    LINKS    minc2-simple-static minc2 glm
)
add_test(NAME TagWrapperOpsTest COMMAND test_tag_wrapper_ops
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Transform extra tests (inverse, LSQ10)
add_nr_test(test_transform_extra
    SOURCES   Transform.cpp
    INCLUDES  ${INC_DIR} ${glm_SOURCE_DIR} ${eigen_SOURCE_DIR} ${minc2-simple-static_SOURCE_DIR}/src
    LINKS     glm minc2-simple-static minc2
)
add_test(NAME TransformExtraTest COMMAND test_transform_extra)
```

Add `test_app_config` and `test_volume_cache` to the GCC < 10 `stdc++fs` linkage block.

---

### Step 7: Build and test

```bash
cd /app/new_register/build
cmake -DENABLE_VULKAN=ON -DENABLE_OPENGL2=ON ..
make -j$(nproc)
ctest --output-on-failure
```

Fix any compilation or test failures.

---

### Step 8: Commit

Single commit with all new/modified test files and CMakeLists.txt changes.

---

## Progress

- Started at **12 test suites**
- After steps 1–2: **13 test suites** (test_app_config added; colour map extended in-place)
- After steps 3–5: **16 test suites** expected
- All HIGH priority gaps from PLAN.md will be covered
- ~570 lines of new test code total
