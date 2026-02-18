#include "AppConfig.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <glaze/glaze.hpp>

// ---- Glaze meta for custom JSON field names --------------------------------

template <>
struct glz::meta<VolumeConfig>
{
    using T = VolumeConfig;
    static constexpr auto value = object(
        "path",         &T::path,
        "colour_map",   &T::colourMap,
        "value_min",    &T::valueMin,
        "value_max",    &T::valueMax,
        "slice_indices",&T::sliceIndices,
        "zoom",         &T::zoom,
        "pan_u",        &T::panU,
        "pan_v",        &T::panV
    );
};

template <>
struct glz::meta<QCColumnConfig>
{
    using T = QCColumnConfig;
    static constexpr auto value = object(
        "colour_map", &T::colourMap,
        "value_min",  &T::valueMin,
        "value_max",  &T::valueMax
    );
};

template <>
struct glz::meta<GlobalConfig>
{
    using T = GlobalConfig;
    static constexpr auto value = object(
        "default_colour_map", &T::defaultColourMap,
        "window_width",       &T::windowWidth,
        "window_height",      &T::windowHeight,
        "sync_cursors",       &T::syncCursors,
        "sync_zoom",          &T::syncZoom,
        "sync_pan",           &T::syncPan,
        "tag_list_visible",   &T::tagListVisible,
        "show_overlay",       &T::showOverlay
    );
};

template <>
struct glz::meta<AppConfig>
{
    using T = AppConfig;
    static constexpr auto value = object(
        "global",     &T::global,
        "volumes",    &T::volumes,
        "qc_columns", &T::qcColumns
    );
};

// ---- Implementation --------------------------------------------------------

std::string globalConfigPath()
{
    // Prefer XDG_CONFIG_HOME, fall back to $HOME/.config
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    std::filesystem::path dir;
    if (xdg && xdg[0] != '\0')
    {
        dir = std::filesystem::path(xdg) / "new_register";
    }
    else
    {
        const char* home = std::getenv("HOME");
        if (!home || home[0] == '\0')
            throw std::runtime_error("Cannot determine home directory");
        dir = std::filesystem::path(home) / ".config" / "new_register";
    }
    return (dir / "config.json").string();
}

AppConfig loadConfig(const std::string& path)
{
    if (!std::filesystem::exists(path))
        return {};

    std::ifstream ifs(path);
    if (!ifs)
        throw std::runtime_error("Cannot open config file: " + path);

    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());

    AppConfig config{};
    auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(config, content);
    if (ec)
    {
        throw std::runtime_error("Failed to parse config file: " + path +
                                 "\n" + glz::format_error(ec, content));
    }
    return config;
}

void saveConfig(const AppConfig& config, const std::string& path)
{
    std::filesystem::path p(path);
    std::filesystem::path dir = p.parent_path();
    if (!dir.empty())
    {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec)
            throw std::runtime_error("Cannot create config directory: " +
                                     dir.string() + " (" + ec.message() + ")");
    }

    std::string buffer{};
    auto ec = glz::write<glz::opts{.prettify = true}>(config, buffer);
    if (ec)
        throw std::runtime_error("Failed to serialize config to JSON");

    std::ofstream ofs(path, std::ios::trunc);
    if (!ofs)
        throw std::runtime_error("Cannot write config file: " + path);
    ofs << buffer;
}

AppConfig mergeConfigs(const AppConfig& global, const AppConfig& local)
{
    AppConfig merged = global;

    // Override global settings with local ones if they differ from defaults
    GlobalConfig defaultGlobal{};
    if (local.global.defaultColourMap != defaultGlobal.defaultColourMap)
        merged.global.defaultColourMap = local.global.defaultColourMap;
    if (local.global.windowWidth.has_value())
        merged.global.windowWidth = local.global.windowWidth;
    if (local.global.windowHeight.has_value())
        merged.global.windowHeight = local.global.windowHeight;
    if (local.global.syncCursors != defaultGlobal.syncCursors)
        merged.global.syncCursors = local.global.syncCursors;
    if (local.global.syncZoom != defaultGlobal.syncZoom)
        merged.global.syncZoom = local.global.syncZoom;
    if (local.global.syncPan != defaultGlobal.syncPan)
        merged.global.syncPan = local.global.syncPan;
    if (local.global.tagListVisible != defaultGlobal.tagListVisible)
        merged.global.tagListVisible = local.global.tagListVisible;
    if (local.global.showOverlay != defaultGlobal.showOverlay)
        merged.global.showOverlay = local.global.showOverlay;

    // Merge volumes: local volumes override global ones matched by path,
    // unmatched local volumes are appended.
    for (const auto& lv : local.volumes)
    {
        bool found = false;
        for (auto& mv : merged.volumes)
        {
            if (mv.path == lv.path)
            {
                mv = lv;
                found = true;
                break;
            }
        }
        if (!found)
            merged.volumes.push_back(lv);
    }

    // Merge QC column configs: local overrides global
    if (local.qcColumns.has_value())
    {
        if (!merged.qcColumns.has_value())
            merged.qcColumns = local.qcColumns;
        else
        {
            for (const auto& [name, cfg] : *local.qcColumns)
                (*merged.qcColumns)[name] = cfg;
        }
    }

    return merged;
}
