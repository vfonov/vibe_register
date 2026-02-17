# Plan: Overlay Rendering Fixes

## Problem Statement

The overlay rendering in `ViewManager::updateOverlayTexture`:
1. Uses simplified coordinate transformation that ignores direction cosines
2. Uses clamped edge values for out-of-bounds voxels

---

## Completed

### Phase 1: Fix Direction Cosines Transformation

**Status**: COMPLETED

Modified `updateOverlayTexture` (ViewManager.cpp) to use proper voxel-to-world transformation:

```cpp
// Before: simplified calculation ignoring direction cosines
double wx = ref.start.x + refX * ref.step.x;
double wy = ref.start.y + refY * ref.step.y;
double wz = ref.start.z + refZ * ref.step.z;

double vx = (wx - vol.start.x) / vol.step.x;
double vy = (wy - vol.start.y) / vol.step.y;
double vz = (wz - vol.start.z) / vol.step.z;

// After: use Volume's transformation matrices
glm::dvec3 worldPos;
ref.transformVoxelToWorld(glm::ivec3(refX, refY, refZ), worldPos);

glm::ivec3 targetVoxel;
vol.transformWorldToVoxel(worldPos, targetVoxel);
```

The `Volume` class already has `voxelToWorld` and `worldToVoxel` matrices that include direction cosines from the MINC header.

---

### Phase 2: Out-of-Bounds Handling

**Status**: COMPLETED

Changed `Volume::transformWorldToVoxel` to return a boolean indicating if the point is within volume bounds. Overlay rendering now skips out-of-bounds pixels, resulting in transparent background instead of clamped edge values.

---

## Future Work

### Phase 3: Interpolation Methods (Not Yet Implemented)

Add support for different interpolation methods:

1. **Nearest Neighbor** - For discrete label data
2. **Linear (Trilinear)** - For continuous intensity values
3. **Cubic (Tricubic)** - For smoother interpolation (requires 4x4x4 neighborhood)

Architecture: Functional style with enum-based selection.

---

## Notes

- `transformWorldToVoxel` handles rounding and clamping internally
- Main slice rendering (`updateSliceTexture`) operates on integer voxel coordinates - no changes needed
