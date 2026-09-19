// tunnel_client.cc — the side behind NAT, with no inbound port.
//
// Two things live in this process:
//
//   1. Echo            the application gRPC SERVER, bound to a loopback port
//                      that nothing outside the container can reach.
//   2. the dialer      calls Tunnel/Register on the server and parks on the
//                      stream. On OPEN it dials (1) and splices the socket
//                      onto that logical stream.
//
// It is a gRPC client to the tunnel and a gRPC server to the application at
// the same time; the server container is the exact mirror image.
#include <grpcpp/grpcpp.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "echo_service.h"
#include "log.h"
#include "net.h"
#include "session.h"
#include "tunnel.grpc.pb.h"

namespace gt {
namespace {

// Serve the application on loopback. Reachable only through the tunnel, which
// is the point: dial-in without exposing a port.
std::unique_ptr<grpc::Server> StartEchoServer(const std::string& addr,
                                              EchoServiceImpl* service) {
    grpc::ServerBuilder builder;
    builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
    builder.RegisterService(service);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    if (server) LOG("Echo service listening on %s", addr.c_str());
    return server;
}

// NAT and load balancers drop idle flows; the tunnel can be idle for hours.
class Keepalive {
public:
    Keepalive(SessionPtr sess, int period_s)
        : thread_([this, sess = std::move(sess), period_s] {
              std::unique_lock<std::mutex> lock(mu_);
              while (!stop_) {
                  if (cv_.wait_for(lock, std::chrono::seconds(period_s),
                                   [this] { return stop_; })) {
                      return;
                  }
                  Frame f;
                  f.set_type(Frame::KEEPALIVE);
                  if (!sess->Send(f)) return;
              }
          }) {}

    ~Keepalive() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

private:
    std::mutex mu_;
    std::condition_variable cv_;
    bool stop_ = false;
    std::thread thread_;  // declared last: starts once the rest is built
};

// One tunnel attempt: dial, register, serve until the stream breaks.
void RunTunnel(const std::string& tunnel_addr, const std::string& target,
               const std::string& echo_host, int echo_port, int keepalive_s) {
    auto channel = grpc::CreateChannel(tunnel_addr,
                                       grpc::InsecureChannelCredentials());
    auto stub = tunnel::Tunnel::NewStub(channel);

    grpc::ClientContext ctx;
    auto stream = stub->Register(&ctx);

    auto sess = std::make_shared<Session>(
        [&stream](const Frame& f) { return stream->Write(f); });

    Frame reg;
    reg.set_type(Frame::REGISTER);
    reg.set_target(target);
    if (!sess->Send(reg)) {
        LOG("could not register with %s", tunnel_addr.c_str());
        sess->Shutdown();
        return;
    }

    Keepalive keepalive(sess, keepalive_s);

    Frame in;
    while (stream->Read(&in)) {
        switch (in.type()) {
            case Frame::REGISTER_ACK:
                LOG("registered with %s as '%s' — waiting for dial-in",
                    tunnel_addr.c_str(), in.target().c_str());
                break;

            case Frame::OPEN: {
                // Dialled inline so the OPEN is finished before the DATA
                // frames behind it are processed. Localhost, so it is quick.
                const uint64_t id = in.stream_id();
                int fd = DialTcp(echo_host, echo_port);
                if (fd < 0) {
                    LOG("stream %llu: cannot reach local service %s:%d",
                        static_cast<unsigned long long>(id), echo_host.c_str(),
                        echo_port);
                    sess->SendClose(id, "local dial failed");
                    break;
                }
                LOG("stream %llu opened -> %s:%d",
                    static_cast<unsigned long long>(id), echo_host.c_str(),
                    echo_port);
                ConnPtr conn = sess->AddConn(id, fd);
                std::thread(&Session::Pump, sess, id, conn).detach();
                break;
            }

            case Frame::DATA: {
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

            default:
                break;
        }
    }

    grpc::Status st = stream->Finish();
    LOG("tunnel closed: %s", st.ok() ? "server ended the stream"
                                     : st.error_message().c_str());
    sess->Shutdown();
    sess->CloseAll();
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
    LogTag() = "client";

    const std::string tunnel_addr = Arg(argc, argv, "--tunnel", "tunnel-server:50051");
    const std::string target      = Arg(argc, argv, "--target", "edge-1");
    const std::string echo_addr   = Arg(argc, argv, "--echo",   "127.0.0.1:50060");
    const int retry_s             = std::atoi(Arg(argc, argv, "--retry", "3"));
    const int keepalive_s         = std::atoi(Arg(argc, argv, "--keepalive", "20"));

    std::string echo_host;
    int echo_port = 0;
    if (!SplitHostPort(echo_addr, &echo_host, &echo_port)) {
        LOG("--echo must be host:port, got '%s'", echo_addr.c_str());
        return 1;
    }

    EchoServiceImpl service;
    auto echo_server = StartEchoServer(echo_addr, &service);
    if (!echo_server) {
        LOG("failed to bind %s", echo_addr.c_str());
        return 1;
    }

    LOG("host %s, dialling %s", Hostname().c_str(), tunnel_addr.c_str());

    // Reconnect forever: container start order is not guaranteed, and a
    // tunnel that does not come back is not much of a tunnel.
    for (;;) {
        RunTunnel(tunnel_addr, target, echo_host, echo_port, keepalive_s);
        LOG("reconnecting in %ds…", retry_s);
        std::this_thread::sleep_for(std::chrono::seconds(retry_s));
    }
}
