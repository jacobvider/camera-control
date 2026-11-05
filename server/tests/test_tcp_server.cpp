// SPDX-License-Identifier: MIT
// End-to-end tests over real TCP sockets with the simulated camera.
#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <thread>

#include "superlotis/log.hpp"
#include "superlotis/simulated_backend.hpp"
#include "superlotis/tcp_server.hpp"

using namespace superlotis;
namespace fs = std::filesystem;

namespace {

class Conn {
public:
    explicit Conn(std::uint16_t port) {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::connect(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0)
            throw std::runtime_error("connect");
        timeval tv{10, 0};
        ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    }
    ~Conn() {
        if (fd_ >= 0) ::close(fd_);
    }
    void send_raw(const std::string& s) {
        ASSERT_EQ(::send(fd_, s.data(), s.size(), MSG_NOSIGNAL), static_cast<ssize_t>(s.size()));
    }
    void half_close() { ::shutdown(fd_, SHUT_WR); }
    std::string line() {
        std::string out;
        char c;
        while (::recv(fd_, &c, 1, 0) == 1) {
            if (c == '\n') return out;
            out += c;
        }
        return out.empty() ? "<eof>" : out;
    }
    std::string ask(const std::string& cmd) {
        send_raw(cmd + "\n");
        return line();
    }

private:
    int fd_ = -1;
};

class ServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        log::set_level(log::Level::Warn);
        dir = fs::temp_directory_path() / ("slcam_tcp_" + std::to_string(::getpid()) + "_" +
                                           ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(dir);
        cfg.storage.data_dir = dir;
        cfg.server.port = 0;
        cfg.server.max_clients = 3;
        cfg.simulator.width = 32;
        cfg.simulator.height = 16;
        cfg.simulator.seed = 7;
        cfg.simulator.time_scale = 0.0;
    }
    void start() {
        controller =
            std::make_unique<CameraController>(std::make_unique<SimulatedBackend>(cfg.simulator), cfg);
        handler = std::make_unique<protocol::CommandHandler>(*controller, cfg.server);
        server = std::make_unique<TcpServer>(*handler, cfg.server);
        server->start();
        thread = std::thread([this] { server->run(); });
    }
    void TearDown() override {
        if (server) server->stop();
        if (thread.joinable()) thread.join();
        server.reset();
        fs::remove_all(dir);
    }
    fs::path dir;
    Config cfg;
    std::unique_ptr<CameraController> controller;
    std::unique_ptr<protocol::CommandHandler> handler;
    std::unique_ptr<TcpServer> server;
    std::thread thread;
};

}  // namespace

TEST_F(ServerTest, PersistentConnectionHandlesManyCommands) {
    start();
    Conn c(server->port());
    EXPECT_EQ(c.ask("ping"), "OK pong");
    EXPECT_EQ(c.ask("exptime 0.5"), "OK 0.500");
    const std::string r = c.ask("expose");
    EXPECT_EQ(r.rfind("OK file=", 0), 0u) << r;
    EXPECT_EQ(c.ask("status").rfind("OK state=idle", 0), 0u);
    EXPECT_EQ(c.ask("quit"), "OK bye");
    EXPECT_EQ(c.line(), "<eof>");
}

TEST_F(ServerTest, PipelinedRequestsInOnePacket) {
    start();
    Conn c(server->port());
    c.send_raw("ping\nexptime 1\n\nversion\n");
    EXPECT_EQ(c.line(), "OK pong");
    EXPECT_EQ(c.line(), "OK 1.000");
    EXPECT_EQ(c.line().rfind("OK version=", 0), 0u);
}

TEST_F(ServerTest, NetcatStyleOneShotWithoutTrailingNewline) {
    start();
    Conn c(server->port());
    c.send_raw("ping");
    c.half_close();
    EXPECT_EQ(c.line(), "OK pong");
}

TEST_F(ServerTest, RejectsOverlongLines) {
    start();
    Conn c(server->port());
    c.send_raw(std::string(5000, 'a'));
    EXPECT_EQ(c.line().rfind("ERR bad_request", 0), 0u);
}

TEST_F(ServerTest, EnforcesClientLimit) {
    start();
    Conn a(server->port()), b(server->port()), c(server->port());
    EXPECT_EQ(a.ask("ping"), "OK pong");
    EXPECT_EQ(b.ask("ping"), "OK pong");
    EXPECT_EQ(c.ask("ping"), "OK pong");
    Conn d(server->port());
    EXPECT_EQ(d.line().rfind("ERR busy", 0), 0u);
}

TEST_F(ServerTest, StatusFromSecondClientWhileExposing) {
    cfg.simulator.time_scale = 1.0;  // real-time exposure
    cfg.simulator.readout_s = 0.0;
    cfg.simulator.cooldown_tau_s = 0.001;
    start();
    Conn exposer(server->port());
    Conn watcher(server->port());
    exposer.send_raw("expose exptime=1.5\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    const auto t0 = std::chrono::steady_clock::now();
    const std::string st = watcher.ask("status");
    const auto dt = std::chrono::steady_clock::now() - t0;
    EXPECT_EQ(st.rfind("OK state=exposing", 0), 0u) << st;
    EXPECT_LT(std::chrono::duration<double>(dt).count(), 0.5) << "status blocked behind the exposure";
    EXPECT_EQ(watcher.ask("expose").rfind("ERR busy", 0), 0u);
    EXPECT_EQ(exposer.line().rfind("OK file=", 0), 0u);
    EXPECT_EQ(watcher.ask("status").rfind("OK state=idle", 0), 0u);
}

TEST_F(ServerTest, RemoteShutdownWhenAllowed) {
    cfg.server.allow_remote_shutdown = true;
    start();
    Conn c(server->port());
    EXPECT_EQ(c.ask("shutdown"), "OK shutting down");
    thread.join();  // run() must return on its own
    SUCCEED();
}

TEST_F(ServerTest, TokenAuthentication) {
    cfg.server.auth_token = "hunter2";
    start();
    Conn c(server->port());
    EXPECT_EQ(c.ask("status").rfind("ERR unauthorized", 0), 0u);
    EXPECT_EQ(c.ask("auth hunter2"), "OK authenticated");
    EXPECT_EQ(c.ask("status").rfind("OK state=", 0), 0u);
    Conn other(server->port());
    EXPECT_EQ(other.ask("status").rfind("ERR unauthorized", 0), 0u) << "auth is per-connection";
}

TEST_F(ServerTest, StopLetsInFlightExposureFinishAndReply) {
    cfg.simulator.time_scale = 1.0;
    cfg.simulator.readout_s = 0.0;
    cfg.simulator.cooldown_tau_s = 0.001;
    start();
    Conn exposer(server->port());
    Conn idle(server->port());
    EXPECT_EQ(idle.ask("ping"), "OK pong");
    exposer.send_raw("expose exptime=1\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    server->stop();
    EXPECT_EQ(idle.line(), "<eof>") << "idle clients are disconnected";
    const std::string reply = exposer.line();
    EXPECT_EQ(reply.rfind("OK file=", 0), 0u) << reply;
    thread.join();
}
