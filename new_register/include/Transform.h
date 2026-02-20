#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

/// Supported transform types for tag-point registration.
/// Matches the legacy register/BICPL naming convention.
enum class TransformType
{
    LSQ6,   ///< 3 rotations + 3 translations (rigid body)
    LSQ7,   ///< 3 rotations + 3 translations + 1 uniform scale (similarity)
    LSQ9,   ///< 3 rotations + 3 translations + 3 independent scales
    LSQ10,  ///< 3 rotations + 3 translations + 3 scales + 1 shear (X)
    LSQ12,  ///< Full affine (12 parameters)
    TPS     ///< Thin-plate spline (non-linear)
};

/// Human-readable display names for each transform type.
const char* transformTypeName(TransformType type);

/// Number of TransformType enum values.
constexpr int kTransformTypeCount = 6;

/// Minimum number of tag point pairs for linear transforms.
constexpr int kMinPointsLinear = 4;

/// Minimum number of tag point pairs for TPS.
constexpr int kMinPointsTPS = 5;

/// Result of a transform computation.  For linear transforms, only
/// `linearMatrix` is populated.  For TPS, `tpsSourcePoints` and
/// `tpsWeights` are also filled in.
struct TransformResult
{
    bool valid = false;                ///< True if transform was computed successfully.
    TransformType type = TransformType::LSQ6;

    /// 4x4 affine matrix (vol2 -> vol1).  Used directly for linear types;
    /// for TPS this holds the affine part only (useful for initial guess).
    glm::dmat4 linearMatrix{1.0};

    /// Per-tag RMS errors (Euclidean distance between transformed vol2 tag
    /// and vol1 tag).
    std::vector<double> perTagRMS;

    /// Average RMS error across all active tag pairs.
    double avgRMS = 0.0;

    /// --- TPS-specific data ---

    /// Source points (vol1 positions) used to build the TPS.
    /// Size: n_points.
    std::vector<glm::dvec3> tpsSourcePoints;

    /// TPS displacement / weight matrix.
    /// Size: (n_points + 4) rows x 3 columns.
    /// Rows 0..n_points-1 are kernel weights.
    /// Row n_points is the constant (a0).
    /// Rows n_points+1..n_points+3 are the linear coefficients.
    std::vector<glm::dvec3> tpsWeights;

    /// Apply this transform to a point (works for both linear and TPS).
    glm::dvec3 transformPoint(const glm::dvec3& pt) const;

    /// Apply the inverse of this transform to a point.
    /// For linear types, uses glm::inverse(linearMatrix).
    /// For TPS, uses Newton-Raphson iteration to invert the forward transform.
    /// @param pt          Point in vol1 (reference) world space.
    /// @param maxIter     Maximum Newton iterations (default 20).
    /// @param tolerance   Convergence tolerance in mm (default 1e-6).
    /// @return            Corresponding point in vol2 (source) world space.
    glm::dvec3 inverseTransformPoint(const glm::dvec3& pt,
                                     int maxIter = 20,
                                     double tolerance = 1e-6) const;
};

/// Compute a transform from paired tag points.
///
/// @param vol1Tags   Tag positions in volume 1 (target / reference).
/// @param vol2Tags   Tag positions in volume 2 (source / moving).
///                   Must be the same size as vol1Tags.
/// @param type       Type of transform to compute.
/// @return           TransformResult with valid=true on success.
///
/// Requires at least kMinPointsLinear (4) pairs for linear types,
/// or kMinPointsTPS (5) for TPS.
TransformResult computeTransform(
    const std::vector<glm::dvec3>& vol1Tags,
    const std::vector<glm::dvec3>& vol2Tags,
    TransformType type);

/// Write a transform to an MNI .xfm file.
/// Supports both linear and TPS transforms.
///
/// @param path     Output file path.
/// @param result   The computed transform to write.
/// @return         True on success.
bool writeXfmFile(const std::string& path, const TransformResult& result);

/// Read a linear transform from an MNI .xfm file.
/// (TPS reading is not yet supported.)
///
/// @param path     Input file path.
/// @param matrix   Output 4x4 matrix.
/// @return         True on success.
bool readXfmFile(const std::string& path, glm::dmat4& matrix);
