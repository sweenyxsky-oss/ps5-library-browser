#include "ps5_library/ui_shell.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <nlohmann/json.hpp>

namespace ps5_library {
using nlohmann::json;

UiShell::UiShell(std::shared_ptr<DaemonLock> daemon_client,
                 std::string cache_path,
                 std::string install_state_path)
    : cache_(std::move(cache_path)),
      daemon_client_(daemon_client ? daemon_client : std::make_shared<DaemonLock>()),
      installer_(std::make_shared<InstallPipeline>(nullptr, std::move(install_state_path))) {
    // Attempt fast index load on startup
    std::vector<GameEntry> cached_entries;
    if (cache_.load(cached_entries)) {
        set_entries(std::move(cached_entries));
    }
    setup_pipeline_callbacks();
}

void UiShell::set_installer(std::shared_ptr<InstallPipeline> installer) {
    installer_ = std::move(installer);
    setup_pipeline_callbacks();
}

void UiShell::setup_pipeline_callbacks() {
    if (!installer_) return;

    installer_->set_task_finished_callback([this](const InstallTask& task) {
        if (task.status == InstallStatus::Completed) {
            refresh_after_install(task.title_id, task.source_path);
        } else if (task.status == InstallStatus::Failed || task.status == InstallStatus::Interrupted) {
            if (inspector_) {
                inspector_->sync_with_pipeline(*installer_);
            }
        }
    });

    installer_->set_batch_finished_callback([this](const BatchProgress& batch) {
        (void)batch;
        if (inspector_) {
            inspector_->sync_with_pipeline(*installer_);
        }
    });
}

bool UiShell::is_shadowmount_available() const {
    if (!daemon_client_) return false;
    return daemon_client_->is_unlocked();
}

void UiShell::set_library_mode(LibraryMode mode) {
    if (mode == LibraryMode::LibraryBrowser) {
        if (!is_shadowmount_available()) {
            show_shadowmount_lock();
            return;
        }
    }
    library_mode_ = mode;
    dismiss_dialog();
}

void UiShell::toggle_library_mode() {
    if (library_mode_ == LibraryMode::InstalledGames) {
        set_library_mode(LibraryMode::LibraryBrowser);
    } else {
        set_library_mode(LibraryMode::InstalledGames);
    }
}

void UiShell::toggle_view_layout() {
    if (view_layout_ == ViewLayout::Coverflow) {
        view_layout_ = ViewLayout::BlockGrid;
    } else {
        view_layout_ = ViewLayout::Coverflow;
    }
}

void UiShell::set_format_filter(FormatFilter filter) {
    format_filter_ = filter;
    update_filtered_entries();
}

void UiShell::cycle_next_filter() {
    format_filter_ = next_filter(format_filter_);
    update_filtered_entries();
}

void UiShell::cycle_prev_filter() {
    format_filter_ = prev_filter(format_filter_);
    update_filtered_entries();
}

void UiShell::set_search_query(const std::string& query) {
    search_query_ = query;
    update_filtered_entries();
}

void UiShell::set_scan_path(const std::string& path) {
    current_scan_path_ = path;
}

void UiShell::set_entries(std::vector<GameEntry> entries) {
    all_entries_ = std::move(entries);
    update_filtered_entries();
}

void UiShell::set_focused_index(size_t index) {
    if (filtered_entries_.empty()) {
        focused_index_ = 0;
        return;
    }
    if (index >= filtered_entries_.size()) {
        focused_index_ = filtered_entries_.size() - 1;
    } else {
        focused_index_ = index;
    }
}

std::optional<GameEntry> UiShell::focused_entry() const {
    if (filtered_entries_.empty() || focused_index_ >= filtered_entries_.size()) {
        return std::nullopt;
    }
    return filtered_entries_[focused_index_];
}

void UiShell::open_inspector(const GameEntry& entry) {
    inspector_ = std::make_unique<GameInspector>(entry);
    if (installer_) {
        inspector_->sync_with_pipeline(*installer_);
    }
}

void UiShell::close_inspector() {
    inspector_.reset();
}

void UiShell::show_shadowmount_lock() {
    dialog_model_.type = DialogType::ShadowMountLock;
    dialog_model_.title = "Feature Locked";
    dialog_model_.message = "ShadowMount Plus is required to unlock these features.";
    dialog_model_.secondary_text = "Ensure the ShadowMountPlus daemon is running on port 10101 to enable remote library browsing.";
    dialog_model_.options = {"Retry Connection", "Return to Installed Games"};
    dialog_model_.selected_option = 0;
    dialog_model_.extra_payload.clear();
}

void UiShell::show_recovery_guidance(const std::string& title,
                                     const std::string& message,
                                     const std::string& steps) {
    dialog_model_.type = DialogType::RecoveryGuidance;
    dialog_model_.title = title.empty() ? "Installation Troubleshooting" : title;
    dialog_model_.message = message;
    dialog_model_.secondary_text = steps;
    dialog_model_.options = {"Dismiss", "Retry Install"};
    dialog_model_.selected_option = 0;
    dialog_model_.extra_payload = steps;
}

void UiShell::show_install_history() {
    dialog_model_.type = DialogType::InstallHistory;
    dialog_model_.title = "Install History & Diagnostics";
    history_selected_index_ = 0;
    update_history_view();
}

void UiShell::show_retry_confirmation(const InstallTask& task) {
    dialog_model_.type = DialogType::RetryConfirmation;
    dialog_model_.title = "Confirm Retry: " + task.display_name;

    std::string target_dir = installer_ ? installer_->target_storage_dir() : "/user/app";
    const auto preflight = run_package_preflight(task.source_path, target_dir);

    std::ostringstream ss;
    ss << "Original Failure Details:\n";
    ss << "- Task ID: " << task.task_id << " (Attempt #" << (task.retry_count + 1) << ")\n";
    ss << "- Failure Code: [" << install_error_name(task.error_code) << "] " << to_string(task.error_code) << "\n";
    if (!task.error_message.empty()) {
        ss << "- Error Message: " << task.error_message << "\n";
    }
    if (!task.completed_timestamp.empty()) {
        ss << "- Failed Timestamp: " << task.completed_timestamp << "\n";
    }
    ss << "\n" << preflight.summary.formatted_summary();

    dialog_model_.message = preflight.passed
        ? "Preflight checks passed on re-run. Proceed with job retry?"
        : "Preflight checks failed on re-run: " + preflight.error_message;
    dialog_model_.secondary_text = ss.str();

    if (preflight.passed) {
        dialog_model_.options = {"Confirm Retry", "Cancel"};
    } else {
        dialog_model_.options = {"Dismiss"};
    }
    dialog_model_.selected_option = 0;

    json payload;
    payload["task_id"] = task.task_id;
    payload["source_path"] = task.source_path;
    payload["passed"] = preflight.passed;
    dialog_model_.extra_payload = payload.dump();
}

void UiShell::show_preflight_summary(const GameItem& item, const std::string& title_id) {
    dialog_model_.type = DialogType::PreflightSummary;
    dialog_model_.title = "Preflight Inspection: " + item.filename;

    std::string target_dir = installer_ ? installer_->target_storage_dir() : "/user/app";
    const auto preflight = run_package_preflight(item.absolute_path, target_dir);

    dialog_model_.message = preflight.passed
        ? "All preflight checks passed. Package is ready for installation."
        : "Preflight check failed: " + preflight.error_message;
    dialog_model_.secondary_text = preflight.summary.formatted_summary();

    if (preflight.passed) {
        dialog_model_.options = {"Proceed with Install", "Cancel"};
    } else {
        dialog_model_.options = {"Dismiss"};
    }
    dialog_model_.selected_option = 0;

    json payload;
    payload["title_id"] = title_id;
    payload["filename"] = item.filename;
    payload["absolute_path"] = item.absolute_path;
    payload["format"] = item.format;
    payload["size_bytes"] = item.size_bytes;
    payload["display_name"] = item.display_name;
    payload["version"] = item.version;
    payload["kind"] = to_string(item.kind);
    payload["passed"] = preflight.passed;
    dialog_model_.extra_payload = payload.dump();
}

void UiShell::show_preflight_summary(const std::string& package_path, const std::string& title_id, const std::string& display_name) {
    GameItem item;
    item.filename = package_path.substr(package_path.find_last_of("/\\") + 1);
    item.absolute_path = package_path;
    item.display_name = display_name.empty() ? item.filename : display_name;
    auto dot = item.filename.find_last_of('.');
    if (dot != std::string::npos) item.format = item.filename.substr(dot + 1);
    show_preflight_summary(item, title_id);
}

void UiShell::update_history_view() {
    if (!installer_) {
        history_items_.clear();
        dialog_model_.message = "No installer pipeline connected";
        dialog_model_.secondary_text.clear();
        return;
    }

    history_filter_options_.query = history_search_query_;
    history_items_ = installer_->filter_and_sort_history(history_filter_options_);

    std::string badge = "[Status: " + (history_filter_options_.outcome_filter ? std::string(to_string(*history_filter_options_.outcome_filter)) : "All") +
                        " | Format: " + (history_filter_options_.format_filter.empty() ? "All" : history_filter_options_.format_filter) +
                        " | Sort: " + to_string(history_filter_options_.sort_field) + " " + to_string(history_filter_options_.sort_order) + "]";

    if (history_items_.empty()) {
        history_selected_index_ = 0;
        dialog_model_.message = history_search_query_.empty() ? "No install jobs recorded" : "No jobs matching query: " + history_search_query_;
        dialog_model_.secondary_text = badge + "\n△ Filter Status | □ Filter Format | L1/R1 Sort | ○ Close";
        return;
    }

    if (history_selected_index_ >= history_items_.size()) {
        history_selected_index_ = history_items_.size() - 1;
    }

    const auto& selected = history_items_[history_selected_index_];
    dialog_model_.message = "[" + std::to_string(history_selected_index_ + 1) + "/" + std::to_string(history_items_.size()) + "] " +
                            selected.display_name + " (" + selected.title_id + ") - " + to_string(selected.status);

    std::string sec = badge + "\nFile: " + selected.package_filename + " | Format: " + selected.format + "\n";
    sec += "Created: " + (selected.created_timestamp.empty() ? "Unknown" : selected.created_timestamp);
    if (!selected.completed_timestamp.empty()) {
        sec += " | Completed: " + selected.completed_timestamp;
    }
    if (selected.retry_count > 0) {
        sec += "\nRetry #" + std::to_string(selected.retry_count) + " (Orig Task: " + selected.original_task_id +
               ", Orig Error: [" + install_error_name(selected.original_error_code) + "])";
    }
    if (!selected.error_message.empty()) {
        sec += "\nError: " + selected.error_message;
    }
    if (!selected.recovery_guidance.empty()) {
        sec += "\nRecovery: " + selected.recovery_guidance;
    }
    dialog_model_.secondary_text = sec;
}

void UiShell::history_select_next() {
    if (history_items_.empty()) return;
    history_selected_index_ = (history_selected_index_ + 1) % history_items_.size();
    update_history_view();
}

void UiShell::history_select_prev() {
    if (history_items_.empty()) return;
    history_selected_index_ = (history_selected_index_ + history_items_.size() - 1) % history_items_.size();
    update_history_view();
}

void UiShell::set_history_search_query(const std::string& query) {
    history_search_query_ = query;
    history_selected_index_ = 0;
    update_history_view();
}

std::optional<InstallTask> UiShell::selected_history_item() const {
    if (history_selected_index_ < history_items_.size()) {
        return history_items_[history_selected_index_];
    }
    return std::nullopt;
}

void UiShell::set_history_filter_options(const HistoryFilterOptions& options) {
    history_filter_options_ = options;
    history_selected_index_ = 0;
    update_history_view();
}

void UiShell::set_history_outcome_filter(std::optional<InstallStatus> outcome) {
    history_filter_options_.outcome_filter = outcome;
    history_selected_index_ = 0;
    update_history_view();
}

void UiShell::set_history_format_filter(const std::string& format) {
    history_filter_options_.format_filter = format;
    history_selected_index_ = 0;
    update_history_view();
}

void UiShell::set_history_date_range(const std::string& from, const std::string& to) {
    history_filter_options_.date_from = from;
    history_filter_options_.date_to = to;
    history_selected_index_ = 0;
    update_history_view();
}

void UiShell::set_history_sort(HistorySortField field, HistorySortOrder order) {
    history_filter_options_.sort_field = field;
    history_filter_options_.sort_order = order;
    history_selected_index_ = 0;
    update_history_view();
}

void UiShell::cycle_history_outcome_filter() {
    if (!history_filter_options_.outcome_filter.has_value()) {
        history_filter_options_.outcome_filter = InstallStatus::Failed;
    } else if (*history_filter_options_.outcome_filter == InstallStatus::Failed) {
        history_filter_options_.outcome_filter = InstallStatus::Interrupted;
    } else if (*history_filter_options_.outcome_filter == InstallStatus::Interrupted) {
        history_filter_options_.outcome_filter = InstallStatus::Completed;
    } else if (*history_filter_options_.outcome_filter == InstallStatus::Completed) {
        history_filter_options_.outcome_filter = InstallStatus::Queued;
    } else {
        history_filter_options_.outcome_filter = std::nullopt;
    }
    history_selected_index_ = 0;
    update_history_view();
}

void UiShell::cycle_history_format_filter() {
    if (history_filter_options_.format_filter.empty()) {
        history_filter_options_.format_filter = "pkg";
    } else if (history_filter_options_.format_filter == "pkg") {
        history_filter_options_.format_filter = "ffpkg";
    } else if (history_filter_options_.format_filter == "ffpkg") {
        history_filter_options_.format_filter = "exfat";
    } else {
        history_filter_options_.format_filter.clear();
    }
    history_selected_index_ = 0;
    update_history_view();
}

void UiShell::cycle_history_sort() {
    if (history_filter_options_.sort_field == HistorySortField::Timestamp &&
        history_filter_options_.sort_order == HistorySortOrder::Descending) {
        history_filter_options_.sort_order = HistorySortOrder::Ascending;
    } else if (history_filter_options_.sort_field == HistorySortField::Timestamp &&
               history_filter_options_.sort_order == HistorySortOrder::Ascending) {
        history_filter_options_.sort_field = HistorySortField::PackageName;
        history_filter_options_.sort_order = HistorySortOrder::Ascending;
    } else if (history_filter_options_.sort_field == HistorySortField::PackageName &&
               history_filter_options_.sort_order == HistorySortOrder::Ascending) {
        history_filter_options_.sort_order = HistorySortOrder::Descending;
    } else {
        history_filter_options_.sort_field = HistorySortField::Timestamp;
        history_filter_options_.sort_order = HistorySortOrder::Descending;
    }
    history_selected_index_ = 0;
    update_history_view();
}

std::string UiShell::export_history_json(int indent) const {
    if (!installer_) return "[]";
    return installer_->export_history_to_json(history_items_, indent);
}

std::string UiShell::export_history_csv() const {
    if (!installer_) return "";
    return installer_->export_history_to_csv(history_items_);
}

bool UiShell::export_history_to_file(const std::string& filepath, bool as_json) const {
    if (!installer_) return false;
    return installer_->export_history_to_file(filepath, history_items_, as_json);
}

void UiShell::dismiss_dialog() {
    dialog_model_.type = DialogType::None;
    dialog_model_.title.clear();
    dialog_model_.message.clear();
    dialog_model_.secondary_text.clear();
    dialog_model_.options.clear();
    dialog_model_.selected_option = 0;
    dialog_model_.extra_payload.clear();
}

bool UiShell::is_title_installed(const std::string& title_id) const {
    if (title_id.empty()) return false;
    for (const auto& entry : all_entries_) {
        if (entry.title_id == title_id) {
            return true;
        }
    }
    return false;
}

void UiShell::refresh_after_install(const std::string& title_id, const std::string& installed_path) {
    (void)installed_path;
    for (auto& entry : all_entries_) {
        if (entry.title_id == title_id) {
            entry.confidence = MetadataConfidence::Verified;
            entry.calculate_totals();

            if (inspector_ && inspector_->title_id() == title_id) {
                inspector_->update_entry(entry);
                if (installer_) {
                    inspector_->sync_with_pipeline(*installer_);
                }
            }
            break;
        }
    }

    cache_.save_atomic(all_entries_, current_scan_path_);
    update_filtered_entries();

    if (inspector_ && installer_) {
        inspector_->sync_with_pipeline(*installer_);
    }
}

void UiShell::update_filtered_entries() {
    std::string fmt_str;
    switch (format_filter_) {
        case FormatFilter::Pkg:    fmt_str = "pkg"; break;
        case FormatFilter::ExFat:  fmt_str = "exfat"; break;
        case FormatFilter::FfPkg:  fmt_str = "ffpkg"; break;
        case FormatFilter::Folder: fmt_str = "folder"; break;
        default:                   fmt_str.clear(); break;
    }

    std::vector<GameEntry> temp;
    if (fmt_str.empty()) {
        temp = all_entries_;
    } else {
        temp = IndexCache::filter_by_format(all_entries_, fmt_str);
    }

    if (!search_query_.empty()) {
        filtered_entries_ = IndexCache::search(temp, search_query_);
    } else {
        filtered_entries_ = std::move(temp);
    }

    if (filtered_entries_.empty()) {
        focused_index_ = 0;
    } else if (focused_index_ >= filtered_entries_.size()) {
        focused_index_ = filtered_entries_.size() - 1;
    }
}

bool UiShell::handle_pad_input(const InputManager& input) {
    if (dialog_model_.type != DialogType::None) {
        if (dialog_model_.type == DialogType::ShadowMountLock) {
            if (input.action_triggered(LogicalAction::Confirm)) {
                if (daemon_client_) {
                    daemon_client_->probe();
                }
                if (is_shadowmount_available()) {
                    dismiss_dialog();
                    library_mode_ = LibraryMode::LibraryBrowser;
                }
                return true;
            }
            if (input.action_triggered(LogicalAction::Cancel)) {
                dismiss_dialog();
                library_mode_ = LibraryMode::InstalledGames;
                return true;
            }
        } else if (dialog_model_.type == DialogType::RecoveryGuidance) {
            if (input.action_triggered(LogicalAction::Confirm) || input.action_triggered(LogicalAction::Cancel)) {
                dismiss_dialog();
                return true;
            }
        } else if (dialog_model_.type == DialogType::InstallHistory) {
            if (input.action_triggered(LogicalAction::Cancel)) {
                dismiss_dialog();
                return true;
            }
            if (input.action_triggered(LogicalAction::NavDown)) {
                history_select_next();
                return true;
            }
            if (input.action_triggered(LogicalAction::NavUp)) {
                history_select_prev();
                return true;
            }
            if (input.action_triggered(LogicalAction::ToggleMode)) {
                cycle_history_outcome_filter();
                return true;
            }
            if (input.action_triggered(LogicalAction::Inspect)) {
                cycle_history_format_filter();
                return true;
            }
            if (input.action_triggered(LogicalAction::PrevFilter) || input.action_triggered(LogicalAction::NextFilter)) {
                cycle_history_sort();
                return true;
            }
            if (input.action_triggered(LogicalAction::Confirm)) {
                const auto selected = selected_history_item();
                if (selected && (selected->status == InstallStatus::Failed || selected->status == InstallStatus::Interrupted)) {
                    show_retry_confirmation(*selected);
                    return true;
                }
                return true;
            }
        } else if (dialog_model_.type == DialogType::RetryConfirmation) {
            if (input.action_triggered(LogicalAction::Cancel)) {
                dismiss_dialog();
                return true;
            }
            if (input.action_triggered(LogicalAction::NavLeft) || input.action_triggered(LogicalAction::NavRight)) {
                if (!dialog_model_.options.empty()) {
                    dialog_model_.selected_option = (dialog_model_.selected_option + 1) % dialog_model_.options.size();
                }
                return true;
            }
            if (input.action_triggered(LogicalAction::Confirm)) {
                if (!dialog_model_.options.empty() && dialog_model_.options[dialog_model_.selected_option] == "Confirm Retry") {
                    try {
                        json payload = json::parse(dialog_model_.extra_payload);
                        std::string task_id = payload.value("task_id", "");
                        if (installer_ && !task_id.empty()) {
                            installer_->retry_task(task_id);
                            update_history_view();
                            if (is_inspector_open()) {
                                inspector_->sync_with_pipeline(*installer_);
                            }
                        }
                    } catch (...) {}
                }
                dismiss_dialog();
                return true;
            }
        } else if (dialog_model_.type == DialogType::PreflightSummary) {
            if (input.action_triggered(LogicalAction::Cancel)) {
                dismiss_dialog();
                return true;
            }
            if (input.action_triggered(LogicalAction::NavLeft) || input.action_triggered(LogicalAction::NavRight)) {
                if (!dialog_model_.options.empty()) {
                    dialog_model_.selected_option = (dialog_model_.selected_option + 1) % dialog_model_.options.size();
                }
                return true;
            }
            if (input.action_triggered(LogicalAction::Confirm)) {
                if (!dialog_model_.options.empty() && dialog_model_.options[dialog_model_.selected_option] == "Proceed with Install") {
                    try {
                        json payload = json::parse(dialog_model_.extra_payload);
                        GameItem item;
                        item.filename = payload.value("filename", "");
                        item.absolute_path = payload.value("absolute_path", "");
                        item.format = payload.value("format", "");
                        item.display_name = payload.value("display_name", "");
                        item.size_bytes = payload.value("size_bytes", uint64_t{0});
                        item.size_known = item.size_bytes > 0;
                        item.formatted_size = format_file_size(item.size_bytes);
                        item.version = payload.value("version", "");
                        item.kind = parse_item_kind(payload.value("kind", "Unknown"));
                        std::string title_id = payload.value("title_id", "");
                        if (installer_) {
                            installer_->queue_item(item, title_id);
                            if (is_inspector_open()) {
                                inspector_->sync_with_pipeline(*installer_);
                            }
                        }
                    } catch (...) {}
                }
                dismiss_dialog();
                return true;
            }
        } else {
            if (input.action_triggered(LogicalAction::Cancel)) {
                dismiss_dialog();
                return true;
            }
        }
        return true;
    }

    if (is_inspector_open()) {
        if (input.action_triggered(LogicalAction::Cancel)) {
            close_inspector();
            return true;
        }

        if (input.action_triggered(LogicalAction::Inspect)) {
            const auto item = inspector_->selected_item();
            if (item.has_value() && (item->install_status == InstallStatus::Failed ||
                                     item->install_status == InstallStatus::Interrupted)) {
                show_recovery_guidance("Troubleshooting: " + item->filename,
                                       item->error_message,
                                       item->recovery_guidance);
                return true;
            }
        }

        if (preflight_summary_before_dispatch_ && input.action_triggered(LogicalAction::Confirm)) {
            if (inspector_->focus_section() == InspectorFocusSection::Actions) {
                const auto labels = inspector_->available_action_labels();
                if (inspector_->selected_action_button() < labels.size()) {
                    const std::string& label = labels[inspector_->selected_action_button()];
                    if (label == "Retry Install") {
                        const auto selected = inspector_->selected_item();
                        if (selected && selected->can_retry && installer_) {
                            const auto task = installer_->get_task_by_path(selected->absolute_path);
                            if (task) {
                                show_retry_confirmation(*task);
                                return true;
                            }
                        }
                    } else if (label == "Install Selected") {
                        const auto selected = inspector_->selected_item();
                        if (selected && !selected->has_active_task) {
                            GameItem item;
                            item.kind = selected->kind;
                            item.filename = selected->filename;
                            item.absolute_path = selected->absolute_path;
                            item.format = selected->extension.empty() ? "" : (selected->extension[0] == '.' ? selected->extension.substr(1) : selected->extension);
                            item.size_bytes = selected->raw_size_bytes;
                            item.size_known = selected->formatted_size != "Unknown";
                            item.formatted_size = selected->formatted_size;
                            item.version = selected->version;
                            item.display_name = selected->display_name;
                            show_preflight_summary(item, inspector_->title_id());
                            return true;
                        }
                    }
                }
            }
        }

        bool handled = inspector_->handle_pad_input(input, *installer_);
        if (installer_) inspector_->sync_with_pipeline(*installer_);
        return handled;
    }

    if (input.action_triggered(LogicalAction::ToggleMode)) {
        toggle_library_mode();
        return true;
    }

    if (input.action_triggered(LogicalAction::SwitchLayout)) {
        toggle_view_layout();
        return true;
    }

    if (input.action_triggered(LogicalAction::PrevFilter)) {
        cycle_prev_filter();
        return true;
    }
    if (input.action_triggered(LogicalAction::NextFilter)) {
        cycle_next_filter();
        return true;
    }

    if (input.action_triggered(LogicalAction::OpenSearch)) {
        if (!search_query_.empty()) {
            set_search_query("");
        } else {
            dialog_model_.type = DialogType::SearchFilter;
            dialog_model_.title = "Search Library";
            dialog_model_.message = "Enter title, Title ID, or package keyword";
        }
        return true;
    }

    if (filtered_entries_.empty()) {
        return false;
    }

    if (view_layout_ == ViewLayout::Coverflow) {
        if (input.action_triggered(LogicalAction::NavLeft)) {
            if (focused_index_ > 0) {
                focused_index_--;
            }
            return true;
        }
        if (input.action_triggered(LogicalAction::NavRight)) {
            if (focused_index_ + 1 < filtered_entries_.size()) {
                focused_index_++;
            }
            return true;
        }
    } else if (view_layout_ == ViewLayout::BlockGrid) {
        int dx = 0;
        int dy = 0;
        if (input.action_triggered(LogicalAction::NavLeft)) dx = -1;
        else if (input.action_triggered(LogicalAction::NavRight)) dx = 1;
        else if (input.action_triggered(LogicalAction::NavUp)) dy = -1;
        else if (input.action_triggered(LogicalAction::NavDown)) dy = 1;

        if (dx != 0 || dy != 0) {
            focused_index_ = ViewLayoutEngine::move_grid_focus(focused_index_, dx, dy, filtered_entries_.size(), 5);
            return true;
        }
    }

    if (input.action_triggered(LogicalAction::Inspect)) {
        if (auto entry = focused_entry()) {
            open_inspector(*entry);
            return true;
        }
    }

    if (input.action_triggered(LogicalAction::Confirm)) {
        if (auto entry = focused_entry()) {
            open_inspector(*entry);
            return true;
        }
    }

    return false;
}

std::vector<CardVisual> UiShell::get_current_view_cards() const {
    if (view_layout_ == ViewLayout::Coverflow) {
        return layout_engine_.compute_coverflow(filtered_entries_, focused_index_);
    } else {
        return layout_engine_.compute_block_grid(filtered_entries_, focused_index_, 5);
    }
}

std::vector<ButtonPrompt> UiShell::get_contextual_prompts() const {
    if (is_inspector_open()) {
        auto prompts = inspector_->get_contextual_prompts();
        const auto item = inspector_->selected_item();
        if (item.has_value() && (item->install_status == InstallStatus::Failed ||
                                 item->install_status == InstallStatus::Interrupted)) {
            prompts.push_back({ButtonGlyph::Square, "Troubleshoot / Recovery", true, false});
        }
        return prompts;
    }

    std::vector<ButtonPrompt> prompts;

    if (dialog_model_.type == DialogType::ShadowMountLock) {
        prompts.push_back({ButtonGlyph::Cross, "Retry Connection", true, true});
        prompts.push_back({ButtonGlyph::Circle, "Installed Games", true, false});
        return prompts;
    }

    if (dialog_model_.type == DialogType::RecoveryGuidance) {
        prompts.push_back({ButtonGlyph::Cross, "Dismiss Guidance", true, true});
        return prompts;
    }

    if (dialog_model_.type == DialogType::InstallHistory) {
        prompts.push_back({ButtonGlyph::DPadUpDown, "Select Job", true, false});
        prompts.push_back({ButtonGlyph::Cross, "Retry Selected", true, true});
        prompts.push_back({ButtonGlyph::Triangle, "Filter Status", true, false});
        prompts.push_back({ButtonGlyph::Square, "Filter Format", true, false});
        prompts.push_back({ButtonGlyph::L1, "Sort (L1/R1)", true, false});
        prompts.push_back({ButtonGlyph::Circle, "Close History", true, false});
        return prompts;
    }

    if (dialog_model_.type == DialogType::RetryConfirmation) {
        if (!dialog_model_.options.empty() && dialog_model_.options[0] == "Confirm Retry") {
            prompts.push_back({ButtonGlyph::Cross, "Confirm Retry", true, true});
            prompts.push_back({ButtonGlyph::Circle, "Cancel", true, false});
        } else {
            prompts.push_back({ButtonGlyph::Cross, "Dismiss", true, true});
            prompts.push_back({ButtonGlyph::Circle, "Close", true, false});
        }
        return prompts;
    }

    if (dialog_model_.type == DialogType::PreflightSummary) {
        if (!dialog_model_.options.empty() && dialog_model_.options[0] == "Proceed with Install") {
            prompts.push_back({ButtonGlyph::Cross, "Proceed Install", true, true});
            prompts.push_back({ButtonGlyph::Circle, "Cancel", true, false});
        } else {
            prompts.push_back({ButtonGlyph::Cross, "Dismiss", true, true});
            prompts.push_back({ButtonGlyph::Circle, "Close", true, false});
        }
        return prompts;
    }

    if (dialog_model_.type != DialogType::None) {
        prompts.push_back({ButtonGlyph::Cross, "Select", true, true});
        prompts.push_back({ButtonGlyph::Circle, "Close", true, false});
        return prompts;
    }

    std::string mode_label = (library_mode_ == LibraryMode::InstalledGames)
                             ? "Library Browser" : "Installed Games";
    prompts.push_back({ButtonGlyph::Triangle, mode_label, true, false});

    std::string layout_label = (view_layout_ == ViewLayout::Coverflow)
                               ? "Grid View" : "Coverflow";
    prompts.push_back({ButtonGlyph::Touchpad, layout_label, true, false});

    std::string filter_label = std::string("Filter: ") + to_string(format_filter_);
    prompts.push_back({ButtonGlyph::L1, filter_label, true, false});

    if (!filtered_entries_.empty()) {
        prompts.push_back({ButtonGlyph::Square, "Inspect Game", true, false});
        prompts.push_back({ButtonGlyph::Cross, "Select / Launch", true, true});
    }

    prompts.push_back({ButtonGlyph::L3, search_query_.empty() ? "Search" : "Clear Search", true, false});

    return prompts;
}

void UiShell::trigger_scan(bool force_rescan,
                           std::function<void(bool success)> on_complete) {
    (void)force_rescan;
    if (scanner_.is_scanning()) return;

    ScanOptions options;
    options.root_path = current_scan_path_;
    options.recursive = true;

    scanner_.start_async_scan(options, nullptr, [this, on_complete](std::vector<GameEntry> entries, ScanStatistics stats) {
        (void)stats;
        set_entries(std::move(entries));
        cache_.save_atomic(all_entries_, current_scan_path_);
        if (on_complete) {
            on_complete(true);
        }
    });
}

} // namespace ps5_library
