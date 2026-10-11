#pragma once

#include "game_metadata.hpp"
#include "scanner.hpp"
#include "index_cache.hpp"
#include "daemon_client.hpp"
#include "input.hpp"
#include "views.hpp"

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
    Notification
};

struct DialogModel {
    DialogType type{DialogType::None};
    std::string title;
    std::string message;
    std::string secondary_text;
    std::vector<std::string> options;
    int selected_option{0};
};

class UiShell {
public:
    UiShell(std::shared_ptr<DaemonLock> daemon_client = nullptr,
            std::string cache_path = "/tmp/ps5_library_cache.json");

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

    // Dialog state
    DialogType active_dialog() const { return dialog_model_.type; }
    const DialogModel& dialog_model() const { return dialog_model_; }
    void show_shadowmount_lock();
    void dismiss_dialog();

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

    ViewLayoutEngine layout_engine_;
    ScannerEngine scanner_;
    IndexCache cache_;
    std::shared_ptr<DaemonLock> daemon_client_;

    void update_filtered_entries();
    void enforce_daemon_gate_if_needed();
};

} // namespace ps5_library
