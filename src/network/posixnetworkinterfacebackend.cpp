/**
 * @file      posixnetworkinterfacebackend.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/platform.h>

#if defined(PROMEKI_PLATFORM_POSIX)

#include <promeki/networkinterfacebackend.h>
#include <promeki/networkinterfaceimpl.h>
#include <promeki/string.h>
#include <promeki/list.h>
#include <promeki/logger.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(PROMEKI_PLATFORM_LINUX)
#include <linux/if_packet.h>
#include <sys/ioctl.h>
#elif defined(PROMEKI_PLATFORM_BSD)
#include <net/if_dl.h>
#include <net/if_types.h>
#endif

PROMEKI_NAMESPACE_BEGIN

namespace {

        struct InterfaceCollect {
                NetworkInterfaceData data;
                bool                 hasMac = false;
        };

        using CollectList = List<InterfaceCollect>;

        // Pulls a MAC out of an AF_PACKET / AF_LINK ifaddrs entry into @p out.
        // Returns true on success.
        bool extractMac(const struct ifaddrs *ifa, MacAddress &out) {
                if (ifa == nullptr || ifa->ifa_addr == nullptr) return false;
#if defined(PROMEKI_PLATFORM_LINUX)
                if (ifa->ifa_addr->sa_family != AF_PACKET) return false;
                const auto *sll = reinterpret_cast<const struct sockaddr_ll *>(ifa->ifa_addr);
                if (sll->sll_halen != 6) return false;
                out = MacAddress(sll->sll_addr[0], sll->sll_addr[1], sll->sll_addr[2],
                                 sll->sll_addr[3], sll->sll_addr[4], sll->sll_addr[5]);
                return true;
#elif defined(PROMEKI_PLATFORM_BSD)
                if (ifa->ifa_addr->sa_family != AF_LINK) return false;
                const auto *sdl = reinterpret_cast<const struct sockaddr_dl *>(ifa->ifa_addr);
                if (sdl->sdl_alen != 6) return false;
                const uint8_t *p = reinterpret_cast<const uint8_t *>(LLADDR(sdl));
                out = MacAddress(p[0], p[1], p[2], p[3], p[4], p[5]);
                return true;
#else
                (void) ifa;
                (void) out;
                return false;
#endif
        }

        // Counts the contiguous high-bit run in the bytes pointed to
        // by @p bytes (length @p len).  Used to derive the IPv6 prefix
        // length from a getifaddrs sockaddr_in6 netmask.  Returns -1
        // for a non-contiguous mask, which IPv6 should never see in
        // practice but the guard keeps the helper honest.
        int prefixLenFromMask(const uint8_t *bytes, int len) {
                int  count   = 0;
                bool sawZero = false;
                for (int i = 0; i < len; ++i) {
                        for (int b = 7; b >= 0; --b) {
                                bool set = ((bytes[i] >> b) & 1U) != 0;
                                if (set) {
                                        if (sawZero) return -1;
                                        ++count;
                                } else {
                                        sawZero = true;
                                }
                        }
                }
                return count;
        }

        // Maps interface flags into our snapshot booleans.
        void applyFlags(unsigned int flags, NetworkInterfaceData &d) {
                d.isUp        = (flags & IFF_UP) != 0;
                d.isRunning   = (flags & IFF_RUNNING) != 0;
                d.hasCarrier  = d.isRunning;
                d.isLoopback  = (flags & IFF_LOOPBACK) != 0;
                d.isMulticast = (flags & IFF_MULTICAST) != 0;
        }

        InterfaceCollect &slot(CollectList &v, const String &name) {
                for (auto &c : v) {
                        if (c.data.name == name) return c;
                }
                v.pushToBack(InterfaceCollect{});
                v.back().data.name = name;
                return v.back();
        }

#if defined(PROMEKI_PLATFORM_LINUX)
        // Reads the first non-empty line of @p path, trims trailing
        // newline, returns empty String on error / missing file.
        String readSysfsLine(const String &path) {
                std::FILE *fp = std::fopen(path.cstr(), "r");
                if (fp == nullptr) return String();
                char buf[256];
                buf[0] = '\0';
                if (std::fgets(buf, sizeof(buf), fp) == nullptr) {
                        std::fclose(fp);
                        return String();
                }
                std::fclose(fp);
                size_t len = std::strlen(buf);
                while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || buf[len - 1] == ' ')) {
                        buf[--len] = '\0';
                }
                return String(buf);
        }

        bool sysfsExists(const String &path) {
                struct stat st;
                return ::stat(path.cstr(), &st) == 0;
        }

        // Classifies a Linux interface using sysfs probes.  Order
        // matters: more-specific predicates run before broader ones
        // (e.g. wireless before generic ethernet).
        NetworkInterfaceKind classifyLinuxKind(const String &name, bool isLoopback) {
                if (isLoopback) return NetworkInterfaceKind::Loopback;
                const String base = String("/sys/class/net/") + name + "/";
                if (sysfsExists(base + "wireless")) return NetworkInterfaceKind::Wifi;
                if (sysfsExists(base + "bridge")) return NetworkInterfaceKind::Bridge;
                if (sysfsExists(base + "bonding")) return NetworkInterfaceKind::Virtual;
                // /proc/net/vlan/<name> exists only when the iface is a
                // VLAN device managed by the 8021q module.
                if (sysfsExists(String("/proc/net/vlan/") + name)) return NetworkInterfaceKind::Vlan;
                const String typeStr = readSysfsLine(base + "type");
                if (!typeStr.isEmpty()) {
                        // ARPHRD_* values from <linux/if_arp.h>.  We don't
                        // include the header — the integers are stable
                        // ABI and well documented in kernel man pages.
                        long t = std::strtol(typeStr.cstr(), nullptr, 10);
                        if (t == 768 || t == 769) return NetworkInterfaceKind::Tunnel;  // ARPHRD_TUNNEL / TUNNEL6
                        if (t == 1) return NetworkInterfaceKind::Ethernet;              // ARPHRD_ETHER
                        if (t == 772) return NetworkInterfaceKind::Loopback;            // ARPHRD_LOOPBACK
                }
                return NetworkInterfaceKind::Unknown;
        }

        void readLinuxLinkInfo(const String &name, NetworkInterfaceData &d) {
                const String base = String("/sys/class/net/") + name + "/";
                String       speedStr = readSysfsLine(base + "speed");
                if (!speedStr.isEmpty()) {
                        // sysfs returns "-1" when the link is down or
                        // the speed is unknown — leave linkSpeedMbps
                        // at 0 in that case so callers see a clean
                        // "unknown" sentinel.
                        long mbps = std::strtol(speedStr.cstr(), nullptr, 10);
                        if (mbps > 0) d.linkSpeedMbps = static_cast<uint64_t>(mbps);
                }
                String duplexStr = readSysfsLine(base + "duplex");
                d.fullDuplex     = (duplexStr == "full");
                String carrierStr = readSysfsLine(base + "carrier");
                if (!carrierStr.isEmpty()) {
                        d.hasCarrier = (carrierStr == "1");
                }
        }
#endif

#if defined(PROMEKI_PLATFORM_BSD)
        NetworkInterfaceKind classifyBsdKind(uint8_t ifiType, bool isLoopback) {
                if (isLoopback) return NetworkInterfaceKind::Loopback;
                switch (ifiType) {
                        case IFT_ETHER:   return NetworkInterfaceKind::Ethernet;
                        case IFT_LOOP:    return NetworkInterfaceKind::Loopback;
                        case IFT_TUNNEL:  return NetworkInterfaceKind::Tunnel;
                        case IFT_BRIDGE:  return NetworkInterfaceKind::Bridge;
                        case IFT_PPP:     return NetworkInterfaceKind::PointToPoint;
                        default:          return NetworkInterfaceKind::Unknown;
                }
        }
#endif

        class PosixInterfaceImpl : public NetworkInterfaceImpl {
                public:
                        PROMEKI_SHARED_DERIVED(PosixInterfaceImpl)

                        explicit PosixInterfaceImpl(NetworkInterfaceData data)
                            : NetworkInterfaceImpl(std::move(data)) {}

                        NetworkInterfaceStats stats() const override {
#if defined(PROMEKI_PLATFORM_LINUX)
                                NetworkInterfaceStats out;
                                const String          name = data().name;
                                if (name.isEmpty()) return out;
                                const String base = String("/sys/class/net/") + name + "/statistics/";
                                if (!sysfsExists(base)) return out;
                                struct Field {
                                        const char *file;
                                        uint64_t   *target;
                                };
                                Field fields[] = {
                                        {"rx_bytes", &out.rxBytes},     {"rx_packets", &out.rxPackets},
                                        {"rx_errors", &out.rxErrors},   {"rx_dropped", &out.rxDropped},
                                        {"tx_bytes", &out.txBytes},     {"tx_packets", &out.txPackets},
                                        {"tx_errors", &out.txErrors},   {"tx_dropped", &out.txDropped},
                                };
                                bool anyRead = false;
                                for (const auto &f : fields) {
                                        String s = readSysfsLine(base + f.file);
                                        if (s.isEmpty()) continue;
                                        // strtoull on a non-numeric line yields 0
                                        // and sets errno; treat any successful read
                                        // (incl. legitimate 0) as "valid", so a
                                        // freshly-zeroed counter still flips the
                                        // valid bit.
                                        char       *endp = nullptr;
                                        const char *cp   = s.cstr();
                                        uint64_t    v    = std::strtoull(cp, &endp, 10);
                                        if (endp == cp) continue;
                                        *f.target = v;
                                        anyRead   = true;
                                }
                                out.valid = anyRead;
                                return out;
#else
                                return NetworkInterfaceImpl::stats();
#endif
                        }
        };

        class PosixNetworkInterfaceBackend : public NetworkInterfaceBackend {
                public:
                        String name() const override { return String("posix"); }
                        int    priority() const override { return 100; }

                        ImplList enumerate() const override {
                                ImplList    out;
                                CollectList collected;

                                struct ifaddrs *head = nullptr;
                                if (getifaddrs(&head) != 0 || head == nullptr) {
                                        promekiWarn("PosixNetworkInterfaceBackend: getifaddrs failed");
                                        return out;
                                }
                                for (struct ifaddrs *ifa = head; ifa != nullptr; ifa = ifa->ifa_next) {
                                        if (ifa->ifa_name == nullptr) continue;
                                        InterfaceCollect &c = slot(collected, String(ifa->ifa_name));
                                        applyFlags(ifa->ifa_flags, c.data);
                                        if (c.data.index == 0) c.data.index = if_nametoindex(ifa->ifa_name);

                                        MacAddress mac;
                                        if (extractMac(ifa, mac) && !mac.isNull()) {
                                                bool dup = false;
                                                for (const auto &existing : c.data.macAddresses) {
                                                        if (existing == mac) {
                                                                dup = true;
                                                                break;
                                                        }
                                                }
                                                if (!dup) c.data.macAddresses.pushToBack(mac);
                                                c.hasMac = true;
                                        }
#if defined(PROMEKI_PLATFORM_BSD)
                                        // Pull MTU from if_data on the AF_LINK
                                        // pass.  Linux uses SIOCGIFMTU below
                                        // since AF_PACKET ifaddrs entries don't
                                        // expose if_data.
                                        if (ifa->ifa_addr != nullptr && ifa->ifa_addr->sa_family == AF_LINK &&
                                            ifa->ifa_data != nullptr) {
                                                const auto *idata =
                                                        reinterpret_cast<const struct if_data *>(ifa->ifa_data);
                                                if (c.data.mtu == 0) c.data.mtu = static_cast<uint32_t>(idata->ifi_mtu);
                                                c.data.kind = classifyBsdKind(idata->ifi_type, c.data.isLoopback);
                                                if (idata->ifi_baudrate > 0) {
                                                        c.data.linkSpeedMbps =
                                                                static_cast<uint64_t>(idata->ifi_baudrate / 1000000ULL);
                                                }
                                        }
#endif
                                        if (ifa->ifa_addr == nullptr) continue;
                                        if (ifa->ifa_addr->sa_family == AF_INET) {
                                                auto [addr, addrErr] = Ipv4Address::fromSockAddr(
                                                        reinterpret_cast<const struct sockaddr_in *>(ifa->ifa_addr));
                                                if (addrErr.isError()) continue;
                                                Ipv4Address mask;
                                                if (ifa->ifa_netmask != nullptr) {
                                                        auto [m, mErr] = Ipv4Address::fromSockAddr(
                                                                reinterpret_cast<const struct sockaddr_in *>(
                                                                        ifa->ifa_netmask));
                                                        if (mErr.isOk()) mask = m;
                                                }
                                                if (mask.isNull()) {
                                                        // Loopback and point-to-point links sometimes
                                                        // omit the netmask; default to /32 so the entry
                                                        // still round-trips through Ipv4Subnet without
                                                        // collapsing into the all-zero "match all" case.
                                                        mask = Ipv4Subnet::netmaskForPrefix(32);
                                                }
                                                c.data.ipv4Subnets.pushToBack(Ipv4Subnet(addr, mask));
                                        } else if (ifa->ifa_addr->sa_family == AF_INET6) {
                                                auto [addr, addrErr] = Ipv6Address::fromSockAddr(
                                                        reinterpret_cast<const struct sockaddr_in6 *>(ifa->ifa_addr));
                                                if (addrErr.isError()) continue;
                                                int prefix = 128;
                                                if (ifa->ifa_netmask != nullptr) {
                                                        const auto *m6 = reinterpret_cast<const struct sockaddr_in6 *>(
                                                                ifa->ifa_netmask);
                                                        int p = prefixLenFromMask(m6->sin6_addr.s6_addr, 16);
                                                        if (p >= 0) prefix = p;
                                                }
                                                c.data.ipv6Subnets.pushToBack(Ipv6Subnet(addr, prefix));
                                        }
                                }
                                freeifaddrs(head);

                                // MTU lookup via SIOCGIFMTU is Linux-only here.
                                // BSD/macOS already populated MTU in the
                                // AF_LINK pass above.
#if defined(PROMEKI_PLATFORM_LINUX)
                                int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
                                if (sock >= 0) {
                                        for (auto &c : collected) {
                                                struct ifreq req;
                                                std::memset(&req, 0, sizeof(req));
                                                std::strncpy(req.ifr_name, c.data.name.cstr(), IFNAMSIZ - 1);
                                                if (::ioctl(sock, SIOCGIFMTU, &req) == 0) {
                                                        c.data.mtu = static_cast<uint32_t>(req.ifr_mtu);
                                                }
                                        }
                                        ::close(sock);
                                }
                                // sysfs-driven enrichment: kind, link
                                // speed/duplex, carrier.  Skipped silently
                                // when /sys/class/net is missing (e.g. in
                                // stripped containers).
                                for (auto &c : collected) {
                                        c.data.kind = classifyLinuxKind(c.data.name, c.data.isLoopback);
                                        readLinuxLinkInfo(c.data.name, c.data);
                                }
#endif

                                for (auto &c : collected) {
                                        // Friendly name on POSIX equals the OS
                                        // name; Windows fills in a separate
                                        // user-visible string.
                                        c.data.friendlyName = c.data.name;
                                        out.pushToBack(NetworkInterfaceImplPtr::takeOwnership(
                                                new PosixInterfaceImpl(std::move(c.data))));
                                }
                                return out;
                        }
        };

        // Auto-register at static-init.  No teardown — the registry
        // owns the pointer for the program's lifetime.
        struct Registrar {
                Registrar() {
                        NetworkInterfaceBackend::registerBackend(new PosixNetworkInterfaceBackend());
                }
        };
        Registrar gPosixRegistrar;
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_PLATFORM_POSIX
