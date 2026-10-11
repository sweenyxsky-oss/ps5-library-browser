#include "ps5_library/scanner.hpp"

#include <algorithm>
#include <system_error>
#include <chrono>

namespace ps5_library {

ScannerEngine::ScannerEngine() = default;

ScannerEngine::~ScannerEngine() {
    cancel();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

ScannerEngine::ScannerEngine(ScannerEngine&& other) noexcept {
    other.cancel();
    if (other.worker_thread_.joinable()) {
        other.worker_thread_.join();
    }
    cancel_requested_.store(other.cancel_requested_.load());
    is_scanning_.store(other.is_scanning_.load());
}

ScannerEngine& ScannerEngine::operator=(ScannerEngine&& other) noexcept {
    if (this != &other) {
        cancel();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        other.cancel();
        if (other.worker_thread_.joinable()) {
            other.worker_thread_.join();
        }
        cancel_requested_.store(other.cancel_requested_.load());
        is_scanning_.store(other.is_scanning_.load());
    }
    return *this;
}

void ScannerEngine::cancel() {
    cancel_requested_.store(true);
}

bool ScannerEngine::is_scanning() const {
    return is_scanning_.load();
}

bool ScannerEngine::is_default_hostapp_available(std::string* path_out) {
    std::error_code ec;
    const std::string hostapp_path = "/hostapp";
    if (std::filesystem::exists(hostapp_path, ec) && std::filesystem::is_directory(hostapp_path, ec)) {
        if (path_out) *path_out = hostapp_path;
        return true;
    }
    return false;
}

std::vector<std::string> ScannerEngine::list_subdirectories(const std::string& path) {
    std::vector<std::string> dirs;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || !std::filesystem::is_directory(path, ec)) {
        return dirs;
    }

    for (const auto& entry : std::filesystem::directory_iterator(path, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (entry.is_directory(ec)) {
            dirs.push_back(entry.path().string());
        }
    }
    std::sort(dirs.begin(), dirs.end());
    return dirs;
}

uint64_t ScannerEngine::calculate_directory_size(const std::filesystem::path& dir, int max_depth) {
    uint64_t total = 0;
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
        return 0;
    }

    try {
        auto it = std::filesystem::recursive_directory_iterator(
            dir, std::filesystem::directory_options::skip_permission_denied, ec);
        auto end = std::filesystem::recursive_directory_iterator();

        while (it != end && !ec) {
            if (it.depth() > max_depth) {
                it.pop();
                continue;
            }
            if (it->is_regular_file(ec)) {
                total += it->file_size(ec);
            }
            it.increment(ec);
        }
    } catch (...) {
        // Fallback gracefully on traversal errors
    }
    return total;
}

void ScannerEngine::register_item(const std::filesystem::path& item_path,
                                  const ParamMetadata& meta,
                                  const std::string& ext,
                                  uint64_t size,
                                  const std::filesystem::path& root,
                                  std::map<std::string, GameEntry>& entries_map,
                                  ScanStatistics& stats) {
    std::error_code ec;
    std::string rel_path;
    try {
        rel_path = std::filesystem::relative(item_path, root, ec).generic_string();
    } catch (...) {
        rel_path = item_path.filename().string();
    }
    if (rel_path.empty()) rel_path = item_path.filename().string();

    std::string key = !meta.title_id.empty() ? meta.title_id : item_path.stem().string();

    auto& entry = entries_map[key];
    if (entry.title_id.empty() && !meta.title_id.empty()) {
        entry.title_id = meta.title_id;
    }

    if (entry.title.empty() || (meta.confidence == MetadataConfidence::Verified && entry.confidence != MetadataConfidence::Verified)) {
        entry.title = !meta.title_name.empty() ? meta.title_name : item_path.stem().string();
        entry.confidence = meta.confidence;
    }

    if (entry.cover_path.empty() && !meta.icon_path.empty()) {
        entry.cover_path = meta.icon_path;
    } else if (entry.cover_path.empty()) {
        // Check for sibling cover file
        std::filesystem::path sibling_icon = item_path.parent_path() / "icon0.png";
        if (std::filesystem::exists(sibling_icon, ec)) {
            entry.cover_path = sibling_icon.string();
        }
    }

    GameItem item;
    item.kind = meta.kind;
    item.filename = item_path.filename().string();
    item.relative_path = rel_path;
    item.absolute_path = item_path.string();
    item.format = ext.empty() ? "folder" : (ext[0] == '.' ? ext.substr(1) : ext);
    item.version = meta.version;
    item.size_bytes = size;
    item.size_known = true;
    item.formatted_size = format_file_size(size);

    if (meta.kind == ItemKind::Update) {
        item.display_name = !meta.version.empty() ? ("Update v" + meta.version) : item.filename;
        entry.updates.push_back(std::move(item));
        stats.updates_found++;
    } else if (meta.kind == ItemKind::Dlc) {
        item.display_name = !meta.title_name.empty() ? meta.title_name : item.filename;
        entry.dlcs.push_back(std::move(item));
        stats.dlcs_found++;
    } else {
        // Default base game
        item.display_name = !meta.title_name.empty() ? meta.title_name : entry.title;
        if (!entry.base_game.has_value()) {
            entry.base_game = std::move(item);
            entry.primary_format = ext.empty() ? "folder" : (ext[0] == '.' ? ext.substr(1) : ext);
            stats.games_found++;
        } else {
            // Additional package under same title
            entry.dlcs.push_back(std::move(item));
            stats.dlcs_found++;
        }
    }
}

void ScannerEngine::scan_directory_internal(const std::filesystem::path& dir,
                                           int current_depth,
                                           const ScanOptions& options,
                                           const std::filesystem::path& root,
                                           std::map<std::string, GameEntry>& entries_map,
                                           ScanStatistics& stats,
                                           const ScanProgressCallback& progress) {
    if (cancel_requested_.load()) return;

    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
        return;
    }

    stats.directories_scanned++;

    // 1. Check if this directory itself is an unpacked game folder (contains sce_sys/param.json)
    std::filesystem::path param_json = dir / "sce_sys" / "param.json";
    if (std::filesystem::exists(param_json, ec)) {
        ParamMetadata meta = parse_param_json_file(param_json);
        if (meta.valid) {
            uint64_t dir_size = options.calculate_directory_sizes ? calculate_directory_size(dir) : 0;
            register_item(dir, meta, "folder", dir_size, root, entries_map, stats);

            if (progress) {
                progress(dir.string(), entries_map.size(), false);
            }
            // Do not recurse into game binaries/assets
            return;
        }
    }

    // 2. Iterate entries in the directory
    for (const auto& entry : std::filesystem::directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (cancel_requested_.load() || ec) break;

        if (entry.is_directory(ec)) {
            // Check if subdirectory is a game
            std::filesystem::path sub_param = entry.path() / "sce_sys" / "param.json";
            if (std::filesystem::exists(sub_param, ec)) {
                ParamMetadata meta = parse_param_json_file(sub_param);
                if (meta.valid) {
                    uint64_t dir_size = options.calculate_directory_sizes ? calculate_directory_size(entry.path()) : 0;
                    register_item(entry.path(), meta, "folder", dir_size, root, entries_map, stats);
                    if (progress) {
                        progress(entry.path().string(), entries_map.size(), false);
                    }
                    continue;
                }
            }

            // Recurse if below max depth
            if (options.recursive && current_depth < options.max_depth) {
                scan_directory_internal(entry.path(), current_depth + 1, options, root, entries_map, stats, progress);
            }
        } else if (entry.is_regular_file(ec)) {
            stats.files_scanned++;
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            bool supported = false;
            for (const auto& sup : options.supported_extensions) {
                if (ext == sup) {
                    supported = true;
                    break;
                }
            }

            if (supported) {
                uint64_t fsize = entry.file_size(ec);
                ParamMetadata meta = parse_filename_heuristics(entry.path().filename().string());
                register_item(entry.path(), meta, ext, fsize, root, entries_map, stats);

                if (progress) {
                    progress(entry.path().string(), entries_map.size(), false);
                }
            }
        }
    }
}

std::vector<GameEntry> ScannerEngine::scan(const ScanOptions& options,
                                          ScanProgressCallback progress,
                                          ScanStatistics* stats) {
    cancel_requested_.store(false);
    is_scanning_.store(true);

    auto t0 = std::chrono::steady_clock::now();
    ScanStatistics local_stats;
    std::map<std::string, GameEntry> entries_map;

    std::filesystem::path root(options.root_path);
    std::error_code ec;

    if (std::filesystem::exists(root, ec)) {
        scan_directory_internal(root, 0, options, root, entries_map, local_stats, progress);
    }

    std::vector<GameEntry> result;
    result.reserve(entries_map.size());
    uint64_t total_bytes = 0;

    for (auto& [key, entry] : entries_map) {
        entry.calculate_totals();
        total_bytes += entry.total_size_bytes;
        result.push_back(std::move(entry));
    }

    // Sort alphabetically by Title
    std::sort(result.begin(), result.end(), [](const GameEntry& a, const GameEntry& b) {
        std::string ta = a.title;
        std::string tb = b.title;
        std::transform(ta.begin(), ta.end(), ta.begin(), ::tolower);
        std::transform(tb.begin(), tb.end(), tb.begin(), ::tolower);
        return ta < tb;
    });

    local_stats.total_library_bytes = total_bytes;
    local_stats.elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);

    if (stats) {
        *stats = local_stats;
    }

    if (progress) {
        progress("Complete", result.size(), true);
    }

    is_scanning_.store(false);
    return result;
}

void ScannerEngine::start_async_scan(const ScanOptions& options,
                                    ScanProgressCallback progress,
                                    std::function<void(std::vector<GameEntry>, ScanStatistics)> on_complete) {
    if (is_scanning_.load()) {
        return;
    }

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    worker_thread_ = std::thread([this, options, progress, on_complete]() {
        ScanStatistics stats;
        auto entries = scan(options, progress, &stats);
        if (on_complete && !cancel_requested_.load()) {
            on_complete(std::move(entries), stats);
        }
    });
}

} // namespace ps5_library
