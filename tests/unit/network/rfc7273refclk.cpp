/**
 * @file      rfc7273refclk.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/rfc7273refclk.h>

using namespace promeki;

TEST_CASE("Rfc7273RefClk") {

        SUBCASE("default-constructed is invalid") {
                Rfc7273RefClk r;
                CHECK_FALSE(r.isValid());
                CHECK(r.kind() == Rfc7273RefClk::Kind::None);
                CHECK(r.toSdpValue().isEmpty());
        }

        SUBCASE("ptp without gmid round-trip") {
                auto r = Rfc7273RefClk::ptp(String("IEEE1588-2008"));
                CHECK(r.isValid());
                CHECK(r.kind() == Rfc7273RefClk::Kind::Ptp);
                CHECK_FALSE(r.isTraceable());
                CHECK(r.profile() == String("IEEE1588-2008"));
                CHECK(r.grandmasterId().isNull());
                CHECK(r.toSdpValue() == String("ptp=IEEE1588-2008"));

                auto [parsed, err] = Rfc7273RefClk::fromSdpValue(r.toSdpValue());
                REQUIRE(err.isOk());
                CHECK(parsed == r);
        }

        SUBCASE("ptp with gmid + domain round-trip") {
                EUI64 gm(0x00, 0x11, 0x22, 0xFF, 0xFE, 0x33, 0x44, 0x55);
                auto  r = Rfc7273RefClk::ptp(String("IEEE1588-2008"), gm, 127);
                CHECK(r.isValid());
                CHECK(r.kind() == Rfc7273RefClk::Kind::Ptp);
                CHECK(r.profile() == String("IEEE1588-2008"));
                CHECK(r.grandmasterId() == gm);
                CHECK(r.domain() == 127);

                String wire = r.toSdpValue();
                CHECK(wire == String("ptp=IEEE1588-2008:00-11-22-ff-fe-33-44-55:127"));

                auto [parsed, err] = Rfc7273RefClk::fromSdpValue(wire);
                REQUIRE(err.isOk());
                CHECK(parsed == r);
        }

        SUBCASE("ptp traceable round-trip") {
                auto r = Rfc7273RefClk::ptpTraceable(String("IEEE1588-2008"));
                CHECK(r.isValid());
                CHECK(r.kind() == Rfc7273RefClk::Kind::Ptp);
                CHECK(r.isTraceable());
                CHECK(r.grandmasterId().isNull());
                CHECK(r.toSdpValue() == String("ptp=IEEE1588-2008:traceable"));

                auto [parsed, err] = Rfc7273RefClk::fromSdpValue(r.toSdpValue());
                REQUIRE(err.isOk());
                CHECK(parsed.isTraceable());
                CHECK(parsed == r);
        }

        SUBCASE("localmac round-trip") {
                auto [mac, mErr] = MacAddress::fromString(String("00:1a:2b:3c:4d:5e"));
                REQUIRE(mErr.isOk());
                auto r = Rfc7273RefClk::localMac(mac);
                CHECK(r.isValid());
                CHECK(r.kind() == Rfc7273RefClk::Kind::LocalMac);
                CHECK(r.localMacAddress() == mac);

                String wire = r.toSdpValue();
                CHECK(wire == String("localmac=00-1a-2b-3c-4d-5e"));

                auto [parsed, err] = Rfc7273RefClk::fromSdpValue(wire);
                REQUIRE(err.isOk());
                CHECK(parsed == r);
        }

        SUBCASE("bare local round-trip") {
                auto r = Rfc7273RefClk::local();
                CHECK(r.isValid());
                CHECK(r.kind() == Rfc7273RefClk::Kind::LocalMac);
                CHECK(r.localMacAddress().isNull());
                CHECK(r.toSdpValue() == String("local"));

                auto [parsed, err] = Rfc7273RefClk::fromSdpValue(String("local"));
                REQUIRE(err.isOk());
                CHECK(parsed == r);
        }

        SUBCASE("default profile falls back to IEEE1588-2008") {
                // Passing an empty profile string should yield the
                // ST 2110 default rather than a malformed "ptp=" value.
                auto r = Rfc7273RefClk::ptp(String());
                CHECK(r.profile() == String("IEEE1588-2008"));
                CHECK(r.toSdpValue() == String("ptp=IEEE1588-2008"));
        }

        SUBCASE("parse rejects unrecognised forms") {
                auto [v1, e1] = Rfc7273RefClk::fromSdpValue(String("ntp=192.168.1.1:123"));
                CHECK(e1.isError());
                auto [v2, e2] = Rfc7273RefClk::fromSdpValue(String());
                CHECK(e2.isError());
                auto [v3, e3] = Rfc7273RefClk::fromSdpValue(String("ptp="));
                CHECK(e3.isError());
                auto [v4, e4] = Rfc7273RefClk::fromSdpValue(String("private"));
                CHECK(e4.isError());
        }

        SUBCASE("parse rejects malformed gmid / domain") {
                auto [v1, e1] = Rfc7273RefClk::fromSdpValue(String("ptp=IEEE1588-2008:notamac:0"));
                CHECK(e1.isError());
                auto [v2, e2] = Rfc7273RefClk::fromSdpValue(
                        String("ptp=IEEE1588-2008:00-11-22-ff-fe-33-44-55:9999"));
                CHECK(e2.isError());
        }

        SUBCASE("parse rejects malformed localmac") {
                auto [v, e] = Rfc7273RefClk::fromSdpValue(String("localmac=not-a-mac"));
                CHECK(e.isError());
        }

        SUBCASE("equality and inequality") {
                auto a = Rfc7273RefClk::ptp(String("IEEE1588-2008"));
                auto b = Rfc7273RefClk::ptp(String("IEEE1588-2008"));
                auto c = Rfc7273RefClk::ptpTraceable(String("IEEE1588-2008"));
                CHECK(a == b);
                CHECK(a != c);
                CHECK(Rfc7273RefClk() != a);
        }
}
