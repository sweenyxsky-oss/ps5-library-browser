#include "ps5_library/index_cache.hpp"

#include <chrono>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace ps5_library {

namespace {

std::string get_iso8601_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&in_time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

std::string to_lower_copy(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return str;
}

} // namespace

IndexCache::IndexCache(std::string cache_file_path)
    : cache_path_(std::move(cache_file_path)) {}

bool IndexCache::save_atomic(const std::vector<GameEntry>& entries,
                             const std::string& source_root,
                             std::string* error) {
    if (cache_path_.empty()) {
        if (error) *error = "cache path is empty";
        return false;
    }

    std::filesystem::path target(cache_path_);
    std::error_code ec;

    // Ensure parent directory exists
    auto parent = target.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent, ec)) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            if (error) *error = "cannot create directory " + parent.string() + ": " + ec.message();
            return false;
        }
    }

    // Build unique temporary filename: target.tmp.<pid>.<timestamp>
    auto now_ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::string temp_filename = target.filename().string() + ".tmp." +
                                std::to_string(getpid()) + "." + std::to_string(now_ns);
    std::filesystem::path temp_path = parent / temp_filename;

    try {
        nlohmann::json entries_arr = nlohmann::json::array();
        uint64_t total_library_size = 0;
        for (const auto& entry : entries) {
            entries_arr.push_back(entry.to_json());
            total_library_size += entry.total_size_bytes;
        }

        nlohmann::json root_doc = {
            {"schema_version", 2},
            {"source_root", source_root},
            {"timestamp_utc", get_iso8601_timestamp()},
            {"entry_count", entries.size()},
            {"total_size_bytes", total_library_size},
            {"formatted_total_size", format_file_size(total_library_size)},
            {"entries", std::move(entries_arr)}
        };

        std::string serialized = root_doc.dump(2) + "\n";

        // Open temporary file with O_SYNC / fsync guarantees
        int fd = open(temp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            if (error) *error = "cannot open temp file: " + temp_path.string();
            return false;
        }

        size_t total_written = 0;
        while (total_written < serialized.size()) {
            ssize_t w = write(fd, serialized.data() + total_written, serialized.size() - total_written);
            if (w <= 0) {
                close(fd);
                unlink(temp_path.c_str());
                if (error) *error = "failed writing to temp file: " + temp_path.string();
                return false;
            }
            total_written += static_cast<size_t>(w);
        }

        // Commit to disk
        fsync(fd);
        close(fd);

        // Atomic swap
        if (rename(temp_path.c_str(), target.c_str()) != 0) {
            unlink(temp_path.c_str());
            if (error) *error = "failed atomic rename from temp file to target";
            return false;
        }

        return true;

    } catch (const std::exception& ex) {
        unlink(temp_path.c_str());
        if (error) *error = std::string("exception during save: ") + ex.what();
        return false;
    }
}

bool IndexCache::load(std::vector<GameEntry>& entries_out,
                      CacheHeader* header_out,
                      std::string* error) const {
    entries_out.clear();
    std::filesystem::path target(cache_path_);
    std::error_code ec;

    if (!std::filesystem::exists(target, ec)) {
        if (error) *error = "cache file does not exist: " + cache_path_;
        return false;
    }

    std::ifstream file(cache_path_, std::ios::binary);
    if (!file.is_open()) {
        if (error) *error = "cannot open cache file: " + cache_path_;
        return false;
    }

    try {
        nlohmann::json doc;
        file >> doc;

        if (!doc.is_object() || !doc.contains("entries") || !doc["entries"].is_array()) {
            if (error) *error = "malformed cache document structure";
            return false;
        }

        int schema = doc.value("schema_version", 1);
        CacheHeader hdr;
        hdr.schema_version = schema;
        hdr.source_root = doc.value("source_root", "");
        hdr.timestamp_utc = doc.value("timestamp_utc", "");
        hdr.entry_count = doc.value("entry_count", 0ULL);
        hdr.total_size_bytes = doc.value("total_size_bytes", 0ULL);
        hdr.formatted_total_size = doc.value("formatted_total_size", "");

        for (const auto& item_doc : doc["entries"]) {
            if (item_doc.is_object()) {
                entries_out.push_back(GameEntry::from_json(item_doc));
            }
        }

        if (header_out) {
            *header_out = std::move(hdr);
        }

        return true;

    } catch (const std::exception& ex) {
        if (error) *error = std::string("failed parsing cache JSON: ") + ex.what();
        return false;
    }
}

bool IndexCache::is_fresh(const std::string& source_root) const {
    std::error_code ec;
    std::filesystem::path cache_p(cache_path_);
    std::filesystem::path root_p(source_root);

    if (!std::filesystem::exists(cache_p, ec) || !std::filesystem::exists(root_p, ec)) {
        return false;
    }

    auto cache_mtime = std::filesystem::last_write_time(cache_p, ec);
    if (ec) return false;

    auto root_mtime = std::filesystem::last_write_time(root_p, ec);
    if (ec) return false;

    return cache_mtime >= root_mtime;
}

std::vector<GameEntry> IndexCache::search(const std::vector<GameEntry>& entries,
                                         const std::string& query) {
    if (query.empty()) {
        return entries;
    }

    std::string q_lower = to_lower_copy(query);
    std::vector<GameEntry> results;

    for (const auto& entry : entries) {
        std::string title_lower = to_lower_copy(entry.title);
        std::string tid_lower = to_lower_copy(entry.title_id);
        std::string format_lower = to_lower_copy(entry.primary_format);

        if (title_lower.find(q_lower) != std::string::npos ||
            tid_lower.find(q_lower) != std::string::npos ||
            format_lower.find(q_lower) != std::string::npos) {
            results.push_back(entry);
            continue;
        }

        // Search through DLC or Update names
        bool match_inner = false;
        for (const auto& dlc : entry.dlcs) {
            if (to_lower_copy(dlc.display_name).find(q_lower) != std::string::npos ||
                to_lower_copy(dlc.filename).find(q_lower) != std::string::npos) {
                match_inner = true;
                break;
            }
        }
        if (!match_inner) {
            for (const auto& upd : entry.updates) {
                if (to_lower_copy(upd.display_name).find(q_lower) != std::string::npos ||
                    to_lower_copy(upd.version).find(q_lower) != std::string::npos) {
                    match_inner = true;
                    break;
                }
            }
        }

        if (match_inner) {
            results.push_back(entry);
        }
    }

    return results;
}

std::optional<GameEntry> IndexCache::find_by_title_id(const std::vector<GameEntry>& entries,
                                                     const std::string& title_id) {
    std::string tid_upper = title_id;
    std::transform(tid_upper.begin(), tid_upper.end(), tid_upper.begin(), ::toupper);

    for (const auto& entry : entries) {
        std::string cur = entry.title_id;
        std::transform(cur.begin(), cur.end(), cur.begin(), ::toupper);
        if (cur == tid_upper) {
            return entry;
        }
    }
    return std::nullopt;
}

std::vector<GameEntry> IndexCache::filter_by_format(const std::vector<GameEntry>& entries,
                                                   const std::string& format) {
    std::string fmt_lower = to_lower_copy(format);
    if (!fmt_lower.empty() && fmt_lower[0] == '.') {
        fmt_lower = fmt_lower.substr(1);
    }

    std::vector<GameEntry> results;
    for (const auto& entry : entries) {
        if (to_lower_copy(entry.primary_format) == fmt_lower) {
            results.push_back(entry);
        }
    }
    return results;
}

} // namespace ps5_library
