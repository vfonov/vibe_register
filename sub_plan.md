# Sub-Plan: main.cpp Refactoring (Split into Components)

## Goal
Split the monolithic `main.cpp` (~2450 lines) into independent logical components to simplify support and future development. Maintain C++23 standards.

## Current State
- `main.cpp` contains:
  - Global state variables (`g_Volumes`, `g_ViewStates`, `g_Overlay`, etc.)
  - State structs: `VolumeViewState`, `OverlayState`
  - Texture generation logic (`UpdateSliceTexture`, `UpdateOverlayTexture`)
  - UI rendering (`RenderSliceView`, `RenderOverlayView`, Tools panel)
  - Mouse/keyboard interaction handling
  - Main event loop
  - CLI parsing and config loading

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                        main.cpp                              │
│  - Initialization                                            │
│  - Main event loop                                           │
│  - Orchestration                                             │
└─────────────────────────────────────────────────────────────┘
         │                  │                  │
         ▼                  ▼                  ▼
┌─────────────────┐ ┌─────────────────┐ ┌─────────────────┐
│   AppState      │ │  ViewManager    │ │   Interface     │
│  (Data/State)   │ │  (Logic)        │ │  (Presentation) │
│                 │ │                 │ │                 │
│ - volumes       │ │ - Texture gen   │ │ - ImGui windows │
│ - viewStates    │ │ - Slice updates │ │ - Mouse events  │
│ - overlay       │ │ - Cursor sync   │ │ - Rendering     │
│ - global flags  │ │ - View reset    │ │ - Layout        │
└─────────────────┘ └─────────────────┘ └─────────────────┘
```

---

## Phase 1: Create AppState (Data Layer)

### 1.1 New Files
- `new_register/include/AppState.h`
- `new_register/src/AppState.cpp`

### 1.2 Responsibilities
- Store all global application state
- Manage lifecycle of volumes and view states

### 1.3 Structs to Move
| Struct | Description |
|--------|-------------|
| `VolumeViewState` | Per-volume view: slice indices, zoom, pan, colour map, value range |
| `OverlayState` | Overlay panel state: zoom, pan, textures |

### 1.4 Members to Move
| Variable | Type | Description |
|----------|------|-------------|
| `volumes_` | `std::vector<Volume>` | All loaded volumes |
| `volumeNames_` | `std::vector<std::string>` | Display names (file basenames) |
| `volumePaths_` | `std::vector<std::string>` | Full file paths |
| `viewStates_` | `std::vector<VolumeViewState>` | Per-volume view state |
| `overlay_` | `OverlayState` | Overlay panel state |
| `tagsVisible_` | `bool` | Tag visibility toggle |
| `cleanMode_` | `bool` | Hide UI controls |
| `syncCursors_` | `bool` | Synchronize cursor position |
| `lastSyncSource_` | `int` | Last interacted volume index |
| `lastSyncView_` | `int` | Last interacted view index |
| `dpiScale_` | `float` | DPI scaling factor |
| `localConfigPath_` | `std::string` | Local config file path |
| `layoutInitialized_` | `bool` | ImGui layout initialized flag |

### 1.5 Methods to Add
| Method | Description |
|--------|-------------|
| `volumeCount() const` | Return number of loaded volumes |
| `hasOverlay() const` | Return true if multiple volumes loaded |
| `getVolume(int index)` | Get volume reference |
| `getViewState(int index)` | Get view state reference |
| `loadVolume(const std::string& path)` | Load a volume |
| `loadTagsForVolume(int index)` | Load .tag file for volume |
| `applyConfig(const AppConfig& cfg)` | Apply config to volumes |

### 1.6 Constants to Move
| Constant | Value | Description |
|----------|-------|-------------|
| `kClampCurrent` | -2 | Use volume's colour map endpoint |
| `kClampTransparent` | -1 | Transparent |

---

## Phase 2: Create ViewManager (Logic Layer)

### 2.1 New Files
- `new_register/include/ViewManager.h`
- `new_register/src/ViewManager.cpp`

### 2.2 Responsibilities
- Generate and update slice textures
- Handle overlay compositing
- Coordinate cursor synchronization
- Reset views

### 2.3 Methods to Move/Implement
| Method | Description |
|--------|-------------|
| `updateSliceTexture(int volumeIndex, int viewIndex)` | Generate pixel data for slice |
| `updateOverlayTexture(int viewIndex)` | Composite overlay for view |
| `updateAllOverlayTextures()` | Update all 3 overlay views |
| `syncCursors()` | Sync cursor positions across volumes |
| `resetViews()` | Reset all view states to defaults |
| `sliceIndicesToWorld(const Volume&, const int[3], double[3])` | Convert voxel to world |
| `worldToSliceIndices(const Volume&, const double[3], int[3])` | Convert world to voxel |

### 2.4 Dependencies
- `AppState` (reference)
- `VulkanHelpers` (texture creation)
- `ColourMap` (colour lookup)

---

## Phase 3: Create Interface (Presentation Layer)

### 3.1 New Files
- `new_register/include/Interface.h`
- `new_register/src/Interface.cpp`

### 3.2 Responsibilities
- Render all ImGui windows and panels
- Handle mouse/keyboard interaction
- Manage layout

### 3.3 Methods to Move/Implement
| Method | Description |
|--------|-------------|
| `render(GraphicsBackend& backend, GLFWwindow* window)` | Main render entry point |
| `renderToolsPanel()` | Tools panel (left side) |
| `renderVolumeColumn(int volumeIndex)` | Per-volume column window |
| `renderOverlayPanel()` | Overlay panel (when multiple volumes) |
| `renderSliceView(int vi, int viewIndex, const ImVec2& childSize)` | Single slice view |
| `renderOverlayView(int viewIndex, const ImVec2& childSize)` | Overlay slice view |
| `drawTagsOnSlice(...)` | Draw tag points on a slice |
| `resolveClampColour(...)` | Resolve under/over colour |
| `clampColourLabel(int mode)` | Get label for clamp mode |

### 3.4 Input Handling
- Mouse drag (pan, zoom, slice scroll)
- Mouse wheel zoom
- Keyboard shortcuts (R, C, P, Q)
- Slice slider interaction

---

## Phase 4: Refactor main.cpp

### 4.1 Simplified main.cpp
- Initialize `AppState`
- Initialize `GraphicsBackend`
- Parse CLI arguments
- Load configuration
- Load volumes into `AppState`
- Enter main loop:
  - Poll events
  - Begin frame
  - Call `Interface::render()`
  - End frame
- Cleanup

### 4.2 Removed Code (moved to components)
- All global variables
- `VolumeViewState` and `OverlayState` structs
- `UpdateSliceTexture` and related functions
- `RenderSliceView`, `RenderOverlayView`
- UI rendering code
- Input handling code

---

## Phase 5: Testing

### 5.1 Build Verification
```bash
cd new_register/build
cmake .. && make
```

### 5.2 Run Tests
```bash
ctest --output-on-failure
```

### 5.3 Manual Testing
- Load single volume - verify display
- Load multiple volumes - verify overlay
- Test zoom/pan/scroll - verify interaction
- Test keyboard shortcuts
- Test config save/load
- Test clean mode
- Test cursor sync

---

## Summary of File Changes

| File | Action | Description |
|------|--------|-------------|
| `new_register/include/AppState.h` | Create | State container header |
| `new_register/src/AppState.cpp` | Create | State container implementation |
| `new_register/include/ViewManager.h` | Create | View logic header |
| `new_register/src/ViewManager.cpp` | Create | View logic implementation |
| `new_register/include/Interface.h` | Create | UI rendering header |
| `new_register/src/Interface.cpp` | Create | UI rendering implementation |
| `new_register/src/main.cpp` | Refactor | Simplified entry point |
| `new_register/CMakeLists.txt` | Update | Add new source files |

---

## Implementation Order

1. **Phase 1** - Create `AppState` class
2. **Phase 2** - Create `ViewManager` class
3. **Phase 3** - Create `Interface` class
4. **Phase 4** - Refactor `main.cpp`
5. **Phase 5** - Build and test

---

# Appendix: Tag Support Refactoring (Completed)

## Goal
Associate tag collection with Volume class, add methods to load/save/clear tags.

## Current State
- `TagPoint` struct defined in main.cpp (line 79-83)
- `g_TagPoints` is global `std::vector<std::vector<TagPoint>>` (line 86)
- Tag loading code embedded in main.cpp (lines 1485-1509)
- `TagWrapper` class exists but unused in main.cpp
- Tags rendered in `drawTagsOnSlice()` function

---

## Phase 1: Enhance TagWrapper class

### 1.1 TagWrapper.hpp - Add save and setter functionality
| Location | Change |
|----------|--------|
| Line 30 | Add `void save(const std::string& path)` method |
| Add method | `void setPoints(const std::vector<glm::dvec3>& points)` |
| Add method | `void setLabels(const std::vector<std::string>& labels)` |
| Add method | `void clear()` already exists, ensure it clears points_ and labels_ |

### 1.2 TagWrapper.cpp - Implement save
| Location | Change |
|----------|--------|
| New method | Implement `save()` using `minc2_tags_save()` |
| New method | Implement `setPoints()` to populate tags_ structure |
| New method | Implement `setLabels()` to populate labels |

---

## Phase 2: Add tag members to Volume class

### 2.1 Volume.h - Add tag members and methods
| Location | Change |
|----------|--------|
| Add include | `#include "TagWrapper.hpp"` |
| Add member | `TagWrapper tags;` |
| Add method | `void loadTags(const std::string& path)` |
| Add method | `void saveTags(const std::string& path)` |
| Add method | `void clearTags()` |
| Add method | `const std::vector<glm::dvec3>& getTagPoints() const` |
| Add method | `const std::vector<std::string>& getTagLabels() const` |
| Add method | `int getTagCount() const` |
| Add method | `bool hasTags() const` |

### 2.2 Volume.cpp - Implement tag methods
| Location | Change |
|----------|--------|
| loadTags() | Call `tags.load(path)` |
| saveTags() | Call `tags.save(path)` |
| clearTags() | Call `tags.clear()` |
| getTagPoints() | Return `tags.points()` converted to glm::dvec3 |
| getTagLabels() | Return `tags.labels()` |
| getTagCount() | Return `tags.points().size()` |
| hasTags() | Return `!tags.points().empty()` |

---

## Phase 3: Refactor main.cpp to use Volume's tag methods

### 3.1 Remove global tag variables
| Location | Change |
|----------|--------|
| Lines 78-83 | Remove `TagPoint` struct |
| Line 86 | Remove `g_TagPoints` global |
| Line 87 | Keep `g_TagsVisible` (UI toggle) |

### 3.2 Update drawTagsOnSlice function
| Location | Change |
|----------|--------|
| Line 475-476 | Change to check `vol.hasTags()` instead of `g_TagPoints[volumeIndex]` |
| Line 510 | Change to iterate `vol.getTagPoints()` instead of `g_TagPoints[volumeIndex]` |
| Parameter | Remove `volumeIndex` parameter - use volume from context |

### 3.3 Remove tag loading code from main()
| Location | Change |
|----------|--------|
| Lines 1485-1486 | Remove `g_TagPoints.resize()` |
| Lines 1488-1509 | Remove tag loading loop |

### 3.4 Update UI checkbox
| Location | Change |
|----------|--------|
| Line 2410 | Change to check `g_Volumes[vi].hasTags()` |

---

## Phase 4: Testing

### 4.1 Verify existing tests pass
- test_tag_load.cpp should continue to work

### 4.2 Manual testing
- Load volume with .tag file - verify tags display
- Save tags to new file - verify file is created
- Clear tags - verify no tags displayed

---

## Summary of Changes

| File | Changes |
|------|---------|
| TagWrapper.hpp | Add save(), setPoints(), setLabels() |
| TagWrapper.cpp | Implement save and setter methods |
| Volume.h | Add tags member and tag methods |
| Volume.cpp | Implement tag methods |
| main.cpp | Remove global tags, use Volume's tag methods |

---

## Implementation Order

1. **Phase 1** - Enhance TagWrapper (save functionality)
2. **Phase 2** - Add tag members to Volume
3. **Phase 3** - Refactor main.cpp
4. **Phase 4** - Test and verify
