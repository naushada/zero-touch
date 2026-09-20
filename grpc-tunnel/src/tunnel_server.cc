// tunnel_server.cc — the side with a reachable address.
//
// Three things live in this process:
//
//   1. Tunnel/Register    the gRPC SERVER the client dials in to. Accepting
//                         that call is all the inbound connectivity we get.
//   2. the forwarder      a plain TCP listener on --forward. Anything that
//                         connects to it is spliced onto a fresh logical
//                         stream inside the tunnel and carried to the client.
//   3. EchoCaller         the application gRPC CLIENT, pointed at (2). It
//                         believes it is talking to a local service.
//
// So the request direction is server -> client, against the grain of the one
// TCP connection, which is the entire trick.
#include <grpcpp/grpcpp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>

#include "echo_call.h"
#include "log.h"
#include "net.h"
#include "session.h"
#include "tunnel.grpc.pb.h"

namespace gt {
namespace {

// Which tunnel serves which target name. One entry per registered client, so
// a single server can fan out to many edges; the forwarder picks by --target.
class Registry {
public:
    void Add(const std::string& target, SessionPtr sess) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            sessions_[target] = std::move(sess);
        }
        cv_.notify_all();
    }

    // Only drop the entry if it is still ours: a client that reconnects fast
    // can register again before the old handler finishes unwinding.
    void Remove(const std::string& target, const SessionPtr& sess) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = sessions_.find(target);
        if (it != sessions_.end() && it->second == sess) sessions_.erase(it);
    }

    SessionPtr Get(const std::string& target) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = sessions_.find(target);
        return it == sessions_.end() ? nullptr : it->second;
    }

    // Used by --auto so the demo does not fire RPCs into an empty registry.
    SessionPtr WaitFor(const std::string& target) {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait(lock, [&] { return sessions_.count(target) > 0; });
        return sessions_[target];
    }

private:
    std::mutex mu_;
    std::condition_variable cv_;
    std::map<std::string, SessionPtr> sessions_;
};

class TunnelServiceImpl final : public tunnel::Tunnel::Service {
public:
    explicit TunnelServiceImpl(Registry* registry) : registry_(registry) {}

    grpc::Status Register(
        grpc::ServerContext* ctx,
        grpc::ServerReaderWriter<Frame, Frame>* stream) override {
        const std::string peer = ctx->peer();
        LOG("tunnel opened by %s", peer.c_str());

        // The raw `stream` is valid only until this handler returns; Shutdown()
        // below is what makes it safe for pump threads to share.
        auto sess = std::make_shared<Session>(
            [stream](const Frame& f) { return stream->Write(f); });

        std::string target;
        Frame in;
        while (stream->Read(&in)) {
            switch (in.type()) {
                case Frame::REGISTER: {
                    // A second REGISTER replaces the binding (SPEC.md 4.1).
                    // Dropping the old name matters: only `target` is carried
                    // to the teardown below, so an entry left behind here
                    // would outlive the session and hand a later forwarder a
                    // corpse instead of an honest "nothing registered".
                    if (!target.empty() && target != in.target()) {
                        registry_->Remove(target, sess);
                        LOG("target '%s' replaced by '%s'", target.c_str(),
                            in.target().c_str());
                    }
                    target = in.target();
                    registry_->Add(target, sess);
                    LOG("registered target '%s' from %s", target.c_str(),
                        peer.c_str());
                    Frame ack;
                    ack.set_type(Frame::REGISTER_ACK);
                    ack.set_target(target);
                    sess->Send(ack);
                    break;
                }
                case Frame::DATA: {
                    // Tunnel -> socket. Inline, so frames reach the socket in
                    // the order the peer sent them.
                    ConnPtr conn = sess->GetConn(in.stream_id());
                    if (!conn) {
                        // Already torn down at this end; a plain CLOSE stops
                        // the peer without claiming anything went wrong.
                        sess->SendClose(in.stream_id());
                        break;
                    }
                    if (!WriteAll(conn->fd, in.data().data(), in.data().size())) {
                        sess->CloseConn(in.stream_id());
                        sess->SendClose(in.stream_id(), "local write failed");
                    }
                    break;
                }
                case Frame::CLOSE: {
                    // Only a stream we still held can have failed; anything
                    // else is the other half of a simultaneous close.
                    bool ours = sess->CloseConn(in.stream_id());
                    if (ours && !in.error().empty()) {
                        LOG("stream %llu closed by peer: %s",
                            static_cast<unsigned long long>(in.stream_id()),
                            in.error().c_str());
                    }
                    break;
                }
                case Frame::KEEPALIVE:
                    break;
                default:
                    break;
            }
        }

        LOG("tunnel from %s closed (target '%s')", peer.c_str(), target.c_str());
        registry_->Remove(target, sess);
        sess->Shutdown();   // barrier: no pump may touch `stream` after this
        sess->CloseAll();
        return grpc::Status::OK;
    }

private:
    Registry* registry_;
};

// TCP -> tunnel. One accepted connection becomes one logical stream; the
// gRPC client above opens these without knowing it.
void ForwarderLoop(int listen_fd, Registry* registry, const std::string& target) {
    for (;;) {
        int fd = ::accept(listen_fd, nullptr, nullptr);
        if (fd < 0) {
            if (errno == EINTR) continue;
            LOG("forwarder accept failed: %s", strerror(errno));
            return;
        }
        SetNoDelay(fd);

        SessionPtr sess = registry->Get(target);
        if (!sess) {
            LOG("forwarder: no tunnel registered for '%s' yet, refusing",
                target.c_str());
            ::close(fd);
            continue;
        }

        const uint64_t id = sess->NextStreamId();
        ConnPtr conn = sess->AddConn(id, fd);

        Frame open;
        open.set_stream_id(id);
        open.set_type(Frame::OPEN);
        open.set_target(target);
        if (!sess->Send(open)) {
            LOG("forwarder: tunnel went away opening stream %llu",
                static_cast<unsigned long long>(id));
            sess->CloseConn(id);
            continue;
        }
        LOG("stream %llu opened -> '%s'", static_cast<unsigned long long>(id),
            target.c_str());

        // Detached: the pump holds its own refs and cleans up after itself.
        std::thread(&Session::Pump, sess, id, conn).detach();
    }
}

// --auto: keep calling the application service so `logs -f` shows the whole
// path working without anyone typing a command.
void AutoCallLoop(Registry* registry, const std::string& target,
                  const std::string& forward_addr, int period_s) {
    registry->WaitFor(target);
    EchoCaller caller(forward_addr);
    uint64_t n = 0;

    for (;;) {
        const std::string msg = "auto-" + std::to_string(++n);
        std::string reply, error;
        if (caller.Say(msg, &reply, &error)) {
            LOG("Say(%s) -> %s", msg.c_str(), reply.c_str());
        } else {
            LOG("Say(%s) FAILED: %s", msg.c_str(), error.c_str());
        }
        std::this_thread::sleep_for(std::chrono::seconds(period_s));
    }
}

const char* Arg(int argc, char** argv, const std::string& flag,
                const char* fallback) {
    const std::string prefix = flag + "=";
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == flag && i + 1 < argc) return argv[i + 1];
        if (strncmp(argv[i], prefix.c_str(), prefix.size()) == 0) {
            return argv[i] + prefix.size();
        }
    }
    return fallback;
}

}  // namespace
}  // namespace gt

int main(int argc, char** argv) {
    using namespace gt;
    LogTag() = "server";

    const std::string listen_addr  = Arg(argc, argv, "--listen",  "0.0.0.0:50051");
    const std::string forward_addr = Arg(argc, argv, "--forward", "127.0.0.1:50052");
    const std::string target       = Arg(argc, argv, "--target",  "edge-1");
    const int auto_period          = std::atoi(Arg(argc, argv, "--auto", "0"));

    std::string fwd_host;
    int fwd_port = 0;
    if (!SplitHostPort(forward_addr, &fwd_host, &fwd_port)) {
        LOG("--forward must be host:port, got '%s'", forward_addr.c_str());
        return 1;
    }

    Registry registry;

    int listen_fd = ListenTcp(fwd_host, fwd_port);
    if (listen_fd < 0) return 1;
    std::thread(ForwarderLoop, listen_fd, &registry, target).detach();
    LOG("forwarder listening on %s -> tunnel target '%s'", forward_addr.c_str(),
        target.c_str());

    if (auto_period > 0) {
        std::thread(AutoCallLoop, &registry, target, forward_addr, auto_period)
            .detach();
        LOG("auto-rpc every %ds once '%s' registers", auto_period,
            target.c_str());
    }

    TunnelServiceImpl service(&registry);
    grpc::ServerBuilder builder;
    builder.AddListeningPort(listen_addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    if (!server) {
        LOG("failed to bind %s", listen_addr.c_str());
        return 1;
    }
    LOG("tunnel service listening on %s (host %s)", listen_addr.c_str(),
        Hostname().c_str());
    server->Wait();
    return 0;
}
