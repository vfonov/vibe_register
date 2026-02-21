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
    points2_.clear();
    labels_.clear();
    n_volumes_ = 0;
}

TagWrapper::TagWrapper(TagWrapper&& other) noexcept
    : tags_(other.tags_),
      n_volumes_(other.n_volumes_),
      points_(std::move(other.points_)),
      points2_(std::move(other.points2_)),
      labels_(std::move(other.labels_))
{
    other.tags_ = nullptr;
    other.n_volumes_ = 0;
}

TagWrapper& TagWrapper::operator=(TagWrapper&& other) noexcept {
    if (this != &other) {
        if (tags_) {
            minc2_tags_free(tags_);
        }
        tags_ = other.tags_;
        n_volumes_ = other.n_volumes_;
        points_ = std::move(other.points_);
        points2_ = std::move(other.points2_);
        labels_ = std::move(other.labels_);
        
        other.tags_ = nullptr;
        other.n_volumes_ = 0;
    }
    return *this;
}

TagWrapper::TagWrapper(const TagWrapper& other)
    : tags_(nullptr),
      n_volumes_(other.n_volumes_),
      points_(other.points_),
      points2_(other.points2_),
      labels_(other.labels_)
{
    // We only copy the high-level points/labels, not the raw C minc2_tags
    // handle.  The raw handle is only needed for save() and is rebuilt on
    // demand by that method.
}

TagWrapper& TagWrapper::operator=(const TagWrapper& other) {
    if (this != &other) {
        if (tags_) {
            minc2_tags_free(tags_);
            tags_ = nullptr;
        }
        n_volumes_ = other.n_volumes_;
        points_ = other.points_;
        points2_ = other.points2_;
        labels_ = other.labels_;
    }
    return *this;
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
        points_.push_back(glm::dvec3(vol1[i * 3 + 0], vol1[i * 3 + 1], vol1[i * 3 + 2]));
    }

    // Copy points for the second volume if present (two-volume tag file).
    if (n_volumes_ >= 2 && tags_->tags_volume2) {
        points2_.reserve(count);
        const double* vol2 = tags_->tags_volume2;
        for (int i = 0; i < count; ++i) {
            points2_.push_back(glm::dvec3(vol2[i * 3 + 0], vol2[i * 3 + 1], vol2[i * 3 + 2]));
        }
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

void TagWrapper::save(const std::string& path) {
    if (points_.empty()) {
        throw std::runtime_error("No tags to save");
    }

    if (!tags_) {
        tags_ = minc2_tags_allocate0();
        if (!tags_) {
            throw std::runtime_error("Failed to allocate tag structure");
        }
    }

    int count = static_cast<int>(points_.size());
    int nVols = points2_.empty() ? 1 : 2;

    if (minc2_tags_init(tags_, count, nVols, 0, 0, 0, 1) != MINC2_SUCCESS) {
        throw std::runtime_error("Failed to initialize tag structure");
    }

    // Write volume 1 points
    for (int i = 0; i < count; ++i) {
        tags_->tags_volume1[i * 3 + 0] = points_[i].x;
        tags_->tags_volume1[i * 3 + 1] = points_[i].y;
        tags_->tags_volume1[i * 3 + 2] = points_[i].z;
    }

    // Write volume 2 points if present
    if (nVols == 2 && tags_->tags_volume2) {
        for (int i = 0; i < count; ++i) {
            // If points2_ is shorter than points_, pad with zeros
            if (i < static_cast<int>(points2_.size())) {
                tags_->tags_volume2[i * 3 + 0] = points2_[i].x;
                tags_->tags_volume2[i * 3 + 1] = points2_[i].y;
                tags_->tags_volume2[i * 3 + 2] = points2_[i].z;
            } else {
                tags_->tags_volume2[i * 3 + 0] = 0.0;
                tags_->tags_volume2[i * 3 + 1] = 0.0;
                tags_->tags_volume2[i * 3 + 2] = 0.0;
            }
        }
    }

    if (!labels_.empty() && labels_.size() == static_cast<size_t>(count)) {
        for (int i = 0; i < count; ++i) {
            if (!labels_[i].empty()) {
                tags_->labels[i] = strdup(labels_[i].c_str());
            }
        }
    }

    if (minc2_tags_save(tags_, path.c_str()) != MINC2_SUCCESS) {
        throw std::runtime_error("Failed to save tag file: " + path);
    }
}

void TagWrapper::setPoints(const std::vector<glm::dvec3>& points) {
    points_.clear();
    points_.reserve(points.size());
    for (const auto& p : points) {
        points_.push_back(p);
    }
}

void TagWrapper::setPoints2(const std::vector<glm::dvec3>& points) {
    points2_.clear();
    points2_.reserve(points.size());
    for (const auto& p : points) {
        points2_.push_back(p);
    }
}

void TagWrapper::setLabels(const std::vector<std::string>& labels) {
    labels_ = labels;
}

void TagWrapper::removeTag(int index) {
    if (index < 0 || index >= static_cast<int>(points_.size()))
        return;
    points_.erase(points_.begin() + index);
    if (index < static_cast<int>(points2_.size()))
        points2_.erase(points2_.begin() + index);
    if (index < static_cast<int>(labels_.size()))
        labels_.erase(labels_.begin() + index);
}

void TagWrapper::updateTag(int index, const glm::dvec3& newPos, const std::string& label) {
    if (index < 0 || index >= static_cast<int>(points_.size()))
        return;
    points_[index] = newPos;
    if (!label.empty() && index < static_cast<int>(labels_.size()))
        labels_[index] = label;
}
