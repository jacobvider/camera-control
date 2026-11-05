// SPDX-License-Identifier: MIT
// Multi-client TCP front end. Each connection gets its own thread, so a
// client can poll `status` while another client's exposure is running.
#pragma once

#include <atomic>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "superlotis/config.hpp"
#include "superlotis/protocol.hpp"

namespace superlotis {

class TcpServer {
public:
    TcpServer(protocol::CommandHandler& handler, ServerConfig config);
    ~TcpServer();
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    /// Binds and listens. Throws std::runtime_error on failure.
    void start();
    /// Accept loop. Blocks until stop() is called (from any thread).
    void run();
    /// Requests shutdown: stops accepting and disconnects all clients.
    void stop();
    /// The bound port (useful when configured with port 0).
    [[nodiscard]] std::uint16_t port() const noexcept { return bound_port_; }
    [[nodiscard]] int active_clients() const;

private:
    struct Client {
        std::thread thread;
        int fd = -1;
        std::atomic<bool> done{false};
    };

    void serve(Client& client, const std::string& peer);
    void reap_finished();

    protocol::CommandHandler& handler_;
    ServerConfig config_;
    int listen_fd_ = -1;
    int wake_pipe_[2] = {-1, -1};
    std::uint16_t bound_port_ = 0;
    std::atomic<bool> stopping_{false};

    mutable std::mutex clients_mutex_;
    std::list<std::unique_ptr<Client>> clients_;
};

}  // namespace superlotis
