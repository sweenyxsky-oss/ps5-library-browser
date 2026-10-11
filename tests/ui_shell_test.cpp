#include "ps5_library/ui_shell.hpp"
#include <cstdlib>
#include <iostream>

#define TEST_ASSERT(cond) do { if (!(cond)) { std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; std::exit(1); } } while (0)

using namespace ps5_library;

static GameEntry make_entry(const std::string& id, const std::string& title, const std::string& format) {
    GameEntry entry;
    entry.title_id = id;
    entry.title = title;
    entry.primary_format = format;
    entry.formatted_total_size = "10.0 GB";
    return entry;
}

void test_modes_and_lock() {
    UiShell shell(nullptr, "/tmp/ui_shell_test_cache.json");
    TEST_ASSERT(shell.library_mode() == LibraryMode::InstalledGames);
    shell.toggle_library_mode();
    TEST_ASSERT(shell.library_mode() == LibraryMode::InstalledGames);
    TEST_ASSERT(shell.active_dialog() == DialogType::ShadowMountLock);
    TEST_ASSERT(shell.dialog_model().message.find("ShadowMount Plus is required") != std::string::npos);
    TEST_ASSERT(shell.get_contextual_prompts().size() == 2);
    shell.dismiss_dialog();
    shell.set_library_mode(LibraryMode::InstalledGames);
    TEST_ASSERT(shell.active_dialog() == DialogType::None);
}

void test_layouts_and_navigation() {
    UiShell shell(nullptr, "/tmp/ui_shell_test_cache.json");
    shell.set_entries({make_entry("PPSA00001", "A Game", "pkg"),
                       make_entry("PPSA00002", "B Game", "exfat"),
                       make_entry("PPSA00003", "C Game", "folder"),
                       make_entry("PPSA00004", "D Game", "ffpkg"),
                       make_entry("PPSA00005", "E Game", "pkg"),
                       make_entry("PPSA00006", "F Game", "pkg")});
    TEST_ASSERT(shell.filtered_entries().size() == 6);
    auto cards = shell.get_current_view_cards();
    TEST_ASSERT(!cards.empty());
    size_t focused = 0;
    for (const auto& card : cards) if (card.is_focused) focused = card.index;
    TEST_ASSERT(focused == 0);

    InputManager input;
    input.feed_raw_buttons(pad_bits::kRight);
    TEST_ASSERT(shell.handle_pad_input(input));
    TEST_ASSERT(shell.focused_index() == 1);

    shell.toggle_view_layout();
    TEST_ASSERT(shell.view_layout() == ViewLayout::BlockGrid);
    auto grid_cards = shell.get_current_view_cards();
    TEST_ASSERT(!grid_cards.empty());
    input.feed_raw_buttons(0);
    input.feed_raw_buttons(pad_bits::kDown);
    TEST_ASSERT(shell.handle_pad_input(input));
    TEST_ASSERT(shell.focused_index() == 5);

    input.feed_raw_buttons(0);
    input.feed_raw_buttons(pad_bits::kTriangle);
    TEST_ASSERT(shell.handle_pad_input(input));
    TEST_ASSERT(shell.active_dialog() == DialogType::ShadowMountLock);
}

void test_search_and_filters() {
    UiShell shell(nullptr, "/tmp/ui_shell_test_cache.json");
    shell.set_entries({make_entry("PPSA00001", "A Game", "pkg"),
                       make_entry("PPSA00002", "B Game", "exfat"),
                       make_entry("PPSA00003", "C Game", "folder")});
    shell.set_format_filter(FormatFilter::Pkg);
    TEST_ASSERT(shell.filtered_entries().size() == 1);
    TEST_ASSERT(shell.filtered_entries()[0].title == "A Game");
    shell.set_format_filter(FormatFilter::All);
    shell.set_search_query("PPSA00002");
    TEST_ASSERT(shell.filtered_entries().size() == 1);
    TEST_ASSERT(shell.filtered_entries()[0].title == "B Game");
    shell.set_search_query("no match");
    TEST_ASSERT(shell.filtered_entries().empty());

    auto prompts = shell.get_contextual_prompts();
    TEST_ASSERT(!prompts.empty());
    TEST_ASSERT(prompts[0].glyph == ButtonGlyph::Triangle);
}


static void test_install_history_dialog() {
    auto delegate = std::make_shared<MockInstallDelegate>();
    auto pipeline = std::make_shared<InstallPipeline>(delegate);
    UiShell shell;
    shell.set_installer(pipeline);

    // Queue and fail an item
    GameItem item;
    item.filename = "patch.pkg";
    item.display_name = "Update Patch";
    item.format = "pkg";
    std::string task_id = pipeline->queue_item(item, "PPSA12345");
    delegate->configure_failure("patch.pkg", InstallErrorCode::CorruptedHeader, "Corrupted patch payload");
    pipeline->process_one_step();

    // Open install history
    shell.show_install_history();
    TEST_ASSERT(shell.active_dialog() == DialogType::InstallHistory);
    TEST_ASSERT(shell.dialog_model().title == "Install History & Diagnostics");
    TEST_ASSERT(shell.selected_history_item().has_value());
    TEST_ASSERT(shell.selected_history_item()->task_id == task_id);
    TEST_ASSERT(shell.selected_history_item()->status == InstallStatus::Failed);

    // Filter history search
    shell.set_history_search_query("nonexistent");
    TEST_ASSERT(!shell.selected_history_item().has_value());
    shell.set_history_search_query("PPSA12345");
    TEST_ASSERT(shell.selected_history_item().has_value());

    // Dismiss dialog using Circle (Cancel)
    InputManager input;
    input.feed_raw_buttons(pad_bits::kCircle);
    TEST_ASSERT(shell.handle_pad_input(input));
    TEST_ASSERT(shell.active_dialog() == DialogType::None);
}

int main() {
    std::cout << "[TEST] UI mode/lock" << std::endl;
    test_modes_and_lock();
    std::cout << "[TEST] layout/input" << std::endl;
    test_layouts_and_navigation();
    std::cout << "[TEST] search/filter" << std::endl;
    test_search_and_filters();
    std::cout << "[TEST] install history dialog" << std::endl;
    test_install_history_dialog();
    std::cout << "All UI shell tests passed." << std::endl;
    return 0;
}
