#pragma once

#include "game_metadata.hpp"

#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace ps5_library {

struct CacheHeader {
    int schema_version{2};
    std::string source_root;
    std::string timestamp_utc;
    size_t entry_count{0};
    uint64_t total_size_bytes{0};
    std::string formatted_total_size;
};

class IndexCache {
public:
    explicit IndexCache(std::string cache_file_path);

    const std::string& cache_path() const { return cache_path_; }

    // Atomic write using unique temporary file, fsync, and atomic rename
    bool save_atomic(const std::vector<GameEntry>& entries,
                     const std::string& source_root,
                     std::string* error = nullptr);

    // Fast load from JSON cache file
    bool load(std::vector<GameEntry>& entries_out,
              CacheHeader* header_out = nullptr,
              std::string* error = nullptr) const;

    // Checks if the cache exists and is newer than source root mtime
    bool is_fresh(const std::string& source_root) const;

    // Search and filter helpers
    static std::vector<GameEntry> search(const std::vector<GameEntry>& entries,
                                         const std::string& query);
    static std::optional<GameEntry> find_by_title_id(const std::vector<GameEntry>& entries,
                                                     const std::string& title_id);
    static std::vector<GameEntry> filter_by_format(const std::vector<GameEntry>& entries,
                                                   const std::string& format);

private:
    std::string cache_path_;
};

} // namespace ps5_library
