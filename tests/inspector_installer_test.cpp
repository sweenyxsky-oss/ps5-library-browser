#include "ps5_library/inspector.hpp"
#include "ps5_library/installer.hpp"
#include "ps5_library/ui_shell.hpp"
#include "ps5_library/input.hpp"

#include <cassert>
#include <iostream>
#include <memory>

using namespace ps5_library;

static GameItem make_item(ItemKind kind, const std::string& filename, uint64_t size, const std::string& version = "") {
    GameItem item;
    item.kind = kind;
    item.filename = filename;
    item.absolute_path = "/packages/" + filename;
    item.format = filename.substr(filename.find_last_of('.') + 1);
    item.size_bytes = size;
    item.size_known = true;
    item.formatted_size = format_file_size(size);
    item.display_name = filename;
    item.version = version;
    return item;
}

static GameEntry create_test_game() {
    GameEntry entry;
    entry.title_id = "PPSA01234";
    entry.title = "Marvel's Spider-Man 2";
    entry.primary_format = "pkg";
    entry.cover_path = "/covers/PPSA01234.png";
    entry.base_game = make_item(ItemKind::Game, "SpiderMan2.pkg", 45ULL * 1024ULL * 1024ULL * 1024ULL, "01.000.000");

    entry.dlcs = {
        make_item(ItemKind::Dlc, "SpiderMan2_DLC1_Suits.pkg", 500ULL * 1024ULL * 1024ULL),
        make_item(ItemKind::Dlc, "SpiderMan2_DLC2_Story.ffpkg", 2ULL * 1024ULL * 1024ULL * 1024ULL),
        make_item(ItemKind::Dlc, "SpiderMan2_DLC3_Bonus.exfat", 1ULL * 1024ULL * 1024ULL * 1024ULL)
    };

    entry.updates = {
        make_item(ItemKind::Update, "SpiderMan2_Patch_01.002.pkg", 15ULL * 1024ULL * 1024ULL * 1024ULL, "01.002.000")
    };

    entry.calculate_totals();
    return entry;
}

void test_inspector_outside_info_and_tabs() {
    GameEntry entry = create_test_game();
    GameInspector inspector(entry);

    // Outside header and info (shown outside the buttons)
    assert(inspector.title() == "Marvel's Spider-Man 2");
    assert(inspector.title_id() == "PPSA01234");
    assert(inspector.base_format() == "pkg");
    assert(!inspector.base_size_formatted().empty());
    assert(inspector.dlc_count() == 3);
    assert(inspector.update_count() == 1);
    assert(inspector.has_base_game());

    std::string header_info = inspector.get_outside_header_info();
    assert(header_info.find("pkg") != std::string::npos);
    assert(header_info.find("Total:") != std::string::npos);

    // Tab labels
    assert(inspector.get_game_tab_label() == "Game (1)");
    assert(inspector.get_dlc_tab_label() == "DLC (3)");
    assert(inspector.get_update_tab_label() == "Update (1)");

    // Default tab is Game
    assert(inspector.active_tab() == InspectorTab::Game);
    assert(inspector.current_tab_items().size() == 1);
    auto base_detail = inspector.selected_item();
    assert(base_detail.has_value());
    assert(base_detail->filename == "SpiderMan2.pkg");
    assert(base_detail->extension == ".pkg");
    assert(base_detail->highlighted_format == "[PKG]");
    assert(base_detail->formatted_size.find("GB") != std::string::npos);

    // Switch to DLC tab
    inspector.set_tab(InspectorTab::Dlc);
    assert(inspector.active_tab() == InspectorTab::Dlc);
    assert(inspector.current_tab_items().size() == 3);
    assert(inspector.can_install_all_dlcs());

    // Highlighted formats for DLCs (.pkg, .ffpkg, .exfat)
    assert(inspector.current_tab_items()[0].highlighted_format == "[PKG]");
    assert(inspector.current_tab_items()[1].highlighted_format == "[FFPKG]");
    assert(inspector.current_tab_items()[2].highlighted_format == "[EXFAT]");

    // Switch to Update tab
    inspector.set_tab(InspectorTab::Update);
    assert(inspector.active_tab() == InspectorTab::Update);
    assert(inspector.current_tab_items().size() == 1);
    assert(inspector.current_tab_items()[0].version == "01.002.000");

    std::cout << "PASS: test_inspector_outside_info_and_tabs\n";
}

void test_per_item_install_and_sync() {
    GameEntry entry = create_test_game();
    GameInspector inspector(entry);
    auto mock_delegate = std::make_shared<MockInstallDelegate>();
    InstallPipeline pipeline(mock_delegate);

    // Queue single DLC item
    std::string task_id = pipeline.queue_item(entry.dlcs[0], entry.title_id);
    assert(!task_id.empty());
    assert(pipeline.is_busy());

    // Sync inspector with pipeline
    inspector.set_tab(InspectorTab::Dlc);
    inspector.sync_with_pipeline(pipeline);
    assert(inspector.current_tab_items()[0].install_status == InstallStatus::Queued);
    assert(inspector.current_tab_items()[0].status_badge == "QUEUED");

    // Execute step
    bool ok = pipeline.process_one_step();
    assert(ok);
    (void)ok;
    inspector.sync_with_pipeline(pipeline);
    assert(inspector.current_tab_items()[0].install_status == InstallStatus::Completed);
    assert(inspector.current_tab_items()[0].status_badge == "INSTALLED");

    std::cout << "PASS: test_per_item_install_and_sync\n";
}

void test_install_all_dlcs_pipeline_success() {
    GameEntry entry = create_test_game();
    auto mock_delegate = std::make_shared<MockInstallDelegate>();
    InstallPipeline pipeline(mock_delegate);

    size_t progress_callbacks = 0;
    size_t task_finished_count = 0;
    bool batch_completed_event = false;

    pipeline.set_progress_callback([&](const InstallTask& task, const BatchProgress& batch) {
        (void)task;
        (void)batch;
        progress_callbacks++;
    });

    pipeline.set_task_finished_callback([&](const InstallTask& task) {
        assert(task.status == InstallStatus::Completed);
        (void)task;
        task_finished_count++;
    });

    pipeline.set_batch_finished_callback([&](const BatchProgress& batch) {
        assert(batch.is_completed);
        assert(!batch.has_failures);
        assert(batch.completed_items == 3);
        (void)batch;
        batch_completed_event = true;
    });

    std::string batch_id = pipeline.install_all_dlcs(entry);
    assert(!batch_id.empty());

    BatchProgress init_batch = pipeline.current_batch_progress();
    assert(init_batch.is_running);
    assert(init_batch.total_items == 3);
    assert(init_batch.completed_items == 0);

    // Process all 3 DLCs
    while (pipeline.is_busy()) {
        bool step_ok = pipeline.process_one_step();
        assert(step_ok);
        (void)step_ok;
    }

    BatchProgress finished_batch = pipeline.current_batch_progress();
    assert(finished_batch.is_completed);
    assert(finished_batch.completed_items == 3);
    assert(finished_batch.failed_items == 0);
    assert(finished_batch.overall_progress_percent() == 100);
    assert(task_finished_count == 3);
    assert(batch_completed_event);
    assert(progress_callbacks > 0);
    assert(mock_delegate->installed_count() == 3);

    std::cout << "PASS: test_install_all_dlcs_pipeline_success\n";
}

void test_install_all_dlcs_failure_handling() {
    GameEntry entry = create_test_game();
    auto mock_delegate = std::make_shared<MockInstallDelegate>();

    // Configure 2nd DLC to fail with InsufficientSpace
    mock_delegate->configure_failure("SpiderMan2_DLC2_Story.ffpkg",
                                     InstallErrorCode::InsufficientSpace,
                                     "Not enough storage space on target drive");

    InstallPipeline pipeline(mock_delegate);
    pipeline.install_all_dlcs(entry);

    while (pipeline.is_busy()) {
        pipeline.process_one_step();
    }

    BatchProgress batch = pipeline.current_batch_progress();
    assert(batch.is_completed);
    assert(batch.has_failures);
    assert(batch.completed_items == 2);
    assert(batch.failed_items == 1);
    assert(batch.failure_messages.size() == 1);
    assert(batch.failure_messages[0].find("Not enough storage space") != std::string::npos);

    std::cout << "PASS: test_install_all_dlcs_failure_handling\n";
}

void test_install_all_dlcs_cancellation() {
    GameEntry entry = create_test_game();
    auto mock_delegate = std::make_shared<MockInstallDelegate>();
    InstallPipeline pipeline(mock_delegate);

    pipeline.install_all_dlcs(entry);

    // Step 1: 1st DLC completed
    pipeline.process_one_step();
    assert(pipeline.current_batch_progress().completed_items == 1);

    // User cancels remaining tasks
    pipeline.cancel_all();

    BatchProgress batch = pipeline.current_batch_progress();
    assert(batch.is_cancelled);
    assert(batch.cancelled_items == 2);
    assert(!pipeline.is_busy());

    std::cout << "PASS: test_install_all_dlcs_cancellation\n";
}

void test_ui_shell_integration_with_inspector() {
    UiShell shell(nullptr, "/tmp/test_cache_milestone4.json");
    GameEntry entry = create_test_game();
    shell.set_entries({entry});

    assert(!shell.is_inspector_open());
    assert(shell.focused_entry().has_value());

    // Press Cross on focused game -> opens Inspector
    InputManager input;
    input.feed_raw_buttons(pad_bits::kCross);
    input.process_pad_input(input.current_state());
    bool handled = shell.handle_pad_input(input);
    assert(handled);
    (void)handled;
    assert(shell.is_inspector_open());
    assert(shell.inspector() != nullptr);
    assert(shell.inspector()->title() == "Marvel's Spider-Man 2");

    // Contextual prompts when inspector is open
    auto prompts = shell.get_contextual_prompts();
    bool has_back = false;
    for (const auto& p : prompts) {
        if (p.glyph == ButtonGlyph::Circle) has_back = true;
    }
    assert(has_back);
    (void)has_back;

    // Press Circle in inspector -> closes Inspector and returns to library
    input.feed_raw_buttons(pad_bits::kCircle);
    input.process_pad_input(input.current_state());
    handled = shell.handle_pad_input(input);
    assert(handled);
    assert(!shell.is_inspector_open());

    std::cout << "PASS: test_ui_shell_integration_with_inspector\n";
}

int main() {
    std::cout << "--- Starting Milestone 4 Inspector & Installer Tests ---\n";
    test_inspector_outside_info_and_tabs();
    test_per_item_install_and_sync();
    test_install_all_dlcs_pipeline_success();
    test_install_all_dlcs_failure_handling();
    test_install_all_dlcs_cancellation();
    test_ui_shell_integration_with_inspector();
    std::cout << "All Milestone 4 Inspector & Installer tests passed successfully!\n";
    return 0;
}
