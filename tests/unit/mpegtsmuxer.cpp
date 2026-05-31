/**
 * @file      tests/mpegtsmuxer.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Tests for @ref MpegTsMuxer and its counterpart @ref MpegTsDemuxer
 * via the round-trip path.  These tests exercise the muxer's PSI
 * scheduling, PES packetization, PCR insertion, and continuity
 * counter handling without dragging in encoder backends — synthetic
 * payload bytes are sufficient.
 */

#include <doctest/doctest.h>
#include <promeki/mpegts.h>
#include <promeki/mpegtsmuxer.h>
#include <promeki/mpegtsdemuxer.h>
#include <cstring>
#include <vector>

using namespace promeki;

namespace {

        // Builds a Buffer with the given bytes.
        Buffer makeBuffer(const std::vector<uint8_t> &v) {
                Buffer b(v.size() ? v.size() : 1);
                b.setSize(v.size());
                if (!v.empty()) std::memcpy(b.data(), v.data(), v.size());
                return b;
        }

        BufferView viewOf(const Buffer &b) {
                return BufferView(b, 0, b.size());
        }

        // A handful of dummy NAL-shaped payload bytes that the muxer
        // will emit verbatim inside its PES envelope.
        std::vector<uint8_t> makeFakeAccessUnit(size_t size, uint8_t firstByte) {
                std::vector<uint8_t> v(size);
                for (size_t i = 0; i < size; ++i) v[i] = static_cast<uint8_t>((firstByte + i) & 0xFF);
                return v;
        }

} // namespace

TEST_CASE("MpegTsMuxer: addStream rejects reserved / duplicate / out-of-range PIDs") {
        MpegTsMuxer mux;
        CHECK(mux.addStream(MpegTs::PidPat, MpegTs::StreamTypeH264) == Error::InvalidArgument);
        CHECK(mux.addStream(MpegTs::PidNull, MpegTs::StreamTypeH264) == Error::InvalidArgument);
        CHECK(mux.addStream(0x2000, MpegTs::StreamTypeH264) == Error::InvalidArgument); // > MaxPid
        CHECK(mux.addStream(MpegTs::DefaultPmtPid, MpegTs::StreamTypeH264) == Error::InvalidArgument); // PMT PID
        REQUIRE(mux.addStream(0x100, MpegTs::StreamTypeH264) == Error::Ok);
        CHECK(mux.addStream(0x100, MpegTs::StreamTypeH264) == Error::Exists);
        CHECK(mux.hasStream(0x100));
        CHECK_FALSE(mux.hasStream(0x101));
}

TEST_CASE("MpegTsMuxer: every emitted buffer is N * 188 bytes") {
        MpegTsMuxer mux;
        mux.setPcrPid(0x100);
        REQUIRE(mux.addStream(0x100, MpegTs::StreamTypeH264) == Error::Ok);
        REQUIRE(mux.addStream(0x101, MpegTs::StreamTypeAacAdts) == Error::Ok);

        std::vector<size_t> sizes;
        auto                emit = [&sizes](const BufferView &v) -> Error {
                sizes.push_back(v.size());
                return Error::Ok;
        };

        const auto au = makeFakeAccessUnit(1234, 0x10);
        Buffer     buf = makeBuffer(au);
        REQUIRE(mux.writeAccessUnit(0x100, viewOf(buf), 90000, 90000, true, emit) == Error::Ok);

        // Two PSI packets (PAT + PMT) and one access unit blob.  Each
        // emit() call hands a multiple of 188 bytes.
        REQUIRE(sizes.size() >= 3);
        for (size_t s : sizes) CHECK(s % MpegTs::PacketSize == 0);
}

TEST_CASE("MpegTsMuxer: PAT / PMT are not re-emitted within the interval") {
        MpegTsMuxer mux;
        mux.setPcrPid(0x100);
        mux.setPatPmtIntervalMs(100);
        REQUIRE(mux.addStream(0x100, MpegTs::StreamTypeH264) == Error::Ok);
        int psiCallCount = 0;
        auto emit = [&psiCallCount](const BufferView &v) -> Error {
                // First two emits per access unit are PAT then PMT
                // (when due).  Count by examining the first packet's
                // PID.
                if (v.size() >= MpegTs::PacketSize) {
                        const uint8_t *p = v.data();
                        const uint16_t pid = static_cast<uint16_t>(((p[1] & 0x1F) << 8) | p[2]);
                        if (pid == MpegTs::PidPat || pid == MpegTs::DefaultPmtPid) ++psiCallCount;
                }
                return Error::Ok;
        };
        const auto au = makeFakeAccessUnit(500, 0x20);
        Buffer     buf = makeBuffer(au);
        // First write triggers PAT + PMT (forced).
        REQUIRE(mux.writeAccessUnit(0x100, viewOf(buf), 90000, 90000, true, emit) == Error::Ok);
        const int afterFirst = psiCallCount;
        CHECK(afterFirst == 2);
        // Second write 10ms later — no PSI re-emit.
        REQUIRE(mux.writeAccessUnit(0x100, viewOf(buf), 90000 + 900, 90000 + 900, false, emit) == Error::Ok);
        CHECK(psiCallCount == afterFirst);
        // 150 ms later — PSI re-emit due.
        REQUIRE(mux.writeAccessUnit(0x100, viewOf(buf), 90000 + 13500, 90000 + 13500, false, emit) == Error::Ok);
        CHECK(psiCallCount == afterFirst + 2);
}

TEST_CASE("MpegTsMuxer + Demuxer: round-trip a sequence of video access units") {
        MpegTsMuxer mux;
        mux.setPcrPid(0x100);
        REQUIRE(mux.addStream(0x100, MpegTs::StreamTypeH264) == Error::Ok);

        // Collect every emitted packet into one flat byte stream the
        // demuxer can replay.
        std::vector<uint8_t> tape;
        auto                 emit = [&tape](const BufferView &v) -> Error {
                tape.insert(tape.end(), v.data(), v.data() + v.size());
                return Error::Ok;
        };

        struct Au {
                        std::vector<uint8_t> bytes;
                        uint64_t             pts;
                        bool                 key;
        };
        std::vector<Au> aus;
        const int       kCount = 12;
        for (int i = 0; i < kCount; ++i) {
                Au au;
                au.bytes = makeFakeAccessUnit(800 + i * 137, static_cast<uint8_t>(0x30 + i));
                au.pts = 90000ull + static_cast<uint64_t>(i) * 3000;
                au.key = (i == 0); // First frame keyframe; the rest non-key.
                Buffer b = makeBuffer(au.bytes);
                REQUIRE(mux.writeAccessUnit(0x100, viewOf(b), au.pts, au.pts, au.key, emit) == Error::Ok);
                aus.push_back(std::move(au));
        }

        // Demux.
        MpegTsDemuxer            demux;
        std::vector<MpegTsDemuxer::AccessUnit> received;
        struct ReceivedAu {
                        std::vector<uint8_t>     bytes;
                        uint64_t                 pts;
                        bool                     hasPts;
                        bool                     randomAccess;
                        MpegTs::StreamType       streamType;
        };
        std::vector<ReceivedAu> receivedCopies;
        demux.setStreamCallback([&receivedCopies](const MpegTsDemuxer::AccessUnit &au) -> Error {
                ReceivedAu r;
                r.bytes.assign(au.payload.data(), au.payload.data() + au.payload.size());
                r.pts = au.pts90k;
                r.hasPts = au.hasPts;
                r.randomAccess = au.randomAccess;
                r.streamType = au.streamType;
                receivedCopies.push_back(std::move(r));
                return Error::Ok;
        });

        Buffer tapeBuf = makeBuffer(tape);
        REQUIRE(demux.push(viewOf(tapeBuf)) == Error::Ok);
        REQUIRE(demux.flush() == Error::Ok);

        // Reader should observe stream topology.
        REQUIRE(demux.streams().size() == 1);
        CHECK(demux.streams()[0].pid == 0x100);
        CHECK(demux.streams()[0].streamType == MpegTs::StreamTypeH264);

        // The demuxer emits access units once it sees the next PUSI
        // for an unbounded video PES.  That means the last AU we wrote
        // is held back until flush(); we asserted flush() ran, so all
        // should be present.
        REQUIRE(receivedCopies.size() == aus.size());
        for (size_t i = 0; i < aus.size(); ++i) {
                CHECK(receivedCopies[i].streamType == MpegTs::StreamTypeH264);
                CHECK(receivedCopies[i].hasPts);
                CHECK(receivedCopies[i].pts == aus[i].pts);
                CHECK(receivedCopies[i].randomAccess == aus[i].key);
                CHECK(receivedCopies[i].bytes == aus[i].bytes);
        }
        CHECK(demux.continuityErrors() == 0);
}

TEST_CASE("MpegTsDemuxer: re-syncs on torn input") {
        // Build a valid mini-stream, then mangle the leading bytes
        // to confirm the demuxer skips them and re-aligns to 0x47.
        MpegTsMuxer mux;
        mux.setPcrPid(0x100);
        REQUIRE(mux.addStream(0x100, MpegTs::StreamTypeH264) == Error::Ok);
        std::vector<uint8_t> tape;
        auto                 emit = [&tape](const BufferView &v) -> Error {
                tape.insert(tape.end(), v.data(), v.data() + v.size());
                return Error::Ok;
        };
        Buffer b = makeBuffer(makeFakeAccessUnit(500, 0x40));
        REQUIRE(mux.writeAccessUnit(0x100, viewOf(b), 90000, 90000, true, emit) == Error::Ok);

        // Prepend 37 junk bytes that don't contain 0x47.
        std::vector<uint8_t> junk(37, 0x10);
        std::vector<uint8_t> torn;
        torn.insert(torn.end(), junk.begin(), junk.end());
        torn.insert(torn.end(), tape.begin(), tape.end());

        MpegTsDemuxer demux;
        int           audCount = 0;
        demux.setStreamCallback([&audCount](const MpegTsDemuxer::AccessUnit &) -> Error {
                ++audCount;
                return Error::Ok;
        });
        Buffer tornBuf = makeBuffer(torn);
        REQUIRE(demux.push(viewOf(tornBuf)) == Error::Ok);
        REQUIRE(demux.flush() == Error::Ok);
        CHECK(audCount == 1);
        CHECK(demux.bytesDiscarded() >= 37);
        CHECK(demux.continuityErrors() == 0);
}

TEST_CASE("MpegTsDemuxer: accepts byte-by-byte feeding") {
        // Same as above but pushed one byte at a time — exercises
        // the carry buffer's straddle handling.
        MpegTsMuxer mux;
        mux.setPcrPid(0x100);
        REQUIRE(mux.addStream(0x100, MpegTs::StreamTypeH264) == Error::Ok);
        std::vector<uint8_t> tape;
        auto                 emit = [&tape](const BufferView &v) -> Error {
                tape.insert(tape.end(), v.data(), v.data() + v.size());
                return Error::Ok;
        };
        const int kAus = 4;
        for (int i = 0; i < kAus; ++i) {
                Buffer b = makeBuffer(makeFakeAccessUnit(300 + i * 50, static_cast<uint8_t>(0x50 + i)));
                REQUIRE(mux.writeAccessUnit(0x100, viewOf(b), 90000ull + i * 3000, 90000ull + i * 3000, i == 0,
                                            emit) == Error::Ok);
        }
        MpegTsDemuxer demux;
        int           audCount = 0;
        demux.setStreamCallback([&audCount](const MpegTsDemuxer::AccessUnit &) -> Error {
                ++audCount;
                return Error::Ok;
        });
        for (size_t i = 0; i < tape.size(); ++i) {
                Buffer one(1);
                one.setSize(1);
                static_cast<uint8_t *>(one.data())[0] = tape[i];
                REQUIRE(demux.push(viewOf(one)) == Error::Ok);
        }
        REQUIRE(demux.flush() == Error::Ok);
        CHECK(audCount == kAus);
}

TEST_CASE("MpegTsMuxer: bounded audio PES is finalized at the literal length") {
        // Audio PES carry a concrete packet_length; the demuxer must
        // not need the next PUSI to finalize.
        MpegTsMuxer mux;
        mux.setPcrPid(0x100);
        REQUIRE(mux.addStream(0x100, MpegTs::StreamTypeH264) == Error::Ok);
        REQUIRE(mux.addStream(0x101, MpegTs::StreamTypeAacAdts) == Error::Ok);
        std::vector<uint8_t> tape;
        auto                 emit = [&tape](const BufferView &v) -> Error {
                tape.insert(tape.end(), v.data(), v.data() + v.size());
                return Error::Ok;
        };
        const auto audioAu = makeFakeAccessUnit(361, 0xA0);
        Buffer     audioBuf = makeBuffer(audioAu);
        REQUIRE(mux.writeAccessUnit(0x101, viewOf(audioBuf), 12345, 12345, true, emit) == Error::Ok);

        MpegTsDemuxer demux;
        std::vector<uint8_t> received;
        bool                 gotAu = false;
        demux.setStreamCallback([&](const MpegTsDemuxer::AccessUnit &au) -> Error {
                if (au.streamType != MpegTs::StreamTypeAacAdts) return Error::Ok;
                received.assign(au.payload.data(), au.payload.data() + au.payload.size());
                gotAu = true;
                return Error::Ok;
        });
        Buffer tapeBuf = makeBuffer(tape);
        REQUIRE(demux.push(viewOf(tapeBuf)) == Error::Ok);
        // No flush needed — bounded PES end at packet_length.
        CHECK(gotAu);
        CHECK(received == audioAu);
}

TEST_CASE("MpegTsDemuxer: surfaces PMT registration_descriptor on StreamInfo + AccessUnit") {
        // Build a PMT-bearing stream where the elementary stream is
        // tagged with stream_type=0x06 (private PES) + a registration
        // descriptor carrying format_identifier "Opus".  Verify the
        // demuxer:
        //   - exposes registrationFormat=0x4F707573 on streams()
        //   - stamps the same value on every emitted AccessUnit
        //
        // The mux side passes the registration descriptor via the
        // addStream(descriptors) parameter and the StreamKind::Audio
        // hint (so the stream_id lands on private_stream_1 / 0xBD).
        MpegTsMuxer mux;
        mux.setPcrPid(0x100);
        REQUIRE(mux.addStream(0x100, MpegTs::StreamTypeH264) == Error::Ok);

        Buffer regDesc;
        REQUIRE(MpegTs::buildRegistrationDescriptor(MpegTs::RegFormatOpus, regDesc).isOk());
        REQUIRE(mux.addStream(0x101, MpegTs::StreamTypePrivatePes,
                              BufferView(regDesc, 0, regDesc.size()), MpegTs::StreamKind::Audio) == Error::Ok);

        std::vector<uint8_t> tape;
        auto                 emit = [&tape](const BufferView &v) -> Error {
                tape.insert(tape.end(), v.data(), v.data() + v.size());
                return Error::Ok;
        };

        // One AU on the Opus stream.
        Buffer opusAu = makeBuffer(makeFakeAccessUnit(64, 0x99));
        REQUIRE(mux.writeAccessUnit(0x101, viewOf(opusAu), 90000, 90000, true, emit) == Error::Ok);

        MpegTsDemuxer demux;
        struct Captured {
                        uint16_t pid;
                        MpegTs::StreamType streamType;
                        uint32_t           registrationFormat;
                        std::vector<uint8_t> bytes;
        };
        std::vector<Captured> received;
        demux.setStreamCallback([&received](const MpegTsDemuxer::AccessUnit &au) -> Error {
                Captured c;
                c.pid = au.pid;
                c.streamType = au.streamType;
                c.registrationFormat = au.registrationFormat;
                c.bytes.assign(au.payload.data(), au.payload.data() + au.payload.size());
                received.push_back(std::move(c));
                return Error::Ok;
        });

        Buffer tapeBuf = makeBuffer(tape);
        REQUIRE(demux.push(viewOf(tapeBuf)) == Error::Ok);
        REQUIRE(demux.flush() == Error::Ok);

        // PMT topology: two streams, the Opus one carries the
        // registration_format from the registration_descriptor.
        const auto streams = demux.streams();
        REQUIRE(streams.size() == 2);
        bool sawOpus = false;
        bool sawH264 = false;
        for (size_t i = 0; i < streams.size(); ++i) {
                if (streams[i].streamType == MpegTs::StreamTypePrivatePes) {
                        sawOpus = true;
                        CHECK(streams[i].registrationFormat == MpegTs::RegFormatOpus);
                } else if (streams[i].streamType == MpegTs::StreamTypeH264) {
                        sawH264 = true;
                        CHECK(streams[i].registrationFormat == 0u);
                }
        }
        CHECK(sawOpus);
        CHECK(sawH264);

        // AU-level: every Opus AU carries the registration_format.
        REQUIRE_FALSE(received.empty());
        for (const Captured &c : received) {
                if (c.pid == 0x101) {
                        CHECK(c.streamType == MpegTs::StreamTypePrivatePes);
                        CHECK(c.registrationFormat == MpegTs::RegFormatOpus);
                        CHECK(c.bytes == std::vector<uint8_t>(opusAu.data() ?
                                                              static_cast<uint8_t *>(opusAu.data()) :
                                                              nullptr,
                                                              static_cast<uint8_t *>(opusAu.data()) +
                                                                      opusAu.size()));
                }
        }
}
