#include "net.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "log.h"

namespace gt {

void SetNoDelay(int fd) {
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

bool SplitHostPort(const std::string& addr, std::string* host, int* port) {
    auto pos = addr.rfind(':');
    if (pos == std::string::npos || pos == 0 || pos + 1 >= addr.size()) return false;
    *host = addr.substr(0, pos);
    *port = std::atoi(addr.c_str() + pos + 1);
    return *port > 0;
}

int ListenTcp(const std::string& host, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(static_cast<uint16_t>(port));
    if (host.empty() || host == "0.0.0.0" || host == "*") {
        sa.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, host.c_str(), &sa.sin_addr) != 1) {
        LOG("listen: cannot parse address %s", host.c_str());
        close(fd);
        return -1;
    }

    if (bind(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0 ||
        listen(fd, 128) != 0) {
        LOG("listen %s:%d failed: %s", host.c_str(), port, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

int DialTcp(const std::string& host, int port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    const std::string service = std::to_string(port);
    addrinfo* res = nullptr;
    int rc = getaddrinfo(host.c_str(), service.c_str(), &hints, &res);
    if (rc != 0) {
        LOG("resolve %s failed: %s", host.c_str(), gai_strerror(rc));
        return -1;
    }

    int fd = -1;
    for (addrinfo* ai = res; ai != nullptr; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd >= 0) SetNoDelay(fd);
    return fd;
}

bool WriteAll(int fd, const char* buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, buf + off, len - off, MSG_NOSIGNAL);
        if (n > 0) {
            off += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        return false;
    }
    return true;
}

std::string Hostname() {
    char buf[256];
    if (gethostname(buf, sizeof(buf)) != 0) return "unknown";
    buf[sizeof(buf) - 1] = '\0';
    return buf;
}

}  // namespace gt
