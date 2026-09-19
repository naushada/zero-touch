// session.h — one tunnel, and the logical streams multiplexed inside it.
//
// Both ends use this same class; only the direction of OPEN differs. A Session
// owns:
//   * serialised access to the underlying gRPC stream (gRPC permits exactly
//     one in-flight Write, and several threads want to write),
//   * the id -> socket table for the logical streams,
//   * the pump that copies socket bytes into DATA frames.
//
// Lifetime is the subtle part. Pump threads outlive the RPC handler that made
// the Session, and the handler's ServerReaderWriter* dies the moment it
// returns. Shutdown() closes that window: it takes the same mutex Send() does,
// so it cannot complete until any in-flight Write has finished, and every
// Send() afterwards fails without touching the dead pointer.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "tunnel.grpc.pb.h"

namespace gt {

using Frame = tunnel::Frame;

// One spliced TCP connection. The fd is closed by ~Conn, i.e. when both the
// stream table and the pump thread have let go, never while one still reads.
struct Conn {
    explicit Conn(int f) : fd(f) {}
    ~Conn();
    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;

    int fd;
    std::atomic<bool> closed{false};
};

using ConnPtr = std::shared_ptr<Conn>;

class Session {
public:
    using WriteFn = std::function<bool(const Frame&)>;

    explicit Session(WriteFn write) : write_(std::move(write)) {}

    // Serialised write to the tunnel. False once the tunnel is gone.
    bool Send(const Frame& f);

    // Convenience wrappers for the frames every caller sends.
    bool SendData(uint64_t id, const char* buf, size_t len);
    bool SendClose(uint64_t id, const std::string& error = "");

    // Server side only: ids are allocated by whoever opens streams.
    uint64_t NextStreamId() { return next_id_.fetch_add(1); }

    ConnPtr AddConn(uint64_t id, int fd);
    ConnPtr GetConn(uint64_t id);

    // Drop the stream and unblock any pump parked in recv() on it. False if
    // it was already gone — the two ends often close at the same instant.
    bool CloseConn(uint64_t id);
    void CloseAll();

    // Barrier described above: no Send() touches the stream after this.
    void Shutdown();

    // Socket -> tunnel. Runs on its own thread until EOF or tunnel loss, then
    // sends CLOSE. Takes ConnPtr by value to keep the fd alive while reading.
    void Pump(uint64_t id, ConnPtr conn);

    size_t OpenStreams();

private:
    WriteFn write_;
    std::mutex write_mu_;
    bool alive_ = true;

    std::mutex conns_mu_;
    std::map<uint64_t, ConnPtr> conns_;
    std::atomic<uint64_t> next_id_{1};
};

using SessionPtr = std::shared_ptr<Session>;

}  // namespace gt
