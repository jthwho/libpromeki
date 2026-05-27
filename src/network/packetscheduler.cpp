/**
 * @file      packetscheduler.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/packetscheduler.h>

#include <promeki/burstpacketscheduler.h>
#include <promeki/cadencepacketscheduler.h>
#include <promeki/enums_rtp.h>
#include <promeki/kernelfqpacketscheduler.h>
#include <promeki/logger.h>
#include <promeki/txtimepacketscheduler.h>

PROMEKI_NAMESPACE_BEGIN

Error PacketScheduler::configure(const Spec &spec) {
        // Store the spec unconditionally — it is plain configuration
        // data, not a transport operation.  The historical
        // @c _transport == nullptr early-return silently discarded
        // every configureScheduler() call that ran before
        // @ref RtpSession::start (which is the only place the
        // transport gets attached), leaving the scheduler stuck on a
        // default-constructed @c _spec.frameInterval — i.e. the
        // "degrade to burst" path on every send, defeating the
        // userspace pacing the caller asked for.
        _spec = spec;
        return Error::Ok;
}

Error PacketScheduler::setRate(uint64_t bytesPerSec) {
        Spec s = _spec;
        s.bytesPerSec = bytesPerSec;
        return configure(s);
}

PacketScheduler::UPtr PacketScheduler::create(const Enum &mode, PacketTransport *transport) {
        // Resolve to a concrete subclass.  Auto degrades to KernelFq on
        // Linux (the prior default in RtpMediaIO::configurePacingMode)
        // and Userspace on other platforms.
        int v = mode.value();
        if (v == RtpPacingMode::Auto.value()) {
#if defined(PROMEKI_PLATFORM_LINUX)
                v = RtpPacingMode::KernelFq.value();
#else
                v = RtpPacingMode::Userspace.value();
#endif
        }

        PacketScheduler::UPtr s;
        if (v == RtpPacingMode::None.value()) {
                s = UniquePtr<BurstPacketScheduler>::create();
        } else if (v == RtpPacingMode::Userspace.value()) {
                s = UniquePtr<CadencePacketScheduler>::create();
        } else if (v == RtpPacingMode::KernelFq.value()) {
                s = UniquePtr<KernelFqPacketScheduler>::create();
        } else if (v == RtpPacingMode::TxTime.value()) {
                s = UniquePtr<TxTimePacketScheduler>::create();
        } else {
                promekiWarn("PacketScheduler::create unknown mode %d — using BurstPacketScheduler", v);
                s = UniquePtr<BurstPacketScheduler>::create();
        }
        s->setTransport(transport);
        return s;
}

PROMEKI_NAMESPACE_END
