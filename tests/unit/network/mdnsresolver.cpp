/**
 * @file      mdnsresolver.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/mdnsresolver.h>
#include <promeki/mdnsservicetype.h>

using namespace promeki;

TEST_CASE("MdnsResolver: default state is inactive") {
        MdnsServiceInstance target;
        target.setType(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));
        target.setInstanceName("Studio Camera");
        MdnsResolver r(target);
        CHECK_FALSE(r.isActive());
}

TEST_CASE("MdnsResolver: resolve rejects an incomplete target") {
        MdnsServiceInstance bad;   // no type, no instance name
        MdnsResolver r(bad);
        CHECK(r.resolve() == Error::Invalid);
}

TEST_CASE("MdnsResolver: default timeout matches the documented constant") {
        MdnsServiceInstance target;
        target.setType(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));
        target.setInstanceName("Studio Camera");
        MdnsResolver r(target);
        CHECK(r.timeout() == Duration::fromMilliseconds(MdnsResolver::DefaultTimeoutMs));
}

TEST_CASE("MdnsResolver: setTimeout round-trips") {
        MdnsServiceInstance target;
        target.setType(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));
        target.setInstanceName("Studio Camera");
        MdnsResolver r(target);
        r.setTimeout(Duration::fromMilliseconds(500));
        CHECK(r.timeout() == Duration::fromMilliseconds(500));
}

TEST_CASE("MdnsResolver: target accessor returns the configured target") {
        MdnsServiceInstance target;
        target.setType(MdnsServiceType("rtsp", MdnsServiceType::Protocol::Tcp));
        target.setInstanceName("Camera A");
        MdnsResolver r(target);
        CHECK(r.target().type() == target.type());
        CHECK(r.target().instanceName() == String("Camera A"));
}
