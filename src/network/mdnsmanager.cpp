/**
 * @file      mdnsmanager.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <promeki/eventloop.h>
#include <promeki/mdnsmanager.h>
#include <promeki/mdnsbrowser.h>
#include <promeki/mdnsname.h>
#include <promeki/mdnspublisher.h>
#include <promeki/mdnstypebrowser.h>
#include <promeki/networkinterfacemonitor.h>
#include <promeki/objectbase.tpp>
#include <promeki/logger.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

MdnsManager::MdnsManager(ObjectBase *parent) : Thread(parent) {
        _active.setValue(false);
        _datagramCount.setValue(0);
        _byteCount.setValue(0);
        // The default thread name picks up the engine's role in
        // top / htop / perf.  Truncated by the kernel to ~15 bytes.
        Thread::setName(String("mdns"));
}

MdnsManager::~MdnsManager() {
        stop();
}

void MdnsManager::setPort(uint16_t port) {
        _port = port;
}

void MdnsManager::setIncludeLoopback(bool include) {
        _includeLoopback = include;
}

void MdnsManager::setAutoTrackInterfaces(bool enable) {
        _autoTrackInterfaces = enable;
}

void MdnsManager::setIpFamily(IpFamily family) {
        _ipFamily = family;
}

Ipv6Address MdnsManager::ipv6Group() {
        // ff02::fb is the link-local mDNS group; the leading 0xff02
        // identifies link-local-scope multicast and the trailing
        // 0xfb is the mDNS application ID.
        static const uint8_t kBytes[16] = {
                0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfb,
        };
        return Ipv6Address(kBytes);
}

void MdnsManager::setMaxPacketSize(size_t bytes) {
        _maxPacketSize = (bytes == 0) ? DefaultMaxPacketSize : bytes;
}

void MdnsManager::setTickInterval(unsigned int ms) {
        _tickIntervalMs = (ms == 0) ? DefaultTickIntervalMs : ms;
}

void MdnsManager::setPacketHook(PacketHook hook) {
        _packetHook = std::move(hook);
}

int MdnsManager::registerPacketObserver(PacketHook observer) {
        if (!observer) {
                promekiWarn("MdnsManager::registerPacketObserver: empty observer");
                return -1;
        }
        const int     handle = _nextObserverHandle.fetchAndAdd(1) + 1;
        Mutex::Locker lock(_packetObserversMtx);
        PacketObserver entry;
        entry.handle = handle;
        entry.hook   = std::move(observer);
        _packetObservers += std::move(entry);
        return handle;
}

void MdnsManager::unregisterPacketObserver(int handle) {
        if (handle < 0) return;
        // Hold the lock through the removal so a concurrent fan-out
        // either dispatches the observer before we remove it (and
        // returns first, blocking us on the same mutex) or doesn't
        // see it at all on the next datagram.
        Mutex::Locker lock(_packetObserversMtx);
        _packetObservers.removeIf(
            [handle](const PacketObserver &e) { return e.handle == handle; });
}

Buffer MdnsManager::buildQuery(const String &name, uint16_t recordType, uint16_t transactionId,
                               bool unicastResponse) {
        return buildQueryWithKnownAnswers(name, recordType, List<MdnsRecord>(),
                                          transactionId, unicastResponse);
}

Buffer MdnsManager::buildQueryWithKnownAnswers(const String &name, uint16_t recordType,
                                               const List<MdnsRecord> &knownAnswers,
                                               uint16_t transactionId, bool unicastResponse) {
        // Build a query packet with one question + optional Answer
        // section carrying known-answer records (RFC 6762 §7.1).
        // We share the heavy lifting with mdnsBuildAnnounce by
        // wrapping a synthetic record list for the answer section —
        // but the header semantics (qdcount/ancount) and the
        // trailing question encoding are query-specific, so we
        // build the bytes here directly.
        //
        // Wire layout:
        //   header (12 bytes)
        //   question      = name + 2(type) + 2(class)
        //   ancount records (the known answers)

        // Escape-aware split — same convention as mdnsrecord.cpp,
        // so a name like @c "Studio\.B Camera._http._tcp.local."
        // encodes as four labels rather than five.
        const List<String> labels = mdnsSplitName(name);
        size_t encodedNameLen = 1;   // trailing zero root
        for (const String &lab : labels) encodedNameLen += 1 + lab.size();

        // For the known-answer block we lean on the same encoder
        // that mdnsBuildAnnounce uses, then splice its
        // record-section bytes onto the end of our header +
        // question.  This keeps every record encoder in one place
        // (no duplicate name/SRV/TXT/A encoders inside MdnsManager).
        Buffer knownBytes;
        if (!knownAnswers.isEmpty()) {
                Buffer wrapped = mdnsBuildAnnounce(knownAnswers, /*txId*/ 0);
                // wrapped is [12 bytes header][records...].  Take just
                // the records.
                if (wrapped.size() > 12) {
                        const size_t recBytes = wrapped.size() - 12;
                        knownBytes = Buffer(recBytes);
                        std::memcpy(knownBytes.data(),
                                    static_cast<const uint8_t *>(wrapped.data()) + 12, recBytes);
                        knownBytes.setSize(recBytes);
                }
        }

        const size_t total = 12 + encodedNameLen + 4 + knownBytes.size();
        Buffer       buf(total);
        uint8_t     *out = static_cast<uint8_t *>(buf.data());

        auto writeU16 = [&](size_t pos, uint16_t v) {
                out[pos]     = static_cast<uint8_t>(v >> 8);
                out[pos + 1] = static_cast<uint8_t>(v & 0xFF);
        };
        writeU16(0,  transactionId);
        writeU16(2,  0x0000);                                                 // flags: standard query
        writeU16(4,  0x0001);                                                 // qdcount
        writeU16(6,  static_cast<uint16_t>(knownAnswers.size()));             // ancount (KAS, §7.1)
        writeU16(8,  0x0000);                                                 // nscount
        writeU16(10, 0x0000);                                                 // arcount

        size_t pos = 12;
        for (const String &lab : labels) {
                size_t      len = lab.size();
                const char *src = lab.cstr();
                out[pos++] = static_cast<uint8_t>(len);
                for (size_t j = 0; j < len; ++j) out[pos++] = static_cast<uint8_t>(src[j]);
        }
        out[pos++] = 0;
        writeU16(pos, recordType);
        pos += 2;
        // QU bit (RFC 6762 §5.4) on the top bit of the class field.
        const uint16_t klass = unicastResponse ? 0x8001 : 0x0001;
        writeU16(pos, klass);
        pos += 2;

        if (knownBytes.size() > 0) {
                std::memcpy(out + pos, knownBytes.data(), knownBytes.size());
                pos += knownBytes.size();
        }

        buf.setSize(pos);
        return buf;
}

Error MdnsManager::sendQuery(const String &name, uint16_t recordType, uint16_t transactionId,
                             bool unicastResponse) {
        if (!_active.value()) return Error::NotReady;
        const Buffer pkt = buildQuery(name, recordType, transactionId, unicastResponse);

        // Send on every joined interface, on both families when the
        // engine has them up.  With wildcard-bound sockets the kernel
        // would otherwise pick one egress per routing-table — wrong
        // on a multi-NIC LAN where mDNS expects the query on every
        // joined link.  We re-set @c IP_MULTICAST_IF / @c IPV6_MULTICAST_IF
        // before each write to steer the datagram.  The
        // per-(socket,interface) syscall overhead is acceptable
        // because mDNS sends are O(seconds) apart on the backoff
        // schedule.
        int successes = 0;
        if (_socket.isValid()) {
                successes += sendOnSocketPerInterface(_socket.ptr(), pkt,
                                                      SocketAddress(ipv4Group(), _port));
        }
        if (_socketV6.isValid()) {
                successes += sendOnSocketPerInterface(_socketV6.ptr(), pkt,
                                                      SocketAddress(ipv6Group(), _port));
        }
        return (successes > 0) ? Error::Ok : Error::LibraryFailure;
}

Error MdnsManager::sendQueryWithKnownAnswers(const String &name, uint16_t recordType,
                                             const List<MdnsRecord> &knownAnswers,
                                             uint16_t transactionId) {
        if (!_active.value()) return Error::NotReady;
        const Buffer pkt = buildQueryWithKnownAnswers(name, recordType, knownAnswers,
                                                      transactionId, /*unicast*/ false);
        int successes = 0;
        if (_socket.isValid()) {
                successes += sendOnSocketPerInterface(_socket.ptr(), pkt,
                                                      SocketAddress(ipv4Group(), _port));
        }
        if (_socketV6.isValid()) {
                successes += sendOnSocketPerInterface(_socketV6.ptr(), pkt,
                                                      SocketAddress(ipv6Group(), _port));
        }
        return (successes > 0) ? Error::Ok : Error::LibraryFailure;
}

int MdnsManager::sendOnSocketPerInterface(UdpSocket *sock, const Buffer &pkt,
                                          const SocketAddress &dest) {
        NetworkInterface::List ifaces;
        {
                Mutex::Locker lock(_interfacesMtx);
                ifaces = _joinedInterfaces;
        }
        if (ifaces.isEmpty()) return 0;
        int successes = 0;
        for (const NetworkInterface &iface : ifaces) {
                Error err = sock->setMulticastInterface(iface.name());
                if (err.isError()) continue;
                int64_t sent = sock->writeDatagram(pkt.data(), pkt.size(), dest);
                if (sent == static_cast<int64_t>(pkt.size())) ++successes;
        }
        return successes;
}

Error MdnsManager::sendMetaQuery() {
        // PTR query on the well-known meta-browse name.  Receivers
        // respond with one PTR record per advertised service type.
        return sendQuery(metaBrowseFqdn(), /*PTR*/ 12, 0);
}

void MdnsManager::registerBrowser(MdnsBrowser *browser) {
        if (browser == nullptr) return;
        Mutex::Locker lock(_browsersMtx);
        if (_browsers.contains(browser)) return;
        _browsers += browser;
}

void MdnsManager::unregisterBrowser(MdnsBrowser *browser) {
        if (browser == nullptr) return;
        Mutex::Locker lock(_browsersMtx);
        _browsers.removeFirst(browser);
}

void MdnsManager::registerPublisher(MdnsPublisher *publisher) {
        if (publisher == nullptr) return;
        Mutex::Locker lock(_publishersMtx);
        if (_publishers.contains(publisher)) return;
        _publishers += publisher;
}

void MdnsManager::unregisterPublisher(MdnsPublisher *publisher) {
        if (publisher == nullptr) return;
        Mutex::Locker lock(_publishersMtx);
        _publishers.removeFirst(publisher);
}

void MdnsManager::registerTypeBrowser(MdnsTypeBrowser *typeBrowser) {
        if (typeBrowser == nullptr) return;
        Mutex::Locker lock(_typeBrowsersMtx);
        if (_typeBrowsers.contains(typeBrowser)) return;
        _typeBrowsers += typeBrowser;
}

void MdnsManager::unregisterTypeBrowser(MdnsTypeBrowser *typeBrowser) {
        if (typeBrowser == nullptr) return;
        Mutex::Locker lock(_typeBrowsersMtx);
        _typeBrowsers.removeFirst(typeBrowser);
}

Error MdnsManager::writeMulticast(const Buffer &packet, bool ipv6) {
        UdpSocket *sock = ipv6 ? _socketV6.ptr() : _socket.ptr();
        if (sock == nullptr) return Error::NotReady;
        const SocketAddress dest = ipv6 ? SocketAddress(ipv6Group(), _port)
                                        : SocketAddress(ipv4Group(), _port);
        int successes = sendOnSocketPerInterface(sock, packet, dest);
        return (successes > 0) ? Error::Ok : Error::LibraryFailure;
}

NetworkInterface::List MdnsManager::joinedInterfaces() const {
        Mutex::Locker lock(_interfacesMtx);
        return _joinedInterfaces;
}

NetworkInterface::List MdnsManager::selectAllMulticastInterfaces() const {
        NetworkInterface::List all = NetworkInterface::enumerate();
        NetworkInterface::List out;
        for (const NetworkInterface &i : all) {
                if (acceptInterface(i)) out += i;
        }
        return out;
}

bool MdnsManager::acceptInterface(const NetworkInterface &i) const {
        if (!i.isValid())               return false;
        if (!i.isUp())                  return false;
        if (!i.isMulticast())           return false;
        if (i.isLoopback() && !_includeLoopback) return false;
        return true;
}

Error MdnsManager::openAndJoin(const NetworkInterface::List &ifaces) {
        if (_socket.isValid() || _socketV6.isValid()) return Error::Busy;

        const bool wantV4 = (_ipFamily != IpFamily::IPv6Only);
        const bool wantV6 = (_ipFamily != IpFamily::IPv4Only);

        if (wantV4) {
                Error err = openSocket(/*ipv6*/ false);
                if (err.isError()) return err;
        }
        if (wantV6) {
                Error err = openSocket(/*ipv6*/ true);
                if (err.isError()) {
                        // v4 already up — leave it up only if v4 was
                        // also requested.  When the user explicitly
                        // asked for v6-only, propagate the failure.
                        if (!wantV4) return err;
                        promekiWarn("MdnsManager: IPv6 socket open failed (%s); "
                                    "engine continues in IPv4-only mode",
                                    err.name().cstr());
                }
        }

        int joined = 0;
        NetworkInterface::List confirmed;
        for (const NetworkInterface &iface : ifaces) {
                int delta = 0;
                joinGroupsOnInterface(iface, &delta);
                if (delta > 0) {
                        ++joined;
                        confirmed += iface;
                }
        }
        if (joined == 0) {
                promekiErr("MdnsManager: no interfaces accepted any group join");
                closeAndLeave();
                return Error::LibraryFailure;
        }

        {
                Mutex::Locker lock(_interfacesMtx);
                _joinedInterfaces = confirmed;
                _byIfIndex.clear();
                for (const NetworkInterface &iface : confirmed) {
                        _byIfIndex.insert(static_cast<unsigned int>(iface.index()), iface);
                }
        }
        for (const NetworkInterface &iface : confirmed) {
                interfaceAddedSignal.emit(iface);
        }
        return Error::Ok;
}

Error MdnsManager::openSocket(bool ipv6) {
        UdpSocket::UPtr sock = UdpSocket::UPtr::create();
        Error err = ipv6 ? sock->openIpv6(IODevice::ReadWrite)
                         : sock->open(IODevice::ReadWrite);
        if (err.isError()) {
                promekiErr("MdnsManager: %s socket open failed: %s",
                           ipv6 ? "IPv6" : "IPv4", err.desc().cstr());
                return err;
        }

        // SO_REUSEADDR is mandatory at the mDNS port: it lets us
        // coexist with any host-side mDNS daemon already bound to
        // 5353 (Avahi, mDNSResponder), and lets the v4 and v6
        // engine sockets share a bind on systems that don't put them
        // in separate kernel-level buckets.
        err = sock->setReuseAddress(true);
        if (err.isError()) {
                promekiErr("MdnsManager: setReuseAddress failed: %s", err.desc().cstr());
                sock->close();
                return err;
        }

        // Multicast loopback ON so a single-host engine sees its own
        // outbound queries / announces — required for tests and for
        // running multiple mDNS-aware processes on the same machine.
        err = sock->setMulticastLoopback(true);
        if (err.isError()) {
                promekiWarn("MdnsManager: setMulticastLoopback failed (%s): %s",
                            ipv6 ? "v6" : "v4", err.desc().cstr());
                // Non-fatal.
        }

        // RFC 6762 §11: outbound mDNS multicast packets MUST carry
        // TTL=255.  Same setsockopt covers @c IP_MULTICAST_TTL and
        // @c IPV6_MULTICAST_HOPS — the UdpSocket wrapper dispatches
        // by the open-family flag.
        err = sock->setMulticastTTL(255);
        if (err.isError()) {
                promekiWarn("MdnsManager: setMulticastTTL(255) failed (%s): %s",
                            ipv6 ? "v6" : "v4", err.desc().cstr());
                // Non-fatal.
        }

        if (ipv6) {
                err = sock->bind(SocketAddress(Ipv6Address::any(), _port));
        } else {
                err = sock->bind(SocketAddress::any(_port));
        }
        if (err.isError()) {
                promekiErr("MdnsManager: bind %s:%u failed: %s",
                           ipv6 ? "[::]" : "*",
                           static_cast<unsigned>(_port), err.desc().cstr());
                sock->close();
                return err;
        }

        // No @c SO_RCVTIMEO: the receive thread is driven by the
        // worker @ref EventLoop's IO multiplexer (which only wakes
        // on real readability or @c IoError), not by per-recv
        // timeouts.  Shutdown is delivered via @c EventLoop::quit.

        // Enable @c IP_PKTINFO / @c IPV6_RECVPKTINFO so the receive
        // path can identify the ingress interface from the cmsg
        // without per-interface sockets.  Falls back gracefully on
        // platforms that don't expose the option (Windows takes the
        // @ref Error::NotSupported path in UdpSocket; mDNS still
        // works but ingress interface attribution stays unknown).
        err = sock->setReceivePktInfo(true);
        if (err.isError()) {
                promekiWarn("MdnsManager: setReceivePktInfo failed (%s): %s "
                            "(ingress interface attribution will be unknown)",
                            ipv6 ? "v6" : "v4", err.desc().cstr());
                // Non-fatal.
        }

        if (ipv6) {
                _socketV6 = std::move(sock);
        } else {
                _socket = std::move(sock);
        }
        return Error::Ok;
}

Error MdnsManager::joinGroupsOnInterface(const NetworkInterface &iface, int *joined) {
        Error firstErr = Error::Ok;
        int   delta    = 0;
        if (_socket.isValid()) {
                const SocketAddress group(ipv4Group(), _port);
                Error err = _multicastManager.joinGroup(group, _socket.ptr(), iface.name());
                if (err.isError()) {
                        if (firstErr.isOk()) firstErr = err;
                        promekiWarn("MdnsManager: join %s on %s failed: %s",
                                    group.toString().cstr(), iface.name().cstr(), err.desc().cstr());
                } else {
                        ++delta;
                }
        }
        if (_socketV6.isValid()) {
                const SocketAddress group6(ipv6Group(), _port);
                Error err = _multicastManager.joinGroup(group6, _socketV6.ptr(), iface.name());
                if (err.isError()) {
                        if (firstErr.isOk()) firstErr = err;
                        promekiWarn("MdnsManager: join %s on %s failed: %s",
                                    group6.toString().cstr(), iface.name().cstr(), err.desc().cstr());
                } else {
                        ++delta;
                }
        }
        if (joined != nullptr) *joined = delta;
        return (delta > 0) ? Error::Ok : (firstErr.isError() ? firstErr : Error::LibraryFailure);
}

NetworkInterface MdnsManager::interfaceFromIfIndex(unsigned int ifindex) const {
        if (ifindex == 0) return NetworkInterface();
        Mutex::Locker lock(_interfacesMtx);
        auto it = _byIfIndex.find(ifindex);
        if (it == _byIfIndex.end()) return NetworkInterface();
        return it->second;
}

void MdnsManager::closeAndLeave() {
        NetworkInterface::List leaving;
        {
                Mutex::Locker lock(_interfacesMtx);
                leaving = _joinedInterfaces;
                _joinedInterfaces.clear();
                _byIfIndex.clear();
        }
        for (const NetworkInterface &iface : leaving) {
                interfaceRemovedSignal.emit(iface);
        }
        // MulticastManager owns the per-(socket, group, iface)
        // memberships; one call here drops every one of them.  Safe
        // to invoke even if no joins succeeded (the manager is empty
        // and the call is a no-op).
        _multicastManager.leaveAllGroups();
        if (_socket.isValid()) {
                _socket->close();
                _socket.clear();
        }
        if (_socketV6.isValid()) {
                _socketV6->close();
                _socketV6.clear();
        }
}

Error MdnsManager::start() {
        return start(selectAllMulticastInterfaces());
}

Error MdnsManager::start(const NetworkInterface::List &ifaces) {
        if (_active.value()) return Error::Busy;
        if (ifaces.isEmpty()) {
                promekiErr("MdnsManager: start() with no interfaces");
                return Error::Invalid;
        }

        _datagramCount.setValue(0);
        _byteCount.setValue(0);

        Error err = openAndJoin(ifaces);
        if (err.isError()) return err;

        if (_autoTrackInterfaces) attachInterfaceMonitor();

        _active.setValue(true);
        return Thread::start();
}

void MdnsManager::stop() {
        // Two valid entry states: (1) thread running + socket open
        // (active engine), and (2) socket open + thread not yet
        // started (start() failed somewhere after openAndJoin).
        // closeAndLeave handles both — and is a no-op when neither
        // applies, so a stop() on a never-started engine is fine.
        if (!_active.value() && !_socket.isValid()) return;

        // Ask the worker EventLoop to exit.  Safe to call cross-
        // thread (the @ref EventLoop wake fd unblocks an exec()
        // currently sleeping inside @c processEvents) and a no-op
        // when the thread never started.
        Thread::quit(0);

        // Skip the join when stop() is invoked from inside the
        // worker (a callback that requested shutdown from itself);
        // Thread::wait() on the current thread would deadlock.
        if (!isCurrentThread()) {
                Thread::wait();
        }

        // Drop the interface monitor before tearing down state the
        // monitor slots touch.  With the worker already joined, no
        // monitor callable can land on a half-deconstructed engine
        // — but detach also stops the monitor itself so no new
        // signals queue up against the manager during teardown.
        detachInterfaceMonitor();

        closeAndLeave();
        _active.setValue(false);
}

void MdnsManager::run() {
        // Move our @ref ObjectBase affinity onto this worker so that
        // every signal connected with the manager as the owner —
        // including the @ref NetworkInterfaceMonitor slots wired up
        // by @ref attachInterfaceMonitor — dispatches via the
        // worker @ref EventLoop from here on.  Without this hop the
        // monitor signals would still land on the construction
        // thread (typically main) even though all the state they
        // mutate is now driven by the worker.
        moveToThread(this);

        EventLoop *loop = threadEventLoop();
        PROMEKI_ASSERT(loop != nullptr);

        // Register one read source per open socket.  Each callback
        // drains its socket until @c readDatagramWithIfIndex
        // reports nothing pending — bursts of inbound traffic
        // therefore produce one IO event per readable transition,
        // not one per datagram.  The cast wraps @c this and the
        // socket pointer — both outlive the EventLoop because the
        // loop is owned by this Thread, which we are.
        List<int> ioSourceIds;
        const uint32_t ioMask = EventLoop::IoRead | EventLoop::IoError;
        if (_socket.isValid()) {
                UdpSocket *sock = _socket.ptr();
                int id = loop->addIoSource(sock->socketDescriptor(), ioMask,
                                           [this, sock](int /*fd*/, uint32_t events) {
                                                   handleSocketEvents(sock, events);
                                           });
                if (id >= 0) ioSourceIds += id;
        }
        if (_socketV6.isValid()) {
                UdpSocket *sock = _socketV6.ptr();
                int id = loop->addIoSource(sock->socketDescriptor(), ioMask,
                                           [this, sock](int /*fd*/, uint32_t events) {
                                                   handleSocketEvents(sock, events);
                                           });
                if (id >= 0) ioSourceIds += id;
        }

        // Periodic housekeeping tick — fires every @ref tickInterval
        // ms unconditionally, regardless of whether the network is
        // active.  Drives browser backoff / eviction and publisher
        // probe / announce timers.
        int tickTimerId = loop->startTimer(_tickIntervalMs,
                                           [this]() { runTick(); },
                                           /*singleShot*/ false);

        // Drive the loop.  Returns when @ref stop calls
        // @c Thread::quit on the worker @ref EventLoop.
        loop->exec();

        // Explicit unwind so a future @ref start cycle on the same
        // manager would re-register from a clean slate.  The
        // EventLoop itself dies with the Thread on the next
        // teardown, but registered timers and IO sources hold
        // captures of @c this — clearing them here is cheap and
        // makes the symmetry obvious.
        if (tickTimerId >= 0) loop->stopTimer(tickTimerId);
        for (int id : ioSourceIds) loop->removeIoSource(id);
}

void MdnsManager::handleSocketEvents(UdpSocket *sock, uint32_t events) {
        if (events & EventLoop::IoError) {
                // POLLERR / POLLHUP / loop-side failure — surface to
                // subscribers and then still try to drain, since on
                // some platforms a hangup can co-occur with queued
                // data that the kernel will yield to one final
                // recvmsg.
                receiveErrorSignal.emit(Error::LibraryFailure);
        }

        // Scratch buffer lives at function scope rather than as a
        // member so the receive path stays thread-local even when
        // multiple manager instances coexist.
        List<uint8_t> scratch;
        scratch.resize(_maxPacketSize);

        for (;;) {
                SocketAddress sender;
                unsigned int  ifindex = 0;
                int64_t       n = sock->readDatagramWithIfIndex(
                        scratch.data(), scratch.size(), &sender, &ifindex);
                if (n <= 0) break;

                Buffer datagram(static_cast<size_t>(n));
                std::memcpy(datagram.data(), scratch.data(), static_cast<size_t>(n));
                datagram.setSize(static_cast<size_t>(n));

                ++_datagramCount;
                _byteCount.fetchAndAdd(static_cast<uint64_t>(n));

                NetworkInterface ingress = interfaceFromIfIndex(ifindex);

                if (_packetHook) {
                        _packetHook(ingress, sender, datagram);
                }

                // Fan out to per-handle observers under their own
                // mutex.  Take a snapshot copy of the hook list so
                // observer bodies that call back into
                // @ref unregisterPacketObserver don't deadlock on
                // @ref _packetObserversMtx (the unregister blocks
                // until in-flight calls return; that contract holds
                // because the snapshot keeps the hook callable alive
                // for the duration of the dispatch even if the
                // underlying entry is removed mid-iteration).
                {
                        List<PacketObserver> snapshot;
                        {
                                Mutex::Locker lock(_packetObserversMtx);
                                snapshot = _packetObservers;
                        }
                        for (const PacketObserver &obs : snapshot) {
                                if (obs.hook) obs.hook(ingress, sender, datagram);
                        }
                }

                // Hold each fan-out mutex through the entire
                // dispatch so a concurrent ~MdnsBrowser /
                // ~MdnsPublisher / ~MdnsTypeBrowser blocks on the
                // matching unregister call until in-flight callbacks
                // return.  This is the same race-protection contract
                // the previous poll loop relied on.
                {
                        Mutex::Locker lock(_browsersMtx);
                        for (MdnsBrowser *b : _browsers) {
                                b->handlePacket(datagram, sender, ingress);
                        }
                }
                {
                        Mutex::Locker lock(_publishersMtx);
                        for (MdnsPublisher *p : _publishers) {
                                p->handlePacket(datagram, sender, ingress);
                        }
                }
                {
                        Mutex::Locker lock(_typeBrowsersMtx);
                        for (MdnsTypeBrowser *t : _typeBrowsers) {
                                t->handlePacket(datagram, sender, ingress);
                        }
                }
        }
}

void MdnsManager::runTick() {
        // EventLoop drives the timer cadence directly, so there is
        // no wall-clock comparison to make here — every fire is a
        // tick.  Same locking discipline as the receive fan-out
        // (see @ref handleSocketEvents).
        TimeStamp now = TimeStamp::now();
        {
                Mutex::Locker lock(_browsersMtx);
                for (MdnsBrowser *b : _browsers) {
                        b->onManagerTick(now);
                }
        }
        {
                Mutex::Locker lock(_publishersMtx);
                for (MdnsPublisher *p : _publishers) {
                        p->onManagerTick(now);
                }
        }
        {
                Mutex::Locker lock(_typeBrowsersMtx);
                for (MdnsTypeBrowser *t : _typeBrowsers) {
                        t->onManagerTick(now);
                }
        }
}

void MdnsManager::attachInterfaceMonitor() {
        if (_ifMonitor) return;
        _ifMonitor = UniquePtr<NetworkInterfaceMonitor>::create();

        // Cross-thread connect: the monitor lives on the
        // constructing thread (typically main); the manager has
        // moved its affinity to the worker @ref EventLoop inside
        // @ref run.  Connecting with @c this as the owner therefore
        // routes every monitor signal slot through the worker's
        // loop — the slot bodies (onInterfaceAppeared /
        // onInterfaceDisappeared) mutate joined-set bookkeeping
        // and call into @c _multicastManager, so running them on
        // the worker keeps the entire engine-mutation surface
        // single-threaded.
        //
        // We listen on both "interface appeared" and "link came up"
        // — a freshly-discovered interface fires interfaceAdded;
        // an interface that was already enumerated but down at
        // start() time fires linkUp when it comes online.  Same
        // join semantics apply.
        _ifMonitor->interfaceAddedSignal.connect(
                [this](NetworkInterface i) { onInterfaceAppeared(i); }, this);
        _ifMonitor->linkUpSignal.connect(
                [this](NetworkInterface i) { onInterfaceAppeared(i); }, this);
        _ifMonitor->interfaceRemovedSignal.connect(
                [this](NetworkInterface i) { onInterfaceDisappeared(i); }, this);
        _ifMonitor->linkDownSignal.connect(
                [this](NetworkInterface i) { onInterfaceDisappeared(i); }, this);

        Error err = _ifMonitor->start();
        if (err.isError()) {
                // Non-fatal: hosts without the netlink notification
                // source (sandboxes, exotic ports) operate as if
                // setAutoTrackInterfaces(false) had been called.
                promekiWarn("MdnsManager: NetworkInterfaceMonitor start failed: %s "
                            "(hot interface add/remove will not be tracked)",
                            err.name().cstr());
                _ifMonitor.clear();
        }
}

void MdnsManager::detachInterfaceMonitor() {
        if (!_ifMonitor) return;
        _ifMonitor->stop();
        _ifMonitor.clear();
}

void MdnsManager::onInterfaceAppeared(const NetworkInterface &iface) {
        if (!_active.value()) return;
        if (!acceptInterface(iface)) return;

        // Skip if already joined — linkUp can fire alongside
        // interfaceAdded for an interface that's up at first
        // enumeration, and acceptInterface intentionally lets both
        // through.
        {
                Mutex::Locker lock(_interfacesMtx);
                for (const NetworkInterface &j : _joinedInterfaces) {
                        if (j == iface) return;
                }
        }

        int delta = 0;
        Error err = joinGroupsOnInterface(iface, &delta);
        if (err.isError() || delta == 0) {
                promekiWarn("MdnsManager: late join on %s failed (no families joined)",
                            iface.name().cstr());
                return;
        }
        {
                Mutex::Locker lock(_interfacesMtx);
                _joinedInterfaces += iface;
                _byIfIndex.insert(static_cast<unsigned int>(iface.index()), iface);
        }
        interfaceAddedSignal.emit(iface);
}

void MdnsManager::onInterfaceDisappeared(const NetworkInterface &iface) {
        if (!_active.value()) return;

        // Trim our bookkeeping only.  We intentionally do NOT call
        // @ref UdpSocket::leaveMulticastGroup here: that API takes
        // no interface argument and passes @c INADDR_ANY to
        // @c IP_DROP_MEMBERSHIP, which lets the kernel pick "an
        // appropriate interface" (per ip(7)) — almost certainly the
        // wrong one when several interfaces are joined on the same
        // socket.  When the interface itself disappears (cable
        // pulled, device removed, link down) Linux purges the
        // associated membership automatically; for the stale-bookkeeping
        // case the membership simply lingers until @ref closeAndLeave
        // closes the socket.  Pass B.2's per-interface sockets
        // collapse this asymmetry away.
        bool wasJoined = false;
        {
                Mutex::Locker lock(_interfacesMtx);
                for (size_t i = 0; i < _joinedInterfaces.size(); ++i) {
                        if (_joinedInterfaces[i] == iface) {
                                _joinedInterfaces.remove(i);
                                wasJoined = true;
                                break;
                        }
                }
                _byIfIndex.remove(static_cast<unsigned int>(iface.index()));
        }
        if (!wasJoined) return;
        interfaceRemovedSignal.emit(iface);
}

PROMEKI_NAMESPACE_END
