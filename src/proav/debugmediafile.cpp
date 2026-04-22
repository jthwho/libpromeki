/**
 * @file      debugmediafile.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <promeki/debugmediafile.h>
#include <promeki/file.h>
#include <promeki/buffer.h>
#include <promeki/bufferiodevice.h>
#include <promeki/datastream.h>
#include <promeki/imagedesc.h>
#include <promeki/audiodesc.h>
#include <promeki/mediapacket.h>
#include <promeki/mediaconfig.h>
#include <promeki/logger.h>
#include <promeki/buildinfo.h>
#include <promeki/system.h>
#include <promeki/platform.h>
#include <promeki/util.h>
#ifdef PROMEKI_PLATFORM_WINDOWS
# include <process.h>
# define PROMEKI_PMDF_GETPID() (static_cast<int64_t>(_getpid()))
#else
# include <unistd.h>
# define PROMEKI_PMDF_GETPID() (static_cast<int64_t>(::getpid()))
#endif

PROMEKI_NAMESPACE_BEGIN

// ============================================================
// On-disk constants
// ============================================================

namespace {

constexpr size_t kSignatureSize    = 32;
constexpr size_t kChunkHeaderSize  = 16;

constexpr uint32_t kFourCC_SESN = 0x4E534553u;  ///< 'SESN' little-endian.
constexpr uint32_t kFourCC_FRAM = 0x4D415246u;  ///< 'FRAM'.
constexpr uint32_t kFourCC_IMAG = 0x47414D49u;  ///< 'IMAG'.
constexpr uint32_t kFourCC_AUDO = 0x4F445541u;  ///< 'AUDO'.
constexpr uint32_t kFourCC_TOC  = 0x20434F54u;  ///< 'TOC '.
constexpr uint32_t kFourCC_ENDF = 0x46444E45u;  ///< 'ENDF'.

constexpr uint32_t kFramTrailerMagic = kFourCC_FRAM;

// File signature layout:
//   [0..8)   magic
//   [8..12)  uint32 formatVersion (LE)
//   [12..16) uint32 fileFlags (LE)
//   [16..24) uint64 createTimeUs (LE) — informational only
//   [24..32) reserved zero
constexpr size_t kSigOffVersion    = 8;
constexpr size_t kSigOffFlags      = 12;
constexpr size_t kSigOffCreateTime = 16;

// Little-endian helpers.  The file format is little-endian for
// every fixed-size header field; DataStream-encoded payloads set
// their own byte order in the DataStream header.
static inline void writeU32LE(uint8_t *p, uint32_t v) {
        p[0] = static_cast<uint8_t>( v        & 0xFF);
        p[1] = static_cast<uint8_t>((v >>  8) & 0xFF);
        p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
        p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}
static inline void writeU64LE(uint8_t *p, uint64_t v) {
        for(int i = 0; i < 8; ++i) p[i] = static_cast<uint8_t>((v >> (i * 8)) & 0xFF);
}
static inline uint32_t readU32LE(const uint8_t *p) {
        return static_cast<uint32_t>(p[0])
             | (static_cast<uint32_t>(p[1]) <<  8)
             | (static_cast<uint32_t>(p[2]) << 16)
             | (static_cast<uint32_t>(p[3]) << 24);
}
static inline uint64_t readU64LE(const uint8_t *p) {
        uint64_t v = 0;
        for(int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(p[i]) << (i * 8);
        return v;
}

static int64_t currentTimeUs() {
        using namespace std::chrono;
        return duration_cast<microseconds>(
                system_clock::now().time_since_epoch()).count();
}

// Write a 16-byte chunk header at the current file position.
static Error writeChunkHeader(File &f, uint32_t fourCC, uint32_t flags, uint64_t payloadSize) {
        uint8_t hdr[kChunkHeaderSize];
        writeU32LE(hdr + 0, fourCC);
        writeU32LE(hdr + 4, flags);
        writeU64LE(hdr + 8, payloadSize);
        int64_t n = f.write(hdr, sizeof(hdr));
        if(n != static_cast<int64_t>(sizeof(hdr))) return Error::IOError;
        return Error::Ok;
}

// Read a 16-byte chunk header.
// Returns Error::EndOfFile when the file is truly at EOF (a short
// read yields IOError / CorruptData — a truncated header is corrupt).
static Error readChunkHeader(File &f, uint32_t &fourCC, uint32_t &flags, uint64_t &payloadSize) {
        uint8_t hdr[kChunkHeaderSize];
        int64_t n = f.read(hdr, sizeof(hdr));
        if(n == 0) return Error::EndOfFile;
        if(n != static_cast<int64_t>(sizeof(hdr))) return Error::CorruptData;
        fourCC      = readU32LE(hdr + 0);
        flags       = readU32LE(hdr + 4);
        payloadSize = readU64LE(hdr + 8);
        return Error::Ok;
}

// Open a BufferIODevice-backed DataStream into an owned Buffer.
// The Buffer is returned so the caller can inspect its bytes
// after the DataStream tears down.
static Buffer makeStagingBuffer(size_t initialCapacity = 4096) {
        Buffer b(initialCapacity);
        b.setSize(0);
        return b;
}

// Serialise a MediaPacket into a DataStream block (minus the buffer
// payload — the raw bytes travel with the enclosing plane / audio
// buffer so MediaPacket references can share the same backing
// Buffer::Ptr on round-trip).
//
// MediaTimeStamp is only emitted when valid: its toString/fromString
// round-trip does not cover the default-constructed invalid sentinel,
// so we wrap with a presence bit.  Duration is serialised as a raw
// int64 nanosecond count (no DataStream overload for Duration).
static void writePacketMeta(DataStream &s, const MediaPacket &pkt) {
        s << pkt.pixelDesc();
        bool hasPts = pkt.pts().isValid();
        s << hasPts;
        if(hasPts) s << pkt.pts();
        bool hasDts = pkt.dts().isValid();
        s << hasDts;
        if(hasDts) s << pkt.dts();
        s << static_cast<int64_t>(pkt.duration().nanoseconds());
        s << pkt.flags();
}

static Error readPacketMeta(DataStream &s, MediaPacket &out) {
        PixelDesc pd;
        bool hasPts = false, hasDts = false;
        MediaTimeStamp pts, dts;
        int64_t durNs = 0;
        uint32_t flags = 0;
        s >> pd >> hasPts;
        if(hasPts) s >> pts;
        s >> hasDts;
        if(hasDts) s >> dts;
        s >> durNs >> flags;
        if(s.status() != DataStream::Ok) return Error::CorruptData;
        out.setPixelDesc(pd);
        out.setPts(pts);
        out.setDts(dts);
        out.setDuration(Duration::fromNanoseconds(durNs));
        out.setFlags(flags);
        return Error::Ok;
}

// ---- IMAG sub-chunk -----------------------------------------

static Error serialiseImage(const Image &img, Buffer &outPayload) {
        BufferIODevice dev(&outPayload);
        dev.setAutoGrow(true);
        if(Error e = dev.open(IODevice::WriteOnly); e.isError()) return e;

        DataStream s = DataStream::createWriter(&dev);
        s << img.desc();

        bool hasPacket = img.packet().isValid();
        s << hasPacket;
        if(hasPacket) writePacketMeta(s, *img.packet());

        // Plane count + each plane (size, flags, reserved, alignment, raw bytes)
        const Buffer::PtrList &planes = img.planes();
        s << static_cast<uint32_t>(planes.size());

        for(const Buffer::Ptr &p : planes) {
                // Plane size is serialised as uint32 on the wire.  A >=4 GiB
                // plane would silently truncate the header size field while
                // still writing the full payload, producing an unrecoverable
                // file.  Assert rather than silently corrupt.
                PROMEKI_ASSERT(!p.isValid() || p->size() <= UINT32_MAX);
                uint32_t planeSize = p.isValid() ? static_cast<uint32_t>(p->size()) : 0u;
                uint32_t planeFlags = 0;
                uint32_t reserved   = 0;
                uint32_t alignment  = 0; // no alignment hint in v1
                s << planeSize << planeFlags << reserved << alignment;
                if(planeSize > 0) {
                        int64_t n = dev.write(p->data(), planeSize);
                        if(n != static_cast<int64_t>(planeSize)) {
                                dev.close();
                                return Error::IOError;
                        }
                }
        }
        if(s.status() != DataStream::Ok) {
                dev.close();
                return Error::IOError;
        }
        dev.close();
        return Error::Ok;
}

static Error deserialiseImage(DataStream &s, BufferIODevice &dev, Image::Ptr &out) {
        ImageDesc desc;
        s >> desc;
        if(s.status() != DataStream::Ok) return Error::CorruptData;
        if(!desc.isValid()) return Error::CorruptData;

        bool hasPacket = false;
        s >> hasPacket;
        if(s.status() != DataStream::Ok) return Error::CorruptData;

        MediaPacket::Ptr packet;
        if(hasPacket) {
                packet = MediaPacket::Ptr::create();
                Error e = readPacketMeta(s, *packet.modify());
                if(e.isError()) return e;
        }

        uint32_t planeCount = 0;
        s >> planeCount;
        if(s.status() != DataStream::Ok) return Error::CorruptData;

        // Allocate plane buffers sized to match the serialized sizes —
        // this handles both uncompressed (planes sized to the ImageDesc)
        // and compressed formats (single plane sized to the encoded
        // payload) uniformly.
        Buffer::PtrList planes;
        for(uint32_t i = 0; i < planeCount; ++i) {
                uint32_t planeSize = 0, planeFlags = 0, reserved = 0, alignment = 0;
                s >> planeSize >> planeFlags >> reserved >> alignment;
                if(s.status() != DataStream::Ok) return Error::CorruptData;
                if(planeFlags != 0) {
                        promekiErr("DebugMediaFile: plane %u has unsupported flags 0x%08x",
                                   i, planeFlags);
                        return Error::NotSupported;
                }

                Buffer::Ptr buf = Buffer::Ptr::create(
                        planeSize > 0 ? planeSize : size_t(1));
                if(planeSize > 0) {
                        int64_t n = dev.read(buf->data(), planeSize);
                        if(n != static_cast<int64_t>(planeSize)) return Error::CorruptData;
                        buf->setSize(planeSize);
                } else {
                        buf->setSize(0);
                }
                planes.pushToBack(buf);
        }

        // Build the Image: for a single-plane case use fromBuffer (zero-
        // copy adoption of the plane buffer), for multi-plane allocate
        // from the desc and copy each plane in.  Multi-plane
        // uncompressed is the only multi-plane case we hit today and
        // fromBuffer does not support it.
        Image::Ptr imgPtr;
        if(planes.size() == 1) {
                Image tmp = Image::fromBuffer(planes[0],
                                              desc.size().width(),
                                              desc.size().height(),
                                              desc.pixelDesc(),
                                              desc.metadata());
                if(!tmp.isValid()) return Error::CorruptData;
                imgPtr = Image::Ptr::create(std::move(tmp));
        } else {
                Image tmp(desc);
                if(!tmp.isValid()) return Error::CorruptData;
                for(size_t i = 0; i < planes.size() && i < tmp.planes().size(); ++i) {
                        size_t n = std::min(planes[i]->size(), tmp.plane(i)->availSize());
                        std::memcpy(tmp.plane(i)->data(), planes[i]->data(), n);
                        tmp.plane(i)->setSize(n);
                }
                imgPtr = Image::Ptr::create(std::move(tmp));
        }

        if(packet.isValid()) {
                if(imgPtr->planes().size() > 0 && imgPtr->plane(0).isValid()) {
                        packet.modify()->setBuffer(imgPtr->plane(0));
                }
                imgPtr.modify()->setPacket(packet);
        }
        out = std::move(imgPtr);
        return Error::Ok;
}

// ---- AUDO sub-chunk -----------------------------------------

static Error serialiseAudio(const Audio &aud, Buffer &outPayload) {
        BufferIODevice dev(&outPayload);
        dev.setAutoGrow(true);
        if(Error e = dev.open(IODevice::WriteOnly); e.isError()) return e;

        DataStream s = DataStream::createWriter(&dev);
        s << aud.desc();
        s << static_cast<uint64_t>(aud.samples());
        s << static_cast<uint64_t>(aud.maxSamples());

        bool hasPacket = aud.packet().isValid();
        s << hasPacket;
        if(hasPacket) writePacketMeta(s, *aud.packet());

        const Buffer::Ptr &buf = aud.buffer();
        PROMEKI_ASSERT(!buf.isValid() || buf->size() <= UINT32_MAX);
        uint32_t bufSize    = buf.isValid() ? static_cast<uint32_t>(buf->size()) : 0u;
        uint32_t bufFlags   = 0;
        uint32_t reserved   = 0;
        uint32_t alignment  = 0;
        s << bufSize << bufFlags << reserved << alignment;
        if(bufSize > 0) {
                int64_t n = dev.write(buf->data(), bufSize);
                if(n != static_cast<int64_t>(bufSize)) {
                        dev.close();
                        return Error::IOError;
                }
        }
        if(s.status() != DataStream::Ok) {
                dev.close();
                return Error::IOError;
        }
        dev.close();
        return Error::Ok;
}

static Error deserialiseAudio(DataStream &s, BufferIODevice &dev, Audio::Ptr &out) {
        AudioDesc desc;
        s >> desc;
        if(s.status() != DataStream::Ok) return Error::CorruptData;
        if(!desc.isValid()) return Error::CorruptData;

        uint64_t samples = 0, maxSamples = 0;
        s >> samples >> maxSamples;
        if(s.status() != DataStream::Ok) return Error::CorruptData;

        bool hasPacket = false;
        s >> hasPacket;
        if(s.status() != DataStream::Ok) return Error::CorruptData;

        MediaPacket::Ptr packet;
        if(hasPacket) {
                packet = MediaPacket::Ptr::create();
                Error e = readPacketMeta(s, *packet.modify());
                if(e.isError()) return e;
        }

        uint32_t bufSize = 0, bufFlags = 0, reserved = 0, alignment = 0;
        s >> bufSize >> bufFlags >> reserved >> alignment;
        if(s.status() != DataStream::Ok) return Error::CorruptData;
        if(bufFlags != 0) {
                promekiErr("DebugMediaFile: audio buffer has unsupported flags 0x%08x", bufFlags);
                return Error::NotSupported;
        }

        Buffer::Ptr audioBuf;
        if(bufSize > 0) {
                audioBuf = Buffer::Ptr::create(bufSize);
                int64_t n = dev.read(audioBuf->data(), bufSize);
                if(n != static_cast<int64_t>(bufSize)) return Error::CorruptData;
                audioBuf->setSize(bufSize);
        } else {
                audioBuf = Buffer::Ptr::create(static_cast<size_t>(maxSamples * desc.bytesPerSampleStride()));
                audioBuf->setSize(0);
        }

        Audio aud = Audio::fromBuffer(audioBuf, desc);
        if(!aud.isValid()) return Error::CorruptData;
        aud.resize(static_cast<size_t>(samples));

        Audio::Ptr audPtr = Audio::Ptr::create(std::move(aud));
        if(packet.isValid()) {
                packet.modify()->setBuffer(audioBuf);
                audPtr.modify()->setPacket(packet);
        }
        out = std::move(audPtr);
        return Error::Ok;
}

} // namespace

// ============================================================
// DebugMediaFile
// ============================================================

DebugMediaFile::DebugMediaFile(ObjectBase *parent) : ObjectBase(parent) {}

DebugMediaFile::~DebugMediaFile() {
        if(_mode != NotOpen) {
                (void)close();
        }
}

Error DebugMediaFile::open(const String &filename, Mode mode, const OpenOptions &opts) {
        if(_mode != NotOpen) return Error::AlreadyOpen;
        if(mode != Read && mode != Write) return Error::InvalidArgument;

        _filename       = filename;
        _mode           = mode;
        _framesWritten  = 0;
        _readCursor     = 0;
        _indexBuilt     = false;
        _index.clear();
        _sessionInfo    = Metadata();
        _fileFlags      = 0;
        _fileVersion    = kFormatVersion;
        _firstFramePos  = 0;

        // Parented to this so the ObjectBase destructor tree handles
        // cleanup if the caller drops us without calling close().
        _file = new File(filename, this);

        if(mode == Write) {
                Error e = _file->open(IODevice::WriteOnly,
                                      File::Create | File::Truncate);
                if(e.isError()) {
                        delete _file; _file = nullptr;
                        _mode = NotOpen;
                        return e;
                }
                if(Error se = writeSignature(0); se.isError()) {
                        _file->close(); delete _file; _file = nullptr; _mode = NotOpen;
                        return se;
                }
                // Start from caller-supplied session info, fold in the
                // standard MediaIO write defaults (Software / Date /
                // OriginationDateTime / Originator / OriginatorReference /
                // UMID — all setIfMissing so caller values win), then
                // stamp PMDF-specific session details.  Everything is
                // setIfMissing so callers can pre-set any key to override
                // the auto-stamp.
                Metadata sessionInfo = opts.sessionInfo;
                sessionInfo.applyMediaIOWriteDefaults();
                sessionInfo.setIfMissing(Metadata::SessionHostname,
                                         System::hostname());
                sessionInfo.setIfMissing(Metadata::SessionProcessId,
                                         PROMEKI_PMDF_GETPID());
                sessionInfo.setIfMissing(Metadata::LibraryBuildInfo,
                                         buildInfoString());
                sessionInfo.setIfMissing(Metadata::LibraryPlatform,
                                         buildPlatformString());
                sessionInfo.setIfMissing(Metadata::LibraryFeatures,
                                         buildFeatureString());

                if(Error se = writeSessionChunk(sessionInfo); se.isError()) {
                        _file->close(); delete _file; _file = nullptr; _mode = NotOpen;
                        return se;
                }
                _sessionInfo   = std::move(sessionInfo);
                _firstFramePos = _file->pos();
                return Error::Ok;
        }

        // Read mode.
        Error e = _file->open(IODevice::ReadOnly);
        if(e.isError()) {
                delete _file; _file = nullptr;
                _mode = NotOpen;
                return e;
        }
        if(Error se = readSignature(); se.isError()) {
                _file->close(); delete _file; _file = nullptr; _mode = NotOpen;
                return se;
        }
        if(Error se = readSessionChunk(); se.isError()) {
                _file->close(); delete _file; _file = nullptr; _mode = NotOpen;
                return se;
        }
        _firstFramePos = _file->pos();
        return Error::Ok;
}

Error DebugMediaFile::close() {
        if(_mode == NotOpen) return Error::Ok;

        Error ret = Error::Ok;
        if(_mode == Write) {
                Error e = appendFooter();
                if(e.isError()) ret = e;
        }
        if(_file != nullptr) _file->close();
        delete _file;
        _file = nullptr;
        _mode = NotOpen;
        return ret;
}

Error DebugMediaFile::writeSignature(uint32_t flags) {
        uint8_t sig[kSignatureSize] = {0};
        std::memcpy(sig, kMagic, sizeof(kMagic));
        writeU32LE(sig + kSigOffVersion, kFormatVersion);
        writeU32LE(sig + kSigOffFlags,   flags);
        writeU64LE(sig + kSigOffCreateTime, static_cast<uint64_t>(currentTimeUs()));
        // Bytes 24..31 reserved zero.
        int64_t n = _file->write(sig, sizeof(sig));
        if(n != static_cast<int64_t>(sizeof(sig))) return Error::IOError;
        return Error::Ok;
}

Error DebugMediaFile::writeSessionChunk(const Metadata &sessionInfo) {
        Buffer payload = makeStagingBuffer();
        {
                BufferIODevice dev(&payload);
                dev.setAutoGrow(true);
                Error e = dev.open(IODevice::WriteOnly);
                if(e.isError()) return e;
                DataStream s = DataStream::createWriter(&dev);
                s << sessionInfo;
                if(s.status() != DataStream::Ok) {
                        dev.close();
                        return Error::IOError;
                }
                dev.close();
        }
        Error he = writeChunkHeader(*_file, kFourCC_SESN, 0, payload.size());
        if(he.isError()) return he;
        int64_t n = _file->write(payload.data(), payload.size());
        if(n != static_cast<int64_t>(payload.size())) return Error::IOError;
        return Error::Ok;
}

Error DebugMediaFile::readSignature() {
        uint8_t sig[kSignatureSize];
        int64_t n = _file->read(sig, sizeof(sig));
        if(n != static_cast<int64_t>(sizeof(sig))) return Error::CorruptData;
        if(std::memcmp(sig, kMagic, sizeof(kMagic)) != 0) {
                promekiErr("DebugMediaFile: bad magic on %s", _filename.cstr());
                return Error::CorruptData;
        }
        _fileVersion = readU32LE(sig + kSigOffVersion);
        _fileFlags   = readU32LE(sig + kSigOffFlags);
        if(_fileVersion != kFormatVersion) {
                promekiErr("DebugMediaFile: unsupported format version %u (expected %u)",
                           _fileVersion, kFormatVersion);
                return Error::NotSupported;
        }
        return Error::Ok;
}

Error DebugMediaFile::readSessionChunk() {
        uint32_t fourCC = 0, flags = 0;
        uint64_t size   = 0;
        Error e = readChunkHeader(*_file, fourCC, flags, size);
        if(e.isError()) return e;
        if(fourCC != kFourCC_SESN) return Error::CorruptData;

        Buffer payload(static_cast<size_t>(size) + 8);
        int64_t n = _file->read(payload.data(), static_cast<int64_t>(size));
        if(n != static_cast<int64_t>(size)) return Error::CorruptData;
        payload.setSize(static_cast<size_t>(size));

        BufferIODevice dev(&payload);
        if(Error oe = dev.open(IODevice::ReadOnly); oe.isError()) return oe;
        DataStream s = DataStream::createReader(&dev);
        Metadata meta;
        s >> meta;
        dev.close();
        if(s.status() != DataStream::Ok) return Error::CorruptData;
        _sessionInfo = std::move(meta);
        return Error::Ok;
}

// ---- Write path --------------------------------------------

Error DebugMediaFile::writeFrame(const Frame::Ptr &frame) {
        if(_mode != Write) return Error::NotOpen;
        if(!frame.isValid()) return Error::InvalidArgument;

        // Stage the FRAM payload in memory so we know the chunk size
        // before we commit it to the file.  This also contains the
        // trailing magic.
        Buffer payload = makeStagingBuffer(8192);
        BufferIODevice dev(&payload);
        dev.setAutoGrow(true);
        if(Error e = dev.open(IODevice::WriteOnly); e.isError()) return e;
        DataStream s = DataStream::createWriter(&dev);

        s << static_cast<uint64_t>(_framesWritten.value());
        s << frame->metadata();
        s << frame->configUpdate();
        s << static_cast<uint32_t>(frame->imageList().size());
        s << static_cast<uint32_t>(frame->audioList().size());

        if(s.status() != DataStream::Ok) { dev.close(); return Error::IOError; }

        // Helper: write exactly n bytes to the staging device.  A short
        // write on the in-memory BufferIODevice means the resize /
        // auto-grow failed, which would silently corrupt the frame
        // payload if left unchecked.
        auto writeAll = [&dev](const void *data, size_t n) -> Error {
                int64_t w = dev.write(data, n);
                return w == static_cast<int64_t>(n) ? Error::Ok : Error::IOError;
        };

        // Images — each one a nested IMAG sub-chunk with its own
        // 16-byte header so a future tool can skip per-image.
        for(const Image::Ptr &imgPtr : frame->imageList()) {
                if(!imgPtr.isValid()) {
                        // Emit an empty IMAG so counts match.
                        uint8_t hdr[kChunkHeaderSize] = {0};
                        writeU32LE(hdr + 0, kFourCC_IMAG);
                        writeU32LE(hdr + 4, 0);
                        writeU64LE(hdr + 8, 0);
                        if(Error we = writeAll(hdr, sizeof(hdr)); we.isError()) {
                                dev.close(); return we;
                        }
                        continue;
                }
                Buffer imgPayload = makeStagingBuffer(8192);
                Error e = serialiseImage(*imgPtr, imgPayload);
                if(e.isError()) { dev.close(); return e; }

                uint8_t hdr[kChunkHeaderSize] = {0};
                writeU32LE(hdr + 0, kFourCC_IMAG);
                writeU32LE(hdr + 4, 0);
                writeU64LE(hdr + 8, imgPayload.size());
                if(Error we = writeAll(hdr, sizeof(hdr)); we.isError()) {
                        dev.close(); return we;
                }
                if(Error we = writeAll(imgPayload.data(), imgPayload.size()); we.isError()) {
                        dev.close(); return we;
                }
        }

        // Audio tracks.
        for(const Audio::Ptr &audPtr : frame->audioList()) {
                if(!audPtr.isValid()) {
                        uint8_t hdr[kChunkHeaderSize] = {0};
                        writeU32LE(hdr + 0, kFourCC_AUDO);
                        writeU32LE(hdr + 4, 0);
                        writeU64LE(hdr + 8, 0);
                        if(Error we = writeAll(hdr, sizeof(hdr)); we.isError()) {
                                dev.close(); return we;
                        }
                        continue;
                }
                Buffer audPayload = makeStagingBuffer(4096);
                Error e = serialiseAudio(*audPtr, audPayload);
                if(e.isError()) { dev.close(); return e; }

                uint8_t hdr[kChunkHeaderSize] = {0};
                writeU32LE(hdr + 0, kFourCC_AUDO);
                writeU32LE(hdr + 4, 0);
                writeU64LE(hdr + 8, audPayload.size());
                if(Error we = writeAll(hdr, sizeof(hdr)); we.isError()) {
                        dev.close(); return we;
                }
                if(Error we = writeAll(audPayload.data(), audPayload.size()); we.isError()) {
                        dev.close(); return we;
                }
        }

        // Trailing magic for cheap resync on corruption.
        uint32_t trailer = kFramTrailerMagic;
        s << trailer;

        if(s.status() != DataStream::Ok) { dev.close(); return Error::IOError; }
        dev.close();

        // Record the file offset so the TOC written at close points here.
        int64_t offset = _file->pos();
        FrameIndexEntry e{};
        e.fileOffset     = offset;
        e.frameNumber    = toFrameNumber(_framesWritten);
        e.presentationUs = 0; // Reserved for future use.
        _index.pushToBack(e);
        _indexBuilt = true;

        // Commit the chunk: header + payload.
        Error he = writeChunkHeader(*_file, kFourCC_FRAM, 0, payload.size());
        if(he.isError()) return he;
        int64_t n = _file->write(payload.data(), payload.size());
        if(n != static_cast<int64_t>(payload.size())) return Error::IOError;

        ++_framesWritten;
        return Error::Ok;
}

Error DebugMediaFile::appendFooter() {
        if(_framesWritten == 0) return Error::Ok;
        int64_t tocOffset = _file->pos();

        // TOC chunk.
        Buffer tocPayload = makeStagingBuffer(_index.size() * 32 + 64);
        {
                BufferIODevice dev(&tocPayload);
                dev.setAutoGrow(true);
                if(Error e = dev.open(IODevice::WriteOnly); e.isError()) return e;
                DataStream s = DataStream::createWriter(&dev);
                s << static_cast<uint64_t>(_index.size());
                for(const FrameIndexEntry &ent : _index) {
                        s << ent.fileOffset
                          << static_cast<int64_t>(ent.frameNumber.value())
                          << ent.presentationUs;
                }
                if(s.status() != DataStream::Ok) { dev.close(); return Error::IOError; }
                dev.close();
        }
        if(Error he = writeChunkHeader(*_file, kFourCC_TOC, 0, tocPayload.size()); he.isError())
                return he;
        int64_t n = _file->write(tocPayload.data(), tocPayload.size());
        if(n != static_cast<int64_t>(tocPayload.size())) return Error::IOError;

        // ENDF chunk: uint64 tocOffset.
        Buffer endPayload(16);
        endPayload.setSize(0);
        {
                BufferIODevice dev(&endPayload);
                dev.setAutoGrow(true);
                if(Error e = dev.open(IODevice::WriteOnly); e.isError()) return e;
                DataStream s = DataStream::createWriter(&dev);
                s << static_cast<int64_t>(tocOffset);
                if(s.status() != DataStream::Ok) { dev.close(); return Error::IOError; }
                dev.close();
        }
        if(Error he = writeChunkHeader(*_file, kFourCC_ENDF, 0, endPayload.size()); he.isError())
                return he;
        int64_t n2 = _file->write(endPayload.data(), endPayload.size());
        if(n2 != static_cast<int64_t>(endPayload.size())) return Error::IOError;

        // Rewrite the file-flags field on the signature to mark the footer present.
        if(Error se = _file->seek(kSigOffFlags); se.isError()) return se;
        uint8_t flagsBytes[4];
        writeU32LE(flagsBytes, kFileFlagHasFooter);
        int64_t nf = _file->write(flagsBytes, sizeof(flagsBytes));
        if(nf != 4) return Error::IOError;
        _fileFlags = kFileFlagHasFooter;
        return Error::Ok;
}

// ---- Read path ---------------------------------------------

Error DebugMediaFile::readFrame(Frame::Ptr &out) {
        if(_mode != Read) return Error::NotOpen;

        uint32_t fourCC = 0, flags = 0;
        uint64_t size   = 0;
        while(true) {
                Error e = readChunkHeader(*_file, fourCC, flags, size);
                if(e == Error::EndOfFile) return Error::EndOfFile;
                if(e.isError()) return e;

                // Skip non-FRAM chunks (TOC / ENDF / unknown).
                if(fourCC != kFourCC_FRAM) {
                        if(Error se = _file->seek(_file->pos() + static_cast<int64_t>(size)); se.isError())
                                return se;
                        continue;
                }
                break;
        }

        // Read entire FRAM payload into memory and deserialise.
        Buffer payload(static_cast<size_t>(size) + 8);
        int64_t n = _file->read(payload.data(), static_cast<int64_t>(size));
        if(n != static_cast<int64_t>(size)) return Error::CorruptData;
        payload.setSize(static_cast<size_t>(size));

        BufferIODevice dev(&payload);
        if(Error oe = dev.open(IODevice::ReadOnly); oe.isError()) return oe;
        DataStream s = DataStream::createReader(&dev);

        uint64_t frameIdx = 0;
        Metadata md;
        MediaConfig cfg;
        uint32_t imageCount = 0, audioCount = 0;
        s >> frameIdx >> md >> cfg >> imageCount >> audioCount;
        if(s.status() != DataStream::Ok) return Error::CorruptData;

        Frame::Ptr frame = Frame::Ptr::create();
        Frame *raw = frame.modify();
        raw->metadata()     = std::move(md);
        raw->configUpdate() = std::move(cfg);

        for(uint32_t i = 0; i < imageCount; ++i) {
                // IMAG sub-chunk header.
                uint8_t hdr[kChunkHeaderSize];
                int64_t hn = dev.read(hdr, sizeof(hdr));
                if(hn != static_cast<int64_t>(sizeof(hdr))) return Error::CorruptData;
                uint32_t fc    = readU32LE(hdr + 0);
                uint64_t isize = readU64LE(hdr + 8);
                if(fc != kFourCC_IMAG) return Error::CorruptData;

                if(isize == 0) {
                        raw->imageList().pushToBack(Image::Ptr());
                        continue;
                }
                // Use a nested BufferIODevice over the same backing buffer —
                // we just need a start/length window.  Simpler: read the
                // sub-chunk bytes into their own buffer.
                Buffer sub(static_cast<size_t>(isize));
                int64_t sn = dev.read(sub.data(), static_cast<int64_t>(isize));
                if(sn != static_cast<int64_t>(isize)) return Error::CorruptData;
                sub.setSize(static_cast<size_t>(isize));

                BufferIODevice subDev(&sub);
                if(Error oe = subDev.open(IODevice::ReadOnly); oe.isError()) return oe;
                DataStream ss = DataStream::createReader(&subDev);
                Image::Ptr imgPtr;
                Error ie = deserialiseImage(ss, subDev, imgPtr);
                subDev.close();
                if(ie.isError()) return ie;
                raw->imageList().pushToBack(imgPtr);
        }

        for(uint32_t i = 0; i < audioCount; ++i) {
                uint8_t hdr[kChunkHeaderSize];
                int64_t hn = dev.read(hdr, sizeof(hdr));
                if(hn != static_cast<int64_t>(sizeof(hdr))) return Error::CorruptData;
                uint32_t fc    = readU32LE(hdr + 0);
                uint64_t asize = readU64LE(hdr + 8);
                if(fc != kFourCC_AUDO) return Error::CorruptData;

                if(asize == 0) {
                        raw->audioList().pushToBack(Audio::Ptr());
                        continue;
                }
                Buffer sub(static_cast<size_t>(asize));
                int64_t sn = dev.read(sub.data(), static_cast<int64_t>(asize));
                if(sn != static_cast<int64_t>(asize)) return Error::CorruptData;
                sub.setSize(static_cast<size_t>(asize));

                BufferIODevice subDev(&sub);
                if(Error oe = subDev.open(IODevice::ReadOnly); oe.isError()) return oe;
                DataStream ss = DataStream::createReader(&subDev);
                Audio::Ptr audPtr;
                Error ae = deserialiseAudio(ss, subDev, audPtr);
                subDev.close();
                if(ae.isError()) return ae;
                raw->audioList().pushToBack(audPtr);
        }

        // Trailer magic.
        uint32_t trailer = 0;
        s >> trailer;
        if(s.status() != DataStream::Ok || trailer != kFramTrailerMagic) {
                promekiWarn("DebugMediaFile: FRAM trailer mismatch (expected 0x%08x, got 0x%08x)",
                            kFramTrailerMagic, trailer);
                // Non-fatal: the frame content is already assembled.
        }

        ++_readCursor;
        out = std::move(frame);
        return Error::Ok;
}

Error DebugMediaFile::buildIndex() const {
        if(_indexBuilt) return Error::Ok;
        if(_mode != Read) return Error::NotOpen;

        _index.clear();

        // If a footer is advertised, read it for a single-shot index build.
        if(hasFooter()) {
                int64_t savedPos = _file->pos();
                auto fsz = _file->size();
                if(!fsz.second().isOk()) return fsz.second();
                int64_t fileSize = fsz.first();
                if(fileSize < static_cast<int64_t>(kChunkHeaderSize)) return Error::CorruptData;

                // ENDF chunk is the last chunk.  Its payload is a single
                // DataStream-tagged int64 (the TOC offset).  We know the
                // ENDF payload size is fixed-ish but cheaper to walk back
                // from the current known structure: scan forward from the
                // first-frame position looking for TOC then ENDF.
                if(Error e = _file->seek(_firstFramePos); e.isError()) return e;
                int64_t tocOffset = -1;
                while(_file->pos() < fileSize) {
                        uint32_t fc = 0, fl = 0;
                        uint64_t sz = 0;
                        Error e = readChunkHeader(*_file, fc, fl, sz);
                        if(e == Error::EndOfFile) break;
                        if(e.isError()) return e;
                        if(fc == kFourCC_TOC) {
                                tocOffset = _file->pos() - static_cast<int64_t>(kChunkHeaderSize);
                                Buffer payload(static_cast<size_t>(sz) + 8);
                                int64_t n = _file->read(payload.data(), static_cast<int64_t>(sz));
                                if(n != static_cast<int64_t>(sz)) return Error::CorruptData;
                                payload.setSize(static_cast<size_t>(sz));
                                BufferIODevice dev(&payload);
                                if(Error oe = dev.open(IODevice::ReadOnly); oe.isError()) return oe;
                                DataStream s = DataStream::createReader(&dev);
                                uint64_t count = 0;
                                s >> count;
                                for(uint64_t i = 0; i < count && s.status() == DataStream::Ok; ++i) {
                                        FrameIndexEntry ent;
                                        int64_t fnum = 0;
                                        s >> ent.fileOffset >> fnum >> ent.presentationUs;
                                        ent.frameNumber = FrameNumber(fnum);
                                        _index.pushToBack(ent);
                                }
                                dev.close();
                        } else if(fc == kFourCC_ENDF) {
                                // Cross-check: ENDF's payload is a single
                                // int64 pointing at the TOC chunk header.
                                // A mismatch here means the footer was
                                // corrupted between TOC write and ENDF
                                // write — invalidate the index we just
                                // built and fall through to linear scan.
                                Buffer endPayload(static_cast<size_t>(sz) + 8);
                                int64_t en = _file->read(endPayload.data(),
                                                         static_cast<int64_t>(sz));
                                if(en != static_cast<int64_t>(sz)) return Error::CorruptData;
                                endPayload.setSize(static_cast<size_t>(sz));
                                BufferIODevice endDev(&endPayload);
                                if(Error oe = endDev.open(IODevice::ReadOnly); oe.isError()) return oe;
                                DataStream es = DataStream::createReader(&endDev);
                                int64_t recorded = -1;
                                es >> recorded;
                                endDev.close();
                                if(es.status() != DataStream::Ok || recorded != tocOffset) {
                                        promekiWarn("DebugMediaFile: ENDF tocOffset mismatch "
                                                    "(expected %lld, got %lld); rescanning",
                                                    static_cast<long long>(tocOffset),
                                                    static_cast<long long>(recorded));
                                        _index.clear();
                                }
                                break;
                        } else {
                                if(Error se = _file->seek(_file->pos() + static_cast<int64_t>(sz)); se.isError())
                                        return se;
                        }
                }
                if(Error se = _file->seek(savedPos); se.isError()) return se;
                if(!_index.isEmpty()) { _indexBuilt = true; return Error::Ok; }
                // Fall through to linear scan if footer was malformed.
        }

        // Linear scan.
        int64_t savedPos = _file->pos();
        if(Error e = _file->seek(_firstFramePos); e.isError()) return e;
        while(true) {
                int64_t chunkStart = _file->pos();
                uint32_t fc = 0, fl = 0;
                uint64_t sz = 0;
                Error e = readChunkHeader(*_file, fc, fl, sz);
                if(e == Error::EndOfFile) break;
                if(e.isError()) break;

                auto fsz = _file->size();
                int64_t fileSize = fsz.second().isOk() ? fsz.first() : -1;
                if(fileSize >= 0 && chunkStart + static_cast<int64_t>(kChunkHeaderSize)
                                    + static_cast<int64_t>(sz) > fileSize) {
                        // Truncated trailing chunk — stop.
                        break;
                }

                if(fc == kFourCC_FRAM) {
                        FrameIndexEntry ent;
                        ent.fileOffset     = chunkStart;
                        ent.frameNumber    = FrameNumber(static_cast<int64_t>(_index.size()));
                        ent.presentationUs = 0;
                        _index.pushToBack(ent);
                }

                if(Error se = _file->seek(_file->pos() + static_cast<int64_t>(sz)); se.isError())
                        break;
        }
        if(Error se = _file->seek(savedPos); se.isError()) return se;
        _indexBuilt = true;
        return Error::Ok;
}

Error DebugMediaFile::seek(const FrameNumber &frameNumber) {
        if(_mode != Read) return Error::NotOpen;
        Error e = buildIndex();
        if(e.isError()) return e;
        if(!frameNumber.isValid()
           || frameNumber.value() >= static_cast<int64_t>(_index.size()))
                return Error::IllegalSeek;
        Error se = _file->seek(_index[frameNumber.value()].fileOffset);
        if(se.isError()) return se;
        _readCursor = frameNumber;
        return Error::Ok;
}

Error DebugMediaFile::readFrameAt(const FrameNumber &frameNumber, Frame::Ptr &out) {
        if(Error e = seek(frameNumber); e.isError()) return e;
        return readFrame(out);
}

FrameCount DebugMediaFile::frameCount() const {
        if(_mode == Write) return _framesWritten;
        if(buildIndex().isError()) return FrameCount(0);
        return FrameCount(static_cast<int64_t>(_index.size()));
}

const List<DebugMediaFile::FrameIndexEntry> &DebugMediaFile::index() const {
        if(_mode == Read && !_indexBuilt) buildIndex();
        return _index;
}

PROMEKI_NAMESPACE_END
