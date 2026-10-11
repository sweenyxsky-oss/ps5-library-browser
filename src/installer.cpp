#include "ps5_library/installer.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <random>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <cstdint>
#if defined(__linux__)
#include <sys/statvfs.h>
#endif
#include <system_error>
#include <nlohmann/json.hpp>

#if defined(__FreeBSD__) || defined(__ORBIS__) || defined(__PROSPERO__)
#include <dlfcn.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <unistd.h>
#define PS5_INSTALL_API_AVAILABLE 1
#else
#define PS5_INSTALL_API_AVAILABLE 0
#endif

namespace ps5_library {
using nlohmann::json;

const char* to_string(InstallStatus status) {
    switch (status) {
        case InstallStatus::Queued: return "Queued";
        case InstallStatus::InProgress: return "Installing";
        case InstallStatus::Completed: return "Completed";
        case InstallStatus::Failed: return "Failed";
        case InstallStatus::Cancelled: return "Cancelled";
        case InstallStatus::Interrupted: return "Interrupted";
        default: return "Unknown";
    }
}

InstallStatus install_status_from_string(const std::string& value) {
    if (value == "Queued") return InstallStatus::Queued;
    if (value == "Installing" || value == "InProgress") return InstallStatus::InProgress;
    if (value == "Completed") return InstallStatus::Completed;
    if (value == "Failed") return InstallStatus::Failed;
    if (value == "Cancelled") return InstallStatus::Cancelled;
    if (value == "Interrupted") return InstallStatus::Interrupted;
    return InstallStatus::Queued;
}

const char* install_error_name(InstallErrorCode code) {
    switch (code) {
        case InstallErrorCode::None: return "None";
        case InstallErrorCode::FileNotFound: return "FileNotFound";
        case InstallErrorCode::InvalidPackage: return "InvalidPackage";
        case InstallErrorCode::InsufficientSpace: return "InsufficientSpace";
        case InstallErrorCode::Cancelled: return "Cancelled";
        case InstallErrorCode::DaemonRequired: return "DaemonRequired";
        case InstallErrorCode::IoError: return "IoError";
        case InstallErrorCode::UnsupportedPlatform: return "UnsupportedPlatform";
        case InstallErrorCode::MissingPrivilege: return "MissingPrivilege";
        case InstallErrorCode::SystemModuleMissing: return "SystemModuleMissing";
        case InstallErrorCode::CorruptedHeader: return "CorruptedHeader";
        case InstallErrorCode::TargetAppRunning: return "TargetAppRunning";
        case InstallErrorCode::InstallInterrupted: return "InstallInterrupted";
        case InstallErrorCode::FileUnreadable: return "FileUnreadable";
        case InstallErrorCode::MetadataMissing: return "MetadataMissing";
        case InstallErrorCode::DuplicateInstallBlocked: return "DuplicateInstallBlocked";
        default: return "Unknown";
    }
}

const char* to_string(InstallErrorCode code) {
    switch (code) {
        case InstallErrorCode::None: return "None";
        case InstallErrorCode::FileNotFound: return "Package file not found";
        case InstallErrorCode::InvalidPackage: return "Corrupt or invalid package structure";
        case InstallErrorCode::InsufficientSpace: return "Insufficient storage space";
        case InstallErrorCode::Cancelled: return "Installation cancelled";
        case InstallErrorCode::DaemonRequired: return "ShadowMountPlus daemon unavailable";
        case InstallErrorCode::IoError: return "I/O read/write error";
        case InstallErrorCode::UnsupportedPlatform: return "Unsupported platform";
        case InstallErrorCode::MissingPrivilege: return "Required PS5 system privilege is unavailable";
        case InstallErrorCode::SystemModuleMissing: return "PS5 package installation module unavailable";
        case InstallErrorCode::CorruptedHeader: return "Package metadata is invalid";
        case InstallErrorCode::TargetAppRunning: return "Target title must be closed";
        case InstallErrorCode::InstallInterrupted: return "Installation interrupted by application restart";
        case InstallErrorCode::FileUnreadable: return "Package file cannot be read";
        case InstallErrorCode::MetadataMissing: return "Package metadata missing";
        case InstallErrorCode::DuplicateInstallBlocked: return "Duplicate installation blocked";
        default: return "Unknown error";
    }
}

InstallErrorCode install_error_code_from_string(const std::string& value) {
    static const std::pair<const char*, InstallErrorCode> values[] = {
        {"None", InstallErrorCode::None}, {"FileNotFound", InstallErrorCode::FileNotFound},
        {"InvalidPackage", InstallErrorCode::InvalidPackage}, {"InsufficientSpace", InstallErrorCode::InsufficientSpace},
        {"Cancelled", InstallErrorCode::Cancelled}, {"DaemonRequired", InstallErrorCode::DaemonRequired},
        {"IoError", InstallErrorCode::IoError}, {"UnsupportedPlatform", InstallErrorCode::UnsupportedPlatform},
        {"MissingPrivilege", InstallErrorCode::MissingPrivilege}, {"SystemModuleMissing", InstallErrorCode::SystemModuleMissing},
        {"CorruptedHeader", InstallErrorCode::CorruptedHeader}, {"TargetAppRunning", InstallErrorCode::TargetAppRunning},
        {"InstallInterrupted", InstallErrorCode::InstallInterrupted}, {"FileUnreadable", InstallErrorCode::FileUnreadable},
        {"MetadataMissing", InstallErrorCode::MetadataMissing}, {"DuplicateInstallBlocked", InstallErrorCode::DuplicateInstallBlocked},
        {"Unknown", InstallErrorCode::Unknown}
    };
    for (const auto& value_pair : values) if (value == value_pair.first) return value_pair.second;
    return InstallErrorCode::Unknown;
}

std::string get_recovery_guidance(InstallErrorCode code, const std::string& extra_details) {
    std::string guidance;
    switch (code) {
        case InstallErrorCode::None: guidance = "No recovery action is needed."; break;
        case InstallErrorCode::FileNotFound: guidance = "Reconnect the storage or NFS mount, verify the package path and permissions, then rescan the library and retry."; break;
        case InstallErrorCode::InvalidPackage: case InstallErrorCode::CorruptedHeader:
            guidance = "Verify the package is complete and intended for this console. Re-copy or re-download it, then run the package integrity check before retrying."; break;
        case InstallErrorCode::InsufficientSpace:
            guidance = "Free storage on the PS5, check available space for temporary installation data, then retry."; break;
        case InstallErrorCode::Cancelled: guidance = "The package was not completed. Retry the install when ready; check the console for partial installation data first."; break;
        case InstallErrorCode::DaemonRequired:
            guidance = "Start ShadowMountPlus and confirm its local service is responding, then retry."; break;
        case InstallErrorCode::IoError:
            guidance = "Check the source drive/NFS connection, cable or network stability, and file permissions. Rescan, then retry from reliable storage."; break;
        case InstallErrorCode::UnsupportedPlatform:
            guidance = "This installer requires a supported jailbroken PS5 runtime and PS5 package-install service. Run the native PS5 build on compatible firmware."; break;
        case InstallErrorCode::MissingPrivilege:
            guidance = "The current process lacks the package-install privilege. Use the supported PS5 payload/launcher environment and verify its privilege setup; this app will not attempt privilege escalation."; break;
        case InstallErrorCode::SystemModuleMissing:
            guidance = "Restart the console and verify the system package-install service/module is available for this firmware. If it remains unavailable, collect the runtime diagnostic and report the firmware version."; break;
        case InstallErrorCode::TargetAppRunning:
            guidance = "Close the game or application using this title, confirm it is no longer running, then retry."; break;
        case InstallErrorCode::InstallInterrupted:
            guidance = "The app restarted during installation. Check the PS5 notification/installation queue for the package result before retrying to avoid a duplicate install."; break;
        case InstallErrorCode::FileUnreadable:
            guidance = "Check that the package exists, the media is mounted, and the current process can read it. For NFS, verify export permissions and credentials, remount the share, then retry."; break;
        case InstallErrorCode::MetadataMissing:
            guidance = "The package does not expose the required PS5 metadata to this host-side preflight. Verify it is a supported PS5 package and not a renamed archive or incomplete split package; use a target-side validator before installation."; break;
        case InstallErrorCode::DuplicateInstallBlocked:
            guidance = "An active or queued job already targets this package. Let that job finish or cancel it before attempting another install."; break;
        default: guidance = "Review the console installation logs and package path, then retry. If the error persists, include the error code and runtime diagnostic in a bug report."; break;
    }
    if (!extra_details.empty()) guidance += "\nRuntime details: " + extra_details;
    return guidance;
}

const char* to_string(HistorySortField field) {
    switch (field) {
        case HistorySortField::Timestamp: return "Timestamp";
        case HistorySortField::PackageName: return "PackageName";
        default: return "Unknown";
    }
}

const char* to_string(HistorySortOrder order) {
    switch (order) {
        case HistorySortOrder::Descending: return "DESC";
        case HistorySortOrder::Ascending: return "ASC";
        default: return "Unknown";
    }
}

HistorySortField history_sort_field_from_string(const std::string& str) {
    if (str == "PackageName" || str == "name") return HistorySortField::PackageName;
    return HistorySortField::Timestamp;
}

HistorySortOrder history_sort_order_from_string(const std::string& str) {
    if (str == "ASC" || str == "asc" || str == "Ascending") return HistorySortOrder::Ascending;
    return HistorySortOrder::Descending;
}

std::string PreflightCheckSummary::formatted_summary() const {
    std::ostringstream ss;
    ss << "--- Preflight Check Summary ---\n";
    for (const auto& item : checks()) {
        ss << (item.passed ? "[PASS] " : "[FAIL] ") << item.name << ": ";
        if (item.passed) {
            ss << item.details << "\n";
        } else {
            ss << item.failure_reason << "\n";
            if (!item.remediation.empty()) {
                ss << "       Remediation: " << item.remediation << "\n";
            }
        }
    }
    ss << "Result: " << (all_passed ? "Ready for installation" : "Checks failed - resolution required");
    return ss.str();
}

std::vector<PreflightCheckDiff> diff_preflight_summaries(const PreflightCheckSummary& previous,
                                                         const PreflightCheckSummary& current) {
    std::vector<PreflightCheckDiff> diffs;
    auto compare_item = [&](const std::string& name, const PreflightCheckItem& prev, const PreflightCheckItem& curr) {
        bool status_changed = (prev.passed != curr.passed);
        std::string prev_val = prev.passed ? prev.details : prev.failure_reason;
        std::string curr_val = curr.passed ? curr.details : curr.failure_reason;
        bool value_changed = (prev_val != curr_val);

        if (status_changed || value_changed) {
            PreflightCheckDiff diff;
            diff.check_name = name;
            diff.status_changed = status_changed;
            diff.previously_passed = prev.passed;
            diff.currently_passed = curr.passed;
            diff.previous_details = prev_val;
            diff.current_details = curr_val;

            if (status_changed) {
                diff.description = name + ": Status changed from " + (prev.passed ? "PASSED" : "FAILED") +
                                   " to " + (curr.passed ? "PASSED" : "FAILED") +
                                   " (" + curr_val + ")";
            } else {
                diff.description = name + ": Value changed from [" + prev_val + "] to [" + curr_val + "]";
            }
            diffs.push_back(diff);
        }
    };

    compare_item("File Readability", previous.readability, current.readability);
    compare_item("Package Metadata", previous.metadata, current.metadata);
    compare_item("Available Storage", previous.storage, current.storage);

    return diffs;
}

std::string format_preflight_diffs(const std::vector<PreflightCheckDiff>& diffs) {
    if (diffs.empty()) {
        return "Preflight status unchanged since previous summary.";
    }
    std::ostringstream ss;
    ss << "--- Preflight Status Changes Detected ---\n";
    for (const auto& diff : diffs) {
        ss << "* " << diff.description << "\n";
    }
    return ss.str();
}

std::string serialize_history_filter_options(const HistoryFilterOptions& options) {
    json j;
    j["query"] = options.query;
    if (options.outcome_filter.has_value()) {
        j["outcome_filter"] = to_string(*options.outcome_filter);
    } else {
        j["outcome_filter"] = nullptr;
    }
    j["format_filter"] = options.format_filter;
    j["date_from"] = options.date_from;
    j["date_to"] = options.date_to;
    j["sort_field"] = static_cast<int>(options.sort_field);
    j["sort_order"] = static_cast<int>(options.sort_order);
    return j.dump();
}

HistoryFilterOptions deserialize_history_filter_options(const std::string& serialized) {
    HistoryFilterOptions options;
    if (serialized.empty()) return options;
    try {
        json j = json::parse(serialized);
        options.query = j.value("query", "");
        if (j.contains("outcome_filter") && !j["outcome_filter"].is_null()) {
            std::string st = j["outcome_filter"].get<std::string>();
            options.outcome_filter = install_status_from_string(st);
        } else {
            options.outcome_filter = std::nullopt;
        }
        options.format_filter = j.value("format_filter", "");
        options.date_from = j.value("date_from", "");
        options.date_to = j.value("date_to", "");
        if (j.contains("sort_field")) {
            options.sort_field = static_cast<HistorySortField>(j["sort_field"].get<int>());
        }
        if (j.contains("sort_order")) {
            options.sort_order = static_cast<HistorySortOrder>(j["sort_order"].get<int>());
        }
    } catch (...) {}
    return options;
}

namespace {
std::string utc_timestamp_now() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool has_pkg_magic(const unsigned char* bytes, size_t count) {
    if (count < 4) return false;
    bool is_sony = (bytes[0] == 0x7F && bytes[1] == 'P' && bytes[2] == 'K' && bytes[3] == 'G');
    bool is_hb = (bytes[0] == 'P' && bytes[1] == 'K' && bytes[2] == 'G' && bytes[3] == '\0');
    return is_sony || is_hb;
}

std::string escape_csv_field(const std::string& value) {
    bool need_quotes = false;
    for (char c : value) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            need_quotes = true;
            break;
        }
    }
    if (!need_quotes) return value;
    std::string out = "\"";
    for (char c : value) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += "\"";
    return out;
}

bool passes_date_from(const std::string& ts, const std::string& date_from) {
    if (date_from.empty()) return true;
    if (ts.empty()) return false;
    if (date_from.size() == 10) {
        return ts.substr(0, 10) >= date_from;
    }
    return ts >= date_from;
}

bool passes_date_to(const std::string& ts, const std::string& date_to) {
    if (date_to.empty()) return true;
    if (ts.empty()) return false;
    if (date_to.size() == 10) {
        return ts.substr(0, 10) <= date_to;
    }
    return ts <= date_to;
}

bool matches_format(const InstallTask& task, const std::string& format_filter) {
    if (format_filter.empty()) return true;
    std::string needle = lower_copy(format_filter);
    if (!needle.empty() && needle[0] == '.') needle = needle.substr(1);
    std::string task_fmt = lower_copy(task.format);
    if (task_fmt.find('.') != std::string::npos) task_fmt = task_fmt.substr(task_fmt.find_last_of('.') + 1);
    if (task_fmt == needle) return true;
    auto dot = task.package_filename.find_last_of('.');
    if (dot != std::string::npos) {
        std::string ext = lower_copy(task.package_filename.substr(dot + 1));
        if (ext == needle) return true;
    }
    return false;
}
} // namespace

PreflightResult run_package_preflight(const std::string& package_path,
                                      const std::string& target_storage_dir,
                                      uint64_t safety_margin_bytes) {
    PreflightResult result;
    result.summary.readability.name = "File Readability";
    result.summary.metadata.name = "Package Metadata";
    result.summary.storage.name = "Available Storage";

    const std::filesystem::path path(package_path);
    std::error_code ec;

    // --- Check 1: File Readability ---
    if (!std::filesystem::exists(path, ec) || ec) {
        result.summary.readability.passed = false;
        result.summary.readability.failure_reason = "Package does not exist: " + package_path;
        result.summary.readability.remediation = get_recovery_guidance(InstallErrorCode::FileNotFound, result.summary.readability.failure_reason);

        result.summary.metadata.passed = false;
        result.summary.metadata.failure_reason = "Cannot inspect metadata: package file not found";
        result.summary.metadata.remediation = result.summary.readability.remediation;

        result.summary.storage.passed = false;
        result.summary.storage.failure_reason = "Storage requirement cannot be calculated for missing file";
        result.summary.storage.remediation = result.summary.readability.remediation;

        result.summary.all_passed = false;
        result.passed = false;
        result.error_code = InstallErrorCode::FileNotFound;
        result.error_message = result.summary.readability.failure_reason;
        result.recovery_guidance = result.summary.readability.remediation;
        return result;
    }

    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        result.summary.readability.passed = false;
        result.summary.readability.failure_reason = "Package path is not a regular file";
        result.summary.readability.remediation = get_recovery_guidance(InstallErrorCode::InvalidPackage, result.summary.readability.failure_reason);

        result.summary.metadata.passed = false;
        result.summary.metadata.failure_reason = "Cannot inspect metadata: invalid file object";
        result.summary.metadata.remediation = result.summary.readability.remediation;

        result.summary.storage.passed = false;
        result.summary.storage.failure_reason = "Storage requirement cannot be calculated";
        result.summary.storage.remediation = result.summary.readability.remediation;

        result.summary.all_passed = false;
        result.passed = false;
        result.error_code = InstallErrorCode::InvalidPackage;
        result.error_message = result.summary.readability.failure_reason;
        result.recovery_guidance = result.summary.readability.remediation;
        return result;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        result.summary.readability.passed = false;
        result.summary.readability.failure_reason = "Cannot open package for reading (permission denied or I/O failure)";
        result.summary.readability.remediation = get_recovery_guidance(InstallErrorCode::FileUnreadable, package_path);

        result.summary.metadata.passed = false;
        result.summary.metadata.failure_reason = "Cannot inspect metadata: package is unreadable";
        result.summary.metadata.remediation = result.summary.readability.remediation;

        result.summary.storage.passed = false;
        result.summary.storage.failure_reason = "Storage calculation blocked by unreadable file";
        result.summary.storage.remediation = result.summary.readability.remediation;

        result.summary.all_passed = false;
        result.passed = false;
        result.error_code = InstallErrorCode::FileUnreadable;
        result.error_message = result.summary.readability.failure_reason;
        result.recovery_guidance = result.summary.readability.remediation;
        return result;
    }

    std::error_code size_ec;
    result.file_size_bytes = std::filesystem::file_size(path, size_ec);
    if (size_ec || result.file_size_bytes == 0) {
        result.summary.readability.passed = false;
        result.summary.readability.failure_reason = "Package file is empty (0 bytes) or size could not be determined";
        result.summary.readability.remediation = get_recovery_guidance(InstallErrorCode::InvalidPackage);

        result.summary.metadata.passed = false;
        result.summary.metadata.failure_reason = "Metadata header missing due to zero file size";
        result.summary.metadata.remediation = result.summary.readability.remediation;

        result.summary.storage.passed = false;
        result.summary.storage.failure_reason = "Storage check skipped for empty file";
        result.summary.storage.remediation = result.summary.readability.remediation;

        result.summary.all_passed = false;
        result.passed = false;
        result.error_code = InstallErrorCode::InvalidPackage;
        result.error_message = result.summary.readability.failure_reason;
        result.recovery_guidance = result.summary.readability.remediation;
        return result;
    }

    result.summary.readability.passed = true;
    result.summary.readability.details = "File exists and is readable (" + format_file_size(result.file_size_bytes) + ")";

    // --- Check 2: Package Metadata ---
    std::array<unsigned char, 64> header{};
    in.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    const size_t got = static_cast<size_t>(in.gcount());
    in.close();

    std::string ext = lower_copy(path.extension().string());
    result.detected_format = (ext.rfind(".", 0) == 0) ? ext.substr(1) : ext;

    if (ext == ".pkg") {
        if (got < 4 || !has_pkg_magic(header.data(), got)) {
            result.summary.metadata.passed = false;
            result.summary.metadata.failure_reason = "PKG header magic is missing or unreadable";
            result.summary.metadata.remediation = get_recovery_guidance(InstallErrorCode::CorruptedHeader);
        } else {
            result.summary.metadata.passed = true;
            result.summary.metadata.details = "PKG header signature is valid (format: pkg)";
        }
    } else if (ext == ".ffpkg" || ext == ".exfat") {
        if (got < 16) {
            result.summary.metadata.passed = false;
            result.summary.metadata.failure_reason = "Package container is too short to expose required metadata (< 16 bytes)";
            result.summary.metadata.remediation = get_recovery_guidance(InstallErrorCode::MetadataMissing);
        } else {
            result.summary.metadata.passed = true;
            result.summary.metadata.details = "Container structure is readable (format: " + result.detected_format + ")";
        }
    } else {
        result.summary.metadata.passed = false;
        result.summary.metadata.failure_reason = "Unsupported package extension: " + ext;
        result.summary.metadata.remediation = get_recovery_guidance(InstallErrorCode::MetadataMissing);
    }

    // --- Check 3: Available Storage ---
    result.required_storage_bytes = result.file_size_bytes + safety_margin_bytes;
#if defined(__linux__)
    struct statvfs fs{};
    std::filesystem::path target(target_storage_dir);
    while (!target.empty() && !std::filesystem::exists(target, ec)) {
        ec.clear();
        const auto parent = target.parent_path();
        if (parent == target) break;
        target = parent;
    }
    if (!target.empty() && ::statvfs(target.c_str(), &fs) == 0) {
        result.available_storage_bytes = static_cast<uint64_t>(fs.f_bavail) * static_cast<uint64_t>(fs.f_frsize);
        if (result.available_storage_bytes < result.required_storage_bytes) {
            result.summary.storage.passed = false;
            result.summary.storage.failure_reason = "Available storage (" + format_file_size(result.available_storage_bytes) +
                                                    ") is below package size plus margin (" + format_file_size(result.required_storage_bytes) + ")";
            result.summary.storage.remediation = get_recovery_guidance(InstallErrorCode::InsufficientSpace);
        } else {
            result.summary.storage.passed = true;
            result.summary.storage.details = "Available: " + format_file_size(result.available_storage_bytes) +
                                             " >= Required: " + format_file_size(result.required_storage_bytes) +
                                             " (Margin: " + format_file_size(safety_margin_bytes) + ")";
        }
    } else {
        result.summary.storage.passed = false;
        result.summary.storage.failure_reason = "Could not query target installation storage capacity";
        result.summary.storage.remediation = get_recovery_guidance(InstallErrorCode::UnsupportedPlatform);
    }
#else
    (void)target_storage_dir;
    result.summary.storage.passed = false;
    result.summary.storage.failure_reason = "Storage capacity check is unavailable on this platform";
    result.summary.storage.remediation = get_recovery_guidance(InstallErrorCode::UnsupportedPlatform);
#endif

    // --- Combine Results ---
    result.summary.all_passed = result.summary.readability.passed &&
                               result.summary.metadata.passed &&
                               result.summary.storage.passed;
    result.passed = result.summary.all_passed;

    if (!result.summary.readability.passed) {
        result.error_code = InstallErrorCode::InvalidPackage;
        result.error_message = result.summary.readability.failure_reason;
        result.recovery_guidance = result.summary.readability.remediation;
    } else if (!result.summary.metadata.passed) {
        result.error_code = (ext == ".pkg") ? InstallErrorCode::CorruptedHeader : InstallErrorCode::MetadataMissing;
        result.error_message = result.summary.metadata.failure_reason;
        result.recovery_guidance = result.summary.metadata.remediation;
    } else if (!result.summary.storage.passed) {
#if defined(__linux__)
        result.error_code = (result.available_storage_bytes < result.required_storage_bytes && result.available_storage_bytes > 0)
                            ? InstallErrorCode::InsufficientSpace : InstallErrorCode::UnsupportedPlatform;
#else
        result.error_code = InstallErrorCode::UnsupportedPlatform;
#endif
        result.error_message = result.summary.storage.failure_reason;
        result.recovery_guidance = result.summary.storage.remediation;
    } else {
        result.error_code = InstallErrorCode::None;
        result.error_message = "All preflight checks passed successfully";
        result.recovery_guidance = "No recovery action is needed.";
    }

    return result;
}

float InstallTask::progress_ratio() const {
    if (total_bytes == 0) return status == InstallStatus::Completed ? 1.0f : 0.0f;
    return std::min(1.0f, static_cast<float>(processed_bytes) / static_cast<float>(total_bytes));
}

int InstallTask::progress_percent() const {
    return static_cast<int>(progress_ratio() * 100.0f);
}

bool InstallTask::is_terminal() const {
    return status == InstallStatus::Completed || status == InstallStatus::Failed ||
           status == InstallStatus::Cancelled || status == InstallStatus::Interrupted;
}

float BatchProgress::overall_progress_ratio() const {
    if (total_items == 0) return 0.0f;
    if (total_bytes > 0) {
        return std::min(1.0f, static_cast<float>(processed_bytes) / static_cast<float>(total_bytes));
    }
    return std::min(1.0f, static_cast<float>(completed_items) / static_cast<float>(total_items));
}

int BatchProgress::overall_progress_percent() const {
    return static_cast<int>(overall_progress_ratio() * 100.0f);
}

float BatchProgress::current_item_progress_ratio() const {
    return 0.0f;
}

int BatchProgress::current_item_progress_percent() const {
    return 0;
}

LocalInstallDelegate::LocalInstallDelegate(std::string target_base_directory)
    : target_base_dir_(std::move(target_base_directory)) {}

bool LocalInstallDelegate::install_package(const InstallTask& task,
                                           std::function<bool(uint64_t written_bytes, uint64_t total_bytes)> on_chunk,
                                           InstallErrorCode& out_code,
                                           std::string& out_error) {
    out_code = InstallErrorCode::None;
    out_error.clear();
    uint64_t total = task.total_bytes > 0 ? task.total_bytes : 10ULL * 1024ULL * 1024ULL;
    uint64_t written = 0;
    uint64_t chunk_size = std::max<uint64_t>(1024ULL * 256ULL, total / 20);

    while (written < total) {
        written = std::min(total, written + chunk_size);
        if (on_chunk && !on_chunk(written, total)) {
            out_code = InstallErrorCode::Cancelled;
            out_error = "Installation cancelled by user";
            return false;
        }
    }
    return true;
}

Ps5InstallDelegate::Ps5InstallDelegate(bool auto_check_capabilities) {
    if (auto_check_capabilities) refresh_capabilities();
}
Ps5InstallDelegate::~Ps5InstallDelegate() {
#if PS5_INSTALL_API_AVAILABLE
    if (module_handle_) dlclose(module_handle_);
#endif
}

void Ps5InstallDelegate::refresh_capabilities() {
    if (mock_override_) return;
    caps_ = SystemCapabilities{};
#if PS5_INSTALL_API_AVAILABLE
    caps_.is_ps5_environment = true;
    caps_.has_kernel_privilege = false;
    caps_.diagnostic_reason = "PS5-compatible runtime detected; privilege readiness is not exposed through a safe portable check";
    caps_.appinst_module_loaded = false;
    caps_.init_succeeded = false;
#else
    caps_.diagnostic_reason = "This build was compiled for a non-PS5 host; the PS5 AppInstUtil API is unavailable";
#endif
}

bool Ps5InstallDelegate::resolve_symbols_and_init() {
#if PS5_INSTALL_API_AVAILABLE
    if (!caps_.is_ps5_environment) return false;
    const char* module_path = "/system/common/lib/libSceAppInstUtil.sprx";
    module_handle_ = dlopen(module_path, RTLD_NOW);
    if (!module_handle_) {
        caps_.last_system_error = errno;
        caps_.diagnostic_reason = std::string("Could not load ") + module_path + ": " + (dlerror() ? dlerror() : "unknown loader error");
        return false;
    }
    caps_.appinst_module_loaded = true;
    using InitializeFn = int (*)();
    auto initialize = reinterpret_cast<InitializeFn>(dlsym(module_handle_, "sceAppInstUtilInitialize"));
    if (!initialize) {
        caps_.diagnostic_reason = "The AppInstUtil initialization symbol is missing";
        return false;
    }
    int rc = initialize();
    caps_.last_system_error = rc;
    caps_.init_succeeded = (rc == 0);
    if (rc != 0) caps_.diagnostic_reason = "sceAppInstUtilInitialize returned error " + std::to_string(rc);
    return caps_.init_succeeded;
#else
    return false;
#endif
}

bool Ps5InstallDelegate::is_platform_supported() const {
    return caps_.is_ps5_environment && caps_.has_kernel_privilege && caps_.appinst_module_loaded && caps_.init_succeeded;
}
std::string Ps5InstallDelegate::platform_status_description() const {
    if (is_platform_supported()) return "PS5 package installer ready";
    if (!caps_.is_ps5_environment) return "Unsupported platform: PS5 package installation is only available in a compatible native PS5 runtime";
    if (!caps_.has_kernel_privilege) return "Unsupported environment: required PS5 package-install privileges are unavailable";
    if (!caps_.appinst_module_loaded) return "Unsupported environment: PS5 AppInstUtil system module is unavailable";
    return "PS5 package installer is not initialized: " + caps_.diagnostic_reason;
}
void Ps5InstallDelegate::set_mock_capabilities(bool is_ps5, bool has_priv, bool module_loaded, int err, const std::string& reason) {
    mock_override_ = true;
    caps_.is_ps5_environment = is_ps5; caps_.has_kernel_privilege = has_priv;
    caps_.appinst_module_loaded = module_loaded; caps_.init_succeeded = is_ps5 && has_priv && module_loaded && err == 0;
    caps_.last_system_error = err; caps_.diagnostic_reason = reason;
}
bool Ps5InstallDelegate::install_package(const InstallTask& task,
    std::function<bool(uint64_t, uint64_t)> on_chunk, InstallErrorCode& out_code, std::string& out_error) {
    (void)on_chunk;
    if (!caps_.is_ps5_environment && !mock_override_) refresh_capabilities();
    if (!caps_.is_ps5_environment) {
        out_code = InstallErrorCode::UnsupportedPlatform; out_error = platform_status_description(); return false;
    }
    if (!caps_.has_kernel_privilege) {
        out_code = InstallErrorCode::MissingPrivilege; out_error = platform_status_description(); return false;
    }
    if (!caps_.appinst_module_loaded || !caps_.init_succeeded) {
        if (!resolve_symbols_and_init()) {
            out_code = InstallErrorCode::SystemModuleMissing; out_error = platform_status_description(); return false;
        }
    }
#if PS5_INSTALL_API_AVAILABLE
    using InstallByPackageFn = int (*)(void*, void*, void*);
    auto install_by_package = reinterpret_cast<InstallByPackageFn>(dlsym(module_handle_, "sceAppInstUtilInstallByPackage"));
    if (!install_by_package) {
        out_code = InstallErrorCode::SystemModuleMissing;
        out_error = "sceAppInstUtilInstallByPackage symbol is unavailable; firmware API/SDK mismatch";
        return false;
    }
    (void)task;
    out_code = InstallErrorCode::UnsupportedPlatform;
    out_error = "PS5 AppInstUtil detected, but this build has no verified target-SDK metadata/ABI adapter; package installation is disabled rather than risking a malformed system call";
    return false;
#else
    (void)task;
    out_code = InstallErrorCode::UnsupportedPlatform; out_error = platform_status_description(); return false;
#endif
}

void MockInstallDelegate::configure_failure(const std::string& filename,
                                           InstallErrorCode code,
                                           const std::string& message,
                                           size_t fail_at_offset) {
    ItemRule rule;
    rule.should_fail = true;
    rule.failure_code = code;
    rule.failure_message = message;
    rule.fail_at_byte_offset = fail_at_offset;
    rules_[filename] = rule;
}

void MockInstallDelegate::reset() {
    rules_.clear();
    installed_count_ = 0;
}

bool MockInstallDelegate::install_package(const InstallTask& task,
                                         std::function<bool(uint64_t, uint64_t)> on_chunk,
                                         InstallErrorCode& out_code,
                                         std::string& out_error) {
    uint64_t total = task.total_bytes > 0 ? task.total_bytes : 10ULL * 1024ULL * 1024ULL;
    uint64_t step = std::max<uint64_t>(1024ULL * 256ULL, total / 10);
    uint64_t written = 0;

    auto it = rules_.find(task.package_filename);
    bool should_fail = it != rules_.end() && it->second.should_fail;

    while (written < total) {
        written = std::min(total, written + step);
        if (should_fail && written >= it->second.fail_at_byte_offset) {
            out_code = it->second.failure_code;
            out_error = it->second.failure_message;
            return false;
        }
        if (on_chunk && !on_chunk(written, total)) {
            out_code = InstallErrorCode::Cancelled;
            out_error = "Installation cancelled";
            return false;
        }
    }
    installed_count_++;
    out_code = InstallErrorCode::None;
    out_error.clear();
    return true;
}

namespace {
std::string make_unique_temp_path(const std::string& path) {
    std::ostringstream os;
    os << path << ".tmp." << std::chrono::steady_clock::now().time_since_epoch().count() << "." << std::random_device{}();
    return os.str();
}

InstallTask task_from_json(const json& j) {
    InstallTask task;
    task.task_id = j.value("task_id", "");
    task.title_id = j.value("title_id", "");
    task.package_filename = j.value("package_filename", "");
    task.display_name = j.value("display_name", "");
    task.source_path = j.value("source_path", "");
    task.target_path = j.value("target_path", "");
    task.format = j.value("format", "");
    task.kind = parse_item_kind(j.value("kind", "Unknown"));
    task.total_bytes = j.value("total_bytes", uint64_t{0});
    task.processed_bytes = j.value("processed_bytes", uint64_t{0});
    task.status = install_status_from_string(j.value("status", "Queued"));
    task.error_code = install_error_code_from_string(j.value("error_code", "None"));
    task.error_message = j.value("error_message", "");
    task.recovery_guidance = j.value("recovery_guidance", "");
    task.retry_count = j.value("retry_count", size_t{0});
    task.original_task_id = j.value("original_task_id", "");
    task.original_error_code = install_error_code_from_string(j.value("original_error_code", "None"));
    task.original_error_message = j.value("original_error_message", "");
    task.created_timestamp = j.value("created_timestamp", "");
    task.completed_timestamp = j.value("completed_timestamp", "");
    if (j.contains("preflight_changes") && j["preflight_changes"].is_array()) {
        for (const auto& d : j["preflight_changes"]) {
            PreflightCheckDiff diff;
            diff.check_name = d.value("check_name", "");
            diff.status_changed = d.value("status_changed", false);
            diff.previously_passed = d.value("previously_passed", false);
            diff.currently_passed = d.value("currently_passed", false);
            diff.previous_details = d.value("previous_details", "");
            diff.current_details = d.value("current_details", "");
            diff.description = d.value("description", "");
            task.preflight_changes.push_back(diff);
        }
    }
    return task;
}

json task_to_json(const InstallTask& task) {
    json res = json{
        {"task_id", task.task_id},
        {"title_id", task.title_id},
        {"package_filename", task.package_filename},
        {"display_name", task.display_name},
        {"source_path", task.source_path},
        {"target_path", task.target_path},
        {"format", task.format},
        {"kind", to_string(task.kind)},
        {"total_bytes", task.total_bytes},
        {"processed_bytes", task.processed_bytes},
        {"status", to_string(task.status)},
        {"error_code", install_error_name(task.error_code)},
        {"error_message", task.error_message},
        {"recovery_guidance", task.recovery_guidance},
        {"retry_count", task.retry_count},
        {"original_task_id", task.original_task_id},
        {"original_error_code", install_error_name(task.original_error_code)},
        {"original_error_message", task.original_error_message},
        {"created_timestamp", task.created_timestamp},
        {"completed_timestamp", task.completed_timestamp}
    };
    if (!task.preflight_changes.empty()) {
        json diffs_arr = json::array();
        for (const auto& d : task.preflight_changes) {
            diffs_arr.push_back({
                {"check_name", d.check_name},
                {"status_changed", d.status_changed},
                {"previously_passed", d.previously_passed},
                {"currently_passed", d.currently_passed},
                {"previous_details", d.previous_details},
                {"current_details", d.current_details},
                {"description", d.description}
            });
        }
        res["preflight_changes"] = diffs_arr;
    }
    return res;
}
} // namespace

std::string export_history_to_json(const std::vector<InstallTask>& tasks, int indent) {
    json root = json::array();
    for (const auto& task : tasks) {
        root.push_back(task_to_json(task));
    }
    return root.dump(indent);
}

std::string export_history_to_csv(const std::vector<InstallTask>& tasks) {
    std::ostringstream out;
    out << "task_id,title_id,package_filename,display_name,source_path,target_path,format,kind,"
        << "total_bytes,processed_bytes,status,error_code,error_message,recovery_guidance,retry_count,"
        << "original_task_id,original_error_code,original_error_message,created_timestamp,completed_timestamp\n";

    for (const auto& task : tasks) {
        out << escape_csv_field(task.task_id) << ","
            << escape_csv_field(task.title_id) << ","
            << escape_csv_field(task.package_filename) << ","
            << escape_csv_field(task.display_name) << ","
            << escape_csv_field(task.source_path) << ","
            << escape_csv_field(task.target_path) << ","
            << escape_csv_field(task.format) << ","
            << escape_csv_field(to_string(task.kind)) << ","
            << task.total_bytes << ","
            << task.processed_bytes << ","
            << escape_csv_field(to_string(task.status)) << ","
            << escape_csv_field(install_error_name(task.error_code)) << ","
            << escape_csv_field(task.error_message) << ","
            << escape_csv_field(task.recovery_guidance) << ","
            << task.retry_count << ","
            << escape_csv_field(task.original_task_id) << ","
            << escape_csv_field(install_error_name(task.original_error_code)) << ","
            << escape_csv_field(task.original_error_message) << ","
            << escape_csv_field(task.created_timestamp) << ","
            << escape_csv_field(task.completed_timestamp) << "\n";
    }
    return out.str();
}

bool export_history_to_file(const std::string& filepath, const std::vector<InstallTask>& tasks, bool as_json) {
    try {
        std::filesystem::path p(filepath);
        if (!p.parent_path().empty()) {
            std::filesystem::create_directories(p.parent_path());
        }
        std::ofstream out(filepath, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        std::string content = as_json ? export_history_to_json(tasks, 2) : export_history_to_csv(tasks);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        return out.good();
    } catch (...) {
        return false;
    }
}

InstallPipeline::InstallPipeline(std::shared_ptr<IInstallDelegate> delegate, std::string state_filepath)
    : delegate_(delegate ? std::move(delegate) : std::make_shared<LocalInstallDelegate>()),
      state_filepath_(std::move(state_filepath)) {
    if (!state_filepath_.empty()) load_and_restore_state();
}

InstallPipeline::~InstallPipeline() {
    stop_worker();
    save_state();
}

void InstallPipeline::set_delegate(std::shared_ptr<IInstallDelegate> delegate) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    delegate_ = delegate ? std::move(delegate) : std::make_shared<LocalInstallDelegate>();
}

std::string InstallPipeline::generate_task_id() {
    return "task_" + std::to_string(next_id_counter_++);
}

std::string InstallPipeline::queue_item(const GameItem& item, const std::string& title_id,
                                        const std::optional<PreflightCheckSummary>& summary_snapshot) {
    const std::string source = item.absolute_path.empty() ? item.filename : item.absolute_path;
    const auto duplicate = check_queue_preflight(source, false);
    if (!duplicate.can_queue) return {};

    std::lock_guard<std::mutex> lock(queue_mutex_);
    InstallTask task;
    task.task_id = generate_task_id();
    task.title_id = title_id;
    task.package_filename = item.filename;
    task.display_name = item.display_name.empty() ? item.filename : item.display_name;
    task.source_path = source;
    task.format = item.format;
    task.kind = item.kind;
    task.total_bytes = item.size_bytes;
    task.status = InstallStatus::Queued;
    task.created_timestamp = utc_timestamp_now();
    task.has_preflight_snapshot = summary_snapshot.has_value();
    if (summary_snapshot.has_value()) {
        task.preflight_summary_snapshot = *summary_snapshot;
    }
    task_queue_.push_back(task);
    save_state_locked();
    return task.task_id;
}

void InstallPipeline::set_saved_history_filter_options(const HistoryFilterOptions& options) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    saved_history_filter_options_ = options;
    save_state_locked();
}

QueueCheckResult InstallPipeline::check_queue_preflight(const std::string& source_path, bool is_retry_intent) const {
    QueueCheckResult result;
    result.is_retry = is_retry_intent;
    const auto tasks = all_tasks();
    for (auto it = tasks.rbegin(); it != tasks.rend(); ++it) {
        if (it->source_path != source_path && it->package_filename != source_path) continue;
        if (it->status == InstallStatus::Queued || it->status == InstallStatus::InProgress) {
            result.can_queue = false;
            result.is_duplicate = true;
            result.reason = "A queued or active install already targets this package";
            result.existing_task = *it;
            return result;
        }
        if (is_retry_intent) {
            if (it->status != InstallStatus::Failed && it->status != InstallStatus::Interrupted) {
                result.can_queue = false;
                result.reason = "Only failed or interrupted package jobs can be retried";
                result.existing_task = *it;
                return result;
            }
        } else {
            if (it->status == InstallStatus::Failed || it->status == InstallStatus::Interrupted) {
                result.can_queue = false;
                result.reason = "Package has previous failure/interruption. Use retry to re-attempt install.";
                result.existing_task = *it;
                return result;
            }
            if (it->status == InstallStatus::Completed) {
                result.can_queue = false;
                result.reason = "Package has already been installed successfully.";
                result.existing_task = *it;
                return result;
            }
        }
        break;
    }
    if (enable_preflight_) {
        const auto preflight = run_package_preflight(source_path, target_storage_dir_);
        if (!preflight.passed) {
            result.can_queue = false;
            result.reason = preflight.error_message + " | " + preflight.recovery_guidance;
        }
    }
    return result;
}

std::string InstallPipeline::retry_task(const std::string& task_id_or_path, std::string* out_error) {
    std::optional<InstallTask> original;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        for (auto it = task_history_.rbegin(); it != task_history_.rend(); ++it) {
            if (it->task_id == task_id_or_path || it->source_path == task_id_or_path || it->package_filename == task_id_or_path) {
                original = *it;
                break;
            }
        }
        if (!original && active_task_ && (active_task_->task_id == task_id_or_path || active_task_->source_path == task_id_or_path)) {
            original = *active_task_;
        }
    }
    if (!original || (original->status != InstallStatus::Failed && original->status != InstallStatus::Interrupted)) {
        if (out_error) *out_error = "Retry is available only for failed or interrupted jobs";
        return {};
    }
    const auto check = check_queue_preflight(original->source_path, true);
    if (!check.can_queue) {
        if (out_error) *out_error = check.reason;
        return {};
    }

    std::lock_guard<std::mutex> lock(queue_mutex_);
    InstallTask retry = *original;
    retry.task_id = generate_task_id();
    retry.status = InstallStatus::Queued;
    retry.error_code = InstallErrorCode::None;
    retry.error_message.clear();
    retry.recovery_guidance.clear();
    retry.processed_bytes = 0;
    retry.retry_count = original->retry_count + 1;
    retry.original_task_id = original->task_id;
    retry.original_error_code = original->error_code;
    retry.original_error_message = original->error_message;
    retry.created_timestamp = utc_timestamp_now();
    retry.completed_timestamp.clear();
    retry.start_time = {};
    retry.finish_time = {};
    if (enable_preflight_) {
        const auto fresh_p = run_package_preflight(retry.source_path, target_storage_dir_);
        retry.has_preflight_snapshot = true;
        retry.preflight_summary_snapshot = fresh_p.summary;
    }
    task_queue_.push_back(retry);
    save_state_locked();
    return retry.task_id;
}

std::string InstallPipeline::install_all_dlcs(const GameEntry& entry) {
    return queue_batch(entry.title_id, "DLCs for " + entry.title, ItemKind::Dlc, entry.dlcs);
}

std::string InstallPipeline::queue_batch(const std::string& title_id,
                                        const std::string& batch_title,
                                        ItemKind target_kind,
                                        const std::vector<GameItem>& items) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    active_batch_ = BatchProgress{};
    active_batch_.batch_id = generate_task_id();
    active_batch_.title_id = title_id;
    active_batch_.title_name = batch_title;
    active_batch_.target_kind = target_kind;
    active_batch_.total_items = items.size();
    active_batch_.is_running = !items.empty();

    for (const auto& item : items) {
        InstallTask task;
        task.task_id = generate_task_id();
        task.title_id = title_id;
        task.package_filename = item.filename;
        task.display_name = item.display_name.empty() ? item.filename : item.display_name;
        task.source_path = item.absolute_path.empty() ? item.filename : item.absolute_path;
        task.format = item.format;
        task.kind = item.kind;
        task.total_bytes = item.size_bytes;
        task.status = InstallStatus::Queued;
        task.created_timestamp = utc_timestamp_now();
        active_batch_.total_bytes += item.size_bytes;
        task_queue_.push_back(task);
    }
    save_state_locked();
    return active_batch_.batch_id;
}

void InstallPipeline::cancel_active_task() {
    cancel_requested_ = true;
}

void InstallPipeline::cancel_all() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    cancel_requested_ = true;
    for (auto& task : task_queue_) {
        task.status = InstallStatus::Cancelled;
        task.error_code = InstallErrorCode::Cancelled;
        task.error_message = "Batch cancelled by user";
        task.recovery_guidance = get_recovery_guidance(task.error_code);
        task_history_.push_back(task);
        if (active_batch_.is_running) active_batch_.cancelled_items++;
    }
    task_queue_.clear();
    if (active_batch_.is_running) {
        active_batch_.is_cancelled = true;
        active_batch_.is_running = false;
        if (batch_finished_callback_) batch_finished_callback_(active_batch_);
    }
    save_state_locked();
}

bool InstallPipeline::is_busy() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return active_task_.has_value() || !task_queue_.empty();
}

std::optional<InstallTask> InstallPipeline::current_active_task() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return active_task_;
}

BatchProgress InstallPipeline::current_batch_progress() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return active_batch_;
}

std::vector<InstallTask> InstallPipeline::all_tasks() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    auto result = task_history_;
    if (active_task_) result.push_back(*active_task_);
    result.insert(result.end(), task_queue_.begin(), task_queue_.end());
    return result;
}

std::vector<InstallTask> InstallPipeline::queued_tasks() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return task_queue_;
}

std::vector<InstallTask> InstallPipeline::completed_tasks() const {
    auto tasks = all_tasks();
    tasks.erase(std::remove_if(tasks.begin(), tasks.end(), [](const auto& task) {
        return task.status != InstallStatus::Completed;
    }), tasks.end());
    return tasks;
}

std::vector<InstallTask> InstallPipeline::failed_tasks() const {
    auto tasks = all_tasks();
    tasks.erase(std::remove_if(tasks.begin(), tasks.end(), [](const auto& task) {
        return task.status != InstallStatus::Failed && task.status != InstallStatus::Interrupted;
    }), tasks.end());
    return tasks;
}

std::optional<InstallTask> InstallPipeline::get_task(const std::string& task_id) const {
    const auto tasks = all_tasks();
    for (const auto& task : tasks) {
        if (task.task_id == task_id) return task;
    }
    return std::nullopt;
}

std::optional<InstallTask> InstallPipeline::get_task_by_path(const std::string& source_path) const {
    const auto tasks = all_tasks();
    for (auto it = tasks.rbegin(); it != tasks.rend(); ++it) {
        if (it->source_path == source_path || it->package_filename == source_path) return *it;
    }
    return std::nullopt;
}

std::vector<InstallTask> InstallPipeline::search_history(const std::string& query,
                                                        std::optional<InstallStatus> status_filter) const {
    HistoryFilterOptions options;
    options.query = query;
    options.outcome_filter = status_filter;
    options.sort_field = HistorySortField::Timestamp;
    options.sort_order = HistorySortOrder::Descending;
    return filter_and_sort_history(options);
}

std::vector<InstallTask> InstallPipeline::filter_and_sort_history(const HistoryFilterOptions& options) const {
    auto tasks = all_tasks();
    std::string needle = lower_copy(options.query);
    tasks.erase(std::remove_if(tasks.begin(), tasks.end(), [&](const InstallTask& task) {
        if (options.outcome_filter.has_value() && task.status != *options.outcome_filter) {
            return true;
        }
        if (!options.format_filter.empty()) {
            if (!matches_format(task, options.format_filter)) {
                return true;
            }
        }
        const std::string& ts = !task.created_timestamp.empty() ? task.created_timestamp : task.completed_timestamp;
        if (!options.date_from.empty() && !passes_date_from(ts, options.date_from)) {
            return true;
        }
        if (!options.date_to.empty() && !passes_date_to(ts, options.date_to)) {
            return true;
        }
        if (!needle.empty()) {
            const std::string haystack = lower_copy(task.title_id + " " + task.package_filename + " " + task.display_name + " " +
                task.source_path + " " + task.task_id + " " + to_string(task.status) + " " + to_string(task.error_code) + " " +
                task.error_message + " " + task.recovery_guidance + " " + task.created_timestamp + " " + task.completed_timestamp);
            if (haystack.find(needle) == std::string::npos) {
                return true;
            }
        }
        return false;
    }), tasks.end());

    std::stable_sort(tasks.begin(), tasks.end(), [&](const InstallTask& a, const InstallTask& b) {
        if (options.sort_field == HistorySortField::PackageName) {
            std::string name_a = lower_copy(!a.package_filename.empty() ? a.package_filename : a.display_name);
            std::string name_b = lower_copy(!b.package_filename.empty() ? b.package_filename : b.display_name);
            if (options.sort_order == HistorySortOrder::Ascending) {
                return name_a < name_b;
            } else {
                return name_a > name_b;
            }
        } else {
            const std::string& ts_a = !a.created_timestamp.empty() ? a.created_timestamp : a.completed_timestamp;
            const std::string& ts_b = !b.created_timestamp.empty() ? b.created_timestamp : b.completed_timestamp;
            if (options.sort_order == HistorySortOrder::Ascending) {
                return ts_a < ts_b;
            } else {
                return ts_a > ts_b;
            }
        }
    });

    return tasks;
}

std::string InstallPipeline::export_history_to_json(int indent) const {
    return export_history_to_json(all_tasks(), indent);
}

std::string InstallPipeline::export_history_to_json(const std::vector<InstallTask>& tasks, int indent) const {
    return ps5_library::export_history_to_json(tasks, indent);
}

std::string InstallPipeline::export_history_to_csv() const {
    return export_history_to_csv(all_tasks());
}

std::string InstallPipeline::export_history_to_csv(const std::vector<InstallTask>& tasks) const {
    return ps5_library::export_history_to_csv(tasks);
}

bool InstallPipeline::export_history_to_file(const std::string& filepath, bool as_json) const {
    return export_history_to_file(filepath, all_tasks(), as_json);
}

bool InstallPipeline::export_history_to_file(const std::string& filepath, const std::vector<InstallTask>& tasks, bool as_json) const {
    return ps5_library::export_history_to_file(filepath, tasks, as_json);
}

InstallStatus InstallPipeline::get_item_status(const std::string& source_path) const {
    const auto task = get_task_by_path(source_path);
    return task ? task->status : InstallStatus::Queued;
}

void InstallPipeline::set_progress_callback(TaskProgressCallback cb) {
    progress_callback_ = std::move(cb);
}

void InstallPipeline::set_task_finished_callback(TaskFinishedCallback cb) {
    task_finished_callback_ = std::move(cb);
}

void InstallPipeline::set_batch_finished_callback(BatchFinishedCallback cb) {
    batch_finished_callback_ = std::move(cb);
}

void InstallPipeline::set_persistence_path(const std::string& path) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    state_filepath_ = path;
    if (!path.empty()) load_and_restore_state();
}

bool InstallPipeline::save_state() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    save_state_locked();
    return state_filepath_.empty() || std::filesystem::exists(state_filepath_);
}

void InstallPipeline::save_state_locked() {
    if (state_filepath_.empty()) return;
    try {
        json root;
        root["schema_version"] = 2;
        root["tasks"] = json::array();
        for (const auto& task : task_history_) root["tasks"].push_back(task_to_json(task));
        if (active_task_) root["tasks"].push_back(task_to_json(*active_task_));
        for (const auto& task : task_queue_) root["tasks"].push_back(task_to_json(task));
        root["next_id"] = next_id_counter_;
        root["saved_history_filter"] = json::parse(serialize_history_filter_options(saved_history_filter_options_));

        const std::string tmp = make_unique_temp_path(state_filepath_);
        std::filesystem::path target(state_filepath_);
        if (!target.parent_path().empty()) std::filesystem::create_directories(target.parent_path());
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) return;
            out << root.dump(2);
            out.flush();
            if (!out) return;
        }
        std::error_code ec;
        std::filesystem::rename(tmp, state_filepath_, ec);
        if (ec) {
            std::filesystem::remove(state_filepath_, ec);
            ec.clear();
            std::filesystem::rename(tmp, state_filepath_, ec);
        }
        if (ec) std::filesystem::remove(tmp, ec);
    } catch (...) {}
}

bool InstallPipeline::load_and_restore_state() {
    if (state_filepath_.empty()) return false;
    std::lock_guard<std::mutex> lock(queue_mutex_);
    try {
        std::ifstream in(state_filepath_);
        if (!in) return false;
        json root;
        in >> root;
        const int schema = root.value("schema_version", 0);
        if ((schema != 1 && schema != 2) || !root.contains("tasks")) return false;

        task_queue_.clear();
        task_history_.clear();
        active_task_.reset();

        for (const auto& encoded : root["tasks"]) {
            InstallTask task = task_from_json(encoded);
            if (task.status == InstallStatus::InProgress) {
                task.status = InstallStatus::Interrupted;
                task.error_code = InstallErrorCode::InstallInterrupted;
                task.error_message = "Application stopped while this package was installing";
                task.completed_timestamp = utc_timestamp_now();
                task.recovery_guidance = get_recovery_guidance(task.error_code);
                task_history_.push_back(task);
            } else if (task.status == InstallStatus::Queued) {
                task_queue_.push_back(task);
            } else {
                task_history_.push_back(task);
            }
        }
        next_id_counter_ = root.value("next_id", next_id_counter_);
        if (root.contains("saved_history_filter")) {
            saved_history_filter_options_ = deserialize_history_filter_options(root["saved_history_filter"].dump());
        }
        return true;
    } catch (...) {
        return false;
    }
}

void InstallPipeline::start_worker() {
    if (worker_running_.load()) return;
    worker_running_ = true;
    cancel_requested_ = false;
    worker_thread_ = std::thread(&InstallPipeline::worker_loop, this);
}

void InstallPipeline::stop_worker() {
    if (!worker_running_.load()) return;
    worker_running_ = false;
    cancel_requested_ = true;
    if (worker_thread_.joinable()) worker_thread_.join();
}

void InstallPipeline::worker_loop() {
    while (worker_running_.load()) {
        bool did_work = process_one_step();
        if (!did_work) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

bool InstallPipeline::process_one_step() {
    InstallTask current;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (task_queue_.empty()) return false;
        current = task_queue_.front();
        task_queue_.erase(task_queue_.begin());
        current.status = InstallStatus::InProgress;
        current.start_time = std::chrono::steady_clock::now();
        active_task_ = current;
        if (active_batch_.is_running) {
            active_batch_.current_item_index = active_batch_.completed_items + active_batch_.failed_items + 1;
            active_batch_.current_item_name = current.package_filename;
        }
        save_state_locked();
    }

    if (enable_preflight_) {
        const auto preflight = run_package_preflight(current.source_path, target_storage_dir_);
        if (current.has_preflight_snapshot) {
            current.preflight_changes = diff_preflight_summaries(current.preflight_summary_snapshot, preflight.summary);
        }
        if (!preflight.passed) {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            current.status = InstallStatus::Failed;
            current.error_code = preflight.error_code;
            std::string err_msg = "Preflight check failed immediately before dispatch: " + preflight.error_message;
            if (!current.preflight_changes.empty()) {
                err_msg += "\n" + format_preflight_diffs(current.preflight_changes);
            }
            current.error_message = err_msg;
            current.recovery_guidance = preflight.recovery_guidance;
            current.finish_time = std::chrono::steady_clock::now();
            current.completed_timestamp = utc_timestamp_now();
            task_history_.push_back(current);
            active_task_.reset();
            if (active_batch_.is_running) {
                active_batch_.failed_items++;
                active_batch_.has_failures = true;
                active_batch_.failure_messages.push_back(current.package_filename + ": " + current.error_message);
            }
            save_state_locked();
            if (task_finished_callback_) task_finished_callback_(current);
            return true;
        }
    }

    std::shared_ptr<IInstallDelegate> delegate_copy;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        delegate_copy = delegate_;
    }

    InstallErrorCode err_code = InstallErrorCode::None;
    std::string err_str;
    bool success = false;

    if (delegate_copy) {
        success = delegate_copy->install_package(
            current,
            [&](uint64_t written, uint64_t total) -> bool {
                if (cancel_requested_.load()) return false;
                std::lock_guard<std::mutex> lock(queue_mutex_);
                if (active_task_) {
                    active_task_->processed_bytes = written;
                    active_task_->total_bytes = total;
                    if (progress_callback_) progress_callback_(*active_task_, active_batch_);
                }
                return true;
            },
            err_code,
            err_str
        );
    }

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        current.finish_time = std::chrono::steady_clock::now();
        current.completed_timestamp = utc_timestamp_now();

        if (success) {
            current.status = InstallStatus::Completed;
            current.processed_bytes = current.total_bytes;
            current.error_code = InstallErrorCode::None;
            current.error_message.clear();
            current.recovery_guidance = get_recovery_guidance(InstallErrorCode::None);
            if (active_batch_.is_running) {
                active_batch_.completed_items++;
                active_batch_.processed_bytes += current.total_bytes;
            }
        } else if (err_code == InstallErrorCode::Cancelled || cancel_requested_.load()) {
            current.status = InstallStatus::Cancelled;
            current.error_code = InstallErrorCode::Cancelled;
            current.error_message = err_str.empty() ? "Installation cancelled" : err_str;
            current.recovery_guidance = get_recovery_guidance(InstallErrorCode::Cancelled);
            if (active_batch_.is_running) active_batch_.cancelled_items++;
        } else {
            current.status = InstallStatus::Failed;
            current.error_code = err_code;
            current.error_message = err_str.empty() ? "Installation failed" : err_str;
            current.recovery_guidance = get_recovery_guidance(current.error_code, current.error_message);
            if (active_batch_.is_running) {
                active_batch_.failed_items++;
                active_batch_.has_failures = true;
                active_batch_.failure_messages.push_back(current.package_filename + ": " + current.error_message);
            }
        }

        task_history_.push_back(current);
        active_task_.reset();

        if (active_batch_.is_running && task_queue_.empty()) {
            active_batch_.is_running = false;
            active_batch_.is_completed = true;
            if (batch_finished_callback_) batch_finished_callback_(active_batch_);
        }
        save_state_locked();
    }

    if (task_finished_callback_) task_finished_callback_(current);
    return true;
}

} // namespace ps5_library
