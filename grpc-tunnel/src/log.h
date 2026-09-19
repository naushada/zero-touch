// log.h — timestamped stderr logging. Container logs are the demo's UI, so
// both sides print the same shape and the two halves read as one transcript.
#pragma once

#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>

namespace gt {

inline std::mutex& LogMutex() {
    static std::mutex m;
    return m;
}

inline std::string Now() {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    tm tmv{};
    localtime_r(&ts.tv_sec, &tmv);
    char buf[32];
    size_t n = strftime(buf, sizeof(buf), "%H:%M:%S", &tmv);
    snprintf(buf + n, sizeof(buf) - n, ".%03ld", ts.tv_nsec / 1000000);
    return buf;
}

// Component tag, set once at startup so every line says who is talking.
inline std::string& LogTag() {
    static std::string tag = "gt";
    return tag;
}

}  // namespace gt

#define LOG(...)                                                    \
    do {                                                            \
        std::lock_guard<std::mutex> _l(::gt::LogMutex());           \
        fprintf(stderr, "%s [%s] ", ::gt::Now().c_str(),            \
                ::gt::LogTag().c_str());                            \
        fprintf(stderr, __VA_ARGS__);                               \
        fputc('\n', stderr);                                        \
        fflush(stderr);                                             \
    } while (0)
