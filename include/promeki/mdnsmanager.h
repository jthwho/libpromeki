/**
 * @file      mdnsmanager.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_MDNS
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/atomic.h>
#include <promeki/buffer.h>
#include <promeki/error.h>
#include <promeki/function.h>
#include <promeki/ipv4address.h>
#include <promeki/ipv6address.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/mdnsrecord.h>
#include <promeki/multicastmanager.h>
#include <promeki/mutex.h>
#include <promeki/networkinterface.h>
#include <promeki/socketaddress.h>
#include <promeki/string.h>
#include <promeki/thread.h>
#include <promeki/timestamp.h>
#include <promeki/udpsocket.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

class MdnsBrowser;
class MdnsPublisher;
class MdnsTypeBrowser;
class NetworkInterfaceMonitor;

/**
 * @brief Top-level engine that owns the mDNS / DNS-SD socket lifecycle.
 * @ingroup network
 *
 * @c MdnsManager is the per-process entry point to mDNS.  It owns:
 *
 *  - One @ref UdpSocket per address family (wildcard-bound to
 *    @c 0.0.0.0:5353 and @c [::]:5353).  Either family can be
 *    individually disabled via @ref setIpFamily.
 *  - The multicast group joins (@c 224.0.0.251 and @c ff02::fb)
 *    against every interface in the start set, tracked through
 *    the bundled @ref MulticastManager so kernel-side memberships
 *    unwind cleanly on @ref stop or destruction.
 *  - A dedicated receive @ref Thread that polls both sockets,
 *    uses @c IP_PKTINFO / @c IPV6_PKTINFO to identify the ingress
 *    interface per packet, and fans inbound datagrams out to
 *    every registered @ref MdnsBrowser and @ref MdnsPublisher.
 *  - The follow-up bookkeeping that drives @ref MdnsBrowser
 *    backoff / eviction and @ref MdnsPublisher probe / announce
 *    timers via the @ref tickInterval housekeeping cadence.
 *  - An optional embedded @ref NetworkInterfaceMonitor (controlled
 *    by @ref setAutoTrackInterfaces) so hot interface add /
 *    remove triggers per-interface joins / leaves without a
 *    restart.
 *
 * @par Threading model
 *
 * The engine itself is a @ref Thread subclass — the worker thread
 * IS the @ref MdnsManager instance.  Public configuration setters
 * and @ref start / @ref stop are called from the owning thread.
 * The receive thread runs the worker @ref EventLoop, with each
 * socket registered as an @c IoRead source and a single
 * @ref tickInterval timer driving housekeeping.  @ref run calls
 * @c moveToThread on itself first thing, so every signal emitted
 * with the manager as the owner — including
 * @ref NetworkInterfaceMonitor callbacks — dispatches from the
 * worker loop.  Signal slots delivered to external subscribers
 * still hop through their own @ref EventLoop per the project's
 * standard cross-thread marshalling.
 *
 * @par Example — passive sniffer skeleton
 * @code
 * MdnsManager m;
 * m.setPacketHook([](NetworkInterface, SocketAddress sender, Buffer data) {
 *     promekiInfo("mdns %zu bytes from %s", data.size(), sender.toString().cstr());
 * });
 * Error err = m.start();                    // joins on every up multicast iface
 * if (err.isError()) return err;
 * // ... event loop runs ...
 * m.stop();
 * @endcode
 *
 * @par Thread Safety
 * Configuration setters and @ref start / @ref stop are intended to
 * be called from the engine's owning thread.  The packet hook is
 * invoked on the receive thread spawned internally — the hook body
 * must be thread-safe with respect to whatever state it touches.
 */
class MdnsManager : public Thread {
                PROMEKI_OBJECT(MdnsManager, Thread)
        public:
                /** @brief Default mDNS UDP port (RFC 6762 §5). */
                static constexpr uint16_t DefaultPort = 5353;

                /** @brief Default receive buffer size (jumbo-frame safe). */
                static constexpr size_t DefaultMaxPacketSize = 9000;

                /**
                 * @brief Default housekeeping tick interval in ms.
                 *
                 * Drives @ref MdnsBrowser::onManagerTick on every
                 * registered browser; controls how often the engine
                 * re-fires backoff queries and evicts expired cache
                 * entries.  One second matches the granularity of the
                 * RFC 6762 §5.2 backoff schedule (1 s, 2 s, 4 s, …);
                 * tests pin a smaller value via @ref setTickInterval
                 * so the schedule runs at unit-test cadence.
                 */
                static constexpr unsigned int DefaultTickIntervalMs = 1000;

                /** @brief Returns the IPv4 mDNS multicast group (@c 224.0.0.251). */
                static Ipv4Address ipv4Group() { return Ipv4Address(224, 0, 0, 251); }

                /** @brief Returns the IPv6 link-local mDNS multicast group (@c ff02::fb). */
                static Ipv6Address ipv6Group();

                /**
                 * @brief Address family the engine should bring online.
                 *
                 * Defaults to @ref IpFamily::Both.  Tests and exotic
                 * deployments can pin one family or the other to skip
                 * the matching socket and group joins.
                 */
                enum class IpFamily : uint8_t {
                        IPv4Only = 1,
                        IPv6Only = 2,
                        Both     = 3,
                };

                /** @brief Sets the address family the engine binds to. Must be called before @ref start. */
                void setIpFamily(IpFamily family);

                /** @brief Returns the configured address family. */
                IpFamily ipFamily() const { return _ipFamily; }

                /**
                 * @brief Per-datagram callback signature.
                 *
                 * Invoked from the receive thread for every datagram
                 * the bound socket observes.  The @ref NetworkInterface
                 * argument is invalid in step 3 (the engine binds a
                 * single socket to @c 0.0.0.0 so there is no
                 * ingress-interface information without
                 * @c IP_PKTINFO); step 4 fills this in.
                 *
                 * @param iface  Ingress @ref NetworkInterface (invalid in step 3).
                 * @param sender Source @ref SocketAddress of the datagram.
                 * @param data   Datagram bytes.  The @ref Buffer owns
                 *               its allocation but only the call body
                 *               is guaranteed to see the same backing
                 *               memory — the next iteration may reuse
                 *               the scratch.
                 */
                using PacketHook = Function<void(NetworkInterface iface,
                                                 SocketAddress sender,
                                                 Buffer data)>;

                /** @brief Constructs an unstarted manager. */
                explicit MdnsManager(ObjectBase *parent = nullptr);

                /** @brief Destructor.  Calls @ref stop if active. */
                ~MdnsManager() override;

                /**
                 * @brief Sets the UDP port used for socket bind and group joins.
                 *
                 * Defaults to @ref DefaultPort.  Must be called before
                 * @ref start.  Overriding the port is intended for
                 * test rigs and embedded link-local devices that
                 * announce on a non-RFC port; well-known mDNS
                 * consumers always speak port 5353.
                 */
                void setPort(uint16_t port);

                /** @brief Returns the configured UDP port. */
                uint16_t port() const { return _port; }

                /**
                 * @brief Sets whether the engine auto-tracks interface changes.
                 *
                 * When @c true (the default) the engine constructs
                 * and starts a @ref NetworkInterfaceMonitor on
                 * @ref start and dynamically joins / leaves the mDNS
                 * groups on every interface that appears, disappears,
                 * gains link, or loses link.  When @c false the
                 * engine takes a one-shot snapshot at @ref start and
                 * never re-evaluates — the legacy step-3 behaviour
                 * suitable for tests and embedded systems with a
                 * known-fixed network configuration.
                 */
                void setAutoTrackInterfaces(bool enable);

                /** @brief Returns whether interface-change auto-tracking is enabled. */
                bool autoTrackInterfaces() const { return _autoTrackInterfaces; }

                /**
                 * @brief Sets whether the loopback interface is auto-included.
                 *
                 * When @ref start is called without an explicit
                 * interface list and this flag is @c true (the
                 * default), the loopback interface is joined along
                 * with every other multicast-capable interface.  Most
                 * tests rely on the loopback path; production code
                 * that only cares about LAN discovery can switch it
                 * off without touching the explicit-list overload.
                 */
                void setIncludeLoopback(bool include);

                /** @brief Returns whether the loopback interface is auto-included. */
                bool includeLoopback() const { return _includeLoopback; }

                /**
                 * @brief Sets the maximum datagram size the receiver will accept.
                 *
                 * Defaults to @ref DefaultMaxPacketSize.  Must be set
                 * before @ref start.  Larger values are silently
                 * clamped by the kernel's per-socket receive buffer.
                 */
                void setMaxPacketSize(size_t bytes);

                /** @brief Returns the configured maximum datagram size. */
                size_t maxPacketSize() const { return _maxPacketSize; }

                /**
                 * @brief Sets the housekeeping tick interval in ms.
                 *
                 * @c 0 restores @ref DefaultTickIntervalMs.  Tests
                 * typically set 20 ms so backoff / eviction kick over
                 * within a single test case body without sleeping a
                 * full second.  Smaller values increase CPU on idle
                 * networks; production code should leave the default.
                 */
                void setTickInterval(unsigned int ms);

                /** @brief Returns the configured housekeeping tick interval. */
                unsigned int tickInterval() const { return _tickIntervalMs; }

                /**
                 * @brief Sends a one-shot mDNS query on the bound socket.
                 *
                 * Builds a minimal DNS query packet — header + one
                 * question — and writes it to the multicast group
                 * @c 224.0.0.251:port (and @c ff02::fb:port when the
                 * IPv6 socket is up) via the engine's send fan-out.
                 *
                 * The transaction ID defaults to 0 (standard for mDNS
                 * per RFC 6762 §18.1); tests can pin a specific ID to
                 * round-trip the receive side.
                 *
                 * @param name        Owner name to query for (e.g.
                 *                    @c "_http._tcp.local.").  May be
                 *                    written with or without a
                 *                    trailing root marker; both encode
                 *                    to the same wire form.
                 * @param recordType  RFC 1035 record-type code
                 *                    (@c 12 for PTR, @c 33 for SRV,
                 *                    @c 16 for TXT, @c 255 for ANY).
                 * @param transactionId Header transaction ID.
                 * @param unicastResponse When @c true, sets the QU
                 *                    (unicast-response-preferred) bit
                 *                    on the question class per RFC
                 *                    6762 §5.4.  Useful for one-shot
                 *                    legacy resolvers running on
                 *                    ephemeral source ports — the
                 *                    receiver replies directly to the
                 *                    source address so an OS-stack
                 *                    that does not forward 224.0.0.251
                 *                    multicast to non-5353 sockets
                 *                    still works.  Default @c false
                 *                    because the engine binds to 5353
                 *                    and receives the multicast
                 *                    response with everyone else.
                 * @return @ref Error::Ok on a successful send.
                 *         @ref Error::NotReady when the engine is
                 *         not currently active.
                 */
                Error sendQuery(const String &name, uint16_t recordType,
                                uint16_t transactionId = 0,
                                bool unicastResponse = false);

                /**
                 * @brief Sends the canonical meta-browse query.
                 *
                 * Sends a PTR query for @c _services._dns-sd._udp.local.
                 * (RFC 6763 §9).  Responses enumerate every service
                 * type currently advertised on the link; consumers
                 * route them through whatever browser machinery they
                 * use to surface the list to the user.
                 */
                Error sendMetaQuery();

                /** @brief FQDN of the RFC 6763 §9 meta-browse target. */
                static String metaBrowseFqdn() {
                        return String("_services._dns-sd._udp.local.");
                }

                /**
                 * @brief Sends a query carrying a known-answer list.
                 *
                 * RFC 6762 §7.1 — a continuous-query browser includes
                 * its currently-cached PTR responses in the query's
                 * Authority section so responders can suppress
                 * duplicate replies for records the client already
                 * knows.  When @p knownAnswers is empty this behaves
                 * exactly like @ref sendQuery.
                 *
                 * The known-answer records are emitted with their
                 * remaining TTL; receivers ignore the TTL field on
                 * Authority records and use the names + types to
                 * decide what to suppress.
                 *
                 * @param name           Owner name to query for.
                 * @param recordType     RFC 1035 record-type code.
                 * @param knownAnswers   Records to advertise as
                 *                       already-known.
                 * @param transactionId  Header transaction ID.
                 */
                Error sendQueryWithKnownAnswers(const String &name, uint16_t recordType,
                                                const List<MdnsRecord> &knownAnswers,
                                                uint16_t transactionId = 0);

                /**
                 * @brief Builds the bytes of an mDNS query without sending.
                 *
                 * Exposed mainly for testing and for callers that want
                 * to relay packets through a non-socket transport.
                 * The returned @ref Buffer's logical size matches the
                 * encoded length; capacity may exceed it slightly.
                 *
                 * @param name        Owner name (e.g. @c "_http._tcp.local.").
                 * @param recordType  RFC 1035 record-type code.
                 * @param transactionId Header transaction ID.
                 * @param unicastResponse  When @c true, sets the QU
                 *                    bit on the question class — see
                 *                    @ref sendQuery for semantics.
                 */
                static Buffer buildQuery(const String &name, uint16_t recordType,
                                         uint16_t transactionId = 0,
                                         bool unicastResponse = false);

                /**
                 * @brief Builds the bytes of an mDNS query that carries
                 *        a known-answer Authority section.
                 *
                 * Companion to @ref sendQueryWithKnownAnswers; exposed
                 * mainly for tests and packet-relay scenarios.
                 */
                static Buffer buildQueryWithKnownAnswers(const String &name, uint16_t recordType,
                                                         const List<MdnsRecord> &knownAnswers,
                                                         uint16_t transactionId = 0,
                                                         bool unicastResponse = false);

                /**
                 * @brief Attaches a browser to receive every inbound datagram.
                 *
                 * Called from @ref MdnsBrowser::setManager — most
                 * callers should not invoke this directly.  The
                 * browser is held by raw pointer; ownership remains
                 * with the caller.  Re-registering an already-attached
                 * browser is a no-op.  Safe to call from any thread.
                 */
                void registerBrowser(MdnsBrowser *browser);

                /**
                 * @brief Attaches a publisher to the manager.
                 *
                 * Called from @ref MdnsPublisher::setManager — most
                 * callers should not invoke directly.  Publishers
                 * receive every inbound datagram and every
                 * housekeeping tick alongside browsers.  Re-registering
                 * an already-attached publisher is a no-op.
                 */
                void registerPublisher(MdnsPublisher *publisher);

                /**
                 * @brief Detaches a previously-registered publisher.
                 *
                 * @par Race protection
                 * Blocks until any in-flight fan-out callback into
                 * the publisher returns — same contract as
                 * @ref unregisterBrowser.
                 */
                void unregisterPublisher(MdnsPublisher *publisher);

                /**
                 * @brief Attaches a type browser to the manager.
                 *
                 * Called from @ref MdnsTypeBrowser::setManager — most
                 * callers should not invoke directly.  Same
                 * raw-pointer registration contract as the browser /
                 * publisher paths.
                 */
                void registerTypeBrowser(MdnsTypeBrowser *typeBrowser);

                /**
                 * @brief Detaches a previously-registered type browser.
                 *
                 * Blocks until any in-flight fan-out callback into
                 * the type browser returns.
                 */
                void unregisterTypeBrowser(MdnsTypeBrowser *typeBrowser);

                /**
                 * @brief Sends a pre-built packet to the link-local mDNS group.
                 *
                 * Used by @ref MdnsPublisher to emit probe / announce
                 * / goodbye / query-response packets it has built
                 * with @ref mdnsBuildAnnounce / @ref mdnsBuildGoodbye
                 * / @ref mdnsBuildProbe.  Fan-out works the same way
                 * as @ref sendQuery — one write per joined interface
                 * with @c IP_MULTICAST_IF / @c IPV6_MULTICAST_IF
                 * steering.  @p ipv6 picks the v4 group (@c 224.0.0.251)
                 * or v6 group (@c ff02::fb); the corresponding
                 * socket must be open or the call is a no-op.
                 */
                Error writeMulticast(const Buffer &packet, bool ipv6);

                /**
                 * @brief Detaches a previously-registered browser.
                 *
                 * No-op when @p browser is not currently attached.
                 * Called by @ref MdnsBrowser::setManager (including
                 * the destructor path).  Safe to call from any
                 * thread.
                 *
                 * @par Race protection
                 * Blocks until any in-flight fan-out callback into
                 * the browser returns — guarantees the browser is
                 * not being dereferenced once this call has
                 * completed.  Calling from a slot dispatched off
                 * the same browser's @ref MdnsBrowser::handlePacket
                 * is a deadlock.
                 */
                void unregisterBrowser(MdnsBrowser *browser);

                /**
                 * @brief Installs the per-datagram inspection hook.
                 *
                 * Optional in step 3; the engine runs fine with no
                 * hook installed (the datagrams are simply counted
                 * and discarded).  Replaces (does not append) any
                 * prior hook.  Safe to call while the engine is
                 * running; the new hook takes effect on the next
                 * inbound datagram.
                 */
                void setPacketHook(PacketHook hook);

                /**
                 * @brief Brings the engine online on all multicast-capable interfaces.
                 *
                 * Snapshots the result of @ref NetworkInterface::enumerate,
                 * filters by @c isUp && @c isMulticast && (@c !isLoopback
                 * || @ref includeLoopback), and forwards to the
                 * explicit-list overload.
                 *
                 * @return @ref Error::Ok if at least one interface
                 *         join succeeded.  When every join fails the
                 *         engine rolls back and returns the first
                 *         failure.
                 */
                Error start();

                /**
                 * @brief Brings the engine online on the given interface set.
                 *
                 * Opens the socket, calls @ref MulticastManager::joinGroup
                 * once per interface, and spawns the receive thread.
                 * Interfaces that fail to join are skipped and their
                 * failure is logged at warning level; an overall
                 * failure (socket open, bind, no interfaces joined)
                 * rolls back the partial state and returns the first
                 * error.
                 *
                 * @param ifaces Set of @ref NetworkInterface objects
                 *               to join on.  Empty defers to the
                 *               zero-arg overload's auto-selection.
                 */
                Error start(const NetworkInterface::List &ifaces);

                /**
                 * @brief Stops the receive thread, leaves groups, and closes the socket.
                 *
                 * Safe to call from any thread, including the receive
                 * thread itself.  Also called automatically from the
                 * destructor.  Idempotent — a second call after the
                 * engine has stopped is a no-op.
                 */
                void stop();

                /** @brief Returns @c true between successful @ref start and @ref stop. */
                bool isActive() const { return _active.value(); }

                /** @brief Snapshot of the interface set the engine is joined on. */
                NetworkInterface::List joinedInterfaces() const;

                /**
                 * @brief Total number of datagrams delivered to the hook.
                 *
                 * Atomic counter incremented by the receive thread.
                 * Reset to zero on each @ref start call.
                 */
                uint64_t datagramCount() const { return _datagramCount.value(); }

                /** @brief Total number of bytes delivered to the hook. */
                uint64_t byteCount() const { return _byteCount.value(); }

                /**
                 * @brief Returns the bound IPv4 socket, or @c nullptr.
                 *
                 * Exposed so tests and advanced callers can read
                 * socket-level state (local address, options); the
                 * receive thread owns the read side.  May be
                 * @c nullptr when the engine was configured for
                 * @ref IpFamily::IPv6Only.
                 */
                UdpSocket *socket() const { return _socket.get(); }

                /**
                 * @brief Returns the bound IPv6 socket, or @c nullptr.
                 *
                 * Symmetric to @ref socket.  @c nullptr when
                 * configured for @ref IpFamily::IPv4Only or when the
                 * IPv6 socket failed to open at @ref start time.
                 */
                UdpSocket *socketV6() const { return _socketV6.get(); }

                /** @brief Emitted on each successful interface join. @signal */
                PROMEKI_SIGNAL(interfaceAdded,   NetworkInterface);
                /** @brief Emitted on each successful interface leave. @signal */
                PROMEKI_SIGNAL(interfaceRemoved, NetworkInterface);
                /** @brief Emitted when the receive loop sees a non-timeout error. @signal */
                PROMEKI_SIGNAL(receiveError,     Error);

        protected:
                /** @brief Thread entry — runs the receive loop. */
                void run() override;

        private:
                Error openAndJoin(const NetworkInterface::List &ifaces);
                Error openSocket(bool ipv6);
                Error joinGroups(const NetworkInterface::List &ifaces, int *joined);
                Error joinGroupsOnInterface(const NetworkInterface &iface, int *joined);
                void  closeAndLeave();
                NetworkInterface::List selectAllMulticastInterfaces() const;
                bool                   acceptInterface(const NetworkInterface &iface) const;
                NetworkInterface       interfaceFromIfIndex(unsigned int ifindex) const;

                // Sends @p pkt out @p dest on every joined interface
                // via @p sock with @c IP_MULTICAST_IF / @c IPV6_MULTICAST_IF
                // steering.  Returns the number of successful sends.
                int sendOnSocketPerInterface(UdpSocket *sock, const Buffer &pkt,
                                             const SocketAddress &dest);

                // EventLoop IO-source callback: drains @p sock until
                // EAGAIN, then fans every received datagram out to
                // browsers / publishers / type browsers under the
                // standard mutex discipline.  On @c IoError emits
                // @ref receiveErrorSignal before attempting the drain
                // (POLLERR / POLLHUP can co-occur with queued data).
                void handleSocketEvents(UdpSocket *sock, uint32_t events);

                // EventLoop timer callback: drives onManagerTick on
                // every registered browser / publisher / type browser.
                // Fires unconditionally — no wall-clock comparison
                // because the EventLoop timer cadence already
                // matches @ref tickInterval.
                void runTick();

                // Monitor wiring.  @ref attachInterfaceMonitor is
                // called from @ref start; @ref detachInterfaceMonitor
                // from @ref stop.  Slots are dispatched on the
                // manager's affinity thread (the @ref ObjectBase
                // construction thread); their bodies mutate the
                // joined set under @ref _interfacesMtx and call into
                // the @ref MulticastManager to issue per-interface
                // joins / leaves on the wildcard socket.
                void attachInterfaceMonitor();
                void detachInterfaceMonitor();
                void onInterfaceAppeared(const NetworkInterface &iface);
                void onInterfaceDisappeared(const NetworkInterface &iface);

                UdpSocket::UPtr                    _socket;       ///< IPv4 socket (wildcard-bound to @c 0.0.0.0:port).
                UdpSocket::UPtr                    _socketV6;     ///< IPv6 socket (wildcard-bound to @c [::]:port).
                MulticastManager                   _multicastManager;
                NetworkInterface::List             _joinedInterfaces;
                // Cached ifindex → NetworkInterface for fast receive-
                // side attribution.  Rebuilt on each interface join /
                // leave so the receive thread can look up by the
                // @c IP_PKTINFO / @c IPV6_PKTINFO cmsg without
                // walking @ref _joinedInterfaces.  Guarded by
                // @ref _interfacesMtx.
                Map<unsigned int, NetworkInterface> _byIfIndex;
                mutable Mutex                      _interfacesMtx;
                PacketHook                         _packetHook;
                List<MdnsBrowser *>                _browsers;
                mutable Mutex                      _browsersMtx;
                List<MdnsPublisher *>              _publishers;
                mutable Mutex                      _publishersMtx;
                List<MdnsTypeBrowser *>            _typeBrowsers;
                mutable Mutex                      _typeBrowsersMtx;
                UniquePtr<NetworkInterfaceMonitor> _ifMonitor;

                uint16_t                           _port               = DefaultPort;
                bool                               _includeLoopback    = true;
                bool                               _autoTrackInterfaces = true;
                IpFamily                           _ipFamily           = IpFamily::Both;
                size_t                             _maxPacketSize      = DefaultMaxPacketSize;
                unsigned int                       _tickIntervalMs     = DefaultTickIntervalMs;

                Atomic<bool>                       _active;
                Atomic<uint64_t>                   _datagramCount;
                Atomic<uint64_t>                   _byteCount;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_MDNS
