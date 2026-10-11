#include "ps5_library/game_metadata.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>

namespace ps5_library {

const char* to_string(ItemKind kind) {
    switch (kind) {
        case ItemKind::Game:   return "game";
        case ItemKind::Dlc:    return "dlc";
        case ItemKind::Update: return "update";
        default:               return "unknown";
    }
}

ItemKind parse_item_kind(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "game" || lower == "gd" || lower == "gdc" || lower == "gda") return ItemKind::Game;
    if (lower == "dlc" || lower == "ac" || lower == "acc" || lower == "addon") return ItemKind::Dlc;
    if (lower == "update" || lower == "gp" || lower == "gpc" || lower == "patch") return ItemKind::Update;
    return ItemKind::Unknown;
}

const char* to_string(MetadataConfidence conf) {
    switch (conf) {
        case MetadataConfidence::Verified: return "verified";
        case MetadataConfidence::Manifest: return "manifest";
        case MetadataConfidence::Filename: return "filename";
        default:                           return "unknown";
    }
}

MetadataConfidence parse_metadata_confidence(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "verified") return MetadataConfidence::Verified;
    if (lower == "manifest") return MetadataConfidence::Manifest;
    if (lower == "filename") return MetadataConfidence::Filename;
    return MetadataConfidence::Unknown;
}

std::string format_file_size(uint64_t bytes) {
    if (bytes == 0) {
        return "0 B";
    }
    static const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    double d = static_cast<double>(bytes);
    int unit_idx = 0;
    while (d >= 1024.0 && unit_idx < 5) {
        d /= 1024.0;
        unit_idx++;
    }

    std::ostringstream ss;
    if (unit_idx == 0) {
        ss << bytes << " B";
    } else if (d >= 10.0) {
        ss << std::fixed << std::setprecision(1) << d << " " << units[unit_idx];
    } else {
        ss << std::fixed << std::setprecision(2) << d << " " << units[unit_idx];
    }
    return ss.str();
}

bool is_valid_title_id(const std::string& title_id) {
    if (title_id.size() != 9) return false;
    std::string prefix = title_id.substr(0, 4);
    if (prefix != "PPSA" && prefix != "CUSA" && prefix != "LAPY" && prefix != "FAKE") {
        return false;
    }
    for (size_t i = 4; i < 9; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(title_id[i]))) {
            return false;
        }
    }
    return true;
}

std::string extract_title_id(const std::string& text) {
    // Regex matches PPSA12345, CUSA12345, PPSA-12345, etc.
    static const std::regex pattern(R"((PPSA|CUSA|LAPY|FAKE)[-_]?(\d{5}))", std::regex::icase);
    std::smatch match;
    if (std::regex_search(text, match, pattern)) {
        std::string prefix = match[1].str();
        std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::toupper);
        return prefix + match[2].str();
    }
    return "";
}

nlohmann::json GameItem::to_json() const {
    return {
        {"kind", to_string(kind)},
        {"filename", filename},
        {"relative_path", relative_path},
        {"absolute_path", absolute_path},
        {"format", format},
        {"version", version},
        {"display_name", display_name},
        {"size_bytes", size_bytes},
        {"size_known", size_known},
        {"formatted_size", formatted_size}
    };
}

GameItem GameItem::from_json(const nlohmann::json& j) {
    GameItem item;
    item.kind = parse_item_kind(j.value("kind", "unknown"));
    item.filename = j.value("filename", "");
    item.relative_path = j.value("relative_path", "");
    item.absolute_path = j.value("absolute_path", "");
    item.format = j.value("format", "");
    item.version = j.value("version", "");
    item.display_name = j.value("display_name", "");
    item.size_bytes = j.value("size_bytes", 0ULL);
    item.size_known = j.value("size_known", false);
    item.formatted_size = j.value("formatted_size", "");
    if (item.formatted_size.empty() && item.size_known) {
        item.formatted_size = format_file_size(item.size_bytes);
    }
    return item;
}

void GameEntry::calculate_totals() {
    uint64_t total = 0;
    if (base_game.has_value()) {
        total += base_game->size_bytes;
        if (primary_format.empty()) {
            primary_format = base_game->format;
        }
    }
    for (const auto& upd : updates) {
        total += upd.size_bytes;
    }
    for (const auto& dlc : dlcs) {
        total += dlc.size_bytes;
    }
    total_size_bytes = total;
    formatted_total_size = format_file_size(total);
}

nlohmann::json GameEntry::to_json() const {
    nlohmann::json upd_arr = nlohmann::json::array();
    for (const auto& u : updates) upd_arr.push_back(u.to_json());

    nlohmann::json dlc_arr = nlohmann::json::array();
    for (const auto& d : dlcs) dlc_arr.push_back(d.to_json());

    nlohmann::json j = {
        {"title_id", title_id},
        {"title", title},
        {"primary_format", primary_format},
        {"cover_path", cover_path},
        {"confidence", to_string(confidence)},
        {"total_size_bytes", total_size_bytes},
        {"formatted_total_size", formatted_total_size},
        {"updates", std::move(upd_arr)},
        {"dlcs", std::move(dlc_arr)}
    };

    if (base_game.has_value()) {
        j["base_game"] = base_game->to_json();
    } else {
        j["base_game"] = nullptr;
    }

    return j;
}

GameEntry GameEntry::from_json(const nlohmann::json& j) {
    GameEntry entry;
    entry.title_id = j.value("title_id", "");
    entry.title = j.value("title", "");
    entry.primary_format = j.value("primary_format", "");
    entry.cover_path = j.value("cover_path", "");
    entry.confidence = parse_metadata_confidence(j.value("confidence", "unknown"));
    entry.total_size_bytes = j.value("total_size_bytes", 0ULL);
    entry.formatted_total_size = j.value("formatted_total_size", "");

    if (j.contains("base_game") && !j["base_game"].is_null()) {
        entry.base_game = GameItem::from_json(j["base_game"]);
    }

    if (j.contains("updates") && j["updates"].is_array()) {
        for (const auto& u_json : j["updates"]) {
            entry.updates.push_back(GameItem::from_json(u_json));
        }
    }

    if (j.contains("dlcs") && j["dlcs"].is_array()) {
        for (const auto& d_json : j["dlcs"]) {
            entry.dlcs.push_back(GameItem::from_json(d_json));
        }
    }

    entry.calculate_totals();
    return entry;
}

ParamMetadata parse_param_json_string(const std::string& json_str) {
    ParamMetadata meta;
    if (json_str.empty()) return meta;

    try {
        auto root = nlohmann::json::parse(json_str);
        if (!root.is_object()) return meta;

        // Title ID
        std::string raw_id;
        if (root.contains("titleId") && root["titleId"].is_string()) {
            raw_id = root["titleId"].get<std::string>();
        } else if (root.contains("title_id") && root["title_id"].is_string()) {
            raw_id = root["title_id"].get<std::string>();
        }

        if (!raw_id.empty()) {
            std::string extracted = extract_title_id(raw_id);
            if (!extracted.empty()) {
                meta.title_id = extracted;
            } else {
                meta.title_id = raw_id;
            }
        }

        // Title Name (localized vs root)
        std::string name;
        if (root.contains("localizedParameters") && root["localizedParameters"].is_object()) {
            const auto& loc = root["localizedParameters"];
            if (loc.contains("en-US") && loc["en-US"].is_object() && loc["en-US"].contains("titleName")) {
                name = loc["en-US"]["titleName"].get<std::string>();
            } else if (loc.contains("defaultLanguage") && loc["defaultLanguage"].is_string()) {
                std::string def_lang = loc["defaultLanguage"].get<std::string>();
                if (loc.contains(def_lang) && loc[def_lang].is_object() && loc[def_lang].contains("titleName")) {
                    name = loc[def_lang]["titleName"].get<std::string>();
                }
            }
            if (name.empty()) {
                for (auto it = loc.begin(); it != loc.end(); ++it) {
                    if (it.value().is_object() && it.value().contains("titleName") && it.value()["titleName"].is_string()) {
                        name = it.value()["titleName"].get<std::string>();
                        if (!name.empty()) break;
                    }
                }
            }
        }

        if (name.empty() && root.contains("titleName") && root["titleName"].is_string()) {
            name = root["titleName"].get<std::string>();
        }

        meta.title_name = name;

        // Category
        if (root.contains("category") && root["category"].is_string()) {
            meta.category = root["category"].get<std::string>();
            meta.kind = parse_item_kind(meta.category);
        } else if (root.contains("content_type") && root["content_type"].is_string()) {
            meta.category = root["content_type"].get<std::string>();
            meta.kind = parse_item_kind(meta.category);
        } else {
            meta.kind = ItemKind::Game; // Default to game if category unspecified
        }

        // Version
        if (root.contains("contentVersion") && root["contentVersion"].is_string()) {
            meta.version = root["contentVersion"].get<std::string>();
        } else if (root.contains("version") && root["version"].is_string()) {
            meta.version = root["version"].get<std::string>();
        }

        meta.valid = !meta.title_id.empty();
        meta.confidence = MetadataConfidence::Verified;

    } catch (...) {
        meta.valid = false;
    }

    return meta;
}

ParamMetadata parse_param_json_file(const std::filesystem::path& path) {
    ParamMetadata meta;
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return meta;

    std::ostringstream ss;
    ss << file.rdbuf();
    meta = parse_param_json_string(ss.str());

    // Locate icon0.png in sibling directory
    auto parent = path.parent_path();
    auto icon_path = parent / "icon0.png";
    if (std::filesystem::exists(icon_path)) {
        meta.icon_path = icon_path.string();
    }
    return meta;
}

ParamMetadata parse_filename_heuristics(const std::string& filename) {
    ParamMetadata meta;
    meta.confidence = MetadataConfidence::Filename;

    std::string tid = extract_title_id(filename);
    meta.title_id = tid;

    // Detect kind
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower.find("patch") != std::string::npos || 
        lower.find("update") != std::string::npos ||
        std::regex_search(lower, std::regex(R"(v\d+\.\d+)"))) {
        meta.kind = ItemKind::Update;
        // Try extracting version
        std::smatch vmatch;
        if (std::regex_search(lower, vmatch, std::regex(R"(v(\d+(\.\d+)+))"))) {
            meta.version = vmatch[1].str();
        }
    } else if (lower.find("dlc") != std::string::npos ||
               lower.find("addon") != std::string::npos ||
               lower.find("bonus") != std::string::npos) {
        meta.kind = ItemKind::Dlc;
    } else {
        meta.kind = ItemKind::Game;
    }

    // Attempt clean title from filename: strip extension, brackets, etc.
    std::string clean = filename;
    size_t dot = clean.find_last_of('.');
    if (dot != std::string::npos) {
        clean = clean.substr(0, dot);
    }
    // Remove bracketed parts like [PPSA01234] [Update]
    clean = std::regex_replace(clean, std::regex(R"(\[[^\]]*\]|\([^\)]*\))"), " ");
    // Replace dots and underscores with spaces
    std::replace(clean.begin(), clean.end(), '.', ' ');
    std::replace(clean.begin(), clean.end(), '_', ' ');

    // Trim whitespace
    size_t first = clean.find_first_not_of(" \t\r\n");
    size_t last = clean.find_last_not_of(" \t\r\n");
    if (first != std::string::npos && last != std::string::npos) {
        meta.title_name = clean.substr(first, (last - first + 1));
    } else {
        meta.title_name = !tid.empty() ? tid : filename;
    }

    meta.valid = !meta.title_id.empty() || !meta.title_name.empty();
    return meta;
}

} // namespace ps5_library
