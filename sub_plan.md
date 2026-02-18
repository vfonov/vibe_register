# sub_plan.md — QC Mode: Detailed Implementation Plan

This document specifies exact code changes, insertion points, and new file contents
for implementing QC (Quality Control) mode in `new_register`.

---

## Phase 1: Core Data Structures and CSV I/O

### 1.1 Create `include/QCState.h`

New file. Defines all QC-specific types and state.

```cpp
#pragma once
#include <string>
#include <vector>
#include <map>
#include <optional>
#include "ColourMap.h"

enum class QCVerdict { UNRATED, PASS, FAIL };

struct QCColumnConfig
{
    std::string colourMap = "GrayScale";
    std::optional<double> valueMin;
    std::optional<double> valueMax;
};

struct QCRowResult
{
    std::string id;
    std::vector<QCVerdict> verdicts;   // parallel with columnNames
    std::vector<std::string> comments; // parallel with columnNames
};

class QCState
{
public:
    bool active = false;

    std::string inputCsvPath;
    std::string outputCsvPath;

    // Parsed from input CSV header (excluding "ID")
    std::vector<std::string> columnNames;

    // Per-row data from input CSV
    std::vector<std::string> rowIds;
    std::vector<std::vector<std::string>> rowPaths; // rowPaths[row][col]

    // Per-row results (parallel with rowIds)
    std::vector<QCRowResult> results;

    // Per-column display config (from JSON config, keyed by column name)
    std::map<std::string, QCColumnConfig> columnConfigs;

    // Runtime
    int currentRowIndex = -1;
    bool showOverlay = true;

    // Methods
    void loadInputCsv(const std::string& path);
    void loadOutputCsv(const std::string& path);
    void saveOutputCsv() const;

    int columnCount() const;
    int rowCount() const;
    int ratedCount() const;           // rows with at least one non-UNRATED verdict
    int firstUnratedRow() const;      // -1 if all rated

    // Get paths for a specific row
    const std::vector<std::string>& pathsForRow(int row) const;
};
```

### 1.2 Create `src/QCState.cpp`

New file. Implements CSV parsing and saving.

**`loadInputCsv(path)`:**
- Open file with `std::ifstream`.
- Read first line as header. Split on `,`. First token must be `"ID"` (case-insensitive).
  Remaining tokens populate `columnNames`.
- For each subsequent line: split on `,`. First token -> `rowIds`. Remaining tokens ->
  `rowPaths[row]`. If fewer tokens than columns, pad with empty strings.
- Initialize `results` vector: one `QCRowResult` per row, with `verdicts` and `comments`
  vectors sized to `columnNames.size()`, all `UNRATED` / empty.
- Handle quoted CSV fields: if a field starts with `"`, read until closing `"` (handling
  `""` escape for embedded quotes).
- Throw `std::runtime_error` on missing ID column or empty file.

**`loadOutputCsv(path)`:**
- If file doesn't exist, return silently (results stay at UNRATED defaults).
- Parse header. Expected columns: `ID`, then pairs `{col}_verdict`, `{col}_comment` for
  each column name.
- Build a map from column name -> (verdict_col_index, comment_col_index) in the output CSV.
- For each data row: match `ID` against `rowIds`. If found, populate the corresponding
  `results[row].verdicts[col]` and `results[row].comments[col]`.
- Verdict string mapping: `"PASS"` -> `QCVerdict::PASS`, `"FAIL"` -> `QCVerdict::FAIL`,
  anything else -> `QCVerdict::UNRATED`.
- Unknown IDs in the output file are silently ignored (they may be from a previous
  version of the input CSV).

**`saveOutputCsv()`:**
- Open `outputCsvPath` with `std::ofstream` (truncate mode).
- Write header: `ID`, then for each column: `{col}_verdict,{col}_comment`.
- Write one row per entry in `results`:
  - ID from `rowIds[i]`.
  - For each column: verdict string (`"PASS"`, `"FAIL"`, or empty), then comment.
  - If comment contains `,`, `"`, or newline, quote it with `"` and escape embedded
    `"` as `""`.
- Flush and close. Use `std::format` for formatting.

**Helper CSV parsing (private):**
- `splitCsvLine(const std::string& line) -> std::vector<std::string>`: split a CSV line
  respecting quoted fields.
- `quoteCsvField(const std::string& field) -> std::string`: quote if needed.

**Accessors:**
- `columnCount()`: return `columnNames.size()`.
- `rowCount()`: return `rowIds.size()`.
- `ratedCount()`: count rows where any verdict != UNRATED.
- `firstUnratedRow()`: return index of first row where all verdicts == UNRATED, or -1.
- `pathsForRow(row)`: return `rowPaths[row]`.

### 1.3 Extend `include/AppConfig.h`

**Move `QCColumnConfig` here** (before `GlobalConfig`, around line 8). This avoids
circular includes since `QCState.h` will include `AppConfig.h` for this struct.

**Add to `GlobalConfig` struct** (after `tagListVisible` at line 30):
```cpp
bool showOverlay = true;
```

**Add to `AppConfig` struct** (after `volumes` at line 37):
```cpp
std::optional<std::map<std::string, QCColumnConfig>> qcColumns;
```

**Add includes:** `#include <map>` (already has `<optional>`).

### 1.4 Extend `src/AppConfig.cpp`

**Add Glaze meta for `QCColumnConfig`** (after `VolumeConfig` meta, before line 29):
```cpp
template <>
struct glz::meta<QCColumnConfig>
{
    using T = QCColumnConfig;
    static constexpr auto value = object(
        "colourMap", &T::colourMap,
        "valueMin",  &T::valueMin,
        "valueMax",  &T::valueMax
    );
};
```

**Update `GlobalConfig` meta** (lines 33-37) to include `showOverlay`:
```cpp
static constexpr auto value = object(
    "default_colour_map", &T::defaultColourMap,
    "window_width",       &T::windowWidth,
    "window_height",      &T::windowHeight,
    "show_overlay",       &T::showOverlay
);
```

**Update `AppConfig` meta** (lines 44-47) to include `qcColumns`:
```cpp
static constexpr auto value = object(
    "global",     &T::global,
    "volumes",    &T::volumes,
    "qc_columns", &T::qcColumns
);
```

### 1.5 Update `CMakeLists.txt`

Sources are auto-globbed (`file(GLOB_RECURSE SOURCES "src/*.cpp" "include/*.h")` at
line 93), so adding `src/QCState.cpp` and `include/QCState.h` requires no change to
the main executable target.

**Add test target** (after line 199):
```cmake
add_executable(test_qc_csv tests/test_qc_csv.cpp src/QCState.cpp src/AppConfig.cpp)
target_include_directories(test_qc_csv PRIVATE include)
target_link_libraries(test_qc_csv PRIVATE glaze::glaze)
add_test(NAME QCCsvTest COMMAND test_qc_csv)
```

Note: `test_qc_csv` only depends on `QCState.cpp`, `AppConfig.cpp`, and standard
library — no Vulkan, no MINC, no ImGui. This keeps the test lightweight.

### 1.6 Create `tests/test_qc_csv.cpp`

Test cases:
1. **Write + read round-trip**: Create a `QCState`, populate with synthetic data,
   call `saveOutputCsv()`, then create a second `QCState` and `loadOutputCsv()`.
   Verify verdicts and comments match.
2. **Parse input CSV**: Write a temporary input CSV with known content, call
   `loadInputCsv()`, verify `columnNames`, `rowIds`, `rowPaths`.
3. **Quoted fields**: Input CSV with commas and quotes in fields. Verify correct parsing.
4. **Missing output file**: `loadOutputCsv()` on non-existent path should not throw.
5. **Partial output**: Output CSV with fewer rows than input. Verify matched rows are
   populated, others remain UNRATED.
6. **ratedCount / firstUnratedRow**: Verify helper functions.

### Phase 1 Checkpoint
- `cmake .. && make` succeeds.
- `ctest -R QCCsv` passes all 6 test cases.
- App still runs normally in non-QC mode.

---

## Phase 2: Volume Lifecycle Management

### 2.1 Fix `VulkanTexture` destructor

Currently `VulkanTexture` has no destructor. When `unique_ptr<VulkanTexture>` is reset,
C++ memory is freed but Vulkan resources (VkImage, VkDeviceMemory, VkImageView,
VkSampler, VkDescriptorSet) are leaked. This must be fixed before we can swap volumes.

**File: `include/VulkanHelpers.h`** — Add to `VulkanTexture` class (after line 22):
```cpp
~VulkanTexture();
```

**File: `src/VulkanHelpers.cpp`** — Add after `cleanup()` at line 321:
```cpp
VulkanTexture::~VulkanTexture()
{
    if (image != VK_NULL_HANDLE || descriptor_set != VK_NULL_HANDLE)
    {
        cleanup(g_Device);
    }
}
```

The guard ensures already-cleaned textures don't double-free, and default-constructed
textures (all `VK_NULL_HANDLE`) are safe. The destructor is defined in the .cpp file
where `g_Device` (module-global, line 9) is accessible.

**Impact on existing code:** The explicit `DestroyTexture()` calls in `ViewManager.cpp`
remain correct — `cleanup()` sets handles to `VK_NULL_HANDLE`, so the destructor is a
no-op afterward. Shutdown code in `main.cpp` (lines 303-314) now properly cleans up.

### 2.2 Add volume unload/reload to `AppState`

**File: `include/AppState.h`** — Add declarations after `applyConfig()` at line 79:
```cpp
void clearAllVolumes();
void loadVolumeSet(const std::vector<std::string>& paths);
```

**File: `src/AppState.cpp`** — Implement:

**`clearAllVolumes()`:**
- Reset all `sliceTextures[3]` in each `viewStates_` entry (destructor handles Vulkan cleanup).
- Reset all `overlay_.textures[3]`.
- Clear `volumes_`, `volumePaths_`, `volumeNames_`, `viewStates_`.
- Reset `selectedTagIndex_ = -1`.

**`loadVolumeSet(paths)`:**
- Call `clearAllVolumes()`.
- For each path: if empty, push a default-constructed `Volume` as placeholder
  (with name `"(missing)"`). If non-empty, try `loadVolume(path)`; on exception,
  push placeholder with name `"(error)"` and log to stderr.
- Call `initializeViewStates()`.
- Does NOT create textures — caller must call `viewManager.initializeAllTextures()`.

### 2.3 Add texture lifecycle methods to `ViewManager`

**File: `include/ViewManager.h`** — Add after `resetViews()` at line 19:
```cpp
void initializeAllTextures();
void destroyAllTextures();
```

**File: `src/ViewManager.cpp`** — Implement:

**`initializeAllTextures()`:** Refactored from `main.cpp` lines 267-276:
- For each volume: skip if `data.empty()` (placeholder), else call
  `updateSliceTexture(vi, 0/1/2)`.
- If `hasOverlay()`: call `updateAllOverlayTextures()`.

**`destroyAllTextures()`:**
- Reset all `sliceTextures` and `overlay_.textures` via `unique_ptr::reset()`.

**Update `main.cpp`:**
- Replace inline texture init (lines 267-276) with `viewManager.initializeAllTextures()`.
- Replace inline shutdown cleanup (lines 303-314) with:
  ```cpp
  backend->waitIdle();
  viewManager.destroyAllTextures();
  ```

### Phase 2 Checkpoint
- Full build succeeds.
- App runs normally, startup and shutdown work.
- Existing tests still pass.

---

## Phase 3: CLI and Mode Activation

### 3.1 Add QC CLI arguments to `main.cpp`

**Add include** (after line 27): `#include "QCState.h"`

**Add variables** (after `pendingLut` at line 42):
```cpp
std::string qcInputPath;
std::string qcOutputPath;
```

**Add parsing** (in arg loop, after `--lut` block at line 101):
```cpp
else if (arg == "--qc" && i + 1 < argc)
{
    qcInputPath = argv[++i];
}
else if (arg == "--qc-output" && i + 1 < argc)
{
    qcOutputPath = argv[++i];
}
```

**Add validation** (after arg loop, around line 123):
```cpp
if (!qcInputPath.empty() && qcOutputPath.empty())
{
    std::cerr << "Error: --qc requires --qc-output\n";
    return 1;
}
```

**Update help text** (lines 69-85), add:
```
  --qc <input.csv>       Enable QC mode with input CSV
  --qc-output <out.csv>  Output CSV for QC verdicts (required with --qc)
```

### 3.2 Integrate QC startup into `main.cpp`

**After config merging** (line 152), add QC state initialization:
```cpp
QCState qcState;
if (!qcInputPath.empty())
{
    qcState.active = true;
    qcState.inputCsvPath = qcInputPath;
    qcState.outputCsvPath = qcOutputPath;
    qcState.loadInputCsv(qcInputPath);
    if (std::filesystem::exists(qcOutputPath))
        qcState.loadOutputCsv(qcOutputPath);
    if (mergedCfg.qcColumns)
        qcState.columnConfigs = *mergedCfg.qcColumns;
    qcState.showOverlay = mergedCfg.global.showOverlay;
}
```

**Wrap existing volume loading** (lines 154-196) in `if (!qcState.active)`:
```cpp
if (qcState.active)
{
    int startRow = qcState.firstUnratedRow();
    if (startRow < 0) startRow = 0;
    qcState.currentRowIndex = startRow;
    // Volumes loaded after backend init (below)
}
else
{
    // ... existing volume loading code unchanged ...
}
```

**After ViewManager/Interface construction** (line 260), change Interface constructor
to accept QCState reference and add QC volume loading:
```cpp
Interface interface(state, viewManager, qcState);

if (qcState.active && qcState.rowCount() > 0)
{
    const auto& paths = qcState.pathsForRow(qcState.currentRowIndex);
    state.loadVolumeSet(paths);
    // Apply per-column configs (colour map, range)
    for (int ci = 0; ci < qcState.columnCount() && ci < state.volumeCount(); ++ci)
    {
        auto it = qcState.columnConfigs.find(qcState.columnNames[ci]);
        if (it != qcState.columnConfigs.end())
        {
            VolumeViewState& vs = state.viewStates_[ci];
            vs.colourMap = colourMapFromName(it->second.colourMap);
            if (it->second.valueMin) vs.valueRange[0] = *it->second.valueMin;
            if (it->second.valueMax) vs.valueRange[1] = *it->second.valueMax;
        }
    }
    viewManager.initializeAllTextures();
}
else if (!state.volumes_.empty())
{
    // Normal mode: existing init code
    state.initializeViewStates();
    state.applyConfig(mergedCfg, initW, initH);
    viewManager.initializeAllTextures();
}
```

### 3.3 Add `colourMapFromName()` helper

**File: `include/ColourMap.h`** — Add declaration:
```cpp
ColourMapType colourMapFromName(const std::string& name);
```

**File: `src/ColourMap.cpp`** — Implement: iterate over all `ColourMapType` values
(0 to `colourMapCount()-1`), compare `colourMapName(type)` with the input string
(case-insensitive). Return matching type or `ColourMapType::GrayScale` as default.

### Phase 3 Checkpoint
- `new_register --qc test.csv --qc-output results.csv` loads the first row's volumes.
- `new_register vol1.mnc vol2.mnc` still works normally.
- Full build + existing tests pass.

---

## Phase 4: QC User Interface

### 4.1 Update `Interface` class

**File: `include/Interface.h`:**
- Add forward declaration: `class QCState;`
- Change constructor: `Interface(AppState&, ViewManager&, QCState&);`
- Add private member: `QCState& qcState_;`
- Add private member: `bool scrollToCurrentRow_ = true;`
- Add private methods:
  ```cpp
  void renderQCListWindow();
  void renderQCVerdictPanel(int volumeIndex);
  void switchQCRow(int newRow);
  ```

**File: `src/Interface.cpp`:**
- Update constructor to accept and store `QCState&`.
- Add `#include "QCState.h"`.

**File: `src/main.cpp`:**
- Update `Interface` construction to pass `qcState`.
- For non-QC mode, `qcState.active` is `false`, so all QC code paths are skipped.

### 4.2 Implement `switchQCRow(int newRow)`

Core volume-swap function. Called by QC list clicks and `[`/`]` keys.

- Bounds check: return if `newRow < 0` or `>= rowCount()` or `== currentRowIndex`.
- Set `qcState_.currentRowIndex = newRow`.
- Call `state_.loadVolumeSet(qcState_.pathsForRow(newRow))`.
- Re-apply per-column configs (colour map, value range) from `qcState_.columnConfigs`.
- Call `viewManager_.initializeAllTextures()`.
- Rebuild `columnNames_` from QC column headers.
- Set `state_.layoutInitialized_ = false` to force layout rebuild.
- Set `scrollToCurrentRow_ = true`.

### 4.3 Implement `renderQCListWindow()`

A docked ImGui window with a scrollable table.

- Window title: `"QC List"` (fixed, for stable DockBuilder ID).
- Show progress in content: `"Rated: 12 / 50"`.
- Table with 3 columns: `#`, `ID`, `Status`.
- Per row:
  - Compute status: `anyFail`, `allPass`, `anyRated` from verdicts.
  - Color row background: red tint if `anyFail`, green tint if `allPass`, default otherwise.
  - `Selectable` spanning all columns. On click: `switchQCRow(ri)`.
  - Status column: colored text `FAIL`/`PASS`/`...`/`---`.
  - Unique ImGui ID per row: `"##qc_{row}"` suffix.
- Auto-scroll to current row on first render (`scrollToCurrentRow_` flag +
  `ImGui::SetScrollHereY()`).

### 4.4 Implement `renderQCVerdictPanel(int volumeIndex)`

Rendered inside each volume column, below the slice views.

- Guard: return if `currentRowIndex` invalid or `volumeIndex >= columnCount()`.
- Get mutable reference to `QCRowResult.verdicts[volumeIndex]` and `.comments[volumeIndex]`.
- `ImGui::PushID(volumeIndex + 5000)` to avoid ID conflicts.
- Three `ImGui::RadioButton` on one line: `PASS`, `FAIL`, `---` (UNRATED).
- `ImGui::InputText` for comment (256 char buffer).
- On any change: call `qcState_.saveOutputCsv()`.
- `ImGui::PopID()`.

### 4.5 Integrate QC into `Interface::render()`

**Keyboard shortcuts** — Inside the `!WantTextInput` guard (after `T` key at line 103):
```cpp
if (qcState_.active)
{
    if (ImGui::IsKeyPressed(ImGuiKey_RightBracket))  // ]
        switchQCRow(qcState_.currentRowIndex + 1);
    if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket))   // [
        switchQCRow(qcState_.currentRowIndex - 1);
}
```

**Suppress tag list** — Modify tag list rendering (line 114):
```cpp
if (!qcState_.active && state_.tagListWindowVisible_ && state_.volumeCount() > 0)
    renderTagListWindow();
```

**Add QC list** — After tag list block:
```cpp
if (qcState_.active)
    renderQCListWindow();
```

**Column names** — Modify `columnNames_` init (lines 29-34): in QC mode, use
`qcState_.columnNames` instead of `state_.volumeNames_`.

**Overlay** — Modify overlay block (line 110):
```cpp
bool showOverlayPanel = hasOverlay;
if (qcState_.active)
    showOverlayPanel = showOverlayPanel && qcState_.showOverlay;
if (showOverlayPanel)
    renderOverlayPanel();
```

### 4.6 Add verdict panel to `renderVolumeColumn()`

**File: `src/Interface.cpp`, `renderVolumeColumn()` (starts at line 317):**

After the controls child block (`if (!state_.cleanMode_) { ... }` ending around line 530),
before `ImGui::End()`:
```cpp
if (qcState_.active)
{
    ImGui::BeginChild("##qc_verdict", ImVec2(viewWidth, 0), ImGuiChildFlags_Borders);
    renderQCVerdictPanel(vi);
    ImGui::EndChild();
}
```

Adjust `viewAreaHeight` (line 327) to reserve space for the verdict panel:
```cpp
const float verdictHeight = qcState_.active ? 60.0f * state_.dpiScale_ : 0.0f;
float viewAreaHeight = avail.y - controlsHeight - verdictHeight;
```

Note: Verdict panel is always visible in QC mode, even in clean mode, so the user
can always rate volumes.

### 4.7 QC mode restrictions in `renderToolsPanel()`

- Wrap "Save Global" / "Save Local" buttons with `if (!qcState_.active)`.
- Wrap tag list checkbox with `if (!qcState_.active)`.
- Add QC progress info at top of tools panel when QC active:
  ```cpp
  if (qcState_.active)
  {
      ImGui::Text("QC Mode");
      ImGui::Text("%d / %d rated", qcState_.ratedCount(), qcState_.rowCount());
      if (qcState_.currentRowIndex >= 0)
          ImGui::Text("ID: %s", qcState_.rowIds[qcState_.currentRowIndex].c_str());
      ImGui::Separator();
  }
  ```

### 4.8 Suppress right-click tag creation in QC mode

**File: `src/Interface.cpp`, `renderSliceView()`** — The right-click handler (around
line 1012):
```cpp
if (!qcState_.active && imageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
```

### 4.9 Layout adjustments for QC mode

**File: `src/Interface.cpp`, `render()`** — In the layout block (lines 41-78):

When `qcState_.active`, add a QC List dock on the far left:
```cpp
if (qcState_.active)
{
    ImGuiID qcListId, remainder;
    ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.12f,
        &qcListId, &remainder);
    ImGui::DockBuilderDockWindow("QC List", qcListId);

    ImGuiID toolsId, contentId;
    ImGui::DockBuilderSplitNode(remainder, ImGuiDir_Left, 0.06f,
        &toolsId, &contentId);
    ImGui::DockBuilderDockWindow("Tools", toolsId);

    // Volume columns + optional overlay split into contentId
    // ... same column splitting logic as existing code ...
}
else
{
    // ... existing layout code unchanged ...
}
```

### Phase 4 Checkpoint
- QC mode: list window shows all rows, color coded.
- Clicking row or pressing `[`/`]` swaps volumes.
- Verdict radio buttons + comment field per column.
- PASS/FAIL changes auto-save to output CSV.
- Tags disabled, save buttons hidden.
- Normal mode: no visible changes.

---

## Phase 5: Polish and Edge Cases

### 5.1 Missing file handling

In `renderVolumeColumn()`, check `vol.data.empty()` before rendering slices:
```cpp
if (vol.data.empty())
{
    ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Volume not loaded");
    if (!state_.volumePaths_[vi].empty())
        ImGui::TextWrapped("File: %s", state_.volumePaths_[vi].c_str());
    if (qcState_.active)
        renderQCVerdictPanel(vi);
    ImGui::End();
    return;
}
```

### 5.2 Auto-scroll to current row

In `Interface`, add `scrollToCurrentRow_` bool member (default `true`).
In `renderQCListWindow()`, after the `Selectable` for the current row:
```cpp
if (qcState_.currentRowIndex == ri && scrollToCurrentRow_)
{
    ImGui::SetScrollHereY();
    scrollToCurrentRow_ = false;
}
```
Reset to `true` in `switchQCRow()`.

### 5.3 Clean mode interaction

In clean mode, verdict panel remains visible (remove `!state_.cleanMode_` guard).
Controls (colour map, range inputs) are hidden as usual. The QC list window and
tools panel follow clean mode visibility rules.

### 5.4 Proper flush on quit

After the main loop in `main.cpp`, before shutdown:
```cpp
if (qcState.active)
    qcState.saveOutputCsv();
```

---

## Implementation Order and Checkpoints

| Step | Phase | Files Changed | Checkpoint |
|---|---|---|---|
| 1 | 1.1-1.2 | `QCState.h` (new), `QCState.cpp` (new) | Compiles standalone |
| 2 | 1.3-1.4 | `AppConfig.h`, `AppConfig.cpp` | Config loads with `qc_columns` |
| 3 | 1.5-1.6 | `CMakeLists.txt`, `test_qc_csv.cpp` (new) | `ctest -R QCCsv` passes |
| 4 | 2.1 | `VulkanHelpers.h`, `VulkanHelpers.cpp` | Full build, existing tests pass |
| 5 | 2.2 | `AppState.h`, `AppState.cpp` | Full build |
| 6 | 2.3 | `ViewManager.h`, `ViewManager.cpp`, `main.cpp` | App runs normally |
| 7 | 3.1-3.3 | `main.cpp`, `ColourMap.h`, `ColourMap.cpp` | `--qc` loads first row |
| 8 | 4.1-4.4 | `Interface.h`, `Interface.cpp` | QC list + verdict panels render |
| 9 | 4.5-4.9 | `Interface.cpp` | Full QC UI functional |
| 10 | 5.1-5.4 | `Interface.cpp`, `main.cpp` | Edge cases handled |

Each step should be a separate commit. The app compiles and runs (in normal mode)
after every step. QC mode becomes functional at step 7 and fully featured at step 9.

---

## File Changes Summary

| File | Action | Description |
|---|---|---|
| `include/QCState.h` | **New** | QC data structures, enums, state class |
| `src/QCState.cpp` | **New** | CSV parsing, saving, row management |
| `tests/test_qc_csv.cpp` | **New** | Unit test for CSV round-trip |
| `include/AppConfig.h` | Edit | Add `QCColumnConfig`, `showOverlay`, `qcColumns` |
| `src/AppConfig.cpp` | Edit | Glaze meta for `QCColumnConfig`, updated serialization |
| `include/VulkanHelpers.h` | Edit | Add `~VulkanTexture()` destructor |
| `src/VulkanHelpers.cpp` | Edit | Implement destructor with `cleanup(g_Device)` |
| `include/AppState.h` | Edit | Add `clearAllVolumes()`, `loadVolumeSet()` |
| `src/AppState.cpp` | Edit | Implement volume lifecycle methods |
| `include/ViewManager.h` | Edit | Add `initializeAllTextures()`, `destroyAllTextures()` |
| `src/ViewManager.cpp` | Edit | Implement texture lifecycle methods |
| `include/Interface.h` | Edit | Add QCState ref, QC methods, `scrollToCurrentRow_` |
| `src/Interface.cpp` | Edit | QC list, verdict panels, mode restrictions, layout |
| `include/ColourMap.h` | Edit | Add `colourMapFromName()` |
| `src/ColourMap.cpp` | Edit | Implement `colourMapFromName()` |
| `src/main.cpp` | Edit | QC CLI args, startup integration, shutdown flush |
| `CMakeLists.txt` | Edit | Add `test_qc_csv` test target |
