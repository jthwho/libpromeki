/**
 * @file      mediapayload.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Owns the subclass-factory registry that backs
 * @ref MediaPayload::createEmpty so @ref DataStream's deserialisation
 * path can build the right concrete leaf from the FourCC tag without
 * knowing the type statically.
 */

#include <promeki/mediapayload.h>
#include <promeki/datastream.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/mediatimestamp.h>
#include <promeki/duration.h>
#include <promeki/metadata.h>
#include <promeki/map.h>
#include <promeki/mutex.h>
#include <promeki/videopayload.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/compressedaudiopayload.h>
#include <promeki/variantlookup.h>
#include <promeki/variantdatabase.h>
#include <promeki/stringlist.h>
#include <cstring>

PROMEKI_NAMESPACE_BEGIN

namespace {

// Single process-wide mutex + map — the registry is touched only at
// static-init time and from the DataStream read path, so contention
// is minimal.
Mutex &registryMutex() {
        static Mutex m;
        return m;
}

Map<uint32_t, MediaPayload::Factory> &registry() {
        static Map<uint32_t, MediaPayload::Factory> r;
        return r;
}

} // namespace

void MediaPayload::registerSubclass(uint32_t fourcc, Factory factory) {
        if(factory == nullptr) return;
        Mutex::Locker lock(registryMutex());
        registry().insert(fourcc, factory);
}

MediaPayload::Ptr MediaPayload::createEmpty(uint32_t fourcc) {
        Factory f = nullptr;
        {
                Mutex::Locker lock(registryMutex());
                auto &r = registry();
                if(r.contains(fourcc)) f = r[fourcc];
        }
        if(f == nullptr) return Ptr();
        return f();
}

const char *MediaPayload::flagName(Flag f) {
        switch(f) {
                case None:         return "None";
                case Keyframe:     return "Keyframe";
                case Discardable:  return "Discardable";
                case Corrupt:      return "Corrupt";
                case EndOfStream:  return "EndOfStream";
                case IntraRefresh: return "IntraRefresh";
        }
        return nullptr;
}

namespace {

// Renders a 64-bit base-flag mask as a comma-separated list of flag
// names, falling back to the bit index (decimal) for any set bit that
// @ref MediaPayload::flagName does not recognise.  Empty masks render
// as "None" so the getter never emits an empty Variant string.
String flagsToString(uint64_t flags) {
        if(flags == 0) return String("None");
        String out;
        for(unsigned i = 0; i < 64; ++i) {
                const uint64_t bit = 1ull << i;
                if((flags & bit) == 0) continue;
                if(!out.isEmpty()) out += ',';
                const char *name = MediaPayload::flagName(
                                static_cast<MediaPayload::Flag>(bit));
                if(name != nullptr) out += name;
                else out += String::number(static_cast<uint32_t>(i));
        }
        return out;
}

// Parses the comma-separated form emitted by @ref flagsToString.
// Tokens may be flag names (as returned by @ref MediaPayload::flagName)
// or a decimal bit index in [0, 63].  "None" and the empty string both
// resolve to @c 0.  Sets @p ok to @c false when any token fails to
// resolve to a known flag or bit index.
uint64_t stringToFlags(const String &s, bool &ok) {
        ok = true;
        const String trimmed = s.trim();
        if(trimmed.isEmpty() || trimmed == "None") return 0;
        uint64_t mask = 0;
        StringList tokens = trimmed.split(",");
        for(const String &rawTok : tokens) {
                const String tok = rawTok.trim();
                if(tok.isEmpty()) continue;
                bool matched = false;
                for(unsigned i = 0; i < 64; ++i) {
                        const char *name = MediaPayload::flagName(
                                        static_cast<MediaPayload::Flag>(1ull << i));
                        if(name != nullptr && tok == name) {
                                mask |= 1ull << i;
                                matched = true;
                                break;
                        }
                }
                if(matched) continue;
                Error numErr;
                const uint32_t idx = tok.to<uint32_t>(&numErr);
                if(!numErr.isError() && idx < 64) {
                        mask |= 1ull << idx;
                        continue;
                }
                ok = false;
                return 0;
        }
        return mask;
}

} // namespace

// ============================================================================
// DataStream operators for MediaPayload::Ptr
//
// Wire format (after the TypeMediaPayload tag):
//   uint32   subclass FourCC     — selects the concrete leaf on read
//   uint32   plane count
//   for each plane:
//     Buffer — raw plane bytes (shares may be reconstituted on read
//              as independent buffers; we do not preserve aliasing
//              across planes that share a backing allocation).
//   MediaTimeStamp  PTS
//   MediaTimeStamp  DTS
//   uint32   stream index (stored as int but written unsigned for
//            simplicity — negative values are not expected on-wire).
//   uint64   flags
//   <subclass-specific tail via serialisePayload — the descriptor
//    that rides in the tail carries the metadata, so the base does
//    not write a separate metadata block.  Duration is not carried
//    at this level: audio derives it from sampleCount+sampleRate,
//    and video writes it through VideoPayload::serialiseVideoCommon
//    (called from each concrete video leaf's tail).>
// ============================================================================

DataStream &operator<<(DataStream &s, const MediaPayload::Ptr &p) {
        s.writeTag(DataStream::TypeMediaPayload);
        const bool valid = p.isValid();
        s << static_cast<uint32_t>(valid ? p->subclassFourCC() : 0u);
        if(!valid) return s;
        const MediaPayload &mp = *p;

        // Planes: write each BufferView's bytes as a Buffer payload.
        // We intentionally materialise the bytes so a plane that was
        // a sub-view of a larger buffer survives on-wire.
        const BufferView &planes = mp.data();
        s << static_cast<uint32_t>(planes.count());
        for(auto v : planes) {
                Buffer tmp(v.size());
                if(v.size() > 0) {
                        std::memcpy(tmp.data(), v.data(), v.size());
                        tmp.setSize(v.size());
                }
                s << tmp;
        }

        // MediaTimeStamp's toString/fromString doesn't round-trip the
        // default-constructed invalid sentinel, so wrap PTS/DTS in a
        // presence bit.
        const bool hasPts = mp.pts().isValid();
        s << hasPts;
        if(hasPts) s << mp.pts();
        const bool hasDts = mp.dts().isValid();
        s << hasDts;
        if(hasDts) s << mp.dts();
        s << static_cast<int32_t>(mp.streamIndex());
        s << static_cast<uint64_t>(mp.flags());

        // Subclass tail — serialises the descriptor (which carries
        // the metadata) and any codec-specific fields.
        mp.serialisePayload(s);
        return s;
}

DataStream &operator>>(DataStream &s, MediaPayload::Ptr &p) {
        p = MediaPayload::Ptr();
        if(!s.readTag(DataStream::TypeMediaPayload)) return s;

        uint32_t fourcc = 0;
        s >> fourcc;
        if(s.status() != DataStream::Ok) return s;
        if(fourcc == 0) return s; // null payload marker

        MediaPayload::Ptr out = MediaPayload::createEmpty(fourcc);
        if(!out.isValid()) {
                s.setError(DataStream::ReadCorruptData,
                           "MediaPayload: unknown subclass FourCC");
                return s;
        }
        MediaPayload *raw = out.modify();

        // Planes.
        uint32_t planeCount = 0;
        s >> planeCount;
        if(s.status() != DataStream::Ok) { p = MediaPayload::Ptr(); return s; }
        BufferView planes;
        for(uint32_t i = 0; i < planeCount; ++i) {
                Buffer::Ptr buf = Buffer::Ptr::create();
                s >> *buf.modify();
                if(s.status() != DataStream::Ok) { p = MediaPayload::Ptr(); return s; }
                const size_t sz = buf->size();
                planes.pushToBack(buf, 0, sz);
        }
        raw->setData(planes);

        MediaTimeStamp pts;
        MediaTimeStamp dts;
        int32_t streamIdx = 0;
        uint64_t flags = 0;
        bool hasPts = false;
        s >> hasPts;
        if(hasPts) s >> pts;
        bool hasDts = false;
        s >> hasDts;
        if(hasDts) s >> dts;
        s >> streamIdx;
        s >> flags;
        if(s.status() != DataStream::Ok) { p = MediaPayload::Ptr(); return s; }
        raw->setPts(pts);
        raw->setDts(dts);
        raw->setStreamIndex(streamIdx);
        raw->setFlags(flags);

        // Metadata rides in the subclass tail via the descriptor, so
        // there is no separate metadata block to read here.
        raw->deserialisePayload(s);
        if(s.status() != DataStream::Ok) { p = MediaPayload::Ptr(); return s; }
        p = std::move(out);
        return s;
}

// ============================================================================
// Concrete subclass serialise / deserialise hooks
// ============================================================================

void VideoPayload::serialiseVideoCommon(DataStream &s) const {
        s << _duration.nanoseconds();
}

void VideoPayload::deserialiseVideoCommon(DataStream &s) {
        int64_t ns = 0;
        s >> ns;
        if(s.status() != DataStream::Ok) return;
        _duration = Duration::fromNanoseconds(ns);
}

void UncompressedVideoPayload::serialisePayload(DataStream &s) const {
        s << desc();
        serialiseVideoCommon(s);
}

void UncompressedVideoPayload::deserialisePayload(DataStream &s) {
        ImageDesc d;
        s >> d;
        if(s.status() == DataStream::Ok) setDesc(d);
        deserialiseVideoCommon(s);
}

void CompressedVideoPayload::serialisePayload(DataStream &s) const {
        s << desc();
        serialiseVideoCommon(s);
        s << _frameType;
        const bool hasCodec = _inBandCodecData.isValid();
        s << hasCodec;
        if(hasCodec) s << _inBandCodecData;
}

void CompressedVideoPayload::deserialisePayload(DataStream &s) {
        ImageDesc d;
        s >> d;
        if(s.status() == DataStream::Ok) setDesc(d);
        deserialiseVideoCommon(s);

        FrameType ft;
        s >> ft;
        if(s.status() == DataStream::Ok) _frameType = ft;

        bool hasCodec = false;
        s >> hasCodec;
        if(s.status() == DataStream::Ok && hasCodec) {
                Buffer::Ptr b;
                s >> b;
                if(s.status() == DataStream::Ok) _inBandCodecData = b;
        }
}

void PcmAudioPayload::serialisePayload(DataStream &s) const {
        s << desc();
        s << static_cast<uint64_t>(sampleCount());
}

void PcmAudioPayload::deserialisePayload(DataStream &s) {
        AudioDesc d;
        s >> d;
        if(s.status() == DataStream::Ok) setDesc(d);

        uint64_t sc = 0;
        s >> sc;
        if(s.status() == DataStream::Ok) setSampleCount(static_cast<size_t>(sc));
}

void CompressedAudioPayload::serialisePayload(DataStream &s) const {
        s << desc();
        s << static_cast<uint64_t>(sampleCount());
        const bool hasCodec = _inBandCodecData.isValid();
        s << hasCodec;
        if(hasCodec) s << _inBandCodecData;
}

void CompressedAudioPayload::deserialisePayload(DataStream &s) {
        AudioDesc d;
        s >> d;
        if(s.status() == DataStream::Ok) setDesc(d);

        uint64_t sc = 0;
        s >> sc;
        if(s.status() == DataStream::Ok) setSampleCount(static_cast<size_t>(sc));

        bool hasCodec = false;
        s >> hasCodec;
        if(s.status() == DataStream::Ok && hasCodec) {
                Buffer::Ptr b;
                s >> b;
                if(s.status() == DataStream::Ok) _inBandCodecData = b;
        }
}

// ============================================================================
// VariantLookup registration — common fields every MediaPayload carries
//
// Everything on the polymorphic base surfaces through this block so a
// caller holding a @ref MediaPayload reference (or a base view like
// @ref VideoPayload / @ref AudioPayload) can reach PTS, DTS, duration,
// stream index, flags, and plane-count / byte-size without knowing
// the concrete leaf.  @ref VideoPayload and @ref AudioPayload declare
// @c inheritsFrom<MediaPayload>() so their registries layer on top of
// this block rather than duplicating it.
// ============================================================================

PROMEKI_LOOKUP_REGISTER(MediaPayload)
        .scalar("PTS",
                [](const MediaPayload &p) -> std::optional<Variant> {
                        return Variant(p.pts());
                },
                [](MediaPayload &p, const Variant &v) -> Error {
                        Error e;
                        MediaTimeStamp ts = v.get<MediaTimeStamp>(&e);
                        if(e.isError()) return Error::ConversionFailed;
                        p.setPts(ts);
                        return Error::Ok;
                })
        .scalar("DTS",
                [](const MediaPayload &p) -> std::optional<Variant> {
                        return Variant(p.dts());
                },
                [](MediaPayload &p, const Variant &v) -> Error {
                        Error e;
                        MediaTimeStamp ts = v.get<MediaTimeStamp>(&e);
                        if(e.isError()) return Error::ConversionFailed;
                        p.setDts(ts);
                        return Error::Ok;
                })
        .scalar("Duration",
                [](const MediaPayload &p) -> std::optional<Variant> {
                        if(!p.hasDuration()) return std::nullopt;
                        return Variant(p.duration());
                },
                [](MediaPayload &p, const Variant &v) -> Error {
                        Error e;
                        Duration d = v.get<Duration>(&e);
                        if(e.isError()) return Error::ConversionFailed;
                        return p.setDuration(d);
                })
        .scalar("HasDuration",
                [](const MediaPayload &p) -> std::optional<Variant> {
                        return Variant(p.hasDuration());
                })
        .scalar("StreamIndex",
                [](const MediaPayload &p) -> std::optional<Variant> {
                        return Variant(static_cast<int32_t>(p.streamIndex()));
                },
                [](MediaPayload &p, const Variant &v) -> Error {
                        Error e;
                        int32_t idx = v.get<int32_t>(&e);
                        if(e.isError()) return Error::ConversionFailed;
                        p.setStreamIndex(idx);
                        return Error::Ok;
                })
        .scalar("Flags",
                [](const MediaPayload &p) -> std::optional<Variant> {
                        return Variant(flagsToString(p.flags()));
                },
                [](MediaPayload &p, const Variant &v) -> Error {
                        // Accept either the string form (comma-separated
                        // names / bit indices emitted by the getter) or
                        // a raw integer mask for programmatic callers.
                        if(v.type() == Variant::TypeString) {
                                Error e;
                                String s = v.get<String>(&e);
                                if(e.isError()) return Error::ConversionFailed;
                                bool ok = true;
                                uint64_t f = stringToFlags(s, ok);
                                if(!ok) return Error::ConversionFailed;
                                p.setFlags(f);
                                return Error::Ok;
                        }
                        Error e;
                        uint64_t f = v.get<uint64_t>(&e);
                        if(e.isError()) return Error::ConversionFailed;
                        p.setFlags(f);
                        return Error::Ok;
                })
        .scalar("Kind",
                [](const MediaPayload &p) -> std::optional<Variant> {
                        return Variant(String(p.kind().valueName()));
                })
        .scalar("PlaneCount",
                [](const MediaPayload &p) -> std::optional<Variant> {
                        return Variant(static_cast<uint64_t>(p.planeCount()));
                })
        .scalar("ByteSize",
                [](const MediaPayload &p) -> std::optional<Variant> {
                        return Variant(static_cast<uint64_t>(p.size()));
                })
        .scalar("IsValid",
                [](const MediaPayload &p) -> std::optional<Variant> {
                        return Variant(p.isValid());
                })
        .scalar("IsCompressed",
                [](const MediaPayload &p) -> std::optional<Variant> {
                        return Variant(p.isCompressed());
                })
        .scalar("IsKeyframe",
                [](const MediaPayload &p) -> std::optional<Variant> {
                        return Variant(p.isKeyframe());
                })
        .scalar("IsSafeCutPoint",
                [](const MediaPayload &p) -> std::optional<Variant> {
                        return Variant(p.isSafeCutPoint());
                })
        .scalar("IsDiscardable",
                [](const MediaPayload &p) -> std::optional<Variant> {
                        return Variant(p.isDiscardable());
                })
        .scalar("IsCorrupt",
                [](const MediaPayload &p) -> std::optional<Variant> {
                        return Variant(p.isCorrupt());
                })
        .scalar("IsEndOfStream",
                [](const MediaPayload &p) -> std::optional<Variant> {
                        return Variant(p.isEndOfStream());
                })
        .scalar("IsExclusive",
                [](const MediaPayload &p) -> std::optional<Variant> {
                        return Variant(p.isExclusive());
                })
        .scalar("CorruptReason",
                [](const MediaPayload &p) -> std::optional<Variant> {
                        return Variant(p.corruptReason());
                })
        .scalar("SubclassFourCC",
                [](const MediaPayload &p) -> std::optional<Variant> {
                        // Render as the four-byte ASCII mnemonic for
                        // human-readable dumps; callers that need the
                        // raw integer can go through Variant::get.
                        uint32_t v = p.subclassFourCC();
                        char buf[5] = {
                                static_cast<char>((v >> 24) & 0xFF),
                                static_cast<char>((v >> 16) & 0xFF),
                                static_cast<char>((v >>  8) & 0xFF),
                                static_cast<char>((v      ) & 0xFF),
                                '\0'
                        };
                        return Variant(String(buf));
                })
        .database<"Metadata">("Meta",
                [](const MediaPayload &p) -> const VariantDatabase<"Metadata"> * {
                        return &p.metadata();
                },
                [](MediaPayload &p) -> VariantDatabase<"Metadata"> * {
                        return &p.metadata();
                })
        // Buffer[N].{Index,Offset,Size,BufferSize,IsValid}: expose
        // each BufferView slice as an indexed child.  Uses the
        // by-value overload because BufferView::Entry is a
        // lightweight proxy materialised on demand from the parent
        // BufferView — there's no stable pointer to the slice.
        .indexedChildByValue<BufferView::Entry>("Buffer",
                [](const MediaPayload &p, size_t idx) -> std::optional<BufferView::Entry> {
                        if(idx >= p.data().count()) return std::nullopt;
                        return p.data()[idx];
                });

// ============================================================================
// Concrete-leaf registrations for the codec-side payloads.
//
// The uncompressed-video and PCM-audio leaves live next to their own
// implementation files (uncompressedvideopayload.cpp /
// pcmaudiopayload.cpp); the compressed leaves don't have a dedicated
// .cpp yet so they piggy-back here alongside their serialise /
// deserialise hooks.
// ============================================================================

PROMEKI_LOOKUP_REGISTER(CompressedVideoPayload)
        .inheritsFrom<VideoPayload>()
        .scalar("FrameType",
                [](const CompressedVideoPayload &p) -> std::optional<Variant> {
                        return Variant(String(p.frameType().valueName()));
                })
        .scalar("IsParameterSet",
                [](const CompressedVideoPayload &p) -> std::optional<Variant> {
                        return Variant(p.isParameterSet());
                })
        .scalar("HasInBandCodecData",
                [](const CompressedVideoPayload &p) -> std::optional<Variant> {
                        return Variant(p.inBandCodecData().isValid());
                })
        .scalar("InBandCodecDataSize",
                [](const CompressedVideoPayload &p) -> std::optional<Variant> {
                        const auto &b = p.inBandCodecData();
                        return Variant(static_cast<uint64_t>(b.isValid() ? b->size() : 0u));
                });

PROMEKI_LOOKUP_REGISTER(CompressedAudioPayload)
        .inheritsFrom<AudioPayload>()
        .scalar("HasInBandCodecData",
                [](const CompressedAudioPayload &p) -> std::optional<Variant> {
                        return Variant(p.inBandCodecData().isValid());
                })
        .scalar("InBandCodecDataSize",
                [](const CompressedAudioPayload &p) -> std::optional<Variant> {
                        const auto &b = p.inBandCodecData();
                        return Variant(static_cast<uint64_t>(b.isValid() ? b->size() : 0u));
                });

PROMEKI_NAMESPACE_END

// Subclass registrations — each introduces a static initializer that
// calls MediaPayload::registerSubclass with the stable FourCC above.
PROMEKI_REGISTER_MEDIAPAYLOAD(UncompressedVideoPayload, "UVdp")
PROMEKI_REGISTER_MEDIAPAYLOAD(CompressedVideoPayload,   "CVdp")
PROMEKI_REGISTER_MEDIAPAYLOAD(PcmAudioPayload,          "PAdp")
PROMEKI_REGISTER_MEDIAPAYLOAD(CompressedAudioPayload,   "CAdp")
