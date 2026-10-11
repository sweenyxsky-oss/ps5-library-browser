#include "ps5_library/inspector.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <filesystem>

namespace ps5_library {

const char* to_string(InspectorTab tab) {
    switch (tab) {
        case InspectorTab::Game: return "Game";
        case InspectorTab::Dlc: return "DLC";
        case InspectorTab::Update: return "Update";
        default: return "Unknown";
    }
}

std::string FormattedPackageDetail::formatted_summary_line() const {
    std::ostringstream out;
    out << highlighted_format << "  " << filename
        << "  |  Size: " << (formatted_size.empty() ? "Unknown" : formatted_size)
        << "  |  Ext: " << (extension.empty() ? "(none)" : extension)
        << "  |  " << (status_badge.empty() ? "READY" : status_badge);
    return out.str();
}

std::string FormattedPackageDetail::formatted_detail_box() const {
    std::ostringstream out;
    out << "Name: " << (display_name.empty() ? filename : display_name) << '\n'
        << "File: " << filename << '\n'
        << "Format: " << highlighted_format << "  " << extension << '\n'
        << "Size: " << (formatted_size.empty() ? "Unknown" : formatted_size);
    if (!version.empty()) out << '\n' << "Version: " << version;
    if (!absolute_path.empty()) out << '\n' << "Path: " << absolute_path;
    out << '\n' << "Status: " << (status_badge.empty() ? "READY" : status_badge);
    return out.str();
}

GameInspector::GameInspector(GameEntry entry) : entry_(std::move(entry)) {
    outside_base_size_ = entry_.base_game && entry_.base_game->size_known
        ? (entry_.base_game->formatted_size.empty()
            ? format_file_size(entry_.base_game->size_bytes)
            : entry_.base_game->formatted_size)
        : "Unknown";
    if (entry_.formatted_total_size.empty()) {
        entry_.calculate_totals();
    }
    rebuild_tab_items();
}

void GameInspector::set_tab(InspectorTab tab) {
    active_tab_ = tab;
    selected_item_index_ = 0;
    selected_action_button_ = 0;
    focus_section_ = InspectorFocusSection::Tabs;
    rebuild_tab_items();
}

void GameInspector::next_tab() {
    switch (active_tab_) {
        case InspectorTab::Game: set_tab(InspectorTab::Dlc); break;
        case InspectorTab::Dlc: set_tab(InspectorTab::Update); break;
        case InspectorTab::Update: set_tab(InspectorTab::Game); break;
    }
}

void GameInspector::prev_tab() {
    switch (active_tab_) {
        case InspectorTab::Game: set_tab(InspectorTab::Update); break;
        case InspectorTab::Dlc: set_tab(InspectorTab::Game); break;
        case InspectorTab::Update: set_tab(InspectorTab::Dlc); break;
    }
}

void GameInspector::rebuild_tab_items() {
    current_tab_items_.clear();
    if (active_tab_ == InspectorTab::Game) {
        if (entry_.base_game) current_tab_items_.push_back(make_package_detail(*entry_.base_game));
    } else if (active_tab_ == InspectorTab::Dlc) {
        for (const auto& item : entry_.dlcs) current_tab_items_.push_back(make_package_detail(item));
    } else {
        for (const auto& item : entry_.updates) current_tab_items_.push_back(make_package_detail(item));
    }
    if (selected_item_index_ >= current_tab_items_.size()) selected_item_index_ = current_tab_items_.empty() ? 0 : current_tab_items_.size() - 1;
}

FormattedPackageDetail GameInspector::make_package_detail(const GameItem& item) {
    FormattedPackageDetail detail;
    detail.filename = item.filename;
    detail.formatted_size = item.size_known
        ? (item.formatted_size.empty() ? format_file_size(item.size_bytes) : item.formatted_size)
        : "Unknown";
    detail.raw_size_bytes = item.size_bytes;
    detail.extension = std::filesystem::path(item.filename).extension().string();
    if (detail.extension.empty() && !item.format.empty()) detail.extension = "." + item.format;
    std::string upper = item.format.empty() ? detail.extension : item.format;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (!upper.empty() && upper.front() == '.') upper.erase(0, 1);
    detail.highlighted_format = "[" + (upper.empty() ? "UNKNOWN" : upper) + "]";
    detail.display_name = item.display_name;
    detail.version = item.version;
    detail.absolute_path = item.absolute_path;
    detail.kind = item.kind;
    detail.status_badge = "READY";
    return detail;
}

void GameInspector::select_next_item() {
    if (!current_tab_items_.empty()) selected_item_index_ = (selected_item_index_ + 1) % current_tab_items_.size();
}

void GameInspector::select_prev_item() {
    if (!current_tab_items_.empty()) selected_item_index_ = (selected_item_index_ + current_tab_items_.size() - 1) % current_tab_items_.size();
}

std::optional<FormattedPackageDetail> GameInspector::selected_item() const {
    if (selected_item_index_ >= current_tab_items_.size()) return std::nullopt;
    return current_tab_items_[selected_item_index_];
}

bool GameInspector::can_install_selected() const {
    return selected_item().has_value();
}

bool GameInspector::can_install_all_dlcs() const {
    return !entry_.dlcs.empty();
}

void GameInspector::select_next_action() {
    const size_t count = (active_tab_ == InspectorTab::Dlc && can_install_all_dlcs()) ? 2 : 1;
    selected_action_button_ = (selected_action_button_ + 1) % count;
}

void GameInspector::select_prev_action() {
    const size_t count = (active_tab_ == InspectorTab::Dlc && can_install_all_dlcs()) ? 2 : 1;
    selected_action_button_ = (selected_action_button_ + count - 1) % count;
}

bool GameInspector::handle_pad_input(const InputManager& input, InstallPipeline& pipeline) {
    if (input.action_triggered(LogicalAction::Cancel)) return false;
    if (input.action_triggered(LogicalAction::NavLeft)) {
        if (focus_section_ == InspectorFocusSection::Tabs) prev_tab();
        else if (focus_section_ == InspectorFocusSection::Actions) select_prev_action();
        return true;
    }
    if (input.action_triggered(LogicalAction::NavRight)) {
        if (focus_section_ == InspectorFocusSection::Tabs) next_tab();
        else if (focus_section_ == InspectorFocusSection::Actions) select_next_action();
        return true;
    }
    if (input.action_triggered(LogicalAction::NavUp)) {
        if (focus_section_ == InspectorFocusSection::ItemList) focus_section_ = InspectorFocusSection::Tabs;
        else if (focus_section_ == InspectorFocusSection::Actions) focus_section_ = InspectorFocusSection::ItemList;
        return true;
    }
    if (input.action_triggered(LogicalAction::NavDown)) {
        if (focus_section_ == InspectorFocusSection::Tabs) focus_section_ = InspectorFocusSection::ItemList;
        else if (focus_section_ == InspectorFocusSection::ItemList) focus_section_ = InspectorFocusSection::Actions;
        return true;
    }
    if (input.action_triggered(LogicalAction::Confirm)) {
        if (focus_section_ == InspectorFocusSection::Actions) {
            if (active_tab_ == InspectorTab::Dlc && can_install_all_dlcs() && selected_action_button_ == 1) {
                pipeline.install_all_dlcs(entry_);
            } else if (const auto selected = selected_item()) {
                GameItem item;
                item.kind = selected->kind;
                item.filename = selected->filename;
                item.absolute_path = selected->absolute_path;
                item.format = selected->extension.empty() ? "" : selected->extension.substr(1);
                item.size_bytes = selected->raw_size_bytes;
                item.size_known = selected->formatted_size != "Unknown";
                item.formatted_size = selected->formatted_size;
                item.version = selected->version;
                item.display_name = selected->display_name;
                pipeline.queue_item(item, entry_.title_id);
            }
        }
        return true;
    }
    if (input.action_triggered(LogicalAction::Inspect) && focus_section_ == InspectorFocusSection::ItemList) {
        if (active_tab_ == InspectorTab::Game) set_tab(InspectorTab::Dlc);
        else if (active_tab_ == InspectorTab::Dlc) set_tab(InspectorTab::Update);
        else set_tab(InspectorTab::Game);
        return true;
    }
    if (input.action_triggered(LogicalAction::ToggleMode)) {
        if (active_tab_ == InspectorTab::Dlc && can_install_all_dlcs()) pipeline.install_all_dlcs(entry_);
        return true;
    }
    return false;
}

std::vector<ButtonPrompt> GameInspector::get_contextual_prompts() const {
    std::vector<ButtonPrompt> prompts{
        {ButtonGlyph::Circle, "Back", true, false},
        {ButtonGlyph::DPadLeftRight, "Switch Game / DLC / Update", true, false},
        {ButtonGlyph::DPadUpDown, "Navigate", true, false}
    };
    if (can_install_selected()) prompts.push_back({ButtonGlyph::Cross, "Install Selected", true, true});
    if (active_tab_ == InspectorTab::Dlc && can_install_all_dlcs()) prompts.push_back({ButtonGlyph::Triangle, "Install All DLCs", true, false});
    return prompts;
}

void GameInspector::sync_with_pipeline(const InstallPipeline& pipeline) {
    for (auto& detail : current_tab_items_) {
        const auto tasks = pipeline.all_tasks();
        for (const auto& task : tasks) {
            if (task.source_path == detail.absolute_path || task.package_filename == detail.filename) {
                detail.install_status = task.status;
                detail.progress_percent = task.progress_percent();
                switch (task.status) {
                    case InstallStatus::Queued: detail.status_badge = "QUEUED"; break;
                    case InstallStatus::InProgress: detail.status_badge = "INSTALLING (" + std::to_string(task.progress_percent()) + "%)"; break;
                    case InstallStatus::Completed: detail.status_badge = "INSTALLED"; break;
                    case InstallStatus::Failed: detail.status_badge = "FAILED: " + task.error_message; break;
                    case InstallStatus::Cancelled: detail.status_badge = "CANCELLED"; break;
                }
            }
        }
    }
}

std::string GameInspector::get_outside_header_info() const {
    return "Base game: " + base_format() + "  |  Size: " + base_size_formatted() +
           "  |  Total: " + total_package_size_formatted();
}

std::string GameInspector::get_game_tab_label() const {
    return "Game (" + std::to_string(has_base_game() ? 1 : 0) + ")";
}
std::string GameInspector::get_dlc_tab_label() const {
    return "DLC (" + std::to_string(dlc_count()) + ")";
}
std::string GameInspector::get_update_tab_label() const {
    return "Update (" + std::to_string(update_count()) + ")";
}

} // namespace ps5_library
