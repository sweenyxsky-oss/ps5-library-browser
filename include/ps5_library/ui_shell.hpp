#pragma once

#include "game_metadata.hpp"
#include "scanner.hpp"
#include "index_cache.hpp"
#include "daemon_client.hpp"
#include "input.hpp"
#include "views.hpp"
#include "installer.hpp"
#include "inspector.hpp"

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <functional>

namespace ps5_library {

enum class DialogType {
    None,
    ShadowMountLock,
    FolderPicker,
    SearchFilter,
    Notification,
    RecoveryGuidance,
    InstallHistory,
    RetryConfirmation,
    PreflightSummary,
    ExportFilePicker
};

struct DialogModel {
    DialogType type{DialogType::None};
    std::string title;
    std::string message;
    std::string secondary_text;
    std::vector<std::string> options;
    int selected_option{0};
    std::string extra_payload;
};

class UiShell {
public:
    UiShell(std::shared_ptr<DaemonLock> daemon_client = nullptr,
            std::string cache_path = "/tmp/ps5_library_cache.json",
            std::string install_state_path = "");

    // Mode and Layout getters/setters
    LibraryMode library_mode() const { return library_mode_; }
    void set_library_mode(LibraryMode mode);
    void toggle_library_mode();

    ViewLayout view_layout() const { return view_layout_; }
    void set_view_layout(ViewLayout layout) { view_layout_ = layout; }
    void toggle_view_layout();

    FormatFilter format_filter() const { return format_filter_; }
    void set_format_filter(FormatFilter filter);
    void cycle_next_filter();
    void cycle_prev_filter();

    const std::string& search_query() const { return search_query_; }
    void set_search_query(const std::string& query);

    // Active path
    const std::string& current_scan_path() const { return current_scan_path_; }
    void set_scan_path(const std::string& path);

    // Data management
    void set_entries(std::vector<GameEntry> entries);
    const std::vector<GameEntry>& all_entries() const { return all_entries_; }
    const std::vector<GameEntry>& filtered_entries() const { return filtered_entries_; }

    size_t focused_index() const { return focused_index_; }
    void set_focused_index(size_t index);
    std::optional<GameEntry> focused_entry() const;

    // Inspector state and controls
    bool is_inspector_open() const { return inspector_ != nullptr; }
    GameInspector* inspector() { return inspector_.get(); }
    const GameInspector* inspector() const { return inspector_.get(); }
    void open_inspector(const GameEntry& entry);
    void close_inspector();

    // Installer pipeline access
    std::shared_ptr<InstallPipeline> installer() const { return installer_; }
    void set_installer(std::shared_ptr<InstallPipeline> installer);

    // Dialog state
    DialogType active_dialog() const { return dialog_model_.type; }
    const DialogModel& dialog_model() const { return dialog_model_; }
    void show_shadowmount_lock();
    void show_recovery_guidance(const std::string& title, const std::string& message, const std::string& steps);
    void show_install_history();
    void show_retry_confirmation(const InstallTask& task);
    void show_preflight_summary(const GameItem& item, const std::string& title_id);
    void show_preflight_summary(const std::string& package_path, const std::string& title_id, const std::string& display_name = "");
    void dismiss_dialog();

    // Install History navigation, filtering, and sorting
    const std::vector<InstallTask>& history_items() const { return history_items_; }
    size_t history_selected_index() const { return history_selected_index_; }
    void history_select_next();
    void history_select_prev();
    void set_history_search_query(const std::string& query);
    const std::string& history_search_query() const { return history_search_query_; }
    std::optional<InstallTask> selected_history_item() const;

    const HistoryFilterOptions& history_filter_options() const { return history_filter_options_; }
    void set_history_filter_options(const HistoryFilterOptions& options);
    void set_history_outcome_filter(std::optional<InstallStatus> outcome);
    void set_history_format_filter(const std::string& format);
    void set_history_date_range(const std::string& from, const std::string& to);
    void set_history_sort(HistorySortField field, HistorySortOrder order);
    void cycle_history_outcome_filter();
    void cycle_history_format_filter();
    void cycle_history_sort();

    // History export
    std::string export_history_json(int indent = 2) const;
    std::string export_history_csv() const;
    bool export_history_to_file(const std::string& filepath, bool as_json = true) const;

    // Export file picker and configurable filenames
    void open_export_file_picker(bool as_json = true);
    void set_export_directory(const std::string& dir);
    void set_export_filename(const std::string& filename);
    void set_export_as_json(bool as_json);
    const std::string& export_directory() const { return export_directory_; }
    const std::string& export_filename() const { return export_filename_; }
    bool export_as_json() const { return export_as_json_; }
    std::string export_full_path() const;
    void cycle_export_directory();
    void cycle_export_filename_preset();
    void toggle_export_format();
    bool execute_export();

    // Preflight summary preference before dispatch
    bool preflight_summary_before_dispatch() const { return preflight_summary_before_dispatch_; }
    void set_preflight_summary_before_dispatch(bool enable) { preflight_summary_before_dispatch_ = enable; }

    // Input processing
    bool handle_pad_input(const InputManager& input);

    // Rendering output models
    std::vector<CardVisual> get_current_view_cards() const;
    std::vector<ButtonPrompt> get_contextual_prompts() const;

    // Scanner integration
    void trigger_scan(bool force_rescan = false,
                      std::function<void(bool success)> on_complete = nullptr);
    bool is_scanning() const { return scanner_.is_scanning(); }

    // Daemon client access
    bool is_shadowmount_available() const;

    // View layout engine access
    ViewLayoutEngine& layout_engine() { return layout_engine_; }
    const ViewLayoutEngine& layout_engine() const { return layout_engine_; }

    // Post-install refresh: updates index cache and any open inspector instance
    void refresh_after_install(const std::string& title_id, const std::string& installed_path);

    // Check if an entry is installed locally
    bool is_title_installed(const std::string& title_id) const;

private:
    LibraryMode library_mode_{LibraryMode::InstalledGames};
    ViewLayout view_layout_{ViewLayout::Coverflow};
    FormatFilter format_filter_{FormatFilter::All};
    std::string search_query_;
    std::string current_scan_path_{"/hostapp"};

    std::vector<GameEntry> all_entries_;
    std::vector<GameEntry> filtered_entries_;
    size_t focused_index_{0};

    DialogModel dialog_model_;

    // Install History dialog state
    std::string history_search_query_;
    size_t history_selected_index_{0};
    std::vector<InstallTask> history_items_;
    HistoryFilterOptions history_filter_options_;
    bool preflight_summary_before_dispatch_{false};
    void update_history_view();

    // Export configuration and picker state
    std::string export_directory_{"/data/exports"};
    std::string export_filename_{"install_history_export"};
    bool export_as_json_{true};
    size_t export_dir_index_{0};
    size_t export_name_index_{0};
    void update_export_picker_dialog();

    ViewLayoutEngine layout_engine_;
    ScannerEngine scanner_;
    IndexCache cache_;
    std::shared_ptr<DaemonLock> daemon_client_;

    std::unique_ptr<GameInspector> inspector_;
    std::shared_ptr<InstallPipeline> installer_;

    void update_filtered_entries();
    void setup_pipeline_callbacks();
};

} // namespace ps5_library
