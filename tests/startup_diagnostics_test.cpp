#include "ps5_library/startup_diagnostics.hpp"
#include "ps5_library/ui_shell.hpp"
#include "ps5_library/input.hpp"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#define TEST_ASSERT(cond)                                                                     \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ << ": " #cond \
                      << std::endl;                                                           \
            std::abort();                                                                     \
        }                                                                                     \
    } while (0)

namespace {

void test_mount_diagnostics() {
    std::cout << "[Test] Running mount diagnostics tests..." << std::endl;

    ps5_library::StartupDiagnosticsConfig config;
    
    // 1. Non-existent path
    config.mount_path = "/tmp/ps5_test_nonexistent_mount_dir_12345";
    std::filesystem::remove_all(config.mount_path);
    ps5_library::StartupDiagnosticsEngine engine(config);
    auto item = engine.check_mount();
    TEST_ASSERT(item.is_failure());
    TEST_ASSERT(item.severity == ps5_library::DiagnosticSeverity::Error);
    TEST_ASSERT(!item.recovery_guidance.empty());
    TEST_ASSERT(item.recovery_guidance.find("USB") != std::string::npos ||
                item.recovery_guidance.find("mount") != std::string::npos);

    // 2. Path exists but is a regular file
    std::string file_mount = "/tmp/ps5_test_file_mount.txt";
    {
        std::ofstream out(file_mount);
        out << "not a directory";
    }
    config.mount_path = file_mount;
    engine.set_config(config);
    item = engine.check_mount();
    TEST_ASSERT(item.is_failure());
    TEST_ASSERT(item.error_code == ENOTDIR);
    std::filesystem::remove(file_mount);

    // 3. Empty directory -> Warning
    std::string empty_dir = "/tmp/ps5_test_empty_mount_dir";
    std::filesystem::create_directories(empty_dir);
    config.mount_path = empty_dir;
    engine.set_config(config);
    item = engine.check_mount();
    TEST_ASSERT(item.is_warning());
    TEST_ASSERT(!item.recovery_guidance.empty());
    TEST_ASSERT(item.summary.find("empty") != std::string::npos);

    // 4. Populated directory -> Ok
    std::string test_file = empty_dir + "/test_game.pkg";
    {
        std::ofstream out(test_file);
        out << "pkg";
    }
    item = engine.check_mount();
    TEST_ASSERT(item.is_ok());
    TEST_ASSERT(item.severity == ps5_library::DiagnosticSeverity::Ok);

    std::filesystem::remove_all(empty_dir);
    std::cout << "  Mount diagnostics passed." << std::endl;
}

void test_daemon_diagnostics() {
    std::cout << "[Test] Running daemon diagnostics tests..." << std::endl;

    ps5_library::StartupDiagnosticsConfig config;
    config.daemon_host = "127.0.0.1";
    // Choose an unused port
    config.daemon_port = 59871;
    config.probe_timeout_ms = 100;

    ps5_library::StartupDiagnosticsEngine engine(config);
    auto item = engine.check_daemon();
    TEST_ASSERT(item.is_warning());
    TEST_ASSERT(item.subsystem == ps5_library::DiagnosticSubsystem::Daemon);
    TEST_ASSERT(item.summary.find("offline") != std::string::npos || item.summary.find("unreachable") != std::string::npos);
    TEST_ASSERT(item.recovery_guidance.find("ShadowMountPlus") != std::string::npos);

    // Spin up a mock server on port 59871
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in sin{};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(59871);
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(server_sock, (struct sockaddr*)&sin, sizeof(sin)) == 0 && listen(server_sock, 1) == 0) {
        auto online_item = engine.check_daemon();
        TEST_ASSERT(online_item.is_ok());
        TEST_ASSERT(online_item.summary.find("online") != std::string::npos);
        close(server_sock);
    } else {
        if (server_sock >= 0) close(server_sock);
    }

    std::cout << "  Daemon diagnostics passed." << std::endl;
}

void test_network_and_service_diagnostics() {
    std::cout << "[Test] Running network & service diagnostics tests..." << std::endl;

    ps5_library::StartupDiagnosticsConfig config;
    config.mount_path = "/tmp";
    config.cache_path = "/tmp/ps5_test_diag_cache.json";
    config.jobs_path = "/tmp/ps5_test_diag_jobs.json";

    ps5_library::StartupDiagnosticsEngine engine(config);
    
    // Network test
    auto net_item = engine.check_network();
    TEST_ASSERT(net_item.is_ok());
    TEST_ASSERT(net_item.subsystem == ps5_library::DiagnosticSubsystem::Network);

    // Services test (valid paths)
    auto svc_item = engine.check_services();
    TEST_ASSERT(svc_item.is_ok());
    TEST_ASSERT(svc_item.subsystem == ps5_library::DiagnosticSubsystem::Service);

    // Services test (invalid unwritable path)
    config.jobs_path = "/proc/invalid_nonexistent_jobs_file_forbidden/jobs.json";
    engine.set_config(config);
    auto bad_svc_item = engine.check_services();
    TEST_ASSERT(bad_svc_item.is_failure());
    TEST_ASSERT(!bad_svc_item.recovery_guidance.empty());

    // Report aggregation
    config.jobs_path = "/tmp/ps5_test_diag_jobs.json";
    engine.set_config(config);
    auto report = engine.run_all_checks();
    TEST_ASSERT(!report.items.empty());
    TEST_ASSERT(!report.format_summary().empty());
    TEST_ASSERT(!report.format_detailed_report().empty());

    std::cout << "  Network & service diagnostics passed." << std::endl;
}

void test_ui_shell_diagnostics_integration() {
    std::cout << "[Test] Running UI Shell diagnostics integration tests..." << std::endl;

    ps5_library::StartupDiagnosticsConfig config;
    config.mount_path = "/tmp/test_nonexistent_for_ui";
    config.cache_path = "/tmp/test_ui_cache.json";
    config.jobs_path = "/tmp/test_ui_jobs.json";

    ps5_library::StartupDiagnosticsEngine engine(config);
    auto report = engine.run_all_checks();
    TEST_ASSERT(report.has_failures() || report.has_warnings());

    ps5_library::UiShell shell(nullptr, config.cache_path, config.jobs_path);
    shell.show_startup_diagnostics(report);

    TEST_ASSERT(shell.active_dialog() == ps5_library::DialogType::StartupDiagnostics);
    const auto& model = shell.dialog_model();
    TEST_ASSERT(model.title == "Startup Diagnostics & Recovery");
    TEST_ASSERT(!model.options.empty());
    TEST_ASSERT(!model.secondary_text.empty());

    // Contextual prompts
    auto prompts = shell.get_contextual_prompts();
    bool found_cross = false;
    bool found_circle = false;
    bool found_triangle = false;
    for (const auto& p : prompts) {
        if (p.glyph == ps5_library::ButtonGlyph::Cross) found_cross = true;
        if (p.glyph == ps5_library::ButtonGlyph::Circle) found_circle = true;
        if (p.glyph == ps5_library::ButtonGlyph::Triangle) found_triangle = true;
    }
    TEST_ASSERT(found_cross);
    TEST_ASSERT(found_circle);
    TEST_ASSERT(found_triangle);

    // Test input: NavDown and NavUp
    ps5_library::InputManager input;
    input.feed_raw_buttons(ps5_library::pad_bits::kDown);
    shell.handle_pad_input(input);
    TEST_ASSERT(shell.dialog_model().selected_option == 1);

    input.feed_raw_buttons(0);
    input.feed_raw_buttons(ps5_library::pad_bits::kUp);
    shell.handle_pad_input(input);
    TEST_ASSERT(shell.dialog_model().selected_option == 0);

    // Test toggle details via Triangle / LogicalAction::ToggleMode
    input.feed_raw_buttons(0);
    input.feed_raw_buttons(ps5_library::pad_bits::kTriangle);
    shell.handle_pad_input(input);
    TEST_ASSERT(shell.dialog_model().message == report.format_detailed_report());

    // Toggle back
    input.feed_raw_buttons(0);
    input.feed_raw_buttons(ps5_library::pad_bits::kTriangle);
    shell.handle_pad_input(input);
    TEST_ASSERT(shell.dialog_model().message == report.format_summary());

    // Test dismiss via Circle
    input.feed_raw_buttons(0);
    input.feed_raw_buttons(ps5_library::pad_bits::kCircle);
    shell.handle_pad_input(input);
    TEST_ASSERT(shell.active_dialog() == ps5_library::DialogType::None);

    std::cout << "  UI Shell diagnostics integration passed." << std::endl;
}

} // namespace

int main() {
    std::cout << "========================================\n";
    std::cout << "  Startup Diagnostics & Recovery Test  \n";
    std::cout << "========================================\n" << std::endl;

    test_mount_diagnostics();
    test_daemon_diagnostics();
    test_network_and_service_diagnostics();
    test_ui_shell_diagnostics_integration();

    std::cout << "\nAll 4 startup diagnostics test suites PASSED successfully." << std::endl;
    return 0;
}
