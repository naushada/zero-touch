#ifndef __zerotouch_gnmi_sink_hpp__
#define __zerotouch_gnmi_sink_hpp__

#include <string>
#include <utility>
#include <vector>

/**
 * @file gnmi_sink.hpp
 * @brief The gNMI-backend seam: turn parsed GET/SET operations into gNMI RPCs.
 *
 * The gnmi executor only ever sees GnmiSink, so the whole command layer is
 * host-testable against MockGnmiSink — no gRPC, no event loop, no device. The
 * production model is LocalGnmiSink, which drives grace-server's gnmi_client
 * against the device-local gNMI server (127.0.0.1). See DESIGN.md.
 */

namespace zerotouch {

/// Result for a single path within a GET/SET operation.
struct GnmiPathResult {
    std::string xpath;   ///< the YANG instance-identifier this row is for
    std::string value;   ///< GET: decoded value; SET: echoed/empty on success
    std::string error;   ///< per-path error ("" on success; e.g. path denied)
};

/// Outcome of one GnmiSink call. `ok` is true only when the RPC succeeded AND
/// no path carried an error.
struct GnmiResult {
    bool                        ok = false;
    int                         grpc_status = -1;   ///< 0 = OK, -1 = transport error
    std::string                 grpc_message;       ///< grpc-message trailer, may be empty
    std::vector<GnmiPathResult> paths;
};

/// Backend for the two gnmi operations. Implementations must never return a
/// value for a denylisted (sensitive) path — see LocalGnmiSink.
struct GnmiSink {
    virtual ~GnmiSink() = default;

    /// gNMI Get for one or more xpaths.
    virtual GnmiResult get(const std::vector<std::string>& xpaths) = 0;

    /// gNMI Set: each (xpath, value) becomes an update. Positional pairing is
    /// the caller's responsibility (parser guarantees equal counts).
    virtual GnmiResult set(
        const std::vector<std::pair<std::string, std::string>>& updates) = 0;
};

} // namespace zerotouch

#endif /* __zerotouch_gnmi_sink_hpp__ */
