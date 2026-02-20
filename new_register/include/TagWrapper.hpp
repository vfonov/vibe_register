// TagWrapper.hpp - C++ wrapper for minc2_simple tag handling using glm vectors
#ifndef TAG_WRAPPER_HPP
#define TAG_WRAPPER_HPP

#include <string>
#include <vector>
#include <glm/vec3.hpp>

// Forward declaration of minc2_tags from minc2-simple
struct minc2_tags;

/**
 * TagWrapper
 * ---------
 * Provides a simple RAII C++ interface around the C minc2_simple tag API.
 * It loads a .tag file and stores the tag points as glm::dvec3 objects and
 * optional string labels. The class manages the underlying `minc2_tags`
 * structure and frees it automatically.
 */
class TagWrapper {
public:
    TagWrapper();
    ~TagWrapper();

    /**
     * Load a .tag file.
     * @param path Path to the tag file.
     * @return true on success, false on failure.
     */
    void load(const std::string& path); // throws std::runtime_error on failure

    /** Get the loaded points.
     * @return Vector of glm::dvec3 representing tag coordinates.
     */
    const std::vector<glm::dvec3>& points() const { return points_; }

    /** Get the loaded labels (may be empty strings if none).
     * @return Vector of label strings, same size as points().
     */
    const std::vector<std::string>& labels() const { return labels_; }

    /** Number of volumes indicated in the tag file (usually 1).
     */
    int volumeCount() const { return n_volumes_; }

    /** Save tags to a .tag file.
     * @param path Path to the tag file to save.
     * @throws std::runtime_error on failure.
     */
    void save(const std::string& path);

    /** Set tag points (for creating new tag files).
     * @param points Vector of glm::dvec3 world coordinates.
     */
    void setPoints(const std::vector<glm::dvec3>& points);

    /** Set tag labels.
     * @param labels Vector of label strings (must match points size).
     */
    void setLabels(const std::vector<std::string>& labels);

    /** Remove a tag at given index.
     * @param index Index of tag to remove.
     */
    void removeTag(int index);

    /** Update tag position and label.
     * @param index Index of tag to update.
     * @param newPos New world coordinates.
     * @param label New label (empty string to keep existing).
     */
    void updateTag(int index, const glm::dvec3& newPos, const std::string& label);

    /** Check if any tags are loaded.
     * @return true if tags are not empty.
     */
    bool hasTags() const { return !points_.empty(); }

    /** Get number of tag points.
     * @return Number of tag points.
     */
    int tagCount() const { return static_cast<int>(points_.size()); }

    void clear();

    TagWrapper(TagWrapper&& other) noexcept;
    TagWrapper& operator=(TagWrapper&& other) noexcept;

    /// Copy constructor — copies points and labels but not the raw C handle.
    TagWrapper(const TagWrapper& other);
    TagWrapper& operator=(const TagWrapper& other);

private:
    minc2_tags* tags_;                // Raw C structure allocated by minc2_simple
    int n_volumes_;                   // Number of volumes stored in the tag file
    std::vector<glm::dvec3> points_;   // Tag coordinates (first volume)
    std::vector<std::string> labels_; // Optional labels (may be empty strings)
};

#endif // TAG_WRAPPER_HPP
