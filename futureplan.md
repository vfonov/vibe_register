# Font System Upgrade Plan

## Overview

Replace the pixelated default ImGui font with vector-based rendering and add user-specified font configuration support.

## Current State

- **Current font:** `AddFontDefault()` loads ProggyClean bitmap font (pixelated at non-native sizes)
- **Problem:** Bitmap fonts look blurry/pixelated when scaled on HiDPI displays
- **Solution:** Use `AddFontDefaultVector()` for scalable vector font + allow user font customization

## Implementation Phases

### Phase 1: Vector Font Base (Priority: HIGH)

**Goal:** Replace bitmap font with vector-based ProggyForever font

**Changes:**
1. **VulkanBackend.cpp** (`initImGui()`):
   - Replace `io.Fonts->AddFontDefault()` with `io.Fonts->AddFontDefaultVector()`
   - Keep existing glyph range logic (no changes needed)

2. **OpenGL2Backend.cpp** (`initImGui()`):
   - Same change as VulkanBackend

3. **Testing:**
   - Verify font renders correctly at various scale factors (0.5x, 1.0x, 1.5x, 2.0x)
   - Check that all Unicode glyphs still load properly

**Files to modify:**
- `new_register/src/VulkanBackend.cpp`
- `new_register/src/OpenGL2Backend.cpp`

**Estimated effort:** 30 minutes

---

### Phase 2: User Font Configuration (Priority: MEDIUM)

**Goal:** Allow users to specify custom font via config file

**Changes:**
1. **AppConfig.h**:
   ```cpp
   std::string font_path;      // Empty = use ProggyForever
   float font_size = 13.0f;    // Default font size
   ```

2. **AppConfig.cpp**:
   - Add serialization/deserialization for `font_path` and `font_size`
   - Handle missing fields gracefully (default to ProggyForever)

3. **VulkanBackend.cpp & OpenGL2Backend.cpp**:
   - Check if `font_path` is specified
   - If yes: load font via `AddFontFromFileTTF()`
   - If no: use `AddFontDefaultVector()`
   - Add error handling for missing/invalid font files

**Files to modify:**
- `new_register/include/AppConfig.h`
- `new_register/src/AppConfig.cpp`
- `new_register/src/VulkanBackend.cpp`
- `new_register/src/OpenGL2Backend.cpp`

**Estimated effort:** 1 hour

---

### Phase 3: Font Configuration UI Panel (Priority: MEDIUM)

**Goal:** Add UI for font configuration in Tools/Configuration window

**Changes:**
1. **Interface.cpp**:
   - Add "Font Configuration" panel in Tools/Configuration window
   - UI elements:
     - Font path text input with file browser button
     - Font size slider (8-48px)
     - Preview text showing current font rendering
     - "Reset to default" button
   - Add "Font" section alongside existing "Colours" and "Slice" sections

2. **Hotkey panel update**:
   - Document font configuration access (if any shortcuts added)

**Files to modify:**
- `new_register/src/Interface.cpp`

**Estimated effort:** 2 hours

---

### Phase 4: Fallback Font Embedding (Priority: LOW)

**Goal:** Embed ProggyForever.ttf as fallback for when user font fails

**Changes:**
1. **Build system**:
   - Embed ProggyForever.ttf into binary (or ship as resource)
   - Update CMakeLists.txt to include font resource

2. **Backend code**:
   - Load embedded font as fallback when user-specified font fails
   - Add graceful error messages

**Files to modify:**
- `new_register/CMakeLists.txt`
- `new_register/src/VulkanBackend.cpp`
- `new_register/src/OpenGL2Backend.cpp`

**Estimated effort:** 1.5 hours

---

### Phase 5: Documentation & Testing (Priority: LOW)

**Goal:** Document font system and add test coverage

**Changes:**
1. **README.md** or **docs/**:
   - Document `--scale` flag interaction with font rendering
   - Document font configuration options
   - Provide examples of good font choices for medical imaging

2. **Testing:**
   - Manual test: Various scale factors + custom fonts
   - Verify font persists across sessions
   - Test error cases (missing font file, invalid path, etc.)

**Files to modify:**
- `new_register/README.md` or create `new_register/docs/FONTS.md`

**Estimated effort:** 1 hour

---

## Technical Notes

### ImGui Font System (v1.92+)

- **Dynamic glyph loading:** No need to pre-specify glyph ranges
- **Font merging:** Multiple fonts can be merged into single atlas
- **Oversampling:** ImGui handles font rasterization at correct DPI
- **API reference:** https://github.com/ocornut/imgui/blob/master/docs/FONTS.md

### Font Rendering Pipeline

```
User config (font_path, font_size)
    ↓
AppConfig deserialization
    ↓
Backend loads font (AddFontFromFileTTF or AddFontDefaultVector)
    ↓
ImGui builds font atlas with correct scale
    ↓
Font rendered at framebuffer resolution (HiDPI-aware)
```

### Error Handling Strategy

```cpp
if (!font_loaded && !config.font_path.empty())
{
    std::cerr << "Failed to load font: " << config.font_path 
              << ", falling back to ProggyForever\n";
    io.Fonts->AddFontDefaultVector();
}
```

### Known Gotchas

1. **Font size vs scale:** Font size is specified in pixels at 1.0x scale
   - At 2.0x scale, 13px font appears as 26px
   - User may want smaller font_size at higher scales

2. **Font atlas rebuild:** Changing font requires full atlas rebuild
   - Must call `GetTexDataAsRGBA32()` after font changes
   - May cause brief UI flicker during rebuild

3. **Unicode support:** ProggyForever supports full Unicode
   - User fonts may have limited glyph ranges
   - Consider fallback font for missing glyphs

---

## Dependencies

- ImGui 1.92+ (already satisfied)
- ProggyForever.ttf (shipped with ImGui or downloaded separately)
- No external library dependencies

---

## Acceptance Criteria

### Phase 1
- [x] Font renders crisply at all scale factors (ProggyForever vector font)
- [x] All existing tests pass (15/15)
- [x] No visual regression in UI text

### Phase 2
- [x] User can specify font path in config file (`font_path` in global config)
- [x] Font size is configurable (`font_size` in global config, default 13.0)
- [x] Invalid font paths fall back to ProggyForever with stderr warning

### Phase 3
- [x] Font configuration panel exists in Tools panel (collapsible "Font" header)
- [ ] Live preview shows font changes (requires restart — restart note shown in UI)
- [x] Changes persist to config file (saved via Save Config)

### Phase 4
- [ ] Embedded font available as fallback
- [ ] No external font file dependencies required

### Phase 5
- [ ] Documentation updated with font configuration guide
- [ ] Test coverage for font loading edge cases

---

## Timeline Estimate

| Phase | Priority | Effort | Status |
|-------|----------|--------|--------|
| Phase 1: Vector Font Base | HIGH | 30 min | ✅ Done (2026-03-16) |
| Phase 2: User Font Config | MEDIUM | 1 hour | ✅ Done (2026-03-16) |
| Phase 3: Font Config UI | MEDIUM | 2 hours | ✅ Done (2026-03-16) |
| Phase 4: Fallback Font | LOW | 1.5 hours | ⏳ Not started |
| Phase 5: Docs & Testing | LOW | 1 hour | ⏳ Not started |

**Total estimated effort:** ~6 hours

---

## Related Work

- **HiDPI scaling implementation** (COMPLETED): Enables proper font scaling on HiDPI displays
- **Config system** (EXISTING): JSON-based configuration already in place
- **ImGui docking** (EXISTING): UI framework supports custom panels

---

## Notes

- This work was paused on 2026-03-15 to allow user to focus on more urgent task
- All phases should maintain backward compatibility with existing config files
- Font changes should NOT affect existing hotkey functionality
