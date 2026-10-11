#include "ps5_library/ui_shell.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

static void test_history_filters_and_export_controls() {
    auto delegate = std::make_shared<MockInstallDelegate>();
    auto pipeline = std::make_shared<InstallPipeline>(delegate);
    UiShell shell;
    shell.set_installer(pipeline);

    GameItem item1;
    item1.filename = "Alpha.pkg";
    item1.display_name = "Alpha Game";
    item1.format = "pkg";
    std::string id1 = pipeline->queue_item(item1, "PPSA11111");
    pipeline->process_one_step(); // Completed

    GameItem item2;
    item2.filename = "Beta.ffpkg";
    item2.display_name = "Beta DLC";
    item2.format = "ffpkg";
    std::string id2 = pipeline->queue_item(item2, "PPSA22222");
    delegate->configure_failure("Beta.ffpkg", InstallErrorCode::CorruptedHeader, "Corrupted header");
    pipeline->process_one_step(); // Failed

    shell.show_install_history();
    TEST_ASSERT(shell.active_dialog() == DialogType::InstallHistory);
    TEST_ASSERT(shell.history_items().size() == 2);

    // Verify prompts include history filter and export controls
    auto prompts = shell.get_contextual_prompts();
    bool has_triangle = false;
    bool has_square = false;
    for (const auto& p : prompts) {
        if (p.glyph == ButtonGlyph::Triangle) has_triangle = true;
        if (p.glyph == ButtonGlyph::Square) has_square = true;
    }
    TEST_ASSERT(has_triangle);
    TEST_ASSERT(has_square);

    // Test cycling outcome filter (Triangle pad input)
    InputManager input;
    input.feed_raw_buttons(pad_bits::kTriangle);
    TEST_ASSERT(shell.handle_pad_input(input));
    // Filtered by first outcome
    TEST_ASSERT(shell.history_filter_options().outcome_filter.has_value());

    // Cycle format filter (Square pad input)
    input.feed_raw_buttons(0);
    input.feed_raw_buttons(pad_bits::kSquare);
    TEST_ASSERT(shell.handle_pad_input(input));
    TEST_ASSERT(!shell.history_filter_options().format_filter.empty());

    // Reset filters
    HistoryFilterOptions reset_opts;
    shell.set_history_filter_options(reset_opts);
    TEST_ASSERT(shell.history_items().size() == 2);

    // Test cycle sort (R1 pad input)
    input.feed_raw_buttons(0);
    input.feed_raw_buttons(pad_bits::kR1);
    TEST_ASSERT(shell.handle_pad_input(input));

    // Test history exports to files
    const std::string json_export = "/tmp/ui_shell_history.json";
    const std::string csv_export = "/tmp/ui_shell_history.csv";
    std::filesystem::remove(json_export);
    std::filesystem::remove(csv_export);

    TEST_ASSERT(shell.export_history_to_file(json_export, true));
    TEST_ASSERT(std::filesystem::exists(json_export));

    TEST_ASSERT(shell.export_history_to_file(csv_export, false));
    TEST_ASSERT(std::filesystem::exists(csv_export));

    std::filesystem::remove(json_export);
    std::filesystem::remove(csv_export);
}

static void test_retry_confirmation_modal_and_preflight_summary() {
    auto delegate = std::make_shared<MockInstallDelegate>();
    auto pipeline = std::make_shared<InstallPipeline>(delegate);
    UiShell shell;
    shell.set_installer(pipeline);

    const std::string tmp_pkg = "/tmp/ui_shell_retry_test.pkg";
    {
        std::ofstream ofs(tmp_pkg, std::ios::binary);
        std::string magic = "\x7FPKG\x00\x00\x00\x01";
        magic.resize(1024, 0);
        ofs.write(magic.data(), magic.size());
    }

    GameItem item;
    item.filename = "ui_shell_retry_test.pkg";
    item.absolute_path = tmp_pkg;
    item.display_name = "Retry Test";
    item.format = "pkg";
    item.size_bytes = 1024;

    std::string id = pipeline->queue_item(item, "PPSA99999");
    delegate->configure_failure("ui_shell_retry_test.pkg", InstallErrorCode::InsufficientSpace, "Disk full error");
    pipeline->process_one_step(); // Failed

    // Open history dialog
    shell.show_install_history();
    TEST_ASSERT(shell.active_dialog() == DialogType::InstallHistory);

    // Press Cross (Confirm) on failed item to trigger retry confirmation dialog
    InputManager input;
    input.feed_raw_buttons(pad_bits::kCross);
    TEST_ASSERT(shell.handle_pad_input(input));
    TEST_ASSERT(shell.active_dialog() == DialogType::RetryConfirmation);
    TEST_ASSERT(shell.dialog_model().title.find("Retry Test") != std::string::npos);
    TEST_ASSERT(shell.dialog_model().secondary_text.find("Disk full error") != std::string::npos);
    TEST_ASSERT(shell.dialog_model().secondary_text.find("[PASS] File Readability:") != std::string::npos);

    // Confirm retry via Cross
    delegate->reset();
    input.feed_raw_buttons(0);
    input.feed_raw_buttons(pad_bits::kCross);
    TEST_ASSERT(shell.handle_pad_input(input));
    TEST_ASSERT(shell.active_dialog() == DialogType::None);

    // Check pipeline has retried item
    TEST_ASSERT(pipeline->queued_tasks().size() == 1);
    TEST_ASSERT(pipeline->queued_tasks()[0].retry_count == 1);
    TEST_ASSERT(pipeline->queued_tasks()[0].original_task_id == id);

    // Test show_preflight_summary directly
    shell.show_preflight_summary(item, "PPSA99999");
    TEST_ASSERT(shell.active_dialog() == DialogType::PreflightSummary);
    TEST_ASSERT(shell.dialog_model().secondary_text.find("[PASS] File Readability:") != std::string::npos);

    // Confirm preflight install
    input.feed_raw_buttons(0);
    input.feed_raw_buttons(pad_bits::kCross);
    TEST_ASSERT(shell.handle_pad_input(input));
    TEST_ASSERT(shell.active_dialog() == DialogType::None);

    std::filesystem::remove(tmp_pkg);
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
    std::cout << "[TEST] history filters and export controls" << std::endl;
    test_history_filters_and_export_controls();
    std::cout << "[TEST] retry confirmation modal and preflight summary" << std::endl;
    test_retry_confirmation_modal_and_preflight_summary();
    std::cout << "All UI shell tests passed." << std::endl;
    return 0;
}
