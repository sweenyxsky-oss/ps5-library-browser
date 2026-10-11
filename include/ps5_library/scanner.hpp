#pragma once

#include "game_metadata.hpp"

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <memory>
#include <thread>
#include <mutex>
#include <map>
#include <filesystem>
#include <chrono>

namespace ps5_library {

struct ScanOptions {
    std::string root_path{"/hostapp"};
    bool recursive{true};
    int max_depth{4};               // Maximum folder crawl depth to avoid slow NFS hangs
    bool calculate_directory_sizes{true};
    std::vector<std::string> supported_extensions{".pkg", ".exfat", ".ffpkg", ".ffpfs", ".ffpfsc"};
};

struct ScanStatistics {
    size_t directories_scanned{0};
    size_t files_scanned{0};
    size_t games_found{0};
    size_t updates_found{0};
    size_t dlcs_found{0};
    uint64_t total_library_bytes{0};
    std::chrono::milliseconds elapsed_time{0};
};

using ScanProgressCallback = std::function<void(const std::string& current_item, size_t items_found, bool is_done)>;

class ScannerEngine {
public:
    ScannerEngine();
    ~ScannerEngine();

    // Non-copyable, movable
    ScannerEngine(const ScannerEngine&) = delete;
    ScannerEngine& operator=(const ScannerEngine&) = delete;
    ScannerEngine(ScannerEngine&&) noexcept;
    ScannerEngine& operator=(ScannerEngine&&) noexcept;

    // Synchronous scan
    std::vector<GameEntry> scan(const ScanOptions& options, 
                                ScanProgressCallback progress = nullptr,
                                ScanStatistics* stats = nullptr);

    // Asynchronous scan on background thread
    void start_async_scan(const ScanOptions& options,
                          ScanProgressCallback progress,
                          std::function<void(std::vector<GameEntry> entries, ScanStatistics stats)> on_complete);

    void cancel();
    bool is_scanning() const;

    // Helper: checks if default /hostapp is available and accessible
    static bool is_default_hostapp_available(std::string* path_out = nullptr);

    // Folder browser helper: lists subdirectories at a given path for folder picker UI
    static std::vector<std::string> list_subdirectories(const std::string& path);

    // Fast calculation of directory size (skips nested sce_sys once counted)
    static uint64_t calculate_directory_size(const std::filesystem::path& dir, int max_depth = 8);

private:
    std::atomic<bool> cancel_requested_{false};
    std::atomic<bool> is_scanning_{false};
    std::thread worker_thread_;
    mutable std::mutex mutex_;

    void scan_directory_internal(const std::filesystem::path& dir,
                                 int current_depth,
                                 const ScanOptions& options,
                                 const std::filesystem::path& root,
                                 std::map<std::string, GameEntry>& entries_map,
                                 ScanStatistics& stats,
                                 const ScanProgressCallback& progress);

    void register_item(const std::filesystem::path& item_path,
                       const ParamMetadata& meta,
                       const std::string& ext,
                       uint64_t size,
                       const std::filesystem::path& root,
                       std::map<std::string, GameEntry>& entries_map,
                       ScanStatistics& stats);
};

} // namespace ps5_library
