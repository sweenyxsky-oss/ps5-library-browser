#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include <memory>

namespace ps5_library {

enum class DiagnosticSeverity {
    Ok,
    Info,
    Warning,
    Error
};

enum class DiagnosticSubsystem {
    Mount,
    Daemon,
    Network,
    Service
};

struct DiagnosticItem {
    DiagnosticSubsystem subsystem{DiagnosticSubsystem::Service};
    std::string name;
    DiagnosticSeverity severity{DiagnosticSeverity::Ok};
    std::string summary;
    std::string details;
    std::string recovery_guidance;
    int error_code{0};

    bool is_ok() const { return severity == DiagnosticSeverity::Ok || severity == DiagnosticSeverity::Info; }
    bool is_failure() const { return severity == DiagnosticSeverity::Error; }
    bool is_warning() const { return severity == DiagnosticSeverity::Warning; }
    std::string severity_string() const;
    std::string subsystem_string() const;
};

struct StartupDiagnosticsConfig {
    std::string mount_path{"/hostapp"};
    std::string daemon_host{"127.0.0.1"};
    uint16_t daemon_port{10101};
    std::string cache_path{"/data/ps5_library_cache.json"};
    std::string jobs_path{"/data/ps5_library_install_jobs.json"};
    bool check_network{true};
    int probe_timeout_ms{500};
};

struct StartupDiagnosticsReport {
    std::vector<DiagnosticItem> items;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};

    bool all_ok() const;
    bool has_failures() const;
    bool has_warnings() const;
    size_t failure_count() const;
    size_t warning_count() const;
    std::string format_summary() const;
    std::string format_detailed_report() const;
    std::string format_on_screen_guidance() const;
    std::vector<DiagnosticItem> failed_or_warning_items() const;
};

class StartupDiagnosticsEngine {
public:
    explicit StartupDiagnosticsEngine(StartupDiagnosticsConfig config = {});

    StartupDiagnosticsReport run_all_checks();
    DiagnosticItem check_mount();
    DiagnosticItem check_daemon();
    DiagnosticItem check_network();
    DiagnosticItem check_services();

    const StartupDiagnosticsConfig& config() const { return config_; }
    void set_config(const StartupDiagnosticsConfig& config) { config_ = config; }

private:
    StartupDiagnosticsConfig config_;
};

} // namespace ps5_library
