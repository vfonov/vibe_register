# Transform Computation: Replace Nelder-Mead with Levenberg-Marquardt

## Summary

Replace the broken Nelder-Mead optimizer with Eigen's Levenberg-Marquardt for unified transform fitting across LSQ6/7/9/10. This fixes the LSQ9/LSQ10 convergence bug and provides a cleaner, more maintainable codebase.

## Problem Statement

The current Nelder-Mead implementation for LSQ9/LSQ10 fails to converge properly (avgRMS=4.19 instead of <0.5). The root cause is likely a mismatch between Euler angle extraction and the optimizer's parameter space.

## Solution

Use Eigen's Levenberg-Marquardt optimizer from the `unsupported` module, which is specifically designed for least-squares problems. The cost function (sum of squared distances) is exactly what LM minimizes efficiently.

## Changes Overview

### Files Modified

1. **`/app/new_register/src/Transform.cpp`** — Main implementation changes
2. **`/app/new_register/tests/test_transform.cpp`** — Update test tolerances after fix

### Files Unchanged

- `/app/new_register/include/Transform.h` — No API changes
- `/app/new_register/CMakeLists.txt` — Eigen already configured
- `/app/new_register/tests/CMakeLists.txt` — Eigen includes already present

---

## Implementation Steps

### Step 1: Update includes in Transform.cpp

**Location:** Lines 1-22

**Change:** Add Eigen unsupported module includes

```cpp
// OLD:
#include <Eigen/Dense>

// NEW:
#include <Eigen/Dense>
#include <unsupported/Eigen/NumericalDiff>
#include <unsupported/Eigen/NonLinearOptimization>
```

Also update the file header comment to reference LM instead of Nelder-Mead.

---

### Step 2: Add TransformResidualFunctor struct

**Location:** After `tpsKernel3D()` function (around line 157)

**Add:** A functor struct compatible with Eigen's Levenberg-Marquardt:

```cpp
// ---------------------------------------------------------------------------
// Levenberg-Marquardt Functor for Transform Optimization
// ---------------------------------------------------------------------------

/// Functor for Levenberg-Marquardt optimization of transform parameters.
///
/// Maps a parameter vector to a residual vector:
///   residual[3*i + d] = vol1[i][d] - (M * vol2[i])[d]
///
/// where M is the affine matrix built from the parameters.
///
/// Parameter layout depends on TransformType:
///   LSQ6:  [tx, ty, tz, rx, ry, rz]                          (6 params)
///   LSQ7:  [tx, ty, tz, rx, ry, rz, s]                       (7 params)
///   LSQ9:  [tx, ty, tz, rx, ry, rz, sx, sy, sz]              (9 params)
///   LSQ10: [tx, ty, tz, rx, ry, rz, sx, sy, sz, shx]         (10 params)
struct TransformResidualFunctor
{
    using Scalar = double;
    enum {
        InputsAtCompileTime = Eigen::Dynamic,
        ValuesAtCompileTime = Eigen::Dynamic
    };
    using InputType    = Eigen::VectorXd;
    using ValueType    = Eigen::VectorXd;
    using JacobianType = Eigen::MatrixXd;

    const std::vector<Eigen::Vector3d>& vol1;
    const std::vector<Eigen::Vector3d>& vol2;
    TransformType type;
    int nParams;
    int nResiduals;

    TransformResidualFunctor(
        const std::vector<Eigen::Vector3d>& v1,
        const std::vector<Eigen::Vector3d>& v2,
        TransformType t)
        : vol1(v1), vol2(v2), type(t)
    {
        int n = static_cast<int>(v1.size());
        nResiduals = n * 3;

        switch (t)
        {
        case TransformType::LSQ6:  nParams = 6;  break;
        case TransformType::LSQ7:  nParams = 7;  break;
        case TransformType::LSQ9:  nParams = 9;  break;
        case TransformType::LSQ10: nParams = 10; break;
        default:                   nParams = 9;  break;
        }
    }

    int inputs() const { return nParams; }
    int values() const { return nResiduals; }

    int operator()(const InputType& x, ValueType& fvec) const
    {
        Eigen::Vector3d trans(x(0), x(1), x(2));
        Eigen::Vector3d angles(x(3), x(4), x(5));
        Eigen::Vector3d scales;
        double shear = 0.0;

        switch (type)
        {
        case TransformType::LSQ6:
            scales = Eigen::Vector3d(1.0, 1.0, 1.0);
            break;
        case TransformType::LSQ7:
            scales = Eigen::Vector3d(x(6), x(6), x(6));
            break;
        case TransformType::LSQ9:
            scales = Eigen::Vector3d(x(6), x(7), x(8));
            break;
        case TransformType::LSQ10:
            scales = Eigen::Vector3d(x(6), x(7), x(8));
            shear = x(9);
            break;
        default:
            scales = Eigen::Vector3d(1.0, 1.0, 1.0);
            break;
        }

        Eigen::Matrix4d mat = buildAffineMatrix(trans, angles, scales, shear);

        int n = static_cast<int>(vol2.size());
        for (int i = 0; i < n; ++i)
        {
            Eigen::Vector4d pt(vol2[i](0), vol2[i](1), vol2[i](2), 1.0);
            Eigen::Vector3d transformed = (mat * pt).head<3>();
            Eigen::Vector3d diff = transformed - vol1[i];
            fvec(3 * i + 0) = diff(0);
            fvec(3 * i + 1) = diff(1);
            fvec(3 * i + 2) = diff(2);
        }
        return 0;
    }
};
```

---

### Step 3: Add computeWithLM() function

**Location:** After `TransformResidualFunctor` struct

**Add:** The unified LM solver:

```cpp
// ---------------------------------------------------------------------------
// Unified Levenberg-Marquardt Solver for LSQ6/7/9/10
// ---------------------------------------------------------------------------

/// Unified Levenberg-Marquardt solver for LSQ6/7/9/10.
///
/// Uses Procrustes (LSQ7) as initial guess, extracts parameters,
/// then refines with LM to handle non-uniform scales and shear.
static TransformResult computeWithLM(
    const std::vector<glm::dvec3>& vol1Tags,
    const std::vector<glm::dvec3>& vol2Tags,
    TransformType type)
{
    TransformResult result;
    result.type = type;

    int n = static_cast<int>(vol1Tags.size());

    // --- Get initial guess via Procrustes (with scale) ---
    TransformResult procResult = computeProcrustes(vol1Tags, vol2Tags, true);
    Eigen::Matrix4d procMat = glmToEigen(procResult.linearMatrix);

    // Extract rotation and uniform scale from the 3x3 sub-matrix
    Eigen::Matrix3d upper = procMat.block<3, 3>(0, 0);
    double uniformScale = std::cbrt(std::abs(upper.determinant()));
    if (uniformScale < 1e-30) uniformScale = 1.0;
    Eigen::Matrix3d R = upper / uniformScale;

    // Re-orthogonalize R via SVD to ensure it's a proper rotation
    Eigen::JacobiSVD<Eigen::Matrix3d> svdR(R, Eigen::ComputeFullU | Eigen::ComputeFullV);
    R = svdR.matrixU() * svdR.matrixV().transpose();
    if (R.determinant() < 0)
    {
        Eigen::Matrix3d Vr = svdR.matrixV();
        Vr.col(2) *= -1.0;
        R = svdR.matrixU() * Vr.transpose();
    }

    Eigen::Vector3d angles = rotationToEuler(R);
    Eigen::Vector3d translation = procMat.block<3, 1>(0, 3);

    // --- Determine parameter count ---
    int nParams;
    switch (type)
    {
    case TransformType::LSQ6:  nParams = 6;  break;
    case TransformType::LSQ7:  nParams = 7;  break;
    case TransformType::LSQ9:  nParams = 9;  break;
    case TransformType::LSQ10: nParams = 10; break;
    default:                   nParams = 9;  break;
    }

    // --- Build initial parameter vector ---
    Eigen::VectorXd params(nParams);
    params(0) = translation(0);
    params(1) = translation(1);
    params(2) = translation(2);
    params(3) = angles(0);
    params(4) = angles(1);
    params(5) = angles(2);

    switch (type)
    {
    case TransformType::LSQ6:
        // No scale parameters
        break;
    case TransformType::LSQ7:
        params(6) = uniformScale;
        break;
    case TransformType::LSQ9:
        params(6) = uniformScale;
        params(7) = uniformScale;
        params(8) = uniformScale;
        break;
    case TransformType::LSQ10:
        params(6) = uniformScale;
        params(7) = uniformScale;
        params(8) = uniformScale;
        params(9) = 0.0;  // X shear
        break;
    default:
        break;
    }

    // --- Convert tags to Eigen vectors ---
    std::vector<Eigen::Vector3d> eigVol1(n), eigVol2(n);
    for (int i = 0; i < n; ++i)
    {
        eigVol1[i] = toEigen(vol1Tags[i]);
        eigVol2[i] = toEigen(vol2Tags[i]);
    }

    // --- Run Levenberg-Marquardt ---
    TransformResidualFunctor functor(eigVol1, eigVol2, type);
    Eigen::NumericalDiff<TransformResidualFunctor> numDiff(functor);
    Eigen::LevenbergMarquardt<Eigen::NumericalDiff<TransformResidualFunctor>> lm(numDiff);

    lm.parameters.maxfev = 2000;
    lm.parameters.xtol   = 1e-12;
    lm.parameters.ftol   = 1e-12;
    lm.parameters.gtol   = 1e-12;

    lm.minimize(params);

    // --- Build final matrix from optimized parameters ---
    Eigen::Vector3d bestTrans(params(0), params(1), params(2));
    Eigen::Vector3d bestAngles(params(3), params(4), params(5));
    Eigen::Vector3d bestScales;
    double bestShear = 0.0;

    switch (type)
    {
    case TransformType::LSQ6:
        bestScales = Eigen::Vector3d(1.0, 1.0, 1.0);
        break;
    case TransformType::LSQ7:
        bestScales = Eigen::Vector3d(params(6), params(6), params(6));
        break;
    case TransformType::LSQ9:
        bestScales = Eigen::Vector3d(params(6), params(7), params(8));
        break;
    case TransformType::LSQ10:
        bestScales = Eigen::Vector3d(params(6), params(7), params(8));
        bestShear = params(9);
        break;
    default:
        bestScales = Eigen::Vector3d(1.0, 1.0, 1.0);
        break;
    }

    Eigen::Matrix4d finalMat = buildAffineMatrix(bestTrans, bestAngles, bestScales, bestShear);
    result.linearMatrix = eigenToGlm(finalMat);
    result.valid = true;

    computeRmsErrors(result, vol1Tags, vol2Tags);
    return result;
}
```

---

### Step 4: Remove Nelder-Mead code

**Location:** Lines 337-590 (approximately)

**Remove:** Two functions:
1. `nelderMead()` — the entire simplex optimizer (lines ~337-486)
2. `computeArbParam()` — the broken LSQ9/10 solver (lines ~492-590)

Delete everything between the comment:
```cpp
// ---------------------------------------------------------------------------
// Nelder-Mead Simplex Optimizer
// ---------------------------------------------------------------------------
```

And the comment:
```cpp
// ---------------------------------------------------------------------------
// LSQ12: Full Affine via Direct Linear Least Squares
// ---------------------------------------------------------------------------
```

---

### Step 5: Update the dispatcher in computeTransform()

**Location:** Lines 787-804 (in `computeTransform()` function)

**Change:**

```cpp
// OLD:
switch (type)
{
case TransformType::LSQ6:
    return computeProcrustes(vol1Tags, vol2Tags, false);

case TransformType::LSQ7:
    return computeProcrustes(vol1Tags, vol2Tags, true);

case TransformType::LSQ9:
case TransformType::LSQ10:
    return computeArbParam(vol1Tags, vol2Tags, type);

case TransformType::LSQ12:
    return computeLSQ12(vol1Tags, vol2Tags);

case TransformType::TPS:
    return computeTPS(vol1Tags, vol2Tags);
}

// NEW:
switch (type)
{
case TransformType::LSQ6:
case TransformType::LSQ7:
case TransformType::LSQ9:
case TransformType::LSQ10:
    return computeWithLM(vol1Tags, vol2Tags, type);

case TransformType::LSQ12:
    return computeLSQ12(vol1Tags, vol2Tags);

case TransformType::TPS:
    return computeTPS(vol1Tags, vol2Tags);
}
```

---

### Step 6: Update test_transform.cpp (after verifying fix)

**Location:** `testLSQ9()` function (lines 280-320)

**Changes:**

1. Update comment on line 307:
```cpp
// OLD:
// Nelder-Mead may not converge to exact zero but should be very close

// NEW:
// LM should converge to near-exact solution
```

2. Tighten RMS tolerance (line 308):
```cpp
// OLD:
if (result.avgRMS > 0.5)

// NEW:
if (result.avgRMS > 1e-4)
```

3. Tighten check on line 310:
```cpp
// OLD:
CHECK(result.avgRMS < 0.5, "LSQ9 RMS should be small");

// NEW:
CHECK(result.avgRMS < 1e-4, "LSQ9 RMS should be near zero");
```

4. Tighten point recovery tolerance (line 315):
```cpp
// OLD:
CHECK(vecApprox(recovered, vol1[i], 1.0),

// NEW:
CHECK(vecApprox(recovered, vol1[i], 1e-3),
```

---

## Build & Test Commands

```bash
cd /app/new_register/build
cmake -DENABLE_VULKAN=ON -DENABLE_OPENGL2=ON .. && make -j$(nproc)
ctest --output-on-failure -R TransformTest
```

---

## Expected Results

| Test | Before | After |
|------|--------|-------|
| testMinPoints | PASS | PASS |
| testLSQ6Translation | PASS | PASS |
| testLSQ6Rotation | PASS | PASS |
| testLSQ7Scale | PASS | PASS |
| testLSQ9 | **FAIL** (avgRMS=4.19) | **PASS** (avgRMS < 1e-4) |
| testLSQ12 | PASS | PASS |
| testTPSIdentity | PASS | PASS |
| testTPSDeformation | PASS | PASS |
| testXfmIO | PASS | PASS |
| testXfmTPS | PASS | PASS |
| testTransformNames | PASS | PASS |

---

## Rollback Plan

If LM fails, revert to the Nelder-Mead implementation by:
1. `git checkout main -- new_register/src/Transform.cpp`
2. `git checkout main -- new_register/tests/test_transform.cpp`

---

## References

- Eigen Levenberg-Marquardt: `/app/new_register/build/_deps/eigen-src/unsupported/Eigen/src/NonLinearOptimization/LevenbergMarquardt.h`
- Eigen NumericalDiff: `/app/new_register/build/_deps/eigen-src/unsupported/Eigen/src/NumericalDiff/NumericalDiff.h`
- Legacy BICPL algorithm: `/app/legacy/register/Functionality/tags/tag_transform.c`
