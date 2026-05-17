/**
 * @file      rtmpmediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>

#include <promeki/aacbitstream.h>
#include <promeki/audiocodec.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/audiopayload.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/clock.h>
#include <promeki/compressedaudiopayload.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/flvtag.h>
#include <promeki/frame.h>
#include <promeki/h264bitstream.h>
#include <promeki/hevcbitstream.h>
#include <promeki/imagedesc.h>
#include <promeki/logger.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediapayload.h>
#include <promeki/objectbase.tpp>
#include <promeki/pixelformat.h>
#include <promeki/rtmpclient.h>
#include <promeki/rtmpmediaio.h>
#include <promeki/thread.h>
#include <promeki/timestamp.h>
#include <promeki/url.h>
#include <promeki/videocodec.h>
#include <promeki/videopayload.h>

#if defined(PROMEKI_ENABLE_TLS)
#include <promeki/sslcontext.h>
#endif

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(RtmpMediaIO)
PROMEKI_REGISTER_MEDIAIO_FACTORY(RtmpFactory)

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

constexpr unsigned int kReadPollMs = 100;
constexpr unsigned int kDepacketizerPopMs = 100;
constexpr unsigned int kClientPushTimeoutMs = 250;
constexpr unsigned int kPublishTimeoutMs = 5000;
constexpr unsigned int kPlayTimeoutMs = 5000;
constexpr unsigned int kOpenTimeoutMs = 10000;

/// @brief Returns true if @p view bytes start with an Annex-B start code.
bool looksLikeAnnexB(const BufferView &view) {
        if (view.count() == 0) return false;
        BufferView::Entry e = view[0];
        const uint8_t   *p = e.data();
        const size_t     n = e.size();
        if (n < 3) return false;
        if (p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x01) return true;
        return n >= 4 && p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x00 && p[3] == 0x01;
}

/// @brief Copies a payload's first-plane bytes into a fresh standalone Buffer.
Buffer copyFirstPlane(const BufferView &view) {
        Buffer out;
        if (view.count() == 0) return out;
        BufferView::Entry e = view[0];
        out = Buffer(e.size());
        out.setSize(e.size());
        if (e.size() > 0) {
                std::memcpy(out.data(), e.data(), e.size());
        }
        return out;
}

/// @brief Maps a video codec to its FLV codec ID.
FlvVideoTag::Codec flvCodecForVideo(const VideoCodec &c) {
        switch (c.id()) {
                case VideoCodec::H264: return FlvVideoTag::Avc;
                case VideoCodec::HEVC: return FlvVideoTag::ExHevc;
                default:               return FlvVideoTag::CodecUnknown;
        }
}

/// @brief Picks a FLV @c SoundRate enum closest to @p hz.
FlvAudioTag::SoundRate flvRateFor(float hz) {
        if (hz >= 32000.0f) return FlvAudioTag::Rate44000;
        if (hz >= 16500.0f) return FlvAudioTag::Rate22000;
        if (hz >= 8250.0f)  return FlvAudioTag::Rate11000;
        return FlvAudioTag::Rate5500;
}

/// @brief Quantize a @ref MediaTimeStamp to a 32-bit millisecond stamp suitable for RTMP.
uint32_t timestampMs(const MediaTimeStamp &mts) {
        if (!mts.isValid()) return 0;
        const int64_t ms = mts.timeStamp().milliseconds();
        if (ms < 0) return 0;
        return static_cast<uint32_t>(ms & 0xFFFFFFFFu);
}

/// @brief Builds an @c avcC blob from an Annex-B access unit's parameter sets.
///
/// Populates @p outCfg as a side effect so the caller can cache the
/// parameter-set NALs for in-band re-injection ahead of subsequent IDRs
/// (the @ref MediaConfig::RtmpRepeatParameterSets policy).
Error buildAvccSequenceHeader(const BufferView &au, Buffer &outAvcc, AvcDecoderConfig &outCfg) {
        Error err = AvcDecoderConfig::fromAnnexB(au, outCfg);
        if (err.isError()) return err;
        if (outCfg.sps.isEmpty() || outCfg.pps.isEmpty()) return Error::InvalidArgument;
        return outCfg.serialize(outAvcc);
}

/// @brief Builds an @c hvcC blob from an Annex-B access unit's parameter sets.
///
/// Populates @p outCfg as a side effect so the caller can cache the
/// parameter-set NALs for in-band re-injection ahead of subsequent IDRs.
Error buildHvccSequenceHeader(const BufferView &au, Buffer &outHvcc, HevcDecoderConfig &outCfg) {
        Error err = HevcDecoderConfig::fromAnnexB(au, outCfg);
        if (err.isError()) return err;
        if (outCfg.vps.isEmpty() || outCfg.sps.isEmpty() || outCfg.pps.isEmpty()) return Error::InvalidArgument;
        return outCfg.serialize(outHvcc);
}

/// @brief NAL types of interest for in-band parameter-set detection.
enum AnnexBNalCategory {
        NalCategoryUnknown = 0,
        NalCategoryH264Sps = 1,        ///< H.264 nal_unit_type == 7
        NalCategoryH264Pps = 1 << 1,   ///< H.264 nal_unit_type == 8
        NalCategoryHevcVps = 1 << 2,   ///< HEVC nal_unit_type == 32
        NalCategoryHevcSps = 1 << 3,   ///< HEVC nal_unit_type == 33
        NalCategoryHevcPps = 1 << 4,   ///< HEVC nal_unit_type == 34
};

/// @brief Walks @p au and returns a bitmask of which parameter-set NALs are present.
///
/// Bits use the @ref AnnexBNalCategory layout.  Codec-aware so the
/// caller can distinguish "H.264 SPS missing" from "HEVC SPS missing"
/// in the same access unit.
unsigned int detectAnnexBParameterSets(const BufferView &au, bool isHevc) {
        unsigned int mask = 0;
        (void)H264Bitstream::forEachAnnexBNal(au,
                [&mask, isHevc](const H264Bitstream::NalUnit &nu) -> Error {
                        if (isHevc) {
                                const uint8_t nalType = static_cast<uint8_t>((nu.header0 >> 1) & 0x3F);
                                if (nalType == 32) mask |= NalCategoryHevcVps;
                                else if (nalType == 33) mask |= NalCategoryHevcSps;
                                else if (nalType == 34) mask |= NalCategoryHevcPps;
                        } else {
                                const uint8_t nalType = static_cast<uint8_t>(nu.header0 & 0x1F);
                                if (nalType == 7) mask |= NalCategoryH264Sps;
                                else if (nalType == 8) mask |= NalCategoryH264Pps;
                        }
                        return Error::Ok;
                });
        return mask;
}

/// @brief Builds a length-prefixed AVCC blob containing every NAL in @p nals.
///
/// Each NAL receives a 4-byte big-endian length prefix (matching the
/// @c lengthSizeMinusOne = 3 default RTMP picks).  Returns an empty
/// buffer when @p nals is empty.
Buffer buildNalsAsAvcc4(const List<Buffer> &nals) {
        size_t total = 0;
        for (size_t i = 0; i < nals.size(); ++i) {
                total += 4 + nals[i].size();
        }
        Buffer out;
        if (total == 0) return out;
        out = Buffer(total);
        out.setSize(total);
        uint8_t *p = static_cast<uint8_t *>(out.data());
        for (size_t i = 0; i < nals.size(); ++i) {
                const Buffer  &n = nals[i];
                const uint32_t sz = static_cast<uint32_t>(n.size());
                p[0] = static_cast<uint8_t>((sz >> 24) & 0xFF);
                p[1] = static_cast<uint8_t>((sz >> 16) & 0xFF);
                p[2] = static_cast<uint8_t>((sz >>  8) & 0xFF);
                p[3] = static_cast<uint8_t>( sz        & 0xFF);
                if (n.size() > 0) {
                        std::memcpy(p + 4, n.data(), n.size());
                }
                p += 4 + n.size();
        }
        return out;
}

/// @brief Concatenate two buffers into a single fresh one (zero-copy if @p a is empty).
Buffer concatBuffers(const Buffer &a, const Buffer &b) {
        if (a.size() == 0) return b;
        if (b.size() == 0) return a;
        const size_t total = a.size() + b.size();
        Buffer       out(total);
        out.setSize(total);
        uint8_t *p = static_cast<uint8_t *>(out.data());
        std::memcpy(p, a.data(), a.size());
        std::memcpy(p + a.size(), b.data(), b.size());
        return out;
}

} // namespace

// ----------------------------------------------------------------------------
// PacketizerThread — sink-side worker
// ----------------------------------------------------------------------------

/**
 * @brief Per-instance sink-side worker.
 *
 * Drains the strand-side payload queue, converts each Frame's video /
 * audio payloads to FLV tags, emits sequence-header tags on the first
 * keyframe of each kind, and dispatches via RtmpClient::sendVideo /
 * sendAudio.  Runs on its own thread so the strand never blocks on
 * the chunk-layer write queue.
 */
class RtmpMediaIO::PacketizerThread : public Thread {
        public:
                /// @brief Strand-side handoff payload — captured CoW Frame plus optional config-update flag.
                struct Item {
                                Frame frame;
                };

                static constexpr size_t kDefaultQueueDepth = 64;

                explicit PacketizerThread(RtmpMediaIO *owner)
                    : _owner(owner) {
                        _queue.setMaxSize(static_cast<size_t>(_owner->_sendQueueDepth));
                        setName(String("RtmpPacketizer"));
                }

                ~PacketizerThread() override { (void)stop(); }

                /// @brief Push a frame; returns @c Error::TryAgain on full-queue timeout.
                Error pushFrame(Frame f) {
                        Item it{std::move(f)};
                        Error err = _queue.pushBlocking(std::move(it), kClientPushTimeoutMs);
                        if (err == Error::Timeout) return Error::TryAgain;
                        return err;
                }

                /// @brief Wake any blocked push/pop and join the thread.
                Error stop() {
                        if (!isRunning() && !_started.value()) return Error::Ok;
                        _stopping.setValue(true);
                        _queue.cancelWaiters();
                        return wait();
                }

                /// @brief Live depth of the strand→worker queue.
                int64_t queueDepth() const { return static_cast<int64_t>(_queue.size()); }

        protected:
                void run() override {
                        _started.setValue(true);
                        while (!_stopping.value()) {
                                // If the underlying client tore down
                                // (peer went away, socket failure, etc.)
                                // stop pulling frames rather than
                                // spewing per-frame send failures —
                                // the strand-side write path surfaces
                                // the error to the pipeline via
                                // RtmpMediaIO::_clientDisconnected.
                                if (_owner->_clientDisconnected.value()) break;
                                Result<Item> r = _queue.pop(kDepacketizerPopMs);
                                if (r.second() == Error::Cancelled) break;
                                if (r.second() == Error::Timeout) continue;
                                if (r.second().isError()) {
                                        promekiWarn("RtmpMediaIO::PacketizerThread: pop failed: %s",
                                                    r.second().desc().cstr());
                                        break;
                                }
                                handleFrame(r.first().frame);
                        }
                }

        private:
                void handleFrame(const Frame &frame) {
                        // Video first.  Skip silently when no compressed
                        // payload is available; the planner is expected
                        // to insert a VideoEncoderMediaIO upstream when
                        // the source is uncompressed.
                        VideoPayload::PtrList vpl = frame.videoPayloads();
                        for (const VideoPayload::Ptr &vp : vpl) {
                                if (!vp.isValid()) continue;
                                const CompressedVideoPayload *cv = vp->as<CompressedVideoPayload>();
                                if (cv == nullptr) continue;
                                handleVideoPayload(*cv);
                        }
                        AudioPayload::PtrList apl = frame.audioPayloads();
                        for (const AudioPayload::Ptr &ap : apl) {
                                if (!ap.isValid()) continue;
                                const CompressedAudioPayload *ca = ap->as<CompressedAudioPayload>();
                                if (ca == nullptr) continue;
                                handleAudioPayload(*ca);
                        }
                }

                void handleVideoPayload(const CompressedVideoPayload &p) {
                        if (!p.isValid()) return;
                        const VideoCodec &codec = p.desc().pixelFormat().videoCodec();
                        const FlvVideoTag::Codec flvCodec = flvCodecForVideo(codec);
                        if (flvCodec == FlvVideoTag::CodecUnknown) {
                                promekiWarn("RtmpMediaIO: unsupported video codec %s",
                                            codec.name().cstr());
                                return;
                        }

                        const BufferView &data = p.data();
                        const bool        isKey = p.isKeyframe();

                        // First-IDR gate.  Most ingest servers reject a
                        // publish that begins on an inter-frame, so drop
                        // anything that arrives before the first IDR
                        // until we have seen one.
                        if (!_videoSequenceSent && !isKey) {
                                if (_owner->_dropUntilKeyframe) {
                                        _owner->_videoFramesDroppedPreIdr.fetchAndAdd(1);
                                }
                                return;
                        }

                        // Sequence header on the first IDR.  Annex-B in,
                        // avcC / hvcC out.  When the source is already
                        // AVCC we cannot recover the parameter sets
                        // from the access unit alone — skip the
                        // sequence header on the first frame and rely
                        // on out-of-band signaling once that path lands.
                        const bool annexB = looksLikeAnnexB(data);
                        const bool isHevc = (codec.id() == VideoCodec::HEVC);
                        if (!_videoSequenceSent && isKey && annexB) {
                                Buffer seq;
                                Error  err = isHevc
                                                     ? buildHvccSequenceHeader(data, seq, _hevcConfig)
                                                     : buildAvccSequenceHeader(data, seq, _avcConfig);
                                if (err.isOk() && seq.size() > 0) {
                                        FlvVideoTag tag;
                                        tag.frameType = FlvVideoTag::Keyframe;
                                        tag.codec = flvCodec;
                                        tag.packetType = FlvVideoTag::SequenceHeader;
                                        tag.compositionTimeOffsetMs = 0;
                                        tag.data = seq;
                                        Error sendErr = _owner->_client->sendVideo(tag, timestampMs(p.dts().isValid() ? p.dts() : p.pts()));
                                        if (sendErr.isError() && sendErr != Error::Cancelled) {
                                                promekiWarn("RtmpMediaIO: sequence-header send failed: %s",
                                                            sendErr.desc().cstr());
                                                return;
                                        }
                                        _videoSequenceSent = true;
                                        _haveCachedParameterSets = true;
                                }
                        }

                        // Convert Annex-B to AVCC on the fly so the wire
                        // payload is what RTMP expects regardless of
                        // what the encoder emitted.
                        Buffer avccPayload;
                        if (annexB) {
                                Error err = H264Bitstream::annexBToAvcc(data, /*lenSize=*/4, avccPayload);
                                if (err.isError()) {
                                        promekiWarn("RtmpMediaIO: Annex-B → AVCC failed: %s",
                                                    err.desc().cstr());
                                        return;
                                }
                        } else {
                                avccPayload = copyFirstPlane(data);
                        }

                        if (avccPayload.size() == 0) return;

                        // In-band parameter-set repeat: ahead of every
                        // IDR, ensure SPS/PPS (and VPS for HEVC) are
                        // present in the AVCC payload itself so a
                        // subscriber joining mid-stream can decode the
                        // next IDR without waiting for the next
                        // out-of-band sequence-header refresh.  Only
                        // active on Annex-B input (we need to inspect
                        // NAL types to avoid double-injection) and only
                        // when the caller asked for it.
                        if (isKey && annexB && _haveCachedParameterSets
                            && _owner->_repeatParameterSets) {
                                const unsigned int present =
                                        detectAnnexBParameterSets(data, isHevc);
                                List<Buffer> missing;
                                if (isHevc) {
                                        if ((present & NalCategoryHevcVps) == 0) {
                                                for (size_t i = 0; i < _hevcConfig.vps.size(); ++i) {
                                                        missing.pushToBack(_hevcConfig.vps[i]);
                                                }
                                        }
                                        if ((present & NalCategoryHevcSps) == 0) {
                                                for (size_t i = 0; i < _hevcConfig.sps.size(); ++i) {
                                                        missing.pushToBack(_hevcConfig.sps[i]);
                                                }
                                        }
                                        if ((present & NalCategoryHevcPps) == 0) {
                                                for (size_t i = 0; i < _hevcConfig.pps.size(); ++i) {
                                                        missing.pushToBack(_hevcConfig.pps[i]);
                                                }
                                        }
                                } else {
                                        if ((present & NalCategoryH264Sps) == 0) {
                                                for (size_t i = 0; i < _avcConfig.sps.size(); ++i) {
                                                        missing.pushToBack(_avcConfig.sps[i]);
                                                }
                                        }
                                        if ((present & NalCategoryH264Pps) == 0) {
                                                for (size_t i = 0; i < _avcConfig.pps.size(); ++i) {
                                                        missing.pushToBack(_avcConfig.pps[i]);
                                                }
                                        }
                                }
                                if (!missing.isEmpty()) {
                                        Buffer prefix = buildNalsAsAvcc4(missing);
                                        avccPayload = concatBuffers(prefix, avccPayload);
                                }
                        }

                        FlvVideoTag tag;
                        tag.frameType = isKey ? FlvVideoTag::Keyframe : FlvVideoTag::InterFrame;
                        tag.codec = flvCodec;
                        tag.packetType = FlvVideoTag::Nalu;

                        // Composition-time offset (PTS - DTS) carries
                        // B-frame reorder offsets.  Zero on no-reorder
                        // streams and on streams where DTS is missing.
                        int32_t cto = 0;
                        if (p.pts().isValid() && p.dts().isValid()) {
                                const int64_t ptsMs = p.pts().timeStamp().milliseconds();
                                const int64_t dtsMs = p.dts().timeStamp().milliseconds();
                                cto = static_cast<int32_t>(ptsMs - dtsMs);
                        }
                        tag.compositionTimeOffsetMs = cto;
                        tag.data = avccPayload;

                        // The RTMP message timestamp is DTS (decode
                        // order); CTO carries the PTS - DTS offset.
                        const MediaTimeStamp &stamp = p.dts().isValid() ? p.dts() : p.pts();
                        Error sendErr = _owner->_client->sendVideo(tag, timestampMs(stamp));
                        if (sendErr.isError() && sendErr != Error::Cancelled) {
                                if (sendErr == Error::TryAgain) {
                                        _owner->_sendQueueOverflows.fetchAndAdd(1);
                                } else if (sendErr == Error::Invalid) {
                                        // Client torn down; the strand-side
                                        // _clientDisconnected slot will surface
                                        // the failure to the pipeline.
                                } else {
                                        promekiWarn("RtmpMediaIO: video send failed: %s",
                                                    sendErr.desc().cstr());
                                }
                        }
                }

                void handleAudioPayload(const CompressedAudioPayload &p) {
                        if (!p.isValid()) return;
                        const AudioCodec &codec = p.desc().format().audioCodec();
                        if (codec.id() != AudioCodec::AAC) {
                                promekiWarn("RtmpMediaIO: unsupported audio codec %s",
                                            codec.name().cstr());
                                return;
                        }

                        // Sequence header on the first audio access
                        // unit.  Derived from the descriptor — works
                        // for any encoder backend that populated a
                        // valid AudioDesc on the payload.
                        if (!_audioSequenceSent) {
                                AacDecoderConfig cfg = AacDecoderConfig::fromAudioDesc(p.desc());
                                Buffer           hdr;
                                Error            err = cfg.serialize(hdr);
                                if (err.isOk() && hdr.size() > 0) {
                                        FlvAudioTag tag;
                                        tag.format = FlvAudioTag::Aac;
                                        tag.rate = flvRateFor(p.desc().sampleRate());
                                        tag.size = FlvAudioTag::Bits16;
                                        tag.channelType = (p.desc().channels() > 1) ? FlvAudioTag::Stereo : FlvAudioTag::Mono;
                                        tag.aacPacketType = FlvAudioTag::AudioSpecificConfig;
                                        tag.data = hdr;
                                        Error sendErr = _owner->_client->sendAudio(tag, timestampMs(p.pts()));
                                        if (sendErr.isError() && sendErr != Error::Cancelled) {
                                                promekiWarn("RtmpMediaIO: audio sequence header send failed: %s",
                                                            sendErr.desc().cstr());
                                                return;
                                        }
                                        _audioSequenceSent = true;
                                }
                        }

                        FlvAudioTag tag;
                        tag.format = FlvAudioTag::Aac;
                        tag.rate = flvRateFor(p.desc().sampleRate());
                        tag.size = FlvAudioTag::Bits16;
                        tag.channelType = (p.desc().channels() > 1) ? FlvAudioTag::Stereo : FlvAudioTag::Mono;
                        tag.aacPacketType = FlvAudioTag::Raw;
                        tag.data = copyFirstPlane(p.data());

                        if (tag.data.size() == 0) return;

                        Error sendErr = _owner->_client->sendAudio(tag, timestampMs(p.pts()));
                        if (sendErr.isError() && sendErr != Error::Cancelled) {
                                if (sendErr == Error::TryAgain) {
                                        _owner->_sendQueueOverflows.fetchAndAdd(1);
                                } else if (sendErr == Error::Invalid) {
                                        // Client torn down; the strand-side
                                        // _clientDisconnected slot will surface
                                        // the failure to the pipeline.
                                } else {
                                        promekiWarn("RtmpMediaIO: audio send failed: %s",
                                                    sendErr.desc().cstr());
                                }
                        }
                }

                RtmpMediaIO       *_owner;
                Queue<Item>        _queue;
                Atomic<bool>       _stopping{false};
                Atomic<bool>       _started{false};
                bool               _videoSequenceSent = false;
                bool               _audioSequenceSent = false;

                // Parameter-set cache populated from the first IDR's
                // Annex-B access unit.  Used by the
                // @ref MediaConfig::RtmpRepeatParameterSets policy to
                // prepend SPS / PPS (and VPS for HEVC) ahead of every
                // IDR access unit that doesn't already carry them
                // in-band, so a subscriber joining mid-stream can
                // decode the next IDR without waiting for a fresh
                // out-of-band sequence header.  Only one of the two
                // configs is populated per stream (codec-dependent).
                bool               _haveCachedParameterSets = false;
                AvcDecoderConfig   _avcConfig;
                HevcDecoderConfig  _hevcConfig;
};

// ----------------------------------------------------------------------------
// DepacketizerThread — source-side worker
// ----------------------------------------------------------------------------

/**
 * @brief Per-instance source-side worker.
 *
 * Drains @ref RtmpClient::takeVideo / @ref takeAudio / @ref takeMetadata,
 * parses the FLV tags, builds @ref Frame payloads, and pushes the
 * resulting frames onto the strand-side reader queue.  Runs on its
 * own thread so the strand isn't blocked on socket-arrival cadence.
 */
class RtmpMediaIO::DepacketizerThread : public Thread {
        public:
                explicit DepacketizerThread(RtmpMediaIO *owner) : _owner(owner) {
                        setName(String("RtmpDepacketizer"));
                }

                ~DepacketizerThread() override { (void)stop(); }

                Error stop() {
                        if (!isRunning() && !_started.value()) return Error::Ok;
                        _stopping.setValue(true);
                        return wait();
                }

        protected:
                void run() override {
                        _started.setValue(true);
                        while (!_stopping.value()) {
                                // Client tore down (peer went away,
                                // socket failure, etc.) — exit the
                                // pump rather than busy-looping on
                                // Error::Invalid take results.
                                if (_owner->_clientDisconnected.value()) break;

                                bool any = false;

                                // Video first — most likely to be hot.
                                Result<FlvVideoTag> vr = _owner->_client->takeVideo(kDepacketizerPopMs);
                                if (vr.second().isOk()) {
                                        handleVideo(vr.first());
                                        any = true;
                                } else if (vr.second() == Error::TryAgain || vr.second() == Error::Timeout) {
                                        // nothing pending — fall through
                                } else if (vr.second() == Error::Cancelled
                                           || vr.second() == Error::Invalid) {
                                        break;
                                }

                                // Audio.
                                Result<FlvAudioTag> ar = _owner->_client->takeAudio(1);
                                if (ar.second().isOk()) {
                                        handleAudio(ar.first());
                                        any = true;
                                } else if (ar.second() == Error::Cancelled
                                           || ar.second() == Error::Invalid) {
                                        break;
                                }

                                // Metadata.  Optional.
                                Result<Metadata> mr = _owner->_client->takeMetadata(0);
                                if (mr.second().isOk()) {
                                        handleMetadata(mr.first());
                                        any = true;
                                } else if (mr.second() == Error::Cancelled
                                           || mr.second() == Error::Invalid) {
                                        break;
                                }

                                if (!any) {
                                        // Brief yield so we don't spin
                                        // when the wire is idle.
                                        Thread::sleepMs(5);
                                }
                        }
                }

        private:
                void handleVideo(const FlvVideoTag &tag) {
                        if (tag.data.size() == 0) return;

                        // Map the FLV codec to a PixelFormat once per
                        // tag — used by both the SequenceHeader and
                        // Nalu paths.
                        PixelFormat pf;
                        if (tag.codec == FlvVideoTag::ExHevc) {
                                pf = PixelFormat(PixelFormat::HEVC);
                        } else if (tag.codec == FlvVideoTag::Avc) {
                                pf = PixelFormat(PixelFormat::H264);
                        } else {
                                return;
                        }
                        const bool isHevc = (tag.codec == FlvVideoTag::ExHevc);

                        // SequenceHeader: parse the avcC / hvcC blob,
                        // cache the parameter sets, extract resolution
                        // from the first SPS, and emit a
                        // CompressedVideoPayload tagged ParameterSet so
                        // downstream decoders that prefer an
                        // out-of-band primer (NVDEC, ffmpeg's
                        // h264_cuvid) can configure themselves.
                        if (tag.packetType == FlvVideoTag::SequenceHeader) {
                                _videoSequenceSeen = true;
                                BufferView cfgView(tag.data, 0, tag.data.size());

                                List<Buffer> psNals;
                                Size2Du32    spsRes;
                                if (isHevc) {
                                        HevcDecoderConfig cfg;
                                        Error             err = HevcDecoderConfig::parse(cfgView, cfg);
                                        if (err.isError()) {
                                                promekiWarn("RtmpMediaIO: hvcC parse failed: %s",
                                                            err.desc().cstr());
                                                return;
                                        }
                                        for (size_t i = 0; i < cfg.vps.size(); ++i) psNals.pushToBack(cfg.vps[i]);
                                        for (size_t i = 0; i < cfg.sps.size(); ++i) psNals.pushToBack(cfg.sps[i]);
                                        for (size_t i = 0; i < cfg.pps.size(); ++i) psNals.pushToBack(cfg.pps[i]);
                                        if (!cfg.sps.isEmpty()) {
                                                HevcDecoderConfig::SpsInfo info;
                                                if (HevcDecoderConfig::parseSpsResolution(
                                                            BufferView(cfg.sps[0], 0, cfg.sps[0].size()),
                                                            info).isOk()) {
                                                        spsRes = Size2Du32(info.width, info.height);
                                                }
                                        }
                                } else {
                                        AvcDecoderConfig cfg;
                                        Error            err = AvcDecoderConfig::parse(cfgView, cfg);
                                        if (err.isError()) {
                                                promekiWarn("RtmpMediaIO: avcC parse failed: %s",
                                                            err.desc().cstr());
                                                return;
                                        }
                                        for (size_t i = 0; i < cfg.sps.size(); ++i) psNals.pushToBack(cfg.sps[i]);
                                        for (size_t i = 0; i < cfg.pps.size(); ++i) psNals.pushToBack(cfg.pps[i]);
                                        if (!cfg.sps.isEmpty()) {
                                                H264Bitstream::SpsInfo info;
                                                if (H264Bitstream::parseSpsResolution(
                                                            BufferView(cfg.sps[0], 0, cfg.sps[0].size()),
                                                            info).isOk()) {
                                                        spsRes = Size2Du32(info.width, info.height);
                                                }
                                        }
                                }

                                _videoDesc = ImageDesc(spsRes, pf);

                                // Emit a parameter-set Frame so a
                                // downstream decoder can configure
                                // itself before any NALU frames land.
                                // The payload carries the AVCC bytes
                                // (length-prefixed parameter-set NALs)
                                // that exactly match what subsequent
                                // Nalu payloads carry.
                                if (!psNals.isEmpty()) {
                                        Buffer psPayload = buildNalsAsAvcc4(psNals);
                                        CompressedVideoPayload::Ptr cv =
                                                CompressedVideoPayload::Ptr::create(_videoDesc, psPayload);
                                        cv.modify()->markParameterSet(true);
                                        Frame f;
                                        f.addPayload(cv);
                                        (void)_owner->_readerQueue.pushDropOldest(std::move(f));
                                        _owner->_readerFramesReceived.fetchAndAdd(1);
                                }
                                return;
                        }
                        if (tag.packetType != FlvVideoTag::Nalu) return;

                        // If we never saw a SequenceHeader (server
                        // joined-late, broken publisher) fall back to a
                        // descriptor with the codec only — downstream
                        // gets unknown resolution but can still decode
                        // when the encoder re-injects SPS/PPS in band.
                        if (_videoDesc.pixelFormat().id() != pf.id()) {
                                _videoDesc = ImageDesc(Size2Du32(), pf);
                        }

                        Buffer payload(tag.data.size());
                        payload.setSize(tag.data.size());
                        std::memcpy(payload.data(), tag.data.data(), tag.data.size());

                        CompressedVideoPayload::Ptr cv =
                                CompressedVideoPayload::Ptr::create(_videoDesc, payload);
                        if (tag.frameType == FlvVideoTag::Keyframe ||
                            tag.frameType == FlvVideoTag::GeneratedKeyframe) {
                                cv.modify()->addFlag(MediaPayload::Keyframe);
                        }

                        Frame f;
                        f.addPayload(cv);
                        (void)_owner->_readerQueue.pushDropOldest(std::move(f));
                        _owner->_readerFramesReceived.fetchAndAdd(1);
                }

                void handleAudio(const FlvAudioTag &tag) {
                        if (tag.format != FlvAudioTag::Aac) return;
                        if (tag.aacPacketType == FlvAudioTag::AudioSpecificConfig) {
                                _audioSequenceSeen = true;
                                AacDecoderConfig cfg;
                                BufferView       view(tag.data, 0, tag.data.size());
                                Error            err = AacDecoderConfig::parse(view, cfg);
                                if (err.isError()) {
                                        promekiWarn("RtmpMediaIO: AudioSpecificConfig parse failed: %s",
                                                    err.desc().cstr());
                                        return;
                                }
                                _audioDesc = cfg.toAudioDesc();
                                return;
                        }
                        if (tag.data.size() == 0) return;

                        // Fall back to a sensible default when no
                        // SequenceHeader has arrived — most subscribers
                        // start at 48 kHz stereo and the FLV
                        // soundrate / soundtype fields are coarse
                        // 4 kHz / mono-or-stereo enums anyway.
                        if (!_audioSequenceSeen) {
                                _audioDesc = AudioDesc(AudioFormat(AudioFormat::AAC), 48000.0f, 2u);
                        }

                        Buffer payload(tag.data.size());
                        payload.setSize(tag.data.size());
                        std::memcpy(payload.data(), tag.data.data(), tag.data.size());

                        CompressedAudioPayload::Ptr ca =
                                CompressedAudioPayload::Ptr::create(_audioDesc, payload);
                        Frame f;
                        f.addPayload(ca);
                        (void)_owner->_readerQueue.pushDropOldest(std::move(f));
                        _owner->_readerFramesReceived.fetchAndAdd(1);
                }

                void handleMetadata(const Metadata &m) {
                        // Pass the @c onMetaData blob through as a
                        // metadata-only frame so a downstream Pipeline
                        // stage can read width / height / framerate /
                        // bitrate hints out of the publisher's
                        // announcement.  This is informational only —
                        // the wire-side parameter-set + sample-rate
                        // recovery above is the authoritative source.
                        Frame f;
                        f.metadata() = m;
                        (void)_owner->_readerQueue.pushDropOldest(std::move(f));
                        _owner->_readerFramesReceived.fetchAndAdd(1);
                }

                RtmpMediaIO *_owner;
                Atomic<bool> _stopping{false};
                Atomic<bool> _started{false};
                bool         _videoSequenceSeen = false;
                bool         _audioSequenceSeen = false;

                // Descriptors built from the most recent SequenceHeader.
                // ImageDesc carries the SPS-derived resolution; AudioDesc
                // carries the AudioSpecificConfig-derived rate / channels.
                // Reset only when a fresh SequenceHeader arrives, so any
                // mid-stream parameter change is honored.
                ImageDesc _videoDesc;
                AudioDesc _audioDesc{AudioFormat(AudioFormat::AAC), 48000.0f, 2u};
};

// ----------------------------------------------------------------------------
// Factory
// ----------------------------------------------------------------------------

RtmpFactory::Config::SpecMap RtmpFactory::configSpecs() const {
        Config::SpecMap specs;
        auto            s = [&specs](MediaConfig::ID id, const Variant &def) {
                const VariantSpec *gs = MediaConfig::spec(id);
                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
        };
        // RtmpUrl is required; no sensible default exists.  Caller
        // must populate it from a parsed @ref Url before open.  The
        // global spec is still recognised because the @c MediaConfig
        // registry sees the @c PROMEKI_DECLARE_ID; only the
        // factory's defaultConfig view omits it.
        s(MediaConfig::RtmpStreamKey, String());
        s(MediaConfig::RtmpAppName, String());
        s(MediaConfig::RtmpDropUntilKeyframe, true);
        s(MediaConfig::RtmpRepeatParameterSets, true);
        s(MediaConfig::RtmpEnhancedRtmp, true);
        s(MediaConfig::RtmpEmitAnnexB, false);
        s(MediaConfig::RtmpDataEnabled, true);
        s(MediaConfig::RtmpConnectTimeoutMs, int32_t(10000));
        s(MediaConfig::RtmpHandshakeTimeoutMs, int32_t(10000));
        s(MediaConfig::RtmpCommandTimeoutMs, int32_t(5000));
        s(MediaConfig::RtmpSendQueueDepth, int32_t(64));
        s(MediaConfig::RtmpReadQueueDepth, int32_t(64));
        s(MediaConfig::RtmpChunkSize, int32_t(60000));
        s(MediaConfig::RtmpWindowAckSize, int32_t(5'000'000));
        s(MediaConfig::RtmpPeerBandwidth, int32_t(5'000'000));
        s(MediaConfig::RtmpStartTcpNoDelay, true);
        s(MediaConfig::RtmpFcSubscribe, false);
#if defined(PROMEKI_ENABLE_TLS)
        s(MediaConfig::RtmpTlsVerify, true);
#endif
        return specs;
}

Error RtmpFactory::urlToConfig(const Url &url, Config *outConfig) const {
        if (outConfig == nullptr) return Error::InvalidArgument;
        const String scheme = url.scheme().toLower();
        if (scheme != "rtmp" && scheme != "rtmps") {
                return Error::InvalidArgument;
        }
        outConfig->set(MediaConfig::Type, String("Rtmp"));
        outConfig->set(MediaConfig::RtmpUrl, url);
        return Error::Ok;
}

MediaIO *RtmpFactory::create(const Config &config, ObjectBase *parent) const {
        auto *io = new RtmpMediaIO(parent);
        io->setConfig(config);
        return io;
}

// ----------------------------------------------------------------------------
// Ctor / dtor
// ----------------------------------------------------------------------------

Atomic<uint64_t> RtmpMediaIO::_nextObjectId{0};

RtmpMediaIO::RtmpMediaIO(ObjectBase *parent) : DedicatedThreadMediaIO(parent) {
        _objectId = _nextObjectId.fetchAndAdd(uint64_t(1)) + uint64_t(1);
}

RtmpMediaIO::~RtmpMediaIO() {
        if (isOpen()) (void)close().wait();
        resetAll();
}

// ----------------------------------------------------------------------------
// Internal helpers
// ----------------------------------------------------------------------------

void RtmpMediaIO::resetAll() {
        // Stop strand-side blocking pops first so reader-side close
        // doesn't deadlock.
        _readerQueue.cancelWaiters();

        // Stop the packetizer (sink) and depacketizer (source) workers
        // before the client; their inner loops touch _client via
        // sendVideo / takeVideo, so the client must outlive them.
        if (_packetizer.isValid()) {
                (void)_packetizer->stop();
                _packetizer.reset();
        }
        if (_depacketizer.isValid()) {
                (void)_depacketizer->stop();
                _depacketizer.reset();
        }

        if (_client.isValid()) {
                // Detach our disconnect slot before close() runs so
                // the synthetic Ok-disconnect it emits on shutdown
                // doesn't queue an onClientDisconnected callable that
                // races a subsequent open() and re-arms
                // _clientDisconnected on the new client.
                _client->disconnectedSignal.disconnectFromObject(this);
                (void)_client->close();
                _client.reset();
        }

        _readerQueue.clear();
        _readCancelled.setValue(false);
        _clientDisconnected.setValue(false);
        _disconnectErrorCode.setValue(0);
        _imageDesc = ImageDesc();
        _audioDesc = AudioDesc();
        _url = Url();
        _streamKey = String();
        _readerMode = false;
        _frameCount = FrameCount(0);
        _framesSent = FrameCount(0);
        _readerFramesReceived.setValue(0);
        _videoFramesDroppedPreIdr.setValue(0);
        _sendQueueOverflows.setValue(0);
        _connectDurationMs = 0;
        _handshakeDurationMs = 0;
        _videoPacingMode = RtmpVideoPacing::Internal;
        _videoPaceGate.setClock(Clock::Ptr());
        _videoPaceGate.setPeriod(Duration());
        _frameRate = FrameRate();
        _paceSkipThresholdMs = 0;
        _paceReanchorThresholdMs = 0;
        _paceClockIsExternal = false;
}

void RtmpMediaIO::onClientDisconnected(Error reason) {
        // Coalesce repeat firings — RtmpClient::close on a still-open
        // client emits a second disconnect with Error::Ok after a
        // peer-driven disconnect already latched the reason.
        if (_clientDisconnected.value()) return;
        _disconnectErrorCode.setValue(static_cast<int>(reason.code()));
        _clientDisconnected.setValue(true);
        // Surface as a generic MediaIO error so any listener observing
        // errorOccurredSignal sees the failure too.  The pipeline's
        // primary error cascade still comes through the Write/Read
        // command results below.
        if (reason.isError()) {
                promekiWarn("RtmpMediaIO: RTMP client disconnected: %s",
                            reason.desc().cstr());
                errorOccurredSignal.emit(reason);
        }
}

RtmpConnectOptions RtmpMediaIO::buildConnectOptions(const MediaIO::Config &cfg) const {
        RtmpConnectOptions opts;
        const String       app = cfg.getAs<String>(MediaConfig::RtmpAppName, String());
        if (!app.isEmpty()) opts.app = app;
        opts.flashVer = cfg.getAs<String>(MediaConfig::RtmpFlashVer, opts.flashVer);
        opts.tcUrl = cfg.getAs<String>(MediaConfig::RtmpTcUrl, opts.tcUrl);
        opts.pageUrl = cfg.getAs<String>(MediaConfig::RtmpPageUrl, opts.pageUrl);
        opts.swfUrl = cfg.getAs<String>(MediaConfig::RtmpSwfUrl, opts.swfUrl);
        return opts;
}

bool RtmpMediaIO::hasVideoEssence(const Frame &frame) {
        VideoPayload::PtrList vpl = frame.videoPayloads();
        for (const VideoPayload::Ptr &vp : vpl) {
                if (vp.isValid()) return true;
        }
        return false;
}

bool RtmpMediaIO::hasAudioEssence(const Frame &frame) {
        AudioPayload::PtrList apl = frame.audioPayloads();
        for (const AudioPayload::Ptr &ap : apl) {
                if (ap.isValid()) return true;
        }
        return false;
}

// ----------------------------------------------------------------------------
// executeCmd handlers
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// proposeInput
// ----------------------------------------------------------------------------

Error RtmpMediaIO::proposeInput(const MediaDesc &offered, MediaDesc *preferred) const {
        if (preferred == nullptr) return Error::Invalid;

        // Reader-mode sources are queried via proposeOutput, not
        // proposeInput.  If a caller does ask, fall back to the base
        // class behaviour (transparent passthrough) so we don't fight
        // the planner with a fabricated shape.
        const Enum modeEnum = config().get(MediaConfig::OpenMode).asEnum(MediaIOOpenMode::Type);
        const bool isWrite = modeEnum.value() == MediaIOOpenMode::Write.value();
        if (!isWrite) {
                *preferred = offered;
                return Error::Ok;
        }

        MediaDesc out = offered;

        // Video: rewrite the first image's PixelFormat to the compressed
        // representative of the configured RtmpVideoCodec when the
        // offered shape isn't already compressed.  Picks the first
        // entry in the codec's compressedPixelFormats list — every
        // built-in codec has at least one — so VideoEncoderFactory::bridge
        // sees an uncompressed→compressed gap and splices in an encoder.
        if (!out.imageList().isEmpty()) {
                const PixelFormat &pd = out.imageList()[0].pixelFormat();
                if (pd.isValid() && !pd.isCompressed()) {
                        VideoCodec vc =
                                config().getAs<VideoCodec>(MediaConfig::RtmpVideoCodec,
                                                           VideoCodec(VideoCodec::H264));
                        if (vc.isValid()) {
                                List<PixelFormat> compressed = vc.compressedPixelFormats();
                                if (!compressed.isEmpty()) {
                                        ImageDesc::List &imgs = out.imageList();
                                        for (size_t i = 0; i < imgs.size(); ++i) {
                                                imgs[i].setPixelFormat(compressed[0]);
                                        }
                                }
                        }
                }
        }

        // Audio: rewrite the first audio descriptor's AudioFormat to the
        // compressed representative of the configured RtmpAudioCodec
        // when the offered shape isn't already compressed.  Walks the
        // registered AudioFormat IDs to find the format whose
        // @c audioCodec() reverses to the configured codec — every
        // well-known compressed codec has exactly one such format.
        if (!out.audioList().isEmpty()) {
                const AudioFormat &af = out.audioList()[0].format();
                if (af.isValid() && !af.isCompressed()) {
                        AudioCodec ac =
                                config().getAs<AudioCodec>(MediaConfig::RtmpAudioCodec,
                                                           AudioCodec(AudioCodec::AAC));
                        if (ac.isValid()) {
                                AudioFormat compressedFmt;
                                for (AudioFormat::ID fid : AudioFormat::registeredIDs()) {
                                        AudioFormat candidate(fid);
                                        if (!candidate.isCompressed()) continue;
                                        if (candidate.audioCodec() == ac) {
                                                compressedFmt = candidate;
                                                break;
                                        }
                                }
                                if (compressedFmt.isValid()) {
                                        AudioDesc::List &auds = out.audioList();
                                        for (size_t i = 0; i < auds.size(); ++i) {
                                                auds[i].setFormat(compressedFmt);
                                        }
                                }
                        }
                }
        }

        *preferred = out;
        return Error::Ok;
}

Error RtmpMediaIO::executeCmd(MediaIOCommandOpen &cmd) {
        const MediaIO::Config &cfg = cmd.config;

        Enum modeEnum = cfg.get(MediaConfig::OpenMode).asEnum(MediaIOOpenMode::Type);
        const bool isWrite = modeEnum.value() == MediaIOOpenMode::Write.value();
        _readerMode = !isWrite;
        _readCancelled.setValue(false);

        // URL is required.
        Variant urlVar = cfg.get(MediaConfig::RtmpUrl);
        if (urlVar.type() != DataTypeUrl) {
                promekiErr("RtmpMediaIO: RtmpUrl config key is required");
                return Error::InvalidArgument;
        }
        _url = urlVar.get<Url>();
        if (!_url.isValid()) {
                promekiErr("RtmpMediaIO: RtmpUrl is invalid");
                return Error::InvalidArgument;
        }

        _streamKey = cfg.getAs<String>(MediaConfig::RtmpStreamKey, String());
        _dropUntilKeyframe = cfg.getAs<bool>(MediaConfig::RtmpDropUntilKeyframe, true);
        _repeatParameterSets = cfg.getAs<bool>(MediaConfig::RtmpRepeatParameterSets, true);
        _enhancedRtmp = cfg.getAs<bool>(MediaConfig::RtmpEnhancedRtmp, true);
        _emitAnnexB = cfg.getAs<bool>(MediaConfig::RtmpEmitAnnexB, false);
        _dataEnabled = cfg.getAs<bool>(MediaConfig::RtmpDataEnabled, true);
        _sendQueueDepth = cfg.getAs<int>(MediaConfig::RtmpSendQueueDepth, 64);
        _readQueueDepth = cfg.getAs<int>(MediaConfig::RtmpReadQueueDepth, 64);
        if (_sendQueueDepth < 2)   _sendQueueDepth = 2;
        if (_sendQueueDepth > 1024) _sendQueueDepth = 1024;
        if (_readQueueDepth < 2)   _readQueueDepth = 2;
        if (_readQueueDepth > 1024) _readQueueDepth = 1024;
        _readerQueue.setMaxSize(static_cast<size_t>(_readQueueDepth));

        // Cache the pre-open descriptors so the packetizer can build
        // sequence headers later.
        if (!cmd.pendingMediaDesc.imageList().isEmpty()) {
                _imageDesc = cmd.pendingMediaDesc.imageList()[0];
        }
        if (!cmd.pendingMediaDesc.audioList().isEmpty()) {
                _audioDesc = cmd.pendingMediaDesc.audioList()[0];
        }

        // Resolve the frame rate from the pending MediaDesc, falling
        // back to the MediaConfig::FrameRate key, then to 29.97 fps.
        // Used to seed the pacing gate's period and skip/reanchor
        // thresholds.
        _frameRate = cmd.pendingMediaDesc.frameRate();
        if (!_frameRate.isValid()) {
                _frameRate = cfg.getAs<FrameRate>(MediaConfig::FrameRate,
                                                  FrameRate(FrameRate::FPS_29_97));
        }

        // Pacing mode + thresholds (sink-mode only; readers ignore
        // the gate).  armVideoPaceGate() binds an Internal wall clock
        // when configured; External waits for executeCmd(SetClock).
        Enum pacingEnum = cfg.get(MediaConfig::RtmpVideoPacing)
                                  .asEnum(RtmpVideoPacing::Type);
        _videoPacingMode = RtmpVideoPacing(pacingEnum.value());
        _paceSkipThresholdMs = cfg.getAs<int>(MediaConfig::RtmpPaceSkipThresholdMs, 0);
        _paceReanchorThresholdMs =
                cfg.getAs<int>(MediaConfig::RtmpPaceReanchorThresholdMs, 0);
        if (!_readerMode) armVideoPaceGate();

        _client.reset(new RtmpClient(this));

        // Latch peer-disconnect / socket-failure events.  The slot
        // marshals to the strand event loop via the @c , this binding;
        // the latched flag stops the packetizer / depacketizer from
        // spewing per-frame send failures and causes subsequent
        // executeCmd(Write) / executeCmd(Read) calls to fail with the
        // captured reason so the pipeline's error cascade tears the
        // stage down.
        _client->disconnectedSignal.connect(
                [this](Error err) { onClientDisconnected(err); }, this);

#if defined(PROMEKI_ENABLE_TLS)
        // If the caller wired an SslContext through the MediaConfig
        // database, hand it to the underlying client.  The Variant
        // type-tag guards against a populated-but-empty slot; the
        // shared SslContext itself is always handle-valid post-
        // construction so we don't filter further here.
        Variant tlsVar = cfg.get(MediaConfig::RtmpTlsContext);
        if (tlsVar.type() == DataTypeSslContext) {
                _client->setSslContext(tlsVar.get<SslContext>());
        }
#endif

        RtmpConnectOptions opts = buildConnectOptions(cfg);
        const unsigned int connectTimeoutMs =
                static_cast<unsigned int>(cfg.getAs<int>(MediaConfig::RtmpConnectTimeoutMs, kOpenTimeoutMs));

        const TimeStamp openStart = TimeStamp::now();
        Error err = _client->open(_url, opts, connectTimeoutMs);
        if (err.isError()) {
                promekiErr("RtmpMediaIO: open(%s) failed: %s",
                           _url.toString().cstr(), err.desc().cstr());
                resetAll();
                return err;
        }
        _connectDurationMs = openStart.elapsedMilliseconds();
        _handshakeDurationMs = _connectDurationMs;  // Phase 5 v1: not yet split out.

        if (_readerMode) {
                const unsigned int playTimeoutMs =
                        static_cast<unsigned int>(cfg.getAs<int>(MediaConfig::RtmpCommandTimeoutMs, kPlayTimeoutMs));
                const bool useFcSubscribe = cfg.getAs<bool>(MediaConfig::RtmpFcSubscribe, false);
                err = _client->play(_streamKey, playTimeoutMs, useFcSubscribe);
                if (err.isError()) {
                        promekiErr("RtmpMediaIO: play failed: %s", err.desc().cstr());
                        resetAll();
                        return err;
                }

                // Add a single source port so writeFrame upstream sinks
                // know where to bind.  The MediaDesc is unknown until
                // the first SequenceHeader lands; we expose a generic
                // H.264 + 48 kHz AAC placeholder that downstream
                // planners can reconcile when the real descriptor
                // surfaces.
                MediaDesc rxDesc;
                rxDesc.imageList().pushToBack(ImageDesc(Size2Du32(), PixelFormat(PixelFormat::H264)));
                rxDesc.audioList().pushToBack(AudioDesc(AudioFormat(AudioFormat::AAC), 48000.0f, 2));
                MediaIOPortGroup *group = addPortGroup(String("rtmp-source"));
                if (group != nullptr) {
                        addSource(group, rxDesc, String("source"));
                }
                cmd.mediaDesc = rxDesc;

                _depacketizer.reset(new DepacketizerThread(this));
                _depacketizer->start();
        } else {
                const unsigned int publishTimeoutMs =
                        static_cast<unsigned int>(cfg.getAs<int>(MediaConfig::RtmpCommandTimeoutMs, kPublishTimeoutMs));
                err = _client->publish(_streamKey, String("live"), publishTimeoutMs);
                if (err.isError()) {
                        promekiErr("RtmpMediaIO: publish failed: %s", err.desc().cstr());
                        resetAll();
                        return err;
                }

                MediaDesc txDesc;
                if (_imageDesc.isValid()) {
                        txDesc.imageList().pushToBack(_imageDesc);
                } else {
                        txDesc.imageList().pushToBack(ImageDesc(Size2Du32(), PixelFormat(PixelFormat::H264)));
                }
                if (_audioDesc.isValid()) {
                        txDesc.audioList().pushToBack(_audioDesc);
                } else {
                        txDesc.audioList().pushToBack(AudioDesc(AudioFormat(AudioFormat::AAC), 48000.0f, 2));
                }
                MediaIOPortGroup *group = addPortGroup(String("rtmp-sink"));
                if (group != nullptr) {
                        addSink(group, txDesc, String("sink"));
                }
                cmd.mediaDesc = txDesc;

                _packetizer.reset(new PacketizerThread(this));
                _packetizer->start();
        }

        cmd.frameCount = FrameCount::infinity();
        return Error::Ok;
}

Error RtmpMediaIO::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        resetAll();
        return Error::Ok;
}

Error RtmpMediaIO::executeCmd(MediaIOCommandRead &cmd) {
        if (!_readerMode) return Error::NotSupported;
        for (;;) {
                Result<Frame> r = _readerQueue.pop(kReadPollMs);
                if (r.second().isOk()) {
                        cmd.frame = r.first();
                        _frameCount += 1;
                        cmd.currentFrame = toFrameNumber(_frameCount);
                        return Error::Ok;
                }
                if (r.second() == Error::Cancelled) {
                        return Error::Cancelled;
                }
                if (r.second() != Error::Timeout) {
                        return r.second();
                }
                if (_readCancelled.value()) {
                        return Error::Cancelled;
                }
                if (_clientDisconnected.value()) {
                        // Drain any frames the depacketizer queued
                        // before the disconnect latched (handled above
                        // via the .isOk() path); once empty, surface
                        // the captured reason — or @c BrokenPipe as a
                        // sentinel when the disconnect was clean.
                        Error::Code code = static_cast<Error::Code>(
                                _disconnectErrorCode.value());
                        return code == Error::Ok ? Error(Error::BrokenPipe) : Error(code);
                }
        }
}

Error RtmpMediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        if (_readerMode) return Error::NotSupported;
        if (!cmd.frame.isValid()) return Error::InvalidArgument;
        if (!_packetizer.isValid() || !_client.isValid()) return Error::Invalid;

        // The peer went away mid-stream; surface the captured reason
        // so MediaIOPortConnection's writeError signal cascades the
        // failure to the pipeline (which then initiates a teardown).
        if (_clientDisconnected.value()) {
                Error::Code code = static_cast<Error::Code>(
                        _disconnectErrorCode.value());
                return code == Error::Ok ? Error(Error::BrokenPipe) : Error(code);
        }

        if (!hasVideoEssence(cmd.frame) && !hasAudioEssence(cmd.frame)) {
                // Nothing to do; treat as a silent no-op so an empty
                // sentinel frame upstream doesn't generate noise.
                _frameCount += 1;
                _framesSent += 1;
                cmd.currentFrame = toFrameNumber(_frameCount);
                cmd.frameCount = FrameCount::infinity();
                return Error::Ok;
        }

        // Strand-side video pacing.  The gate is a no-op when no clock
        // is bound (External-without-setClock or None mode), so it has
        // zero cost when pacing is disabled.  Skip verdicts drop the
        // frame to bound lag — same shape as RtpMediaIO::paceVideoFrame.
        if (hasVideoEssence(cmd.frame)) {
                if (!paceVideoFrame()) {
                        _frameCount += 1;
                        _framesSent += 1;
                        cmd.currentFrame = toFrameNumber(_frameCount);
                        cmd.frameCount = FrameCount::infinity();
                        return Error::Ok;
                }
        }

        Error err = _packetizer->pushFrame(cmd.frame);
        if (err.isError() && err != Error::TryAgain) {
                if (err == Error::Cancelled) {
                        return err;
                }
                promekiWarn("RtmpMediaIO: pushFrame failed: %s", err.desc().cstr());
                return err;
        }
        if (err == Error::TryAgain) {
                _sendQueueOverflows.fetchAndAdd(1);
                return err;
        }

        _frameCount += 1;
        _framesSent += 1;
        cmd.currentFrame = toFrameNumber(_frameCount);
        cmd.frameCount = FrameCount::infinity();
        return Error::Ok;
}

Error RtmpMediaIO::executeCmd(MediaIOCommandStats &cmd) {
        if (_client.isValid()) {
                cmd.stats.set(StatsBytesSent, _client->bytesSent());
                cmd.stats.set(StatsBytesReceived, _client->bytesReceived());
                cmd.stats.set(StatsVideoMessagesSent, _client->videoMessagesSent());
                cmd.stats.set(StatsAudioMessagesSent, _client->audioMessagesSent());
                cmd.stats.set(StatsVideoMessagesReceived, _client->videoMessagesReceived());
                cmd.stats.set(StatsAudioMessagesReceived, _client->audioMessagesReceived());
        }
        cmd.stats.set(StatsFramesSent, static_cast<int64_t>(_framesSent.value()));
        cmd.stats.set(StatsFramesReceived, _readerFramesReceived.value());
        cmd.stats.set(StatsSendQueueDepth,
                      _packetizer.isValid() ? _packetizer->queueDepth() : int64_t(0));
        cmd.stats.set(StatsReadQueueDepth, static_cast<int64_t>(_readerQueue.size()));
        cmd.stats.set(StatsSendQueueOverflows, _sendQueueOverflows.value());
        cmd.stats.set(StatsVideoFramesDroppedPreIdr, _videoFramesDroppedPreIdr.value());
        cmd.stats.set(StatsConnectDurationMs, _connectDurationMs);
        cmd.stats.set(StatsHandshakeDurationMs, _handshakeDurationMs);
        cmd.stats.set(StatsPacingTicksOnTime, _videoPaceGate.ticksOnTime());
        cmd.stats.set(StatsPacingTicksLate, _videoPaceGate.ticksLate());
        cmd.stats.set(StatsPacingTicksSkipped, _videoPaceGate.ticksSkipped());
        cmd.stats.set(StatsPacingReanchors, _videoPaceGate.reanchors());
        cmd.stats.set(StatsPacingClockKind, paceClockKind());
        return Error::Ok;
}

Error RtmpMediaIO::executeCmd(MediaIOCommandSetClock &cmd) {
        // RX timing is driven by network arrival; the user can't
        // meaningfully replace it.
        if (_readerMode) return Error::NotSupported;

        if (cmd.clock.isValid()) {
                // Honor the externally-supplied clock regardless of
                // configured mode — capture-card upstreams typically
                // arrive after Open via the planner forwarding the
                // port-group clock.
                _videoPaceGate.setClock(cmd.clock);
                _paceClockIsExternal = true;
                if (_frameRate.isValid()) {
                        _videoPaceGate.setPeriod(_frameRate.frameDuration());
                }
        } else {
                // Detach the external clock.  For Internal mode, re-arm
                // a fresh wall clock so pacing continues uninterrupted;
                // for External / None modes the gate stays unbound.
                _paceClockIsExternal = false;
                if (_videoPacingMode.value() == RtmpVideoPacing::Internal.value()) {
                        armVideoPaceGate();
                } else {
                        _videoPaceGate.setClock(Clock::Ptr());
                }
        }
        return Error::Ok;
}

void RtmpMediaIO::armVideoPaceGate() {
        _paceClockIsExternal = false;
        if (_videoPacingMode.value() == RtmpVideoPacing::Internal.value()) {
                _videoPaceGate.setClock(Clock::Ptr::takeOwnership(new WallClock()));
        } else {
                _videoPaceGate.setClock(Clock::Ptr());
        }
        if (_frameRate.isValid()) {
                _videoPaceGate.setPeriod(_frameRate.frameDuration());
                if (_paceSkipThresholdMs > 0) {
                        _videoPaceGate.setSkipThreshold(
                                Duration::fromMilliseconds(_paceSkipThresholdMs));
                }
                if (_paceReanchorThresholdMs > 0) {
                        _videoPaceGate.setReanchorThreshold(
                                Duration::fromMilliseconds(_paceReanchorThresholdMs));
                }
        }
}

bool RtmpMediaIO::paceVideoFrame() {
        if (!_videoPaceGate.hasClock()) return true;
        if (!_frameRate.isValid()) return true;

        // Refresh the period each call so a mid-stream frame-rate
        // change takes effect immediately (rare, but legal — matches
        // RtpMediaIO behavior).
        _videoPaceGate.setPeriod(_frameRate.frameDuration());

        PacingResult pr = _videoPaceGate.wait();
        if (pr.error.isError()) {
                promekiErr("RtmpMediaIO: video pacing clock failure: %s",
                           pr.error.name().cstr());
                return true;
        }
        switch (pr.verdict) {
                case PacingVerdict::Skip:
                        return false;
                case PacingVerdict::Reanchor:
                        promekiWarn("RtmpMediaIO: video pacing re-anchored after %s lag",
                                    pr.slack.toString().cstr());
                        return true;
                case PacingVerdict::OnTime:
                case PacingVerdict::Late:
                        return true;
        }
        return true;
}

String RtmpMediaIO::paceClockKind() const {
        if (!_videoPaceGate.hasClock()) return String("none");
        return _paceClockIsExternal ? String("external") : String("internal");
}

void RtmpMediaIO::cancelBlockingWork() {
        _readCancelled.setValue(true);
        _readerQueue.cancelWaiters();
}

PROMEKI_NAMESPACE_END
