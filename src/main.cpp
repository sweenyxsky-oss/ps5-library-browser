/**
 * PS5 Library Browser - Native Payload & Application Entrypoint
 * 
 * Target: Sony PlayStation 5 (Prospero) & Host Development
 * License: GNU General Public License v3.0
 */

#include "ps5_library/daemon_client.hpp"
#include "ps5_library/game_metadata.hpp"
#include "ps5_library/index_cache.hpp"
#include "ps5_library/input.hpp"
#include "ps5_library/inspector.hpp"
#include "ps5_library/installer.hpp"
#include "ps5_library/scanner.hpp"
#include "ps5_library/startup_diagnostics.hpp"
#include "ps5_library/ui_shell.hpp"
#include "ps5_library/views.hpp"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#if defined(__PROSPERO__) || defined(__PS5__)
#define PS5_PLATFORM 1
#else
#define PS5_PLATFORM 0
#endif

namespace {
volatile std::sig_atomic_t g_running = 1;

void handle_signal(int sig) {
    (void)sig;
    g_running = 0;
}

std::string resolve_default_path(const std::string& preferred, const std::string& fallback) {
    std::error_code ec;
    if (std::filesystem::exists(preferred, ec)) {
        return preferred;
    }
    return fallback;
}
} // namespace

int main(int argc, char* argv[]) {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    std::cout << "====================================================\n";
    std::cout << "  PS5 Library Browser - Native Launcher v1.0.0     \n";
#if PS5_PLATFORM
    std::cout << "  Target Platform: Sony PlayStation 5 (Prospero)   \n";
#else
    std::cout << "  Target Platform: Host / Simulator Mode           \n";
#endif
    std::cout << "====================================================\n" << std::endl;

    // Parse command line arguments
    std::string root_scan_dir = "/hostapp";
    std::string cache_path = "/data/ps5_library_cache.json";
    std::string install_state_path = "/data/ps5_library_install_jobs.json";
    std::string daemon_host = "127.0.0.1";
    uint16_t daemon_port = 10101;
    bool headless = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--headless") {
            headless = true;
        } else if (arg == "--scan-dir" && i + 1 < argc) {
            root_scan_dir = argv[++i];
        } else if (arg == "--cache" && i + 1 < argc) {
            cache_path = argv[++i];
        } else if (arg == "--jobs" && i + 1 < argc) {
            install_state_path = argv[++i];
        } else if (arg == "--daemon-port" && i + 1 < argc) {
            daemon_port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: ps5-library-browser.elf [options]\n"
                      << "  --scan-dir <path>     Root library scan directory (default: /hostapp)\n"
                      << "  --cache <path>        Path to index cache file (default: /data/ps5_library_cache.json)\n"
                      << "  --jobs <path>         Path to job persistence file (default: /data/ps5_library_install_jobs.json)\n"
                      << "  --daemon-port <port>  ShadowMountPlus daemon port (default: 10101)\n"
                      << "  --headless            Run single-pass scan and state verification then exit\n";
            return 0;
        }
    }

    // Resolve directories for host vs target PS5
#if !PS5_PLATFORM
    cache_path = resolve_default_path(cache_path, "/tmp/ps5_library_cache.json");
    install_state_path = resolve_default_path(install_state_path, "/tmp/ps5_library_install_jobs.json");
    if (!std::filesystem::exists(root_scan_dir)) {
        root_scan_dir = "/tmp";
    }
#endif

    std::cout << "[Init] Library Scan Root : " << root_scan_dir << "\n";
    std::cout << "[Init] Index Cache File  : " << cache_path << "\n";
    std::cout << "[Init] Job State File    : " << install_state_path << "\n";
    std::cout << "[Init] ShadowMount Daemon: " << daemon_host << ":" << daemon_port << "\n\n";

    // 1. Run Structured Startup Diagnostics (Mount, Daemon, Network, Services)
    ps5_library::StartupDiagnosticsConfig diag_config;
    diag_config.mount_path = root_scan_dir;
    diag_config.daemon_host = daemon_host;
    diag_config.daemon_port = daemon_port;
    diag_config.cache_path = cache_path;
    diag_config.jobs_path = install_state_path;

    std::cout << "[Diagnostics] Running startup health checks...\n";
    ps5_library::StartupDiagnosticsEngine diag_engine(diag_config);
    auto diag_report = diag_engine.run_all_checks();
    std::cout << diag_report.format_detailed_report() << "\n\n";

    // 2. Initialize ShadowMountPlus Daemon Probe & Feature Lock
    auto daemon_client = std::make_shared<ps5_library::DaemonLock>(daemon_host, daemon_port);
    daemon_client->probe();
    const bool daemon_online = daemon_client->is_unlocked();
    std::cout << "[Probe] ShadowMountPlus Daemon Status: " 
              << (daemon_online ? "ONLINE (Features Unlocked)" : "OFFLINE (Remote Features Locked)") << "\n";

    // 3. Initialize UI Shell, Index Cache & Install Pipeline
    ps5_library::UiShell shell(daemon_client, cache_path, install_state_path);
    shell.set_scan_path(root_scan_dir);

    // Present on-screen startup diagnostics dialog if warnings or failures were detected
    if (diag_report.has_failures() || diag_report.has_warnings()) {
        std::cout << "[Diagnostics] Active issues detected. Presenting on-screen recovery guidance dialog.\n";
        shell.show_startup_diagnostics(diag_report);
    }

    // 4. Perform initial scan of root path
    std::cout << "[Scanner] Probing directory: " << root_scan_dir << "...\n";
    bool scan_finished = false;
    shell.trigger_scan(true, [&](bool success) {
        (void)success;
        scan_finished = true;
    });

    // Poll until initial scan completes
    while (!scan_finished && g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    const size_t total_games = shell.filtered_entries().size();
    std::cout << "[Scanner] Scan complete. Found " << total_games << " game entries.\n";

    if (headless) {
        std::cout << "[Headless] Execution finished successfully.\n";
        return 0;
    }

    // 5. Interactive Application Loop
    std::cout << "[App] Entering interactive UI loop. Ready for DualSense input.\n";
    ps5_library::InputManager input_manager;

    while (g_running) {
        // Poll for inputs and tick animations / background workers
        shell.handle_pad_input(input_manager);
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 Hz tick
    }

    std::cout << "\n[Shutdown] Shutting down PS5 Library Browser cleanly...\n";
    return 0;
}
