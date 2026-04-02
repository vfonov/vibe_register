/// test_coord_sq2tr.cpp — Targeted diagnostics for the three coordinate-chain
/// hypotheses that could cause OverlapTest to fail with sq2_tr.mnc.
///
/// Usage: test_coord_sq2tr <sq1.mnc> <sq2_tr.mnc>
///
/// H1  Was voxelToWorld built correctly from the raw MINC metadata?
///     Checks each column of the matrix against step[i]*dirCos[i] and
///     checks the translation column against start.
///
/// H2  Is worldToVoxel the exact inverse of voxelToWorld?
///     Checks M_inv * M == I and that known world points round-trip cleanly.
///
/// H3  Is the complete chain  sq1-voxel → world → sq2_tr-voxel  correct?
///     Computes the chain analytically (from raw metadata, avoiding the
///     possibly-buggy matrices) and compares against the matrix result.
///
/// All expected values were computed independently with NumPy using the
/// metadata printed by dump_vol.

#include "Volume.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <string>

#include <glm/glm.hpp>

static int g_pass = 0, g_fail = 0;

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

static bool near(double a, double b, double tol = 1e-4)
{
    return std::abs(a - b) <= tol;
}

static bool nearVec3(glm::dvec3 a, glm::dvec3 b, double tol = 1e-4)
{
    return near(a.x, b.x, tol) && near(a.y, b.y, tol) && near(a.z, b.z, tol);
}

static bool nearVec4(glm::dvec4 a, glm::dvec4 b, double tol = 1e-4)
{
    return near(a.x, b.x, tol) && near(a.y, b.y, tol)
        && near(a.z, b.z, tol) && near(a.w, b.w, tol);
}

#define CHECK(name, cond) \
    do { \
        if (cond) { std::cerr << "  PASS  " << name << "\n"; ++g_pass; } \
        else      { std::cerr << "  FAIL  " << name << "\n"; ++g_fail; } \
    } while (0)

#define CHECK_VEC3(name, got, exp, tol) \
    do { \
        bool ok = nearVec3(got, exp, tol); \
        std::cerr << (ok ? "  PASS  " : "  FAIL  ") << name \
                  << "  got=(" << got.x << "," << got.y << "," << got.z << ")" \
                  << "  exp=(" << exp.x << "," << exp.y << "," << exp.z << ")\n"; \
        if (ok) ++g_pass; else ++g_fail; \
    } while (0)

// --------------------------------------------------------------------------
// H1 — voxelToWorld built correctly from metadata?
// --------------------------------------------------------------------------
static void testH1(const Volume& v2)
{
    std::cerr << "\n=== H1: voxelToWorld columns match step[i]*dirCos[i] ===\n";
    std::cerr << std::fixed << std::setprecision(6);

    // For each axis i, column i of voxelToWorld must equal dirCos[i] * step[i].
    // Translation column (col 3) must equal start.
    for (int i = 0; i < 3; ++i)
    {
        glm::dvec3 expectedCol(
            v2.dirCos[i][0] * v2.step[i],
            v2.dirCos[i][1] * v2.step[i],
            v2.dirCos[i][2] * v2.step[i]);

        glm::dvec3 gotCol(
            v2.voxelToWorld[i][0],  // GLM: [col][row]
            v2.voxelToWorld[i][1],
            v2.voxelToWorld[i][2]);

        std::string name = "col[" + std::to_string(i) + "] == dirCos[" + std::to_string(i) + "]*step";
        CHECK_VEC3(name, gotCol, expectedCol, 1e-4);
    }

    glm::dvec3 gotTrans(v2.voxelToWorld[3][0], v2.voxelToWorld[3][1], v2.voxelToWorld[3][2]);
    glm::dmat3 dc3check(v2.dirCos[0][0], v2.dirCos[0][1], v2.dirCos[0][2],
                        v2.dirCos[1][0], v2.dirCos[1][1], v2.dirCos[1][2],
                        v2.dirCos[2][0], v2.dirCos[2][1], v2.dirCos[2][2]);
    glm::dvec3 expTrans = dc3check * glm::dvec3(v2.start.x, v2.start.y, v2.start.z);
    CHECK_VEC3("translation column == dirCos3*start", gotTrans, expTrans, 1e-4);

    // Spot check: voxelToWorld*(0,0,0,1) == dirCos3 * start  (NOT start directly)
    {
        auto w = v2.voxelToWorld * glm::dvec4(0, 0, 0, 1);
        // dirCos3 * start: column i of dirCos3 is dirCos[i], so result = sum_i dirCos[i]*start[i]
        glm::dmat3 dc3(v2.dirCos[0][0], v2.dirCos[0][1], v2.dirCos[0][2],
                       v2.dirCos[1][0], v2.dirCos[1][1], v2.dirCos[1][2],
                       v2.dirCos[2][0], v2.dirCos[2][1], v2.dirCos[2][2]);
        glm::dvec3 exp = dc3 * glm::dvec3(v2.start.x, v2.start.y, v2.start.z);
        CHECK_VEC3("V2W*(0,0,0,1) == dirCos3*start", glm::dvec3(w), exp, 1e-4);
    }
    // Spot check: voxelToWorld*(1,0,0,1) == dirCos3*start + step_x*dirCos_x
    {
        glm::dmat3 dc3(v2.dirCos[0][0], v2.dirCos[0][1], v2.dirCos[0][2],
                       v2.dirCos[1][0], v2.dirCos[1][1], v2.dirCos[1][2],
                       v2.dirCos[2][0], v2.dirCos[2][1], v2.dirCos[2][2]);
        glm::dvec3 t = dc3 * glm::dvec3(v2.start.x, v2.start.y, v2.start.z);
        glm::dvec3 exp(t.x + v2.step.x * v2.dirCos[0][0],
                       t.y + v2.step.x * v2.dirCos[0][1],
                       t.z + v2.step.x * v2.dirCos[0][2]);
        auto w = v2.voxelToWorld * glm::dvec4(1, 0, 0, 1);
        CHECK_VEC3("V2W*(1,0,0,1) == dirCos3*start + step_x*dirCos_x", glm::dvec3(w), exp, 1e-4);
    }
    // voxelToWorld*(0,1,0,1) == dirCos3*start + step_y*dirCos_y
    {
        glm::dmat3 dc3(v2.dirCos[0][0], v2.dirCos[0][1], v2.dirCos[0][2],
                       v2.dirCos[1][0], v2.dirCos[1][1], v2.dirCos[1][2],
                       v2.dirCos[2][0], v2.dirCos[2][1], v2.dirCos[2][2]);
        glm::dvec3 t = dc3 * glm::dvec3(v2.start.x, v2.start.y, v2.start.z);
        glm::dvec3 exp(t.x + v2.step.y * v2.dirCos[1][0],
                       t.y + v2.step.y * v2.dirCos[1][1],
                       t.z + v2.step.y * v2.dirCos[1][2]);
        auto w = v2.voxelToWorld * glm::dvec4(0, 1, 0, 1);
        CHECK_VEC3("V2W*(0,1,0,1) == dirCos3*start + step_y*dirCos_y", glm::dvec3(w), exp, 1e-4);
    }
    // voxelToWorld*(0,0,1,1) == dirCos3*start + step_z*dirCos_z
    {
        glm::dmat3 dc3(v2.dirCos[0][0], v2.dirCos[0][1], v2.dirCos[0][2],
                       v2.dirCos[1][0], v2.dirCos[1][1], v2.dirCos[1][2],
                       v2.dirCos[2][0], v2.dirCos[2][1], v2.dirCos[2][2]);
        glm::dvec3 t = dc3 * glm::dvec3(v2.start.x, v2.start.y, v2.start.z);
        glm::dvec3 exp(t.x + v2.step.z * v2.dirCos[2][0],
                       t.y + v2.step.z * v2.dirCos[2][1],
                       t.z + v2.step.z * v2.dirCos[2][2]);
        auto w = v2.voxelToWorld * glm::dvec4(0, 0, 1, 1);
        CHECK_VEC3("V2W*(0,0,1,1) == dirCos3*start + step_z*dirCos_z", glm::dvec3(w), exp, 1e-4);
    }
    // Last voxel (49,49,49) -> expected corrected world corner (~30.3, ~116.8, ~101.2)
    {
        int mx = v2.dimensions.x - 1, my = v2.dimensions.y - 1, mz = v2.dimensions.z - 1;
        auto w = v2.voxelToWorld * glm::dvec4(mx, my, mz, 1);
        // Expected from Python with corrected translation: (30.341, 116.844, 101.212)
        glm::dvec3 exp(30.341, 116.844, 101.212);
        CHECK_VEC3("V2W*(49,49,49,1) == corrected world corner", glm::dvec3(w), exp, 1e-2);
    }
}

// --------------------------------------------------------------------------
// H2 — worldToVoxel is exact inverse of voxelToWorld?
// --------------------------------------------------------------------------
static void testH2(const Volume& v2)
{
    std::cerr << "\n=== H2: worldToVoxel is exact inverse of voxelToWorld ===\n";

    // Product should be identity
    glm::dmat4 product = v2.worldToVoxel * v2.voxelToWorld;
    bool isIdentity = true;
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
        {
            double expected = (col == row) ? 1.0 : 0.0;
            if (!near(product[col][row], expected, 1e-9))
                isIdentity = false;
        }
    CHECK("worldToVoxel * voxelToWorld == I (1e-9)", isIdentity);
    if (!isIdentity)
    {
        std::cerr << "    Matrix product:\n";
        for (int row = 0; row < 4; ++row)
        {
            std::cerr << "    [";
            for (int col = 0; col < 4; ++col)
                std::cerr << std::setw(12) << product[col][row];
            std::cerr << " ]\n";
        }
    }

    // W2V * (dirCos3*start) == (0,0,0)  — world origin voxel maps back to (0,0,0)
    {
        glm::dmat3 dc3(v2.dirCos[0][0], v2.dirCos[0][1], v2.dirCos[0][2],
                       v2.dirCos[1][0], v2.dirCos[1][1], v2.dirCos[1][2],
                       v2.dirCos[2][0], v2.dirCos[2][1], v2.dirCos[2][2]);
        glm::dvec3 origin = dc3 * glm::dvec3(v2.start.x, v2.start.y, v2.start.z);
        auto sv = v2.worldToVoxel * glm::dvec4(origin, 1);
        CHECK_VEC3("W2V * (dirCos3*start) == (0,0,0)", glm::dvec3(sv), glm::dvec3(0), 1e-6);
    }
    // W2V * (world of voxel (1,0,0)) == (1,0,0)
    {
        auto world = v2.voxelToWorld * glm::dvec4(1, 0, 0, 1);
        auto sv = v2.worldToVoxel * world;
        CHECK_VEC3("W2V * (world of voxel (1,0,0)) == (1,0,0)", glm::dvec3(sv), glm::dvec3(1,0,0), 1e-6);
    }
    // W2V * (world of voxel (0,1,0)) == (0,1,0)
    {
        auto world = v2.voxelToWorld * glm::dvec4(0, 1, 0, 1);
        auto sv = v2.worldToVoxel * world;
        CHECK_VEC3("W2V * (world of voxel (0,1,0)) == (0,1,0)", glm::dvec3(sv), glm::dvec3(0,1,0), 1e-6);
    }
    // W2V * (world of voxel (0,0,1)) == (0,0,1)
    {
        auto world = v2.voxelToWorld * glm::dvec4(0, 0, 1, 1);
        auto sv = v2.worldToVoxel * world;
        CHECK_VEC3("W2V * (world of voxel (0,0,1)) == (0,0,1)", glm::dvec3(sv), glm::dvec3(0,0,1), 1e-6);
    }
    // Round-trip: a mid-volume voxel goes out and comes back exactly
    {
        glm::dvec4 voxIn(24, 17, 31, 1.0);
        auto world = v2.voxelToWorld * voxIn;
        auto voxOut = v2.worldToVoxel * world;
        CHECK_VEC3("round-trip voxel (24,17,31)", glm::dvec3(voxOut), glm::dvec3(24,17,31), 1e-9);
    }
}

// --------------------------------------------------------------------------
// H3 — end-to-end chain: sq1-voxel → world → sq2_tr-voxel
//      computed analytically (bypassing v2.worldToVoxel) vs matrix result
// --------------------------------------------------------------------------
static void testH3(const Volume& v1, const Volume& v2)
{
    std::cerr << "\n=== H3: sq1 voxel -> world -> sq2_tr voxel chain (axial, centerZ=50) ===\n";
    std::cerr << "    Expected values computed independently from raw metadata (Python/NumPy)\n";

    // Build analytic V2W and W2V from raw metadata, independently of Volume's matrices.
    // This is the ground-truth reference that H1/H2 already verified.
    // If H3 fails but H1+H2 pass, the bug is in renderOverlaySlice's loop logic.

    // Analytic voxelToWorld for sq2_tr (column-major):
    //   col i = dirCos[i] * step[i]
    //   col 3 = dirCos3 * start  (translation must be rotated)
    glm::dmat3 analytic_dc3(v2.dirCos[0][0], v2.dirCos[0][1], v2.dirCos[0][2],
                            v2.dirCos[1][0], v2.dirCos[1][1], v2.dirCos[1][2],
                            v2.dirCos[2][0], v2.dirCos[2][1], v2.dirCos[2][2]);
    glm::dvec3 analytic_trans = analytic_dc3 * glm::dvec3(v2.start.x, v2.start.y, v2.start.z);

    glm::dmat4 analytic_V2W(1.0);
    for (int i = 0; i < 3; ++i)
    {
        analytic_V2W[i][0] = v2.dirCos[i][0] * v2.step[i];
        analytic_V2W[i][1] = v2.dirCos[i][1] * v2.step[i];
        analytic_V2W[i][2] = v2.dirCos[i][2] * v2.step[i];
    }
    analytic_V2W[3] = glm::dvec4(analytic_trans, 1.0);
    glm::dmat4 analytic_W2V = glm::inverse(analytic_V2W);

    // Chain test cases:  {sq1_px, sq1_py, centerZ,  expected_tgt_x, expected_tgt_y, expected_tgt_z}
    // Pre-computed by Python using the exact same formula:
    //   world = sq1.start + [px,py,50]*sq1.step   (sq1 has identity dirCos)
    //   tgt   = analytic_W2V * world
    // Values below are rounded to nearest integer for the nearest-neighbor check.
    struct Case
    {
        int px, py;                           // sq1 pixel (axial, centerZ=50)
        double exp_tx, exp_ty, exp_tz;        // expected fractional sq2_tr voxel
    };

    // Expected values recomputed after translation fix (dirCos3 * start):
    const Case cases[] = {
        { 50, 50,  27.748,  24.145,  27.054 },
        { 25, 25,  11.970,  16.674,  29.836 },
        { 10, 10,   2.503,  12.191,  31.505 },
        {  0, 50,   4.612,  32.695,  31.133 },
        { 50, 10,  21.011,   5.351,  28.242 },
        { 20, 30,  10.498,  19.878,  30.096 },
    };

    for (const auto& c : cases)
    {
        // sq1 voxel -> world (sq1 has identity dirCos, step=2, start=-100)
        glm::dvec4 w1 = v1.voxelToWorld * glm::dvec4(c.px, c.py, 50, 1);

        // world -> sq2_tr voxel using the matrix from Volume
        glm::dvec4 tv_matrix = v2.worldToVoxel * w1;

        // world -> sq2_tr voxel using analytic matrix
        glm::dvec4 tv_analytic = analytic_W2V * w1;

        glm::dvec3 expected(c.exp_tx, c.exp_ty, c.exp_tz);

        bool matrix_ok   = nearVec3(glm::dvec3(tv_matrix),   expected, 0.01);
        bool analytic_ok = nearVec3(glm::dvec3(tv_analytic), expected, 0.01);

        std::cerr << "  sq1(" << c.px << "," << c.py << ",50):\n";
        std::cerr << "    matrix   tgt = ("
                  << tv_matrix.x   << "," << tv_matrix.y   << "," << tv_matrix.z   << ")  "
                  << (matrix_ok   ? "PASS" : "FAIL") << "\n";
        std::cerr << "    analytic tgt = ("
                  << tv_analytic.x << "," << tv_analytic.y << "," << tv_analytic.z << ")  "
                  << (analytic_ok ? "PASS" : "FAIL") << "\n";

        if (matrix_ok)   ++g_pass; else ++g_fail;
        if (analytic_ok) ++g_pass; else ++g_fail;

        // Also verify the two approaches agree with each other
        bool agree = nearVec3(glm::dvec3(tv_matrix), glm::dvec3(tv_analytic), 1e-6);
        if (!agree)
            std::cerr << "    *** DIVERGENCE between matrix and analytic paths! ***\n";
    }
}

// --------------------------------------------------------------------------
// Main
// --------------------------------------------------------------------------
int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cerr << "Usage: test_coord_sq2tr <sq1.mnc> <sq2_tr.mnc>\n";
        return 1;
    }

    Volume v1, v2;
    try { v1.load(argv[1]); }
    catch (const std::exception& e) { std::cerr << "Cannot load sq1: " << e.what() << "\n"; return 1; }
    try { v2.load(argv[2]); }
    catch (const std::exception& e) { std::cerr << "Cannot load sq2_tr: " << e.what() << "\n"; return 1; }

    std::cerr << std::fixed << std::setprecision(6);
    std::cerr << "sq1:    " << v1.dimensions.x << "x" << v1.dimensions.y << "x" << v1.dimensions.z
              << "  step=" << v1.step.x << "  start=(" << v1.start.x << "," << v1.start.y << "," << v1.start.z << ")\n";
    std::cerr << "sq2_tr: " << v2.dimensions.x << "x" << v2.dimensions.y << "x" << v2.dimensions.z
              << "  step=" << v2.step.x << "  start=(" << v2.start.x << "," << v2.start.y << "," << v2.start.z << ")\n";

    testH1(v2);
    testH2(v2);
    testH3(v1, v2);

    std::cerr << "\nResult: " << g_pass << " passed, " << g_fail << " failed.\n";
    return g_fail > 0 ? 1 : 0;
}
