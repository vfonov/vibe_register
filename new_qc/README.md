# new_qc - Quality Control Tool

A streamlined manual quality control tool for medical imaging datasets.

## Features

- **Simple CSV-based workflow**: Input CSV with id, visit, picture columns
- **Image viewer**: Displays JPEG/PNG images scaled to window size
- **Quick navigation**: Scroll through datasets with keyboard or UI buttons
- **QC marking**: Press 'P' for Pass or 'F' for Fail
- **Notes field**: Add custom notes for each dataset
- **Auto-save**: Progress saved automatically after each QC decision
- **Resume support**: Load existing output CSV to continue interrupted work

## Build Instructions

```bash
cd new_qc/build
cmake ..
make
```

## Usage

```bash
./new_qc input.csv output.csv
```

**Note:** Requires a display (X11 or Wayland). For headless servers:
```bash
xvfb-run ./new_qc input.csv output.csv
```

### Input CSV Format

```csv
id,visit,picture
subject001,baseline,/path/to/image1.png
subject002,followup,/path/to/image2.jpg
```

### Output CSV Format

```csv
id,visit,picture,QC,notes
subject001,baseline,/path/to/image1.png,Pass,Good quality
subject002,followup,/path/to/image2.jpg,Fail,Artifact present
```

## Controls

| Key/Button | Action |
|------------|--------|
| **P** | Mark current image as Pass |
| **F** | Mark current image as Fail |
| **Left/Right Arrow** | Navigate to previous/next image |
| **Page Up/Down** | Navigate to previous/next image |
| **Ctrl+S** | Save progress manually |
| **Escape** | Exit application |

## UI Layout

- **Left panel**: Image display (scaled to fit)
- **Right panel**: 
  - Dataset info (id, visit, picture path)
  - QC status buttons (Pass/Fail)
  - Notes text field
  - Progress indicator
  - Navigation buttons

## Dependencies

- GLFW3
- Vulkan (for OpenGL context)
- OpenGL 3.3+
- ImGui (fetched automatically)
- stb_image (fetched automatically)
- nlohmann/json (fetched automatically)

## Architecture

```
new_qc/
├── CMakeLists.txt      # Build configuration
├── src/
│   ├── main.cpp       # Entry point, CLI parsing
│   ├── QCApp.cpp      # Main application logic
│   ├── QCApp.h        # Application header
│   ├── CSVHandler.cpp # CSV read/write
│   └── CSVHandler.h   # CSV handler header
├── test_images/       # Sample test images
├── test_input.csv     # Example input CSV
└── build/             # Build directory
```

## Comparison with new_register --qc Mode

| Feature | new_qc | new_register --qc |
|---------|--------|-------------------|
| Purpose | Dedicated QC tool | Part of registration tool |
| UI Complexity | Simple, streamlined | Complex with volume viewer |
| Image Types | JPEG/PNG | MINC2 volumes |
| Configuration | CSV-based | JSON config + CSV |
| Dependencies | Minimal (GLFW, ImGui) | Heavy (MINC, Vulkan, BICGL) |
| Best For | Quick QC of image datasets | Detailed volume QC |

## Example Workflow

1. Prepare input CSV:
   ```bash
   echo "id,visit,picture" > subjects.csv
   echo "subj001,baseline,/data/subj001_t1.png" >> subjects.csv
   echo "subj002,baseline,/data/subj002_t1.png" >> subjects.csv
   ```

2. Run QC tool:
   ```bash
   ./new_qc subjects.csv qc_results.csv
   ```

3. Review results:
   ```bash
   cat qc_results.csv
   ```

## Notes

- Images are loaded on-demand (not pre-loaded)
- Output CSV is created/updated automatically
- Existing output files are loaded to resume work
- Tool auto-saves after each QC decision

## Troubleshooting

### "Failed to initialize GLFW"

This error occurs when running in a headless environment (no display). Solutions:

1. **Use xvfb-run** (recommended for servers):
   ```bash
   xvfb-run ./new_qc input.csv output.csv
   ```

2. **Set up a virtual display**:
   ```bash
   Xvfb :99 -screen 0 1280x720x24 &
   export DISPLAY=:99
   ./new_qc input.csv output.csv
   ```

### CSV Loading Issues

- Ensure input CSV has exactly 3 columns: `id`, `visit`, `picture`
- Picture paths must be absolute or relative to current directory
- Supported formats: JPEG (.jpg, .jpeg) and PNG (.png)
