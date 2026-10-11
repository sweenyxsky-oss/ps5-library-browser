#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>

namespace ps5_library {

/**
 * Required requirement message displayed when ShadowMountPlus is not running
 */
inline constexpr const char* SHADOWMOUNT_REQUIRED_MESSAGE =
    "ShadowMount Plus is required to unlock these features";

/**
 * Standard default parameters for the ShadowMountPlus daemon
 */
inline constexpr const char* DEFAULT_DAEMON_HOST = "127.0.0.1";
inline constexpr uint16_t DEFAULT_DAEMON_PORT = 10101;
inline constexpr std::chrono::milliseconds DEFAULT_PROBE_TIMEOUT{1500};

enum class LockState {
    Locked,      // ShadowMountPlus daemon is not reachable or incompatible
    Checking,    // Currently probing the daemon
    Unlocked,    // ShadowMountPlus is running and verified
    Error        // Socket error, invalid response, or unexpected failure
};

struct DaemonInfo {
    bool reachable{false};
    std::string host{DEFAULT_DAEMON_HOST};
    uint16_t port{DEFAULT_DAEMON_PORT};
    int api_version{0};
    std::string shadowmount_version;
    std::vector<std::string> capabilities;
    std::chrono::milliseconds latency{0};
    std::string error_message;
};

struct ProbeResult {
    bool success{false};
    LockState new_state{LockState::Locked};
    DaemonInfo info;
};

using StateChangeCallback = std::function<void(LockState old_state, LockState new_state, const DaemonInfo& info)>;

class DaemonLock {
public:
    explicit DaemonLock(std::string host = DEFAULT_DAEMON_HOST, uint16_t port = DEFAULT_DAEMON_PORT);
    ~DaemonLock();

    // Non-copyable, movable
    DaemonLock(const DaemonLock&) = delete;
    DaemonLock& operator=(const DaemonLock&) = delete;
    DaemonLock(DaemonLock&&) noexcept;
    DaemonLock& operator=(DaemonLock&&) noexcept;

    /**
     * Synchronously probes the ShadowMountPlus daemon
     */
    ProbeResult probe(std::chrono::milliseconds timeout = DEFAULT_PROBE_TIMEOUT);

    /**
     * Queries if features are unlocked
     */
    [[nodiscard]] bool is_unlocked() const;

    /**
     * Returns current lock state
     */
    [[nodiscard]] LockState state() const;

    /**
     * Gets the current daemon information
     */
    [[nodiscard]] DaemonInfo daemon_info() const;

    /**
     * Returns the user-facing lock message
     */
    [[nodiscard]] std::string lock_message() const;

    /**
     * Checks if a specific feature is allowed under current lock state.
     * Protected features require ShadowMountPlus to be Unlocked.
     */
    [[nodiscard]] bool is_feature_allowed(const std::string& feature_name) const;

    /**
     * Target host and port configuration
     */
    void set_target(std::string host, uint16_t port);
    [[nodiscard]] std::pair<std::string, uint16_t> target() const;

    /**
     * Registers a callback for state changes
     */
    void set_state_change_callback(StateChangeCallback callback);

    /**
     * Starts background periodic health monitoring
     */
    void start_background_monitor(std::chrono::milliseconds interval = std::chrono::milliseconds(3000));

    /**
     * Stops background monitoring
     */
    void stop_background_monitor();

    /**
     * List of protected feature identifiers
     */
    static const std::vector<std::string>& protected_features();

private:
    void update_state(LockState new_state, const DaemonInfo& info);
    static bool send_http_get(const std::string& host, uint16_t port, const std::string& path,
                             std::chrono::milliseconds timeout, std::string& response_body, int& status_code);

    std::string host_;
    uint16_t port_;
    mutable std::mutex mutex_;
    LockState state_{LockState::Locked};
    DaemonInfo daemon_info_;
    StateChangeCallback callback_;

    // Background thread management
    std::atomic<bool> monitor_running_{false};
    std::thread monitor_thread_;
};

} // namespace ps5_library
