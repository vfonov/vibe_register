/// test_nifti_datatypes.cpp — NIfTI loader correctness on synthetic images.
///
/// Every fixture is written at run time with the vendored niftilib into a
/// temporary directory, so the tests need no data files.
///
/// Covers: signed/unsigned/64-bit integer datatypes, unsupported datatypes,
/// scl_slope/scl_inter, non-finite voxels, constant images, oblique and
/// permuted/flipped affines, 4-D images and file-name detection.

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <nifti1_io.h>

#include "NiftiVolume.h"
#include "Volume.h"

namespace
{

namespace fs = std::filesystem;

/// Fresh per-test scratch directory, removed on destruction.
class TempDir
{
public:
    TempDir()
    {
        std::random_device rd;
        path_ = fs::temp_directory_path() /
                ("nr_nifti_test_" + std::to_string(rd()) + std::to_string(rd()));
        fs::create_directories(path_);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    std::string file(const std::string& name) const { return (path_ / name).string(); }

private:
    fs::path path_;
};

/// A 4×4 affine (row-major, as in nifti mat44) plus scaling, for writeNifti().
struct NiftiSpec
{
    std::array<int, 4> dims{3, 3, 3, 1};  // nx, ny, nz, nt
    int datatype = DT_FLOAT32;
    double affine[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
    float sclSlope = 0.0f;
    float sclInter = 0.0f;
    std::array<float, 3> pixdim{1.0f, 1.0f, 1.0f};
    bool analyze75 = false;  // write a plain ANALYZE 7.5 pair (no NIfTI magic, no affine)
};

bool endsWith(const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/// Write a NIfTI image whose voxel i (file order) holds values[i] cast to T.
template <typename T>
void writeNifti(const std::string& path, const NiftiSpec& spec, const std::vector<double>& values)
{
    const int ndim = spec.dims[3] > 1 ? 4 : 3;
    int dims[8] = {ndim, spec.dims[0], spec.dims[1], spec.dims[2], spec.dims[3], 1, 1, 1};
    nifti_image* nim = nifti_make_new_nim(dims, spec.datatype, 1);
    REQUIRE(nim != nullptr);
    REQUIRE(nim->nvox == values.size());

    T* dst = static_cast<T*>(nim->data);
    for (size_t i = 0; i < values.size(); ++i)
        dst[i] = static_cast<T>(values[i]);

    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            nim->sto_xyz.m[r][c] = (r < 3) ? static_cast<float>(spec.affine[r][c])
                                           : (c == 3 ? 1.0f : 0.0f);
    nim->sform_code = NIFTI_XFORM_SCANNER_ANAT;
    nim->qform_code = NIFTI_XFORM_UNKNOWN;
    nim->scl_slope = spec.sclSlope;
    nim->scl_inter = spec.sclInter;

    nim->dx = nim->pixdim[1] = spec.pixdim[0];
    nim->dy = nim->pixdim[2] = spec.pixdim[1];
    nim->dz = nim->pixdim[3] = spec.pixdim[2];

    const bool gzipped = endsWith(path, ".gz");
    const bool pair = endsWith(path, ".hdr") || endsWith(path, ".hdr.gz");
    nim->nifti_type = spec.analyze75 ? NIFTI_FTYPE_ANALYZE
                      : pair         ? NIFTI_FTYPE_NIFTI1_2
                                     : NIFTI_FTYPE_NIFTI1_1;
    REQUIRE(nifti_set_filenames(nim, path.c_str(), 0, 1) == 0);
    REQUIRE((nifti_is_gzfile(nim->iname) != 0) == gzipped);
    nifti_image_write(nim);
    nifti_image_free(nim);
}

/// Value stored at file voxel (i, j, k) of volume `frame`: unique per voxel.
double voxelValue(int i, int j, int k, int frame = 0)
{
    return 1.0 + i + 10.0 * j + 100.0 * k + 1000.0 * frame;
}

std::vector<double> rampValues(const std::array<int, 4>& d)
{
    std::vector<double> v;
    for (int t = 0; t < d[3]; ++t)
        for (int k = 0; k < d[2]; ++k)
            for (int j = 0; j < d[1]; ++j)
                for (int i = 0; i < d[0]; ++i)
                    v.push_back(voxelValue(i, j, k, t));
    return v;
}

/// For every file voxel, the world point from the NIfTI affine must map back
/// (through vol.worldToVoxel) to a voxel holding that file voxel's value.
void requireGeometryMatchesAffine(const Volume& vol, const NiftiSpec& spec)
{
    for (int k = 0; k < spec.dims[2]; ++k)
        for (int j = 0; j < spec.dims[1]; ++j)
            for (int i = 0; i < spec.dims[0]; ++i)
            {
                glm::dvec4 world(0.0, 0.0, 0.0, 1.0);
                for (int r = 0; r < 3; ++r)
                    world[r] = spec.affine[r][0] * i + spec.affine[r][1] * j +
                               spec.affine[r][2] * k + spec.affine[r][3];
                glm::dvec4 vox = vol.worldToVoxel * world;
                int x = static_cast<int>(std::lround(vox.x));
                int y = static_cast<int>(std::lround(vox.y));
                int z = static_cast<int>(std::lround(vox.z));
                INFO("file voxel " << i << "," << j << "," << k << " -> " << x << "," << y << "," << z);
                REQUIRE(std::fabs(vox.x - x) < 1e-3);
                REQUIRE(std::fabs(vox.y - y) < 1e-3);
                REQUIRE(std::fabs(vox.z - z) < 1e-3);
                REQUIRE(vol.get(x, y, z) == static_cast<float>(voxelValue(i, j, k)));
            }
}

}  // namespace

TEST_CASE("signed integer NIfTI datatypes keep negative values", "[nifti][datatype]")
{
    const int dt = GENERATE(DT_INT8, DT_INT16, DT_INT32, DT_INT64);
    INFO("datatype " << dt);
    TempDir tmp;
    const std::string path = tmp.file("signed.nii");
    NiftiSpec spec;
    spec.dims = {3, 1, 1, 1};
    spec.datatype = dt;
    const std::vector<double> values{-100.0, 0.0, 100.0};
    switch (dt)
    {
    case DT_INT8:  writeNifti<int8_t>(path, spec, values);  break;
    case DT_INT16: writeNifti<int16_t>(path, spec, values); break;
    case DT_INT32: writeNifti<int32_t>(path, spec, values); break;
    case DT_INT64: writeNifti<int64_t>(path, spec, values); break;
    }

    Volume vol;
    vol.load(path);
    REQUIRE(vol.data.size() == 3);
    CHECK(vol.get(0, 0, 0) == -100.0f);
    CHECK(vol.get(1, 0, 0) == 0.0f);
    CHECK(vol.get(2, 0, 0) == 100.0f);
    CHECK(vol.min_value == -100.0f);
    CHECK(vol.max_value == 100.0f);
}

TEST_CASE("unsigned integer NIfTI datatypes keep values above the signed range", "[nifti][datatype]")
{
    const int dt = GENERATE(DT_UINT8, DT_UINT16, DT_UINT32, DT_UINT64);
    INFO("datatype " << dt);
    TempDir tmp;
    const std::string path = tmp.file("unsigned.nii");
    NiftiSpec spec;
    spec.dims = {2, 1, 1, 1};
    spec.datatype = dt;
    double high = 0.0;
    switch (dt)
    {
    case DT_UINT8:  high = 250.0;        writeNifti<uint8_t>(path, spec, {0.0, high});  break;
    case DT_UINT16: high = 60000.0;      writeNifti<uint16_t>(path, spec, {0.0, high}); break;
    case DT_UINT32: high = 3000000000.0; writeNifti<uint32_t>(path, spec, {0.0, high}); break;
    case DT_UINT64: high = 3000000000.0; writeNifti<uint64_t>(path, spec, {0.0, high}); break;
    }

    Volume vol;
    vol.load(path);
    CHECK(vol.get(0, 0, 0) == 0.0f);
    CHECK(vol.get(1, 0, 0) == static_cast<float>(high));
    CHECK(vol.max_value == static_cast<float>(high));
}

TEST_CASE("unsupported NIfTI datatype reports its code", "[nifti][datatype]")
{
    TempDir tmp;
    const std::string path = tmp.file("complex.nii");
    NiftiSpec spec;
    spec.dims = {2, 1, 1, 1};
    spec.datatype = DT_COMPLEX64;
    // complex64 = two float32 per voxel; write the real parts only.
    writeNifti<float>(path, spec, {1.0, 0.0});

    Volume vol;
    REQUIRE_THROWS_WITH(vol.load(path),
                        Catch::Matchers::ContainsSubstring("Unsupported NIfTI datatype: 32"));
}

TEST_CASE("scl_slope and scl_inter are applied before the value range", "[nifti][scaling]")
{
    TempDir tmp;
    const std::string path = tmp.file("scaled.nii");
    NiftiSpec spec;
    spec.dims = {6, 1, 1, 1};
    spec.datatype = DT_UINT8;
    spec.sclSlope = -2.0f;
    spec.sclInter = 10.0f;
    writeNifti<uint8_t>(path, spec, {0, 1, 2, 3, 4, 5});

    Volume vol;
    vol.load(path);
    CHECK(vol.get(0, 0, 0) == 10.0f);
    CHECK(vol.get(5, 0, 0) == 0.0f);
    CHECK(vol.min_value == 0.0f);
    CHECK(vol.max_value == 10.0f);
}

TEST_CASE("non-finite NIfTI float voxels are zeroed by niftilib", "[nifti][range]")
{
    // nifti_read_buffer() (third_party/niftilib/nifti1_io.c) replaces NaN/Inf in
    // float images with 0 before the loader sees them. Unlike MINC, where
    // non-finite voxels survive and render as under/over colour.
    TempDir tmp;
    const std::string path = tmp.file("nonfinite.nii");
    NiftiSpec spec;
    spec.dims = {5, 1, 1, 1};
    spec.datatype = DT_FLOAT32;
    const double inf = std::numeric_limits<double>::infinity();
    writeNifti<float>(path, spec, {std::nan(""), -inf, 2.0, 8.0, inf});

    Volume vol;
    vol.load(path);
    CHECK(vol.get(0, 0, 0) == 0.0f);
    CHECK(vol.get(1, 0, 0) == 0.0f);
    CHECK(vol.get(4, 0, 0) == 0.0f);
    CHECK(vol.min_value == 0.0f);
    CHECK(vol.max_value == 8.0f);
}

TEST_CASE("constant NIfTI image falls back to the same range as MINC", "[nifti][range]")
{
    TempDir tmp;
    const std::string path = tmp.file("constant.nii");
    NiftiSpec spec;
    spec.datatype = DT_FLOAT32;
    writeNifti<float>(path, spec, std::vector<double>(27, 7.0));

    Volume vol;
    vol.load(path);
    // computeValueRange() in Volume.cpp: a degenerate range becomes 0..1.
    CHECK(vol.min_value == 0.0f);
    CHECK(vol.max_value == 1.0f);
}

TEST_CASE("oblique NIfTI affine with tied columns loads with correct geometry", "[nifti][geometry]")
{
    TempDir tmp;
    const std::string path = tmp.file("oblique.nii");
    NiftiSpec spec;
    spec.dims = {3, 4, 5, 1};
    const double c = std::sqrt(0.5);
    // 45° in-plane rotation: columns 0 and 1 both have |x| == |y|.
    const double affine[3][4] = {{c, -c, 0, -3}, {c, c, 0, 5}, {0, 0, 2, 7}};
    std::copy(&affine[0][0], &affine[0][0] + 12, &spec.affine[0][0]);
    writeNifti<float>(path, spec, rampValues(spec.dims));

    Volume vol;
    REQUIRE_NOTHROW(vol.load(path));
    requireGeometryMatchesAffine(vol, spec);
}

TEST_CASE("every NIfTI axis permutation and flip maps voxels to the right world point", "[nifti][geometry]")
{
    const std::array<std::array<int, 3>, 6> perms{{
        {0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}}};
    const auto perm = GENERATE_COPY(Catch::Generators::from_range(perms));
    const int flips = GENERATE(Catch::Generators::range(0, 8));
    INFO("perm " << perm[0] << perm[1] << perm[2] << " flips " << flips);

    TempDir tmp;
    const std::string path = tmp.file("perm.nii");
    NiftiSpec spec;
    spec.dims = {4, 5, 6, 1};
    const double spacing[3] = {1.0, 2.0, 3.0};
    for (int j = 0; j < 3; ++j)
    {
        for (int r = 0; r < 3; ++r)
            spec.affine[r][j] = 0.0;
        const double sign = (flips & (1 << j)) ? -1.0 : 1.0;
        spec.affine[perm[j]][j] = sign * spacing[j];
    }
    spec.affine[0][3] = -10.0;
    spec.affine[1][3] = 20.0;
    spec.affine[2][3] = 5.0;
    writeNifti<float>(path, spec, rampValues(spec.dims));

    Volume vol;
    vol.load(path);
    requireGeometryMatchesAffine(vol, spec);
}

TEST_CASE("4-D NIfTI keeps only the first frame", "[nifti][4d]")
{
    TempDir tmp;
    const std::string path = tmp.file("fourd.nii");
    NiftiSpec spec;
    spec.dims = {2, 3, 4, 3};
    writeNifti<float>(path, spec, rampValues(spec.dims));

    Volume vol;
    vol.load(path);
    REQUIRE(vol.data.size() == static_cast<size_t>(2 * 3 * 4));
    CHECK(vol.min_value == static_cast<float>(voxelValue(0, 0, 0)));
    CHECK(vol.max_value == static_cast<float>(voxelValue(1, 2, 3)));
}

TEST_CASE("NIfTI file names are detected case-insensitively, including .hdr/.img", "[nifti][filename]")
{
    CHECK(isNiftiFile("a.nii"));
    CHECK(isNiftiFile("a.nii.gz"));
    CHECK(isNiftiFile("A.NII"));
    CHECK(isNiftiFile("A.NII.GZ"));
    CHECK(isNiftiFile("a.hdr"));
    CHECK(isNiftiFile("a.img"));
    CHECK(isNiftiFile("A.HDR"));
    CHECK(isNiftiFile("a.hdr.gz"));
    CHECK(isNiftiFile("a.img.gz"));
    CHECK_FALSE(isNiftiFile("a.mnc"));
    CHECK_FALSE(isNiftiFile("a.mnc.gz"));
    CHECK_FALSE(isNiftiFile("nii"));
}

TEST_CASE("NIfTI .hdr/.img pair loads", "[nifti][filename]")
{
    TempDir tmp;
    const std::string path = tmp.file("pair.hdr");
    NiftiSpec spec;
    writeNifti<float>(path, spec, rampValues(spec.dims));
    REQUIRE(fs::exists(tmp.file("pair.img")));

    Volume vol;
    vol.load(path);
    CHECK(vol.get(2, 1, 0) == static_cast<float>(voxelValue(2, 1, 0)));
}

TEST_CASE("NIfTI pair loads the same through the .img name", "[nifti][pair]")
{
    TempDir tmp;
    NiftiSpec spec;
    spec.dims = {4, 5, 6, 1};
    writeNifti<float>(tmp.file("pair.hdr"), spec, rampValues(spec.dims));

    Volume viaHdr;
    viaHdr.load(tmp.file("pair.hdr"));
    Volume viaImg;
    viaImg.load(tmp.file("pair.img"));
    CHECK(viaImg.dimensions == viaHdr.dimensions);
    CHECK(viaImg.data == viaHdr.data);
}

TEST_CASE("NIfTI pair with a permuted, flipped affine is reoriented like a .nii", "[nifti][pair]")
{
    TempDir tmp;
    const std::string path = tmp.file("perm.hdr");
    NiftiSpec spec;
    spec.dims = {4, 5, 6, 1};
    // File axes (i, j, k) run along world (-y, z, x) with spacings 1, 2, 3.
    const double affine[3][4] = {{0, 0, 3, -10}, {-1, 0, 0, 20}, {0, 2, 0, 5}};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c)
            spec.affine[r][c] = affine[r][c];
    writeNifti<float>(path, spec, rampValues(spec.dims));

    Volume vol;
    vol.load(path);
    CHECK(vol.dimensions == glm::ivec3(6, 4, 5));
    requireGeometryMatchesAffine(vol, spec);
}

TEST_CASE("gzipped NIfTI pair loads through either name", "[nifti][pair]")
{
    TempDir tmp;
    NiftiSpec spec;
    writeNifti<float>(tmp.file("pair.hdr.gz"), spec, rampValues(spec.dims));
    REQUIRE(fs::exists(tmp.file("pair.img.gz")));

    const std::string name = GENERATE(std::string("pair.hdr.gz"), std::string("pair.img.gz"));
    INFO(name);
    Volume vol;
    vol.load(tmp.file(name));
    CHECK(vol.get(2, 1, 0) == static_cast<float>(voxelValue(2, 1, 0)));
}

TEST_CASE("NIfTI pair with a missing half names the missing file", "[nifti][pair]")
{
    TempDir tmp;
    NiftiSpec spec;
    writeNifti<float>(tmp.file("pair.hdr"), spec, rampValues(spec.dims));

    SECTION("image missing")
    {
        fs::remove(tmp.file("pair.img"));
        Volume vol;
        REQUIRE_THROWS_WITH(vol.load(tmp.file("pair.hdr")),
                            Catch::Matchers::ContainsSubstring("pair.img"));
    }
    SECTION("header missing")
    {
        fs::remove(tmp.file("pair.hdr"));
        Volume vol;
        REQUIRE_THROWS_WITH(vol.load(tmp.file("pair.img")),
                            Catch::Matchers::ContainsSubstring("pair.hdr"));
    }
}

TEST_CASE("plain ANALYZE 7.5 pair loads with voxel sizes and a zero origin", "[nifti][pair]")
{
    TempDir tmp;
    const std::string path = tmp.file("old.hdr");
    NiftiSpec spec;
    spec.dims = {4, 5, 6, 1};
    spec.pixdim = {1.5f, 2.0f, 2.5f};
    spec.analyze75 = true;
    writeNifti<float>(path, spec, rampValues(spec.dims));

    Volume vol;
    vol.load(path);
    CHECK(vol.dimensions == glm::ivec3(4, 5, 6));
    CHECK(vol.step == glm::dvec3(1.5, 2.0, 2.5));
    CHECK(vol.start == glm::dvec3(0.0));
    CHECK(vol.dirCos == glm::dmat3(1.0));
    CHECK(vol.get(3, 2, 1) == static_cast<float>(voxelValue(3, 2, 1)));
}
