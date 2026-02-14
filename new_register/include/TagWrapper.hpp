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
 * It loads a .tag file and stores the tag points as glm::vec3 objects and
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
    bool load(const std::string& path);

    /** Get the loaded points.
     * @return Vector of glm::vec3 representing tag coordinates.
     */
    const std::vector<glm::vec3>& points() const { return points_; }

    /** Get the loaded labels (may be empty strings if none).
     * @return Vector of label strings, same size as points().
     */
    const std::vector<std::string>& labels() const { return labels_; }

    /** Number of volumes indicated in the tag file (usually 1).
     */
    int volumeCount() const { return n_volumes_; }

private:
    // Non‑copyable
    TagWrapper(const TagWrapper&) = delete;
    TagWrapper& operator=(const TagWrapper&) = delete;

    // Helper to free resources
    void clear();

    minc2_tags* tags_;                // Raw C structure allocated by minc2_simple
    int n_volumes_;                   // Number of volumes stored in the tag file
    std::vector<glm::vec3> points_;   // Tag coordinates (first volume)
    std::vector<std::string> labels_; // Optional labels (may be empty strings)
};

#endif // TAG_WRAPPER_HPP
