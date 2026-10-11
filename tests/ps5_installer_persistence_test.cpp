#include "ps5_library/installer.hpp"
#include "ps5_library/inspector.hpp"
#include "ps5_library/ui_shell.hpp"

#include <cassert>
#include <iostream>
#include <fstream>
#include <filesystem>

using namespace ps5_library;

static GameItem make_test_item(const std::string& filename, uint64_t size) {
    GameItem item;
    item.kind = ItemKind::Game;
    item.filename = filename;
    item.absolute_path = "/tmp/packages/" + filename;
    item.format = "pkg";
    item.size_bytes = size;
    item.size_known = true;
    item.formatted_size = format_file_size(size);
    item.display_name = filename;
    return item;
}

void test_ps5_install_delegate_capabilities_and_unsupported_platform() {
    Ps5InstallDelegate delegate(false);

    // Default on Linux host is non-PS5 environment
    delegate.refresh_capabilities();
    assert(!delegate.is_platform_supported());
    std::string desc = delegate.platform_status_description();
    assert(desc.find("Unsupported") != std::string::npos);

    // Attempting install on host produces UnsupportedPlatform error code and descriptive message
    InstallTask task;
    task.package_filename = "test.pkg";
    task.source_path = "/tmp/test.pkg";
    InstallErrorCode code = InstallErrorCode::None;
    std::string error;
    bool ok = delegate.install_package(task, nullptr, code, error);
    assert(!ok);
    assert(code == InstallErrorCode::UnsupportedPlatform);
    assert(!error.empty());
    assert(error.find("Unsupported platform") != std::string::npos || error.find("compatible native PS5 runtime") != std::string::npos);

    // Test actionable recovery guidance for UnsupportedPlatform
    std::string guidance = get_recovery_guidance(code, error);
    assert(guidance.find("jailbroken PS5 runtime") != std::string::npos);

    // Test mock PS5 environment capability states
    // 1. PS5 runtime without kernel privilege
    delegate.set_mock_capabilities(true, false, false, 0, "Missing ShellCore privilege");
    assert(!delegate.is_platform_supported());
    ok = delegate.install_package(task, nullptr, code, error);
    assert(!ok);
    assert(code == InstallErrorCode::MissingPrivilege);
    guidance = get_recovery_guidance(code);
    assert(guidance.find("package-install privilege") != std::string::npos);

    // 2. PS5 runtime with privilege but missing libSceAppInstUtil module
    delegate.set_mock_capabilities(true, true, false, 2, "Module not found");
    assert(!delegate.is_platform_supported());
    ok = delegate.install_package(task, nullptr, code, error);
    assert(!ok);
    assert(code == InstallErrorCode::SystemModuleMissing);
    guidance = get_recovery_guidance(code);
    assert(guidance.find("package-install service") != std::string::npos);

    // 3. Fully initialized PS5 runtime
    delegate.set_mock_capabilities(true, true, true, 0, "Ready");
    assert(delegate.is_platform_supported());
    assert(delegate.platform_status_description() == "PS5 package installer ready");

    std::cout << "PASS: test_ps5_install_delegate_capabilities_and_unsupported_platform\n";
}

void test_persistence_and_interrupted_install_recovery() {
    const std::string state_file = "/tmp/test_ps5_install_jobs.json";
    std::filesystem::remove(state_file);

    // Phase 1: create pipeline with persistent state file and queue items
    {
        auto mock_del = std::make_shared<MockInstallDelegate>();
        InstallPipeline pipeline(mock_del, state_file);

        GameItem item1 = make_test_item("Game_A.pkg", 1000);
        GameItem item2 = make_test_item("Game_B.pkg", 2000);
        GameItem item3 = make_test_item("Game_C.pkg", 3000);

        pipeline.queue_item(item1, "PPSA00001");
        pipeline.queue_item(item2, "PPSA00002");
        pipeline.queue_item(item3, "PPSA00003");

        assert(pipeline.all_tasks().size() == 3);

        // Process item 1 to Completed
        pipeline.process_one_step();
        assert(pipeline.get_item_status(item1.absolute_path) == InstallStatus::Completed);

        // State file must exist on disk and be valid JSON
        assert(std::filesystem::exists(state_file));
    }

    // Phase 2: Simulate an interrupted run (simulate crash during InProgress)
    // Manually modify state JSON to simulate an item being InProgress when app was killed
    {
        std::ifstream in(state_file);
        nlohmann::json root;
        in >> root;
        in.close();

        // Mark second task as InProgress to simulate unexpected exit / crash
        assert(root["tasks"].size() >= 2);
        root["tasks"][1]["status"] = "InProgress";
        root["tasks"][1]["processed_bytes"] = 500;

        std::ofstream out(state_file);
        out << root.dump(2);
        out.close();
    }

    // Phase 3: Start new pipeline instance reading state file (simulate restart)
    {
        auto mock_del = std::make_shared<MockInstallDelegate>();
        InstallPipeline restored_pipeline(mock_del, state_file);

        auto tasks = restored_pipeline.all_tasks();
        assert(tasks.size() == 3);

        // Task 1: Completed remains Completed
        assert(tasks[0].status == InstallStatus::Completed);

        // Task 2: InProgress item must be restored and clearly marked Interrupted
        assert(tasks[1].status == InstallStatus::Interrupted);
        assert(tasks[1].error_code == InstallErrorCode::InstallInterrupted);
        assert(!tasks[1].error_message.empty());
        assert(!tasks[1].recovery_guidance.empty());
        assert(tasks[1].recovery_guidance.find("restarted during installation") != std::string::npos);

        // Task 3: Queued item remains Queued in the execution queue
        assert(tasks[2].status == InstallStatus::Queued);
        assert(restored_pipeline.queued_tasks().size() == 1);

        // Check inspector syncing with restored pipeline
        GameEntry entry2;
        entry2.title_id = "PPSA00002";
        entry2.title = "Game B";
        entry2.base_game = make_test_item("Game_B.pkg", 2000);

        GameInspector inspector(entry2);
        inspector.sync_with_pipeline(restored_pipeline);

        auto selected = inspector.selected_item();
        assert(selected.has_value());
        assert(selected->install_status == InstallStatus::Interrupted);
        assert(selected->status_badge.find("INTERRUPTED") != std::string::npos);
        assert(!selected->recovery_guidance.empty());
    }

    std::filesystem::remove(state_file);
    std::cout << "PASS: test_persistence_and_interrupted_install_recovery\n";
}

void test_post_install_refresh_and_actionable_recovery_guidance() {
    const std::string cache_path = "/tmp/test_cache_refresh.json";
    const std::string state_path = "/tmp/test_state_refresh.json";
    std::filesystem::remove(cache_path);
    std::filesystem::remove(state_path);

    UiShell shell(nullptr, cache_path, state_path);

    GameEntry entry;
    entry.title_id = "PPSA00777";
    entry.title = "Cyberpunk Adventure";
    entry.primary_format = "pkg";
    entry.confidence = MetadataConfidence::Filename;
    entry.base_game = make_test_item("Cyberpunk.pkg", 50ULL * 1024ULL * 1024ULL * 1024ULL);
    entry.dlcs = { make_test_item("Expansion1.pkg", 5ULL * 1024ULL * 1024ULL * 1024ULL) };
    shell.set_entries({entry});

    // Open inspector on this game
    shell.open_inspector(entry);
    assert(shell.is_inspector_open());
    assert(shell.inspector()->title_id() == "PPSA00777");

    // Trigger post-install refresh
    shell.refresh_after_install("PPSA00777", "/tmp/packages/Cyberpunk.pkg");

    // Verify metadata confidence was promoted to Verified
    assert(shell.all_entries()[0].confidence == MetadataConfidence::Verified);
    assert(shell.inspector()->entry().confidence == MetadataConfidence::Verified);

    // Test failure scenario and actionable recovery guidance display
    auto mock_del = std::make_shared<MockInstallDelegate>();
    mock_del->configure_failure("Cyberpunk.pkg",
                                InstallErrorCode::InsufficientSpace,
                                "Device /user is out of storage space");

    auto pipeline = std::make_shared<InstallPipeline>(mock_del, state_path);
    shell.set_installer(pipeline);

    // Queue item and process failure
    pipeline->queue_item(*entry.base_game, entry.title_id);
    pipeline->process_one_step();

    // Verify inspector updated with failure and guidance
    shell.inspector()->sync_with_pipeline(*pipeline);
    auto detail = shell.inspector()->selected_item();
    assert(detail.has_value());
    assert(detail->install_status == InstallStatus::Failed);
    assert(detail->status_badge.find("FAILED") != std::string::npos);
    assert(!detail->recovery_guidance.empty());
    assert(detail->recovery_guidance.find("Free storage") != std::string::npos);

    // Test opening recovery guidance dialog via controller input (Square / Inspect)
    InputManager input;
    input.feed_raw_buttons(pad_bits::kSquare);
    bool handled = shell.handle_pad_input(input);
    assert(handled);
    assert(shell.active_dialog() == DialogType::RecoveryGuidance);
    assert(shell.dialog_model().title.find("Cyberpunk.pkg") != std::string::npos);
    assert(shell.dialog_model().secondary_text.find("Free storage") != std::string::npos);

    // Dismiss dialog
    input.feed_raw_buttons(0);
    input.feed_raw_buttons(pad_bits::kCross);
    handled = shell.handle_pad_input(input);
    assert(handled);
    assert(shell.active_dialog() == DialogType::None);

    std::filesystem::remove(cache_path);
    std::filesystem::remove(state_path);
    std::cout << "PASS: test_post_install_refresh_and_actionable_recovery_guidance\n";
}

int main() {
    std::cout << "=== Running PS5 Installer, Persistence & Recovery Tests ===\n";
    test_ps5_install_delegate_capabilities_and_unsupported_platform();
    test_persistence_and_interrupted_install_recovery();
    test_post_install_refresh_and_actionable_recovery_guidance();
    std::cout << "All PS5 Installer, Persistence & Recovery tests passed successfully!\n";
    return 0;
}
