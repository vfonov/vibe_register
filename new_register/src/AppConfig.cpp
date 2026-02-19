#include "AppConfig.h"

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
