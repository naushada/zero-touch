// net.h — the small amount of BSD sockets the tunnel needs.
//
// The tunnel splices TCP connections, so it deals in file descriptors rather
// than in gRPC objects: one accepted/dialled socket per logical stream.
#pragma once

#include <cstddef>
#include <string>

namespace gt {

// Listening socket on host:port, SO_REUSEADDR, backlog 128. -1 on failure.
int ListenTcp(const std::string& host, int port);

// Blocking connect, resolving `host` through getaddrinfo so container DNS
// names work. -1 on failure.
int DialTcp(const std::string& host, int port);

// Write the whole buffer, retrying short writes. False if the peer went away.
bool WriteAll(int fd, const char* buf, size_t len);

// Split "host:port". False if `addr` has no port.
bool SplitHostPort(const std::string& addr, std::string* host, int* port);

// Nagle off: a tunnel forwards many small HTTP/2 frames and must not sit on
// them waiting for more.
void SetNoDelay(int fd);

std::string Hostname();

}  // namespace gt
