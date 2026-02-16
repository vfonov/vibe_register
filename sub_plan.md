# Sub-Plan: Tag Support Refactoring

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
