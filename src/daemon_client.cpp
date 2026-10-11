#include "ps5_library/daemon_client.hpp"

#include <nlohmann/json.hpp>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <condition_variable>

namespace ps5_library {

const std::vector<std::string>& DaemonLock::protected_features() {
    static const std::vector<std::string> features = {
        "nfs_browser",
        "nfs_mount",
        "install_game",
        "install_dlc",
        "install_all_dlc",
        "install_update",
        "game_mount",
        "game_uninstall"
    };
    return features;
}

DaemonLock::DaemonLock(std::string host, uint16_t port)
    : host_(std::move(host)), port_(port) {
    daemon_info_.host = host_;
    daemon_info_.port = port_;
}

DaemonLock::~DaemonLock() {
    stop_background_monitor();
}

DaemonLock::DaemonLock(DaemonLock&& other) noexcept {
    other.stop_background_monitor();
    std::lock_guard<std::mutex> lock(other.mutex_);
    host_ = std::move(other.host_);
    port_ = other.port_;
    state_ = other.state_;
    daemon_info_ = std::move(other.daemon_info_);
    callback_ = std::move(other.callback_);
}

DaemonLock& DaemonLock::operator=(DaemonLock&& other) noexcept {
    if (this != &other) {
        stop_background_monitor();
        other.stop_background_monitor();

        std::scoped_lock lock(mutex_, other.mutex_);
        host_ = std::move(other.host_);
        port_ = other.port_;
        state_ = other.state_;
        daemon_info_ = std::move(other.daemon_info_);
        callback_ = std::move(other.callback_);
    }
    return *this;
}

bool DaemonLock::is_unlocked() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == LockState::Unlocked;
}

LockState DaemonLock::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

DaemonInfo DaemonLock::daemon_info() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return daemon_info_;
}

std::string DaemonLock::lock_message() const {
    return SHADOWMOUNT_REQUIRED_MESSAGE;
}

bool DaemonLock::is_feature_allowed(const std::string& feature_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == LockState::Unlocked) {
        return true;
    }
    const auto& protected_list = protected_features();
    return std::find(protected_list.begin(), protected_list.end(), feature_name) == protected_list.end();
}

void DaemonLock::set_target(std::string host, uint16_t port) {
    std::lock_guard<std::mutex> lock(mutex_);
    host_ = std::move(host);
    port_ = port;
    daemon_info_.host = host_;
    daemon_info_.port = port_;
}

std::pair<std::string, uint16_t> DaemonLock::target() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {host_, port_};
}

void DaemonLock::set_state_change_callback(StateChangeCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = std::move(callback);
}

void DaemonLock::update_state(LockState new_state, const DaemonInfo& info) {
    LockState old_state;
    StateChangeCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        old_state = state_;
        state_ = new_state;
        daemon_info_ = info;
        cb = callback_;
    }

    if (cb && old_state != new_state) {
        cb(old_state, new_state, info);
    }
}

bool DaemonLock::send_http_get(const std::string& host, uint16_t port, const std::string& path,
                              std::chrono::milliseconds timeout, std::string& response_body, int& status_code) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return false;
    }

    // Set non-blocking
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(sock);
        return false;
    }

    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
        close(sock);
        return false;
    }

    int res = connect(sock, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr));
    if (res < 0 && errno != EINPROGRESS) {
        close(sock);
        return false;
    }

    struct pollfd pfd{};
    pfd.fd = sock;
    pfd.events = POLLOUT;

    int poll_res = poll(&pfd, 1, static_cast<int>(timeout.count()));
    if (poll_res <= 0) {
        close(sock);
        return false;
    }

    int so_error = 0;
    socklen_t len = sizeof(so_error);
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0 || so_error != 0) {
        close(sock);
        return false;
    }

    // Send HTTP GET request
    std::ostringstream req;
    req << "GET " << path << " HTTP/1.1\r\n"
        << "Host: " << host << ":" << port << "\r\n"
        << "User-Agent: ps5-library-browser/1.0\r\n"
        << "Accept: application/json\r\n"
        << "Connection: close\r\n\r\n";
    std::string req_str = req.str();

    ssize_t sent = send(sock, req_str.c_str(), req_str.size(), 0);
    if (sent < 0 || static_cast<size_t>(sent) != req_str.size()) {
        close(sock);
        return false;
    }

    // Read response with poll
    std::string raw_response;
    char buffer[2048];
    auto start_time = std::chrono::steady_clock::now();

    while (true) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);
        if (elapsed >= timeout) {
            break;
        }

        pfd.events = POLLIN;
        int remaining_ms = static_cast<int>((timeout - elapsed).count());
        if (remaining_ms <= 0) break;

        poll_res = poll(&pfd, 1, remaining_ms);
        if (poll_res <= 0) break;

        ssize_t bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            raw_response.append(buffer, static_cast<size_t>(bytes_read));
        } else if (bytes_read == 0) {
            // Connection closed by server
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            break;
        }
    }

    close(sock);

    if (raw_response.empty()) {
        return false;
    }

    // Parse HTTP status line
    size_t status_line_end = raw_response.find("\r\n");
    if (status_line_end == std::string::npos) {
        return false;
    }
    std::string status_line = raw_response.substr(0, status_line_end);
    std::istringstream iss(status_line);
    std::string http_ver;
    iss >> http_ver >> status_code;

    // Parse body after headers
    size_t header_end = raw_response.find("\r\n\r\n");
    if (header_end != std::string::npos) {
        response_body = raw_response.substr(header_end + 4);
    } else {
        response_body.clear();
    }

    return true;
}

ProbeResult DaemonLock::probe(std::chrono::milliseconds timeout) {
    std::string current_host;
    uint16_t current_port;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        current_host = host_;
        current_port = port_;
    }

    DaemonInfo info;
    info.host = current_host;
    info.port = current_port;
    info.reachable = false;

    auto t0 = std::chrono::steady_clock::now();
    std::string body;
    int status_code = 0;

    bool http_ok = send_http_get(current_host, current_port, "/api/v1/version", timeout, body, status_code);
    auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
    info.latency = latency;

    ProbeResult result;
    if (!http_ok || status_code != 200) {
        info.reachable = false;
        info.error_message = http_ok ? ("HTTP status " + std::to_string(status_code)) : "Connection refused or timed out";
        result.success = false;
        result.new_state = LockState::Locked;
        result.info = info;
        update_state(LockState::Locked, info);
        return result;
    }

    try {
        auto parsed = nlohmann::json::parse(body);
        info.reachable = true;
        info.api_version = parsed.value("api_version", 0);
        info.shadowmount_version = parsed.value("shadowmount_version", "unknown");

        if (parsed.contains("capabilities") && parsed["capabilities"].is_array()) {
            for (const auto& cap : parsed["capabilities"]) {
                if (cap.is_string()) {
                    info.capabilities.push_back(cap.get<std::string>());
                }
            }
        }

        result.success = true;
        result.new_state = LockState::Unlocked;
        result.info = info;
        update_state(LockState::Unlocked, info);
        return result;

    } catch (const std::exception& ex) {
        info.reachable = false;
        info.error_message = std::string("Malformed JSON from daemon: ") + ex.what();
        result.success = false;
        result.new_state = LockState::Error;
        result.info = info;
        update_state(LockState::Error, info);
        return result;
    }
}

void DaemonLock::start_background_monitor(std::chrono::milliseconds interval) {
    if (monitor_running_.exchange(true)) {
        return; // Already running
    }

    monitor_thread_ = std::thread([this, interval]() {
        while (monitor_running_.load()) {
            probe(std::chrono::milliseconds(1000));

            // Sleep in 100ms increments to exit promptly on stop
            auto elapsed = std::chrono::milliseconds(0);
            while (monitor_running_.load() && elapsed < interval) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                elapsed += std::chrono::milliseconds(100);
            }
        }
    });
}

void DaemonLock::stop_background_monitor() {
    if (!monitor_running_.exchange(false)) {
        return;
    }
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
}

} // namespace ps5_library
