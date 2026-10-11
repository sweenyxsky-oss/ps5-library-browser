#include "ps5_library/installer.hpp"
#include "ps5_library/inspector.hpp"
#include "ps5_library/ui_shell.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#define TEST_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)
#include <nlohmann/json.hpp>

using namespace ps5_library;
using nlohmann::json;

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
    TEST_ASSERT(check1.can_queue);

    std::string id1 = pipeline.queue_item(item, "PPSA11111");
    TEST_ASSERT(!id1.empty());

    // 2. While queued, duplicate queueing is blocked
    auto check2 = pipeline.check_queue_preflight(item.absolute_path);
    TEST_ASSERT(!check2.can_queue);
    TEST_ASSERT(check2.reason.find("already targets this package") != std::string::npos);

    // Attempting to queue directly returns empty string
    std::string dup_id = pipeline.queue_item(item, "PPSA11111");
    TEST_ASSERT(dup_id.empty());

    // 3. Process to InProgress / Failed
    delegate->configure_failure("Game_Alpha.pkg",
                               InstallErrorCode::CorruptedHeader,
                               "Package header corrupted");
    pipeline.process_one_step();

    TEST_ASSERT(pipeline.get_item_status(item.absolute_path) == InstallStatus::Failed);

    // 4. Once failed, normal queue is still blocked (must use retry or explicit intent)
    auto check3 = pipeline.check_queue_preflight(item.absolute_path, false);
    TEST_ASSERT(!check3.can_queue);
    TEST_ASSERT(check3.reason.find("Use retry") != std::string::npos || check3.reason.find("has already completed or failed") != std::string::npos || check3.reason.find("retry") != std::string::npos);

    auto check_retry = pipeline.check_queue_preflight(item.absolute_path, true);
    TEST_ASSERT(check_retry.can_queue);
    TEST_ASSERT(check_retry.is_retry);

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
    TEST_ASSERT(!orig_id.empty());

    // Configure failure and process
    delegate->configure_failure("Game_Beta.pkg",
                               InstallErrorCode::CorruptedHeader,
                               "Header check failed at offset 0x00");
    pipeline.process_one_step();

    auto failed_task = pipeline.get_task(orig_id);
    TEST_ASSERT(failed_task.has_value());
    TEST_ASSERT(failed_task->status == InstallStatus::Failed);
    TEST_ASSERT(failed_task->error_code == InstallErrorCode::CorruptedHeader);
    TEST_ASSERT(!failed_task->created_timestamp.empty());
    TEST_ASSERT(!failed_task->completed_timestamp.empty());

    // Now trigger retry
    delegate->reset(); // Succeed on retry
    std::string retry_err;
    std::string retry_id = pipeline.retry_task(orig_id, &retry_err);
    TEST_ASSERT(!retry_id.empty());
    TEST_ASSERT(retry_id != orig_id);

    // Verify retry task provenance
    auto retry_task = pipeline.get_task(retry_id);
    TEST_ASSERT(retry_task.has_value());
    TEST_ASSERT(retry_task->status == InstallStatus::Queued);
    TEST_ASSERT(retry_task->retry_count == 1);
    TEST_ASSERT(retry_task->original_task_id == orig_id);
    TEST_ASSERT(retry_task->original_error_code == InstallErrorCode::CorruptedHeader);
    TEST_ASSERT(retry_task->original_error_message.find("Header check failed") != std::string::npos);

    // Process retry task
    pipeline.process_one_step();
    auto completed_task = pipeline.get_task(retry_id);
    TEST_ASSERT(completed_task.has_value());
    TEST_ASSERT(completed_task->status == InstallStatus::Completed);

    // Crucial: original failure record is PRESERVED intact in history!
    auto orig_preserved = pipeline.get_task(orig_id);
    TEST_ASSERT(orig_preserved.has_value());
    TEST_ASSERT(orig_preserved->status == InstallStatus::Failed);
    TEST_ASSERT(orig_preserved->error_code == InstallErrorCode::CorruptedHeader);

    // Check persistence reloads retry provenance and timestamps
    {
        InstallPipeline reloaded(delegate, state_file);
        auto reloaded_orig = reloaded.get_task(orig_id);
        auto reloaded_retry = reloaded.get_task(retry_id);
        TEST_ASSERT(reloaded_orig.has_value());
        TEST_ASSERT(reloaded_orig->status == InstallStatus::Failed);
        TEST_ASSERT(reloaded_retry.has_value());
        TEST_ASSERT(reloaded_retry->status == InstallStatus::Completed);
        TEST_ASSERT(reloaded_retry->retry_count == 1);
        TEST_ASSERT(reloaded_retry->original_task_id == orig_id);
        TEST_ASSERT(reloaded_retry->original_error_code == InstallErrorCode::CorruptedHeader);
        TEST_ASSERT(!reloaded_retry->created_timestamp.empty());
        TEST_ASSERT(!reloaded_retry->completed_timestamp.empty());
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
    TEST_ASSERT(!res_missing.passed);
    TEST_ASSERT(res_missing.error_code == InstallErrorCode::FileNotFound);
    TEST_ASSERT(res_missing.recovery_guidance.find("verify") != std::string::npos || res_missing.recovery_guidance.find("Verify") != std::string::npos);

    // 2. Corrupted / Invalid format package (empty file)
    std::string empty_file = tmp_dir + "/empty.pkg";
    create_dummy_file(empty_file, "");
    PreflightResult res_empty = run_package_preflight(empty_file);
    TEST_ASSERT(!res_empty.passed);
    TEST_ASSERT(res_empty.error_code == InstallErrorCode::InvalidPackage || res_empty.error_code == InstallErrorCode::CorruptedHeader);

    // 3. Valid dummy PKG file (starts with \x7FPKG magic)
    std::string valid_pkg = tmp_dir + "/valid.pkg";
    std::string pkg_content = "\x7FPKG\x00\x00\x00\x01";
    pkg_content.resize(1024, 0);
    create_dummy_file(valid_pkg, pkg_content);
    PreflightResult res_valid = run_package_preflight(valid_pkg);
    TEST_ASSERT(res_valid.passed);
    TEST_ASSERT(res_valid.detected_format == "pkg");
    TEST_ASSERT(res_valid.file_size_bytes == 1024);

    // 4. Valid ffpkg
    std::string valid_ffpkg = tmp_dir + "/game.ffpkg";
    create_dummy_file(valid_ffpkg, std::string(2048, 'A'));
    PreflightResult res_ffpkg = run_package_preflight(valid_ffpkg);
    TEST_ASSERT(res_ffpkg.passed);
    TEST_ASSERT(res_ffpkg.detected_format == "ffpkg");

    // 5. Enforce preflight at queue time and execution time
    GameItem bad_item;
    bad_item.kind = ItemKind::Game;
    bad_item.filename = "empty.pkg";
    bad_item.absolute_path = empty_file;
    bad_item.format = "pkg";
    bad_item.display_name = "Corrupted Game";

    auto queue_check = pipeline.check_queue_preflight(bad_item.absolute_path, false);
    TEST_ASSERT(!queue_check.can_queue);
    TEST_ASSERT(!queue_check.reason.empty());

    pipeline.set_enable_preflight(false);
    std::string task_id = pipeline.queue_item(bad_item, "PPSA99999");
    TEST_ASSERT(!task_id.empty());

    pipeline.set_enable_preflight(true);
    size_t before_count = delegate->installed_count();
    pipeline.process_one_step();
    TEST_ASSERT(delegate->installed_count() == before_count); // Delegate was NOT called!

    auto task = pipeline.get_task(task_id);
    TEST_ASSERT(task.has_value());
    TEST_ASSERT(task->status == InstallStatus::Failed);
    TEST_ASSERT(task->error_code == InstallErrorCode::InvalidPackage || task->error_code == InstallErrorCode::CorruptedHeader);
    TEST_ASSERT(task->error_message.find("Preflight check failed") != std::string::npos);
    TEST_ASSERT(!task->recovery_guidance.empty());

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

    pipeline_ptr->process_one_step();

    delegate->configure_failure("EldenRing_DLC.pkg",
                               InstallErrorCode::InsufficientSpace,
                               "Target storage partition full");
    pipeline_ptr->process_one_step();

    auto all_history = pipeline_ptr->search_history("");
    TEST_ASSERT(all_history.size() == 2);

    auto demon_search = pipeline_ptr->search_history("Demon");
    TEST_ASSERT(demon_search.size() == 1);
    TEST_ASSERT(demon_search[0].title_id == "PPSA01337");

    auto id_search = pipeline_ptr->search_history("03614");
    TEST_ASSERT(id_search.size() == 1);
    TEST_ASSERT(id_search[0].display_name == "Shadow of the Erdtree");

    auto failed_search = pipeline_ptr->search_history("", InstallStatus::Failed);
    TEST_ASSERT(failed_search.size() == 1);
    TEST_ASSERT(failed_search[0].title_id == "PPSA03614");

    auto completed_search = pipeline_ptr->search_history("", InstallStatus::Completed);
    TEST_ASSERT(completed_search.size() == 1);
    TEST_ASSERT(completed_search[0].title_id == "PPSA01337");

    UiShell shell;
    shell.set_installer(pipeline_ptr);
    shell.show_install_history();
    TEST_ASSERT(shell.active_dialog() == DialogType::InstallHistory);
    TEST_ASSERT(shell.selected_history_item().has_value());

    shell.history_select_next();
    TEST_ASSERT(shell.selected_history_item().has_value());

    shell.set_history_search_query("Erdtree");
    auto selected = shell.selected_history_item();
    TEST_ASSERT(selected.has_value());
    TEST_ASSERT(selected->display_name == "Shadow of the Erdtree");

    std::cout << "PASS: test_searchable_history_and_diagnostics" << std::endl;
}

void test_history_csv_json_export() {
    auto delegate = std::make_shared<MockInstallDelegate>();
    InstallPipeline pipeline(delegate);

    GameItem item1;
    item1.kind = ItemKind::Game;
    item1.filename = "Game_Alpha,Special.pkg";
    item1.absolute_path = "/data/Game_Alpha,Special.pkg";
    item1.format = "pkg";
    item1.display_name = "Alpha \"Special\" Edition";
    std::string id1 = pipeline.queue_item(item1, "PPSA10001");
    pipeline.process_one_step();

    GameItem item2;
    item2.kind = ItemKind::Dlc;
    item2.filename = "DLC_Beta.ffpkg";
    item2.absolute_path = "/data/DLC_Beta.ffpkg";
    item2.format = "ffpkg";
    item2.display_name = "Beta Expansion";
    std::string id2 = pipeline.queue_item(item2, "PPSA10002");
    delegate->configure_failure("DLC_Beta.ffpkg",
                               InstallErrorCode::InsufficientSpace,
                               "Storage volume full,\nneed 50GB free");
    pipeline.process_one_step();

    // Retry item2
    delegate->reset();
    std::string id3 = pipeline.retry_task(id2);
    pipeline.process_one_step();

    auto history = pipeline.all_tasks();
    TEST_ASSERT(history.size() == 3);

    // Test JSON export
    std::string json_str = pipeline.export_history_to_json(history, 2);
    TEST_ASSERT(!json_str.empty());
    json parsed = json::parse(json_str);
    TEST_ASSERT(parsed.is_array());
    TEST_ASSERT(parsed.size() == 3);

    // Verify required fields in JSON
    for (const auto& j : parsed) {
        TEST_ASSERT(j.contains("task_id"));
        TEST_ASSERT(j.contains("title_id"));
        TEST_ASSERT(j.contains("package_filename"));
        TEST_ASSERT(j.contains("display_name"));
        TEST_ASSERT(j.contains("source_path"));
        TEST_ASSERT(j.contains("format"));
        TEST_ASSERT(j.contains("kind"));
        TEST_ASSERT(j.contains("total_bytes"));
        TEST_ASSERT(j.contains("processed_bytes"));
        TEST_ASSERT(j.contains("status"));
        TEST_ASSERT(j.contains("error_code"));
        TEST_ASSERT(j.contains("error_message"));
        TEST_ASSERT(j.contains("recovery_guidance"));
        TEST_ASSERT(j.contains("retry_count"));
        TEST_ASSERT(j.contains("original_task_id"));
        TEST_ASSERT(j.contains("original_error_code"));
        TEST_ASSERT(j.contains("created_timestamp"));
        TEST_ASSERT(j.contains("completed_timestamp"));
    }

    // Verify retry provenance in JSON for task 3
    TEST_ASSERT(parsed[2]["task_id"] == id3);
    TEST_ASSERT(parsed[2]["retry_count"] == 1);
    TEST_ASSERT(parsed[2]["original_task_id"] == id2);
    TEST_ASSERT(parsed[2]["original_error_code"] == "InsufficientSpace");

    // Test CSV export
    std::string csv_str = pipeline.export_history_to_csv(history);
    TEST_ASSERT(!csv_str.empty());
    TEST_ASSERT(csv_str.find("task_id,title_id,package_filename,display_name") != std::string::npos);
    TEST_ASSERT(csv_str.find("\"Alpha \"\"Special\"\" Edition\"") != std::string::npos);
    TEST_ASSERT(csv_str.find("\"Game_Alpha,Special.pkg\"") != std::string::npos);
    TEST_ASSERT(csv_str.find("PPSA10001") != std::string::npos);
    TEST_ASSERT(csv_str.find("PPSA10002") != std::string::npos);
    TEST_ASSERT(csv_str.find("InsufficientSpace") != std::string::npos);
    TEST_ASSERT(csv_str.find("retry_count") != std::string::npos);

    // Test file exports
    const std::string json_file = "/tmp/test_export_history.json";
    const std::string csv_file = "/tmp/test_export_history.csv";
    std::filesystem::remove(json_file);
    std::filesystem::remove(csv_file);

    TEST_ASSERT(pipeline.export_history_to_file(json_file, history, true));
    TEST_ASSERT(std::filesystem::exists(json_file));
    TEST_ASSERT(std::filesystem::file_size(json_file) > 100);

    TEST_ASSERT(pipeline.export_history_to_file(csv_file, history, false));
    TEST_ASSERT(std::filesystem::exists(csv_file));
    TEST_ASSERT(std::filesystem::file_size(csv_file) > 100);

    std::filesystem::remove(json_file);
    std::filesystem::remove(csv_file);
    std::cout << "PASS: test_history_csv_json_export" << std::endl;
}

void test_history_filters_and_sorting() {
    auto delegate = std::make_shared<MockInstallDelegate>();
    InstallPipeline pipeline(delegate);

    GameItem item1;
    item1.kind = ItemKind::Game;
    item1.filename = "Zelda_Clone.pkg";
    item1.absolute_path = "/data/Zelda_Clone.pkg";
    item1.format = "pkg";
    item1.display_name = "Zelda Clone";
    std::string id1 = pipeline.queue_item(item1, "PPSA00001");
    pipeline.process_one_step(); // Completed

    GameItem item2;
    item2.kind = ItemKind::Dlc;
    item2.filename = "Apex_Addon.ffpkg";
    item2.absolute_path = "/data/Apex_Addon.ffpkg";
    item2.format = "ffpkg";
    item2.display_name = "Apex Addon";
    std::string id2 = pipeline.queue_item(item2, "PPSA00002");
    delegate->configure_failure("Apex_Addon.ffpkg", InstallErrorCode::CorruptedHeader, "Bad header");
    pipeline.process_one_step(); // Failed

    GameItem item3;
    item3.kind = ItemKind::Update;
    item3.filename = "Cyber_Patch.exfat";
    item3.absolute_path = "/data/Cyber_Patch.exfat";
    item3.format = "exfat";
    item3.display_name = "Cyber Patch";
    std::string id3 = pipeline.queue_item(item3, "PPSA00003");
    // Leave item3 Queued

    // 1. Outcome Filter: Completed
    HistoryFilterOptions opt1;
    opt1.outcome_filter = InstallStatus::Completed;
    auto res_completed = pipeline.filter_and_sort_history(opt1);
    TEST_ASSERT(res_completed.size() == 1);
    TEST_ASSERT(res_completed[0].task_id == id1);

    // 2. Outcome Filter: Failed
    HistoryFilterOptions opt2;
    opt2.outcome_filter = InstallStatus::Failed;
    auto res_failed = pipeline.filter_and_sort_history(opt2);
    TEST_ASSERT(res_failed.size() == 1);
    TEST_ASSERT(res_failed[0].task_id == id2);

    // 3. Format Filter: ffpkg
    HistoryFilterOptions opt3;
    opt3.format_filter = "ffpkg";
    auto res_ffpkg = pipeline.filter_and_sort_history(opt3);
    TEST_ASSERT(res_ffpkg.size() == 1);
    TEST_ASSERT(res_ffpkg[0].format == "ffpkg");

    // 4. Format Filter: exfat
    HistoryFilterOptions opt4;
    opt4.format_filter = "exfat";
    auto res_exfat = pipeline.filter_and_sort_history(opt4);
    TEST_ASSERT(res_exfat.size() == 1);
    TEST_ASSERT(res_exfat[0].format == "exfat");

    // 5. Sort by PackageName Ascending: Apex -> Cyber -> Zelda
    HistoryFilterOptions opt_sort_name_asc;
    opt_sort_name_asc.sort_field = HistorySortField::PackageName;
    opt_sort_name_asc.sort_order = HistorySortOrder::Ascending;
    auto res_name_asc = pipeline.filter_and_sort_history(opt_sort_name_asc);
    TEST_ASSERT(res_name_asc.size() == 3);
    TEST_ASSERT(res_name_asc[0].package_filename == "Apex_Addon.ffpkg");
    TEST_ASSERT(res_name_asc[1].package_filename == "Cyber_Patch.exfat");
    TEST_ASSERT(res_name_asc[2].package_filename == "Zelda_Clone.pkg");

    // 6. Sort by PackageName Descending: Zelda -> Cyber -> Apex
    HistoryFilterOptions opt_sort_name_desc;
    opt_sort_name_desc.sort_field = HistorySortField::PackageName;
    opt_sort_name_desc.sort_order = HistorySortOrder::Descending;
    auto res_name_desc = pipeline.filter_and_sort_history(opt_sort_name_desc);
    TEST_ASSERT(res_name_desc.size() == 3);
    TEST_ASSERT(res_name_desc[0].package_filename == "Zelda_Clone.pkg");
    TEST_ASSERT(res_name_desc[2].package_filename == "Apex_Addon.ffpkg");

    // 7. Date range filter
    std::string now_ts = res_name_asc[0].created_timestamp;
    HistoryFilterOptions opt_date_match;
    opt_date_match.date_from = now_ts.substr(0, 10); // YYYY-MM-DD
    auto res_date_match = pipeline.filter_and_sort_history(opt_date_match);
    TEST_ASSERT(res_date_match.size() == 3);

    HistoryFilterOptions opt_date_nomatch;
    opt_date_nomatch.date_from = "2099-01-01T00:00:00Z";
    auto res_date_nomatch = pipeline.filter_and_sort_history(opt_date_nomatch);
    TEST_ASSERT(res_date_nomatch.empty());

    std::cout << "PASS: test_history_filters_and_sorting" << std::endl;
}

void test_per_check_preflight_summary_details() {
    const std::string tmp_dir = "/tmp/ps5_preflight_summary_test";
    std::filesystem::create_directories(tmp_dir);

    // 1. Missing file: readability check fails
    std::string missing = tmp_dir + "/does_not_exist.pkg";
    PreflightResult res1 = run_package_preflight(missing, tmp_dir);
    TEST_ASSERT(!res1.passed);
    TEST_ASSERT(!res1.summary.readability.passed);
    TEST_ASSERT(!res1.summary.all_passed);
    std::string summary_str1 = res1.summary.formatted_summary();
    TEST_ASSERT(summary_str1.find("[FAIL] File Readability:") != std::string::npos);

    // 2. Corrupt header file: readability passes, metadata fails
    std::string corrupt = tmp_dir + "/corrupt.pkg";
    create_dummy_file(corrupt, "BAD_DATA_NOT_A_VALID_PKG");
    PreflightResult res2 = run_package_preflight(corrupt, tmp_dir);
    TEST_ASSERT(!res2.passed);
    TEST_ASSERT(res2.summary.readability.passed);
    TEST_ASSERT(!res2.summary.metadata.passed);
    std::string summary_str2 = res2.summary.formatted_summary();
    TEST_ASSERT(summary_str2.find("[PASS] File Readability:") != std::string::npos);
    TEST_ASSERT(summary_str2.find("[FAIL] Package Metadata:") != std::string::npos);

    // 3. Valid PKG with valid header: all checks pass
    std::string valid = tmp_dir + "/valid_game.pkg";
    std::string content = "\x7FPKG\x00\x00\x00\x01";
    content.resize(4096, 0);
    create_dummy_file(valid, content);
    PreflightResult res3 = run_package_preflight(valid, tmp_dir);
    TEST_ASSERT(res3.passed);
    TEST_ASSERT(res3.summary.readability.passed);
    TEST_ASSERT(res3.summary.metadata.passed);
    TEST_ASSERT(res3.summary.storage.passed);
    TEST_ASSERT(res3.summary.all_passed);
    std::string summary_str3 = res3.summary.formatted_summary();
    TEST_ASSERT(summary_str3.find("[PASS] File Readability:") != std::string::npos);
    TEST_ASSERT(summary_str3.find("[PASS] Package Metadata:") != std::string::npos);
    TEST_ASSERT(summary_str3.find("[PASS] Available Storage:") != std::string::npos);

    // 4. Test UiShell show_preflight_summary dialog
    UiShell shell;
    GameItem item;
    item.filename = "valid_game.pkg";
    item.absolute_path = valid;
    item.format = "pkg";
    item.display_name = "Valid Test Game";
    item.size_bytes = 4096;
    item.kind = ItemKind::Game;

    shell.show_preflight_summary(item, "PPSA77777");
    TEST_ASSERT(shell.active_dialog() == DialogType::PreflightSummary);
    TEST_ASSERT(shell.dialog_model().title.find("valid_game.pkg") != std::string::npos);
    TEST_ASSERT(shell.dialog_model().secondary_text.find("[PASS] File Readability:") != std::string::npos);
    TEST_ASSERT(shell.dialog_model().secondary_text.find("[PASS] Available Storage:") != std::string::npos);
    TEST_ASSERT(!shell.dialog_model().options.empty());
    TEST_ASSERT(shell.dialog_model().options[0] == "Proceed with Install");

    std::filesystem::remove_all(tmp_dir);
    std::cout << "PASS: test_per_check_preflight_summary_details" << std::endl;
}

void test_retry_confirmation_dialog_workflow() {
    const std::string tmp_dir = "/tmp/ps5_retry_confirm_test";
    std::filesystem::create_directories(tmp_dir);

    auto delegate = std::make_shared<MockInstallDelegate>();
    auto pipeline = std::make_shared<InstallPipeline>(delegate);

    std::string pkg_path = tmp_dir + "/RetryTest.pkg";
    std::string content = "\x7FPKG\x00\x00\x00\x01";
    content.resize(2048, 0);
    create_dummy_file(pkg_path, content);

    GameItem item;
    item.filename = "RetryTest.pkg";
    item.absolute_path = pkg_path;
    item.format = "pkg";
    item.display_name = "Retry Test Game";
    item.size_bytes = 2048;

    std::string task_id = pipeline->queue_item(item, "PPSA88888");
    delegate->configure_failure("RetryTest.pkg",
                               InstallErrorCode::InsufficientSpace,
                               "Temporary partition locked");
    pipeline->process_one_step();

    auto failed_task = pipeline->get_task(task_id);
    TEST_ASSERT(failed_task.has_value());
    TEST_ASSERT(failed_task->status == InstallStatus::Failed);

    UiShell shell;
    shell.set_installer(pipeline);

    // Open retry confirmation dialog
    shell.show_retry_confirmation(*failed_task);
    TEST_ASSERT(shell.active_dialog() == DialogType::RetryConfirmation);
    TEST_ASSERT(shell.dialog_model().title.find("Retry Test Game") != std::string::npos);
    TEST_ASSERT(shell.dialog_model().secondary_text.find("Original Failure Details:") != std::string::npos);
    TEST_ASSERT(shell.dialog_model().secondary_text.find("InsufficientSpace") != std::string::npos);
    TEST_ASSERT(shell.dialog_model().secondary_text.find("Temporary partition locked") != std::string::npos);
    // And verifies preflight summary was rerun:
    TEST_ASSERT(shell.dialog_model().secondary_text.find("[PASS] File Readability:") != std::string::npos);
    TEST_ASSERT(shell.dialog_model().options[0] == "Confirm Retry");

    // Feed pad input: Cross on "Confirm Retry"
    delegate->reset();
    InputManager input;
    input.feed_raw_buttons(pad_bits::kCross);
    const bool handled = shell.handle_pad_input(input);
    TEST_ASSERT(handled);
    TEST_ASSERT(shell.active_dialog() == DialogType::None);

    // Job should now be requeued in pipeline!
    TEST_ASSERT(pipeline->queued_tasks().size() == 1);
    auto retried_task = pipeline->queued_tasks().at(0);
    TEST_ASSERT(retried_task.retry_count == 1);
    TEST_ASSERT(retried_task.original_task_id == task_id);
    TEST_ASSERT(retried_task.original_error_code == InstallErrorCode::InsufficientSpace);

    std::filesystem::remove_all(tmp_dir);
    std::cout << "PASS: test_retry_confirmation_dialog_workflow" << std::endl;
}


void test_preflight_recheck_immediate_diff_and_reporting() {
    std::string tmp_dir = "/tmp/test_preflight_diff_dir";
    std::filesystem::remove_all(tmp_dir);
    std::filesystem::create_directories(tmp_dir);

    // 1. Unit diffing tests
    PreflightCheckSummary base;
    base.all_passed = true;
    base.readability = {"File Readability", true, "Exists and readable (10.0 MB)", "", ""};
    base.metadata = {"Package Metadata", true, "Recognized PKG package magic", "", ""};
    base.storage = {"Available Storage", true, "Required: 510.0 MB, Available: 50.0 GB", "", ""};

    PreflightCheckSummary deleted_state = base;
    deleted_state.all_passed = false;
    deleted_state.readability = {"File Readability", false, "", "File not found", "Verify path"};

    auto diffs = diff_preflight_summaries(base, deleted_state);
    TEST_ASSERT(diffs.size() == 1);
    TEST_ASSERT(diffs[0].check_name == "File Readability");
    TEST_ASSERT(diffs[0].status_changed);
    TEST_ASSERT(diffs[0].previously_passed);
    TEST_ASSERT(!diffs[0].currently_passed);

    std::string formatted = format_preflight_diffs(diffs);
    TEST_ASSERT(formatted.find("--- Preflight Status Changes Detected ---") != std::string::npos);
    TEST_ASSERT(formatted.find("File Readability: Status changed from PASSED to FAILED") != std::string::npos);

    // 2. Pipeline immediate re-check before dispatch detects deleted file
    std::string pkg_path = tmp_dir + "/recheck_test.pkg";
    std::string pkg_data = std::string("\x7FPKG", 4) + std::string(1024, 'A');
    create_dummy_file(pkg_path, pkg_data);

    auto delegate = std::make_shared<MockInstallDelegate>();
    InstallPipeline pipeline(delegate);
    pipeline.set_target_storage_dir(tmp_dir);
    pipeline.set_enable_preflight(true);

    GameItem item;
    item.filename = "recheck_test.pkg";
    item.absolute_path = pkg_path;
    item.format = "pkg";
    item.size_bytes = pkg_data.size();

    const auto initial_preflight = run_package_preflight(pkg_path, tmp_dir);
    TEST_ASSERT(initial_preflight.passed);

    std::string task_id = pipeline.queue_item(item, "PPSA77777", initial_preflight.summary);
    TEST_ASSERT(!task_id.empty());

    // Delete the file before worker dispatch step
    std::filesystem::remove(pkg_path);

    // Run step - immediate preflight recheck occurs right before dispatch
    pipeline.process_one_step();

    auto failed_task = pipeline.get_task(task_id);
    TEST_ASSERT(failed_task.has_value());
    TEST_ASSERT(failed_task->status == InstallStatus::Failed);
    TEST_ASSERT(!failed_task->preflight_changes.empty());
    TEST_ASSERT(failed_task->error_message.find("Preflight check failed immediately before dispatch") != std::string::npos);
    TEST_ASSERT(failed_task->error_message.find("File Readability") != std::string::npos);

    // 3. UI Shell preflight summary immediate recheck diff display
    create_dummy_file(pkg_path, pkg_data);
    auto pipeline_ptr = std::make_shared<InstallPipeline>(delegate);
    pipeline_ptr->set_target_storage_dir(tmp_dir);
    pipeline_ptr->set_enable_preflight(true);
    UiShell shell;
    shell.set_installer(pipeline_ptr);

    shell.show_preflight_summary(item, "PPSA77777");
    TEST_ASSERT(shell.active_dialog() == DialogType::PreflightSummary);
    TEST_ASSERT(shell.dialog_model().title.find("recheck_test.pkg") != std::string::npos);
    TEST_ASSERT(shell.dialog_model().options[0] == "Proceed with Install");

    // Remove file while summary dialog is open
    std::filesystem::remove(pkg_path);

    // User presses Cross to proceed with install -> immediate recheck diff triggered!
    InputManager input;
    input.feed_raw_buttons(pad_bits::kCross);
    bool handled = shell.handle_pad_input(input);
    TEST_ASSERT(handled);
    // Dialog should now show diff changes before dispatch!
    TEST_ASSERT(shell.active_dialog() == DialogType::PreflightSummary);
    TEST_ASSERT(shell.dialog_model().title == "Preflight Results Changed Before Dispatch");
    TEST_ASSERT(shell.dialog_model().secondary_text.find("File Readability") != std::string::npos);
    TEST_ASSERT(shell.dialog_model().options[0] == "Dismiss");

    std::filesystem::remove_all(tmp_dir);
    std::cout << "PASS: test_preflight_recheck_immediate_diff_and_reporting" << std::endl;
}

void test_export_file_picker_and_configurable_filenames() {
    std::string tmp_dir = "/tmp/test_export_picker_dir";
    std::filesystem::remove_all(tmp_dir);
    std::filesystem::create_directories(tmp_dir);

    auto delegate = std::make_shared<MockInstallDelegate>();
    auto pipeline = std::make_shared<InstallPipeline>(delegate);

    GameItem item;
    item.filename = "ExportSample.pkg";
    item.absolute_path = "/data/ExportSample.pkg";
    item.format = "pkg";
    item.display_name = "Export Sample";
    item.size_bytes = 2048;
    pipeline->queue_item(item, "PPSA11111");

    UiShell shell;
    shell.set_installer(pipeline);

    // 1. Open export file picker
    shell.open_export_file_picker(true);
    TEST_ASSERT(shell.active_dialog() == DialogType::ExportFilePicker);
    TEST_ASSERT(shell.export_as_json() == true);
    TEST_ASSERT(shell.dialog_model().title == "Export Install History");

    // 2. Format toggle
    shell.toggle_export_format();
    TEST_ASSERT(shell.export_as_json() == false);
    TEST_ASSERT(shell.export_full_path().find(".csv") != std::string::npos);

    // 3. Preset cycling
    std::string orig_dir = shell.export_directory();
    shell.cycle_export_directory();
    TEST_ASSERT(shell.export_directory() != orig_dir);

    std::string orig_name = shell.export_filename();
    shell.cycle_export_filename_preset();
    TEST_ASSERT(shell.export_filename() != orig_name);

    // 4. Custom directory and filename configuration
    shell.set_export_directory(tmp_dir);
    shell.set_export_filename("custom_audit_history");
    shell.set_export_as_json(true);
    std::string expected_path = tmp_dir + "/custom_audit_history.json";
    TEST_ASSERT(shell.export_full_path() == expected_path);

    // 5. Successful export execution with clear feedback
    bool success = shell.execute_export();
    TEST_ASSERT(success);
    TEST_ASSERT(shell.active_dialog() == DialogType::Notification);
    TEST_ASSERT(shell.dialog_model().title == "Export Succeeded");
    TEST_ASSERT(shell.dialog_model().secondary_text.find("custom_audit_history.json") != std::string::npos);
    TEST_ASSERT(std::filesystem::exists(expected_path));

    // Verify valid JSON content written
    std::ifstream in(expected_path);
    json exported_json;
    in >> exported_json;
    TEST_ASSERT(exported_json.is_array());
    TEST_ASSERT(exported_json.size() == 1);
    TEST_ASSERT(exported_json[0]["title_id"] == "PPSA11111");

    // 6. Failure feedback on unwritable/invalid destination
    shell.set_export_directory("/dev/null/invalid_exports");
    shell.set_export_filename("fail_test");
    bool fail_res = shell.execute_export();
    TEST_ASSERT(!fail_res);
    TEST_ASSERT(shell.active_dialog() == DialogType::Notification);
    TEST_ASSERT(shell.dialog_model().title == "Export Failed");
    TEST_ASSERT(shell.dialog_model().secondary_text.find("Remediation:") != std::string::npos);

    // 7. Controller navigation in ExportFilePicker
    shell.open_export_file_picker(true);
    InputManager input;

    // Square toggles format
    input.feed_raw_buttons(pad_bits::kSquare);
    shell.handle_pad_input(input);
    TEST_ASSERT(shell.export_as_json() == false);

    // Circle cancels back to InstallHistory
    input.feed_raw_buttons(pad_bits::kCircle);
    shell.handle_pad_input(input);
    TEST_ASSERT(shell.active_dialog() == DialogType::InstallHistory);

    std::filesystem::remove_all(tmp_dir);
    std::cout << "PASS: test_export_file_picker_and_configurable_filenames" << std::endl;
}

void test_saved_history_filters_and_cross_session_persistence() {
    std::string tmp_dir = "/tmp/test_history_pref_dir";
    std::filesystem::remove_all(tmp_dir);
    std::filesystem::create_directories(tmp_dir);

    std::string state_path = tmp_dir + "/install_state.json";

    // 1. Session 1: Create UiShell (which owns the pipeline for state_path), add items, set custom filters and sort
    {
        UiShell shell(nullptr, tmp_dir + "/cache.json", state_path);
        auto pipeline = shell.installer();
        TEST_ASSERT(pipeline != nullptr);

        GameItem g1; g1.filename = "Alpha.pkg"; g1.format = "pkg"; g1.size_bytes = 1000;
        GameItem g2; g2.filename = "Beta.ffpkg"; g2.format = "ffpkg"; g2.size_bytes = 2000;
        pipeline->queue_item(g1, "PPSA00001");
        pipeline->queue_item(g2, "PPSA00002");

        shell.show_install_history();

        // Change filters and sort
        shell.set_history_outcome_filter(InstallStatus::Queued);
        shell.set_history_format_filter("pkg");
        shell.set_history_sort(HistorySortField::PackageName, HistorySortOrder::Ascending);

        // Close history dialog
        shell.dismiss_dialog();
        TEST_ASSERT(shell.active_dialog() == DialogType::None);

        // Reopen history dialog - verify restored within session
        shell.show_install_history();
        TEST_ASSERT(shell.history_filter_options().outcome_filter == InstallStatus::Queued);
        TEST_ASSERT(shell.history_filter_options().format_filter == "pkg");
        TEST_ASSERT(shell.history_filter_options().sort_field == HistorySortField::PackageName);
        TEST_ASSERT(shell.history_filter_options().sort_order == HistorySortOrder::Ascending);
    }

    // 2. Session 2: Fresh start reading from persistence state file
    {
        UiShell session2_shell(nullptr, tmp_dir + "/cache2.json", state_path);
        session2_shell.show_install_history();

        // Verify restored across sessions from disk state!
        TEST_ASSERT(session2_shell.history_filter_options().outcome_filter == InstallStatus::Queued);
        TEST_ASSERT(session2_shell.history_filter_options().format_filter == "pkg");
        TEST_ASSERT(session2_shell.history_filter_options().sort_field == HistorySortField::PackageName);
        TEST_ASSERT(session2_shell.history_filter_options().sort_order == HistorySortOrder::Ascending);
    }

    std::filesystem::remove_all(tmp_dir);
    std::cout << "PASS: test_saved_history_filters_and_cross_session_persistence" << std::endl;
}

int main() {
    test_duplicate_preflight_and_blocking();
    test_retry_provenance_and_preservation();
    test_package_preflight_checks();
    test_searchable_history_and_diagnostics();
    test_history_csv_json_export();
    test_history_filters_and_sorting();
    test_per_check_preflight_summary_details();
    test_retry_confirmation_dialog_workflow();
    test_preflight_recheck_immediate_diff_and_reporting();
    test_export_file_picker_and_configurable_filenames();
    test_saved_history_filters_and_cross_session_persistence();
    std::cout << "\nALL 11 TEST SUITES PASSED PERFECTLY!" << std::endl;
    return 0;
}
