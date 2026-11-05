// SPDX-License-Identifier: MIT
#include "superlotis/tcp_server.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

#include "superlotis/log.hpp"
#include "superlotis/util.hpp"

namespace superlotis {

namespace {

constexpr std::size_t kMaxLine = 4096;

std::string errno_text() {
    return std::strerror(errno);
}

bool send_all(int fd, const std::string& data) {
    std::size_t off = 0;
    while (off < data.size()) {
        const ssize_t n = ::send(fd, data.data() + off, data.size() - off, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        off += static_cast<std::size_t>(n);
    }
    return true;
}

std::string peer_name(const sockaddr_storage& addr) {
    char host[NI_MAXHOST] = {0};
    char serv[NI_MAXSERV] = {0};
    if (getnameinfo(reinterpret_cast<const sockaddr*>(&addr), sizeof addr, host, sizeof host, serv,
                    sizeof serv, NI_NUMERICHOST | NI_NUMERICSERV) != 0)
        return "unknown";
    return std::string(host) + ":" + serv;
}

}  // namespace

TcpServer::TcpServer(protocol::CommandHandler& handler, ServerConfig config)
    : handler_(handler), config_(std::move(config)) {}

TcpServer::~TcpServer() {
    stop();
    for (auto& c : clients_)
        if (c->thread.joinable()) c->thread.join();
    if (listen_fd_ >= 0) ::close(listen_fd_);
    for (int fd : wake_pipe_)
        if (fd >= 0) ::close(fd);
}

void TcpServer::start() {
    if (::pipe2(wake_pipe_, O_CLOEXEC | O_NONBLOCK) != 0)
        throw std::runtime_error("pipe2 failed: " + errno_text());

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
    addrinfo* res = nullptr;
    const std::string port = std::to_string(config_.port);
    const char* host = config_.bind_address.empty() ? nullptr : config_.bind_address.c_str();
    if (const int rc = getaddrinfo(host, port.c_str(), &hints, &res); rc != 0)
        throw std::runtime_error("cannot resolve bind address '" + config_.bind_address +
                                 "': " + gai_strerror(rc));

    std::string last_error = "no usable address";
    for (addrinfo* ai = res; ai != nullptr; ai = ai->ai_next) {
        const int fd = ::socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, ai->ai_protocol);
        if (fd < 0) {
            last_error = errno_text();
            continue;
        }
        const int one = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        if (::bind(fd, ai->ai_addr, ai->ai_addrlen) == 0 && ::listen(fd, 16) == 0) {
            listen_fd_ = fd;
            break;
        }
        last_error = errno_text();
        ::close(fd);
    }
    freeaddrinfo(res);
    if (listen_fd_ < 0)
        throw std::runtime_error("cannot listen on " + config_.bind_address + ":" + port + ": " + last_error);

    sockaddr_storage bound{};
    socklen_t len = sizeof bound;
    ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&bound), &len);
    if (bound.ss_family == AF_INET)
        bound_port_ = ntohs(reinterpret_cast<sockaddr_in*>(&bound)->sin_port);
    else if (bound.ss_family == AF_INET6)
        bound_port_ = ntohs(reinterpret_cast<sockaddr_in6*>(&bound)->sin6_port);

    log::info("Listening on ", config_.bind_address, ":", bound_port_,
              config_.auth_token.empty() ? " (no authentication)" : " (token authentication)");
    if (config_.auth_token.empty() && config_.bind_address != "127.0.0.1" && config_.bind_address != "::1" &&
        config_.bind_address != "localhost")
        log::warn("Server is reachable from the network without authentication; set server.auth_token");
}

void TcpServer::stop() {
    if (stopping_.exchange(true)) return;
    if (wake_pipe_[1] >= 0) {
        const char b = 'x';
        [[maybe_unused]] const ssize_t n = ::write(wake_pipe_[1], &b, 1);
    }
    const std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto& c : clients_)
        if (!c->done.load() && c->fd >= 0) ::shutdown(c->fd, SHUT_RDWR);
}

int TcpServer::active_clients() const {
    const std::lock_guard<std::mutex> lock(clients_mutex_);
    int n = 0;
    for (const auto& c : clients_) n += c->done.load() ? 0 : 1;
    return n;
}

void TcpServer::reap_finished() {
    const std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto it = clients_.begin(); it != clients_.end();) {
        if ((*it)->done.load()) {
            if ((*it)->thread.joinable()) (*it)->thread.join();
            it = clients_.erase(it);
        } else {
            ++it;
        }
    }
}

void TcpServer::run() {
    if (listen_fd_ < 0) throw std::logic_error("TcpServer::run() called before start()");
    while (!stopping_.load()) {
        pollfd fds[2] = {{listen_fd_, POLLIN, 0}, {wake_pipe_[0], POLLIN, 0}};
        const int rc = ::poll(fds, 2, 1000);
        if (rc < 0) {
            if (errno == EINTR) continue;
            log::error("poll failed: ", errno_text());
            break;
        }
        reap_finished();
        if (fds[1].revents != 0 || stopping_.load()) break;
        if ((fds[0].revents & POLLIN) == 0) continue;

        sockaddr_storage addr{};
        socklen_t len = sizeof addr;
        const int fd = ::accept4(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len, SOCK_CLOEXEC);
        if (fd < 0) {
            if (errno != EINTR && errno != EAGAIN) log::warn("accept failed: ", errno_text());
            continue;
        }
        const std::string peer = peer_name(addr);
        if (active_clients() >= config_.max_clients) {
            log::warn("Rejecting ", peer, ": too many clients");
            send_all(fd, protocol::Response::failure(ErrorCode::Busy, "too many clients").wire());
            ::close(fd);
            continue;
        }
        log::info("Client connected: ", peer);
        const std::lock_guard<std::mutex> lock(clients_mutex_);
        auto client = std::make_unique<Client>();
        client->fd = fd;
        Client& ref = *client;
        clients_.push_back(std::move(client));
        ref.thread = std::thread([this, &ref, peer] { serve(ref, peer); });
    }
    stop();
    std::list<std::unique_ptr<Client>> remaining;
    {
        const std::lock_guard<std::mutex> lock(clients_mutex_);
        remaining.swap(clients_);
    }
    if (!remaining.empty()) log::info("Waiting for ", remaining.size(), " client(s) to finish");
    for (auto& c : remaining)
        if (c->thread.joinable()) c->thread.join();
    log::info("Server stopped");
}

void TcpServer::serve(Client& client, const std::string& peer) {
    protocol::Session session;
    session.peer = peer;
    std::string buffer;
    char chunk[1024];
    bool open = true;
    const int idle_ms = static_cast<int>(config_.idle_timeout_s * 1000.0);

    auto process = [&](const std::string& line) {
        if (util::trim(line).empty()) return;
        log::debug(peer, " > ", line);
        const protocol::Response resp = handler_.handle_line(line, session);
        const std::string wire = resp.wire();
        log::debug(peer, " < ", wire.substr(0, wire.size() - 1));
        if (!send_all(client.fd, wire)) open = false;
        if (resp.close_connection) open = false;
        if (resp.shutdown_server) stop();
    };

    while (open && !stopping_.load()) {
        pollfd p{client.fd, POLLIN, 0};
        const int rc = ::poll(&p, 1, idle_ms);
        if (rc == 0) {
            send_all(client.fd, protocol::Response::failure(ErrorCode::BadRequest, "idle timeout").wire());
            break;
        }
        if (rc < 0) {
            if (errno == EINTR) continue;
            break;
        }
        const ssize_t n = ::recv(client.fd, chunk, sizeof chunk, 0);
        if (n <= 0) {
            // Peer closed. Treat a final unterminated line as a request so that
            // `printf expose | nc -N host port` also works.
            if (n == 0 && !buffer.empty()) process(buffer);
            break;
        }
        buffer.append(chunk, static_cast<std::size_t>(n));
        std::size_t pos;
        while (open && (pos = buffer.find('\n')) != std::string::npos) {
            const std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);
            process(line);
        }
        if (buffer.size() > kMaxLine) {
            send_all(client.fd,
                     protocol::Response::failure(ErrorCode::BadRequest, "request line too long").wire());
            break;
        }
    }
    {
        const std::lock_guard<std::mutex> lock(clients_mutex_);
        ::close(client.fd);
        client.fd = -1;
    }
    log::info("Client disconnected: ", peer);
    client.done.store(true);
}

}  // namespace superlotis
