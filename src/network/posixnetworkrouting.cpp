/**
 * @file      posixnetworkrouting.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/platform.h>

#if defined(PROMEKI_PLATFORM_POSIX)

#include <promeki/networkrouting.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/list.h>
#include <promeki/logger.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/socket.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

#if defined(PROMEKI_PLATFORM_LINUX)
        struct RouteV4 {
                Ipv4Address dest;
                Ipv4Address mask;
                Ipv4Address gateway;
                String      iface;
                uint32_t    metric = 0;
        };

        struct RouteV6 {
                Ipv6Address dest;
                int         prefixLen = 0;
                Ipv6Address nextHop;
                String      iface;
                uint32_t    metric = 0;
        };

        // Convert the kernel's hex-text little-endian IPv4 to host
        // dotted-quad order.  /proc/net/route stores
        // "<lsb><..><..><msb>" in 8-character hex form.
        Ipv4Address parseProcIpv4(const char *hex) {
                if (hex == nullptr || std::strlen(hex) < 8) return Ipv4Address();
                char    buf[9];
                std::memcpy(buf, hex, 8);
                buf[8]      = '\0';
                uint32_t le = static_cast<uint32_t>(std::strtoul(buf, nullptr, 16));
                // The hex-text is little-endian: byte 0 is octet 1.
                uint8_t b0 = static_cast<uint8_t>((le >> 0) & 0xFF);
                uint8_t b1 = static_cast<uint8_t>((le >> 8) & 0xFF);
                uint8_t b2 = static_cast<uint8_t>((le >> 16) & 0xFF);
                uint8_t b3 = static_cast<uint8_t>((le >> 24) & 0xFF);
                return Ipv4Address(b0, b1, b2, b3);
        }

        // Decodes "20010db8...." (32 hex chars, no separators) into
        // an Ipv6Address.  Big-endian, byte-by-byte — same order the
        // address itself stores.
        Ipv6Address parseProcIpv6(const char *hex) {
                if (hex == nullptr || std::strlen(hex) < 32) return Ipv6Address();
                Ipv6Address::DataFormat raw{};
                for (int i = 0; i < 16; ++i) {
                        char b[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
                        raw[i] = static_cast<uint8_t>(std::strtoul(b, nullptr, 16));
                }
                return Ipv6Address(raw);
        }

        List<RouteV4> readProcRouteV4() {
                List<RouteV4> out;
                std::FILE    *fp = std::fopen("/proc/net/route", "r");
                if (fp == nullptr) return out;
                char buf[512];
                bool firstLine = true;
                while (std::fgets(buf, sizeof(buf), fp) != nullptr) {
                        if (firstLine) {
                                firstLine = false;
                                continue;
                        }
                        char     iface[64] = {0};
                        char     dest[16]  = {0};
                        char     gw[16]    = {0};
                        unsigned flags     = 0;
                        unsigned refcnt    = 0;
                        unsigned use       = 0;
                        unsigned metric    = 0;
                        char     mask[16]  = {0};
                        // /proc/net/route columns: iface dest gw flags
                        // refcnt use metric mask mtu window irtt.
                        if (std::sscanf(buf, "%63s %15s %15s %x %u %u %u %15s",
                                        iface, dest, gw, &flags, &refcnt, &use, &metric, mask) >= 8) {
                                RouteV4 r;
                                r.iface   = String(iface);
                                r.dest    = parseProcIpv4(dest);
                                r.gateway = parseProcIpv4(gw);
                                r.mask    = parseProcIpv4(mask);
                                r.metric  = metric;
                                out.pushToBack(std::move(r));
                        }
                }
                std::fclose(fp);
                return out;
        }

        List<RouteV6> readProcRouteV6() {
                List<RouteV6> out;
                std::FILE    *fp = std::fopen("/proc/net/ipv6_route", "r");
                if (fp == nullptr) return out;
                char buf[512];
                while (std::fgets(buf, sizeof(buf), fp) != nullptr) {
                        char     dest[64]    = {0};
                        unsigned destPlen    = 0;
                        char     src[64]     = {0};
                        unsigned srcPlen     = 0;
                        char     nextHop[64] = {0};
                        unsigned metric      = 0;
                        unsigned refcnt      = 0;
                        unsigned use         = 0;
                        unsigned flags       = 0;
                        char     iface[64]   = {0};
                        if (std::sscanf(buf, "%63s %x %63s %x %63s %x %x %x %x %63s",
                                        dest, &destPlen, src, &srcPlen, nextHop, &metric, &refcnt, &use, &flags,
                                        iface) >= 10) {
                                RouteV6 r;
                                r.dest      = parseProcIpv6(dest);
                                r.prefixLen = static_cast<int>(destPlen);
                                r.nextHop   = parseProcIpv6(nextHop);
                                r.iface     = String(iface);
                                r.metric    = metric;
                                out.pushToBack(std::move(r));
                        }
                }
                std::fclose(fp);
                return out;
        }
#endif // PROMEKI_PLATFORM_LINUX

        class PosixNetworkRoutingBackend : public NetworkRouting::Backend {
                public:
                        NetworkAddress defaultGatewayIpv4() const override {
#if defined(PROMEKI_PLATFORM_LINUX)
                                auto routes = readProcRouteV4();
                                NetworkAddress best;
                                uint32_t       bestMetric = UINT32_MAX;
                                for (const auto &r : routes) {
                                        if (!r.dest.isNull() || !r.mask.isNull()) continue; // dest=0, mask=0 → default
                                        if (r.metric > bestMetric) continue;
                                        bestMetric = r.metric;
                                        best       = NetworkAddress(r.gateway);
                                }
                                return best;
#else
                                return NetworkAddress();
#endif
                        }

                        NetworkAddress defaultGatewayIpv6() const override {
#if defined(PROMEKI_PLATFORM_LINUX)
                                auto routes = readProcRouteV6();
                                NetworkAddress best;
                                uint32_t       bestMetric = UINT32_MAX;
                                for (const auto &r : routes) {
                                        if (r.prefixLen != 0) continue;
                                        if (r.dest != Ipv6Address()) continue;
                                        if (r.nextHop.isNull()) continue; // local-loopback default
                                        if (r.metric > bestMetric) continue;
                                        bestMetric = r.metric;
                                        best       = NetworkAddress(r.nextHop);
                                }
                                return best;
#else
                                return NetworkAddress();
#endif
                        }

                        NetworkInterface defaultRouteInterfaceIpv4() const override {
#if defined(PROMEKI_PLATFORM_LINUX)
                                auto     routes     = readProcRouteV4();
                                uint32_t bestMetric = UINT32_MAX;
                                String   bestIface;
                                for (const auto &r : routes) {
                                        if (!r.dest.isNull() || !r.mask.isNull()) continue;
                                        if (r.metric > bestMetric) continue;
                                        bestMetric = r.metric;
                                        bestIface  = r.iface;
                                }
                                return bestIface.isEmpty() ? NetworkInterface() : NetworkInterface::findByName(bestIface);
#else
                                return NetworkInterface();
#endif
                        }

                        NetworkInterface defaultRouteInterfaceIpv6() const override {
#if defined(PROMEKI_PLATFORM_LINUX)
                                auto     routes     = readProcRouteV6();
                                uint32_t bestMetric = UINT32_MAX;
                                String   bestIface;
                                for (const auto &r : routes) {
                                        if (r.prefixLen != 0) continue;
                                        if (r.dest != Ipv6Address()) continue;
                                        if (r.metric > bestMetric) continue;
                                        bestMetric = r.metric;
                                        bestIface  = r.iface;
                                }
                                return bestIface.isEmpty() ? NetworkInterface()
                                                           : NetworkInterface::findByName(bestIface);
#else
                                return NetworkInterface();
#endif
                        }

                        List<NetworkRouting::Route> routesForIpv4(const Ipv4Address &dest) const override {
#if defined(PROMEKI_PLATFORM_LINUX)
                                List<NetworkRouting::Route> out;
                                if (dest.isNull()) return out;
                                auto rows = readProcRouteV4();
                                for (const auto &r : rows) {
                                        Ipv4Subnet subnet(r.dest, r.mask);
                                        if (subnet.prefixLen() < 0) continue; // non-contiguous mask
                                        // Default route (0.0.0.0/0) always
                                        // covers; otherwise the dest must
                                        // fall inside the subnet.
                                        if (subnet.prefixLen() != 0 && !subnet.contains(dest)) continue;
                                        NetworkRouting::Route entry;
                                        entry.destination = NetworkAddress(r.dest);
                                        entry.prefixLen   = subnet.prefixLen();
                                        entry.gateway     = r.gateway.isNull() ? NetworkAddress()
                                                                               : NetworkAddress(r.gateway);
                                        entry.iface       = r.iface.isEmpty() ? NetworkInterface()
                                                                              : NetworkInterface::findByName(r.iface);
                                        entry.metric      = r.metric;
                                        out.pushToBack(std::move(entry));
                                }
                                // Sort by metric ascending — the first entry
                                // is the path the kernel actually picks.
                                for (size_t i = 1; i < out.size(); ++i) {
                                        for (size_t j = i; j > 0 && out[j - 1].metric > out[j].metric; --j) {
                                                NetworkRouting::Route tmp = out[j - 1];
                                                out[j - 1]                = out[j];
                                                out[j]                    = tmp;
                                        }
                                }
                                return out;
#else
                                (void) dest;
                                return List<NetworkRouting::Route>();
#endif
                        }

                        List<NetworkRouting::Route> routesForIpv6(const Ipv6Address &dest) const override {
#if defined(PROMEKI_PLATFORM_LINUX)
                                List<NetworkRouting::Route> out;
                                if (dest.isNull()) return out;
                                auto rows = readProcRouteV6();
                                for (const auto &r : rows) {
                                        // Default route (::/0) always covers; otherwise
                                        // dest must fall inside the prefix.
                                        if (r.prefixLen != 0) {
                                                Ipv6Subnet subnet(r.dest, r.prefixLen);
                                                if (!subnet.contains(dest)) continue;
                                        }
                                        NetworkRouting::Route entry;
                                        entry.destination = NetworkAddress(r.dest);
                                        entry.prefixLen   = r.prefixLen;
                                        entry.gateway     = r.nextHop.isNull() ? NetworkAddress()
                                                                               : NetworkAddress(r.nextHop);
                                        entry.iface       = r.iface.isEmpty() ? NetworkInterface()
                                                                              : NetworkInterface::findByName(r.iface);
                                        entry.metric      = r.metric;
                                        out.pushToBack(std::move(entry));
                                }
                                for (size_t i = 1; i < out.size(); ++i) {
                                        for (size_t j = i; j > 0 && out[j - 1].metric > out[j].metric; --j) {
                                                NetworkRouting::Route tmp = out[j - 1];
                                                out[j - 1]                = out[j];
                                                out[j]                    = tmp;
                                        }
                                }
                                return out;
#else
                                (void) dest;
                                return List<NetworkRouting::Route>();
#endif
                        }

                        List<NetworkAddress> dnsServers() const override {
                                List<NetworkAddress> out;
                                std::FILE           *fp = std::fopen("/etc/resolv.conf", "r");
                                if (fp == nullptr) return out;
                                char buf[512];
                                while (std::fgets(buf, sizeof(buf), fp) != nullptr) {
                                        // Strip leading whitespace.
                                        char *p = buf;
                                        while (*p == ' ' || *p == '\t') ++p;
                                        if (*p == '#' || *p == ';' || *p == '\n' || *p == '\0') continue;
                                        // Look for "nameserver <addr>".
                                        const char prefix[] = "nameserver";
                                        if (std::strncmp(p, prefix, sizeof(prefix) - 1) != 0) continue;
                                        p += sizeof(prefix) - 1;
                                        if (*p != ' ' && *p != '\t') continue;
                                        while (*p == ' ' || *p == '\t') ++p;
                                        // Trim trailing whitespace / newline.
                                        char *end = p + std::strlen(p);
                                        while (end > p && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' ||
                                                           end[-1] == '\t')) {
                                                --end;
                                                *end = '\0';
                                        }
                                        if (*p == '\0') continue;
                                        // Many systems append the iface scope as
                                        // "fe80::1%eth0"; chop it for the parser.
                                        if (char *pct = std::strchr(p, '%'); pct != nullptr) *pct = '\0';
                                        auto [addr, err] = NetworkAddress::fromString(String(p));
                                        if (err.isError()) continue;
                                        if (!addr.isResolved()) continue;
                                        // Dedup naive — resolv.conf seldom has more
                                        // than 3 entries in production.
                                        bool dup = false;
                                        for (const auto &existing : out) {
                                                if (existing == addr) {
                                                        dup = true;
                                                        break;
                                                }
                                        }
                                        if (!dup) out.pushToBack(addr);
                                }
                                std::fclose(fp);
                                return out;
                        }
        };

        struct Registrar {
                Registrar() { NetworkRouting::setBackend(new PosixNetworkRoutingBackend()); }
        };
        Registrar gPosixRoutingRegistrar;
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_PLATFORM_POSIX
