# AGENTS.md

> **Note to Agents:** This file contains instructions for operating within this repository. Please read it carefully before making changes.

## 1. Project Overview & Environment

- **Stack:** C/C++ (Legacy codebase), CMake, Autotools (configure/make)
- **Legacy code:** 
  - `register`: Volume registration tool (application), this is the code we are re-implementing
  - `bicgl`: BIC Graphics Library (internal) - also will be reimplemented in the new version
  - `bicpl`: BIC Programming Library (dependency) - will try not to use any functionality from here
  - `libminc`: Medical Imaging NetCDF (dependency), medial image IO library
- **Structure:**
  - `legacy/bicgl/`: **BIC Graphics Library** source code.
    - `OpenGL_graphics/`: OpenGL implementation.
    - `GLUT_windows/`: GLUT windowing system integration.
    - `Testing/`: Test suite for graphics functions.
  - `legacy/register/`: **Register** application source code.
    - `User_interface/`: UI components and event handling.
    - `Functionality/`: Core logic for slicing, tagging, and volume management.
- **Config Files:**
  - `CMakeLists.txt` (located in subdirectories `legacy/bicgl`, `legacy/register`)
  - `configure.in` / `Makefile.am` (Autotools legacy configs), do not use

## 2. Build, Lint, & Test Commands

### Build
**Use  CMake**
This method is preferred for modern environments.
1. Navigate to the specific project directory (e.g., `cd legacy/register`).
2. Create a separate build directory to keep source clean:
   ```bash
   mkdir build && cd build
   ```
3. Configure the project:
   ```bash
   cmake ..
   ```
   *Note: You may need to specify paths for MINC or BICPL if they are in non-standard locations.*
4. Compile:
   ```bash
   make
   ```

**Option 2: Autotools (Legacy)**
Do not use.

### Linting & Formatting
- **Linting:** No automated linter is currently configured.
  - **Recommended:** Use `cppcheck .` or `clang-tidy` to analyze changes.
  - **Rule:** Fix only errors in the files you touch. Avoid "cleaning up" legacy code unless it fixes a bug.
- **Formatting:** No `.clang-format` exists.
  - **Rule:** strictly follow the **existing coding style** of the file you are editing.
  - **Do NOT** reformat entire files. This obscures the git history.

### Testing
- **Test Runner:** CTest (integrated with CMake).
- **Run All Tests:**
  ```bash
  cd build
  ctest --output-on-failure
  ```
- **Run Specific Tests:**
  ```bash
  ctest -R test_read  # Runs tests matching regex "test_read"
  ```
- **Manual Testing:**
  - Build the test executables (e.g., in `legacy/bicgl/Testing`).
  - Run them directly: `./test_read`

## 3. Code Style & Conventions

### Imports/Includes
- **Order:**
  1. System Headers: `<stdio.h>`, `<stdlib.h>`, `<string.h>`
  2. Library Headers: `<GL/gl.h>`, `<minc/minc.h>`, `<bicpl/globals.h>`
  3. Local Headers: `"graphics.h"`, `"GS_graphics.h"`
- **Syntax:** Use `<>` for external/system libraries and `""` for project-local headers.

### Formatting
- **Indentation:** **4 spaces**. Do not use tabs.
- **Braces:** **Allman style** (braces always on a new line).
  ```c
  // Correct
  if (condition)
  {
      statement();
  }
  
  // Incorrect
  if (condition) {
      statement();
  }
  ```
- **Loops:**
  ```c
  for (i = 0; i < count; ++i)
  {
      // ...
  }
  ```
- **Switch Statements:**
  ```c
  switch (variable)
  {
  case CONSTANT_ONE:
      do_something();
      break;
  default:
      break;
  }
  ```

### Naming Conventions
- **Variables:** `snake_case` (e.g., `volume_filenames`, `tag_filename`).
- **Functions:** `snake_case` (e.g., `initialize_global_colours`, `print_usage`).
  - Public API often uses prefixes: `GS_set_point`, `VIO_Status`.
- **Types/Structs:** `PascalCase` or `Capitalized_Snake_Case` (e.g., `VIO_Point`, `UI_struct`).
- **Macros/Constants:** `UPPER_SNAKE_CASE` (e.g., `N_VOLUMES`, `MAX_RETRY_COUNT`, `DEFAULT_CHUNK_SIZE`).
- **File Names:** `snake_case.c` or `snake_case.h`.

### Error Handling
- **Return Codes:** low level minc Functions often return status codes (e.g., `VIO_Status` which can be `VIO_OK` or `VIO_ERROR`).
  ```c
  if (func() != VIO_OK)
  {
      return VIO_ERROR;
  }
  ```
- **Logging:**
  - Use `print_error(message)` or `fprintf(stderr, ...)` for errors.
  - Do not use `printf` for errors.
- **Memory Management:**
  - Be extremely careful with `malloc` and `free`.
  - Ensure every `malloc` has a corresponding `free` in the appropriate lifecycle method.
  - Use project-specific macros like `FREE()` if available (check `bicpl/globals.h`).

## 4. Workflow Rules for Agents

1. **Analysis First:**
   - Always run `ls -F` and `grep` to locate relevant files.
   - Read the `CMakeLists.txt` to understand dependencies for the file you are editing.
2. **Contextual Awareness:**
   - Read the entire function you are modifying to understand the logic flow.
   - Check `register_UI.globals` or `config.h` if modifying configuration logic.
3. **Atomic & Safe Changes:**
   - do not touch any code in legacy directory, use it only as a reference
   - new code should use C++17 (the CMake standard is set to 17), with modern features available in that standard
4. **Verification:**
   - **Compile:** Always attempt to compile after changes: `make`.
   - **Test:** If modifying logic, write a small test case or run existing tests.
5. **Documentation:**
   - Update Doxygen comments (`/** ... */`) if you change function signatures.
   - Add inline comments (`/* ... */`) for obscure logic.

## 5. Cursor/Copilot Specific Rules
*(No specific rules found in repository)*

- **General:** Be concise and efficient.
- **Tech-Specific:**
  - Respect the legacy C nature of the project.
  - Prefer `snake_case` over `camelCase`.
  - Do not assume modern C++ standard library availability (e.g., `std::vector`) unless you see it being used.

## 6. Modern Rewrite (new_register)

This is a rewrite of the `register` application using C++17, Vulkan, ImGui (Docking), GLFW and nlohmann/json for reading and writing configuration in .json files.

> **See also:** [`research.md`](research.md) for a comprehensive codebase review, architecture diagram, module analysis, test coverage, and risk assessment.

**Dependencies:**
- `minc2-simple` (FetchContent from GitHub, develop branch)
- `ImGui` (fetched via CMake)
- `GLFW` (system)
- `Vulkan` (system)
- `nlohmann/json` (v3.11.3, FetchContent) for handling json files

**Structure:**
- `new_register/src/`: Source files (15 `.cpp` files)
- `new_register/include/`: Headers (15 `.h`/`.hpp` files)
- `new_register/tests/`: Test files (14 test suites)

**Build Instructions:**
- `cd new_register/build`
- `cmake ..`
- `make`

**Current Status:**
- Volume loading implemented and verified (supports MINC2).
- UI implementation (ImGui+Vulkan) functional.
- Multi-volume slice viewer with crosshairs, colour maps, zoom/pan, overlay compositing, and JSON config persistence.

**Coding Standards (New Project):**
- C++17 (CMakeLists.txt sets `CMAKE_CXX_STANDARD 17`).
- `std::vector`, `std::string` allowed.
- `PascalCase` for classes, `camelCase` for methods/variables.
- use `std::cerr` for printing debugging mesages or something more moden

**Hotkey Reference Panel:**
- A "Hotkeys" panel in the UI (`Interface::renderHotkeyPanel()` in `Interface.cpp`) displays all current keyboard shortcuts and mouse interactions.
- **When adding, removing, or changing any hotkey or mouse interaction, update `renderHotkeyPanel()` to keep the panel accurate.**

## 7. C++23 Modernization (new_register)

The codebase currently targets C++17 (`CMAKE_CXX_STANDARD 17`). A future upgrade to C++23 is planned. Phases 1-2 are complete (unique_ptr, std::array). Remaining phases (blocked on C++23 upgrade):

### Phase 3: Range-Based For and C++23 Ranges
- ColourMap.cpp: Replace manual for loops with `std::views::iota` + lambda
- Volume.cpp: Use `std::ranges::minmax_element` for min/max calculation
- Volume.cpp: Use range-based for for dimension loops

### Phase 4: constexpr and consteval
- ColourMap.cpp: Replace custom `countOf` template with `std::size()`
- Consider making `packRGBA` constexpr

### Phase 5: auto Type and Structured Bindings
- AppConfig.cpp: Use structured bindings in merge loop
- TagWrapper.cpp: Use range-based for for point/label copy loops
- main.cpp: Use direct assignment for config serialization

### Phase 6: Replace Magic Numbers
- Volume.cpp: Replace hardcoded `256` with `dimensions.x * dimensions.y * dimensions.z`
- main.cpp: Replace `sizeof(quickMaps) / sizeof(quickMaps[0])` with `std::size(quickMaps)`

### Phase 7: Use std::format
- Replace `printf`/`fprintf`/`std::cout` with `std::print` where applicable

### Build & Test
```bash
cd new_register/build
cmake .. && make
ctest --output-on-failure
```

## 8. QC Viewer (new_qc)

A standalone lightweight QC tool for reviewing medical imaging datasets as JPEG/PNG images.
Separate from `new_register --qc`; purpose-built for fast Pass/Fail batch workflows.

> **See also:** [`research.md`](research.md) Part B for full module analysis, test coverage, and risk assessment.

**Dependencies:**
- `GLFW3` (system)
- `OpenGL 3.3+` (system)
- `ImGui` v1.92.0 (FetchContent, docking branch)
- `stb_image` (FetchContent, header-only)
- `nlohmann_json` v3.11.3 (FetchContent — currently unused)

**Structure:**
- `new_qc/src/main.cpp` — CLI entry point
- `new_qc/src/QCApp.h/.cpp` — GLFW+ImGui+OpenGL GUI, image loading, keyboard input
- `new_qc/src/CSVHandler.h/.cpp` — CSV I/O (load input/output, save progress)
- `new_qc/tests/csv_test.cpp` — 42 unit tests for CSVHandler

**Build Instructions:**
```bash
cd new_qc/build
cmake ..
make
```

**Run:**
```bash
./new_qc input.csv output.csv [--scale <factor>]
```

**CSV Formats:**
- Input:  `id,visit,picture`  (3 columns — id, visit, image path)
- Output: `id,visit,picture,QC,notes`  (5 columns)

**Keyboard Shortcuts:**
- `P` — Mark Pass (auto-advances)
- `F` — Mark Fail (auto-advances)
- `←` / `Page Up` — Previous case
- `→` / `Page Down` — Next case
- `Ctrl+S` — Manual save
- `Esc` — Exit

**Coding Standards:**
- C++17 (CMakeLists.txt sets `CMAKE_CXX_STANDARD 17`)
- Namespace `QC::` for all classes
- `PascalCase` for classes, `camelCase` for methods/variables
- `std::cerr` for error output

**Current Status:**
- Pass/Fail verdict workflow with notes field implemented and functional.
- Auto-save after each QC decision; resume from existing output CSV.
- HiDPI support with auto-detection and `--scale` override.
- 42 CSVHandler unit tests passing.
