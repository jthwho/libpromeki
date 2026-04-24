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
#include <promeki/uncompressedvideopayload.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/uncompressedaudiopayload.h>
#include <promeki/compressedaudiopayload.h>
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
//   int64    duration ns
//   uint32   stream index (stored as int but written unsigned for
//            simplicity — negative values are not expected on-wire).
//   uint32   flags
//   Metadata metadata
//   <subclass-specific tail via serialisePayload>
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
        s << static_cast<int64_t>(mp.duration().nanoseconds());
        s << static_cast<int32_t>(mp.streamIndex());
        s << static_cast<uint32_t>(mp.flags());
        s << mp.metadata();

        // Subclass tail.
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
        int64_t durationNs = 0;
        int32_t streamIdx = 0;
        uint32_t flags = 0;
        Metadata meta;
        bool hasPts = false;
        s >> hasPts;
        if(hasPts) s >> pts;
        bool hasDts = false;
        s >> hasDts;
        if(hasDts) s >> dts;
        s >> durationNs;
        s >> streamIdx;
        s >> flags;
        s >> meta;
        if(s.status() != DataStream::Ok) { p = MediaPayload::Ptr(); return s; }
        raw->setPts(pts);
        raw->setDts(dts);
        raw->setDuration(Duration::fromNanoseconds(durationNs));
        raw->setStreamIndex(streamIdx);
        raw->setFlags(flags);
        raw->metadata() = std::move(meta);

        raw->deserialisePayload(s);
        if(s.status() != DataStream::Ok) { p = MediaPayload::Ptr(); return s; }
        p = std::move(out);
        return s;
}

// ============================================================================
// Concrete subclass serialise / deserialise hooks
// ============================================================================

void UncompressedVideoPayload::serialisePayload(DataStream &s) const {
        s << desc();
}

void UncompressedVideoPayload::deserialisePayload(DataStream &s) {
        ImageDesc d;
        s >> d;
        if(s.status() == DataStream::Ok) setDesc(d);
}

void CompressedVideoPayload::serialisePayload(DataStream &s) const {
        s << desc();
        s << _frameType;
        const bool hasCodec = _inBandCodecData.isValid();
        s << hasCodec;
        if(hasCodec) s << _inBandCodecData;
}

void CompressedVideoPayload::deserialisePayload(DataStream &s) {
        ImageDesc d;
        s >> d;
        if(s.status() == DataStream::Ok) setDesc(d);

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

void UncompressedAudioPayload::serialisePayload(DataStream &s) const {
        s << desc();
        s << static_cast<uint64_t>(_sampleCount);
}

void UncompressedAudioPayload::deserialisePayload(DataStream &s) {
        AudioDesc d;
        s >> d;
        if(s.status() == DataStream::Ok) setDesc(d);

        uint64_t sc = 0;
        s >> sc;
        if(s.status() == DataStream::Ok) _sampleCount = static_cast<size_t>(sc);
}

void CompressedAudioPayload::serialisePayload(DataStream &s) const {
        s << desc();
        const bool hasCodec = _inBandCodecData.isValid();
        s << hasCodec;
        if(hasCodec) s << _inBandCodecData;
}

void CompressedAudioPayload::deserialisePayload(DataStream &s) {
        AudioDesc d;
        s >> d;
        if(s.status() == DataStream::Ok) setDesc(d);

        bool hasCodec = false;
        s >> hasCodec;
        if(s.status() == DataStream::Ok && hasCodec) {
                Buffer::Ptr b;
                s >> b;
                if(s.status() == DataStream::Ok) _inBandCodecData = b;
        }
}

PROMEKI_NAMESPACE_END

// Subclass registrations — each introduces a static initializer that
// calls MediaPayload::registerSubclass with the stable FourCC above.
PROMEKI_REGISTER_MEDIAPAYLOAD(UncompressedVideoPayload, "UVdp")
PROMEKI_REGISTER_MEDIAPAYLOAD(CompressedVideoPayload,   "CVdp")
PROMEKI_REGISTER_MEDIAPAYLOAD(UncompressedAudioPayload, "UAdp")
PROMEKI_REGISTER_MEDIAPAYLOAD(CompressedAudioPayload,   "CAdp")
