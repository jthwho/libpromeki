/**
 * @file      dnsresolver.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Resolver-level tests.  Each scenario spins up a single-thread mock
 * DNS server on @c 127.0.0.1:<ephemeral>, points the resolver at it,
 * and validates the end-to-end behaviour (cache, CNAME chase,
 * NXDOMAIN, server failover, IP-literal shortcut, ...).  The mock
 * server runs on its own @ref Thread so the test driver's
 * @ref EventLoop can block on the resolver's sync wrapper without
 * stalling the responder.
 */

#include <cstdint>
#include <doctest/doctest.h>
#include <promeki/atomic.h>
#include <promeki/dnspacket.h>
#include <promeki/dnsrecord.h>
#include <promeki/dnsresolver.h>
#include <promeki/eventloop.h>
#include <promeki/function.h>
#include <promeki/ipv4address.h>
#include <promeki/list.h>
#include <promeki/networkinterface.h>
#include <promeki/objectbase.tpp>
#include <promeki/socketaddress.h>
#if PROMEKI_ENABLE_MDNS
#include <promeki/mdnsmanager.h>
#include <promeki/mdnsrecord.h>
#endif

// Needed for the mock server's getsockname() / ntohs() calls.
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <promeki/string.h>
#include <promeki/tcpserver.h>
#include <promeki/tcpsocket.h>
#include <promeki/thread.h>
#include <promeki/udpsocket.h>

using namespace promeki;

namespace {

// Handler signature for the mock server.  Returns one or more
// response packets to send back to the requester, or an empty list
// to drop the query silently (useful for timeout tests).
using MockHandler =
    Function<List<Buffer>(const DnsPacket &question, const SocketAddress &from)>;

class MockDnsServer : public Thread {
        public:
                explicit MockDnsServer(MockHandler handler)
                    : _handler(std::move(handler)) {
                        setName("MockDnsServer");
                }

                ~MockDnsServer() override {
                        if (isRunning()) {
                                threadEventLoop()->quit();
                                wait();
                        }
                }

                uint16_t boundPort() const { return _boundPort.value(); }

                Error startAndBind() {
                        Error e = start();
                        if (e.isError()) return e;
                        // Wait for run() to publish the bound port.
                        for (int i = 0; i < 200; ++i) {
                                if (_boundPort.value() != 0) break;
                                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        }
                        if (_boundPort.value() == 0) return Error::Timeout;
                        return Error::Ok;
                }

        protected:
                void run() override {
                        UdpSocket sock(this);
                        if (sock.open(IODevice::ReadWrite).isError()) return;
                        if (sock.bind(SocketAddress(Ipv4Address::loopback(), 0)).isError()) return;
                        // Linux: report the bound port back to the
                        // caller.  We use socketDescriptor + getsockname.
                        struct sockaddr_in sa{};
                        socklen_t          len = sizeof(sa);
                        if (::getsockname(sock.socketDescriptor(),
                                          reinterpret_cast<struct sockaddr *>(&sa), &len) == 0) {
                                _boundPort.setValue(ntohs(sa.sin_port));
                        }

                        EventLoop *loop = threadEventLoop();
                        loop->addIoSource(sock.socketDescriptor(), EventLoop::IoRead,
                                          [this, &sock](int, uint32_t) {
                                                  uint8_t       buf[2048];
                                                  SocketAddress sender;
                                                  int64_t n = sock.readDatagram(buf, sizeof(buf), &sender);
                                                  if (n <= 0) return;
                                                  auto r = DnsPacket::parse(buf, static_cast<size_t>(n));
                                                  if (r.second().isError()) return;
                                                  List<Buffer> resps = _handler(r.first(), sender);
                                                  for (const Buffer &resp : resps) {
                                                          if (resp.size() == 0) continue;
                                                          sock.writeDatagram(resp, sender);
                                                  }
                                          });
                        loop->exec();
                }

        private:
                MockHandler        _handler;
                mutable Atomic<uint16_t> _boundPort{0};
};

// Helper: build a response packet for the given question carrying
// the given answer records.
Buffer makeResponse(const DnsPacket &question, uint8_t rcode,
                    const List<DnsRecord> &answers) {
        DnsPacket::Builder b;
        b.setTransactionId(question.transactionId())
         .setResponse(true)
         .setRecursionAvailable(true)
         .setRcode(rcode);
        for (const DnsQuestion &q : question.questions()) {
                b.addQuestion(q.name, static_cast<DnsRecord::Type>(q.type), q.klass);
        }
        for (const DnsRecord &a : answers) b.addAnswer(a);
        return b.finish();
}

// One-shot adoption of the calling thread so the resolver's
// @ref ObjectBase::eventLoop() returns non-null.  Two subtleties:
//  1. Earlier tests (e.g. @c DnsResolver::resolveSync(IP-literal))
//     construct a stack-local EventLoop that registers itself as
//     "current" on entry but does NOT restore the previous pointer
//     on destruction — see the @ref EventLoop docstring.  Adopting
//     after such a test runs would cache that stale pointer in the
//     Thread's @c _threadLoop, and the next @c processEvents on it
//     would dereference freed memory.  Construct a fresh EventLoop
//     before adopting so the captured pointer is to a long-lived
//     instance.
//  2. Static-local init guarantees this runs exactly once per
//     test-binary lifetime, no matter how many TEST_CASEs call it.
EventLoop *ensureTestLoop() {
        static EventLoop loop;
        static Thread   *adopted = Thread::adoptCurrentThread();
        (void)adopted;
        return &loop;
}

class ScopedThreadLoop {
        public:
                ScopedThreadLoop() : _loop(ensureTestLoop()) {}
                EventLoop *loop() const { return _loop; }
        private:
                EventLoop *_loop = nullptr;
};

} // anonymous namespace

TEST_CASE("DnsResolver::resolveSync: IP-literal shortcut bypasses DNS") {
        auto r = DnsResolver::resolveSync(String("192.168.1.1"));
        REQUIRE(r.second().isOk());
        CHECK(r.first().isIPv4());
        CHECK(r.first().toIpv4() == Ipv4Address(192, 168, 1, 1));
}

TEST_CASE("DnsResolver::resolveSync: localhost short-circuits the lookup") {
        auto r = DnsResolver::resolveSync(String("localhost"));
        REQUIRE(r.second().isOk());
        CHECK(r.first().isResolved());
        CHECK(r.first().isLoopback());
}

TEST_CASE("DnsResolver::lookupSync: resolves an A record from a mock server") {
        MockDnsServer srv([](const DnsPacket &q, const SocketAddress &) {
                List<DnsRecord> answers;
                for (const DnsQuestion &qq : q.questions()) {
                        if (qq.type == static_cast<uint16_t>(DnsRecord::Type::A)) {
                                answers += DnsRecord::makeA(qq.name, Ipv4Address(203, 0, 113, 7),
                                                            Duration::fromSeconds(60));
                        }
                }
                return List<Buffer>{makeResponse(q, 0, answers)};
        });
        REQUIRE(srv.startAndBind().isOk());

        ScopedThreadLoop tloop; EventLoop &loop = *tloop.loop();
        DnsResolver res;
        List<SocketAddress> ns;
        ns += SocketAddress(Ipv4Address::loopback(), srv.boundPort());
        res.setNameservers(ns);

        // Drive directly through the resolver (not the global
        // lookupSync) so we keep the configured servers.
        DnsLookup *lk = res.lookup(String("test.example.com."), DnsRecordType::A);
        Atomic<bool> done; done.setValue(false);
        List<DnsRecord> records;
        Error          err;
        lk->answeredSignal.connect([&](List<DnsRecord> recs) {
                records = recs;
                done.setValue(true);
                loop.quit(0);
        }, &res);
        lk->failedSignal.connect([&](Error e) {
                err = e;
                done.setValue(true);
                loop.quit(0);
        }, &res);
        const TimeStamp deadline = TimeStamp::now() + Duration::fromSeconds(3);
        while (!done.value() && TimeStamp::now() < deadline) {
                loop.processEvents(EventLoop::WaitForMore, 50);
        }
        REQUIRE(done.value());
        CHECK(err.isOk());
        REQUIRE(records.size() == 1);
        CHECK(records[0].a == Ipv4Address(203, 0, 113, 7));
}

TEST_CASE("DnsResolver::lookupSync: NXDOMAIN maps to HostNotFound") {
        MockDnsServer srv([](const DnsPacket &q, const SocketAddress &) {
                return List<Buffer>{makeResponse(q, 3, List<DnsRecord>())};
        });
        REQUIRE(srv.startAndBind().isOk());

        ScopedThreadLoop tloop; EventLoop &loop = *tloop.loop();
        DnsResolver res;
        List<SocketAddress> ns;
        ns += SocketAddress(Ipv4Address::loopback(), srv.boundPort());
        res.setNameservers(ns);

        DnsLookup    *lk = res.lookup(String("nope.example.com."), DnsRecordType::A);
        Atomic<bool>  done; done.setValue(false);
        Error         err;
        lk->answeredSignal.connect([&](List<DnsRecord>) {
                done.setValue(true);
                loop.quit(0);
        }, &res);
        lk->failedSignal.connect([&](Error e) {
                err = e;
                done.setValue(true);
                loop.quit(0);
        }, &res);
        const TimeStamp deadline = TimeStamp::now() + Duration::fromSeconds(3);
        while (!done.value() && TimeStamp::now() < deadline) {
                loop.processEvents(EventLoop::WaitForMore, 50);
        }
        REQUIRE(done.value());
        CHECK(err == Error::HostNotFound);
}

TEST_CASE("DnsResolver: CNAME chase resolves through the alias") {
        MockDnsServer srv([](const DnsPacket &q, const SocketAddress &) {
                List<DnsRecord> answers;
                for (const DnsQuestion &qq : q.questions()) {
                        if (qq.type == static_cast<uint16_t>(DnsRecord::Type::A)) {
                                if (qq.name == String("www.example.com.")) {
                                        // Reply with just a CNAME — force the
                                        // resolver to chase.
                                        answers +=
                                            DnsRecord::makeCname(qq.name, String("example.com."),
                                                                 Duration::fromSeconds(60));
                                } else if (qq.name == String("example.com.")) {
                                        answers +=
                                            DnsRecord::makeA(qq.name, Ipv4Address(198, 51, 100, 2),
                                                             Duration::fromSeconds(60));
                                }
                        }
                }
                return List<Buffer>{makeResponse(q, 0, answers)};
        });
        REQUIRE(srv.startAndBind().isOk());

        ScopedThreadLoop tloop; EventLoop &loop = *tloop.loop();
        DnsResolver res;
        List<SocketAddress> ns;
        ns += SocketAddress(Ipv4Address::loopback(), srv.boundPort());
        res.setNameservers(ns);

        DnsLookup    *lk = res.lookup(String("www.example.com."), DnsRecordType::A);
        Atomic<bool>  done; done.setValue(false);
        List<DnsRecord> records;
        lk->answeredSignal.connect([&](List<DnsRecord> recs) {
                records = recs;
                done.setValue(true);
                loop.quit(0);
        }, &res);
        lk->failedSignal.connect([&](Error) {
                done.setValue(true);
                loop.quit(0);
        }, &res);
        const TimeStamp deadline = TimeStamp::now() + Duration::fromSeconds(3);
        while (!done.value() && TimeStamp::now() < deadline) {
                loop.processEvents(EventLoop::WaitForMore, 50);
        }
        REQUIRE(done.value());
        REQUIRE(records.size() == 1);
        CHECK(records[0].a == Ipv4Address(198, 51, 100, 2));
}

TEST_CASE("DnsResolver: server failover after timeout") {
        // First server: drops the query (returns no responses).
        // Second server: answers normally.
        MockDnsServer dead([](const DnsPacket &, const SocketAddress &) {
                return List<Buffer>();
        });
        MockDnsServer good([](const DnsPacket &q, const SocketAddress &) {
                List<DnsRecord> answers;
                for (const DnsQuestion &qq : q.questions()) {
                        answers += DnsRecord::makeA(qq.name, Ipv4Address(192, 0, 2, 99),
                                                    Duration::fromSeconds(30));
                }
                return List<Buffer>{makeResponse(q, 0, answers)};
        });
        REQUIRE(dead.startAndBind().isOk());
        REQUIRE(good.startAndBind().isOk());

        ScopedThreadLoop tloop; EventLoop &loop = *tloop.loop();
        DnsResolver res;
        List<SocketAddress> ns;
        ns += SocketAddress(Ipv4Address::loopback(), dead.boundPort());
        ns += SocketAddress(Ipv4Address::loopback(), good.boundPort());
        res.setNameservers(ns);
        DnsConfig cfg = res.config();
        cfg.setTimeout(Duration::fromMilliseconds(200));
        cfg.setAttempts(1);   // 1 attempt per server, two servers => two tries.
        res.setConfig(cfg);

        DnsLookup    *lk = res.lookup(String("failover.example.com."), DnsRecordType::A);
        Atomic<bool>  done; done.setValue(false);
        List<DnsRecord> records;
        Error          err;
        lk->answeredSignal.connect([&](List<DnsRecord> recs) {
                records = recs;
                done.setValue(true);
                loop.quit(0);
        }, &res);
        lk->failedSignal.connect([&](Error e) {
                err = e;
                done.setValue(true);
                loop.quit(0);
        }, &res);
        const TimeStamp deadline = TimeStamp::now() + Duration::fromSeconds(3);
        while (!done.value() && TimeStamp::now() < deadline) {
                loop.processEvents(EventLoop::WaitForMore, 50);
        }
        REQUIRE(done.value());
        CHECK(err.isOk());
        REQUIRE(records.size() == 1);
        CHECK(records[0].a == Ipv4Address(192, 0, 2, 99));
}

TEST_CASE("DnsResolver: cache hit avoids a second wire round-trip") {
        Atomic<int> queryCount; queryCount.setValue(0);
        MockDnsServer srv([&](const DnsPacket &q, const SocketAddress &) {
                queryCount.setValue(queryCount.value() + 1);
                List<DnsRecord> answers;
                for (const DnsQuestion &qq : q.questions()) {
                        answers += DnsRecord::makeA(qq.name, Ipv4Address(10, 1, 2, 3),
                                                    Duration::fromSeconds(60));
                }
                return List<Buffer>{makeResponse(q, 0, answers)};
        });
        REQUIRE(srv.startAndBind().isOk());

        ScopedThreadLoop tloop; EventLoop &loop = *tloop.loop();
        DnsResolver res;
        List<SocketAddress> ns;
        ns += SocketAddress(Ipv4Address::loopback(), srv.boundPort());
        res.setNameservers(ns);

        for (int i = 0; i < 3; ++i) {
                DnsLookup    *lk = res.lookup(String("hot.example.com."), DnsRecordType::A);
                Atomic<bool>  done; done.setValue(false);
                lk->answeredSignal.connect([&](List<DnsRecord>) {
                        done.setValue(true);
                        loop.quit(0);
                }, &res);
                lk->failedSignal.connect([&](Error) {
                        done.setValue(true);
                        loop.quit(0);
                }, &res);
                const TimeStamp deadline = TimeStamp::now() + Duration::fromSeconds(3);
                while (!done.value() && TimeStamp::now() < deadline) {
                        loop.processEvents(EventLoop::WaitForMore, 50);
                }
                REQUIRE(done.value());
        }
        // Only the first lookup goes to the wire; the next two come
        // out of the cache.
        CHECK(queryCount.value() == 1);
}

// ============================================================================
// TCP fallback test
// ============================================================================

// Mock server that:
//   - UDP path: serves a response with the TC (truncated) bit set
//     so the resolver promotes the question to TCP.
//   - TCP path: serves a length-prefixed answer with the same
//     question.
class TruncatedMockServer : public Thread {
        public:
                TruncatedMockServer() { setName("TruncatedMockServer"); }
                ~TruncatedMockServer() override {
                        if (isRunning()) {
                                threadEventLoop()->quit();
                                wait();
                        }
                }
                uint16_t boundPort() const { return _port.value(); }
                Error    startAndBind() {
                        Error e = start();
                        if (e.isError()) return e;
                        for (int i = 0; i < 200; ++i) {
                                if (_port.value() != 0) break;
                                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        }
                        return _port.value() != 0 ? Error::Ok : Error::Timeout;
                }
                int udpQueryCount() const { return _udpQueryCount.value(); }
                int tcpQueryCount() const { return _tcpQueryCount.value(); }

        protected:
                void run() override {
                        UdpSocket udp(this);
                        if (udp.open(IODevice::ReadWrite).isError()) return;
                        if (udp.bind(SocketAddress(Ipv4Address::loopback(), 0)).isError()) return;

                        // Reuse the bound port for TCP so the
                        // resolver's TCP fallback hits the same
                        // server.
                        struct sockaddr_in sa{};
                        socklen_t          len = sizeof(sa);
                        if (::getsockname(udp.socketDescriptor(),
                                          reinterpret_cast<struct sockaddr *>(&sa), &len) != 0) {
                                return;
                        }
                        const uint16_t port = ntohs(sa.sin_port);
                        TcpServer tcp(this);
                        if (tcp.listen(SocketAddress(Ipv4Address::loopback(), port)).isError()) return;
                        tcp.setNonBlocking(true);
                        _port.setValue(port);

                        EventLoop *loop = threadEventLoop();

                        // UDP read source.
                        loop->addIoSource(udp.socketDescriptor(), EventLoop::IoRead,
                                          [this, &udp](int, uint32_t) {
                                                  uint8_t       buf[2048];
                                                  SocketAddress sender;
                                                  int64_t n = udp.readDatagram(buf, sizeof(buf), &sender);
                                                  if (n <= 0) return;
                                                  _udpQueryCount.setValue(_udpQueryCount.value() + 1);
                                                  auto r = DnsPacket::parse(buf, static_cast<size_t>(n));
                                                  if (r.second().isError()) return;
                                                  // Build a truncated response: TC bit
                                                  // set + empty answer section.
                                                  DnsPacket::Builder b;
                                                  b.setTransactionId(r.first().transactionId())
                                                   .setResponse(true)
                                                   .setRecursionAvailable(true)
                                                   .setTruncated(true);
                                                  for (const DnsQuestion &q : r.first().questions()) {
                                                          b.addQuestion(q.name,
                                                                        static_cast<DnsRecord::Type>(q.type),
                                                                        q.klass);
                                                  }
                                                  Buffer resp = b.finish();
                                                  udp.writeDatagram(resp, sender);
                                          });

                        // Loops over @ref TcpSocket::read until @p want
                        // bytes have been read.  TcpSocket::read returns
                        // whatever is in the kernel buffer (often less
                        // than the request on a fresh connection); the
                        // helper papers over that to match the
                        // expectations of test code below.
                        auto readExact = [](TcpSocket *s, uint8_t *out, size_t want) {
                                size_t got = 0;
                                while (got < want) {
                                        int64_t n =
                                            s->read(out + got, static_cast<int64_t>(want - got));
                                        if (n <= 0) return false;
                                        got += static_cast<size_t>(n);
                                }
                                return true;
                        };

                        // TCP accept source.
                        loop->addIoSource(tcp.socketDescriptor(), EventLoop::IoRead,
                                          [this, &tcp, readExact](int, uint32_t) {
                                                  TcpSocket *cli = tcp.nextPendingConnection();
                                                  if (cli == nullptr) return;
                                                  _tcpQueryCount.setValue(_tcpQueryCount.value() + 1);
                                                  uint8_t prefix[2];
                                                  if (!readExact(cli, prefix, 2)) {
                                                          delete cli;
                                                          return;
                                                  }
                                                  const size_t want =
                                                      (static_cast<size_t>(prefix[0]) << 8) | prefix[1];
                                                  List<uint8_t> req;
                                                  req.resize(want);
                                                  if (!readExact(cli, &req[0], want)) {
                                                          delete cli;
                                                          return;
                                                  }
                                                  auto r = DnsPacket::parse(&req[0], want);
                                                  if (r.second().isError()) {
                                                          delete cli;
                                                          return;
                                                  }
                                                  // Build a clean (un-truncated) response.
                                                  DnsPacket::Builder b;
                                                  b.setTransactionId(r.first().transactionId())
                                                   .setResponse(true)
                                                   .setRecursionAvailable(true);
                                                  for (const DnsQuestion &q : r.first().questions()) {
                                                          b.addQuestion(q.name,
                                                                        static_cast<DnsRecord::Type>(q.type),
                                                                        q.klass);
                                                          b.addAnswer(DnsRecord::makeA(q.name,
                                                                                       Ipv4Address(192, 0, 2, 88),
                                                                                       Duration::fromSeconds(60)));
                                                  }
                                                  Buffer resp = b.finish();
                                                  uint8_t pre[2];
                                                  pre[0] = static_cast<uint8_t>((resp.size() >> 8) & 0xFF);
                                                  pre[1] = static_cast<uint8_t>(resp.size() & 0xFF);
                                                  cli->write(pre, 2);
                                                  cli->write(resp.data(), resp.size());
                                                  delete cli;
                                          });

                        loop->exec();
                }

        private:
                Atomic<uint16_t> _port{0};
                Atomic<int>      _udpQueryCount{0};
                Atomic<int>      _tcpQueryCount{0};
};

TEST_CASE("DnsResolver: TC bit promotes the query to TCP") {
        TruncatedMockServer srv;
        REQUIRE(srv.startAndBind().isOk());

        ScopedThreadLoop tloop; EventLoop &loop = *tloop.loop();
        DnsResolver res;
        List<SocketAddress> ns;
        ns += SocketAddress(Ipv4Address::loopback(), srv.boundPort());
        res.setNameservers(ns);

        DnsLookup    *lk = res.lookup(String("tc.example.com."), DnsRecordType::A);
        Atomic<bool>  done; done.setValue(false);
        List<DnsRecord> records;
        Error          err;
        lk->answeredSignal.connect([&](List<DnsRecord> recs) {
                records = recs;
                done.setValue(true);
                loop.quit(0);
        }, &res);
        lk->failedSignal.connect([&](Error e) {
                err = e;
                done.setValue(true);
                loop.quit(0);
        }, &res);
        const TimeStamp deadline = TimeStamp::now() + Duration::fromSeconds(5);
        while (!done.value() && TimeStamp::now() < deadline) {
                loop.processEvents(EventLoop::WaitForMore, 50);
        }
        REQUIRE(done.value());
        CHECK(err.isOk());
        REQUIRE(records.size() == 1);
        CHECK(records[0].a == Ipv4Address(192, 0, 2, 88));
        // Sanity: we hit both transports.
        CHECK(srv.udpQueryCount() >= 1);
        CHECK(srv.tcpQueryCount() >= 1);
}

// ============================================================================
// Sync-wrapper timeout test
// ============================================================================
TEST_CASE("DnsResolver: lookup fails with Timeout when server never responds") {
        MockDnsServer srv([](const DnsPacket &, const SocketAddress &) {
                return List<Buffer>();   // never reply
        });
        REQUIRE(srv.startAndBind().isOk());

        // Drive the resolver's own loop (the adopted thread's
        // EventLoop) so the per-attempt timer fires.  A stack-local
        // EventLoop here would pollute the thread's @c current
        // pointer for every subsequent test in the file — see the
        // long comment on @ref ensureTestLoop above.
        ScopedThreadLoop tloop; EventLoop &loop = *tloop.loop();
        DnsResolver res;
        List<SocketAddress> ns;
        ns += SocketAddress(Ipv4Address::loopback(), srv.boundPort());
        res.setNameservers(ns);
        DnsConfig cfg = res.config();
        cfg.setTimeout(Duration::fromMilliseconds(150));
        cfg.setAttempts(1);
        res.setConfig(cfg);

        DnsLookup *lk = res.lookup(String("silent.example.com."), DnsRecordType::A);
        Atomic<bool> done; done.setValue(false);
        Error        err;
        lk->answeredSignal.connect([&](List<DnsRecord>) {
                done.setValue(true);
                loop.quit(0);
        }, &res);
        lk->failedSignal.connect([&](Error e) {
                err = e;
                done.setValue(true);
                loop.quit(0);
        }, &res);
        const TimeStamp deadline = TimeStamp::now() + Duration::fromSeconds(2);
        while (!done.value() && TimeStamp::now() < deadline) {
                loop.processEvents(EventLoop::WaitForMore, 25);
        }
        REQUIRE(done.value());
        CHECK(err == Error::Timeout);
}

// ============================================================================
// Multi-answer A test
// ============================================================================
TEST_CASE("DnsResolver: returns every record in a multi-answer RRset") {
        MockDnsServer srv([](const DnsPacket &q, const SocketAddress &) {
                List<DnsRecord> answers;
                for (const DnsQuestion &qq : q.questions()) {
                        answers += DnsRecord::makeA(qq.name, Ipv4Address(10, 0, 0, 1),
                                                    Duration::fromSeconds(60));
                        answers += DnsRecord::makeA(qq.name, Ipv4Address(10, 0, 0, 2),
                                                    Duration::fromSeconds(60));
                        answers += DnsRecord::makeA(qq.name, Ipv4Address(10, 0, 0, 3),
                                                    Duration::fromSeconds(60));
                }
                return List<Buffer>{makeResponse(q, 0, answers)};
        });
        REQUIRE(srv.startAndBind().isOk());

        ScopedThreadLoop tloop; EventLoop &loop = *tloop.loop();
        DnsResolver res;
        List<SocketAddress> ns;
        ns += SocketAddress(Ipv4Address::loopback(), srv.boundPort());
        res.setNameservers(ns);

        DnsLookup    *lk = res.lookup(String("rr.example.com."), DnsRecordType::A);
        Atomic<bool>  done; done.setValue(false);
        List<DnsRecord> records;
        lk->answeredSignal.connect([&](List<DnsRecord> recs) {
                records = recs;
                done.setValue(true);
                loop.quit(0);
        }, &res);
        lk->failedSignal.connect([&](Error) {
                done.setValue(true);
                loop.quit(0);
        }, &res);
        const TimeStamp deadline = TimeStamp::now() + Duration::fromSeconds(3);
        while (!done.value() && TimeStamp::now() < deadline) {
                loop.processEvents(EventLoop::WaitForMore, 50);
        }
        REQUIRE(done.value());
        REQUIRE(records.size() == 3);
        CHECK(records[0].a == Ipv4Address(10, 0, 0, 1));
        CHECK(records[1].a == Ipv4Address(10, 0, 0, 2));
        CHECK(records[2].a == Ipv4Address(10, 0, 0, 3));
}

// ============================================================================
// mDNS (.local.) routing test
// ============================================================================
#if PROMEKI_ENABLE_MDNS
TEST_CASE("DnsResolver: .local hostnames resolve via MdnsManager") {
        // Bring up a private MdnsManager on the loopback interface
        // and pin it as the resolver's mDNS engine.  Publish a
        // synthetic A record for "promeki-test-host.local." via a
        // small announcer; the resolver should pick it up via the
        // multicast group join on lo.
        MdnsManager mgr;
        // The test thread is adopted (so the resolver's
        // eventLoop() is non-null), which means the manager's
        // ObjectBase captured the adopted Thread at construction
        // time.  @ref MdnsManager::run unconditionally calls
        // @ref ObjectBase::moveToThread(this), which asserts on
        // @c _thread @c == @c nullptr or the worker thread.  Clear
        // the affinity so the worker can take it.
        mgr.moveToThread(nullptr);
        mgr.setAutoTrackInterfaces(false);
        NetworkInterface lo;
        for (const NetworkInterface &iface : NetworkInterface::enumerate()) {
                if (iface.isLoopback() && iface.isUp()) { lo = iface; break; }
        }
        REQUIRE(lo.isValid());
        NetworkInterface::List ifaces;
        ifaces += lo;
        Error startErr = mgr.start(ifaces);
        if (startErr.isError()) {
                MESSAGE("MdnsManager start failed (CI/sandbox?): " << startErr.name().cstr());
                return;
        }

        // Quick-and-dirty publisher: write one announce packet
        // carrying the target A record to the multicast group, then
        // wait for the resolver to come up and ask.  We use the
        // MdnsManager's own announce-build helper.
        const String hostName(R"(promeki-test-host.local.)");
        List<MdnsRecord> recs;
        recs += MdnsRecord::a(hostName, Ipv4Address(127, 0, 0, 99),
                              Duration::fromSeconds(60));
        Buffer announce = mdnsBuildAnnounce(recs);

        // Set up the resolver pointed at our private manager.
        ScopedThreadLoop tloop; EventLoop &loop = *tloop.loop();
        DnsResolver res;
        res.setMdnsManager(&mgr);

        DnsLookup    *lk = res.lookup(hostName, DnsRecordType::A);
        Atomic<bool>  done; done.setValue(false);
        List<DnsRecord> records;
        Error          err;
        lk->answeredSignal.connect([&](List<DnsRecord> recs2) {
                records = recs2;
                done.setValue(true);
                loop.quit(0);
        }, &res);
        lk->failedSignal.connect([&](Error e) {
                err = e;
                done.setValue(true);
                loop.quit(0);
        }, &res);

        // Re-broadcast the announce a few times to cover any race
        // between the resolver's observer registration and the
        // first multicast hit.
        const TimeStamp deadline = TimeStamp::now() + Duration::fromSeconds(3);
        TimeStamp       nextAnnounce = TimeStamp::now();
        while (!done.value() && TimeStamp::now() < deadline) {
                if (TimeStamp::now() >= nextAnnounce) {
                        mgr.writeMulticast(announce, /*ipv6=*/false);
                        nextAnnounce = TimeStamp::now() + Duration::fromMilliseconds(100);
                }
                loop.processEvents(EventLoop::WaitForMore, 25);
        }

        // CI sandboxes sometimes block multicast on loopback (rare
        // but observed).  If we didn't hear back, log a MESSAGE and
        // skip — failing the test in that environment would be a
        // false positive.
        if (!done.value()) {
                MESSAGE("multicast not delivered on loopback — skipping");
                lk->cancel();
                return;
        }
        CHECK(err.isOk());
        REQUIRE(records.size() >= 1);
        CHECK(records[0].a == Ipv4Address(127, 0, 0, 99));
        CHECK(records[0].name == hostName);
}
#endif

// ============================================================================
// RFC 5452 §9 — drop responses whose question section doesn't match
// ============================================================================
TEST_CASE("DnsResolver: drops responses with a wrong question section") {
        // Mock server returns a valid-looking response, but its
        // Question section names a different host than the
        // resolver asked about.  The resolver must NOT accept the
        // included A record — it should time out instead.
        MockDnsServer srv([](const DnsPacket &q, const SocketAddress &) {
                DnsPacket::Builder b;
                b.setTransactionId(q.transactionId())
                 .setResponse(true)
                 .setRecursionAvailable(true)
                 .setRcode(0);
                // Echo a different question + a record for THAT
                // bogus name.  The resolver should not bind these
                // to the actual outstanding query.
                b.addQuestion(String("evil.example.com."), DnsRecord::Type::A);
                b.addAnswer(DnsRecord::makeA(String("evil.example.com."),
                                             Ipv4Address(6, 6, 6, 6),
                                             Duration::fromSeconds(60)));
                return List<Buffer>{b.finish()};
        });
        REQUIRE(srv.startAndBind().isOk());

        ScopedThreadLoop tloop; EventLoop &loop = *tloop.loop();
        DnsResolver res;
        List<SocketAddress> ns;
        ns += SocketAddress(Ipv4Address::loopback(), srv.boundPort());
        res.setNameservers(ns);
        DnsConfig cfg = res.config();
        cfg.setTimeout(Duration::fromMilliseconds(200));
        cfg.setAttempts(1);
        res.setConfig(cfg);

        DnsLookup    *lk = res.lookup(String("good.example.com."), DnsRecordType::A);
        Atomic<bool>  done; done.setValue(false);
        Error          err;
        lk->answeredSignal.connect([&](List<DnsRecord>) {
                done.setValue(true);
                loop.quit(0);
        }, &res);
        lk->failedSignal.connect([&](Error e) {
                err = e;
                done.setValue(true);
                loop.quit(0);
        }, &res);
        const TimeStamp deadline = TimeStamp::now() + Duration::fromSeconds(2);
        while (!done.value() && TimeStamp::now() < deadline) {
                loop.processEvents(EventLoop::WaitForMore, 25);
        }
        REQUIRE(done.value());
        // The bogus response was dropped, no other response came,
        // so the resolver times out — proving the wrong-question
        // record did NOT slip into the answer path.
        CHECK(err == Error::Timeout);
}

// ============================================================================
// mDNS routing opt-out
// ============================================================================
TEST_CASE("DnsResolver: setMdnsRoutingEnabled(false) sends .local via unicast") {
        // Mock unicast server returns NXDOMAIN for everything.  With
        // mDNS routing disabled, a .local lookup should reach the
        // mock (proving the unicast path was taken) and surface as
        // HostNotFound — not get silently routed to the mDNS
        // engine (which would still be unstarted in this test).
        Atomic<int> sawQuery; sawQuery.setValue(0);
        MockDnsServer srv([&](const DnsPacket &q, const SocketAddress &) {
                sawQuery.setValue(sawQuery.value() + 1);
                return List<Buffer>{makeResponse(q, /*rcode=*/3, List<DnsRecord>())};
        });
        REQUIRE(srv.startAndBind().isOk());

        ScopedThreadLoop tloop; EventLoop &loop = *tloop.loop();
        DnsResolver res;
        res.setMdnsRoutingEnabled(false);
        CHECK_FALSE(res.mdnsRoutingEnabled());
        List<SocketAddress> ns;
        ns += SocketAddress(Ipv4Address::loopback(), srv.boundPort());
        res.setNameservers(ns);

        DnsLookup    *lk = res.lookup(String("opted-out.local."), DnsRecordType::A);
        Atomic<bool>  done; done.setValue(false);
        Error          err;
        lk->answeredSignal.connect([&](List<DnsRecord>) {
                done.setValue(true);
                loop.quit(0);
        }, &res);
        lk->failedSignal.connect([&](Error e) {
                err = e;
                done.setValue(true);
                loop.quit(0);
        }, &res);
        const TimeStamp deadline = TimeStamp::now() + Duration::fromSeconds(2);
        while (!done.value() && TimeStamp::now() < deadline) {
                loop.processEvents(EventLoop::WaitForMore, 25);
        }
        REQUIRE(done.value());
        CHECK(err == Error::HostNotFound);
        // Unicast server actually saw the .local query — proves
        // routing was bypassed.
        CHECK(sawQuery.value() >= 1);
}
