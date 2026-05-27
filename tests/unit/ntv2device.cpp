/**
 * @file      ntv2device.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_NTV2

#include <doctest/doctest.h>
#include <promeki/error.h>
#include <promeki/ntv2capabilities.h>
#include <promeki/ntv2device.h>
#include <promeki/sdisignalconfig.h>
#include <promeki/uniqueptr.h>
#include <promeki/videoportref.h>
#include <promeki/videoreferenceconfig.h>

using namespace promeki;

namespace {

        // Synthetic Ntv2MediaIO "owner" pointers — the device only
        // ever compares these by identity, so we can fabricate them
        // out of any address that uniquely identifies the test caller.
        // Casting integer literals avoids any real construction.
        Ntv2MediaIO *fakeOwner(uintptr_t tag) {
                return reinterpret_cast<Ntv2MediaIO *>(tag);
        }

        UniquePtr<Ntv2Device> makeTestDevice() {
                // Four channels, four audio systems, four SDI in + four
                // SDI out, multi-format on — generic 4-channel card
                // shape (Kona-class).  Drops every NTV2 frame-buffer
                // format into the supported bitmap so the reservation
                // tests don't trip over pixel-format negotiation.
                return Ntv2DeviceTestAccess::create(
                        /*deviceIndex=*/0, String("test-card"),
                        Ntv2Capabilities::createForTest(/*channels=*/4, /*audioSystems=*/4,
                                                        /*sdiInputs=*/4, /*sdiOutputs=*/4));
        }

} // namespace

TEST_CASE("Ntv2Device: channel reservation rejects double-allocation and unwinds on release") {
        UniquePtr<Ntv2Device> dev = makeTestDevice();
        REQUIRE(dev.isValid());

        Ntv2MediaIO *a = fakeOwner(0xa1);
        Ntv2MediaIO *b = fakeOwner(0xb1);

        // First owner claims channel 2.
        CHECK(dev->reserveChannel(2, a).isOk());
        CHECK(Ntv2DeviceTestAccess::channelOwnerCount(*dev) == 1);
        CHECK(Ntv2DeviceTestAccess::channelOwner(*dev, 2) == a);

        // Second owner can't claim the same channel.
        CHECK(dev->reserveChannel(2, b) == Error::Busy);
        CHECK(Ntv2DeviceTestAccess::channelOwner(*dev, 2) == a);

        // Same owner re-reserving is idempotent.
        CHECK(dev->reserveChannel(2, a).isOk());
        CHECK(Ntv2DeviceTestAccess::channelOwnerCount(*dev) == 1);

        // releaseChannel against a non-owner is a no-op that returns
        // success (the device only tracks the actual owner).
        CHECK(dev->releaseChannel(2, b).isOk());
        CHECK(Ntv2DeviceTestAccess::channelOwner(*dev, 2) == a);

        // Out-of-range channel index rejects with InvalidArgument.
        CHECK(dev->reserveChannel(99, a) == Error::InvalidArgument);
        CHECK(dev->reserveChannel(0, a) == Error::InvalidArgument);

        // After the real owner releases, b can claim it.
        CHECK(dev->releaseChannel(2, a).isOk());
        CHECK(Ntv2DeviceTestAccess::channelOwnerCount(*dev) == 0);
        CHECK(dev->reserveChannel(2, b).isOk());
        CHECK(Ntv2DeviceTestAccess::channelOwner(*dev, 2) == b);
}

TEST_CASE("Ntv2Device: port reservation is atomic across multi-port requests") {
        UniquePtr<Ntv2Device> dev = makeTestDevice();
        REQUIRE(dev.isValid());

        Ntv2MediaIO *a = fakeOwner(0xa2);
        Ntv2MediaIO *b = fakeOwner(0xb2);

        SdiSignalConfig::PortList portsA;
        portsA.pushToBack(VideoPortRef(VideoConnectorKind::Sdi, 1));
        portsA.pushToBack(VideoPortRef(VideoConnectorKind::Sdi, 2));
        CHECK(dev->reservePorts(portsA, a).isOk());
        CHECK(Ntv2DeviceTestAccess::portOwnerCount(*dev) == 2);

        // Owner b wants ports {3, 2} — port 2 already held by a.  The
        // reservation must be atomic: port 3 is NOT claimed for b
        // (otherwise b would have leaked half a reservation across the
        // failure).
        SdiSignalConfig::PortList portsBOverlap;
        portsBOverlap.pushToBack(VideoPortRef(VideoConnectorKind::Sdi, 3));
        portsBOverlap.pushToBack(VideoPortRef(VideoConnectorKind::Sdi, 2));
        CHECK(dev->reservePorts(portsBOverlap, b) == Error::Busy);
        CHECK(Ntv2DeviceTestAccess::portOwnerCount(*dev) == 2);

        // b can claim port 3 cleanly because the failed atomic
        // reservation left no half-state.
        SdiSignalConfig::PortList portsBClean;
        portsBClean.pushToBack(VideoPortRef(VideoConnectorKind::Sdi, 3));
        CHECK(dev->reservePorts(portsBClean, b).isOk());
        CHECK(Ntv2DeviceTestAccess::portOwnerCount(*dev) == 3);
}

TEST_CASE("Ntv2Device: releasePortsOwnedBy releases only the requested owner's ports") {
        UniquePtr<Ntv2Device> dev = makeTestDevice();
        REQUIRE(dev.isValid());

        Ntv2MediaIO *a = fakeOwner(0xa3);
        Ntv2MediaIO *b = fakeOwner(0xb3);

        SdiSignalConfig::PortList portsA;
        portsA.pushToBack(VideoPortRef(VideoConnectorKind::Sdi, 1));
        portsA.pushToBack(VideoPortRef(VideoConnectorKind::Sdi, 2));
        CHECK(dev->reservePorts(portsA, a).isOk());

        SdiSignalConfig::PortList portsB;
        portsB.pushToBack(VideoPortRef(VideoConnectorKind::Sdi, 3));
        portsB.pushToBack(VideoPortRef(VideoConnectorKind::Sdi, 4));
        CHECK(dev->reservePorts(portsB, b).isOk());
        CHECK(Ntv2DeviceTestAccess::portOwnerCount(*dev) == 4);

        dev->releasePortsOwnedBy(a);
        CHECK(Ntv2DeviceTestAccess::portOwnerCount(*dev) == 2);

        // a can now re-take its ports; b's reservation untouched.
        CHECK(dev->reservePorts(portsA, a).isOk());
        CHECK(Ntv2DeviceTestAccess::portOwnerCount(*dev) == 4);
}

TEST_CASE("Ntv2Device: audio-system reservation refuses double-allocation") {
        UniquePtr<Ntv2Device> dev = makeTestDevice();
        REQUIRE(dev.isValid());

        Ntv2MediaIO *a = fakeOwner(0xa4);
        Ntv2MediaIO *b = fakeOwner(0xb4);

        CHECK(dev->reserveAudioSystem(1, a).isOk());
        CHECK(Ntv2DeviceTestAccess::audioSystemOwner(*dev, 1) == a);
        CHECK(dev->reserveAudioSystem(1, b) == Error::Busy);
        CHECK(Ntv2DeviceTestAccess::audioSystemOwner(*dev, 1) == a);

        // Out-of-range system index rejects.
        CHECK(dev->reserveAudioSystem(99, a) == Error::InvalidArgument);
        CHECK(dev->reserveAudioSystem(0, a) == Error::InvalidArgument);

        // Idempotent same-owner re-reservation.
        CHECK(dev->reserveAudioSystem(1, a).isOk());
        CHECK(Ntv2DeviceTestAccess::audioSystemOwnerCount(*dev) == 1);

        // After release, another owner can claim.
        CHECK(dev->releaseAudioSystem(1, a).isOk());
        CHECK(Ntv2DeviceTestAccess::audioSystemOwnerCount(*dev) == 0);
        CHECK(dev->reserveAudioSystem(1, b).isOk());
}

TEST_CASE("Ntv2Device: setReference applies the new request when owners conflict") {
        UniquePtr<Ntv2Device> dev = makeTestDevice();
        REQUIRE(dev.isValid());

        Ntv2MediaIO *a = fakeOwner(0xa5);
        Ntv2MediaIO *b = fakeOwner(0xb5);

        // First setter applies its request silently.
        VideoReferenceConfig refA(VideoReferenceSource::FreeRun, VideoReferenceRateFamily::Integer);
        CHECK(dev->setReference(refA, a).isOk());
        CHECK(Ntv2DeviceTestAccess::referenceSet(*dev) == true);
        CHECK(Ntv2DeviceTestAccess::referenceOwner(*dev) == a);
        const int refAraw = Ntv2DeviceTestAccess::currentReferenceRaw(*dev);

        // Second owner requests a different source; setReference logs
        // a warning but applies the new request (the device hands over
        // ownership and updates the stored value).  The prior owner
        // doesn't retroactively fail.
        VideoReferenceConfig refB(VideoReferenceSource::Genlock, VideoReferenceRateFamily::Integer);
        CHECK(dev->setReference(refB, b).isOk());
        CHECK(Ntv2DeviceTestAccess::referenceOwner(*dev) == b);
        const int refBraw = Ntv2DeviceTestAccess::currentReferenceRaw(*dev);
        CHECK(refBraw != refAraw);

        // Same owner reissuing the same request is a quiet no-op
        // (no state change, no warning).
        CHECK(dev->setReference(refB, b).isOk());
        CHECK(Ntv2DeviceTestAccess::referenceOwner(*dev) == b);
        CHECK(Ntv2DeviceTestAccess::currentReferenceRaw(*dev) == refBraw);
}

TEST_CASE("Ntv2Device: two concurrent owners share independent channel + audio reservations") {
        UniquePtr<Ntv2Device> dev = makeTestDevice();
        REQUIRE(dev.isValid());

        Ntv2MediaIO *a = fakeOwner(0xa6);
        Ntv2MediaIO *b = fakeOwner(0xb6);

        // a opens channel 1 with audio system 1.
        CHECK(dev->reserveChannel(1, a).isOk());
        SdiSignalConfig::PortList portsA;
        portsA.pushToBack(VideoPortRef(VideoConnectorKind::Sdi, 1));
        CHECK(dev->reservePorts(portsA, a).isOk());
        CHECK(dev->reserveAudioSystem(1, a).isOk());

        // b opens channel 2 with audio system 2 — fully independent
        // reservation, both succeed.
        CHECK(dev->reserveChannel(2, b).isOk());
        SdiSignalConfig::PortList portsB;
        portsB.pushToBack(VideoPortRef(VideoConnectorKind::Sdi, 2));
        CHECK(dev->reservePorts(portsB, b).isOk());
        CHECK(dev->reserveAudioSystem(2, b).isOk());

        CHECK(Ntv2DeviceTestAccess::channelOwnerCount(*dev) == 2);
        CHECK(Ntv2DeviceTestAccess::portOwnerCount(*dev) == 2);
        CHECK(Ntv2DeviceTestAccess::audioSystemOwnerCount(*dev) == 2);

        // a closes; b's reservations untouched.
        dev->releaseChannel(1, a);
        dev->releasePortsOwnedBy(a);
        dev->releaseAudioSystem(1, a);
        CHECK(Ntv2DeviceTestAccess::channelOwnerCount(*dev) == 1);
        CHECK(Ntv2DeviceTestAccess::portOwnerCount(*dev) == 1);
        CHECK(Ntv2DeviceTestAccess::audioSystemOwnerCount(*dev) == 1);
        CHECK(Ntv2DeviceTestAccess::channelOwner(*dev, 2) == b);
}

#endif // PROMEKI_ENABLE_NTV2
