/**
 * @file      dnsconfig.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/dnsconfig.h>

#include <promeki/file.h>
#include <promeki/ipv4address.h>
#include <promeki/ipv6address.h>
#include <promeki/logger.h>
#include <promeki/platform.h>

#if defined(PROMEKI_PLATFORM_WINDOWS)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#endif

PROMEKI_NAMESPACE_BEGIN

const char *DnsConfig::DefaultFallbackServerCstr = "127.0.0.53";

namespace {

        // Strips leading whitespace + comment markers and returns the
        // trimmed text view of one resolv.conf line.
        String trimResolvLine(const String &raw) {
                const char  *s = raw.cstr();
                const size_t n = (s != nullptr) ? raw.size() : 0;
                size_t       a = 0;
                size_t       b = n;
                while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
                if (a < b && (s[a] == '#' || s[a] == ';')) return String();
                while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' ||
                                 s[b - 1] == '\r' || s[b - 1] == '\n')) {
                        --b;
                }
                if (a == 0 && b == n) return raw;
                if (b <= a) return String();
                return String(s + a, b - a);
        }

        // Splits one whitespace-separated resolv.conf line into tokens.
        StringList splitTokens(const String &line) {
                StringList out;
                const char  *s = line.cstr();
                const size_t n = (s != nullptr) ? line.size() : 0;
                size_t       i = 0;
                while (i < n) {
                        while (i < n && (s[i] == ' ' || s[i] == '\t')) ++i;
                        size_t start = i;
                        while (i < n && s[i] != ' ' && s[i] != '\t') ++i;
                        if (i > start) out += String(s + start, i - start);
                }
                return out;
        }

        // Try-IPv4-then-IPv6 string parse.  Returns isNull() when
        // both fail.
        NetworkAddress parseIpOnly(const String &s) {
                auto v4 = Ipv4Address::fromString(s);
                if (v4.second().isOk()) return NetworkAddress(v4.first());
                auto v6 = Ipv6Address::fromString(s);
                if (v6.second().isOk()) return NetworkAddress(v6.first());
                return NetworkAddress();
        }

} // anonymous namespace

DnsConfig DnsConfig::fromResolvConfText(const String &text) {
        DnsConfig cfg;

        const char  *s = text.cstr();
        const size_t n = (s != nullptr) ? text.size() : 0;
        size_t       i = 0;
        while (i < n) {
                // Find end of line.
                size_t j = i;
                while (j < n && s[j] != '\n') ++j;
                String line = String(s + i, j - i);
                i           = (j < n) ? j + 1 : j;

                line = trimResolvLine(line);
                if (line.isEmpty()) continue;

                StringList tok = splitTokens(line);
                if (tok.isEmpty()) continue;

                const String &kw = tok[0];
                if (kw == String("nameserver") && tok.size() >= 2) {
                        NetworkAddress addr = parseIpOnly(tok[1]);
                        if (!addr.isNull()) {
                                cfg._nameservers += SocketAddress(addr, DefaultPort);
                        } else {
                                promekiWarn("DnsConfig: ignoring unparseable nameserver '%s'",
                                            tok[1].cstr());
                        }
                        continue;
                }
                if (kw == String("search")) {
                        // The latest 'search' directive wins (replaces
                        // any prior 'search' / 'domain') per
                        // resolv.conf(5).
                        StringList ds;
                        for (size_t k = 1; k < tok.size(); ++k) ds += tok[k];
                        cfg._searchDomains = ds;
                        continue;
                }
                if (kw == String("domain") && tok.size() >= 2) {
                        StringList ds;
                        ds += tok[1];
                        cfg._searchDomains = ds;
                        continue;
                }
                if (kw == String("options")) {
                        for (size_t k = 1; k < tok.size(); ++k) {
                                const String &opt = tok[k];
                                if (opt.size() > 6 && opt.startsWith(String("ndots:"))) {
                                        cfg._ndots = std::atoi(opt.cstr() + 6);
                                } else if (opt.size() > 8 && opt.startsWith(String("timeout:"))) {
                                        int sec = std::atoi(opt.cstr() + 8);
                                        if (sec > 0) cfg._timeout = Duration::fromSeconds(sec);
                                } else if (opt.size() > 9 && opt.startsWith(String("attempts:"))) {
                                        int a = std::atoi(opt.cstr() + 9);
                                        if (a > 0) cfg._attempts = a;
                                }
                        }
                        continue;
                }
                // Unknown directive — quietly ignored.  resolv.conf
                // can carry many vendor-specific knobs (rotate, edns0,
                // single-request, ...) we don't model.
        }

        return cfg;
}

Result<DnsConfig> DnsConfig::loadSystem() {
#if defined(PROMEKI_PLATFORM_EMSCRIPTEN)
        // Browser sandboxes have no resolver to talk to from
        // userspace; the @ref DnsResolver class is not built into
        // Emscripten so this path is unreachable in practice, but
        // we still surface a useful error for tests that mock it.
        return makeError<DnsConfig>(Error::NotSupported);
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        DnsConfig cfg;
        ULONG     bufLen = 16 * 1024;
        List<uint8_t> buf;
        buf.resize(bufLen);
        DWORD rc = ::GetAdaptersAddresses(AF_UNSPEC,
                                          GAA_FLAG_INCLUDE_PREFIX |
                                          GAA_FLAG_SKIP_ANYCAST   |
                                          GAA_FLAG_SKIP_MULTICAST |
                                          GAA_FLAG_SKIP_FRIENDLY_NAME,
                                          nullptr,
                                          reinterpret_cast<PIP_ADAPTER_ADDRESSES>(&buf[0]),
                                          &bufLen);
        if (rc == ERROR_BUFFER_OVERFLOW) {
                buf.resize(bufLen);
                rc = ::GetAdaptersAddresses(AF_UNSPEC, 0, nullptr,
                                            reinterpret_cast<PIP_ADAPTER_ADDRESSES>(&buf[0]),
                                            &bufLen);
        }
        if (rc != NO_ERROR) {
                return makeError<DnsConfig>(Error::syserr(rc));
        }
        auto *adap = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(&buf[0]);
        while (adap != nullptr) {
                for (auto *dns = adap->FirstDnsServerAddress; dns != nullptr; dns = dns->Next) {
                        const struct sockaddr *sa  = dns->Address.lpSockaddr;
                        const int              len = dns->Address.iSockaddrLength;
                        auto na = NetworkAddress::fromSockAddr(sa, static_cast<size_t>(len));
                        if (na.second().isOk() && na.first().isResolved()) {
                                cfg._nameservers += SocketAddress(na.first(), DefaultPort);
                        }
                }
                adap = adap->Next;
        }
        if (cfg._nameservers.isEmpty()) {
                NetworkAddress fb = parseIpOnly(String(DefaultFallbackServerCstr));
                if (!fb.isNull()) cfg._nameservers += SocketAddress(fb, DefaultPort);
        }
        return makeResult(std::move(cfg));
#else
        // POSIX path: read /etc/resolv.conf via the project's File
        // wrapper so the file lookup goes through any installed VFS
        // (cirf paths cannot reach /etc/* but the wrapper is the
        // standard way to read text in this project).
        File f("/etc/resolv.conf");
        Error err = f.open(IODevice::ReadOnly);
        DnsConfig cfg;
        if (err.isError()) {
                // Hosts without /etc/resolv.conf — e.g. NSS-only
                // boxes, containers with no networking — still need
                // a usable resolver for the loopback systemd-resolved
                // stub.  Fall back rather than fail outright.
                NetworkAddress fb = parseIpOnly(String(DefaultFallbackServerCstr));
                if (!fb.isNull()) cfg._nameservers += SocketAddress(fb, DefaultPort);
                return makeResult(std::move(cfg));
        }
        auto          sz   = f.size();
        const int64_t size = sz.second().isOk() ? sz.first() : int64_t{0};
        String        text;
        if (size > 0) {
                List<char> buf;
                buf.resize(static_cast<size_t>(size));
                int64_t got = f.read(&buf[0], size);
                if (got > 0) text = String(&buf[0], static_cast<size_t>(got));
        }
        f.close();
        cfg = fromResolvConfText(text);
        if (cfg._nameservers.isEmpty()) {
                NetworkAddress fb = parseIpOnly(String(DefaultFallbackServerCstr));
                if (!fb.isNull()) cfg._nameservers += SocketAddress(fb, DefaultPort);
        }
        return makeResult(std::move(cfg));
#endif
}

PROMEKI_NAMESPACE_END
