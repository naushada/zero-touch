#include "echo_call.h"

#include <grpcpp/grpcpp.h>

#include <chrono>

#include "log.h"

namespace gt {

EchoCaller::EchoCaller(const std::string& addr)
    : addr_(addr),
      channel_(grpc::CreateChannel(addr, grpc::InsecureChannelCredentials())),
      stub_(echo::Echo::NewStub(channel_)) {}

namespace {
// Every byte of this RPC crosses the tunnel twice (there and back), so allow
// more than a LAN would need before calling it dead.
void SetDeadline(grpc::ClientContext* ctx) {
    ctx->set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(15));
}
}  // namespace

bool EchoCaller::Say(const std::string& message, std::string* reply,
                     std::string* error) {
    echo::SayRequest req;
    req.set_message(message);

    echo::SayReply res;
    grpc::ClientContext ctx;
    SetDeadline(&ctx);

    grpc::Status st = stub_->Say(&ctx, req, &res);
    if (!st.ok()) {
        *error = st.error_message();
        return false;
    }
    *reply = res.message() + "  (served by " + res.served_by() + ")";
    return true;
}

bool EchoCaller::SayStream(const std::string& message, uint32_t count,
                           std::string* error) {
    echo::SayRequest req;
    req.set_message(message);
    req.set_count(count);

    grpc::ClientContext ctx;
    SetDeadline(&ctx);

    auto reader = stub_->SayStream(&ctx, req);
    echo::SayReply res;
    while (reader->Read(&res)) {
        LOG("  stream[%llu] %s  (served by %s)",
            static_cast<unsigned long long>(res.seq()), res.message().c_str(),
            res.served_by().c_str());
    }

    grpc::Status st = reader->Finish();
    if (!st.ok()) {
        *error = st.error_message();
        return false;
    }
    return true;
}

}  // namespace gt
