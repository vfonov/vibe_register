#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

/// Per-volume view state that gets persisted.
struct VolumeConfig
{
    std::string path;                            // Volume file path
    std::string colourMap = "GrayScale";         // Colour map name
    std::optional<double> valueMin;              // Value range min (nullopt = auto)
    std::optional<double> valueMax;              // Value range max (nullopt = auto)
    std::array<int, 3> sliceIndices = {-1, -1, -1};  // -1 means "use midpoint"
    std::array<double, 3> zoom = {1.0, 1.0, 1.0};
    std::array<double, 3> panU = {0.5, 0.5, 0.5};
    std::array<double, 3> panV = {0.5, 0.5, 0.5};
};

/// Global application defaults.
struct GlobalConfig
{
    std::string defaultColourMap = "GrayScale";  // Default colour map for new volumes
    std::optional<int> windowWidth;              // Window width (nullopt = auto-size)
    std::optional<int> windowHeight;             // Window height (nullopt = auto-size)
    bool syncCursors = false;
    bool syncZoom = false;
    bool syncPan = false;
    bool tagListVisible = false;
};

/// Top-level config structure.
struct AppConfig
{
    GlobalConfig global;
    std::vector<VolumeConfig> volumes;
};

/// Return the global config file path: $HOME/.config/new_register/config.json
std::string globalConfigPath();

/// Load a config from a JSON file.  Returns a default AppConfig if the file
/// does not exist.  Throws std::runtime_error on parse errors.
AppConfig loadConfig(const std::string& path);

/// Save a config to a JSON file.  Creates parent directories as needed.
/// Throws std::runtime_error on I/O errors.
void saveConfig(const AppConfig& config, const std::string& path);

/// Merge a local config on top of a global config.
/// Local values override global values where present.
/// Local volume entries override global volume entries matched by path;
/// unmatched local volumes are appended.
AppConfig mergeConfigs(const AppConfig& global, const AppConfig& local);
