#include "zerotouch/at_modem.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include <ace/Log_Msg.h>
#include <ace/Reactor.h>

#include "sms_pdu.hpp"   // cellular::encode_sms_submit / decode_sms_deliver

/**
 * @file at_modem.cpp
 * @brief Generic AT modem driver. Built when ZT_BUILD_STANDALONE is ON; validated
 *        on hardware. See at_modem.hpp.
 */

namespace zerotouch {

namespace {

std::uint64_t now_ms() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

speed_t baud_const(std::uint32_t baud) {
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
#ifdef B230400
        case 230400: return B230400;
#endif
#ifdef B460800
        case 460800: return B460800;
#endif
#ifdef B921600
        case 921600: return B921600;
#endif
        default:     return B115200;
    }
}

bool is_error(const std::string& l) {
    return l == "ERROR" || l.rfind("+CME ERROR", 0) == 0 || l.rfind("+CMS ERROR", 0) == 0;
}

} // namespace

AtModem::~AtModem() {
    if (m_fd >= 0) ::close(m_fd);
}

bool AtModem::open_port() {
    m_fd = ::open(m_cfg.dev.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (m_fd < 0) {
        ACE_ERROR((LM_ERROR, ACE_TEXT("%D [zerotouch] open(%C) failed: %C\n"),
                   m_cfg.dev.c_str(), std::strerror(errno)));
        return false;
    }
    termios t{};
    if (::tcgetattr(m_fd, &t) != 0) return false;
    cfmakeraw(&t);
    const speed_t b = baud_const(m_cfg.baud);
    cfsetispeed(&t, b);
    cfsetospeed(&t, b);
    t.c_cflag |= (CLOCAL | CREAD);
    t.c_cflag &= ~CRTSCTS;
    t.c_cc[VMIN]  = 0;
    t.c_cc[VTIME] = 0;   // non-blocking; we drive timing with select()
    return ::tcsetattr(m_fd, TCSANOW, &t) == 0;
}

bool AtModem::write_all(const std::string& s) {
    std::size_t off = 0;
    while (off < s.size()) {
        const ssize_t n = ::write(m_fd, s.data() + off, s.size() - off);
        if (n < 0) {
            if (errno == EAGAIN) continue;
            return false;
        }
        off += static_cast<std::size_t>(n);
    }
    return true;
}

AtResult AtModem::run_at(const std::string& cmd, int timeout_ms, bool want_prompt) {
    AtResult res;
    if (m_fd < 0) return res;
    if (!cmd.empty() && !write_all(cmd + "\r")) return res;

    const std::uint64_t deadline = now_ms() + static_cast<std::uint64_t>(timeout_ms);
    char buf[512];
    while (now_ms() < deadline) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(m_fd, &rd);
        timeval tv{0, 100 * 1000};   // 100 ms slices
        const int rc = ::select(m_fd + 1, &rd, nullptr, nullptr, &tv);
        if (rc <= 0) continue;

        const ssize_t n = ::read(m_fd, buf, sizeof(buf));
        if (n <= 0) continue;
        m_rx.append(buf, static_cast<std::size_t>(n));

        if (want_prompt && m_rx.find('>') != std::string::npos) {
            res.ok = true;   // ready for the PDU payload
            m_rx.clear();
            return res;
        }

        std::size_t nl;
        while ((nl = m_rx.find('\n')) != std::string::npos) {
            std::string line = m_rx.substr(0, nl);
            m_rx.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line == cmd) continue;   // blank / command echo
            if (line == "OK") { res.ok = true; return res; }
            if (is_error(line)) { res.lines.push_back(line); return res; }
            res.lines.push_back(line);
        }
    }
    return res;   // timeout → ok=false
}

AtResult AtModem::at(const std::string& cmd) { return run_at(cmd, 3000); }

bool AtModem::send_sms(const std::string& to, const std::string& text) {
    std::string pdu;
    int tpdu_len = 0;
    if (!cellular::encode_sms_submit(to, text, pdu, tpdu_len)) return false;

    // AT+CMGS=<tpdu_len> → ">" prompt → <pdu><Ctrl-Z>.
    if (!run_at("AT+CMGS=" + std::to_string(tpdu_len), 5000, /*want_prompt=*/true).ok)
        return false;
    const std::string payload = pdu + "\x1A";   // Ctrl-Z submits
    if (!write_all(payload)) return false;
    return run_at("", 60000).ok;   // wait for +CMGS: / OK (send can be slow)
}

void AtModem::configure() {
    run_at("ATE0", 1000);              // no echo
    run_at("AT+CMEE=1", 1000);         // numeric error codes
    run_at("AT+CMGF=0", 1000);         // PDU mode
    run_at("AT+CNMI=0,0,0,0,0", 1000); // no URCs — we poll storage
    run_at("AT+CPMS=\"ME\",\"ME\",\"ME\"", 1000);
}

void AtModem::start() {
    if (!open_port()) return;
    configure();
    drain_sms();   // clear anything already stored (also baselines)
    ACE_Reactor::instance()->schedule_timer(
        this, nullptr, ACE_Time_Value(m_cfg.poll_sec), ACE_Time_Value(m_cfg.poll_sec));
    ACE_DEBUG((LM_INFO, ACE_TEXT("%D [zerotouch] modem up on %C @ %u, poll %us\n"),
               m_cfg.dev.c_str(), m_cfg.baud, m_cfg.poll_sec));
}

int AtModem::handle_timeout(const ACE_Time_Value&, const void*) {
    drain_sms();
    return 0;
}

void AtModem::drain_sms() {
    // AT+CMGL=4 lists ALL messages in PDU mode as pairs:
    //   +CMGL: <index>,<stat>,,<length>
    //   <pdu-hex>
    const AtResult r = at("AT+CMGL=4");
    if (!r.ok) return;

    std::vector<int> to_delete;
    for (std::size_t i = 0; i + 1 < r.lines.size(); ++i) {
        if (r.lines[i].rfind("+CMGL:", 0) != 0) continue;
        // index is the first field after the colon
        int index = -1;
        try { index = std::stoi(r.lines[i].substr(r.lines[i].find(':') + 1)); }
        catch (const std::exception&) { continue; }

        cellular::SmsMessage msg;
        if (!cellular::decode_sms_deliver(r.lines[i + 1], msg)) { ++i; continue; }
        msg.index = index;
        ++i;   // consumed the PDU line

        cellular::SmsMessage full;
        const bool complete = (msg.total == 0) ? (full = msg, true)
                                               : m_reasm.add(msg, full);
        if (complete && m_on_sms)
            m_on_sms(InboundSms{full.sender, full.text, full.scts});
        to_delete.push_back(index);
    }
    for (int idx : to_delete) at("AT+CMGD=" + std::to_string(idx));
}

} // namespace zerotouch
