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
    Unknown
};

const char* to_string(InstallErrorCode code);
InstallErrorCode install_error_code_from_string(const std::string& str);

/**
 * Returns user-actionable recovery guidance for a given install error.
 */
std::string get_recovery_guidance(InstallErrorCode code, const std::string& extra_details = "");

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
 * Pluggable delegate for executing install/mount operations.
 * Allows host testing simulation and hardware/daemon execution.
 */
class IInstallDelegate {
public:
    virtual ~IInstallDelegate() = default;

    /**
     * Executes the install step for a given task.
     * @param task The task describing the package.
     * @param on_chunk Progress callback: returns false if cancellation was requested.
     * @param out_code Output error code if installation fails.
     * @param out_error Output error message if installation fails.
     * @return true on success, false on failure or cancellation.
     */
    virtual bool install_package(const InstallTask& task,
                                 std::function<bool(uint64_t written_bytes, uint64_t total_bytes)> on_chunk,
                                 InstallErrorCode& out_code,
                                 std::string& out_error) = 0;

    /**
     * Query runtime platform capability of this delegate.
     */
    virtual bool is_platform_supported() const { return true; }
    virtual std::string platform_status_description() const { return "Ready"; }
};

/**
 * Local file-based install delegate (validates package file and simulates or copies).
 */
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
    uint64_t simulated_bytes_per_sec_{100ULL * 1024ULL * 1024ULL}; // 100 MB/s default
};

/**
 * PS5 Hardware / System-level package install delegate.
 * Connects to libSceAppInstUtil.sprx (sceAppInstUtilInstallByPackage / sceAppInstUtilInitialize)
 * or PS5 Direct Package Installer service on PS5 FreeBSD/Prospero environment.
 * Performs runtime environment capability checks, symbol resolution, and gives clear
 * unsupported-platform and privilege diagnostics on non-PS5 host OS or when required
 * modules / privileges are absent.
 */
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

    // Testing / simulation hook to mock PS5 environment behavior on host
    void set_mock_capabilities(bool is_ps5, bool has_priv, bool module_loaded, int err = 0, const std::string& reason = "");

private:
    SystemCapabilities caps_;
    bool mock_override_{false};
    void* module_handle_{nullptr};

    bool resolve_symbols_and_init();
};

/**
 * Mock install delegate for controlled unit testing.
 */
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

/**
 * High-performance Install Pipeline with persistent job state and restart recovery.
 */
class InstallPipeline {
public:
    explicit InstallPipeline(std::shared_ptr<IInstallDelegate> delegate = nullptr,
                            std::string state_filepath = "");
    ~InstallPipeline();

    // Non-copyable, movable
    InstallPipeline(const InstallPipeline&) = delete;
    InstallPipeline& operator=(const InstallPipeline&) = delete;

    // Single item installation (Game, DLC, or Update)
    std::string queue_item(const GameItem& item, const std::string& title_id);

    // Batch "Install All DLCs" pipeline
    std::string install_all_dlcs(const GameEntry& entry);

    // Queue custom list of items as a batch
    std::string queue_batch(const std::string& title_id,
                            const std::string& batch_title,
                            ItemKind target_kind,
                            const std::vector<GameItem>& items);

    // Cancellation
    void cancel_active_task();
    void cancel_all();

    // Status queries
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

    // Callbacks
    void set_progress_callback(TaskProgressCallback cb);
    void set_task_finished_callback(TaskFinishedCallback cb);
    void set_batch_finished_callback(BatchFinishedCallback cb);

    // Execution control
    void start_worker();
    void stop_worker();

    // Step/tick execution (useful for deterministic tests or main-thread stepping)
    bool process_one_step();

    // Delegate access
    void set_delegate(std::shared_ptr<IInstallDelegate> delegate);
    std::shared_ptr<IInstallDelegate> delegate() const { return delegate_; }

    // State persistence & recovery
    void set_persistence_path(const std::string& path);
    const std::string& persistence_path() const { return state_filepath_; }
    bool save_state();
    bool load_and_restore_state();

private:
    std::shared_ptr<IInstallDelegate> delegate_;
    std::string state_filepath_;

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
    bool execute_task_internal(InstallTask& task);
    std::string generate_task_id();
    uint64_t next_id_counter_{1};

    void save_state_locked();
};

} // namespace ps5_library
