#include "AppConfig.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

// ---- nlohmann/json serialization for custom JSON field names ----------------

void to_json(nlohmann::json& j, const VolumeConfig& v)
{
    j["path"] = v.path;
    j["colour_map"] = v.colourMap;
    if (v.valueMin) j["value_min"] = *v.valueMin;
    if (v.valueMax) j["value_max"] = *v.valueMax;
    j["slice_indices"] = v.sliceIndices;
    j["zoom"] = v.zoom;
    j["pan_u"] = v.panU;
    j["pan_v"] = v.panV;
}

void from_json(const nlohmann::json& j, VolumeConfig& v)
{
    if (j.contains("path"))          j.at("path").get_to(v.path);
    if (j.contains("colour_map"))    j.at("colour_map").get_to(v.colourMap);
    if (j.contains("value_min"))     v.valueMin = j.at("value_min").get<double>();
    if (j.contains("value_max"))     v.valueMax = j.at("value_max").get<double>();
    if (j.contains("slice_indices")) j.at("slice_indices").get_to(v.sliceIndices);
    if (j.contains("zoom"))          j.at("zoom").get_to(v.zoom);
    if (j.contains("pan_u"))         j.at("pan_u").get_to(v.panU);
    if (j.contains("pan_v"))         j.at("pan_v").get_to(v.panV);
}

void to_json(nlohmann::json& j, const QCColumnConfig& c)
{
    j["colour_map"] = c.colourMap;
    if (c.valueMin) j["value_min"] = *c.valueMin;
    if (c.valueMax) j["value_max"] = *c.valueMax;
}

void from_json(const nlohmann::json& j, QCColumnConfig& c)
{
    if (j.contains("colour_map")) j.at("colour_map").get_to(c.colourMap);
    if (j.contains("value_min"))  c.valueMin = j.at("value_min").get<double>();
    if (j.contains("value_max"))  c.valueMax = j.at("value_max").get<double>();
}

void to_json(nlohmann::json& j, const GlobalConfig& g)
{
    j["default_colour_map"] = g.defaultColourMap;
    if (g.windowWidth)  j["window_width"] = *g.windowWidth;
    if (g.windowHeight) j["window_height"] = *g.windowHeight;
    j["sync_cursors"] = g.syncCursors;
    j["sync_zoom"] = g.syncZoom;
    j["sync_pan"] = g.syncPan;
    j["tag_list_visible"] = g.tagListVisible;
    j["show_overlay"] = g.showOverlay;
}

void from_json(const nlohmann::json& j, GlobalConfig& g)
{
    if (j.contains("default_colour_map")) j.at("default_colour_map").get_to(g.defaultColourMap);
    if (j.contains("window_width"))       g.windowWidth = j.at("window_width").get<int>();
    if (j.contains("window_height"))      g.windowHeight = j.at("window_height").get<int>();
    if (j.contains("sync_cursors"))       j.at("sync_cursors").get_to(g.syncCursors);
    if (j.contains("sync_zoom"))          j.at("sync_zoom").get_to(g.syncZoom);
    if (j.contains("sync_pan"))           j.at("sync_pan").get_to(g.syncPan);
    if (j.contains("tag_list_visible"))   j.at("tag_list_visible").get_to(g.tagListVisible);
    if (j.contains("show_overlay"))       j.at("show_overlay").get_to(g.showOverlay);
}

void to_json(nlohmann::json& j, const AppConfig& c)
{
    j["global"] = c.global;
    j["volumes"] = c.volumes;
    if (c.qcColumns) j["qc_columns"] = *c.qcColumns;
}

void from_json(const nlohmann::json& j, AppConfig& c)
{
    if (j.contains("global"))     j.at("global").get_to(c.global);
    if (j.contains("volumes"))    j.at("volumes").get_to(c.volumes);
    if (j.contains("qc_columns"))
        c.qcColumns = j.at("qc_columns").get<std::map<std::string, QCColumnConfig>>();
}

// ---- Implementation --------------------------------------------------------

AppConfig loadConfig(const std::string& path)
{
    if (!std::filesystem::exists(path))
        return {};

    std::ifstream ifs(path);
    if (!ifs)
        throw std::runtime_error("Cannot open config file: " + path);

    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(ifs);
    }
    catch (const nlohmann::json::parse_error& e)
    {
        throw std::runtime_error("Failed to parse config file: " + path +
                                 "\n" + e.what());
    }

    AppConfig config{};
    try
    {
        config = j.get<AppConfig>();
    }
    catch (const nlohmann::json::exception& e)
    {
        throw std::runtime_error("Invalid config structure in: " + path +
                                 "\n" + e.what());
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

    nlohmann::json j = config;
    std::string buffer = j.dump(4);

    std::ofstream ofs(path, std::ios::trunc);
    if (!ofs)
        throw std::runtime_error("Cannot write config file: " + path);
    ofs << buffer;
}
