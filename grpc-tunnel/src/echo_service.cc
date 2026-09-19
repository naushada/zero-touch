#include "echo_service.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <thread>

#include "log.h"
#include "net.h"

namespace gt {

grpc::Status EchoServiceImpl::Say(grpc::ServerContext* ctx,
                                  const echo::SayRequest* req,
                                  echo::SayReply* res) {
    LOG("Echo.Say from %s: '%s'", ctx->peer().c_str(), req->message().c_str());
    res->set_message("echo: " + req->message());
    res->set_served_by(Hostname());
    res->set_seq(1);
    return grpc::Status::OK;
}

grpc::Status EchoServiceImpl::SayStream(
    grpc::ServerContext* ctx, const echo::SayRequest* req,
    grpc::ServerWriter<echo::SayReply>* writer) {
    const uint32_t count = req->count() == 0 ? 3 : req->count();
    LOG("Echo.SayStream from %s: '%s' x%u", ctx->peer().c_str(),
        req->message().c_str(), count);

    for (uint32_t i = 1; i <= count; ++i) {
        if (ctx->IsCancelled()) return grpc::Status::CANCELLED;

        echo::SayReply res;
        res.set_message("echo: " + req->message());
        res.set_served_by(Hostname());
        res.set_seq(i);
        if (!writer->Write(res)) break;

        // Spread the messages out so the tunnel is visibly carrying a live
        // stream rather than one buffered burst.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return grpc::Status::OK;
}

}  // namespace gt
