/**
 * @file      rtpmediaclkmode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>

#include <promeki/enums_rtp.h>
#include <promeki/string.h>

using namespace promeki;

TEST_CASE("RtpMediaClkMode: well-known values are distinct") {
        CHECK(RtpMediaClkMode::Auto.value() == 0);
        CHECK(RtpMediaClkMode::Direct.value() == 1);
        CHECK(RtpMediaClkMode::Sender.value() == 2);
        CHECK(RtpMediaClkMode::Auto != RtpMediaClkMode::Direct);
        CHECK(RtpMediaClkMode::Direct != RtpMediaClkMode::Sender);
        CHECK(RtpMediaClkMode::Auto != RtpMediaClkMode::Sender);
}

TEST_CASE("RtpMediaClkMode: default-construct yields the registered default (Auto)") {
        RtpMediaClkMode m;
        CHECK(m == RtpMediaClkMode::Auto);
        CHECK(m.hasListedValue());
}

TEST_CASE("RtpMediaClkMode: valueName round-trips through string constructor") {
        RtpMediaClkMode mAuto(String("Auto"));
        CHECK(mAuto.hasListedValue());
        CHECK(mAuto == RtpMediaClkMode::Auto);
        CHECK(mAuto.valueName() == String("Auto"));

        RtpMediaClkMode mDirect(String("Direct"));
        CHECK(mDirect.hasListedValue());
        CHECK(mDirect == RtpMediaClkMode::Direct);
        CHECK(mDirect.valueName() == String("Direct"));

        RtpMediaClkMode mSender(String("Sender"));
        CHECK(mSender.hasListedValue());
        CHECK(mSender == RtpMediaClkMode::Sender);
        CHECK(mSender.valueName() == String("Sender"));
}

TEST_CASE("RtpMediaClkMode: unknown string yields invalid value") {
        RtpMediaClkMode m(String("Bogus"));
        CHECK_FALSE(m.hasListedValue());
}
