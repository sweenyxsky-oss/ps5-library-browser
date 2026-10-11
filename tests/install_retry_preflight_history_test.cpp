#include "ps5_library/installer.hpp"
#include "ps5_library/inspector.hpp"
#include "ps5_library/ui_shell.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace ps5_library;

namespace {

void create_dummy_file(const std::string& path, const std::string& content) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream ofs(path, std::ios::binary);
    ofs.write(content.data(), content.size());
}

} // namespace

void test_duplicate_preflight_and_blocking() {
    auto delegate = std::make_shared<MockInstallDelegate>();
    InstallPipeline pipeline(delegate);

    GameItem item;
    item.kind = ItemKind::Game;
    item.filename = "Game_Alpha.pkg";
    item.absolute_path = "/data/games/Game_Alpha.pkg";
    item.format = "pkg";
    item.size_bytes = 1024;
    item.size_known = true;
    item.formatted_size = "1.00 KB";
    item.display_name = "Alpha Game";

    // 1. Initial preflight check says OK to queue
    auto check1 = pipeline.check_queue_preflight(item.absolute_path);
    assert(check1.can_queue);

    std::string id1 = pipeline.queue_item(item, "PPSA11111");
    assert(!id1.empty());

    // 2. While queued, duplicate queueing is blocked
    auto check2 = pipeline.check_queue_preflight(item.absolute_path);
    assert(!check2.can_queue);
    assert(check2.reason.find("already targets this package") != std::string::npos);

    // Attempting to queue directly returns empty string
    std::string dup_id = pipeline.queue_item(item, "PPSA11111");
    assert(dup_id.empty());

    // 3. Process to InProgress / Failed
    delegate->configure_failure("Game_Alpha.pkg",
                               InstallErrorCode::CorruptedHeader,
                               "Package header corrupted");
    pipeline.process_one_step();

    assert(pipeline.get_item_status(item.absolute_path) == InstallStatus::Failed);

    // 4. Once failed, normal queue is still blocked (must use retry or explicit intent)
    auto check3 = pipeline.check_queue_preflight(item.absolute_path, false);
    assert(!check3.can_queue);
    assert(check3.reason.find("Use retry") != std::string::npos || check3.reason.find("has already completed or failed") != std::string::npos || check3.reason.find("retry") != std::string::npos);

    auto check_retry = pipeline.check_queue_preflight(item.absolute_path, true);
    assert(check_retry.can_queue);
    assert(check_retry.is_retry);

    std::cout << "PASS: test_duplicate_preflight_and_blocking" << std::endl;
}

void test_retry_provenance_and_preservation() {
    auto delegate = std::make_shared<MockInstallDelegate>();
    const std::string state_file = "/tmp/test_retry_preservation.json";
    std::filesystem::remove(state_file);

    InstallPipeline pipeline(delegate, state_file);

    GameItem item;
    item.kind = ItemKind::Game;
    item.filename = "Game_Beta.pkg";
    item.absolute_path = "/data/games/Game_Beta.pkg";
    item.format = "pkg";
    item.size_bytes = 2048;
    item.size_known = true;
    item.formatted_size = "2.00 KB";
    item.display_name = "Beta Game";

    std::string orig_id = pipeline.queue_item(item, "PPSA22222");
    assert(!orig_id.empty());

    // Configure failure and process
    delegate->configure_failure("Game_Beta.pkg",
                               InstallErrorCode::CorruptedHeader,
                               "Header check failed at offset 0x00");
    pipeline.process_one_step();

    auto failed_task = pipeline.get_task(orig_id);
    assert(failed_task.has_value());
    assert(failed_task->status == InstallStatus::Failed);
    assert(failed_task->error_code == InstallErrorCode::CorruptedHeader);
    assert(!failed_task->created_timestamp.empty());
    assert(!failed_task->completed_timestamp.empty());

    // Now trigger retry
    delegate->reset(); // Succeed on retry
    std::string retry_err;
    std::string retry_id = pipeline.retry_task(orig_id, &retry_err);
    assert(!retry_id.empty());
    assert(retry_id != orig_id);

    // Verify retry task provenance
    auto retry_task = pipeline.get_task(retry_id);
    assert(retry_task.has_value());
    assert(retry_task->status == InstallStatus::Queued);
    assert(retry_task->retry_count == 1);
    assert(retry_task->original_task_id == orig_id);
    assert(retry_task->original_error_code == InstallErrorCode::CorruptedHeader);
    assert(retry_task->original_error_message.find("Header check failed") != std::string::npos);

    // Process retry task
    pipeline.process_one_step();
    auto completed_task = pipeline.get_task(retry_id);
    assert(completed_task.has_value());
    assert(completed_task->status == InstallStatus::Completed);

    // Crucial: original failure record is PRESERVED intact in history!
    auto orig_preserved = pipeline.get_task(orig_id);
    assert(orig_preserved.has_value());
    assert(orig_preserved->status == InstallStatus::Failed);
    assert(orig_preserved->error_code == InstallErrorCode::CorruptedHeader);

    // Check persistence reloads retry provenance and timestamps
    {
        InstallPipeline reloaded(delegate, state_file);
        auto reloaded_orig = reloaded.get_task(orig_id);
        auto reloaded_retry = reloaded.get_task(retry_id);
        assert(reloaded_orig.has_value());
        assert(reloaded_orig->status == InstallStatus::Failed);
        assert(reloaded_retry.has_value());
        assert(reloaded_retry->status == InstallStatus::Completed);
        assert(reloaded_retry->retry_count == 1);
        assert(reloaded_retry->original_task_id == orig_id);
        assert(reloaded_retry->original_error_code == InstallErrorCode::CorruptedHeader);
        assert(!reloaded_retry->created_timestamp.empty());
        assert(!reloaded_retry->completed_timestamp.empty());
    }

    std::filesystem::remove(state_file);
    std::cout << "PASS: test_retry_provenance_and_preservation" << std::endl;
}

void test_package_preflight_checks() {
    auto delegate = std::make_shared<MockInstallDelegate>();
    InstallPipeline pipeline(delegate);
    pipeline.set_enable_preflight(true);

    const std::string tmp_dir = "/tmp/ps5_preflight_test";
    std::filesystem::create_directories(tmp_dir);

    // 1. Missing file
    PreflightResult res_missing = run_package_preflight(tmp_dir + "/nonexistent.pkg");
    assert(!res_missing.passed);
    assert(res_missing.error_code == InstallErrorCode::FileNotFound);
    assert(res_missing.recovery_guidance.find("verify") != std::string::npos || res_missing.recovery_guidance.find("Verify") != std::string::npos);

    // 2. Corrupted / Invalid format package (empty file)
    std::string empty_file = tmp_dir + "/empty.pkg";
    create_dummy_file(empty_file, "");
    PreflightResult res_empty = run_package_preflight(empty_file);
    assert(!res_empty.passed);
    assert(res_empty.error_code == InstallErrorCode::InvalidPackage || res_empty.error_code == InstallErrorCode::CorruptedHeader);

    // 3. Valid dummy PKG file (starts with \x7FPKG magic)
    std::string valid_pkg = tmp_dir + "/valid.pkg";
    std::string pkg_content = "\x7FPKG\x00\x00\x00\x01";
    pkg_content.resize(1024, 0);
    create_dummy_file(valid_pkg, pkg_content);
    PreflightResult res_valid = run_package_preflight(valid_pkg);
    assert(res_valid.passed);
    assert(res_valid.detected_format == "pkg");
    assert(res_valid.file_size_bytes == 1024);

    // 4. Valid ffpkg
    std::string valid_ffpkg = tmp_dir + "/game.ffpkg";
    create_dummy_file(valid_ffpkg, std::string(2048, 'A'));
    PreflightResult res_ffpkg = run_package_preflight(valid_ffpkg);
    assert(res_ffpkg.passed);
    assert(res_ffpkg.detected_format == "ffpkg");

    // 5. Enforce preflight at queue time and execution time
    GameItem bad_item;
    bad_item.kind = ItemKind::Game;
    bad_item.filename = "empty.pkg";
    bad_item.absolute_path = empty_file;
    bad_item.format = "pkg";
    bad_item.display_name = "Corrupted Game";

    // When enable_preflight is active, queueing bad packages fails preflight checks immediately
    auto queue_check = pipeline.check_queue_preflight(bad_item.absolute_path, false);
    assert(!queue_check.can_queue);
    assert(!queue_check.reason.empty());

    // When preflight is disabled at queue time but executed in pipeline, it safely fails before delegate
    pipeline.set_enable_preflight(false);
    std::string task_id = pipeline.queue_item(bad_item, "PPSA99999");
    assert(!task_id.empty());

    pipeline.set_enable_preflight(true);
    size_t before_count = delegate->installed_count();
    pipeline.process_one_step();
    assert(delegate->installed_count() == before_count); // Delegate was NOT called!

    auto task = pipeline.get_task(task_id);
    assert(task.has_value());
    assert(task->status == InstallStatus::Failed);
    assert(task->error_code == InstallErrorCode::InvalidPackage || task->error_code == InstallErrorCode::CorruptedHeader);
    assert(task->error_message.find("Preflight check failed") != std::string::npos);
    assert(!task->recovery_guidance.empty());

    std::filesystem::remove_all(tmp_dir);
    std::cout << "PASS: test_package_preflight_checks" << std::endl;
}

void test_searchable_history_and_diagnostics() {
    auto delegate = std::make_shared<MockInstallDelegate>();
    auto pipeline_ptr = std::make_shared<InstallPipeline>(delegate);

    GameItem item1;
    item1.kind = ItemKind::Game;
    item1.filename = "DemonSouls.pkg";
    item1.absolute_path = "/data/DemonSouls.pkg";
    item1.format = "pkg";
    item1.display_name = "Demon's Souls";
    std::string id1 = pipeline_ptr->queue_item(item1, "PPSA01337");

    GameItem item2;
    item2.kind = ItemKind::Dlc;
    item2.filename = "EldenRing_DLC.pkg";
    item2.absolute_path = "/data/EldenRing_DLC.pkg";
    item2.format = "pkg";
    item2.display_name = "Shadow of the Erdtree";
    std::string id2 = pipeline_ptr->queue_item(item2, "PPSA03614");

    // Process Demon's Souls
    pipeline_ptr->process_one_step();

    // Configure failure for Elden Ring DLC and process
    delegate->configure_failure("EldenRing_DLC.pkg",
                               InstallErrorCode::InsufficientSpace,
                               "Target storage partition full");
    pipeline_ptr->process_one_step();

    // Search tests
    auto all_history = pipeline_ptr->search_history("");
    assert(all_history.size() == 2);

    auto demon_search = pipeline_ptr->search_history("Demon");
    assert(demon_search.size() == 1);
    assert(demon_search[0].title_id == "PPSA01337");

    auto id_search = pipeline_ptr->search_history("03614");
    assert(id_search.size() == 1);
    assert(id_search[0].display_name == "Shadow of the Erdtree");

    auto failed_search = pipeline_ptr->search_history("", InstallStatus::Failed);
    assert(failed_search.size() == 1);
    assert(failed_search[0].title_id == "PPSA03614");

    auto completed_search = pipeline_ptr->search_history("", InstallStatus::Completed);
    assert(completed_search.size() == 1);
    assert(completed_search[0].title_id == "PPSA01337");

    // UiShell history view test
    UiShell shell;
    shell.set_installer(pipeline_ptr);
    shell.show_install_history();
    assert(shell.active_dialog() == DialogType::InstallHistory);
    assert(shell.selected_history_item().has_value());

    // Navigate history items
    shell.history_select_next();
    assert(shell.selected_history_item().has_value());

    // Search query in UiShell
    shell.set_history_search_query("Erdtree");
    auto selected = shell.selected_history_item();
    assert(selected.has_value());
    assert(selected->display_name == "Shadow of the Erdtree");

    std::cout << "PASS: test_searchable_history_and_diagnostics" << std::endl;
}

int main() {
    test_duplicate_preflight_and_blocking();
    test_retry_provenance_and_preservation();
    test_package_preflight_checks();
    test_searchable_history_and_diagnostics();
    std::cout << "\nALL 4 NEW TEST SUITES PASSED PERFECTLY!" << std::endl;
    return 0;
}
