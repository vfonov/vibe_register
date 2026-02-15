# Sub-Plan: Modernize C-style Code to C++23

## Overview

The codebase has several C-style patterns that can be modernized using C++23 features including:
- `std::unique_ptr` for RAII
- `std::array` for fixed-size arrays
- Range-based for loops and C++23 ranges
- `std::views::iota` and `std::views::enumerate`
- `std::size()` (C++17)
- `std::format` / `std::print` (C++20/23)
- Structured bindings
- `constexpr` and `consteval`

---

## Phase 1: Replace Manual Memory Management (Priority: HIGH)

### 1.1 VulkanHelpers.cpp - Replace new/delete with unique_ptr
| Location | Current | Proposed |
|----------|---------|----------|
| Line 37 | `VulkanTexture* tex = new VulkanTexture()` | Use `auto tex = std::make_unique<VulkanTexture>()` |
| Line 47 | `delete tex` | Remove (unique_ptr auto-destroys) |
| Line 307 | `delete tex` | Remove (unique_ptr auto-destroys) |

### 1.2 Volume.cpp - Use unique_ptr with custom deleter for minc2 handle
| Location | Current | Proposed |
|----------|---------|----------|
| Line 16-47 | `class Minc2Handle` with raw pointer | Use `std::unique_ptr<minc2_file_handle, decltype(&minc2_free)>` with lambda deleter |

---

## Phase 2: Replace C-style Arrays with std::array/GLM (Priority: HIGH)

### 2.1 main.cpp - valueRange
| Location | Current | Proposed |
|----------|---------|----------|
| Line 51 | `float valueRange[2] = { 0.0f, 1.0f }` | `std::array<float, 2> valueRange = { 0.0f, 1.0f }` |

### 2.2 Volume.cpp - dim_indices access
| Location | Current | Proposed |
|----------|---------|----------|
| Line 122-124 | `dim_indices[0/1/2]` | Use `.x/.y/.z` (already GLM ivec3) |
| Line 127 | `dim_indices[0] == -1` | `dim_indices.x == -1` |
| Line 140-142 | `dirCos[axis][0/1/2]` | Use GLM column access: `dirCos[0][axis]`, etc. |

### 2.3 main.cpp - Remove remaining [0]/[1]/[2] array access
| Location | Current | Proposed |
|----------|---------|----------|
| Line 320-322 | `ref.start[0/1/2]` | `ref.start.x/y/z` |
| Line 338-340 | `vol.start[0/1/2]`, `vol.step[0/1/2]` | Use GLM accessors |
| Line 347-349 | `vol.dimensions[0/1/2]` | Use GLM accessors |
| Line 985-986 | `g_Volumes[0]`, `g_ViewStates[0]` | Keep (not array access) |
| Line 1721 | `columnIds[0]` | Use `.front()` or structured binding |

---

## Phase 3: Use Range-Based For and C++23 Ranges (Priority: MEDIUM)

### 3.1 ColourMap.cpp - Lazy initialization loop
| Location | Current | Proposed |
|----------|---------|----------|
| Line 381-389 | Manual for loop with index | Use `std::views::iota` + lambda or `std::array` |

### 3.2 Volume.cpp - Min/max calculation
| Location | Current | Proposed |
|----------|---------|----------|
| Line 202-206 | Manual loop with comparisons | Use `std::ranges::minmax_element` (C++20) |

### 3.3 Volume.cpp - Test data generation nested loops
| Location | Current | Proposed |
|----------|---------|----------|
| Line 73-94 | Three nested for loops | Keep nested loops but use structured bindings |

### 3.4 Volume.cpp - Dimension loop
| Location | Current | Proposed |
|----------|---------|----------|
| Line 131-151 | Manual loop with index | Use `for (int axis = 0; axis < 3; ++axis)` with clear axis names |

### 3.5 Volume.cpp - Total voxels calculation
| Location | Current | Proposed |
|----------|---------|----------|
| Line 184-188 | Manual loop with multiplication | Use `std::accumulate` or manual calculation |

---

## Phase 4: constexpr and consteval (Priority: MEDIUM)

### 4.1 ColourMap.cpp - countOf template
| Location | Current | Proposed |
|----------|---------|----------|
| Line 265-266 | Custom `countOf` template | Use `std::size()` (C++17) or `std::size( array )` |

### 4.2 main.cpp - Clamp constants
| Location | Current | Proposed |
|----------|---------|----------|
| Line 43-44 | `constexpr int kClampCurrent` | Keep as constexpr |

### 4.3 ColourMap.cpp - packRGBA function
| Location | Current | Proposed |
|----------|---------|----------|
| Line 22-36 | `inline uint32_t packRGBA` | Consider making constexpr if inputs allow |

---

## Phase 5: auto Type and Structured Bindings (Priority: MEDIUM)

### 5.1 AppConfig.cpp - Merge loop
| Location | Current | Proposed |
|----------|---------|----------|
| Line 132-146 | Manual nested loop | Use structured bindings: `for (const auto& [localVol, mergedVol] : ...)` |

### 5.2 TagWrapper.cpp - Point/label copy loops
| Location | Current | Proposed |
|----------|---------|----------|
| Line 54-60 | Manual index-based loop | Use range-based for: `for (size_t i = 0; i < count; ++i)` or index_sequence |

### 5.3 main.cpp - Config serialization loops
| Location | Current | Proposed |
|----------|---------|----------|
| Line 1628-1636 | Manual array copy | Use direct assignment: `state.zoom = vc.zoom` |

---

## Phase 6: Replace Magic Numbers (Priority: LOW)

### 6.1 Volume.cpp - Hardcoded 256
| Location | Current | Proposed |
|----------|---------|----------|
| Line 68 | `data.resize(256 * 256 * 256)` | Use `dimensions.x * dimensions.y * dimensions.z` |
| Line 73,91 | Hardcoded `256` | Use `dimensions.x`, etc. |

### 6.2 main.cpp - Array size calculation
| Location | Current | Proposed |
|----------|---------|----------|
| Line 2004 | `sizeof(quickMaps) / sizeof(quickMaps[0])` | Use `std::size(quickMaps)` |

---

## Phase 7: Use std::format (C++20/23) (Priority: LOW)

### 7.1 Replace printf/fprintf with std::print
| Location | Current | Proposed |
|----------|---------|----------|
| Various | `printf`, `fprintf`, `std::cout` | Use `std::print` (C++23) where applicable |

---

## Summary of Changes

| Phase | Focus Area | Files Affected |
|-------|------------|----------------|
| 1 | Memory management | VulkanHelpers.cpp, Volume.cpp |
| 2 | C-style arrays | main.cpp, Volume.cpp |
| 3 | Range-based loops | ColourMap.cpp, Volume.cpp |
| 4 | constexpr/consteval | ColourMap.cpp, main.cpp |
| 5 | auto & bindings | AppConfig.cpp, TagWrapper.cpp, main.cpp |
| 6 | Magic numbers | Volume.cpp, main.cpp |
| 7 | std::format | Various |

---

## Implementation Order

1. **Phase 1** - Critical for resource safety (memory leaks)
2. **Phase 2** - High impact, consistency with existing GLM usage
3. **Phase 3** - Code readability and modern C++ style
4. **Phase 4-7** - Incremental improvements

---

## Notes

- Some C-style patterns are intentional for C library compatibility (minc2-simple API)
- GLM already provides modern vector/matrix types - use them consistently
- C++23 ranges require careful compiler support consideration
- Test thoroughly after each phase
