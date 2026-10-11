#include "ps5_library/ui_shell.hpp"

#include <algorithm>
#include <cctype>

namespace ps5_library {

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
            // Keep inspector in sync if open
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
    // 1. Refresh entry inside all_entries_ if present
    for (auto& entry : all_entries_) {
        if (entry.title_id == title_id) {
            entry.confidence = MetadataConfidence::Verified;
            entry.calculate_totals();

            // 2. If inspector is currently viewing this title, update its model
            if (inspector_ && inspector_->title_id() == title_id) {
                inspector_->update_entry(entry);
                if (installer_) {
                    inspector_->sync_with_pipeline(*installer_);
                }
            }
            break;
        }
    }

    // 3. Persist updated cache
    cache_.save_atomic(all_entries_, current_scan_path_);
    update_filtered_entries();

    if (inspector_ && installer_) {
        inspector_->sync_with_pipeline(*installer_);
    }
}

void UiShell::update_filtered_entries() {
    // 1. Format filter
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

    // 2. Search query filter
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
    // Handle dialog input first
    if (dialog_model_.type != DialogType::None) {
        if (dialog_model_.type == DialogType::ShadowMountLock) {
            if (input.action_triggered(LogicalAction::Confirm)) {
                // Retry probe
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
        } else {
            if (input.action_triggered(LogicalAction::Cancel)) {
                dismiss_dialog();
                return true;
            }
        }
        return true;
    }

    // If inspector is open, delegate input to inspector
    if (is_inspector_open()) {
        if (input.action_triggered(LogicalAction::Cancel)) {
            close_inspector();
            return true;
        }

        // Check if user pressed Inspect (Square) on a failed package to view recovery steps
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

        bool handled = inspector_->handle_pad_input(input, *installer_);
        inspector_->sync_with_pipeline(*installer_);
        return handled;
    }

    // Main screen input handling
    // 1. Mode toggle (Triangle)
    if (input.action_triggered(LogicalAction::ToggleMode)) {
        toggle_library_mode();
        return true;
    }

    // 2. Layout toggle (Touchpad / Options)
    if (input.action_triggered(LogicalAction::SwitchLayout)) {
        toggle_view_layout();
        return true;
    }

    // 3. Format filters (L1 / R1)
    if (input.action_triggered(LogicalAction::PrevFilter)) {
        cycle_prev_filter();
        return true;
    }
    if (input.action_triggered(LogicalAction::NextFilter)) {
        cycle_next_filter();
        return true;
    }

    // 4. Search toggle (L3 / R3)
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

    // 5. Navigation based on active view layout
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

    // 6. Inspect Game (Square)
    if (input.action_triggered(LogicalAction::Inspect)) {
        if (auto entry = focused_entry()) {
            open_inspector(*entry);
            return true;
        }
    }

    // 7. Confirm / Launch (Cross)
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
        // If selected item is failed, add prompt for Recovery Guidance
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

    if (dialog_model_.type != DialogType::None) {
        prompts.push_back({ButtonGlyph::Cross, "Select", true, true});
        prompts.push_back({ButtonGlyph::Circle, "Close", true, false});
        return prompts;
    }

    // Mode toggle prompt
    std::string mode_label = (library_mode_ == LibraryMode::InstalledGames)
                             ? "Library Browser" : "Installed Games";
    prompts.push_back({ButtonGlyph::Triangle, mode_label, true, false});

    // View layout prompt
    std::string layout_label = (view_layout_ == ViewLayout::Coverflow)
                               ? "Grid View" : "Coverflow";
    prompts.push_back({ButtonGlyph::Touchpad, layout_label, true, false});

    // Format filter prompt
    std::string filter_label = std::string("Filter: ") + to_string(format_filter_);
    prompts.push_back({ButtonGlyph::L1, filter_label, true, false});

    // Game action prompts if games available
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
        // Save to atomic index cache
        cache_.save_atomic(all_entries_, current_scan_path_);
        if (on_complete) {
            on_complete(true);
        }
    });
}

} // namespace ps5_library
