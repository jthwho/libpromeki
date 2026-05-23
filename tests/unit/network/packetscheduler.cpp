/**
 * @file      packetscheduler.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/burstpacketscheduler.h>
#include <promeki/cadencepacketscheduler.h>
#include <promeki/duration.h>
#include <promeki/enums.h>
#include <promeki/kernelfqpacketscheduler.h>
#include <promeki/list.h>
#include <promeki/packetscheduler.h>
#include <promeki/packettransport.h>
#include <promeki/timestamp.h>
#include <promeki/txtimepacketscheduler.h>

using namespace promeki;

namespace {

/// In-memory transport stub.  Captures every datagram for inspection
/// — every test that needs to verify scheduler output round-trips
/// through this stub rather than the kernel.
class StubTransport : public PacketTransport {
        public:
                struct Captured {
                                size_t        size = 0;
                                SocketAddress dest;
                                uint64_t      txTimeNs = 0;
                                int64_t       enqueuedNs = 0;
                };

                Error open() override {
                        _open = true;
                        return Error::Ok;
                }

                void close() override { _open = false; }

                bool isOpen() const override { return _open; }

                ssize_t sendPacket(const void *, size_t size, const SocketAddress &dest) override {
                        if (!_open) return -1;
                        Captured c;
                        c.size = size;
                        c.dest = dest;
                        c.enqueuedNs = TimeStamp::now().nanoseconds();
                        captured.pushToBack(c);
                        return static_cast<ssize_t>(size);
                }

                int sendPackets(const DatagramList &datagrams) override {
                        if (!_open) return -1;
                        if (acceptLimit >= 0 &&
                            static_cast<int>(datagrams.size()) > acceptLimit) {
                                for (int i = 0; i < acceptLimit; i++) capture(datagrams[i]);
                                int n = acceptLimit;
                                acceptLimit = -1; // one-shot
                                return n;
                        }
                        for (size_t i = 0; i < datagrams.size(); i++) capture(datagrams[i]);
                        return static_cast<int>(datagrams.size());
                }

                ssize_t receivePacket(void *, size_t, SocketAddress *) override { return -1; }

                Error setPacingRate(uint64_t bytesPerSec) override {
                        pacingRate = bytesPerSec;
                        pacingRateCalls++;
                        return Error::Ok;
                }

                Error setTxTime(bool enable) override {
                        if (!txTimeSupported) return Error::NotSupported;
                        txTimeEnabled = enable;
                        return Error::Ok;
                }

                ::promeki::List<Captured> captured;
                uint64_t                  pacingRate = 0;
                int                       pacingRateCalls = 0;
                bool                      txTimeEnabled = false;
                bool                      txTimeSupported = true;
                int                       acceptLimit = -1; // -1 = unlimited
                bool                      _open = false;

        private:
                void capture(const Datagram &d) {
                        Captured c;
                        c.size = d.size;
                        c.dest = d.dest;
                        c.txTimeNs = d.txTimeNs;
                        c.enqueuedNs = TimeStamp::now().nanoseconds();
                        captured.pushToBack(c);
                }
};

PacketTransport::DatagramList makeBatch(size_t n, const SocketAddress &dest, const char *payload) {
        PacketTransport::DatagramList dgs;
        dgs.reserve(n);
        for (size_t i = 0; i < n; i++) {
                PacketTransport::Datagram d;
                d.data = payload;
                d.size = 16;
                d.dest = dest;
                dgs.pushToBack(d);
        }
        return dgs;
}

} // namespace

TEST_CASE("PacketScheduler factory") {

        StubTransport transport;
        transport.open();

        SUBCASE("None → BurstPacketScheduler") {
                auto s = PacketScheduler::create(Enum(RtpPacingMode::None), &transport);
                REQUIRE(s.isValid());
                CHECK(dynamic_cast<BurstPacketScheduler *>(s.ptr()) != nullptr);
                CHECK(s->transport() == &transport);
        }

        SUBCASE("Userspace → CadencePacketScheduler") {
                auto s = PacketScheduler::create(Enum(RtpPacingMode::Userspace), &transport);
                REQUIRE(s.isValid());
                CHECK(dynamic_cast<CadencePacketScheduler *>(s.ptr()) != nullptr);
        }

        SUBCASE("KernelFq → KernelFqPacketScheduler") {
                auto s = PacketScheduler::create(Enum(RtpPacingMode::KernelFq), &transport);
                REQUIRE(s.isValid());
                CHECK(dynamic_cast<KernelFqPacketScheduler *>(s.ptr()) != nullptr);
        }

        SUBCASE("TxTime → TxTimePacketScheduler") {
                auto s = PacketScheduler::create(Enum(RtpPacingMode::TxTime), &transport);
                REQUIRE(s.isValid());
                CHECK(dynamic_cast<TxTimePacketScheduler *>(s.ptr()) != nullptr);
        }

        SUBCASE("Auto resolves to a concrete scheduler") {
                auto s = PacketScheduler::create(Enum(RtpPacingMode::Auto), &transport);
                REQUIRE(s.isValid());
                CHECK(s->transport() == &transport);
        }
}

TEST_CASE("BurstPacketScheduler") {

        StubTransport transport;
        transport.open();
        BurstPacketScheduler sched;
        sched.setTransport(&transport);

        SUBCASE("dispatches the whole batch synchronously") {
                auto batch = makeBatch(5, SocketAddress::localhost(5004), "payload-xxxxxxxx");
                int  n = sched.enqueue(batch);
                CHECK(n == 5);
                CHECK(transport.captured.size() == 5);
        }

        SUBCASE("loops on partial accept") {
                auto batch = makeBatch(6, SocketAddress::localhost(5004), "payload-xxxxxxxx");
                transport.acceptLimit = 2;
                int n = sched.enqueue(batch);
                CHECK(n == 6);
                CHECK(transport.captured.size() == 6);
        }

        SUBCASE("predictedTxDelayUs is zero") {
                CHECK(sched.predictedTxDelayUs() == 0);
        }

        SUBCASE("enqueue without transport returns -1") {
                BurstPacketScheduler bare;
                auto                 batch = makeBatch(1, SocketAddress::localhost(5004), "xxxxxxxxxxxxxxxx");
                CHECK(bare.enqueue(batch) == -1);
        }
}

TEST_CASE("CadencePacketScheduler PerBatch") {

        StubTransport          transport;
        transport.open();
        CadencePacketScheduler sched;
        sched.setTransport(&transport);
        sched.setCadenceMode(PacketScheduler::CadenceMode::PerBatch);

        SUBCASE("spreads N packets across configured frame interval") {
                PacketScheduler::Spec spec;
                spec.frameInterval = Duration::fromMilliseconds(20); // 50fps frame
                REQUIRE(sched.configure(spec).isOk());

                auto      batch = makeBatch(4, SocketAddress::localhost(5004), "video-payloadxxx");
                TimeStamp before = TimeStamp::now();
                int       n = sched.enqueue(batch);
                TimeStamp after = TimeStamp::now();

                CHECK(n == 4);
                REQUIRE(transport.captured.size() == 4);
                // Per-packet interval = 20ms / 4 = 5ms.  Cadence
                // anchors at now() inside enqueue, so packet 0 leaves
                // immediately; packets 1..3 sleep one stride each.
                // Lower bound is therefore 3 × 5ms = 15ms.
                int64_t elapsedUs = (after - before).microseconds();
                CHECK(elapsedUs >= 14000);
                // Packets land in monotone order.
                for (size_t i = 1; i < transport.captured.size(); i++) {
                        CHECK(transport.captured[i].enqueuedNs > transport.captured[i - 1].enqueuedNs);
                }
        }

        SUBCASE("burst on missing frameInterval") {
                PacketScheduler::Spec spec; // frameInterval default = invalid
                REQUIRE(sched.configure(spec).isOk());
                auto      batch = makeBatch(3, SocketAddress::localhost(5004), "xxxxxxxxxxxxxxxx");
                TimeStamp before = TimeStamp::now();
                int       n = sched.enqueue(batch);
                TimeStamp after = TimeStamp::now();
                CHECK(n == 3);
                // No frameInterval → no sleep.
                CHECK((after - before).microseconds() < 5000);
        }

        SUBCASE("predictedTxDelayUs scales with packetsPerFrame") {
                PacketScheduler::Spec spec;
                spec.frameInterval = Duration::fromMilliseconds(20);
                spec.packetsPerFrame = 10;
                REQUIRE(sched.configure(spec).isOk());
                // 9 / 10 of frame interval = 18ms = 18000 µs.
                CHECK(sched.predictedTxDelayUs() == 18000);
        }
}

TEST_CASE("CadencePacketScheduler Streamwide") {

        StubTransport          transport;
        transport.open();
        CadencePacketScheduler sched;
        sched.setTransport(&transport);
        sched.setCadenceMode(PacketScheduler::CadenceMode::Streamwide);

        SUBCASE("sleeps between successive single-packet enqueues") {
                PacketScheduler::Spec spec;
                spec.frameInterval = Duration::fromMilliseconds(5); // audio ptime-ish
                REQUIRE(sched.configure(spec).isOk());

                auto      batch = makeBatch(1, SocketAddress::localhost(5004), "audio-1-xxxxxxxx");
                TimeStamp before = TimeStamp::now();
                CHECK(sched.enqueue(batch) == 1);
                CHECK(sched.enqueue(batch) == 1);
                CHECK(sched.enqueue(batch) == 1);
                int64_t elapsedUs = (TimeStamp::now() - before).microseconds();
                // Three calls, three ticks → at least 2 × 5ms wait
                // (first tick is "now" because anchor is set on first
                // call).
                CHECK(elapsedUs >= 10000);
                CHECK(transport.captured.size() == 3);
        }

        SUBCASE("predictedTxDelayUs is zero (cadence drives every emission)") {
                PacketScheduler::Spec spec;
                spec.frameInterval = Duration::fromMilliseconds(5);
                REQUIRE(sched.configure(spec).isOk());
                CHECK(sched.predictedTxDelayUs() == 0);
        }
}

TEST_CASE("KernelFqPacketScheduler") {

        StubTransport            transport;
        transport.open();
        KernelFqPacketScheduler  sched;
        sched.setTransport(&transport);

        SUBCASE("configure forwards bytesPerSec to transport") {
                PacketScheduler::Spec spec;
                spec.bytesPerSec = 1'250'000; // 10 Mbps
                spec.frameInterval = Duration::fromMilliseconds(20);
                REQUIRE(sched.configure(spec).isOk());
                CHECK(transport.pacingRate == 1'250'000);
                CHECK(transport.pacingRateCalls == 1);
        }

        SUBCASE("setRate deduplicates repeat values") {
                PacketScheduler::Spec spec;
                spec.bytesPerSec = 1'000'000;
                REQUIRE(sched.configure(spec).isOk());
                CHECK(transport.pacingRateCalls == 1);
                CHECK(sched.setRate(1'000'000).isOk());
                // Same value → no extra setsockopt.
                CHECK(transport.pacingRateCalls == 1);
                CHECK(sched.setRate(2'000'000).isOk());
                CHECK(transport.pacingRateCalls == 2);
                CHECK(transport.pacingRate == 2'000'000);
        }

        SUBCASE("enqueue passes through unpaced (kernel handles spread)") {
                PacketScheduler::Spec spec;
                spec.bytesPerSec = 10'000'000;
                REQUIRE(sched.configure(spec).isOk());
                auto      batch = makeBatch(4, SocketAddress::localhost(5004), "payload-xxxxxxxx");
                TimeStamp before = TimeStamp::now();
                int       n = sched.enqueue(batch);
                TimeStamp after = TimeStamp::now();
                CHECK(n == 4);
                // No userspace sleep → fast.
                CHECK((after - before).microseconds() < 5000);
        }

        SUBCASE("predictedTxDelayUs returns frameInterval when both set") {
                PacketScheduler::Spec spec;
                spec.bytesPerSec = 1'000'000;
                spec.frameInterval = Duration::fromMilliseconds(20);
                REQUIRE(sched.configure(spec).isOk());
                CHECK(sched.predictedTxDelayUs() == 20000);
        }
}

TEST_CASE("TxTimePacketScheduler") {

        StubTransport         transport;
        transport.open();
        TxTimePacketScheduler sched;
        sched.setTransport(&transport);

        SUBCASE("configure enables transport txtime") {
                PacketScheduler::Spec spec;
                spec.frameInterval = Duration::fromMilliseconds(20);
                REQUIRE(sched.configure(spec).isOk());
                CHECK(transport.txTimeEnabled);
        }

        SUBCASE("PerBatch stamps monotone txTimeNs on every datagram") {
                PacketScheduler::Spec spec;
                spec.frameInterval = Duration::fromMilliseconds(20);
                REQUIRE(sched.configure(spec).isOk());

                auto batch = makeBatch(5, SocketAddress::localhost(5004), "video-payloadxxx");
                int  n = sched.enqueue(batch);
                CHECK(n == 5);
                REQUIRE(transport.captured.size() == 5);
                for (size_t i = 0; i < transport.captured.size(); i++) {
                        CHECK(transport.captured[i].txTimeNs > 0);
                }
                for (size_t i = 1; i < transport.captured.size(); i++) {
                        CHECK(transport.captured[i].txTimeNs > transport.captured[i - 1].txTimeNs);
                }
                // Stride = 20ms / 5 = 4ms = 4 000 000 ns.
                const uint64_t stride = transport.captured[1].txTimeNs -
                                        transport.captured[0].txTimeNs;
                CHECK(stride == 4'000'000);
        }

        SUBCASE("pre-stamped txTimeNs bypasses scheduler-side cadence") {
                // ST 2110-40 §6.4 LLTM path:
                // @ref RtpSession::sendPackets stamps every datagram's
                // @c txTimeNs from @c batch.deadlineTaiNs.  The
                // scheduler must respect that and pass through —
                // re-computing a cadence deadline would clobber the
                // caller's LLTM deadline.
                PacketScheduler::Spec spec;
                spec.frameInterval = Duration::fromMilliseconds(20);
                REQUIRE(sched.configure(spec).isOk());

                auto batch = makeBatch(4, SocketAddress::localhost(5004), "lltm-payloadxxxx");
                const uint64_t preDeadline = 1'234'567'890'000'000ULL;
                for (size_t i = 0; i < batch.size(); i++) {
                        batch[i].txTimeNs = preDeadline;
                }
                int n = sched.enqueue(batch);
                CHECK(n == 4);
                REQUIRE(transport.captured.size() == 4);
                // Every captured datagram must carry the caller's
                // deadline verbatim — no scheduler-side stride math.
                for (size_t i = 0; i < transport.captured.size(); i++) {
                        CHECK(transport.captured[i].txTimeNs == preDeadline);
                }
        }

        SUBCASE("zero-stamped datagrams still get scheduler-derived cadence") {
                // Backward compat: callers that don't engage the LLTM
                // path leave @c txTimeNs at 0 and the scheduler runs
                // its existing PerBatch cadence math.
                PacketScheduler::Spec spec;
                spec.frameInterval = Duration::fromMilliseconds(20);
                REQUIRE(sched.configure(spec).isOk());

                auto batch = makeBatch(4, SocketAddress::localhost(5004), "no-lltm-payloadx");
                for (size_t i = 0; i < batch.size(); i++) {
                        CHECK(batch[i].txTimeNs == 0);
                }
                CHECK(sched.enqueue(batch) == 4);
                // All datagrams now have non-zero, monotone deadlines.
                REQUIRE(transport.captured.size() == 4);
                for (size_t i = 0; i < transport.captured.size(); i++) {
                        CHECK(transport.captured[i].txTimeNs > 0);
                }
                for (size_t i = 1; i < transport.captured.size(); i++) {
                        CHECK(transport.captured[i].txTimeNs >
                              transport.captured[i - 1].txTimeNs);
                }
        }

        SUBCASE("transport without SO_TXTIME degrades to burst") {
                StubTransport         t;
                t.open();
                t.txTimeSupported = false;
                TxTimePacketScheduler s;
                s.setTransport(&t);
                PacketScheduler::Spec spec;
                spec.frameInterval = Duration::fromMilliseconds(20);
                REQUIRE(s.configure(spec).isOk());
                CHECK_FALSE(t.txTimeEnabled);
                auto batch = makeBatch(3, SocketAddress::localhost(5004), "xxxxxxxxxxxxxxxx");
                CHECK(s.enqueue(batch) == 3);
                // Burst: no txTimeNs stamping.
                for (auto &c : t.captured) {
                        CHECK(c.txTimeNs == 0);
                }
        }
}

TEST_CASE("PacketScheduler::setRate forwarding through base") {
        StubTransport            transport;
        transport.open();
        KernelFqPacketScheduler  sched;
        sched.setTransport(&transport);

        // Bypass the base configure() path entirely — direct setRate
        // call must still propagate to the transport via the override
        // pipeline.
        CHECK(sched.setRate(500'000).isOk());
        CHECK(transport.pacingRate == 500'000);
}
