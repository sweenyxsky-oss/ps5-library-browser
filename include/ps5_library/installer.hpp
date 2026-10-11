#pragma once

#include "game_metadata.hpp"

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <optional>
#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <chrono>

namespace ps5_library {

enum class InstallStatus {
    Queued,
    InProgress,
    Completed,
    Failed,
    Cancelled,
    Interrupted
};

const char* to_string(InstallStatus status);
InstallStatus install_status_from_string(const std::string& str);

enum class InstallErrorCode {
    None,
    FileNotFound,
    InvalidPackage,
    InsufficientSpace,
    Cancelled,
    DaemonRequired,
    IoError,
    UnsupportedPlatform,
    MissingPrivilege,
    SystemModuleMissing,
    CorruptedHeader,
    TargetAppRunning,
    InstallInterrupted,
    FileUnreadable,
    MetadataMissing,
    DuplicateInstallBlocked,
    Unknown
};

const char* to_string(InstallErrorCode code);
const char* install_error_name(InstallErrorCode code);
InstallErrorCode install_error_code_from_string(const std::string& str);

/**
 * Returns user-actionable recovery guidance for a given install error.
 */
std::string get_recovery_guidance(InstallErrorCode code, const std::string& extra_details = "");

/**
 * Individual preflight check item (Readability, Metadata, Available Storage).
 */
struct PreflightCheckItem {
    std::string name;              // e.g. "File Readability", "Package Metadata", "Available Storage"
    bool passed{false};
    std::string details;           // e.g. "Readable (45.2 GB, regular file)"
    std::string failure_reason;    // e.g. "Cannot open package for reading"
    std::string remediation;       // Actionable recovery guidance
};

/**
 * Per-check preflight summary structure.
 */
struct PreflightCheckSummary {
    bool all_passed{true};
    PreflightCheckItem readability;
    PreflightCheckItem metadata;
    PreflightCheckItem storage;

    std::vector<PreflightCheckItem> checks() const {
        return {readability, metadata, storage};
    }

    std::string formatted_summary() const;
};

/**
 * Result of package preflight validation step before dispatch.
 */
struct PreflightResult {
    bool passed{true};
    InstallErrorCode error_code{InstallErrorCode::None};
    std::string error_message;
    std::string recovery_guidance;
    uint64_t file_size_bytes{0};
    uint64_t available_storage_bytes{0};
    uint64_t required_storage_bytes{0};
    std::string detected_format;
    std::string detected_title_id;
    PreflightCheckSummary summary;
};

/**
 * Runs preflight validation checking file readability, package metadata, magic headers,
 * and available storage before dispatching to install delegates.
 */
PreflightResult run_package_preflight(const std::string& package_path,
                                      const std::string& target_storage_dir = "/user/app",
                                      uint64_t safety_margin_bytes = 500ULL * 1024ULL * 1024ULL);

struct InstallTask {
    std::string task_id;
    std::string title_id;
    std::string package_filename;
    std::string display_name;
    std::string source_path;
    std::string target_path;
    std::string format;
    ItemKind kind{ItemKind::Unknown};

    uint64_t total_bytes{0};
    uint64_t processed_bytes{0};

    InstallStatus status{InstallStatus::Queued};
    InstallErrorCode error_code{InstallErrorCode::None};
    std::string error_message;
    std::string recovery_guidance;

    // Retry tracking & preserving original failure details
    size_t retry_count{0};
    std::string original_task_id;
    InstallErrorCode original_error_code{InstallErrorCode::None};
    std::string original_error_message;

    // Timestamps
    std::string created_timestamp;
    std::string completed_timestamp;

    std::chrono::steady_clock::time_point start_time{};
    std::chrono::steady_clock::time_point finish_time{};

    float progress_ratio() const;
    int progress_percent() const;
    bool is_terminal() const;
};

struct BatchProgress {
    std::string batch_id;
    std::string title_id;
    std::string title_name;
    ItemKind target_kind{ItemKind::Dlc};

    size_t total_items{0};
    size_t completed_items{0};
    size_t failed_items{0};
    size_t cancelled_items{0};

    size_t current_item_index{0};
    std::string current_item_name;

    uint64_t total_bytes{0};
    uint64_t processed_bytes{0};

    bool is_running{false};
    bool is_completed{false};
    bool is_cancelled{false};
    bool has_failures{false};

    std::vector<std::string> failure_messages;

    float overall_progress_ratio() const;
    int overall_progress_percent() const;
    float current_item_progress_ratio() const;
    int current_item_progress_percent() const;
};

/**
 * Sorting fields and orders for install history.
 */
enum class HistorySortField {
    Timestamp,
    PackageName
};

enum class HistorySortOrder {
    Descending,
    Ascending
};

const char* to_string(HistorySortField field);
const char* to_string(HistorySortOrder order);
HistorySortField history_sort_field_from_string(const std::string& str);
HistorySortOrder history_sort_order_from_string(const std::string& str);

/**
 * Filter options for install history queries.
 */
struct HistoryFilterOptions {
    std::string query;                                           // Free-text query matching Title ID, filename, error, etc.
    std::optional<InstallStatus> outcome_filter{std::nullopt};   // Filter by outcome (Completed, Failed, Interrupted, etc.)
    std::string format_filter;                                   // Filter by package format (pkg, exfat, ffpkg, or empty for all)
    std::string date_from;                                       // ISO date range start (e.g. "2026-10-01" or full timestamp)
    std::string date_to;                                         // ISO date range end (e.g. "2026-10-12" or full timestamp)
    HistorySortField sort_field{HistorySortField::Timestamp};
    HistorySortOrder sort_order{HistorySortOrder::Descending};
};

/**
 * Export install history to JSON string.
 */
std::string export_history_to_json(const std::vector<InstallTask>& tasks, int indent = 2);

/**
 * Export install history to RFC 4180 CSV string.
 */
std::string export_history_to_csv(const std::vector<InstallTask>& tasks);

/**
 * Export install history to a file on disk (as JSON or CSV).
 */
bool export_history_to_file(const std::string& filepath, const std::vector<InstallTask>& tasks, bool as_json = true);

/**
 * Pluggable delegate for executing install/mount operations.
 */
class IInstallDelegate {
public:
    virtual ~IInstallDelegate() = default;

    virtual bool install_package(const InstallTask& task,
                                 std::function<bool(uint64_t written_bytes, uint64_t total_bytes)> on_chunk,
                                 InstallErrorCode& out_code,
                                 std::string& out_error) = 0;

    virtual bool is_platform_supported() const { return true; }
    virtual std::string platform_status_description() const { return "Ready"; }
};

class LocalInstallDelegate : public IInstallDelegate {
public:
    explicit LocalInstallDelegate(std::string target_base_directory = "/user/app");

    bool install_package(const InstallTask& task,
                         std::function<bool(uint64_t written_bytes, uint64_t total_bytes)> on_chunk,
                         InstallErrorCode& out_code,
                         std::string& out_error) override;

    void set_simulate_speed_bytes_per_sec(uint64_t bytes_per_sec) {
        simulated_bytes_per_sec_ = bytes_per_sec;
    }

private:
    std::string target_base_dir_;
    uint64_t simulated_bytes_per_sec_{100ULL * 1024ULL * 1024ULL};
};

class Ps5InstallDelegate : public IInstallDelegate {
public:
    struct SystemCapabilities {
        bool is_ps5_environment{false};
        bool has_kernel_privilege{false};
        bool appinst_module_loaded{false};
        bool init_succeeded{false};
        int last_system_error{0};
        std::string diagnostic_reason;
    };

    explicit Ps5InstallDelegate(bool auto_check_capabilities = true);
    ~Ps5InstallDelegate() override;

    bool install_package(const InstallTask& task,
                         std::function<bool(uint64_t written_bytes, uint64_t total_bytes)> on_chunk,
                         InstallErrorCode& out_code,
                         std::string& out_error) override;

    bool is_platform_supported() const override;
    std::string platform_status_description() const override;

    const SystemCapabilities& capabilities() const { return caps_; }
    void refresh_capabilities();

    void set_mock_capabilities(bool is_ps5, bool has_priv, bool module_loaded, int err = 0, const std::string& reason = "");

private:
    SystemCapabilities caps_;
    bool mock_override_{false};
    void* module_handle_{nullptr};

    bool resolve_symbols_and_init();
};

class MockInstallDelegate : public IInstallDelegate {
public:
    struct ItemRule {
        bool should_fail{false};
        InstallErrorCode failure_code{InstallErrorCode::IoError};
        std::string failure_message{"Simulated failure"};
        size_t fail_at_byte_offset{0};
    };

    void configure_failure(const std::string& filename,
                           InstallErrorCode code,
                           const std::string& message,
                           size_t fail_at_offset = 0);

    bool install_package(const InstallTask& task,
                         std::function<bool(uint64_t written_bytes, uint64_t total_bytes)> on_chunk,
                         InstallErrorCode& out_code,
                         std::string& out_error) override;

    size_t installed_count() const { return installed_count_; }
    void reset();

private:
    std::unordered_map<std::string, ItemRule> rules_;
    std::atomic<size_t> installed_count_{0};
};

using TaskProgressCallback = std::function<void(const InstallTask& task, const BatchProgress& batch)>;
using TaskFinishedCallback = std::function<void(const InstallTask& task)>;
using BatchFinishedCallback = std::function<void(const BatchProgress& batch)>;

struct QueueCheckResult {
    bool can_queue{true};
    bool is_duplicate{false};
    bool is_retry{false};
    std::string reason;
    std::optional<InstallTask> existing_task;
};

class InstallPipeline {
public:
    explicit InstallPipeline(std::shared_ptr<IInstallDelegate> delegate = nullptr,
                            std::string state_filepath = "");
    ~InstallPipeline();

    InstallPipeline(const InstallPipeline&) = delete;
    InstallPipeline& operator=(const InstallPipeline&) = delete;

    std::string queue_item(const GameItem& item, const std::string& title_id);
    std::string retry_task(const std::string& task_id_or_path, std::string* out_error = nullptr);
    QueueCheckResult check_queue_preflight(const std::string& source_path, bool is_retry_intent = false) const;

    std::string install_all_dlcs(const GameEntry& entry);
    std::string queue_batch(const std::string& title_id,
                            const std::string& batch_title,
                            ItemKind target_kind,
                            const std::vector<GameItem>& items);

    void cancel_active_task();
    void cancel_all();

    bool is_busy() const;
    std::optional<InstallTask> current_active_task() const;
    BatchProgress current_batch_progress() const;
    std::vector<InstallTask> all_tasks() const;
    std::vector<InstallTask> queued_tasks() const;
    std::vector<InstallTask> completed_tasks() const;
    std::vector<InstallTask> failed_tasks() const;
    std::optional<InstallTask> get_task(const std::string& task_id) const;
    InstallStatus get_item_status(const std::string& source_path) const;
    std::optional<InstallTask> get_task_by_path(const std::string& source_path) const;

    // Search and filtering
    std::vector<InstallTask> search_history(const std::string& query = "",
                                           std::optional<InstallStatus> status_filter = std::nullopt) const;
    std::vector<InstallTask> filter_and_sort_history(const HistoryFilterOptions& options) const;

    // Export history (JSON & CSV)
    std::string export_history_to_json(int indent = 2) const;
    std::string export_history_to_json(const std::vector<InstallTask>& tasks, int indent = 2) const;
    std::string export_history_to_csv() const;
    std::string export_history_to_csv(const std::vector<InstallTask>& tasks) const;
    bool export_history_to_file(const std::string& filepath, bool as_json = true) const;
    bool export_history_to_file(const std::string& filepath, const std::vector<InstallTask>& tasks, bool as_json = true) const;

    // Callbacks
    void set_progress_callback(TaskProgressCallback cb);
    void set_task_finished_callback(TaskFinishedCallback cb);
    void set_batch_finished_callback(BatchFinishedCallback cb);

    // Execution control
    void start_worker();
    void stop_worker();
    bool process_one_step();

    // Delegate access
    void set_delegate(std::shared_ptr<IInstallDelegate> delegate);
    std::shared_ptr<IInstallDelegate> delegate() const { return delegate_; }

    // Preflight configuration
    void set_target_storage_dir(const std::string& dir) { target_storage_dir_ = dir; }
    const std::string& target_storage_dir() const { return target_storage_dir_; }
    void set_enable_preflight(bool enable) { enable_preflight_ = enable; }
    bool is_preflight_enabled() const { return enable_preflight_; }

    // State persistence & recovery
    void set_persistence_path(const std::string& path);
    const std::string& persistence_path() const { return state_filepath_; }
    bool save_state();
    bool load_and_restore_state();

private:
    std::shared_ptr<IInstallDelegate> delegate_;
    std::string state_filepath_;
    std::string target_storage_dir_{"/user/app"};
    bool enable_preflight_{false};

    mutable std::mutex queue_mutex_;
    std::vector<InstallTask> task_queue_;
    std::vector<InstallTask> task_history_;
    std::optional<InstallTask> active_task_;
    BatchProgress active_batch_;

    std::atomic<bool> worker_running_{false};
    std::atomic<bool> cancel_requested_{false};
    std::thread worker_thread_;

    TaskProgressCallback progress_callback_;
    TaskFinishedCallback task_finished_callback_;
    BatchFinishedCallback batch_finished_callback_;

    void worker_loop();
    std::string generate_task_id();
    uint64_t next_id_counter_{1};

    void save_state_locked();
};

} // namespace ps5_library
