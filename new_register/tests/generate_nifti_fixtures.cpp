/// generate_nifti_fixtures.cpp — writes the small NIfTI fixtures used by
/// test_nifti_pair (run manually, not a ctest).
///
/// Usage: generate_nifti_fixtures <out_dir>
///
/// Writes two volumes of the same synthetic "head" on the same world grid
/// (24 x 30 x 20 voxels, 2 mm, world corner at -24, -30, -20):
///   pair_t1.hdr + pair_t1.img  two-file NIfTI-1, int16, stored right-to-left
///                              (negative x step, as old ANALYZE/SPM files are)
///   pair_seg.nii.gz            single-file NIfTI-1, uint8 mask, left-to-right
/// The ellipsoid is off-centre on every axis, so a wrong flip or axis swap in
/// the loader moves it off the mask.

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

#include <nifti1_io.h>

namespace
{

constexpr int kNx = 24;
constexpr int kNy = 30;
constexpr int kNz = 20;
constexpr double kStep = 2.0;
constexpr double kStart[3] = {-24.0, -30.0, -20.0};

/// Intensity of the synthetic head at a world point: 0 outside the ellipsoid,
/// 200..1000 inside, brighter towards its centre.
int headValue(double wx, double wy, double wz)
{
    const double dx = (wx - 4.0) / 14.0;
    const double dy = (wy + 6.0) / 20.0;
    const double dz = (wz - 2.0) / 12.0;
    const double r2 = dx * dx + dy * dy + dz * dz;
    if (r2 >= 1.0)
        return 0;
    return static_cast<int>(std::lround(1000.0 - 800.0 * r2));
}

/// Allocate a 3-D image whose file voxel (i, j, k) sits at world
/// (x0 + sx*i, y0 + 2*j, z0 + 2*k), with matching sform and pixdim.
nifti_image* makeImage(int datatype, double sx, double x0)
{
    int dims[8] = {3, kNx, kNy, kNz, 1, 1, 1, 1};
    nifti_image* nim = nifti_make_new_nim(dims, datatype, 1);
    if (!nim)
        return nullptr;

    const double affine[3][4] = {
        {sx, 0.0, 0.0, x0}, {0.0, kStep, 0.0, kStart[1]}, {0.0, 0.0, kStep, kStart[2]}};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            nim->sto_xyz.m[r][c] = (r < 3) ? static_cast<float>(affine[r][c])
                                           : (c == 3 ? 1.0f : 0.0f);
    nim->sto_ijk = nifti_mat44_inverse(nim->sto_xyz);
    nim->sform_code = NIFTI_XFORM_ALIGNED_ANAT;
    nim->qform_code = NIFTI_XFORM_UNKNOWN;
    nim->dx = nim->pixdim[1] = static_cast<float>(kStep);
    nim->dy = nim->pixdim[2] = static_cast<float>(kStep);
    nim->dz = nim->pixdim[3] = static_cast<float>(kStep);
    nim->xyz_units = NIFTI_UNITS_MM;
    return nim;
}

template <typename T, typename F>
void fill(nifti_image* nim, double sx, double x0, F valueOf)
{
    T* dst = static_cast<T*>(nim->data);
    for (int k = 0; k < kNz; ++k)
        for (int j = 0; j < kNy; ++j)
            for (int i = 0; i < kNx; ++i)
            {
                const int head = headValue(x0 + sx * i, kStart[1] + kStep * j, kStart[2] + kStep * k);
                dst[(static_cast<size_t>(k) * kNy + j) * kNx + i] = static_cast<T>(valueOf(head));
            }
}

bool write(nifti_image* nim, const std::string& path, int niftiType)
{
    nim->nifti_type = niftiType;
    const bool ok = nifti_set_filenames(nim, path.c_str(), 0, 1) == 0;
    if (ok)
        nifti_image_write(nim);
    nifti_image_free(nim);
    return ok;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <out_dir>\n";
        return 1;
    }
    const std::string outDir = argv[1];

    // T1: x stored right-to-left, so file voxel i = 0 is the +x end.
    const double t1Step = -kStep;
    const double t1Start = kStart[0] + kStep * (kNx - 1);
    nifti_image* t1 = makeImage(DT_INT16, t1Step, t1Start);
    if (!t1)
        return 1;
    fill<int16_t>(t1, t1Step, t1Start, [](int head) { return head; });
    if (!write(t1, outDir + "/pair_t1.hdr", NIFTI_FTYPE_NIFTI1_2))
        return 1;

    nifti_image* seg = makeImage(DT_UINT8, kStep, kStart[0]);
    if (!seg)
        return 1;
    fill<uint8_t>(seg, kStep, kStart[0], [](int head) { return head > 0 ? 1 : 0; });
    if (!write(seg, outDir + "/pair_seg.nii.gz", NIFTI_FTYPE_NIFTI1_1))
        return 1;

    std::cout << "Wrote pair_t1.hdr, pair_t1.img and pair_seg.nii.gz to " << outDir << "\n";
    return 0;
}
