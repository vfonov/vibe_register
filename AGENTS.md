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
   - new code should use c++23, with all modern features
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

This is a rewrite of the `register` application using modern C++23, Vulkan, ImGui (Docking), GLFW and Glaze for reading and writing configuration in .json files.

**Dependencies:**
- `minc2-simple` (statically linked, provided in `legacy/minc2-simple`)
- `ImGui` (fetched via CMake)
- `GLFW` (system)
- `Vulkan` (system)
- `Glaze` for handling json files

**Structure:**
- `new_register/src/`: Source files (`main.cpp`, `Volume.cpp`, `VulkanHelpers.cpp`)
- `new_register/include/`: Headers
- `new_register/tests/`: Test files

**Build Instructions:**
- `cd new_register/build`
- `cmake ..`
- `make`

**Current Status:**
- Volume loading implemented and verified (supports MINC2).
- UI implementation (ImGui+Vulkan) functional.
- Multi-volume slice viewer with crosshairs, colour maps, zoom/pan, overlay compositing, and JSON config persistence.

**Coding Standards (New Project):**
- C++23 allowed.
- `std::vector`, `std::string` allowed.
- `PascalCase` for classes, `camelCase` for methods/variables.
- use `std::cerr` for printing debugging mesages or something more moden

## 7. C++23 Modernization (new_register)

The codebase is being modernized from C-style patterns to C++23. Phases 1-2 are complete (unique_ptr, std::array). Remaining phases:

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
