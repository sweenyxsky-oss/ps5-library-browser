#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace ps5_library {

enum class ItemKind {
    Game,       // Base game (gd)
    Dlc,        // Downloadable / Add-on content (ac)
    Update,     // Title update / patch (gp)
    Unknown
};

const char* to_string(ItemKind kind);
ItemKind parse_item_kind(const std::string& str);

enum class MetadataConfidence {
    Verified,    // Parsed from sce_sys/param.json
    Manifest,    // Parsed from game.json manifest
    Filename,    // Heuristically derived from filename patterns
    Unknown
};

const char* to_string(MetadataConfidence conf);
MetadataConfidence parse_metadata_confidence(const std::string& str);

// Individual component (Game container, DLC package, or Update package)
struct GameItem {
    ItemKind kind{ItemKind::Unknown};
    std::string filename;          // e.g. "SpiderMan2.pkg"
    std::string relative_path;     // Path relative to scan root
    std::string absolute_path;     // Full absolute path
    std::string format;            // "pkg", "exfat", "ffpkg", "folder", etc.
    std::string version;           // e.g. "01.002.000" (for updates)
    std::string display_name;      // e.g. "Pre-Order Bonus Suit DLC" or "Update v1.02"
    uint64_t size_bytes{0};
    bool size_known{false};
    std::string formatted_size;    // e.g. "45.2 GB"

    nlohmann::json to_json() const;
    static GameItem from_json(const nlohmann::json& j);
};

// Top-level game entity displayed in coverflow / grid
struct GameEntry {
    std::string title_id;          // "PPSA01234", "CUSA01234", etc.
    std::string title;             // Formatted Game Title
    std::string primary_format;    // Format of base game (e.g. "pkg", "exfat", "folder")
    std::string cover_path;        // Path to icon0.png or cover art
    MetadataConfidence confidence{MetadataConfidence::Unknown};

    std::optional<GameItem> base_game;
    std::vector<GameItem> updates;
    std::vector<GameItem> dlcs;

    uint64_t total_size_bytes{0};
    std::string formatted_total_size;

    void calculate_totals();

    nlohmann::json to_json() const;
    static GameEntry from_json(const nlohmann::json& j);
};

// Utilities for size formatting and Title ID validation
std::string format_file_size(uint64_t bytes);
bool is_valid_title_id(const std::string& title_id);
std::string extract_title_id(const std::string& text);

// Extracted metadata from sce_sys/param.json or param.sfo
struct ParamMetadata {
    bool valid{false};
    std::string title_id;
    std::string title_name;
    std::string category;          // "gd", "gp", "ac", etc.
    ItemKind kind{ItemKind::Unknown};
    std::string version;           // "01.000.000"
    std::string icon_path;
    MetadataConfidence confidence{MetadataConfidence::Unknown};
};

ParamMetadata parse_param_json_string(const std::string& json_str);
ParamMetadata parse_param_json_file(const std::filesystem::path& path);
ParamMetadata parse_filename_heuristics(const std::string& filename);

} // namespace ps5_library
