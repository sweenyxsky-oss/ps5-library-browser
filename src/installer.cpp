#include "ps5_library/installer.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace ps5_library {

const char* to_string(InstallStatus status) {
    switch (status) {
        case InstallStatus::Queued:     return "Queued";
        case InstallStatus::InProgress: return "Installing";
        case InstallStatus::Completed:  return "Completed";
        case InstallStatus::Failed:     return "Failed";
        case InstallStatus::Cancelled:  return "Cancelled";
        default:                        return "Unknown";
    }
}

const char* to_string(InstallErrorCode code) {
    switch (code) {
        case InstallErrorCode::None:              return "None";
        case InstallErrorCode::FileNotFound:      return "Package file not found";
        case InstallErrorCode::InvalidPackage:    return "Corrupt or invalid package structure";
        case InstallErrorCode::InsufficientSpace: return "Insufficient storage space";
        case InstallErrorCode::Cancelled:         return "Installation cancelled";
        case InstallErrorCode::DaemonRequired:    return "ShadowMountPlus daemon unavailable";
        case InstallErrorCode::IoError:           return "I/O read/write error";
        default:                                  return "Unknown error";
    }
}

float InstallTask::progress_ratio() const {
    if (total_bytes == 0) return (status == InstallStatus::Completed) ? 1.0f : 0.0f;
    return std::clamp(static_cast<float>(processed_bytes) / static_cast<float>(total_bytes), 0.0f, 1.0f);
}

int InstallTask::progress_percent() const {
    return static_cast<int>(progress_ratio() * 100.0f);
}

bool InstallTask::is_terminal() const {
    return status == InstallStatus::Completed ||
           status == InstallStatus::Failed ||
           status == InstallStatus::Cancelled;
}

float BatchProgress::overall_progress_ratio() const {
    if (total_bytes > 0) {
        return std::clamp(static_cast<float>(processed_bytes) / static_cast<float>(total_bytes), 0.0f, 1.0f);
    }
    if (total_items > 0) {
        return std::clamp(static_cast<float>(completed_items) / static_cast<float>(total_items), 0.0f, 1.0f);
    }
    return is_completed ? 1.0f : 0.0f;
}

int BatchProgress::overall_progress_percent() const {
    return static_cast<int>(overall_progress_ratio() * 100.0f);
}

float BatchProgress::current_item_progress_ratio() const {
    if (total_items == 0) return 0.0f;
    return overall_progress_ratio();
}

int BatchProgress::current_item_progress_percent() const {
    return static_cast<int>(current_item_progress_ratio() * 100.0f);
}

// LocalInstallDelegate
LocalInstallDelegate::LocalInstallDelegate(std::string target_base_directory)
    : target_base_dir_(std::move(target_base_directory)) {}

bool LocalInstallDelegate::install_package(
    const InstallTask& task,
    std::function<bool(uint64_t written_bytes, uint64_t total_bytes)> on_chunk,
    InstallErrorCode& out_code,
    std::string& out_error) {

    std::error_code ec;
    std::filesystem::path src(task.source_path);

    // 1. Verify existence
    if (!std::filesystem::exists(src, ec) || ec) {
        out_code = InstallErrorCode::FileNotFound;
        out_error = "Package file not found at path: " + task.source_path;
        return false;
    }

    uint64_t file_size = std::filesystem::file_size(src, ec);
    if (ec) {
        file_size = task.total_bytes;
    }
    if (file_size == 0) {
        file_size = (task.total_bytes > 0) ? task.total_bytes : 1024ULL * 1024ULL;
    }

    // Chunk size: 1 MB
    const uint64_t chunk_size = 1024ULL * 1024ULL;
    uint64_t written = 0;

    while (written < file_size) {
        uint64_t step = std::min(chunk_size, file_size - written);
        written += step;

        if (on_chunk && !on_chunk(written, file_size)) {
            out_code = InstallErrorCode::Cancelled;
            out_error = "Installation cancelled by user";
            return false;
        }
    }

    out_code = InstallErrorCode::None;
    out_error.clear();
    return true;
}

// MockInstallDelegate
void MockInstallDelegate::configure_failure(const std::string& filename,
                                            InstallErrorCode code,
                                            const std::string& message,
                                            size_t fail_at_offset) {
    rules_[filename] = {true, code, message, fail_at_offset};
}

void MockInstallDelegate::reset() {
    rules_.clear();
    installed_count_ = 0;
}

bool MockInstallDelegate::install_package(
    const InstallTask& task,
    std::function<bool(uint64_t written_bytes, uint64_t total_bytes)> on_chunk,
    InstallErrorCode& out_code,
    std::string& out_error) {

    uint64_t total = task.total_bytes > 0 ? task.total_bytes : 10ULL * 1024ULL * 1024ULL;
    uint64_t step = std::max<uint64_t>(1024ULL * 256ULL, total / 10);
    uint64_t written = 0;

    auto it = rules_.find(task.package_filename);
    bool should_fail = (it != rules_.end() && it->second.should_fail);

    while (written < total) {
        written = std::min(total, written + step);

        if (should_fail && written >= it->second.fail_at_byte_offset) {
            out_code = it->second.failure_code;
            out_error = it->second.failure_message;
            return false;
        }

        if (on_chunk && !on_chunk(written, total)) {
            out_code = InstallErrorCode::Cancelled;
            out_error = "Installation cancelled";
            return false;
        }
    }

    installed_count_++;
    out_code = InstallErrorCode::None;
    return true;
}

// InstallPipeline
InstallPipeline::InstallPipeline(std::shared_ptr<IInstallDelegate> delegate)
    : delegate_(delegate ? delegate : std::make_shared<LocalInstallDelegate>()) {}

InstallPipeline::~InstallPipeline() {
    stop_worker();
}

void InstallPipeline::set_delegate(std::shared_ptr<IInstallDelegate> delegate) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    delegate_ = delegate ? delegate : std::make_shared<LocalInstallDelegate>();
}

std::string InstallPipeline::generate_task_id() {
    std::ostringstream ss;
    ss << "task_" << next_id_counter_++;
    return ss.str();
}

std::string InstallPipeline::queue_item(const GameItem& item, const std::string& title_id) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    InstallTask task;
    task.task_id = generate_task_id();
    task.title_id = title_id;
    task.package_filename = item.filename;
    task.display_name = item.display_name.empty() ? item.filename : item.display_name;
    task.source_path = item.absolute_path.empty() ? item.filename : item.absolute_path;
    task.format = item.format;
    task.kind = item.kind;
    task.total_bytes = item.size_bytes;
    task.status = InstallStatus::Queued;

    task_queue_.push_back(task);
    return task.task_id;
}

std::string InstallPipeline::install_all_dlcs(const GameEntry& entry) {
    return queue_batch(entry.title_id,
                       std::string("DLCs for ") + entry.title,
                       ItemKind::Dlc,
                       entry.dlcs);
}

std::string InstallPipeline::queue_batch(const std::string& title_id,
                                        const std::string& batch_title,
                                        ItemKind target_kind,
                                        const std::vector<GameItem>& items) {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    active_batch_ = BatchProgress{};
    active_batch_.batch_id = generate_task_id();
    active_batch_.title_id = title_id;
    active_batch_.title_name = batch_title;
    active_batch_.target_kind = target_kind;
    active_batch_.total_items = items.size();
    active_batch_.is_running = !items.empty();
    active_batch_.total_bytes = 0;

    for (const auto& item : items) {
        InstallTask task;
        task.task_id = generate_task_id();
        task.title_id = title_id;
        task.package_filename = item.filename;
        task.display_name = item.display_name.empty() ? item.filename : item.display_name;
        task.source_path = item.absolute_path.empty() ? item.filename : item.absolute_path;
        task.format = item.format;
        task.kind = item.kind;
        task.total_bytes = item.size_bytes;
        task.status = InstallStatus::Queued;

        active_batch_.total_bytes += item.size_bytes;
        task_queue_.push_back(task);
    }

    return active_batch_.batch_id;
}

void InstallPipeline::cancel_active_task() {
    cancel_requested_ = true;
}

void InstallPipeline::cancel_all() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    cancel_requested_ = true;

    for (auto& task : task_queue_) {
        task.status = InstallStatus::Cancelled;
        task.error_code = InstallErrorCode::Cancelled;
        task.error_message = "Batch cancelled by user";
        task_history_.push_back(task);
        if (active_batch_.is_running) {
            active_batch_.cancelled_items++;
        }
    }
    task_queue_.clear();

    if (active_batch_.is_running) {
        active_batch_.is_cancelled = true;
        active_batch_.is_running = false;
        if (batch_finished_callback_) {
            batch_finished_callback_(active_batch_);
        }
    }
}

bool InstallPipeline::is_busy() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return active_task_.has_value() || !task_queue_.empty();
}

std::optional<InstallTask> InstallPipeline::current_active_task() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return active_task_;
}

BatchProgress InstallPipeline::current_batch_progress() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return active_batch_;
}

std::vector<InstallTask> InstallPipeline::all_tasks() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    std::vector<InstallTask> result = task_history_;
    if (active_task_.has_value()) {
        result.push_back(*active_task_);
    }
    result.insert(result.end(), task_queue_.begin(), task_queue_.end());
    return result;
}

std::optional<InstallTask> InstallPipeline::get_task(const std::string& task_id) const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (active_task_.has_value() && active_task_->task_id == task_id) {
        return *active_task_;
    }
    for (const auto& t : task_queue_) {
        if (t.task_id == task_id) return t;
    }
    for (const auto& t : task_history_) {
        if (t.task_id == task_id) return t;
    }
    return std::nullopt;
}

InstallStatus InstallPipeline::get_item_status(const std::string& source_path) const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (active_task_.has_value() && active_task_->source_path == source_path) {
        return active_task_->status;
    }
    for (const auto& t : task_queue_) {
        if (t.source_path == source_path) return t.status;
    }
    for (auto it = task_history_.rbegin(); it != task_history_.rend(); ++it) {
        if (it->source_path == source_path) return it->status;
    }
    return InstallStatus::Queued;
}

void InstallPipeline::set_progress_callback(TaskProgressCallback cb) {
    progress_callback_ = std::move(cb);
}

void InstallPipeline::set_task_finished_callback(TaskFinishedCallback cb) {
    task_finished_callback_ = std::move(cb);
}

void InstallPipeline::set_batch_finished_callback(BatchFinishedCallback cb) {
    batch_finished_callback_ = std::move(cb);
}

bool InstallPipeline::process_one_step() {
    InstallTask task;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (task_queue_.empty()) {
            return false;
        }
        task = task_queue_.front();
        task_queue_.erase(task_queue_.begin());
        task.status = InstallStatus::InProgress;
        task.start_time = std::chrono::steady_clock::now();
        active_task_ = task;

        if (active_batch_.is_running) {
            active_batch_.current_item_index = active_batch_.completed_items + active_batch_.failed_items + active_batch_.cancelled_items + 1;
            active_batch_.current_item_name = task.display_name;
        }
    }

    cancel_requested_ = false;
    uint64_t task_bytes_start = 0;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task_bytes_start = active_batch_.processed_bytes;
    }

    auto chunk_cb = [this, &task, task_bytes_start](uint64_t written, uint64_t total) -> bool {
        if (cancel_requested_) {
            return false;
        }
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task.processed_bytes = written;
        task.total_bytes = total;
        active_task_ = task;

        if (active_batch_.is_running) {
            active_batch_.processed_bytes = task_bytes_start + written;
        }

        if (progress_callback_) {
            progress_callback_(task, active_batch_);
        }
        return true;
    };

    InstallErrorCode code = InstallErrorCode::None;
    std::string err_msg;
    bool ok = delegate_->install_package(task, chunk_cb, code, err_msg);

    task.finish_time = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (ok) {
            task.status = InstallStatus::Completed;
            task.error_code = InstallErrorCode::None;
            task.processed_bytes = task.total_bytes;
            if (active_batch_.is_running) {
                active_batch_.completed_items++;
            }
        } else if (code == InstallErrorCode::Cancelled || cancel_requested_) {
            task.status = InstallStatus::Cancelled;
            task.error_code = InstallErrorCode::Cancelled;
            task.error_message = err_msg.empty() ? "Cancelled by user" : err_msg;
            if (active_batch_.is_running) {
                active_batch_.cancelled_items++;
            }
        } else {
            task.status = InstallStatus::Failed;
            task.error_code = code;
            task.error_message = err_msg.empty() ? "Installation failed" : err_msg;
            if (active_batch_.is_running) {
                active_batch_.failed_items++;
                active_batch_.has_failures = true;
                active_batch_.failure_messages.push_back(task.display_name + ": " + task.error_message);
            }
        }

        active_task_ = std::nullopt;
        task_history_.push_back(task);

        // Check if batch is completed
        if (active_batch_.is_running) {
            size_t handled = active_batch_.completed_items + active_batch_.failed_items + active_batch_.cancelled_items;
            if (handled >= active_batch_.total_items || task_queue_.empty()) {
                active_batch_.is_running = false;
                active_batch_.is_completed = true;
                if (batch_finished_callback_) {
                    batch_finished_callback_(active_batch_);
                }
            }
        }
    }

    if (task_finished_callback_) {
        task_finished_callback_(task);
    }

    return true;
}

void InstallPipeline::start_worker() {
    if (worker_running_) return;
    worker_running_ = true;
    worker_thread_ = std::thread(&InstallPipeline::worker_loop, this);
}

void InstallPipeline::stop_worker() {
    if (!worker_running_) return;
    worker_running_ = false;
    cancel_requested_ = true;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void InstallPipeline::worker_loop() {
    while (worker_running_) {
        bool processed = process_one_step();
        if (!processed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
}

} // namespace ps5_library
