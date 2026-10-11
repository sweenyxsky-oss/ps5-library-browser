#include "ps5_library/daemon_client.hpp"

#include <iostream>
#include <cassert>
#include <thread>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <sstream>

using namespace ps5_library;

// Helper to run a lightweight mock HTTP server for ShadowMountPlus API
class MockShadowMountServer {
public:
    explicit MockShadowMountServer(int status_code = 200, const std::string& json_body = "")
        : status_code_(status_code), body_(json_body) {
        if (body_.empty()) {
            body_ = "{\"status\":0,\"api_version\":1,\"shadowmount_version\":\"1.7.0\",\"capabilities\":[\"storage_space\",\"list_images\",\"nfs_mount\"]}";
        }
    }

    ~MockShadowMountServer() {
        stop();
    }

    uint16_t start() {
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        assert(server_fd_ >= 0);

        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0; // Kernel picks free ephemeral port

        int bind_res = bind(server_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        assert(bind_res == 0);

        socklen_t len = sizeof(addr);
        getsockname(server_fd_, reinterpret_cast<struct sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);

        listen(server_fd_, 5);
        running_ = true;

        worker_ = std::thread([this]() {
            while (running_.load()) {
                struct timeval tv{0, 200000}; // 200ms
                fd_set read_fds;
                FD_ZERO(&read_fds);
                FD_SET(server_fd_, &read_fds);

                int sel = select(server_fd_ + 1, &read_fds, nullptr, nullptr, &tv);
                if (sel <= 0 || !FD_ISSET(server_fd_, &read_fds)) {
                    continue;
                }

                struct sockaddr_in client_addr{};
                socklen_t clen = sizeof(client_addr);
                int client_fd = accept(server_fd_, reinterpret_cast<struct sockaddr*>(&client_addr), &clen);
                if (client_fd < 0) continue;

                char buffer[1024];
                ssize_t r = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
                if (r > 0) {
                    buffer[r] = '\0';
                    std::ostringstream response;
                    response << "HTTP/1.1 " << status_code_ << " OK\r\n"
                             << "Content-Type: application/json\r\n"
                             << "Content-Length: " << body_.size() << "\r\n"
                             << "Connection: close\r\n\r\n"
                             << body_;
                    std::string res_str = response.str();
                    send(client_fd, res_str.c_str(), res_str.size(), 0);
                }
                close(client_fd);
            }
        });

        return port_;
    }

    void stop() {
        if (running_.exchange(false)) {
            if (server_fd_ >= 0) {
                close(server_fd_);
                server_fd_ = -1;
            }
            if (worker_.joinable()) {
                worker_.join();
            }
        }
    }

    uint16_t port() const { return port_; }

private:
    int status_code_;
    std::string body_;
    int server_fd_{-1};
    uint16_t port_{0};
    std::atomic<bool> running_{false};
    std::thread worker_;
};

void test_initial_locked_state() {
    std::cout << "[TEST] Running test_initial_locked_state..." << std::endl;
    DaemonLock lock;

    assert(!lock.is_unlocked());
    assert(lock.state() == LockState::Locked);
    assert(lock.lock_message() == "ShadowMount Plus is required to unlock these features");

    // Verify protected features are blocked
    assert(!lock.is_feature_allowed("nfs_browser"));
    assert(!lock.is_feature_allowed("nfs_mount"));
    assert(!lock.is_feature_allowed("install_game"));
    assert(!lock.is_feature_allowed("install_dlc"));
    assert(!lock.is_feature_allowed("install_all_dlc"));
    assert(!lock.is_feature_allowed("install_update"));

    // Verify non-protected features are allowed
    assert(lock.is_feature_allowed("installed_games_view"));
    assert(lock.is_feature_allowed("view_settings"));
    std::cout << "  -> PASSED" << std::endl;
}

void test_probe_down_daemon() {
    std::cout << "[TEST] Running test_probe_down_daemon..." << std::endl;
    // Use an unassigned port unlikely to have a listener
    DaemonLock lock("127.0.0.1", 54321);

    auto result = lock.probe(std::chrono::milliseconds(200));
    assert(!result.success);
    assert(result.new_state == LockState::Locked);
    assert(!lock.is_unlocked());
    assert(!result.info.reachable);
    assert(!result.info.error_message.empty());
    std::cout << "  -> PASSED" << std::endl;
}

void test_successful_daemon_unlock() {
    std::cout << "[TEST] Running test_successful_daemon_unlock..." << std::endl;
    MockShadowMountServer server;
    uint16_t port = server.start();

    DaemonLock lock("127.0.0.1", port);
    assert(!lock.is_unlocked());

    std::atomic<int> callback_count{0};
    LockState last_old = LockState::Error;
    LockState last_new = LockState::Error;

    lock.set_state_change_callback([&](LockState old_s, LockState new_s, const DaemonInfo& info) {
        callback_count++;
        last_old = old_s;
        last_new = new_s;
        (void)info;
    });

    auto result = lock.probe(std::chrono::milliseconds(1000));
    assert(result.success);
    assert(result.new_state == LockState::Unlocked);
    assert(lock.is_unlocked());
    assert(lock.state() == LockState::Unlocked);

    // Verify parsed data
    assert(result.info.reachable);
    assert(result.info.api_version == 1);
    assert(result.info.shadowmount_version == "1.7.0");
    assert(result.info.capabilities.size() == 3);
    assert(result.info.capabilities[0] == "storage_space");
    assert(result.info.capabilities[1] == "list_images");
    assert(result.info.capabilities[2] == "nfs_mount");

    // Verify callback fired
    assert(callback_count == 1);
    assert(last_old == LockState::Locked);
    assert(last_new == LockState::Unlocked);

    // Verify all protected features are now unlocked
    assert(lock.is_feature_allowed("nfs_browser"));
    assert(lock.is_feature_allowed("install_game"));
    assert(lock.is_feature_allowed("install_dlc"));
    assert(lock.is_feature_allowed("install_all_dlc"));

    std::cout << "  -> PASSED" << std::endl;
}

void test_daemon_disconnect_locks_features() {
    std::cout << "[TEST] Running test_daemon_disconnect_locks_features..." << std::endl;
    MockShadowMountServer server;
    uint16_t port = server.start();

    DaemonLock lock("127.0.0.1", port);
    auto res1 = lock.probe(std::chrono::milliseconds(1000));
    assert(res1.success);
    assert(lock.is_unlocked());

    // Stop server to simulate crash or exit
    server.stop();

    // Probe again - should lock
    auto res2 = lock.probe(std::chrono::milliseconds(200));
    assert(!res2.success);
    assert(!lock.is_unlocked());
    assert(lock.state() == LockState::Locked);
    assert(!lock.is_feature_allowed("nfs_browser"));
    assert(lock.lock_message() == "ShadowMount Plus is required to unlock these features");

    std::cout << "  -> PASSED" << std::endl;
}

void test_background_monitor() {
    std::cout << "[TEST] Running test_background_monitor..." << std::endl;
    MockShadowMountServer server;
    uint16_t port = server.start();

    DaemonLock lock("127.0.0.1", port);
    assert(!lock.is_unlocked());

    std::atomic<bool> unlocked_flag{false};
    lock.set_state_change_callback([&](LockState, LockState new_s, const DaemonInfo&) {
        if (new_s == LockState::Unlocked) {
            unlocked_flag = true;
        }
    });

    // Start background monitor with rapid 200ms intervals
    lock.start_background_monitor(std::chrono::milliseconds(200));

    // Wait up to 2 seconds for monitor to unlock
    for (int i = 0; i < 20; ++i) {
        if (unlocked_flag.load()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    assert(unlocked_flag.load());
    assert(lock.is_unlocked());

    // Stop monitor cleanly
    lock.stop_background_monitor();
    std::cout << "  -> PASSED" << std::endl;
}

int main() {
    std::cout << "=== Running Milestone 1: ShadowMountPlus Daemon & Lock Tests ===" << std::endl;

    test_initial_locked_state();
    test_probe_down_daemon();
    test_successful_daemon_unlock();
    test_daemon_disconnect_locks_features();
    test_background_monitor();

    std::cout << "=== ALL MILESTONE 1 TESTS PASSED SUCCESSFULLY ===" << std::endl;
    return 0;
}
