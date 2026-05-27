/**
 * @file      ancpayload.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/optional.h>
#include <promeki/ancpayload.h>
#include <promeki/datastream.h>
#include <promeki/variantlookup.h>

PROMEKI_NAMESPACE_BEGIN

AncPacket::List AncPayload::packetsOfFormat(const AncFormat &fmt) const {
        AncPacket::List ret;
        for (const auto &p : _packets) {
                if (p.format() == fmt) ret.pushToBack(p);
        }
        return ret;
}

AncPacket::List AncPayload::packetsOfCategory(const AncCategory &category) const {
        AncPacket::List ret;
        for (const auto &p : _packets) {
                if (p.format().category() == category) ret.pushToBack(p);
        }
        return ret;
}

AncPacket::List AncPayload::packetsOfTransport(const AncTransport &transport) const {
        AncPacket::List ret;
        for (const auto &p : _packets) {
                if (p.transport() == transport) ret.pushToBack(p);
        }
        return ret;
}

bool AncPayload::hasFormat(const AncFormat &fmt) const {
        for (const auto &p : _packets) {
                if (p.format() == fmt) return true;
        }
        return false;
}

bool AncPayload::hasCategory(const AncCategory &category) const {
        for (const auto &p : _packets) {
                if (p.format().category() == category) return true;
        }
        return false;
}

void AncPayload::serialisePayload(DataStream &s) const {
        s << _desc;
        s << _duration.nanoseconds();
        s << static_cast<uint32_t>(_packets.size());
        for (const auto &p : _packets) s << p;
}

void AncPayload::deserialisePayload(DataStream &s) {
        AncDesc  d;
        int64_t  ns = 0;
        uint32_t count = 0;
        s >> d;
        if (s.status() == DataStream::Ok) setDesc(d);
        s >> ns;
        if (s.status() == DataStream::Ok) _duration = Duration::fromNanoseconds(ns);
        s >> count;
        if (s.status() != DataStream::Ok) return;
        _packets.clear();
        _packets.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
                AncPacket pkt;
                s >> pkt;
                if (s.status() != DataStream::Ok) return;
                _packets.pushToBack(std::move(pkt));
        }
}

// ============================================================================
// VariantLookup registration — surface AncDesc + packet-list fields
// alongside the MediaPayload base scalar set.  Predicates like
// HasCaptions / HasTimecode / HasHdr / HasAfd are evaluated by walking
// the packet list once; for small ANC frames (the common case) that's
// effectively free.
// ============================================================================

PROMEKI_LOOKUP_REGISTER(AncPayload)
        .inheritsFrom<MediaPayload>()
        .scalar("PacketCount",
                [](const AncPayload &p) -> Optional<Variant> {
                        return Variant(static_cast<uint64_t>(p.packets().size()));
                })
        .scalar("HasCaptions",
                [](const AncPayload &p) -> Optional<Variant> {
                        return Variant(p.hasCategory(AncCategory::Captions));
                })
        .scalar("HasTimecode",
                [](const AncPayload &p) -> Optional<Variant> {
                        return Variant(p.hasCategory(AncCategory::Timecode));
                })
        .scalar("HasAfd",
                [](const AncPayload &p) -> Optional<Variant> {
                        return Variant(p.hasFormat(AncFormat(AncFormat::Afd)));
                })
        .scalar("HasHdr",
                [](const AncPayload &p) -> Optional<Variant> {
                        return Variant(p.hasCategory(AncCategory::Hdr));
                })
        .scalar("HasSplice", [](const AncPayload &p) -> Optional<Variant> {
                return Variant(p.hasCategory(AncCategory::Splice));
        });

PROMEKI_NAMESPACE_END

// Stable FourCC registration for cross-process / file serialisation.
PROMEKI_REGISTER_MEDIAPAYLOAD(AncPayload, "ANCp")
