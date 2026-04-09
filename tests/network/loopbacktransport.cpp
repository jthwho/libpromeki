/**
 * @file      loopbacktransport.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/loopbacktransport.h>
#include <cstring>

using namespace promeki;

TEST_CASE("LoopbackTransport") {

        SUBCASE("construction") {
                LoopbackTransport t;
                CHECK_FALSE(t.isOpen());
                CHECK(t.pendingPackets() == 0);
        }

        SUBCASE("open and close") {
                LoopbackTransport t;
                CHECK(t.open().isOk());
                CHECK(t.isOpen());
                t.close();
                CHECK_FALSE(t.isOpen());
        }

        SUBCASE("send without peer fails") {
                LoopbackTransport t;
                t.open();
                CHECK(t.sendPacket("x", 1, SocketAddress()) == -1);
        }

        SUBCASE("paired single send/receive") {
                LoopbackTransport a, b;
                LoopbackTransport::pair(&a, &b);
                a.open();
                b.open();

                const char *msg = "hello loopback";
                SocketAddress src(Ipv4Address::loopback(), 12345);
                ssize_t sent = a.sendPacket(msg, std::strlen(msg), src);
                CHECK(sent == static_cast<ssize_t>(std::strlen(msg)));
                CHECK(b.pendingPackets() == 1);

                char buf[64];
                SocketAddress from;
                ssize_t n = b.receivePacket(buf, sizeof(buf), &from);
                REQUIRE(n > 0);
                CHECK(std::memcmp(buf, msg, n) == 0);
                CHECK(from == src);
                CHECK(b.pendingPackets() == 0);
        }

        SUBCASE("paired batch send") {
                LoopbackTransport a, b;
                LoopbackTransport::pair(&a, &b);
                a.open();
                b.open();

                SocketAddress src(Ipv4Address::loopback(), 1111);
                char msgs[4][16];
                for(int i = 0; i < 4; i++) {
                        std::snprintf(msgs[i], sizeof(msgs[i]), "loop%d", i);
                }
                PacketTransport::DatagramList batch;
                for(int i = 0; i < 4; i++) {
                        PacketTransport::Datagram d;
                        d.data = msgs[i];
                        d.size = std::strlen(msgs[i]);
                        d.dest = src;
                        batch.pushToBack(d);
                }
                int sent = a.sendPackets(batch);
                CHECK(sent == 4);
                CHECK(b.pendingPackets() == 4);

                for(int i = 0; i < 4; i++) {
                        char buf[16];
                        ssize_t n = b.receivePacket(buf, sizeof(buf));
                        REQUIRE(n > 0);
                        char expected[16];
                        std::snprintf(expected, sizeof(expected), "loop%d", i);
                        CHECK(n == static_cast<ssize_t>(std::strlen(expected)));
                        CHECK(std::memcmp(buf, expected, n) == 0);
                }
        }

        SUBCASE("bidirectional") {
                LoopbackTransport a, b;
                LoopbackTransport::pair(&a, &b);
                a.open();
                b.open();

                const char *msgA = "a->b";
                const char *msgB = "b->a";
                a.sendPacket(msgA, 4, SocketAddress());
                b.sendPacket(msgB, 4, SocketAddress());

                char buf[16];
                CHECK(b.receivePacket(buf, sizeof(buf)) == 4);
                CHECK(std::memcmp(buf, msgA, 4) == 0);
                CHECK(a.receivePacket(buf, sizeof(buf)) == 4);
                CHECK(std::memcmp(buf, msgB, 4) == 0);
        }

        SUBCASE("receive on empty queue returns -1") {
                LoopbackTransport a, b;
                LoopbackTransport::pair(&a, &b);
                a.open(); b.open();
                char buf[16];
                CHECK(b.receivePacket(buf, sizeof(buf)) == -1);
        }

        SUBCASE("close clears pending packets") {
                LoopbackTransport a, b;
                LoopbackTransport::pair(&a, &b);
                a.open(); b.open();
                a.sendPacket("x", 1, SocketAddress());
                CHECK(b.pendingPackets() == 1);
                b.close();
                CHECK(b.pendingPackets() == 0);
        }

        SUBCASE("setPacingRate and setTxTime accepted") {
                LoopbackTransport t;
                t.open();
                CHECK(t.setPacingRate(1'000'000).isOk());
                CHECK(t.setTxTime(true).isOk());
                CHECK(t.setTxTime(false).isOk());
        }

        SUBCASE("destructor unhooks peer") {
                LoopbackTransport a;
                {
                        LoopbackTransport b;
                        LoopbackTransport::pair(&a, &b);
                }
                // a's peer pointer should now be clear so a future
                // send does not dereference freed memory.
                a.open();
                CHECK(a.sendPacket("x", 1, SocketAddress()) == -1);
        }
}
