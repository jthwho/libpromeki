/**
 * @file      multicastreceiver.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <doctest/doctest.h>
#include <promeki/buffer.h>
#include <promeki/multicastreceiver.h>
#include <promeki/socketaddress.h>
#include <promeki/udpsocket.h>

using namespace promeki;

namespace {

/// Picks a free UDP port on the loopback interface by binding then
/// closing a throwaway socket.  The returned port may still race,
/// but the odds are low enough for CI and the receiver retries
/// through SO_REUSEADDR anyway.
static uint16_t pickFreePort() {
        UdpSocket sock;
        sock.open(IODevice::ReadWrite);
        sock.bind(SocketAddress::any(0));
        uint16_t port = sock.localAddress().port();
        sock.close();
        return port;
}

} // namespace

TEST_CASE("MulticastReceiver") {

        SUBCASE("construction defaults") {
                MulticastReceiver rx;
                CHECK_FALSE(rx.isActive());
                CHECK(rx.maxPacketSize() == MulticastReceiver::DefaultMaxPacketSize);
                CHECK(rx.receiveTimeout() == MulticastReceiver::DefaultReceiveTimeoutMs);
                CHECK(rx.threadName() == "multicast-rx");
                CHECK(rx.groups().isEmpty());
                CHECK(rx.datagramCount() == 0);
                CHECK(rx.byteCount() == 0);
        }

        SUBCASE("setters round-trip") {
                MulticastReceiver rx;
                rx.setLocalAddress(SocketAddress::any(5004));
                rx.setInterface("lo");
                rx.setMaxPacketSize(4096);
                rx.setReceiveTimeout(50);
                rx.setThreadName("my-custom-rx");
                CHECK(rx.localAddress().port() == 5004);
                CHECK(rx.interfaceName() == "lo");
                CHECK(rx.maxPacketSize() == 4096);
                CHECK(rx.receiveTimeout() == 50);
                CHECK(rx.threadName() == "my-custom-rx");
        }

        SUBCASE("addGroup rejects non-multicast") {
                MulticastReceiver rx;
                Error err = rx.addGroup(SocketAddress(Ipv4Address::loopback(), 5004));
                CHECK(err.isError());
                CHECK(rx.groups().isEmpty());
        }

        SUBCASE("addGroup accepts ASM address") {
                MulticastReceiver rx;
                SocketAddress group(Ipv4Address(239, 255, 0, 42), 5004);
                Error err = rx.addGroup(group);
                CHECK(err.isOk());
                REQUIRE(rx.groups().size() == 1);
                CHECK(rx.groups()[0].group == group);
                CHECK_FALSE(rx.groups()[0].isSSM);
        }

        SUBCASE("addSourceGroup accepts SSM tuple") {
                MulticastReceiver rx;
                SocketAddress group(Ipv4Address(232, 0, 0, 1), 5004);
                SocketAddress source(Ipv4Address(192, 0, 2, 10), 0);
                Error err = rx.addSourceGroup(group, source);
                CHECK(err.isOk());
                REQUIRE(rx.groups().size() == 1);
                CHECK(rx.groups()[0].isSSM);
                CHECK(rx.groups()[0].group == group);
                CHECK(rx.groups()[0].source == source);
        }

        SUBCASE("addSourceGroup rejects non-multicast group") {
                MulticastReceiver rx;
                SocketAddress group(Ipv4Address::loopback(), 5004);
                SocketAddress source(Ipv4Address(192, 0, 2, 10), 0);
                Error err = rx.addSourceGroup(group, source);
                CHECK(err.isError());
                CHECK(rx.groups().isEmpty());
        }

        SUBCASE("addSourceGroup rejects null source") {
                MulticastReceiver rx;
                SocketAddress group(Ipv4Address(232, 0, 0, 1), 5004);
                SocketAddress nullSource;
                Error err = rx.addSourceGroup(group, nullSource);
                CHECK(err.isError());
                CHECK(rx.groups().isEmpty());
        }

        SUBCASE("setMaxPacketSize zero defaults to DefaultMaxPacketSize") {
                MulticastReceiver rx;
                rx.setMaxPacketSize(0);
                CHECK(rx.maxPacketSize() == MulticastReceiver::DefaultMaxPacketSize);
        }

        SUBCASE("setReceiveTimeout zero defaults to DefaultReceiveTimeoutMs") {
                MulticastReceiver rx;
                rx.setReceiveTimeout(0);
                CHECK(rx.receiveTimeout() == MulticastReceiver::DefaultReceiveTimeoutMs);
        }

        SUBCASE("start without callback fails") {
                MulticastReceiver rx;
                rx.setLocalAddress(SocketAddress::any(0));
                Error err = rx.start();
                CHECK(err.isError());
                CHECK_FALSE(rx.isActive());
        }

        SUBCASE("start then stop without groups") {
                // With no groups configured, start() still needs to
                // bind the socket and spin up the receive thread —
                // verifies the thread lifecycle works even with an
                // empty group list.
                MulticastReceiver rx;
                rx.setLocalAddress(SocketAddress::any(0));
                rx.setReceiveTimeout(20);
                rx.setDatagramCallback(
                        [](Buffer::Ptr, const SocketAddress &) {});
                Error err = rx.start();
                REQUIRE(err.isOk());
                CHECK(rx.isActive());
                rx.stop();
                CHECK_FALSE(rx.isActive());
        }

        SUBCASE("receives multicast datagrams") {
                const uint16_t port = pickFreePort();
                SocketAddress group(Ipv4Address(239, 255, 0, 77), port);

                // Set up the receiver before sending so we do not
                // drop the first datagram due to the kernel's
                // post-join hand-off latency.
                MulticastReceiver rx;
                rx.setLocalAddress(SocketAddress::any(port));
                rx.setReceiveTimeout(20);

                std::atomic<int> count{0};
                std::atomic<size_t> lastSize{0};
                uint8_t lastFirstByte = 0;
                std::atomic<bool> sawData{false};
                rx.setDatagramCallback(
                        [&](Buffer::Ptr data, const SocketAddress &) {
                                lastSize.store(data->size());
                                if(data->size() > 0) {
                                        lastFirstByte =
                                                static_cast<const uint8_t *>(
                                                        data->data())[0];
                                }
                                sawData.store(true);
                                count.fetch_add(1);
                        });
                REQUIRE(rx.addGroup(group).isOk());
                REQUIRE(rx.start().isOk());

                // Sender side — use a plain UdpSocket with multicast
                // loopback enabled so a single-host test sees its
                // own packets.
                UdpSocket tx;
                tx.open(IODevice::ReadWrite);
                tx.setMulticastLoopback(true);
                tx.setMulticastTTL(1);
                const uint8_t payload[] = {0xAB, 0xCD, 0xEF, 0x01, 0x02, 0x03};
                ssize_t sent = tx.writeDatagram(
                        payload, sizeof(payload), group);
                CHECK(sent == static_cast<ssize_t>(sizeof(payload)));

                // Wait up to 500 ms for the datagram to arrive on
                // the receive thread.  The loopback path is fast
                // but the test still needs to tolerate scheduler
                // jitter.
                for(int i = 0; i < 50 && !sawData.load(); i++) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                rx.stop();

                CHECK(sawData.load());
                CHECK(count.load() >= 1);
                CHECK(lastSize.load() == sizeof(payload));
                CHECK(lastFirstByte == 0xAB);
                CHECK(rx.datagramCount() >= 1);
                CHECK(rx.byteCount() >= sizeof(payload));
        }

        SUBCASE("stop is idempotent") {
                MulticastReceiver rx;
                rx.setLocalAddress(SocketAddress::any(0));
                rx.setDatagramCallback(
                        [](Buffer::Ptr, const SocketAddress &) {});
                REQUIRE(rx.start().isOk());
                rx.stop();
                rx.stop(); // second call is a no-op
                CHECK_FALSE(rx.isActive());
        }

        SUBCASE("restart after stop") {
                MulticastReceiver rx;
                rx.setLocalAddress(SocketAddress::any(0));
                rx.setReceiveTimeout(20);
                rx.setDatagramCallback(
                        [](Buffer::Ptr, const SocketAddress &) {});
                REQUIRE(rx.start().isOk());
                rx.stop();
                REQUIRE(rx.start().isOk());
                CHECK(rx.isActive());
                rx.stop();
        }
}
