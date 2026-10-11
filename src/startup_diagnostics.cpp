#include "ps5_library/startup_diagnostics.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <netinet/in.h>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

namespace ps5_library {

std::string DiagnosticItem::severity_string() const {
    switch (severity) {
        case DiagnosticSeverity::Ok: return "OK";
        case DiagnosticSeverity::Info: return "INFO";
        case DiagnosticSeverity::Warning: return "WARNING";
        case DiagnosticSeverity::Error: return "ERROR";
    }
    return "UNKNOWN";
}

std::string DiagnosticItem::subsystem_string() const {
    switch (subsystem) {
        case DiagnosticSubsystem::Mount: return "Mount";
        case DiagnosticSubsystem::Daemon: return "Daemon";
        case DiagnosticSubsystem::Network: return "Network";
        case DiagnosticSubsystem::Service: return "Service";
    }
    return "Unknown";
}

bool StartupDiagnosticsReport::all_ok() const {
    for (const auto& item : items) {
        if (item.is_failure() || item.is_warning()) {
            return false;
        }
    }
    return true;
}

bool StartupDiagnosticsReport::has_failures() const {
    for (const auto& item : items) {
        if (item.is_failure()) {
            return true;
        }
    }
    return false;
}

bool StartupDiagnosticsReport::has_warnings() const {
    for (const auto& item : items) {
        if (item.is_warning()) {
            return true;
        }
    }
    return false;
}

size_t StartupDiagnosticsReport::failure_count() const {
    size_t count = 0;
    for (const auto& item : items) {
        if (item.is_failure()) count++;
    }
    return count;
}

size_t StartupDiagnosticsReport::warning_count() const {
    size_t count = 0;
    for (const auto& item : items) {
        if (item.is_warning()) count++;
    }
    return count;
}

std::vector<DiagnosticItem> StartupDiagnosticsReport::failed_or_warning_items() const {
    std::vector<DiagnosticItem> result;
    for (const auto& item : items) {
        if (item.is_failure() || item.is_warning()) {
            result.push_back(item);
        }
    }
    return result;
}

std::string StartupDiagnosticsReport::format_summary() const {
    std::ostringstream ss;
    ss << "Diagnostics Summary: " << (has_failures() ? "FAILURES DETECTED" : (has_warnings() ? "WARNINGS DETECTED" : "ALL SYSTEMS OK"))
       << " (" << failure_count() << " errors, " << warning_count() << " warnings, " << items.size() << " checks)";
    return ss.str();
}

std::string StartupDiagnosticsReport::format_detailed_report() const {
    std::ostringstream ss;
    ss << "=== Startup Diagnostics Report ===\n";
    for (const auto& item : items) {
        ss << "[" << item.subsystem_string() << "] " << item.name << " : " << item.severity_string() << "\n";
        ss << "  Summary : " << item.summary << "\n";
        if (!item.details.empty()) {
            ss << "  Details : " << item.details << "\n";
        }
        if (!item.recovery_guidance.empty() && (item.is_failure() || item.is_warning())) {
            ss << "  Recovery:\n";
            std::istringstream lines(item.recovery_guidance);
            std::string line;
            while (std::getline(lines, line)) {
                ss << "    " << line << "\n";
            }
        }
    }
    ss << "==================================";
    return ss.str();
}

std::string StartupDiagnosticsReport::format_on_screen_guidance() const {
    std::ostringstream ss;
    auto issues = failed_or_warning_items();
    if (issues.empty()) {
        ss << "All core subsystems initialized successfully.";
        return ss.str();
    }

    for (size_t i = 0; i < issues.size(); ++i) {
        const auto& item = issues[i];
        ss << (i + 1) << ". [" << item.subsystem_string() << "] " << item.summary << "\n";
        if (!item.recovery_guidance.empty()) {
            std::istringstream lines(item.recovery_guidance);
            std::string line;
            while (std::getline(lines, line)) {
                ss << "   • " << line << "\n";
            }
        }
        if (i + 1 < issues.size()) {
            ss << "\n";
        }
    }
    return ss.str();
}

StartupDiagnosticsEngine::StartupDiagnosticsEngine(StartupDiagnosticsConfig config)
    : config_(std::move(config)) {}

StartupDiagnosticsReport StartupDiagnosticsEngine::run_all_checks() {
    StartupDiagnosticsReport report;
    report.items.push_back(check_mount());
    report.items.push_back(check_daemon());
    if (config_.check_network) {
        report.items.push_back(check_network());
    }
    report.items.push_back(check_services());
    return report;
}

DiagnosticItem StartupDiagnosticsEngine::check_mount() {
    DiagnosticItem item;
    item.subsystem = DiagnosticSubsystem::Mount;
    item.name = "Root Scan Mount Probing";

    std::error_code ec;
    if (!std::filesystem::exists(config_.mount_path, ec)) {
        item.severity = DiagnosticSeverity::Error;
        item.summary = "Scan root path does not exist";
        item.details = "Path '" + config_.mount_path + "' was not found on the filesystem.";
        item.recovery_guidance =
            "1. Connect your external USB drive or verify host filesystem mount.\n"
            "2. In your PS5 jailbreak environment, verify that /hostapp payload is loaded.\n"
            "3. Launch with --scan-dir <directory> to select a different accessible path.";
        item.error_code = ENOENT;
        return item;
    }

    if (!std::filesystem::is_directory(config_.mount_path, ec)) {
        item.severity = DiagnosticSeverity::Error;
        item.summary = "Scan root is not a directory";
        item.details = "Path '" + config_.mount_path + "' exists but is not a directory.";
        item.recovery_guidance = "Select a valid folder containing game packages or homebrew applications.";
        item.error_code = ENOTDIR;
        return item;
    }

    // Try iterating
    bool is_empty = true;
    for (auto it = std::filesystem::directory_iterator(config_.mount_path, ec);
         !ec && it != std::filesystem::directory_iterator(); ++it) {
        is_empty = false;
        break;
    }

    if (ec) {
        item.severity = DiagnosticSeverity::Error;
        item.summary = "Scan root cannot be read (Permission Denied)";
        item.details = "Filesystem error while reading directory: " + ec.message();
        item.recovery_guidance = "Check permissions for " + config_.mount_path + " or run with root payload permissions.";
        item.error_code = ec.value();
        return item;
    }

    if (is_empty) {
        item.severity = DiagnosticSeverity::Warning;
        item.summary = "Scan root directory is empty";
        item.details = "Directory '" + config_.mount_path + "' contains no files or subdirectories.";
        item.recovery_guidance =
            "1. Copy PS5/PS4 packages (.pkg, .exfat, .ffpkg) or folders with sce_sys/param.json.\n"
            "2. Mount an active NFS share or external storage drive to this location.";
        return item;
    }

    item.severity = DiagnosticSeverity::Ok;
    item.summary = "Scan root is mounted and readable";
    item.details = "Directory '" + config_.mount_path + "' verified.";
    return item;
}

DiagnosticItem StartupDiagnosticsEngine::check_daemon() {
    DiagnosticItem item;
    item.subsystem = DiagnosticSubsystem::Daemon;
    item.name = "ShadowMountPlus Daemon Probe";

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        item.severity = DiagnosticSeverity::Error;
        item.summary = "Socket creation failed for daemon probe";
        item.details = std::string("Error creating socket: ") + std::strerror(errno);
        item.recovery_guidance = "Check network stack initialization and system socket privileges.";
        item.error_code = errno;
        return item;
    }

    // Set non-blocking
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.daemon_port);
    if (inet_pton(AF_INET, config_.daemon_host.c_str(), &addr.sin_addr) <= 0) {
        close(sock);
        item.severity = DiagnosticSeverity::Error;
        item.summary = "Invalid daemon IP address";
        item.details = "Failed to parse daemon IP address: " + config_.daemon_host;
        item.recovery_guidance = "Specify a valid IPv4 address (e.g. 127.0.0.1) for the daemon host.";
        item.error_code = EINVAL;
        return item;
    }

    int res = connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (res < 0 && errno == EINPROGRESS) {
        struct pollfd pfd{};
        pfd.fd = sock;
        pfd.events = POLLOUT;
        int poll_res = poll(&pfd, 1, config_.probe_timeout_ms);
        if (poll_res > 0) {
            int so_error = 0;
            socklen_t len = sizeof(so_error);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
            if (so_error == 0) {
                res = 0;
            } else {
                errno = so_error;
                res = -1;
            }
        } else {
            res = -1;
            errno = ETIMEDOUT;
        }
    }

    close(sock);

    if (res == 0) {
        item.severity = DiagnosticSeverity::Ok;
        item.summary = "ShadowMountPlus daemon online";
        item.details = "Daemon reachable at " + config_.daemon_host + ":" + std::to_string(config_.daemon_port);
    } else {
        item.severity = DiagnosticSeverity::Warning;
        item.summary = "ShadowMountPlus daemon offline or unreachable";
        item.details = "Failed to connect to " + config_.daemon_host + ":" + std::to_string(config_.daemon_port) + " (" + std::strerror(errno) + ")";
        item.recovery_guidance =
            "1. Ensure the ShadowMountPlus homebrew application is installed on your PS5.\n"
            "2. Launch ShadowMountPlus in the background before starting PS5 Library Browser.\n"
            "3. Remote NFS browsing and virtual mounting will remain locked until the daemon is active.";
        item.error_code = errno;
    }

    return item;
}

DiagnosticItem StartupDiagnosticsEngine::check_network() {
    DiagnosticItem item;
    item.subsystem = DiagnosticSubsystem::Network;
    item.name = "Network Subsystem Initializer";

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        item.severity = DiagnosticSeverity::Error;
        item.summary = "Network socket initialization failure";
        item.details = "Failed to allocate UDP socket (" + std::string(std::strerror(errno)) + ")";
        item.recovery_guidance =
            "1. Verify PS5 Network Settings and ensure LAN cable or Wi-Fi is connected.\n"
            "2. Check that jailbreak payload did not disable socket networking APIs.";
        item.error_code = errno;
        return item;
    }
    close(sock);

    item.severity = DiagnosticSeverity::Ok;
    item.summary = "Network stack initialized and ready";
    item.details = "Socket interface operational.";
    return item;
}

DiagnosticItem StartupDiagnosticsEngine::check_services() {
    DiagnosticItem item;
    item.subsystem = DiagnosticSubsystem::Service;
    item.name = "Runtime Storage & Persistence Services";

    auto check_writable = [](const std::string& path) -> bool {
        std::error_code ec;
        std::filesystem::path p(path);
        auto parent = p.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
            if (ec) return false;
        }
        // Try creating or opening file for append
        std::ofstream out(path, std::ios::app);
        if (!out.is_open()) {
            return false;
        }
        out.close();
        return true;
    };

    bool cache_ok = check_writable(config_.cache_path);
    bool jobs_ok = check_writable(config_.jobs_path);

    if (!cache_ok || !jobs_ok) {
        item.severity = DiagnosticSeverity::Error;
        item.summary = "Service persistence paths not writable";
        std::ostringstream ss;
        if (!cache_ok) ss << "Cache path [" << config_.cache_path << "] write failed. ";
        if (!jobs_ok) ss << "Job state path [" << config_.jobs_path << "] write failed.";
        item.details = ss.str();
        item.recovery_guidance =
            "1. Ensure the /data homebrew directory is mounted with read-write permissions.\n"
            "2. Check free storage space on internal SSD /data partition.\n"
            "3. If simulating on host, pass writable paths via --cache <path> and --jobs <path>.";
        item.error_code = EACCES;
        return item;
    }

    item.severity = DiagnosticSeverity::Ok;
    item.summary = "Cache and job persistence services operational";
    item.details = "Verified write access to cache and job state files.";
    return item;
}

} // namespace ps5_library
