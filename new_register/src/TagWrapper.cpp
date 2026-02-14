// TagWrapper.cpp - implementation of TagWrapper using minc2_simple API

#include "TagWrapper.hpp"

// Include the minc2_simple header that declares the C API.
extern "C" {
#include "minc2-simple.h" // path relative to include directories set in CMake
}

#include <cstring> // for strdup if needed
#include <stdexcept> // for exceptions

TagWrapper::TagWrapper() : tags_(nullptr), n_volumes_(0) {}

TagWrapper::~TagWrapper() {
    clear();
}

void TagWrapper::clear() {
    if (tags_) {
        // minc2_tags_free frees the structure and any allocated strings/arrays
        minc2_tags_free(tags_);
        tags_ = nullptr;
    }
    points_.clear();
    labels_.clear();
    n_volumes_ = 0;
}

void TagWrapper::load(const std::string& path) {
    clear();

    // Allocate the tags structure (zeroed)
    tags_ = minc2_tags_allocate0();
    if (!tags_) {
        throw std::runtime_error("Failed to allocate tag structure");
    }

    // Load the tag file using the C API. It returns VIO_OK on success.
    if (minc2_tags_load(tags_, path.c_str()) != MINC2_SUCCESS) {
        clear();
        throw std::runtime_error("Failed to load tag file");
    }

    // Store volume count
    n_volumes_ = tags_->n_volumes;

    // Copy points for the first volume (volume index 0).
    // The C API stores points as three separate double arrays per volume.
    // We will read tags_->tags_volume1 (double*) which contains n_tag_points * 3 values.
    int count = tags_->n_tag_points;
    points_.reserve(count);
    const double* vol1 = tags_->tags_volume1;
    for (int i = 0; i < count; ++i) {
        glm::vec3 p;
        p.x = static_cast<float>(vol1[i * 3 + 0]);
        p.y = static_cast<float>(vol1[i * 3 + 1]);
        p.z = static_cast<float>(vol1[i * 3 + 2]);
        points_.push_back(p);
    }

    // Copy labels if present. The C struct has a char** labels pointer.
    // It may be NULL or individual entries may be NULL. We store empty strings for missing labels.
    labels_.reserve(count);
    if (tags_->labels) {
        for (int i = 0; i < count; ++i) {
            const char* lbl = tags_->labels[i];
            labels_.emplace_back(lbl ? lbl : "");
        }
    } else {
        // No label array – fill with empty strings
        for (int i = 0; i < count; ++i) {
            labels_.emplace_back("");
        }
    }

    return;
}
