# Sub-Plan: Sync Cursor, Zoom, and Pan

## Goal
Add separate synchronization controls for cursor position, zoom level, and pan offset. These settings should be persisted in configuration files.

---

## Phase 1: Add Sync Flags to AppState

### 1.1 Modify `AppState.h`
- Add `bool syncZoom_ = false;` after `syncCursors_`
- Add `bool syncPan_ = false;` after `syncZoom_`

### 1.2 Build and verify

---

## Phase 2: Update UI Controls

### 2.1 Modify `Interface.cpp` - Tools panel (around line 196)
Replace single "Sync All" checkbox with three checkboxes:
```cpp
ImGui::Checkbox("Sync Cursor", &state_.syncCursors_);
ImGui::Checkbox("Sync Zoom", &state_.syncZoom_);
ImGui::Checkbox("Sync Pan", &state_.syncPan_);
```

### 2.2 Build and verify UI shows three checkboxes

---

## Phase 3: Implement Sync Logic in ViewManager

### 3.1 Add method declarations to `ViewManager.h`
```cpp
void syncZoom(int sourceVolume, int viewIndex);
void syncPan(int sourceVolume, int viewIndex);
```

### 3.2 Implement `syncZoom()` in `ViewManager.cpp`
- Copy `viewStates_[source].zoom[viewIndex]` to all other volumes' same view index
- Update all slice textures

### 3.3 Implement `syncPan()` in `ViewManager.cpp`
- Copy `viewStates_[source].panU[viewIndex]` and `panV[viewIndex]` to all other volumes
- Update all slice textures

### 3.4 Build and verify

---

## Phase 4: Update Interface to Call Sync Methods

### 4.1 Update zoom change handling (Interface.cpp)
- After zoom changes (lines 807-808, 856-864): if `state_.syncZoom_` → call `viewManager_.syncZoom(vi, viewIndex)`

### 4.2 Update pan change handling (Interface.cpp)
- After pan changes (mouse drag with shift+left): if `state_.syncPan_` → call `viewManager_.syncPan(vi, viewIndex)`

### 4.3 Build and verify

---

## Phase 5: Configuration Persistence

### 5.1 Modify `AppConfig.h`
Add to `GlobalConfig`:
```cpp
bool syncCursors = false;
bool syncZoom = false;
bool syncPan = false;
```

### 5.2 Update `AppConfig.cpp` - ensure new fields are serialized
- Glaze should handle bools automatically, but verify serialization works

### 5.3 Update config loading in `AppState.cpp` (or main.cpp)
- Apply sync settings from config to `AppState` on load

### 5.4 Build and verify config save/load works

---

## Implementation Notes

1. **Zoom/Pan are relative**: Unlike cursor position (world coordinates), zoom and pan are UV coordinates (0-1 range), so they can be directly copied between volumes regardless of their different dimensions/orientations.

2. **Overlay sync**: Overlay zoom/pan will automatically follow the reference volume's zoom/pan when sync is enabled.

3. **Per-view sync**: Sync applies to the specific view (transverse/sagittal/coronal) that was modified. Other views remain independent.

4. **Enable behavior**: When enabling sync, immediately sync current values from the active volume to others.
