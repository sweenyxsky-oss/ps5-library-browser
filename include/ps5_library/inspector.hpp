#pragma once

#include "game_metadata.hpp"
#include "installer.hpp"
#include "input.hpp"

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace ps5_library {

enum class InspectorTab {
    Game,       // Base Game section
    Dlc,        // DLC Add-ons section
    Update      // Title Updates section
};

const char* to_string(InspectorTab tab);

enum class InspectorFocusSection {
    Tabs,       // Focused on the 3 section buttons (Game, DLC, Update)
    ItemList,   // Focused on list of packages in current tab
    Actions     // Focused on action buttons (Install, Install All DLCs, etc.)
};

/**
 * Highlighted and formatted package details for UI presentation.
 */
struct FormattedPackageDetail {
    std::string filename;          // e.g. "SpiderMan2.pkg"
    std::string formatted_size;    // e.g. "45.2 GB"
    uint64_t raw_size_bytes{0};
    std::string extension;         // e.g. ".pkg", ".exfat", ".ffpkg"
    std::string highlighted_format;// e.g. "[PKG]", "[EXFAT]", "[FFPKG]"
    std::string display_name;      // e.g. "Pre-Order Bonus Suit DLC"
    std::string version;           // e.g. "01.002.000"
    std::string absolute_path;     // Full path on disk or network mount
    ItemKind kind{ItemKind::Unknown};

    InstallStatus install_status{InstallStatus::Queued};
    InstallErrorCode error_code{InstallErrorCode::None};
    int progress_percent{0};
    std::string status_badge;      // "READY", "QUEUED", "INSTALLING (42%)", "INSTALLED", "FAILED", "INTERRUPTED"
    std::string error_message;
    std::string recovery_guidance; // Actionable steps to resolve failures or retry

    /**
     * Formats one-line highlighted package summary:
     * e.g. "[PKG]  SpiderMan2.pkg  |  Size: 45.2 GB  |  Ext: .pkg  |  Ready"
     */
    std::string formatted_summary_line() const;

    /**
     * Formats multi-line detail box with failure recovery steps
     */
    std::string formatted_detail_box() const;
};

/**
 * Inspector view model and controller for Game/DLC/Update inspection.
 */
class GameInspector {
public:
    explicit GameInspector(GameEntry entry);

    // Outside info (always visible regardless of tab)
    const std::string& title() const { return entry_.title; }
    const std::string& title_id() const { return entry_.title_id; }
    const std::string& cover_path() const { return entry_.cover_path; }
    const std::string& base_format() const { return entry_.primary_format; }
    const std::string& base_size_formatted() const { return outside_base_size_; }
    const std::string& total_package_size_formatted() const { return entry_.formatted_total_size; }
    size_t dlc_count() const { return entry_.dlcs.size(); }
    size_t update_count() const { return entry_.updates.size(); }
    bool has_base_game() const { return entry_.base_game.has_value(); }

    const GameEntry& entry() const { return entry_; }
    void update_entry(GameEntry new_entry);

    // Tab control
    InspectorTab active_tab() const { return active_tab_; }
    void set_tab(InspectorTab tab);
    void next_tab();
    void prev_tab();

    // Focus section control
    InspectorFocusSection focus_section() const { return focus_section_; }
    void set_focus_section(InspectorFocusSection section) { focus_section_ = section; }

    // Current tab items & navigation
    const std::vector<FormattedPackageDetail>& current_tab_items() const { return current_tab_items_; }
    size_t selected_item_index() const { return selected_item_index_; }
    void select_next_item();
    void select_prev_item();
    std::optional<FormattedPackageDetail> selected_item() const;

    // Action button states
    bool can_install_selected() const;
    bool can_install_all_dlcs() const;
    size_t selected_action_button() const { return selected_action_button_; }
    void select_next_action();
    void select_prev_action();

    // DualSense input handling
    // Returns true if input was consumed
    bool handle_pad_input(const InputManager& input, InstallPipeline& pipeline);

    // Contextual button prompts for bottom bar
    std::vector<ButtonPrompt> get_contextual_prompts() const;

    // Synchronize package statuses with installer pipeline
    void sync_with_pipeline(const InstallPipeline& pipeline);

    // Header info line
    std::string get_outside_header_info() const;

    // Tab button labels
    std::string get_game_tab_label() const;
    std::string get_dlc_tab_label() const;
    std::string get_update_tab_label() const;

private:
    GameEntry entry_;
    InspectorTab active_tab_{InspectorTab::Game};
    InspectorFocusSection focus_section_{InspectorFocusSection::Tabs};
    size_t selected_item_index_{0};
    size_t selected_action_button_{0};

    std::string outside_base_size_;
    std::vector<FormattedPackageDetail> current_tab_items_;

    void rebuild_tab_items();
    static FormattedPackageDetail make_package_detail(const GameItem& item);
};

} // namespace ps5_library
