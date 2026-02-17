# Plan: Overlay Interpolation Support

## Problem Statement

The current overlay rendering in `ViewManager::updateOverlayTexture`:
1. Uses simplified coordinate transformation that ignores direction cosines
2. Uses only nearest-neighbor sampling for all data types

We need to:
1. Fix the coordinate transformation to use proper voxel-to-world matrices (direction cosines)
2. Support three interpolation methods: nearest neighbor, linear, cubic

---

## Architecture (Functional Style)

```cpp
// Interpolation type enum
enum class InterpolationType {
    NearestNeighbor,
    Linear,   // Bilinear for 2D, Trilinear for 3D
    Cubic     // Bicubic for 2D, Tricubic for 3D
};

// Interpolation function signature
using InterpFunction = std::function<float(const Volume&, double x, double y, double z)>;

// Registry of interpolation functions
std::unordered_map<InterpolationType, InterpFunction> interpolationFunctions = {
    {InterpolationType::NearestNeighbor, sampleNearestNeighbor},
    {InterpolationType::Linear, sampleLinear},
    {InterpolationType::Cubic, sampleCubic},
};
```

---

## Phase 1: Fix Direction Cosines Transformation (Priority - Critical)

**Task 1.1**: Modify `updateOverlayTexture` to use proper transformation matrices

**Current code** (ViewManager.cpp:170-191):
```cpp
double wx = ref.start.x + refX * ref.step.x;
double wy = ref.start.y + refY * ref.step.y;
double wz = ref.start.z + refZ * ref.step.z;

double vx = (wx - vol.start.x) / vol.step.x;
double vy = (wy - vol.start.y) / vol.step.y;
double vz = (wz - vol.start.z) / vol.step.z;
```

**Replace with**:
```cpp
glm::dvec3 worldPos;
ref.transformVoxelToWorld(glm::ivec3(refX, refY, refZ), worldPos);

glm::ivec3 targetVoxel;
vol.transformWorldToVoxel(worldPos, targetVoxel);
```

---

## Phase 2: Add Interpolation Type System

**Task 2.1**: Add interpolation type enum to Volume class

In `Volume.h`, add:
```cpp
enum class InterpolationType {
    NearestNeighbor,
    Linear,
    Cubic
};
```

**Task 2.2**: Add interpolation type member and accessors

In `Volume.h`:
```cpp
InterpolationType interpolationType_ = InterpolationType::Linear;
InterpolationType interpolationType() const { return interpolationType_; }
void setInterpolationType(InterpolationType type) { interpolationType_ = type; }
```

**Task 2.3**: Auto-detect interpolation type during volume loading

In `Volume.cpp::load()`, detect based on MINC data type:
- Integer types (MINC2_BYTE, MINC2_SHORT, MINC2_INT, etc.) → NearestNeighbor
- Float types (MINC2_FLOAT, MINC2_DOUBLE) → Linear (default)

---

## Phase 3: Implement Interpolation Functions

**Task 3.1**: Add interpolation functions to Volume class

In `Volume.h`:
```cpp
float sampleNearestNeighbor(double x, double y, double z) const;
float sampleLinear(double x, double y, double z) const;
float sampleCubic(double x, double y, double z) const;

// Generic sampler that delegates to the appropriate method
float sample(double x, double y, double z) const;
```

In `Volume.cpp`:

**Nearest Neighbor** - Returns value at nearest voxel:
```cpp
float Volume::sampleNearestNeighbor(double x, double y, double z) const {
    int ix = static_cast<int>(std::round(x));
    int iy = static_cast<int>(std::round(y));
    int iz = static_cast<int>(std::round(z));
    if (ix < 0 || ix >= dimensions.x || iy < 0 || iy >= dimensions.y || iz < 0 || iz >= dimensions.z)
        return 0.0f;
    return get(ix, iy, iz);
}
```

**Linear (Trilinear)** - Interpolates across 8 corners:
```cpp
float Volume::sampleLinear(double x, double y, double z) const {
    // Clamp to valid range
    x = std::clamp(x, 0.0, static_cast<double>(dimensions.x) - 1.001);
    y = std::clamp(y, 0.0, static_cast<double>(dimensions.y) - 1.001);
    z = std::clamp(z, 0.0, static_cast<double>(dimensions.z) - 1.001);
    
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    int z0 = static_cast<int>(std::floor(z));
    int x1 = std::min(x0 + 1, dimensions.x - 1);
    int y1 = std::min(y0 + 1, dimensions.y - 1);
    int z1 = std::min(z0 + 1, dimensions.z - 1);
    
    double xd = x - x0;
    double yd = y - y0;
    double zd = z - z0;
    
    // 8 corners
    float c000 = get(x0, y0, z0);
    float c100 = get(x1, y0, z0);
    float c010 = get(x0, y1, z0);
    float c110 = get(x1, y1, z0);
    float c001 = get(x0, y0, z1);
    float c101 = get(x1, y0, z1);
    float c011 = get(x0, y1, z1);
    float c111 = get(x1, y1, z1);
    
    // Interpolate
    float c00 = c000 * (1 - xd) + c100 * xd;
    float c01 = c001 * (1 - xd) + c101 * xd;
    float c10 = c010 * (1 - xd) + c110 * xd;
    float c11 = c011 * (1 - xd) + c111 * xd;
    
    float c0 = c00 * (1 - yd) + c10 * yd;
    float c1 = c01 * (1 - yd) + c11 * yd;
    
    return c0 * (1 - zd) + c1 * zd;
}
```

**Cubic (Tricubic)** - Uses 4x4x4 neighborhood with Catmull-Rom or B-spline basis:
```cpp
float Volume::sampleCubic(double x, double y, double z) const {
    // Requires 4x4x4 neighborhood
    // Use Catmull-Rom or B-spline weights
    // Placeholder: fall back to linear for now
    return sampleLinear(x, y, z);
}
```

**Generic sampler**:
```cpp
float Volume::sample(double x, double y, double z) const {
    switch (interpolationType_) {
        case InterpolationType::NearestNeighbor:
            return sampleNearestNeighbor(x, y, z);
        case InterpolationType::Linear:
            return sampleLinear(x, y, z);
        case InterpolationType::Cubic:
            return sampleCubic(x, y, z);
        default:
            return sampleLinear(x, y, z);
    }
}
```

---

## Phase 4: Update Overlay Rendering

**Task 4.1**: Modify `updateOverlayTexture` to use interpolation

Replace the voxel lookup with:
```cpp
float sampleValue = vol.sample(
    static_cast<double>(targetVoxel.x),
    static_cast<double>(targetVoxel.y),
    static_cast<double>(targetVoxel.z)
);
```

---

## Phase 5: UI Integration

**Task 5.1**: Add interpolation type selector to UI

In `Interface.cpp`, add dropdown in volume settings:
- "Nearest Neighbor" for label data
- "Linear" (default) for intensity
- "Cubic" for smooth interpolation

**Task 5.2**: Persist to config

Add interpolation type to `AppConfig` JSON.

---

## Implementation Order

1. **Phase 1** - Fix direction cosines transformation (critical bug fix)
2. **Phase 2** - Add interpolation type system to Volume class
3. **Phase 3** - Implement interpolation functions (NN, Linear, Cubic stub)
4. **Phase 4** - Update overlay rendering to use interpolation
5. **Phase 5** - UI integration and config persistence

---

## Notes

- **Nearest neighbor**: Fast, correct for discrete labels, no interpolation
- **Linear (trilinear)**: Smooths continuous values, standard for intensity
- **Cubic (tricubic)**: Smoother derivatives, requires 4x4x4 neighborhood, placeholder for future
- The `transformWorldToVoxel` already handles rounding and clamping
- No changes needed to main slice rendering (`updateSliceTexture`) - operates on integer voxels directly

---

## Testing

1. Load two volumes with different orientations - verify overlay aligns
2. Load label volume - verify nearest neighbor is used
3. Load intensity volume - verify linear interpolation is smoother
4. Test UI override - verify interpolation changes
5. Test config persistence - verify setting survives restart
