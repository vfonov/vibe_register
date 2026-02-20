// Transform.cpp — Transform computation from tag point pairs.
//
// Implements LSQ6/7/9/10 (Procrustes initial guess + Levenberg-Marquardt
// refinement), LSQ12 (direct linear least squares), and TPS (thin-plate
// spline).
//
// Mathematical references:
//   - Procrustes: Golub & Van Loan "Matrix Computations" pp. 425-426
//   - TPS kernel: Bookstein (1989), Duchon (1977)
//   - Levenberg-Marquardt: Moré (1978), Eigen unsupported module

#include "Transform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>

#include <Eigen/Dense>
#include <unsupported/Eigen/NumericalDiff>
#include <unsupported/Eigen/NonLinearOptimization>

extern "C" {
#include "minc2-simple.h"
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static constexpr double kPi = 3.14159265358979323846;
static constexpr double kDegToRad = kPi / 180.0;

/// Convert a glm::dvec3 vector to an Eigen 3-vector.
static Eigen::Vector3d toEigen(const glm::dvec3& v)
{
    return Eigen::Vector3d(v.x, v.y, v.z);
}

/// Convert an Eigen 3-vector to glm::dvec3.
static glm::dvec3 toGlm(const Eigen::Vector3d& v)
{
    return glm::dvec3(v(0), v(1), v(2));
}

/// Build a 3x3 rotation matrix from Euler angles (rx, ry, rz) in radians.
/// Convention: Rz * Ry * Rx  (same as legacy BICPL).
static Eigen::Matrix3d eulerToRotation(double rx, double ry, double rz)
{
    double cx = std::cos(rx), sx = std::sin(rx);
    double cy = std::cos(ry), sy = std::sin(ry);
    double cz = std::cos(rz), sz = std::sin(rz);

    Eigen::Matrix3d Rx;
    Rx << 1,  0,   0,
          0,  cx, -sx,
          0,  sx,  cx;

    Eigen::Matrix3d Ry;
    Ry << cy,  0,  sy,
          0,   1,  0,
         -sy,  0,  cy;

    Eigen::Matrix3d Rz;
    Rz << cz, -sz, 0,
          sz,  cz, 0,
          0,   0,  1;

    return Rz * Ry * Rx;
}

/// Extract Euler angles from a 3x3 rotation matrix (Rz * Ry * Rx convention).
/// Returns (rx, ry, rz) in radians.
static Eigen::Vector3d rotationToEuler(const Eigen::Matrix3d& R)
{
    double ry = std::asin(std::clamp(R(0, 2), -1.0, 1.0));
    double cy = std::cos(ry);

    double rx, rz;
    if (std::abs(cy) > 1e-12)
    {
        rx = std::atan2(-R(1, 2) / cy, R(2, 2) / cy);
        rz = std::atan2(-R(0, 1) / cy, R(0, 0) / cy);
    }
    else
    {
        // Gimbal lock
        rx = std::atan2(R(2, 1), R(1, 1));
        rz = 0.0;
    }

    return Eigen::Vector3d(rx, ry, rz);
}

/// Build a 4x4 affine matrix from decomposed parameters.
///
/// Parameters:
///   translation (3)
///   rotation (3 Euler angles in rad)
///   scale (3)
///   shear (0 or 1 value — X shear only)
///
/// Composition:  result(p) = SH * R * S * p + translation
/// This applies scaling first, then rotation, then shear.
/// Note: R * S is NOT the same as S * R when scales are non-uniform!
static Eigen::Matrix4d buildAffineMatrix(
    const Eigen::Vector3d& translation,
    const Eigen::Vector3d& angles,
    const Eigen::Vector3d& scales,
    double shearX = 0.0)
{
    Eigen::Matrix3d R = eulerToRotation(angles(0), angles(1), angles(2));

    // Scale matrix
    Eigen::Matrix3d S = Eigen::Matrix3d::Identity();
    S(0, 0) = scales(0);
    S(1, 1) = scales(1);
    S(2, 2) = scales(2);

    // Shear matrix (applied after rotation, affects the result)
    Eigen::Matrix3d SH = Eigen::Matrix3d::Identity();
    SH(0, 1) = shearX;

    // Combined 3x3: SH * R * S (scale first, then rotate, then shear)
    Eigen::Matrix3d M = SH * R * S;

    // Build 4x4
    Eigen::Matrix4d result = Eigen::Matrix4d::Identity();
    result.block<3, 3>(0, 0) = M;
    result.block<3, 1>(0, 3) = translation;

    return result;
}

/// Convert Eigen 4x4 matrix to glm::dmat4.
static glm::dmat4 eigenToGlm(const Eigen::Matrix4d& m)
{
    glm::dmat4 result;
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            result[col][row] = m(row, col);
    return result;
}

/// Convert glm::dmat4 to Eigen 4x4 matrix.
static Eigen::Matrix4d glmToEigen(const glm::dmat4& m)
{
    Eigen::Matrix4d result;
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            result(row, col) = m[col][row];
    return result;
}

/// TPS radial basis function in 3D: U(r) = r.
/// In 3D, the thin-plate spline kernel is simply the Euclidean distance.
static double tpsKernel3D(double r)
{
    return r;
}

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

static TransformResult computeProcrustes(
    const std::vector<glm::dvec3>& vol1Tags,
    const std::vector<glm::dvec3>& vol2Tags,
    bool withScale);

static void computeRmsErrors(
    TransformResult& result,
    const std::vector<glm::dvec3>& vol1Tags,
    const std::vector<glm::dvec3>& vol2Tags);

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

// ---------------------------------------------------------------------------
// Unified Levenberg-Marquardt Solver for LSQ6/7/9/10
// ---------------------------------------------------------------------------

/// Unified Levenberg-Marquardt solver for LSQ6/7/9/10.
///
/// For LSQ6/7: Uses Procrustes as initial guess (already optimal).
/// For LSQ9/10: Uses LSQ12 decomposition for initial guess, then LM refines.
static TransformResult computeWithLM(
    const std::vector<glm::dvec3>& vol1Tags,
    const std::vector<glm::dvec3>& vol2Tags,
    TransformType type)
{
    TransformResult result;
    result.type = type;

    int n = static_cast<int>(vol1Tags.size());

    // --- Get initial guess ---
    // For LSQ6/7, Procrustes gives exact solution
    // For LSQ9/10, we use LSQ12 to get exact affine, then decompose

    Eigen::Matrix4d initialMat;
    Eigen::Vector3d translation;
    Eigen::Vector3d angles;
    Eigen::Vector3d scales;
    double shear = 0.0;

    if (type == TransformType::LSQ6 || type == TransformType::LSQ7)
    {
        // Use Procrustes for rigid/similarity
        TransformResult procResult = computeProcrustes(vol1Tags, vol2Tags, type == TransformType::LSQ7);
        initialMat = glmToEigen(procResult.linearMatrix);

        translation = initialMat.block<3, 1>(0, 3);

        // Extract rotation
        Eigen::Matrix3d R = initialMat.block<3, 3>(0, 0);
        double uniformScale = 1.0;
        if (type == TransformType::LSQ7)
        {
            uniformScale = std::cbrt(std::abs(R.determinant()));
            if (uniformScale < 1e-30) uniformScale = 1.0;
            R = R / uniformScale;
        }

        // Orthogonalize R
        Eigen::JacobiSVD<Eigen::Matrix3d> svdR(R, Eigen::ComputeFullU | Eigen::ComputeFullV);
        R = svdR.matrixU() * svdR.matrixV().transpose();
        if (R.determinant() < 0)
        {
            Eigen::Matrix3d Vr = svdR.matrixV();
            Vr.col(2) *= -1.0;
            R = svdR.matrixU() * Vr.transpose();
        }

        angles = rotationToEuler(R);
        scales = Eigen::Vector3d(uniformScale, uniformScale, uniformScale);
    }
    else
    {
        // For LSQ9/10, use LSQ12 decomposition as initial guess
        // This gives us the exact affine, then we decompose it into R * S or S * R form

        // Build the LSQ12 matrix directly
        Eigen::MatrixXd A(n, 4);
        for (int i = 0; i < n; ++i)
        {
            A(i, 0) = 1.0;
            A(i, 1) = vol2Tags[i].x;
            A(i, 2) = vol2Tags[i].y;
            A(i, 3) = vol2Tags[i].z;
        }

        initialMat = Eigen::Matrix4d::Identity();
        for (int dim = 0; dim < 3; ++dim)
        {
            Eigen::VectorXd b(n);
            for (int i = 0; i < n; ++i)
            {
                b(i) = (dim == 0) ? vol1Tags[i].x :
                       (dim == 1) ? vol1Tags[i].y : vol1Tags[i].z;
            }
            Eigen::VectorXd sol = A.colPivHouseholderQr().solve(b);
            initialMat(dim, 3) = sol(0);
            initialMat(dim, 0) = sol(1);
            initialMat(dim, 1) = sol(2);
            initialMat(dim, 2) = sol(3);
        }

        translation = initialMat.block<3, 1>(0, 3);

        // The decomposition is M = R * S (our new parameterization)
        // We use SVD: M = U * Σ * V^T
        // For R * S decomposition: R = U * V^T, S = R^T * M = V * Σ * V^T
        //
        // But for R * S with diagonal S, we need a different approach.
        // Given M = R * S, we have S = R^T * M (diagonal in ideal case)
        // Use SVD to find R first, then compute S.

        Eigen::Matrix3d M = initialMat.block<3, 3>(0, 0);
        Eigen::JacobiSVD<Eigen::Matrix3d> svd(M, Eigen::ComputeFullU | Eigen::ComputeFullV);
        Eigen::Matrix3d U = svd.matrixU();
        Eigen::Matrix3d V = svd.matrixV();

        // Rotation: R = U * V^T (closest orthogonal matrix)
        Eigen::Matrix3d R = U * V.transpose();
        if (R.determinant() < 0)
        {
            V.col(2) *= -1.0;
            R = U * V.transpose();
        }

        // For R * S = M, we have S = R^T * M
        // But S should be diagonal. If M can be written as R * S_diag,
        // then R^T * M will be diagonal.
        // Otherwise, there's shear (off-diagonal terms).
        Eigen::Matrix3d S_mat = R.transpose() * M;
        scales = Eigen::Vector3d(S_mat(0, 0), S_mat(1, 1), S_mat(2, 2));

        // Handle negative scales
        for (int i = 0; i < 3; ++i)
        {
            if (scales(i) < 0)
            {
                scales(i) = -scales(i);
                R.col(i) *= -1.0;
            }
        }

        angles = rotationToEuler(R);

        // For LSQ10, also estimate shear
        if (type == TransformType::LSQ10)
        {
            shear = S_mat(0, 1);  // X shear component
        }
    }

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
        params(6) = scales(0);
        break;
    case TransformType::LSQ9:
        params(6) = scales(0);
        params(7) = scales(1);
        params(8) = scales(2);
        break;
    case TransformType::LSQ10:
        params(6) = scales(0);
        params(7) = scales(1);
        params(8) = scales(2);
        params(9) = shear;
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

// ---------------------------------------------------------------------------
// Transform name
// ---------------------------------------------------------------------------

const char* transformTypeName(TransformType type)
{
    switch (type)
    {
    case TransformType::LSQ6:  return "LSQ6 (Rigid)";
    case TransformType::LSQ7:  return "LSQ7 (Similarity)";
    case TransformType::LSQ9:  return "LSQ9 (9 param)";
    case TransformType::LSQ10: return "LSQ10 (10 param)";
    case TransformType::LSQ12: return "LSQ12 (Full Affine)";
    case TransformType::TPS:   return "TPS (Thin-Plate Spline)";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// TransformResult::transformPoint
// ---------------------------------------------------------------------------

glm::dvec3 TransformResult::transformPoint(const glm::dvec3& pt) const
{
    if (!valid)
        return pt;

    if (type != TransformType::TPS || tpsWeights.empty())
    {
        // Linear transform: 4x4 matrix multiply
        glm::dvec4 h(pt, 1.0);
        glm::dvec4 r = linearMatrix * h;
        return glm::dvec3(r);
    }

    // TPS transform
    int n = static_cast<int>(tpsSourcePoints.size());
    glm::dvec3 result(0.0);

    // Kernel contributions
    for (int i = 0; i < n; ++i)
    {
        double dx = pt.x - tpsSourcePoints[i].x;
        double dy = pt.y - tpsSourcePoints[i].y;
        double dz = pt.z - tpsSourcePoints[i].z;
        double r = std::sqrt(dx * dx + dy * dy + dz * dz);
        double u = tpsKernel3D(r);
        result += tpsWeights[i] * u;
    }

    // Affine part: constant + linear
    result += tpsWeights[n];             // a0
    result += tpsWeights[n + 1] * pt.x;  // a1 * x
    result += tpsWeights[n + 2] * pt.y;  // a2 * y
    result += tpsWeights[n + 3] * pt.z;  // a3 * z

    return result;
}

// ---------------------------------------------------------------------------
// TransformResult::inverseTransformPoint
// ---------------------------------------------------------------------------

glm::dvec3 TransformResult::inverseTransformPoint(const glm::dvec3& pt,
                                                   int maxIter,
                                                   double tolerance) const
{
    if (!valid)
        return pt;

    if (type != TransformType::TPS || tpsWeights.empty())
    {
        // Linear transform: apply inverse matrix
        glm::dmat4 inv = glm::inverse(linearMatrix);
        glm::dvec4 h(pt, 1.0);
        glm::dvec4 r = inv * h;
        return glm::dvec3(r);
    }

    // TPS: Newton-Raphson iteration to invert the forward transform.
    // We want to find q such that transformPoint(q) = pt.
    // Start with the inverse of the affine part as initial guess.
    int n = static_cast<int>(tpsSourcePoints.size());

    // Build the 3x3 affine part from tpsWeights[n+1..n+3]
    // transformPoint includes: result += w[n] + w[n+1]*x + w[n+2]*y + w[n+3]*z
    // So the linear part is the 3x3 matrix with columns w[n+1], w[n+2], w[n+3]
    glm::dmat3 A(
        tpsWeights[n + 1].x, tpsWeights[n + 1].y, tpsWeights[n + 1].z,  // col 0
        tpsWeights[n + 2].x, tpsWeights[n + 2].y, tpsWeights[n + 2].z,  // col 1
        tpsWeights[n + 3].x, tpsWeights[n + 3].y, tpsWeights[n + 3].z   // col 2
    );
    glm::dvec3 b = tpsWeights[n];  // constant term
    glm::dmat3 Ainv = glm::inverse(A);

    // Initial guess: invert the affine part only
    glm::dvec3 q = Ainv * (pt - b);

    double tol2 = tolerance * tolerance;

    for (int iter = 0; iter < maxIter; ++iter)
    {
        glm::dvec3 fq = transformPoint(q);
        glm::dvec3 residual = fq - pt;

        if (glm::dot(residual, residual) < tol2)
            break;

        // Compute Jacobian numerically (3x3)
        constexpr double eps = 1e-6;
        glm::dmat3 J;
        for (int d = 0; d < 3; ++d)
        {
            glm::dvec3 qp = q;
            glm::dvec3 qm = q;
            qp[d] += eps;
            qm[d] -= eps;
            glm::dvec3 fp = transformPoint(qp);
            glm::dvec3 fm = transformPoint(qm);
            glm::dvec3 col = (fp - fm) / (2.0 * eps);
            J[d] = col;  // glm stores column-major
        }

        // Newton step: q -= J^{-1} * residual
        glm::dmat3 Jinv = glm::inverse(J);
        q -= Jinv * residual;
    }

    return q;
}

// ---------------------------------------------------------------------------
// RMS error computation
// ---------------------------------------------------------------------------

/// Compute per-tag and average RMS errors.
static void computeRmsErrors(
    TransformResult& result,
    const std::vector<glm::dvec3>& vol1Tags,
    const std::vector<glm::dvec3>& vol2Tags)
{
    int n = static_cast<int>(vol1Tags.size());
    result.perTagRMS.resize(n);
    double sumSq = 0.0;

    for (int i = 0; i < n; ++i)
    {
        glm::dvec3 transformed = result.transformPoint(vol2Tags[i]);
        glm::dvec3 diff = transformed - vol1Tags[i];
        double dist = std::sqrt(glm::dot(diff, diff));
        result.perTagRMS[i] = dist;
        sumSq += dist * dist;
    }

    result.avgRMS = (n > 0) ? std::sqrt(sumSq / n) : 0.0;
}

// ---------------------------------------------------------------------------
// LSQ6 / LSQ7: Procrustes via SVD
// ---------------------------------------------------------------------------

/// Compute a rigid (LSQ6) or similarity (LSQ7) transform using the
/// Procrustes algorithm with SVD decomposition.
///
/// Given point sets A (vol1, target) and B (vol2, source), finds the
/// rotation R, scale s, and translation t that minimizes:
///     sum_i || A_i - (s * R * B_i + t) ||^2
///
/// For LSQ6, s is fixed at 1.0.
static TransformResult computeProcrustes(
    const std::vector<glm::dvec3>& vol1Tags,
    const std::vector<glm::dvec3>& vol2Tags,
    bool withScale)
{
    TransformResult result;
    result.type = withScale ? TransformType::LSQ7 : TransformType::LSQ6;

    int n = static_cast<int>(vol1Tags.size());

    // Compute centroids
    Eigen::Vector3d centA = Eigen::Vector3d::Zero();
    Eigen::Vector3d centB = Eigen::Vector3d::Zero();
    for (int i = 0; i < n; ++i)
    {
        centA += toEigen(vol1Tags[i]);
        centB += toEigen(vol2Tags[i]);
    }
    centA /= n;
    centB /= n;

    // Center the points
    Eigen::MatrixXd Ashift(n, 3);
    Eigen::MatrixXd Bshift(n, 3);
    for (int i = 0; i < n; ++i)
    {
        Ashift.row(i) = (toEigen(vol1Tags[i]) - centA).transpose();
        Bshift.row(i) = (toEigen(vol2Tags[i]) - centB).transpose();
    }

    // Cross-covariance matrix: M = B^T * A
    Eigen::Matrix3d M = Bshift.transpose() * Ashift;

    // SVD: M = U * W * V^T
    // For the Procrustes problem R * B ≈ A with M = B^T * A,
    // the optimal rotation is R = V * U^T.
    Eigen::JacobiSVD<Eigen::Matrix3d> svd(M, Eigen::ComputeFullU | Eigen::ComputeFullV);
    Eigen::Matrix3d U = svd.matrixU();
    Eigen::Matrix3d V = svd.matrixV();

    // Ensure proper rotation (det = +1, not reflection)
    Eigen::Matrix3d R = V * U.transpose();
    if (R.determinant() < 0)
    {
        // Flip the sign of the column of V corresponding to the smallest
        // singular value to ensure a proper rotation.
        V.col(2) *= -1.0;
        R = V * U.transpose();
    }

    // Compute scale (LSQ7 only)
    double scale = 1.0;
    if (withScale)
    {
        // We want R * b_i for each point b_i (column vector).
        // With Bshift as (n x 3) row-form: (R * b_i^T)^T = b_i * R^T.
        Eigen::MatrixXd Brotated = Bshift * R.transpose();
        double num = 0.0;
        double den = 0.0;
        for (int i = 0; i < n; ++i)
        {
            num += Brotated.row(i).dot(Ashift.row(i));
            den += Bshift.row(i).dot(Bshift.row(i));
        }
        if (den > 1e-30)
            scale = num / den;
    }

    // Build 4x4 transform: A_i = scale * R * B_i + (centA - scale * R * centB)
    Eigen::Vector3d translation = centA - scale * R * centB;

    Eigen::Matrix4d mat = Eigen::Matrix4d::Identity();
    mat.block<3, 3>(0, 0) = scale * R;
    mat.block<3, 1>(0, 3) = translation;

    result.linearMatrix = eigenToGlm(mat);
    result.valid = true;

    computeRmsErrors(result, vol1Tags, vol2Tags);
    return result;
}

// ---------------------------------------------------------------------------
// LSQ12: Full Affine via Direct Linear Least Squares
// ---------------------------------------------------------------------------

static TransformResult computeLSQ12(
    const std::vector<glm::dvec3>& vol1Tags,
    const std::vector<glm::dvec3>& vol2Tags)
{
    TransformResult result;
    result.type = TransformType::LSQ12;

    int n = static_cast<int>(vol1Tags.size());

    // For each output dimension (x, y, z), solve an independent least squares:
    //   vol1[i][dim] = a0 + a1*vol2[i].x + a2*vol2[i].y + a3*vol2[i].z
    //
    // This is the standard overdetermined linear system: A * x = b
    // where A is (n x 4) and b is (n x 1).

    Eigen::MatrixXd A(n, 4);
    for (int i = 0; i < n; ++i)
    {
        A(i, 0) = 1.0;             // constant term
        A(i, 1) = vol2Tags[i].x;
        A(i, 2) = vol2Tags[i].y;
        A(i, 3) = vol2Tags[i].z;
    }

    // Solve via normal equations: (A^T A) x = A^T b
    // Use Eigen's ColPivHouseholderQR for numerical stability.
    Eigen::Matrix4d mat = Eigen::Matrix4d::Identity();

    for (int dim = 0; dim < 3; ++dim)
    {
        Eigen::VectorXd b(n);
        for (int i = 0; i < n; ++i)
        {
            b(i) = (dim == 0) ? vol1Tags[i].x :
                   (dim == 1) ? vol1Tags[i].y : vol1Tags[i].z;
        }

        Eigen::VectorXd solution = A.colPivHouseholderQr().solve(b);

        // solution = [a0, a1, a2, a3]
        mat(dim, 3) = solution(0);  // translation
        mat(dim, 0) = solution(1);  // x coefficient
        mat(dim, 1) = solution(2);  // y coefficient
        mat(dim, 2) = solution(3);  // z coefficient
    }

    result.linearMatrix = eigenToGlm(mat);
    result.valid = true;

    computeRmsErrors(result, vol1Tags, vol2Tags);
    return result;
}

// ---------------------------------------------------------------------------
// TPS: Thin-Plate Spline
// ---------------------------------------------------------------------------

/// Compute a thin-plate spline transform.
///
/// The TPS maps vol2 points -> vol1 points.  Following the legacy convention,
/// we compute weights using vol1 as source (kernel) points, so that forward
/// evaluation maps from vol2 space to vol1 space.
///
/// Actually, for the register use case the transform maps vol2 -> vol1:
///   - We solve for f such that f(vol2[i]) ~ vol1[i]
///   - Source/kernel points are the vol2 positions
///   - Target positions are the vol1 positions
///
/// The TPS system is:
///   | K   P | | w |   | Y |
///   | P^T 0 | | a | = | 0 |
///
/// where K[i][j] = U(||source[i] - source[j]||),
///       P[i] = [1, source[i].x, source[i].y, source[i].z],
///       Y = target positions,
///       w = kernel weights, a = affine coefficients.
static TransformResult computeTPS(
    const std::vector<glm::dvec3>& vol1Tags,
    const std::vector<glm::dvec3>& vol2Tags)
{
    TransformResult result;
    result.type = TransformType::TPS;

    int n = static_cast<int>(vol1Tags.size());
    int systemSize = n + 4;  // n kernel + 1 constant + 3 linear

    // Build the L matrix
    Eigen::MatrixXd L = Eigen::MatrixXd::Zero(systemSize, systemSize);

    // Upper-left n x n: kernel matrix K
    for (int i = 0; i < n; ++i)
    {
        for (int j = i + 1; j < n; ++j)
        {
            double dx = vol2Tags[i].x - vol2Tags[j].x;
            double dy = vol2Tags[i].y - vol2Tags[j].y;
            double dz = vol2Tags[i].z - vol2Tags[j].z;
            double r = std::sqrt(dx * dx + dy * dy + dz * dz);
            double u = tpsKernel3D(r);
            L(i, j) = u;
            L(j, i) = u;
        }
        // L(i, i) = 0 (U(0) = 0)
    }

    // Upper-right n x 4: P matrix
    for (int i = 0; i < n; ++i)
    {
        L(i, n)     = 1.0;
        L(i, n + 1) = vol2Tags[i].x;
        L(i, n + 2) = vol2Tags[i].y;
        L(i, n + 3) = vol2Tags[i].z;
    }

    // Lower-left 4 x n: P^T
    for (int i = 0; i < n; ++i)
    {
        L(n, i)     = 1.0;
        L(n + 1, i) = vol2Tags[i].x;
        L(n + 2, i) = vol2Tags[i].y;
        L(n + 3, i) = vol2Tags[i].z;
    }

    // Lower-right 4 x 4: zeros (already initialized)

    // Right-hand side: target positions (vol1) padded with zeros
    Eigen::MatrixXd Y = Eigen::MatrixXd::Zero(systemSize, 3);
    for (int i = 0; i < n; ++i)
    {
        Y(i, 0) = vol1Tags[i].x;
        Y(i, 1) = vol1Tags[i].y;
        Y(i, 2) = vol1Tags[i].z;
    }

    // Solve L * W = Y
    Eigen::ColPivHouseholderQR<Eigen::MatrixXd> solver(L);
    if (!solver.isInvertible())
    {
        // Fall back to least-squares if singular
        Eigen::MatrixXd W = L.completeOrthogonalDecomposition().solve(Y);

        // Store results
        result.tpsSourcePoints.resize(n);
        result.tpsWeights.resize(systemSize);
        for (int i = 0; i < n; ++i)
            result.tpsSourcePoints[i] = vol2Tags[i];
        for (int i = 0; i < systemSize; ++i)
            result.tpsWeights[i] = glm::dvec3(W(i, 0), W(i, 1), W(i, 2));

        result.valid = true;
        computeRmsErrors(result, vol1Tags, vol2Tags);
        return result;
    }

    Eigen::MatrixXd W = solver.solve(Y);

    // Store results
    result.tpsSourcePoints.resize(n);
    result.tpsWeights.resize(systemSize);

    for (int i = 0; i < n; ++i)
        result.tpsSourcePoints[i] = vol2Tags[i];

    for (int i = 0; i < systemSize; ++i)
        result.tpsWeights[i] = glm::dvec3(W(i, 0), W(i, 1), W(i, 2));

    result.valid = true;
    computeRmsErrors(result, vol1Tags, vol2Tags);
    return result;
}

// ---------------------------------------------------------------------------
// Public API: computeTransform
// ---------------------------------------------------------------------------

TransformResult computeTransform(
    const std::vector<glm::dvec3>& vol1Tags,
    const std::vector<glm::dvec3>& vol2Tags,
    TransformType type)
{
    TransformResult result;
    result.type = type;

    int n = static_cast<int>(vol1Tags.size());
    if (n != static_cast<int>(vol2Tags.size()))
        return result;  // Mismatched sizes

    int minPoints = (type == TransformType::TPS) ? kMinPointsTPS : kMinPointsLinear;
    if (n < minPoints)
        return result;  // Not enough points

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

    return result;
}

// ---------------------------------------------------------------------------
// XFM file I/O
// ---------------------------------------------------------------------------

bool writeXfmFile(const std::string& path, const TransformResult& result)
{
    if (!result.valid)
        return false;

    if (result.type == TransformType::TPS)
    {
        std::ofstream out(path);
        if (!out.is_open())
            return false;

        int n = static_cast<int>(result.tpsSourcePoints.size());
        int nDims = 3;

        out << "MNI Transform File\n";
        out << "\nTransform_Type = Thin_Plate_Spline_Transform;\n";
        out << "Invert_Flag = True;\n";
        out << "Number_Dimensions = " << nDims << ";\n";

        out << "Points =\n";
        for (int i = 0; i < n; ++i)
        {
            const glm::dvec3& p = result.tpsSourcePoints[i];
            char buf[256];
            std::snprintf(buf, sizeof(buf), " %.15g %.15g %.15g",
                          p.x, p.y, p.z);
            out << buf;
            if (i < n - 1)
                out << "\n";
            else
                out << ";\n";
        }

        int nWeights = n + nDims + 1;
        out << "Displacements =\n";
        for (int i = 0; i < nWeights; ++i)
        {
            const glm::dvec3& w = result.tpsWeights[i];
            char buf[256];
            std::snprintf(buf, sizeof(buf), " %.15g %.15g %.15g",
                          w.x, w.y, w.z);
            out << buf;
            if (i < nWeights - 1)
                out << "\n";
            else
                out << ";\n";
        }

        return out.good();
    }

    minc2_xfm_file_handle xfm = minc2_xfm_allocate0();
    if (!xfm)
        return false;

    double matrix[16];
    const glm::dmat4& m = result.linearMatrix;
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            matrix[row * 4 + col] = m[col][row];
        }
    }

    if (minc2_xfm_append_linear_transform(xfm, matrix) != MINC2_SUCCESS)
    {
        minc2_xfm_destroy(xfm);
        return false;
    }

    bool success = (minc2_xfm_save(xfm, path.c_str()) == MINC2_SUCCESS);
    minc2_xfm_destroy(xfm);
    return success;
}

bool readXfmFile(const std::string& path, glm::dmat4& matrix)
{
    minc2_xfm_file_handle xfm = minc2_xfm_allocate0();
    if (!xfm)
        return false;

    if (minc2_xfm_open(xfm, path.c_str()) != MINC2_SUCCESS)
    {
        minc2_xfm_destroy(xfm);
        return false;
    }

    int nConcat = 0;
    if (minc2_xfm_get_n_concat(xfm, &nConcat) != MINC2_SUCCESS || nConcat < 1)
    {
        minc2_xfm_destroy(xfm);
        return false;
    }

    int xfmType = 0;
    if (minc2_xfm_get_n_type(xfm, 0, &xfmType) != MINC2_SUCCESS)
    {
        minc2_xfm_destroy(xfm);
        return false;
    }

    if (xfmType != MINC2_XFM_LINEAR)
    {
        std::cerr << "Error: Only linear transforms are supported for reading\n";
        minc2_xfm_destroy(xfm);
        return false;
    }

    double mat[16];
    if (minc2_xfm_get_linear_transform(xfm, 0, mat) != MINC2_SUCCESS)
    {
        minc2_xfm_destroy(xfm);
        return false;
    }

    matrix = glm::dmat4(1.0);
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            matrix[col][row] = mat[row * 4 + col];
        }
    }

    minc2_xfm_destroy(xfm);
    return true;
}
