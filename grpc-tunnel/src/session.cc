#include "session.h"

#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <vector>

#include "log.h"
#include "net.h"

namespace gt {
namespace {
// One read of the spliced socket, and so one DATA frame. 32 KiB comfortably
// holds a batch of the HTTP/2 frames riding inside without forcing the tunnel
// to fragment them further.
constexpr size_t kChunk = 32 * 1024;
}  // namespace

Conn::~Conn() {
    if (fd >= 0) ::close(fd);
}

bool Session::Send(const Frame& f) {
    std::lock_guard<std::mutex> lock(write_mu_);
    if (!alive_) return false;
    return write_(f);
}

bool Session::SendData(uint64_t id, const char* buf, size_t len) {
    Frame f;
    f.set_stream_id(id);
    f.set_type(Frame::DATA);
    f.set_data(buf, len);
    return Send(f);
}

bool Session::SendClose(uint64_t id, const std::string& error) {
    Frame f;
    f.set_stream_id(id);
    f.set_type(Frame::CLOSE);
    if (!error.empty()) f.set_error(error);
    return Send(f);
}

void Session::Shutdown() {
    std::lock_guard<std::mutex> lock(write_mu_);
    alive_ = false;
}

ConnPtr Session::AddConn(uint64_t id, int fd) {
    auto conn = std::make_shared<Conn>(fd);
    std::lock_guard<std::mutex> lock(conns_mu_);
    conns_[id] = conn;
    return conn;
}

ConnPtr Session::GetConn(uint64_t id) {
    std::lock_guard<std::mutex> lock(conns_mu_);
    auto it = conns_.find(id);
    return it == conns_.end() ? nullptr : it->second;
}

bool Session::CloseConn(uint64_t id) {
    ConnPtr conn;
    {
        std::lock_guard<std::mutex> lock(conns_mu_);
        auto it = conns_.find(id);
        if (it == conns_.end()) return false;
        conn = it->second;
        conns_.erase(it);
    }
    conn->closed.store(true);
    // Wake the pump thread; it owns the close() via ~Conn.
    ::shutdown(conn->fd, SHUT_RDWR);
    return true;
}

void Session::CloseAll() {
    std::map<uint64_t, ConnPtr> taken;
    {
        std::lock_guard<std::mutex> lock(conns_mu_);
        taken.swap(conns_);
    }
    for (auto& [id, conn] : taken) {
        conn->closed.store(true);
        ::shutdown(conn->fd, SHUT_RDWR);
    }
}

size_t Session::OpenStreams() {
    std::lock_guard<std::mutex> lock(conns_mu_);
    return conns_.size();
}

void Session::Pump(uint64_t id, ConnPtr conn) {
    std::vector<char> buf(kChunk);
    uint64_t total = 0;

    for (;;) {
        ssize_t n = ::recv(conn->fd, buf.data(), buf.size(), 0);
        if (n > 0) {
            total += static_cast<uint64_t>(n);
            if (!SendData(id, buf.data(), static_cast<size_t>(n))) break;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        break;  // EOF, error, or CloseConn() shut the socket down
    }

    // A stream torn down from the far end is already out of the table and has
    // already been announced; only tell the peer about ends we discovered.
    bool ours = !conn->closed.exchange(true);
    {
        std::lock_guard<std::mutex> lock(conns_mu_);
        conns_.erase(id);
    }
    if (ours) SendClose(id);
    LOG("stream %llu closed (%llu bytes from socket)",
        static_cast<unsigned long long>(id),
        static_cast<unsigned long long>(total));
}

}  // namespace gt
