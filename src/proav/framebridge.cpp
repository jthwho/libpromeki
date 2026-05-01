/**
 * @file      framebridge.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/framebridge.h>
#include <promeki/sharedmemory.h>
#include <promeki/localsocket.h>
#include <promeki/localserver.h>
#include <promeki/klvframe.h>
#include <promeki/bufferiodevice.h>
#include <promeki/datastream.h>
#include <promeki/fnv1a.h>
#include <promeki/logger.h>
#include <promeki/dir.h>
#include <promeki/fourcc.h>
#include <promeki/buildinfo.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/metadata.h>
#include <promeki/pixelformat.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // ============================================================================
        // Protocol constants.  Kept in an internal namespace so the wire format is
        // not part of the public API surface.  All fields are network byte order on
        // the wire (via DataStream's default or the KLV header's explicit BE).
        // ============================================================================

        constexpr FourCC KeyHELO("HELO"); // input → output, handshake opening
        constexpr FourCC KeyACPT("ACPT"); // output → input, handshake accept
        constexpr FourCC KeyRJCT("RJCT"); // output → input, handshake reject
        constexpr FourCC KeyREDY("REDY"); // input → output, shm mapped, ready for TICKs
        constexpr FourCC KeyTICK("TICK"); // output → input, new frame slot
        constexpr FourCC KeyACKT("ACKT"); // input → output, ack of a TICK (sync only)
        constexpr FourCC KeyBYE("BYE ");  // either, graceful disconnect

        constexpr char     ShmMagic[4] = {'P', 'M', 'F', 'B'};
        constexpr uint64_t SlotAlign = 64; // cache-line align slots
        constexpr size_t   HandshakeTimeoutMs = 2000;

        // Reasonable sanity ceiling for handshake blobs.  Large enough for
        // generous MediaDesc + AudioDesc + Metadata, small enough that a
        // malicious peer can't exhaust memory.
        constexpr uint32_t MaxHandshakeValueBytes = 8u * 1024u * 1024u;

        // TICK payload layout (big-endian):
        //   + 0 : uint32_t slot
        //   + 4 : uint64_t seq
        //   +12 : uint64_t frameNumber
        //   +20 : uint32_t flags
        //   +24 : int64_t  timestampNs (steady_clock::time_since_epoch, ns)
        // Total: 32 bytes.
        constexpr uint32_t TickPayloadBytes = 32;

        // ACK payload: 8 bytes, the seq of the TICK being acknowledged.
        constexpr uint32_t AckPayloadBytes = 8;

        // How long the output waits for a sync client's ACK before giving up
        // and dropping the client.  Long enough to tolerate normal consumer
        // processing time; short enough that a truly-dead reader doesn't
        // stall the pipeline indefinitely.
        constexpr unsigned int SyncAckTimeoutMs = 5000;

        size_t roundUp(size_t n, size_t align) {
                return (n + (align - 1)) & ~(align - 1);
        }

} // namespace

// ============================================================================
// ShmLayout — plain POD describing the byte offsets of every field in
// the shared memory region.  Written once at openOutput time and
// re-derived by openInput from the header.
// ============================================================================

struct FrameBridge::Impl {

                // ====================================================================
                // On-disk header — fixed layout.  Not using std::atomic here because
                // the BridgeHeader is written once on create and read once on open;
                // only the per-slot seq counters are accessed concurrently.
                // ====================================================================
                struct BridgeHeader {
                                char     magic[4]; // "PMFB"
                                uint32_t wireMajor;
                                uint32_t wireMinor;
                                char     buildInfo[64]; // NUL-terminated
                                uint64_t configHash;
                                uint8_t  uuidBytes[16];
                                uint32_t ringDepth;
                                uint32_t metadataReserveBytes;
                                uint64_t audioCapacitySamples;
                                uint64_t slotStride;
                                uint64_t slotsOffset;      // offset to slot 0
                                uint64_t configBlobOffset; // DataStream blob (MediaDesc+AudioDesc)
                                uint64_t configBlobSize;
                                uint64_t reserved[16]; // padding / future extensions
                };

                // Per-slot layout:
                //   slot_base = slotsOffset + index * slotStride
                //   + 0    : uint64_t seq   (even = stable, odd = writing)
                //   + 8    : uint64_t frameNumber
                //   + 16   : int64_t  ptsNum
                //   + 24   : int64_t  ptsDen
                //   + 32   : uint32_t flags
                //   + 36   : uint32_t metadataSize  (actual serialized bytes)
                //   + 40   : uint64_t audioSampleCount
                //   + 48   : reserved padding to metadata region
                //   + metadataOffset : metadata[metadataReserveBytes]
                //   + imagesOffset   : packed image planes
                //   + audioOffset    : audio samples (capacity = audioCapacitySamples)
                struct SlotOffsets {
                                size_t seqOff = 0;
                                size_t frameNumberOff = 8;
                                size_t ptsNumOff = 16;
                                size_t ptsDenOff = 24;
                                size_t flagsOff = 32;
                                size_t metadataSizeOff = 36;
                                size_t audioSampleCtOff = 40;
                                size_t metadataOff = 64; // aligned
                                size_t imagesOff = 0;    // filled in at init
                                size_t audioOff = 0;
                };

                // ====================================================================
                // Runtime state
                // ====================================================================
                enum Role {
                        RoleNone,
                        RoleOutput,
                        RoleInput
                };

                Role        role = RoleNone;
                String      name;
                UUID        uuid;
                MediaDesc   mediaDesc;
                AudioDesc   audioDesc;
                int         ringDepth = 0;
                size_t      metadataReserveBytes = 0;
                uint64_t    audioCapacitySamples = 0;
                size_t      slotStride = 0;
                SlotOffsets slotOff;
                uint64_t    configHash = 0;

                std::vector<size_t> planeSizes; // per-plane size, cached from MediaDesc
                size_t              imageBytesTotal = 0;

                SharedMemory shm;
                String       shmName;
                String       socketPath;

                // Output-only
                LocalServer server;
                struct Client {
                                LocalSocket::UPtr sock;
                                bool              syncMode = true;
                };
                std::vector<Client> clients;
                uint64_t            nextFrameNumber = 0;
                uint64_t            slotIndex = 0;           // which slot to write next
                uint64_t            currentSeq = 0;          // seqlock: incremented by 2 per publish
                TimeStamp           lastPublishTs;           // captured at publish time
                bool                waitForConsumer = false; // output-side config flag

                // Thread-safe abort flag.  Set from any thread by
                // FrameBridge::abort() (or as a side effect of close()); checked
                // inside the writeFrame wait loops so a blocking publisher can
                // be woken up promptly for shutdown.
                std::atomic<bool> abortFlag{false};

                // Input-only
                LocalSocket client;
                bool        inputSyncMode = true; // set at openInput()
                bool        inputTicked = false;
                bool        haveFreshTick = false; // set by TICK drain, cleared on successful readSlot
                uint32_t    lastTickSlot = 0;
                uint64_t    lastTickSeq = 0;
                uint64_t    lastTickFrame = 0;
                TimeStamp   lastTickTs;
                uint64_t    pendingAckSeq = 0; // seq to ACK after next successful readSlot

                // Owning FrameBridge pointer for signal emission.
                FrameBridge *owner = nullptr;

                // -------------- Path helpers --------------
                static String makeShmName(const String &logical) { return String("/promeki-fb-") + logical; }
                static String makeSocketPath(const String &logical) {
                        return Dir::ipc().path().join(String("promeki-fb-") + logical + String(".sock")).toString();
                }

                // -------------- Geometry --------------
                Error computeGeometry() {
                        planeSizes.clear();
                        imageBytesTotal = 0;
                        if (!mediaDesc.isValid()) return Error::Invalid;
                        const ImageDesc::List &imgs = mediaDesc.imageList();
                        if (imgs.size() != 1) {
                                // MVP restriction: exactly one image per frame.
                                return Error::NotSupported;
                        }
                        const ImageDesc &id = imgs[0];
                        int              nPlanes = id.planeCount();
                        if (nPlanes <= 0) return Error::Invalid;
                        for (int p = 0; p < nPlanes; ++p) {
                                size_t sz = id.pixelFormat().planeSize(static_cast<size_t>(p), id);
                                planeSizes.push_back(sz);
                                imageBytesTotal += sz;
                        }
                        return Error::Ok;
                }

                size_t audioBytesTotal() const { return audioDesc.bufferSize(audioCapacitySamples); }

                void computeSlotLayout() {
                        slotOff.metadataOff = roundUp(48, SlotAlign);
                        slotOff.imagesOff = roundUp(slotOff.metadataOff + metadataReserveBytes, SlotAlign);
                        slotOff.audioOff = roundUp(slotOff.imagesOff + imageBytesTotal, SlotAlign);
                        size_t slotEnd = slotOff.audioOff + audioBytesTotal();
                        slotStride = roundUp(slotEnd, SlotAlign);
                }

                uint64_t computeConfigHash() const {
                        // Hash over the stable shape fields that must match
                        // between writer and reader.
                        Buffer         blob(1024);
                        BufferIODevice dev(&blob);
                        dev.open(IODevice::ReadWrite);
                        DataStream ws = DataStream::createWriter(&dev);
                        ws << mediaDesc << audioDesc << static_cast<uint32_t>(ringDepth)
                           << static_cast<uint32_t>(metadataReserveBytes)
                           << static_cast<uint64_t>(audioCapacitySamples);
                        size_t n = static_cast<size_t>(dev.pos());
                        return fnv1aData(blob.data(), n);
                }

                Error writeConfigBlob(uint8_t *at, size_t available, size_t &usedOut) {
                        Buffer         blob(available);
                        BufferIODevice dev(&blob);
                        dev.open(IODevice::ReadWrite);
                        DataStream ws = DataStream::createWriter(&dev);
                        ws << mediaDesc << audioDesc;
                        if (ws.status() != DataStream::Ok) return ws.toError();
                        size_t n = static_cast<size_t>(dev.pos());
                        if (n > available) return Error::BufferTooSmall;
                        std::memcpy(at, blob.data(), n);
                        usedOut = n;
                        return Error::Ok;
                }

                Error readConfigBlob(const uint8_t *at, size_t size) {
                        Buffer wrapper = Buffer::wrap(const_cast<uint8_t *>(at), size, 0);
                        wrapper.setSize(size);
                        BufferIODevice dev(&wrapper);
                        dev.open(IODevice::ReadOnly);
                        DataStream rs = DataStream::createReader(&dev);
                        rs >> mediaDesc >> audioDesc;
                        if (rs.status() != DataStream::Ok) return rs.toError();
                        return Error::Ok;
                }

                uint64_t worstCaseAudioSamples(double headroom) const {
                        // nominal samples = sampleRate / frameRate
                        double rate = static_cast<double>(audioDesc.sampleRate());
                        double fps = mediaDesc.frameRate().toDouble();
                        if (fps <= 0.0 || rate <= 0.0) return 0;
                        double nominal = std::ceil(rate / fps);
                        double withHeadroom = std::ceil(nominal * (1.0 + headroom));
                        return static_cast<uint64_t>(withHeadroom);
                }

                // -------------- Slot addressing --------------
                uint8_t *slotBase(uint64_t index) {
                        uint8_t *base = static_cast<uint8_t *>(shm.data());
                        if (base == nullptr) return nullptr;
                        const BridgeHeader *hdr = reinterpret_cast<const BridgeHeader *>(base);
                        return base + hdr->slotsOffset +
                               static_cast<size_t>(index) * static_cast<size_t>(hdr->slotStride);
                }
                const uint8_t *slotBase(uint64_t index) const {
                        const uint8_t *base = static_cast<const uint8_t *>(shm.data());
                        if (base == nullptr) return nullptr;
                        const BridgeHeader *hdr = reinterpret_cast<const BridgeHeader *>(base);
                        return base + hdr->slotsOffset +
                               static_cast<size_t>(index) * static_cast<size_t>(hdr->slotStride);
                }

                // -------------- Seqlock helpers --------------
                static uint64_t loadSeq(const uint8_t *slot) {
                        return std::atomic_ref<const uint64_t>(*reinterpret_cast<const uint64_t *>(slot))
                                .load(std::memory_order_acquire);
                }
                static void storeSeq(uint8_t *slot, uint64_t v) {
                        std::atomic_ref<uint64_t>(*reinterpret_cast<uint64_t *>(slot))
                                .store(v, std::memory_order_release);
                }

                // -------------- Handshake (output side) --------------
                Error handshakeWithClient(LocalSocket &c, bool &syncModeOut) {
                        c.setReceiveTimeout(HandshakeTimeoutMs);
                        c.setSendTimeout(HandshakeTimeoutMs);
                        syncModeOut = true;

                        KlvReader r(&c);
                        KlvWriter w(&c);

                        // Expect HELO
                        KlvFrame hello;
                        Error    err = r.readFrame(hello, MaxHandshakeValueBytes);
                        if (err.isError()) return err;
                        if (hello.key != KeyHELO) {
                                w.writeFrame(KeyRJCT); // minimal rejection
                                return Error::CorruptData;
                        }

                        // Parse HELO: wireMajor, wireMinor, buildInfo, wantHash, syncMode
                        uint32_t peerMajor = 0, peerMinor = 0;
                        String   peerBuild;
                        uint64_t wantHash = 0;
                        uint8_t  peerSync = 1;
                        {
                                Buffer         tmp = hello.value;
                                BufferIODevice dev(&tmp);
                                dev.open(IODevice::ReadOnly);
                                DataStream rs = DataStream::createReader(&dev);
                                rs >> peerMajor >> peerMinor >> peerBuild >> wantHash >> peerSync;
                                if (rs.status() != DataStream::Ok) {
                                        sendReject(w, 1, String("malformed HELO"));
                                        return Error::CorruptData;
                                }
                        }
                        syncModeOut = (peerSync != 0);

                        if (peerMajor != WireMajor) {
                                sendReject(w, 2,
                                           String::sprintf("wire major mismatch: peer=%u.%u, us=%u.%u", peerMajor,
                                                           peerMinor, WireMajor, WireMinor));
                                return Error::NotSupported;
                        }
                        if (peerMinor != WireMinor) {
                                promekiInfo("FrameBridge: wire minor mismatch accepted "
                                            "(peer=%u.%u, us=%u.%u)",
                                            peerMajor, peerMinor, WireMajor, WireMinor);
                        }
                        if (wantHash != 0 && wantHash != configHash) {
                                sendReject(w, 3, String("config hash mismatch"));
                                return Error::FormatMismatch;
                        }

                        // Send ACPT
                        Buffer acptBuf(512);
                        {
                                BufferIODevice dev(&acptBuf);
                                dev.open(IODevice::ReadWrite);
                                DataStream ws = DataStream::createWriter(&dev);
                                ws << static_cast<uint32_t>(WireMajor) << static_cast<uint32_t>(WireMinor)
                                   << buildInfoString() << shmName << static_cast<uint64_t>(shm.size()) << configHash
                                   << uuid << static_cast<uint32_t>(ringDepth) << static_cast<uint64_t>(slotStride)
                                   << mediaDesc << audioDesc;
                                if (ws.status() != DataStream::Ok) {
                                        return ws.toError();
                                }
                                size_t n = static_cast<size_t>(dev.pos());
                                err = w.writeFrame(KeyACPT, acptBuf.data(), static_cast<uint32_t>(n));
                                if (err.isError()) return err;
                        }

                        // Expect REDY
                        KlvFrame ready;
                        err = r.readFrame(ready, MaxHandshakeValueBytes);
                        if (err.isError()) return err;
                        if (ready.key != KeyREDY) return Error::CorruptData;

                        // Restore a more lenient send timeout for TICKs — short
                        // enough that a dead reader doesn't block the write path
                        // for long, but not so short that momentary contention
                        // drops readers.
                        c.setSendTimeout(100);
                        return Error::Ok;
                }

                static void sendReject(KlvWriter &w, uint32_t code, const String &msg) {
                        Buffer         buf(256);
                        BufferIODevice dev(&buf);
                        dev.open(IODevice::ReadWrite);
                        DataStream ws = DataStream::createWriter(&dev);
                        ws << code << msg;
                        size_t n = static_cast<size_t>(dev.pos());
                        w.writeFrame(KeyRJCT, buf.data(), static_cast<uint32_t>(n));
                }

                // -------------- Handshake (input side) --------------
                Error handshakeAsInput(const String &logicalName) {
                        KlvReader r(&client);
                        KlvWriter w(&client);

                        // Send HELO
                        Buffer buf(256);
                        {
                                BufferIODevice dev(&buf);
                                dev.open(IODevice::ReadWrite);
                                DataStream ws = DataStream::createWriter(&dev);
                                ws << static_cast<uint32_t>(WireMajor) << static_cast<uint32_t>(WireMinor)
                                   << buildInfoString() << static_cast<uint64_t>(0) // wantHash=0 → accept any
                                   << static_cast<uint8_t>(inputSyncMode ? 1 : 0);
                                size_t n = static_cast<size_t>(dev.pos());
                                Error  err = w.writeFrame(KeyHELO, buf.data(), static_cast<uint32_t>(n));
                                if (err.isError()) return err;
                        }

                        // Receive ACPT or RJCT
                        KlvFrame resp;
                        Error    err = r.readFrame(resp, MaxHandshakeValueBytes);
                        if (err.isError()) return err;
                        if (resp.key == KeyRJCT) {
                                uint32_t       code = 0;
                                String         reason;
                                Buffer         tmp = resp.value;
                                BufferIODevice dev(&tmp);
                                dev.open(IODevice::ReadOnly);
                                DataStream rs = DataStream::createReader(&dev);
                                rs >> code >> reason;
                                promekiWarn("FrameBridge: rejected: code=%u, %s", code, reason.cstr());
                                return Error::NotSupported;
                        }
                        if (resp.key != KeyACPT) return Error::CorruptData;

                        uint32_t peerMajor = 0, peerMinor = 0;
                        String   peerBuild, shmNameIn;
                        uint64_t shmSize = 0, stride = 0;
                        uint64_t hash = 0;
                        uint32_t depth = 0;
                        UUID     peerUuid;
                        {
                                Buffer         tmp = resp.value;
                                BufferIODevice dev(&tmp);
                                dev.open(IODevice::ReadOnly);
                                DataStream rs = DataStream::createReader(&dev);
                                rs >> peerMajor >> peerMinor >> peerBuild >> shmNameIn >> shmSize >> hash >> peerUuid >>
                                        depth >> stride >> mediaDesc >> audioDesc;
                                if (rs.status() != DataStream::Ok) return Error::CorruptData;
                        }

                        uuid = peerUuid;
                        name = logicalName;
                        shmName = shmNameIn;
                        ringDepth = static_cast<int>(depth);
                        configHash = hash;
                        slotStride = stride;

                        // Map shm read-only.
                        err = shm.open(shmName, SharedMemory::ReadOnly);
                        if (err.isError()) return err;
                        if (shm.size() < sizeof(BridgeHeader)) return Error::CorruptData;
                        const BridgeHeader *hdr = reinterpret_cast<const BridgeHeader *>(shm.data());
                        if (std::memcmp(hdr->magic, ShmMagic, 4) != 0) {
                                return Error::CorruptData;
                        }
                        if (hdr->wireMajor != WireMajor) return Error::NotSupported;
                        if (hdr->configHash != configHash) return Error::FormatMismatch;
                        metadataReserveBytes = hdr->metadataReserveBytes;
                        audioCapacitySamples = hdr->audioCapacitySamples;
                        computeGeometry();   // uses mediaDesc from handshake
                        computeSlotLayout(); // rebuilds slotOff for reads

                        // Send REDY
                        err = w.writeFrame(KeyREDY);
                        if (err.isError()) return err;
                        return Error::Ok;
                }

                // -------------- Slot write --------------
                Error writeSlot(uint64_t index, const Frame &frame) {
                        uint8_t *base = slotBase(index);
                        if (base == nullptr) return Error::NotOpen;
                        // Seqlock: advance to odd (writing).
                        uint64_t seq = std::atomic_ref<uint64_t>(*reinterpret_cast<uint64_t *>(base + slotOff.seqOff))
                                               .load(std::memory_order_relaxed);
                        seq += 1; // odd
                        storeSeq(base + slotOff.seqOff, seq);

                        // Write per-slot header fields.
                        auto put64 = [&](size_t off, uint64_t v) {
                                std::memcpy(base + off, &v, sizeof(v));
                        };
                        auto put32 = [&](size_t off, uint32_t v) {
                                std::memcpy(base + off, &v, sizeof(v));
                        };
                        auto put64s = [&](size_t off, int64_t v) {
                                std::memcpy(base + off, &v, sizeof(v));
                        };

                        put64(slotOff.frameNumberOff, nextFrameNumber);
                        // ptsNum/ptsDen: reserved for a future carry of the
                        // payload's native pts; currently always 0/1 pending
                        // a transport-level pts encoding decision.
                        int64_t ptsNum = 0, ptsDen = 1;
                        put64s(slotOff.ptsNumOff, ptsNum);
                        put64s(slotOff.ptsDenOff, ptsDen);
                        put32(slotOff.flagsOff, 0u);

                        // Metadata blob.
                        size_t metaBytes = 0;
                        {
                                Buffer         tmp(metadataReserveBytes);
                                BufferIODevice dev(&tmp);
                                dev.open(IODevice::ReadWrite);
                                DataStream ws = DataStream::createWriter(&dev);
                                ws << frame.metadata();
                                if (ws.status() != DataStream::Ok) {
                                        // Restore even seq before returning so reader
                                        // sees a consistent (stale) slot.
                                        storeSeq(base + slotOff.seqOff, seq + 1);
                                        return Error::OutOfRange;
                                }
                                metaBytes = static_cast<size_t>(dev.pos());
                                if (metaBytes > metadataReserveBytes) {
                                        storeSeq(base + slotOff.seqOff, seq + 1);
                                        return Error::OutOfRange;
                                }
                                std::memcpy(base + slotOff.metadataOff, tmp.data(), metaBytes);
                        }
                        put32(slotOff.metadataSizeOff, static_cast<uint32_t>(metaBytes));

                        // Images (single image for MVP).
                        auto vids = frame.videoPayloads();
                        if (!vids.isEmpty() && vids[0].isValid()) {
                                const auto *uvp = vids[0]->as<UncompressedVideoPayload>();
                                if (uvp != nullptr) {
                                        size_t       off = slotOff.imagesOff;
                                        const size_t n = uvp->planeCount();
                                        for (size_t p = 0; p < n && p < planeSizes.size(); ++p) {
                                                auto plane = uvp->plane(p);
                                                if (plane.isValid()) {
                                                        size_t copyBytes = planeSizes[p];
                                                        if (plane.size() < copyBytes) {
                                                                copyBytes = plane.size();
                                                        }
                                                        std::memcpy(base + off, plane.data(), copyBytes);
                                                }
                                                off += planeSizes[p];
                                        }
                                }
                        }

                        // Audio (single track for MVP).
                        uint64_t audioSamples = 0;
                        auto     auds = frame.audioPayloads();
                        if (!auds.isEmpty() && auds[0].isValid()) {
                                const auto *uap = auds[0]->as<PcmAudioPayload>();
                                if (uap != nullptr && uap->planeCount() > 0) {
                                        const size_t samples = uap->sampleCount();
                                        if (samples > audioCapacitySamples) {
                                                storeSeq(base + slotOff.seqOff, seq + 1);
                                                return Error::OutOfRange;
                                        }
                                        audioSamples = samples;
                                        auto         pv = uap->plane(0);
                                        const size_t bytes = audioDesc.bufferSize(samples);
                                        const size_t copyBytes = pv.size() < bytes ? pv.size() : bytes;
                                        std::memcpy(base + slotOff.audioOff, pv.data(), copyBytes);
                                }
                        }
                        put64(slotOff.audioSampleCtOff, audioSamples);

                        // Advance seq to even (stable).
                        seq += 1;
                        storeSeq(base + slotOff.seqOff, seq);
                        currentSeq = seq;
                        lastPublishTs = TimeStamp::now();
                        return Error::Ok;
                }

                // -------------- Slot read --------------
                Frame::Ptr readSlot(uint64_t index, Error *errOut) {
                        const uint8_t *base = slotBase(index);
                        if (base == nullptr) {
                                if (errOut) *errOut = Error::NotOpen;
                                return Frame::Ptr();
                        }

                        // Seqlock: read seq1, copy, read seq2; retry on torn read
                        // but cap retries to avoid infinite loop when writer laps us.
                        for (int attempt = 0; attempt < 4; ++attempt) {
                                uint64_t seq1 = loadSeq(base + slotOff.seqOff);
                                if (seq1 & 1ULL) {
                                        // Writer mid-publish — brief spin.
                                        continue;
                                }
                                uint64_t frameNumber = 0, audioSamples = 0;
                                int64_t  ptsNum = 0, ptsDen = 1;
                                uint32_t flags = 0, metaSize = 0;
                                std::memcpy(&frameNumber, base + slotOff.frameNumberOff, 8);
                                std::memcpy(&ptsNum, base + slotOff.ptsNumOff, 8);
                                std::memcpy(&ptsDen, base + slotOff.ptsDenOff, 8);
                                std::memcpy(&flags, base + slotOff.flagsOff, 4);
                                std::memcpy(&metaSize, base + slotOff.metadataSizeOff, 4);
                                std::memcpy(&audioSamples, base + slotOff.audioSampleCtOff, 8);
                                if (metaSize > metadataReserveBytes || audioSamples > audioCapacitySamples) {
                                        continue;
                                }

                                // Build the Frame.  We copy everything into fresh
                                // buffers so the reader doesn't hold stale slot
                                // memory past the seqlock window.
                                Frame::Ptr frame = Frame::Ptr::create();

                                // Metadata
                                Metadata meta;
                                if (metaSize > 0) {
                                        Buffer tmp = Buffer::wrap(static_cast<uint8_t *>(const_cast<uint8_t *>(
                                                                          base + slotOff.metadataOff)),
                                                                  metaSize, 0);
                                        tmp.setSize(metaSize);
                                        BufferIODevice dev(&tmp);
                                        dev.open(IODevice::ReadOnly);
                                        DataStream rs = DataStream::createReader(&dev);
                                        rs >> meta;
                                }

                                // Video payload (single video, multi-plane)
                                UncompressedVideoPayload::Ptr videoPayload;
                                if (!mediaDesc.imageList().isEmpty()) {
                                        const ImageDesc &id = mediaDesc.imageList()[0];
                                        BufferView       planes;
                                        size_t           off = slotOff.imagesOff;
                                        int              n = id.planeCount();
                                        for (int p = 0; p < n && p < static_cast<int>(planeSizes.size()); ++p) {
                                                size_t sz = planeSizes[p];
                                                auto   buf = Buffer::Ptr::create(sz);
                                                buf.modify()->setSize(sz);
                                                if (sz > 0) {
                                                        std::memcpy(buf.modify()->data(), base + off, sz);
                                                }
                                                planes.pushToBack(buf, 0, sz);
                                                off += sz;
                                        }
                                        videoPayload = UncompressedVideoPayload::Ptr::create(id, planes);
                                }

                                // Audio payload (single track)
                                PcmAudioPayload::Ptr audioPayload;
                                if (audioSamples > 0) {
                                        size_t bytes = audioDesc.bufferSize(static_cast<size_t>(audioSamples));
                                        auto   buf = Buffer::Ptr::create(bytes);
                                        buf.modify()->setSize(bytes);
                                        std::memcpy(buf.modify()->data(), base + slotOff.audioOff, bytes);
                                        BufferView planes;
                                        planes.pushToBack(buf, 0, bytes);
                                        audioPayload = PcmAudioPayload::Ptr::create(
                                                audioDesc, static_cast<size_t>(audioSamples), planes);
                                }

                                // Check seq2 after we've copied everything.
                                std::atomic_thread_fence(std::memory_order_acquire);
                                uint64_t seq2 = loadSeq(base + slotOff.seqOff);
                                if (seq1 != seq2) continue; // torn — retry

                                Frame *mut = frame.modify();
                                if (videoPayload) mut->addPayload(videoPayload);
                                if (audioPayload) mut->addPayload(audioPayload);
                                mut->metadata() = meta;
                                mut->metadata().set(Metadata::FrameNumber, FrameNumber(int64_t(frameNumber)));
                                if (errOut) *errOut = Error::Ok;
                                (void)ptsNum;
                                (void)ptsDen;
                                (void)flags;
                                return frame;
                        }

                        if (errOut) *errOut = Error::TryAgain;
                        return Frame::Ptr();
                }

                // -------------- Byte-packing helpers --------------
                static void be32(uint8_t *p, uint32_t v) {
                        p[0] = uint8_t((v >> 24) & 0xFF);
                        p[1] = uint8_t((v >> 16) & 0xFF);
                        p[2] = uint8_t((v >> 8) & 0xFF);
                        p[3] = uint8_t(v & 0xFF);
                }
                static void be64(uint8_t *p, uint64_t v) {
                        p[0] = uint8_t((v >> 56) & 0xFF);
                        p[1] = uint8_t((v >> 48) & 0xFF);
                        p[2] = uint8_t((v >> 40) & 0xFF);
                        p[3] = uint8_t((v >> 32) & 0xFF);
                        p[4] = uint8_t((v >> 24) & 0xFF);
                        p[5] = uint8_t((v >> 16) & 0xFF);
                        p[6] = uint8_t((v >> 8) & 0xFF);
                        p[7] = uint8_t(v & 0xFF);
                }
                static uint32_t rd32(const uint8_t *p) {
                        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
                }
                static uint64_t rd64(const uint8_t *p) {
                        return (uint64_t(p[0]) << 56) | (uint64_t(p[1]) << 48) | (uint64_t(p[2]) << 40) |
                               (uint64_t(p[3]) << 32) | (uint64_t(p[4]) << 24) | (uint64_t(p[5]) << 16) |
                               (uint64_t(p[6]) << 8) | uint64_t(p[7]);
                }

                // -------------- TICK emission --------------
                void emitTick(LocalSocket &c, uint32_t slot, uint64_t seq, uint64_t frameNumber, int64_t timestampNs) {
                        // Fixed 32-byte big-endian payload.
                        uint8_t buf[TickPayloadBytes];
                        be32(&buf[0], slot);
                        be64(&buf[4], seq);
                        be64(&buf[12], frameNumber);
                        be32(&buf[20], 0); // flags reserved
                        be64(&buf[24], static_cast<uint64_t>(timestampNs));
                        KlvWriter w(&c);
                        (void)w.writeFrame(KeyTICK, buf, TickPayloadBytes);
                }

                // -------------- ACK emission (input → output) --------------
                void emitAck(uint64_t seq) {
                        if (!inputSyncMode) return;
                        if (!client.isConnected()) return;
                        uint8_t buf[AckPayloadBytes];
                        be64(&buf[0], seq);
                        KlvWriter w(&client);
                        (void)w.writeFrame(KeyACKT, buf, AckPayloadBytes);
                }

                // -------------- Wait for ACKs (output-side, sync clients) --------------
                //
                // Called after TICK emission.  Blocks until every still-connected
                // sync client either acknowledges @p seq or is determined to be
                // dead (dropped).  No-op when no sync clients are attached.
                void waitForAcks(uint64_t seq) {
                        for (auto it = clients.begin(); it != clients.end();) {
                                if (abortFlag.load(std::memory_order_acquire)) return;
                                Client &cl = *it;
                                if (!cl.sock || !cl.sock->isConnected()) {
                                        if (owner) owner->peerDisconnectedSignal.emit();
                                        it = clients.erase(it);
                                        continue;
                                }
                                if (!cl.syncMode) {
                                        ++it;
                                        continue;
                                }

                                // Read frames from this client until we see an
                                // ACK with matching seq or we've spent our total
                                // budget.  Use a short per-attempt timeout so
                                // abort() wakes us up within ~pollMs instead of
                                // stalling for the full SyncAckTimeoutMs when a
                                // reader is genuinely dead.  Read errors on
                                // LocalSocket can't distinguish timeout from a
                                // real failure — we treat everything as
                                // "keep polling up to the budget" and the
                                // isConnected() check at the top of the outer
                                // loop is the real disconnect tripwire.
                                const unsigned int pollMs = 100;
                                cl.sock->setReceiveTimeout(pollMs);
                                KlvReader r(cl.sock.ptr());
                                bool      acked = false;
                                bool      dead = false;
                                const int maxAttempts = std::max(1, static_cast<int>(SyncAckTimeoutMs / pollMs));
                                for (int tries = 0; tries < maxAttempts && !acked && !dead; ++tries) {
                                        if (abortFlag.load(std::memory_order_acquire)) {
                                                return;
                                        }
                                        KlvFrame f;
                                        Error    err = r.readFrame(f, MaxHandshakeValueBytes);
                                        if (err.isError()) {
                                                // Either a timeout (no ack yet)
                                                // or a real IO error — loop and
                                                // let the total-budget cap decide.
                                                continue;
                                        }
                                        if (f.key == KeyBYE) {
                                                dead = true;
                                                break;
                                        }
                                        if (f.key != KeyACKT) {
                                                // Unknown frame from this side; ignore.
                                                continue;
                                        }
                                        if (f.value.size() < AckPayloadBytes) continue;
                                        uint64_t ackSeq = rd64(static_cast<const uint8_t *>(f.value.data()));
                                        // The publisher may still be catching up on
                                        // an old sync client's ACK after a burst.
                                        // Accept any ack >= seq as "we're caught up".
                                        if (ackSeq >= seq) acked = true;
                                }
                                if (!acked || dead) {
                                        if (owner) owner->peerDisconnectedSignal.emit();
                                        it = clients.erase(it);
                                } else {
                                        ++it;
                                }
                        }
                }

                // -------------- Connection servicing --------------
                void acceptPending() {
                        while (server.hasPendingConnections()) {
                                LocalSocket *ps = server.nextPendingConnection();
                                if (ps == nullptr) break;
                                LocalSocket::UPtr s = LocalSocket::UPtr::takeOwnership(ps);
                                bool              sync = true;
                                Error             err = handshakeWithClient(*s, sync);
                                if (err.isError()) {
                                        promekiInfo("FrameBridge: client handshake "
                                                    "failed: %s",
                                                    err.name().cstr());
                                        continue;
                                }
                                Client cl;
                                cl.sock = std::move(s);
                                cl.syncMode = sync;
                                clients.push_back(std::move(cl));
                        }
                }

                void pruneDeadClients() {
                        for (auto it = clients.begin(); it != clients.end();) {
                                if (it->sock && it->sock->isConnected()) {
                                        ++it;
                                } else {
                                        if (owner) owner->peerDisconnectedSignal.emit();
                                        it = clients.erase(it);
                                }
                        }
                }
};

// ============================================================================
// FrameBridge public
// ============================================================================

FrameBridge::FrameBridge(ObjectBase *parent) : ObjectBase(parent), _d(ImplPtr::create()) {
        _d->owner = this;
}

FrameBridge::~FrameBridge() {
        close();
}

Error FrameBridge::openOutput(const String &name, const Config &config) {
        if (isOpen()) return Error::AlreadyOpen;
        if (name.isEmpty()) return Error::Invalid;
        _d->role = Impl::RoleOutput;
        _d->name = name;
        _d->mediaDesc = config.mediaDesc;
        _d->audioDesc = config.audioDesc;
        _d->ringDepth = config.ringDepth > 0 ? config.ringDepth : DefaultRingDepth;
        _d->metadataReserveBytes = config.metadataReserveBytes;
        _d->uuid = UUID::generateV4();
        _d->waitForConsumer = config.waitForConsumer;
        _d->abortFlag.store(false, std::memory_order_release);
        _d->audioCapacitySamples = _d->worstCaseAudioSamples(config.audioHeadroomFraction);

        Error err = _d->computeGeometry();
        if (err.isError()) {
                _d->role = Impl::RoleNone;
                return err;
        }
        _d->computeSlotLayout();
        _d->configHash = _d->computeConfigHash();

        _d->shmName = Impl::makeShmName(name);
        _d->socketPath = Impl::makeSocketPath(name);

        // Liveness probe: if another FrameBridge output is already
        // publishing under this name, refuse.  Otherwise (socket
        // missing, socket present but unlistened — a crashed prior
        // owner), treat both the socket and the shm region as stale
        // and clean up before re-creating.  LocalServer::listen already
        // unlinks a stale socket file, so we only need to handle the
        // shm side explicitly.
        {
                LocalSocket probe;
                Error       perr = probe.connectTo(_d->socketPath);
                probe.close();
                if (perr.isOk()) {
                        _d->role = Impl::RoleNone;
                        return Error::Exists;
                }
                // Not reachable — any prior region under our shm name
                // is orphaned.  Best-effort unlink; ignore errors so a
                // genuinely-missing name doesn't trip this path.
                SharedMemory::unlink(_d->shmName);
        }

        // Size the shm: header + config-blob (bounded) + ring.
        const size_t configBlobReserve = 64u * 1024u;
        size_t       totalBytes =
                sizeof(Impl::BridgeHeader) + configBlobReserve + static_cast<size_t>(_d->ringDepth) * _d->slotStride;
        totalBytes = roundUp(totalBytes, SlotAlign);

        err = _d->shm.create(_d->shmName, totalBytes, config.accessMode, config.groupName);
        if (err.isError()) {
                _d->role = Impl::RoleNone;
                return err;
        }

        // Zero the region (POSIX ftruncate already zeros, but be explicit).
        std::memset(_d->shm.data(), 0, _d->shm.size());

        // Fill header.
        auto *hdr = static_cast<Impl::BridgeHeader *>(_d->shm.data());
        std::memcpy(hdr->magic, ShmMagic, 4);
        hdr->wireMajor = WireMajor;
        hdr->wireMinor = WireMinor;
        const String buildStr = buildInfoString();
        std::strncpy(hdr->buildInfo, buildStr.cstr(), sizeof(hdr->buildInfo) - 1);
        hdr->buildInfo[sizeof(hdr->buildInfo) - 1] = '\0';
        hdr->configHash = _d->configHash;
        const auto &uuidData = _d->uuid.data();
        std::memcpy(hdr->uuidBytes, uuidData.data(), 16);
        hdr->ringDepth = static_cast<uint32_t>(_d->ringDepth);
        hdr->metadataReserveBytes = static_cast<uint32_t>(_d->metadataReserveBytes);
        hdr->audioCapacitySamples = _d->audioCapacitySamples;
        hdr->slotStride = _d->slotStride;
        hdr->configBlobOffset = roundUp(sizeof(Impl::BridgeHeader), SlotAlign);
        size_t used = 0;
        err = _d->writeConfigBlob(static_cast<uint8_t *>(_d->shm.data()) + hdr->configBlobOffset, configBlobReserve,
                                  used);
        if (err.isError()) {
                close();
                return err;
        }
        hdr->configBlobSize = used;
        hdr->slotsOffset = roundUp(hdr->configBlobOffset + used, SlotAlign);

        // Listen on the control socket.
        // Make sure Dir::ipc() exists so the socket file can be created.
        Dir::ipc().mkpath();
        err = _d->server.listen(_d->socketPath, config.accessMode, config.groupName);
        if (err.isError()) {
                close();
                return err;
        }
        return Error::Ok;
}

Error FrameBridge::openInput(const String &name, bool sync) {
        if (isOpen()) return Error::AlreadyOpen;
        if (name.isEmpty()) return Error::Invalid;
        _d->role = Impl::RoleInput;
        _d->name = name;
        _d->inputSyncMode = sync;
        _d->abortFlag.store(false, std::memory_order_release);
        _d->socketPath = Impl::makeSocketPath(name);

        Error err = _d->client.connectTo(_d->socketPath);
        if (err.isError()) {
                _d->role = Impl::RoleNone;
                return err;
        }
        _d->client.setReceiveTimeout(HandshakeTimeoutMs);
        _d->client.setSendTimeout(HandshakeTimeoutMs);

        err = _d->handshakeAsInput(name);
        if (err.isError()) {
                close();
                return err;
        }

        // After handshake, apply a short read timeout so waitForTick
        // callers can poll periodically.
        _d->client.setReceiveTimeout(0); // blocking reads until next TICK
        return Error::Ok;
}

void FrameBridge::close() {
        // Tripping the abort flag first makes any writeFrame that is
        // currently blocked on another thread (waiting for a consumer
        // or for a sync ACK) unwind with Error::Cancelled, which in
        // turn lets the caller reach this close() — most commonly via
        // a MediaIO worker that would otherwise be gated behind
        // the blocked write.
        _d->abortFlag.store(true, std::memory_order_release);
        if (_d->role == Impl::RoleOutput) {
                // Say goodbye to each client (best-effort).
                for (auto &c : _d->clients) {
                        if (c.sock) {
                                KlvWriter w(c.sock.ptr());
                                (void)w.writeFrame(KeyBYE);
                                c.sock->close();
                        }
                }
                _d->clients.clear();
                _d->server.close();
        } else if (_d->role == Impl::RoleInput) {
                if (_d->client.isConnected()) {
                        KlvWriter w(&_d->client);
                        (void)w.writeFrame(KeyBYE);
                }
                _d->client.close();
        }
        _d->shm.close();
        _d->role = Impl::RoleNone;
        _d->mediaDesc = MediaDesc();
        _d->audioDesc = AudioDesc();
        _d->ringDepth = 0;
        _d->slotStride = 0;
        _d->slotIndex = 0;
        _d->nextFrameNumber = 0;
        _d->currentSeq = 0;
        _d->lastPublishTs = TimeStamp();
        _d->waitForConsumer = false;
        // Leave abortFlag latched here; openOutput / openInput clear
        // it when the bridge is reopened.  This keeps writeFrame
        // returning Cancelled rather than resuming silently if some
        // caller races with a close.
        _d->inputSyncMode = true;
        _d->inputTicked = false;
        _d->haveFreshTick = false;
        _d->lastTickSlot = 0;
        _d->lastTickSeq = 0;
        _d->lastTickFrame = 0;
        _d->lastTickTs = TimeStamp();
        _d->pendingAckSeq = 0;
        _d->planeSizes.clear();
        _d->imageBytesTotal = 0;
        _d->uuid = UUID();
        _d->name = String();
        _d->shmName = String();
        _d->socketPath = String();
        _d->configHash = 0;
}

bool FrameBridge::isOpen() const {
        return _d->role != Impl::RoleNone;
}

bool FrameBridge::isOutput() const {
        return _d->role == Impl::RoleOutput;
}

const UUID &FrameBridge::uuid() const {
        return _d->uuid;
}
const String &FrameBridge::name() const {
        return _d->name;
}
const MediaDesc &FrameBridge::mediaDesc() const {
        return _d->mediaDesc;
}
const AudioDesc &FrameBridge::audioDesc() const {
        return _d->audioDesc;
}
int FrameBridge::ringDepth() const {
        return _d->ringDepth;
}

void FrameBridge::abort() {
        _d->abortFlag.store(true, std::memory_order_release);
}

bool FrameBridge::isSyncInput() const {
        if (_d->role == Impl::RoleInput) return _d->inputSyncMode;
        return true;
}

TimeStamp FrameBridge::lastFrameTimeStamp() const {
        if (_d->role == Impl::RoleOutput) return _d->lastPublishTs;
        if (_d->role == Impl::RoleInput) return _d->lastTickTs;
        return TimeStamp();
}

void FrameBridge::service() {
        if (_d->role != Impl::RoleOutput) return;
        _d->acceptPending();
        _d->pruneDeadClients();
}

size_t FrameBridge::connectionCount() const {
        if (_d->role != Impl::RoleOutput) return 0;
        size_t n = 0;
        for (const auto &c : _d->clients) {
                if (c.sock && c.sock->isConnected()) ++n;
        }
        return n;
}

Error FrameBridge::writeFrame(const Frame::Ptr &frame) {
        if (_d->role != Impl::RoleOutput) return Error::NotOpen;
        if (!frame) return Error::Invalid;
        if (_d->abortFlag.load(std::memory_order_acquire)) {
                return Error::Cancelled;
        }

        // Service the control plane before the data plane.
        _d->acceptPending();
        _d->pruneDeadClients();
        if (_d->clients.empty()) {
                if (_d->waitForConsumer) {
                        // Block until a consumer attaches.  Poll at a
                        // modest interval so the accept path + dead-peer
                        // pruning stay responsive; abort the wait if
                        // close() flips us out of output role or the
                        // caller invoked FrameBridge::abort() from
                        // another thread.
                        while (_d->role == Impl::RoleOutput && !_d->abortFlag.load(std::memory_order_acquire) &&
                               _d->clients.empty()) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                                _d->acceptPending();
                                _d->pruneDeadClients();
                        }
                        if (_d->abortFlag.load(std::memory_order_acquire)) {
                                return Error::Cancelled;
                        }
                        if (_d->role != Impl::RoleOutput) {
                                return Error::NotOpen;
                        }
                } else {
                        // No readers — nothing to publish.  Still bump
                        // the frame counter so the first reader that
                        // joins doesn't see stale state.
                        ++_d->nextFrameNumber;
                        return Error::Ok;
                }
        }

        uint64_t idx = _d->slotIndex;
        // Stamp the publish time just before the slot goes live.  Read
        // back from _d->lastPublishTs (set inside writeSlot) for the
        // on-wire value to keep the public accessor and the TICK in
        // lockstep.
        Error err = _d->writeSlot(idx, *frame);
        if (err.isError()) return err;
        int64_t tsNs = _d->lastPublishTs.nanoseconds();

        // Broadcast TICK to each client.
        for (auto it = _d->clients.begin(); it != _d->clients.end();) {
                if (it->sock && it->sock->isConnected()) {
                        _d->emitTick(*it->sock, static_cast<uint32_t>(idx), _d->currentSeq, _d->nextFrameNumber, tsNs);
                        ++it;
                } else {
                        if (_d->owner) _d->owner->peerDisconnectedSignal.emit();
                        it = _d->clients.erase(it);
                }
        }

        // Block on any sync clients' ACKs before returning.
        _d->waitForAcks(_d->currentSeq);

        _d->slotIndex = (idx + 1) % static_cast<uint64_t>(_d->ringDepth);
        ++_d->nextFrameNumber;
        return Error::Ok;
}

Frame::Ptr FrameBridge::readFrame(Error *err) {
        if (_d->role != Impl::RoleInput) {
                if (err) *err = Error::NotOpen;
                return Frame::Ptr();
        }

        // Drain pending TICKs, keeping the newest.
        while (_d->client.bytesAvailable() >= static_cast<int64_t>(TickPayloadBytes + 8)) {
                KlvReader r(&_d->client);
                KlvFrame  f;
                Error     e = r.readFrame(f, MaxHandshakeValueBytes);
                if (e.isError()) {
                        if (err) *err = e;
                        return Frame::Ptr();
                }
                if (f.key == KeyBYE) {
                        if (_d->owner) _d->owner->peerDisconnectedSignal.emit();
                        if (err) *err = Error::EndOfFile;
                        return Frame::Ptr();
                }
                if (f.key != KeyTICK) continue;
                if (f.value.size() < TickPayloadBytes) continue;
                const uint8_t *p = static_cast<const uint8_t *>(f.value.data());
                uint32_t       slot = Impl::rd32(p + 0);
                uint64_t       seq = Impl::rd64(p + 4);
                uint64_t       fn = Impl::rd64(p + 12);
                int64_t        tsNs = static_cast<int64_t>(Impl::rd64(p + 24));
                if (_d->inputTicked && seq > _d->lastTickSeq + 2) {
                        uint64_t missed = (seq - _d->lastTickSeq) / 2 - 1;
                        if (_d->owner) _d->owner->framesMissedSignal.emit(missed);
                }
                _d->lastTickSlot = slot;
                _d->lastTickSeq = seq;
                _d->lastTickFrame = fn;
                _d->lastTickTs = TimeStamp(TimeStamp::Value(std::chrono::nanoseconds(tsNs)));
                _d->inputTicked = true;
                _d->haveFreshTick = true;
                _d->pendingAckSeq = seq;
                if (_d->owner) _d->owner->frameAvailableSignal.emit();
        }

        // Return a frame only when there's a fresh TICK to read.  Without
        // this guard the MediaIO prefetch loop would keep pulling the
        // same stale slot and fill its queue with duplicates — when a
        // real TICK finally arrived the consumer would see it as a burst.
        if (!_d->haveFreshTick) {
                if (err) *err = Error::Ok;
                return Frame::Ptr();
        }
        Frame::Ptr f = _d->readSlot(_d->lastTickSlot, err);
        if (f) {
                _d->haveFreshTick = false;
                // Ack the TICK we just consumed so the publisher can
                // proceed (sync mode only; no-op otherwise).
                _d->emitAck(_d->pendingAckSeq);
        }
        return f;
}

PROMEKI_NAMESPACE_END
