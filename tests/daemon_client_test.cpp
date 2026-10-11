#include "ps5_library/daemon_client.hpp"

#include <iostream>
#include <cstdlib>
#include <iostream>

#define TEST_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)
#include <thread>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
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
        TEST_ASSERT(server_fd_ >= 0);

        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0; // Kernel picks free ephemeral port

        int bind_res = bind(server_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        TEST_ASSERT(bind_res == 0);
        (void)bind_res;

        socklen_t len = sizeof(addr);
        getsockname(server_fd_, reinterpret_cast<struct sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);

        listen(server_fd_, 5);
        running_ = true;

        worker_ = std::thread([this]() {
            while (running_.load()) {
                int fd = server_fd_;
                if (fd < 0) {
                    break;
                }

                struct pollfd pfd{};
                pfd.fd = fd;
                pfd.events = POLLIN;

                int pr = poll(&pfd, 1, 100);
                if (!running_.load() || pr <= 0 || !(pfd.revents & POLLIN)) {
                    continue;
                }

                struct sockaddr_in client_addr{};
                socklen_t clen = sizeof(client_addr);
                int client_fd = accept(fd, reinterpret_cast<struct sockaddr*>(&client_addr), &clen);
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
            int fd = server_fd_;
            server_fd_ = -1;
            if (fd >= 0) {
                close(fd);
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

    TEST_ASSERT(!lock.is_unlocked());
    TEST_ASSERT(lock.state() == LockState::Locked);
    TEST_ASSERT(lock.lock_message() == "ShadowMount Plus is required to unlock these features");

    // Verify protected features are blocked
    TEST_ASSERT(!lock.is_feature_allowed("nfs_browser"));
    TEST_ASSERT(!lock.is_feature_allowed("nfs_mount"));
    TEST_ASSERT(!lock.is_feature_allowed("install_game"));
    TEST_ASSERT(!lock.is_feature_allowed("install_dlc"));
    TEST_ASSERT(!lock.is_feature_allowed("install_all_dlc"));
    TEST_ASSERT(!lock.is_feature_allowed("install_update"));

    // Verify non-protected features are allowed
    TEST_ASSERT(lock.is_feature_allowed("installed_games_view"));
    TEST_ASSERT(lock.is_feature_allowed("view_settings"));
    std::cout << "  -> PASSED" << std::endl;
}

void test_probe_down_daemon() {
    std::cout << "[TEST] Running test_probe_down_daemon..." << std::endl;
    // Use an unassigned port unlikely to have a listener
    DaemonLock lock("127.0.0.1", 54321);

    auto result = lock.probe(std::chrono::milliseconds(200));
    TEST_ASSERT(!result.success);
    TEST_ASSERT(result.new_state == LockState::Locked);
    TEST_ASSERT(!lock.is_unlocked());
    TEST_ASSERT(!result.info.reachable);
    TEST_ASSERT(!result.info.error_message.empty());
    std::cout << "  -> PASSED" << std::endl;
}

void test_successful_daemon_unlock() {
    std::cout << "[TEST] Running test_successful_daemon_unlock..." << std::endl;
    MockShadowMountServer server;
    uint16_t port = server.start();

    DaemonLock lock("127.0.0.1", port);
    TEST_ASSERT(!lock.is_unlocked());

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
    TEST_ASSERT(result.success);
    TEST_ASSERT(result.new_state == LockState::Unlocked);
    TEST_ASSERT(lock.is_unlocked());
    TEST_ASSERT(lock.state() == LockState::Unlocked);

    // Verify parsed data
    TEST_ASSERT(result.info.reachable);
    TEST_ASSERT(result.info.api_version == 1);
    TEST_ASSERT(result.info.shadowmount_version == "1.7.0");
    TEST_ASSERT(result.info.capabilities.size() == 3);
    TEST_ASSERT(result.info.capabilities[0] == "storage_space");
    TEST_ASSERT(result.info.capabilities[1] == "list_images");
    TEST_ASSERT(result.info.capabilities[2] == "nfs_mount");

    // Verify callback fired
    TEST_ASSERT(callback_count == 1);
    TEST_ASSERT(last_old == LockState::Locked);
    TEST_ASSERT(last_new == LockState::Unlocked);

    // Verify all protected features are now unlocked
    TEST_ASSERT(lock.is_feature_allowed("nfs_browser"));
    TEST_ASSERT(lock.is_feature_allowed("install_game"));
    TEST_ASSERT(lock.is_feature_allowed("install_dlc"));
    TEST_ASSERT(lock.is_feature_allowed("install_all_dlc"));

    std::cout << "  -> PASSED" << std::endl;
}

void test_daemon_disconnect_locks_features() {
    std::cout << "[TEST] Running test_daemon_disconnect_locks_features..." << std::endl;
    MockShadowMountServer server;
    uint16_t port = server.start();

    DaemonLock lock("127.0.0.1", port);
    auto res1 = lock.probe(std::chrono::milliseconds(1000));
    TEST_ASSERT(res1.success);
    TEST_ASSERT(lock.is_unlocked());

    // Stop server to simulate crash or exit
    server.stop();

    // Probe again - should lock
    auto res2 = lock.probe(std::chrono::milliseconds(200));
    TEST_ASSERT(!res2.success);
    TEST_ASSERT(!lock.is_unlocked());
    TEST_ASSERT(lock.state() == LockState::Locked);
    TEST_ASSERT(!lock.is_feature_allowed("nfs_browser"));
    TEST_ASSERT(lock.lock_message() == "ShadowMount Plus is required to unlock these features");

    std::cout << "  -> PASSED" << std::endl;
}

void test_background_monitor() {
    std::cout << "[TEST] Running test_background_monitor..." << std::endl;
    MockShadowMountServer server;
    uint16_t port = server.start();

    DaemonLock lock("127.0.0.1", port);
    TEST_ASSERT(!lock.is_unlocked());

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

    TEST_ASSERT(unlocked_flag.load());
    TEST_ASSERT(lock.is_unlocked());

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
