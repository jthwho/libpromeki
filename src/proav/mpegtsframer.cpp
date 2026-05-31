/**
 * @file      mpegtsframer.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/mpegtsframer.h>

#include <promeki/aacbitstream.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/audiopayload.h>
#include <promeki/bufferview.h>
#include <promeki/clockdomain.h>
#include <promeki/compressedaudiopayload.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/error.h>
#include <promeki/h264bitstream.h>
#include <promeki/hevcbitstream.h>
#include <promeki/imagedesc.h>
#include <promeki/jpegxsbitstream.h>
#include <promeki/logger.h>
#include <promeki/mediadesc.h>
#include <promeki/mediatimestamp.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/pixelformat.h>
#include <promeki/rational.h>
#include <promeki/size2d.h>
#include <promeki/smpte302m.h>
#include <promeki/timestamp.h>
#include <promeki/videopayload.h>

#include <cstring>

PROMEKI_NAMESPACE_BEGIN

namespace {

        constexpr int kPtsHz = 90'000;

        // Map a compressed @ref PixelFormat to its on-wire MPEG-TS
        // @c stream_type and, when relevant, its
        // @c registration_descriptor @c format_identifier.
        //
        // AV1 and JPEG XS need a registration descriptor in the PMT
        // entry to be decodable by ffmpeg / MediaMTX / tsduck; the
        // helper bundles both pieces of information so callers don't
        // accidentally emit a stream_type without its accompanying
        // descriptor.
        struct VideoMapping {
                        MpegTs::StreamType streamType = MpegTs::StreamTypeReserved;
                        uint32_t           registrationFormat = 0; ///< 0 = no descriptor needed.
        };

        VideoMapping videoMapping(const PixelFormat &pf) {
                if (!pf.isValid()) return {};
                switch (pf.id()) {
                        case PixelFormat::H264: return {MpegTs::StreamTypeH264, 0};
                        case PixelFormat::HEVC: return {MpegTs::StreamTypeHevc, 0};
                        case PixelFormat::AV1: return {MpegTs::StreamTypePrivatePes, MpegTs::RegFormatAv1};
                        case PixelFormat::JPEG_XS_YUV8_422_Rec709:
                        case PixelFormat::JPEG_XS_YUV10_422_Rec709:
                        case PixelFormat::JPEG_XS_YUV12_422_Rec709:
                        case PixelFormat::JPEG_XS_YUV8_420_Rec709:
                        case PixelFormat::JPEG_XS_YUV10_420_Rec709:
                        case PixelFormat::JPEG_XS_YUV12_420_Rec709:
                        case PixelFormat::JPEG_XS_RGB8_sRGB:
                                return {MpegTs::StreamTypeJpegXs, MpegTs::RegFormatJpegXs};
                        default: return {};
                }
        }

        struct AudioMapping {
                        MpegTs::StreamType streamType = MpegTs::StreamTypeReserved;
                        uint32_t           registrationFormat = 0;
        };

        AudioMapping audioMapping(const AudioFormat &af, MpegTsFramer::AacFraming framing) {
                if (!af.isValid()) return {};
                switch (af.id()) {
                        case AudioFormat::AAC:
                                return {framing == MpegTsFramer::AacFraming::Latm ? MpegTs::StreamTypeAacLatm
                                                                                  : MpegTs::StreamTypeAacAdts,
                                        0};
                        case AudioFormat::MP3: return {MpegTs::StreamTypeMpeg1Audio, 0};
                        case AudioFormat::AC3: return {MpegTs::StreamTypeAc3, 0};
                        case AudioFormat::Opus: return {MpegTs::StreamTypePrivatePes, MpegTs::RegFormatOpus};
                        default:
                                // SMPTE 302M handles every uncompressed
                                // PCM format the packer accepts.
                                if (Smpte302M::isFormatSupported(af)) {
                                        return {MpegTs::StreamTypePrivatePes, MpegTs::RegFormatSmpte302M};
                                }
                                return {};
                }
        }

        PixelFormat::ID pixelFormatForStreamType(MpegTs::StreamType st, uint32_t regFmt) {
                switch (st) {
                        case MpegTs::StreamTypeH264: return PixelFormat::H264;
                        case MpegTs::StreamTypeHevc: return PixelFormat::HEVC;
                        case MpegTs::StreamTypeJpegXs:
                                // 7 chroma / range variants share one stream_type;
                                // pick the most common (Rec.709 422 10-bit) as
                                // the placeholder.  The actual chroma is carried
                                // in JPEG XS's own bitstream header, which a
                                // downstream decoder will reparse anyway.
                                return PixelFormat::JPEG_XS_YUV10_422_Rec709;
                        case MpegTs::StreamTypePrivatePes:
                                if (regFmt == MpegTs::RegFormatAv1) return PixelFormat::AV1;
                                return PixelFormat::Invalid;
                        default: return PixelFormat::Invalid;
                }
        }

        AudioFormat::ID audioFormatForStreamType(MpegTs::StreamType st, uint32_t regFmt) {
                switch (st) {
                        case MpegTs::StreamTypeAacAdts:
                        case MpegTs::StreamTypeAacLatm: return AudioFormat::AAC;
                        case MpegTs::StreamTypeMpeg1Audio:
                        case MpegTs::StreamTypeMpeg2Audio: return AudioFormat::MP3;
                        case MpegTs::StreamTypeAc3: return AudioFormat::AC3;
                        case MpegTs::StreamTypePrivatePes:
                                if (regFmt == MpegTs::RegFormatOpus) return AudioFormat::Opus;
                                // 302M / BSSD demuxing is handled by the
                                // caller via Smpte302M::parse — return
                                // Invalid here so the caller routes
                                // those access units down a different
                                // path.
                                return AudioFormat::Invalid;
                        default: return AudioFormat::Invalid;
                }
        }

        // 90 kHz PES ticks → MediaTimeStamp anchored at the Synthetic
        // clock domain epoch (ticks * 1e9 / 90000 nanoseconds).
        MediaTimeStamp ptsToMediaTimeStamp(uint64_t pts90k) {
                const int64_t ns = static_cast<int64_t>((pts90k * 1'000'000'000ull) / kPtsHz);
                return MediaTimeStamp(TimeStamp(ns), ClockDomain(ClockDomain::Synthetic));
        }

} // namespace

MpegTsFramer::MpegTsFramer() : _writerFrameRate() {}
MpegTsFramer::~MpegTsFramer() = default;

MpegTsMuxer *MpegTsFramer::ensureMuxer() {
        if (_muxer.isValid()) return _muxer.get();
        _muxer.reset(new MpegTsMuxer);
        _muxer->setProgramNumber(_programNumber);
        _muxer->setPmtPid(_pmtPid);
        _muxer->setPcrPid(_videoPid);
        _muxer->setPatPmtIntervalMs(_patPmtIntervalMs);
        _muxer->setPcrIntervalMs(_pcrIntervalMs);
        _muxer->setMuxRateBps(_muxRateBps);
        return _muxer.get();
}

MpegTsDemuxer *MpegTsFramer::ensureDemuxer() {
        if (_demuxer.isValid()) return _demuxer.get();
        _demuxer.reset(new MpegTsDemuxer);
        _demuxer->setStreamCallback([this](const MpegTsDemuxer::AccessUnit &au) -> Error { return handleAccessUnit(au); });
        return _demuxer.get();
}

MpegTsMuxer *MpegTsFramer::muxer() {
        return ensureMuxer();
}

MpegTsDemuxer *MpegTsFramer::demuxer() {
        return ensureDemuxer();
}

// Concatenates @p extra (if valid + non-empty) onto @p out.  Used to
// stack a codec-specific descriptor (Opus extension / AV1 video /
// JPEG XS) after the @c registration_descriptor.
static Error appendDescriptor(Buffer &out, const Buffer &extra) {
        if (!extra.isValid() || extra.size() == 0) return Error::Ok;
        const size_t oldSize = out.size();
        const size_t newSize = oldSize + extra.size();
        Buffer       grown(newSize);
        if (!grown.isValid()) return Error::NoMem;
        grown.setSize(newSize);
        if (oldSize > 0) std::memcpy(grown.data(), out.data(), oldSize);
        std::memcpy(static_cast<uint8_t *>(grown.data()) + oldSize, extra.data(), extra.size());
        out = std::move(grown);
        return Error::Ok;
}

Error MpegTsFramer::registerVideoStream(MpegTs::StreamType st, uint32_t registrationFormat,
                                         const ImageDesc &imageDesc) {
        if (_haveVideoStream) return Error::Ok;
        MpegTsMuxer *m = ensureMuxer();
        Buffer       desc;
        if (registrationFormat != 0) {
                Error de = MpegTs::buildRegistrationDescriptor(registrationFormat, desc);
                if (de.isError()) return de;
        }
        // Codec-specific additional descriptors so the PMT entry is
        // complete enough for spec-strict consumers.
        if (registrationFormat == MpegTs::RegFormatAv1) {
                Buffer av1;
                Error  ae = MpegTs::buildAv1VideoDescriptor(av1);
                if (ae.isError()) return ae;
                Error ce = appendDescriptor(desc, av1);
                if (ce.isError()) return ce;
        } else if (registrationFormat == MpegTs::RegFormatJpegXs) {
                Buffer jxs;
                const Size2Du32 sz = imageDesc.size();
                const uint16_t  w = static_cast<uint16_t>(sz.width() > 0xFFFF ? 0 : sz.width());
                const uint16_t  h = static_cast<uint16_t>(sz.height() > 0xFFFF ? 0 : sz.height());
                uint32_t        fpsNum = 0, fpsDen = 1;
                if (_writerFrameRate.isValid()) {
                        const Rational r = _writerFrameRate.rational();
                        fpsNum = static_cast<uint32_t>(r.numerator() > 0 ? r.numerator() : 0);
                        fpsDen = static_cast<uint32_t>(r.denominator() > 0 ? r.denominator() : 1);
                }
                Error je = MpegTs::buildJpegXsVideoDescriptor(w, h, fpsNum, fpsDen, jxs);
                if (je.isError()) return je;
                Error ce = appendDescriptor(desc, jxs);
                if (ce.isError()) return ce;
        }
        BufferView dv;
        if (desc.isValid() && desc.size() > 0) dv = BufferView(desc, 0, desc.size());
        Error e = m->addStream(_videoPid, st, dv, MpegTs::StreamKind::Video);
        if (e.isError() && e != Error::Exists) return e;
        _haveVideoStream = true;
        return Error::Ok;
}

Error MpegTsFramer::registerAudioStream(MpegTs::StreamType st, uint32_t registrationFormat,
                                         const AudioDesc &audioDesc) {
        if (_haveAudioStream) return Error::Ok;
        MpegTsMuxer *m = ensureMuxer();
        Buffer       desc;
        if (registrationFormat != 0) {
                Error de = MpegTs::buildRegistrationDescriptor(registrationFormat, desc);
                if (de.isError()) return de;
        }
        if (registrationFormat == MpegTs::RegFormatOpus) {
                const unsigned ch = audioDesc.channels() == 0 ? 2u : audioDesc.channels();
                Buffer         opus;
                Error          oe = MpegTs::buildOpusExtensionDescriptor(ch, opus);
                if (oe.isError()) return oe;
                Error ce = appendDescriptor(desc, opus);
                if (ce.isError()) return ce;
        }
        BufferView dv;
        if (desc.isValid() && desc.size() > 0) dv = BufferView(desc, 0, desc.size());
        Error e = m->addStream(_audioPid, st, dv, MpegTs::StreamKind::Audio);
        if (e.isError() && e != Error::Exists) return e;
        _haveAudioStream = true;
        // Audio stream (re-)registered — reset the SMPTE 302M 192-frame
        // F-bit phase counter so the first AES3 frame after this point
        // is correctly marked as the block boundary.
        _writer302MBlockPhase = 0;
        return Error::Ok;
}

Error MpegTsFramer::configureStreams(const MediaDesc &desc) {
        for (const ImageDesc &id : desc.imageList()) {
                const PixelFormat &pf = id.pixelFormat();
                if (!pf.isValid() || !pf.isCompressed()) continue;
                const VideoMapping vm = videoMapping(pf);
                if (vm.streamType == MpegTs::StreamTypeReserved) {
                        promekiWarn("MpegTsFramer: pixel format '%s' has no MPEG-TS stream_type mapping; skipping",
                                    pf.name().cstr());
                        continue;
                }
                Error e = registerVideoStream(vm.streamType, vm.registrationFormat, id);
                if (e.isError()) return e;
                break; // single video stream in v1
        }
        for (const AudioDesc &ad : desc.audioList()) {
                const AudioFormat &af = ad.format();
                if (!af.isValid()) continue;
                // Accept either compressed or 302M-eligible uncompressed
                // PCM here — both map to a valid PMT entry.
                const AudioMapping am = audioMapping(af, _aacFraming);
                if (am.streamType == MpegTs::StreamTypeReserved) {
                        if (af.isCompressed()) {
                                promekiWarn("MpegTsFramer: audio format '%s' has no MPEG-TS stream_type mapping; skipping",
                                            af.name().cstr());
                        }
                        continue;
                }
                Error e = registerAudioStream(am.streamType, am.registrationFormat, ad);
                if (e.isError()) return e;
                break; // single audio stream in v1
        }
        return Error::Ok;
}

void MpegTsFramer::forcePatPmt() {
        if (_muxer.isValid()) _muxer->forcePatPmt();
}

Error MpegTsFramer::markNextAccessUnitDiscontinuous(uint16_t pid) {
        if (!_muxer.isValid()) return Error::IdNotFound;
        return _muxer->markNextAccessUnitDiscontinuous(pid);
}

Error MpegTsFramer::writeFrame(const Frame &frame, const EmitCallback &emit) {
        if (!frame.isValid()) return Error::InvalidArgument;
        if (!emit) return Error::InvalidArgument;
        MpegTsMuxer *m = ensureMuxer();

        // Compute the video PTS for this frame from the configured
        // frame rate.  ticks_per_frame_90k = 90000 * den / num.
        uint64_t videoPts = 0;
        if (_writerFrameRate.isValid()) {
                const Rational fr = _writerFrameRate.rational();
                const uint64_t num = static_cast<uint64_t>(fr.numerator());
                const uint64_t den = static_cast<uint64_t>(fr.denominator());
                if (num > 0) {
                        videoPts = (static_cast<uint64_t>(kPtsHz) * den * _writerFrameIndex) / num;
                }
        }

        const VideoPayload::PtrList vps = frame.videoPayloads();
        const AudioPayload::PtrList aps = frame.audioPayloads();


        // Video — first compressed payload only in v1.  Audio encoders
        // emit audio-only "echo" Frames when the AAC frame size doesn't
        // divide the input's per-frame PCM count (1.56 AAC frames per
        // video frame at 29.97 fps / 48 kHz), so the framer sees a
        // sprinkling of Frames with no compressed video payload.
        // _writerFrameIndex must only advance when a video AU is
        // actually written or the on-wire video PTS sequence gets
        // gaps that the receiver's DTS-vs-PCR check reads as a broken
        // stream.
        bool videoEmitted = false;
        for (const VideoPayload::Ptr &vp : vps) {
                if (!vp.isValid()) continue;
                const auto *cvp = vp->as<CompressedVideoPayload>();
                if (cvp == nullptr) continue;
                if (!_haveVideoStream) {
                        const VideoMapping vm = videoMapping(cvp->desc().pixelFormat());
                        Error e = registerVideoStream(vm.streamType, vm.registrationFormat, cvp->desc());
                        if (e.isError()) return e;
                }
                size_t total = 0;
                for (size_t i = 0; i < cvp->planeCount(); ++i) total += cvp->plane(i).size();
                if (total == 0) continue;
                Buffer scratch(total);
                if (!scratch.isValid()) return Error::NoMem;
                scratch.setSize(total);
                size_t off = 0;
                for (size_t i = 0; i < cvp->planeCount(); ++i) {
                        auto pv = cvp->plane(i);
                        if (pv.size() == 0) continue;
                        std::memcpy(static_cast<uint8_t *>(scratch.data()) + off, pv.data(), pv.size());
                        off += pv.size();
                }
                const bool isKey = cvp->isKeyframe();
                Error      e = m->writeAccessUnit(_videoPid, BufferView(scratch, 0, total), videoPts, videoPts,
                                                  isKey, emit);
                if (e.isError()) return e;
                ++_writerAccessUnitsEmitted;
                videoEmitted = true;
                break;
        }

        // Audio — every audio payload becomes its own AU.  Compressed
        // payloads (AAC, Opus, MP3, AC-3) go through the codec-specific
        // wrap path; PCM payloads (PcmAudioPayload) are packed into
        // SMPTE 302M private-PES bytes here.
        for (const AudioPayload::Ptr &ap : aps) {
                if (!ap.isValid()) continue;
                const auto *cap = ap->as<CompressedAudioPayload>();
                const auto *pap = ap->as<PcmAudioPayload>();
                if (cap == nullptr && pap == nullptr) continue;

                const AudioDesc audDesc = cap != nullptr ? cap->desc() : pap->desc();
                const size_t    audSampleCount = cap != nullptr ? cap->sampleCount() : pap->sampleCount();

                if (!_haveAudioStream) {
                        const AudioMapping am = audioMapping(audDesc.format(), _aacFraming);
                        Error e = registerAudioStream(am.streamType, am.registrationFormat, audDesc);
                        if (e.isError()) return e;
                }

                Buffer framed;
                size_t framedSize = 0;
                if (cap != nullptr) {
                        // Compressed payload — concatenate planes into
                        // a contiguous AU buffer.
                        size_t total = 0;
                        for (size_t i = 0; i < cap->planeCount(); ++i) total += cap->plane(i).size();
                        if (total == 0) continue;
                        Buffer scratch(total);
                        if (!scratch.isValid()) return Error::NoMem;
                        scratch.setSize(total);
                        size_t off = 0;
                        for (size_t i = 0; i < cap->planeCount(); ++i) {
                                auto pv = cap->plane(i);
                                if (pv.size() == 0) continue;
                                std::memcpy(static_cast<uint8_t *>(scratch.data()) + off, pv.data(), pv.size());
                                off += pv.size();
                        }
                        framed = scratch;

                        // For stream_type 0x0F (AAC ADTS) the bytes
                        // inside the PES MUST start with a 0xFFF
                        // syncword.  Library encoders (fdk-aac
                        // TT_MP4_RAW path) emit raw AAC access units
                        // — wrap them in a 7-byte ADTS header here.
                        // Already-ADTS bytes pass through unchanged.
                        // LATM (stream_type 0x11) requires the
                        // caller's encoder to emit LATM-framed bytes
                        // directly.
                        if (_aacFraming == AacFraming::Adts &&
                            audDesc.format().id() == AudioFormat::AAC) {
                                BufferView v(scratch, 0, total);
                                if (!AdtsParser::isAdts(v)) {
                                        AacDecoderConfig cfg = AacDecoderConfig::fromAudioDesc(audDesc);
                                        Buffer           wrapped;
                                        Error wrapErr = AdtsParser::wrapFrame(cfg, v, wrapped);
                                        if (wrapErr.isError()) return wrapErr;
                                        framed = wrapped;
                                }
                        }
                        framedSize = framed.size();
                } else {
                        // Uncompressed PCM payload — pack into a
                        // SMPTE 302M PES payload.  302M only accepts
                        // contiguous interleaved bytes, so the
                        // payload's single plane must already hold
                        // sampleCount * channels * bytesPerSample
                        // bytes in PCMI_S* layout.
                        if (pap->planeCount() != 1) {
                                promekiWarnThrottled(1000,
                                                     "MpegTsFramer: 302M packing requires a single-plane "
                                                     "PCM payload (planar PCM is not supported)");
                                continue;
                        }
                        auto plane = pap->plane(0);
                        if (plane.size() == 0 || audSampleCount == 0) continue;
                        Error packErr = Smpte302M::pack(plane.data(), audDesc, audSampleCount,
                                                       _writer302MBlockPhase, /*firstChannelId=*/0,
                                                       framed);
                        if (packErr.isError()) {
                                promekiWarnThrottled(1000,
                                                     "MpegTsFramer: SMPTE 302M pack failed: %s "
                                                     "(format=%s rate=%g ch=%u samples=%zu)",
                                                     packErr.name().cstr(),
                                                     audDesc.format().name().cstr(),
                                                     static_cast<double>(audDesc.sampleRate()),
                                                     audDesc.channels(),
                                                     audSampleCount);
                                return packErr;
                        }
                        framedSize = framed.size();
                }

                const uint64_t pts = _writerAudioPts90k;
                if (audDesc.sampleRate() > 0.0f && audSampleCount > 0) {
                        _writerAudioPts90k += static_cast<uint64_t>(
                                (static_cast<double>(audSampleCount) * kPtsHz) /
                                static_cast<double>(audDesc.sampleRate()));
                }
                Error e = m->writeAccessUnit(_audioPid, BufferView(framed, 0, framedSize), pts, pts, true, emit);
                if (e.isError()) return e;
                ++_writerAccessUnitsEmitted;
        }

        if (videoEmitted) ++_writerFrameIndex;
        return Error::Ok;
}

Error MpegTsFramer::pushBytes(const BufferView &data) {
        MpegTsDemuxer *d = ensureDemuxer();
        return d->push(data);
}

Error MpegTsFramer::flushReader() {
        if (!_demuxer.isValid()) return Error::Ok;
        return _demuxer->flush();
}

size_t MpegTsFramer::drainFrames(Frame::List &out) {
        if (_readQueue.isEmpty()) return 0;
        const size_t before = out.size();
        for (size_t i = 0; i < _readQueue.size(); ++i) {
                out.pushToBack(std::move(_readQueue[i]));
        }
        _readQueue.clear();
        return out.size() - before;
}

void MpegTsFramer::probeVideoDimensions(uint16_t pid, MpegTs::StreamType st, const BufferView &au) {
        if (_readerDimsProbed.contains(pid)) return;
        // Walk the access unit's Annex-B NALs looking for an SPS.
        // H.264 SPS = NAL type 7; HEVC SPS = NAL type 33.
        bool found = false;
        if (st == MpegTs::StreamTypeH264) {
                H264Bitstream::forEachAnnexBNal(au, [&](const H264Bitstream::NalUnit &nal) -> Error {
                        if (found) return Error::Ok;
                        const uint8_t nalType = nal.header0 & 0x1F;
                        if (nalType != 7) return Error::Ok;
                        H264Bitstream::SpsInfo info;
                        if (H264Bitstream::parseSpsResolution(nal.view, info).isOk() && info.width > 0 &&
                            info.height > 0) {
                                _readerImageDesc.insert(pid,
                                                        ImageDesc(Size2Du32(info.width, info.height),
                                                                  PixelFormat(PixelFormat::H264)));
                                found = true;
                        }
                        return Error::Ok;
                });
        } else if (st == MpegTs::StreamTypeHevc) {
                H264Bitstream::forEachAnnexBNal(au, [&](const H264Bitstream::NalUnit &nal) -> Error {
                        if (found) return Error::Ok;
                        // HEVC NAL type is bits 1-6 of header0.
                        const uint8_t nalType = (nal.header0 >> 1) & 0x3F;
                        if (nalType != 33) return Error::Ok;
                        // HevcDecoderConfig::parseSpsResolution expects
                        // the raw 2-byte HEVC NAL header + payload,
                        // which is what `nal.view` already provides
                        // (the NAL walker strips the start code, not
                        // the header).
                        HevcDecoderConfig::SpsInfo info;
                        if (HevcDecoderConfig::parseSpsResolution(nal.view, info).isOk() && info.width > 0 &&
                            info.height > 0) {
                                _readerImageDesc.insert(pid,
                                                        ImageDesc(Size2Du32(info.width, info.height),
                                                                  PixelFormat(PixelFormat::HEVC)));
                                found = true;
                        }
                        return Error::Ok;
                });
        } else if (st == MpegTs::StreamTypeJpegXs) {
                // JPEG XS access unit is a raw ISO 21122 codestream
                // beginning with the SOC marker (0xFF10).  Walk it to
                // recover the actual chroma + bit depth so the
                // emitted ImageDesc carries a specific
                // JPEG_XS_YUV*_* variant rather than the
                // YUV10_422_Rec709 placeholder.
                JpegXsBitstream::PictureInfo info;
                if (JpegXsBitstream::parsePictureHeader(au, info).isOk() && info.width > 0 && info.height > 0) {
                        PixelFormat::ID pid_id = JpegXsBitstream::pixelFormatFor(info);
                        if (pid_id == PixelFormat::Invalid) {
                                pid_id = PixelFormat::JPEG_XS_YUV10_422_Rec709;
                        }
                        _readerImageDesc.insert(pid, ImageDesc(Size2Du32(info.width, info.height),
                                                                PixelFormat(pid_id)));
                        found = true;
                }
        }
        if (found) {
                _readerDimsProbed.insert(pid, true);
        }
}

Error MpegTsFramer::handleAccessUnit(const MpegTsDemuxer::AccessUnit &au) {
        Frame frame;
        const PixelFormat::ID pid = pixelFormatForStreamType(au.streamType, au.registrationFormat);
        if (pid != PixelFormat::Invalid) {
                // Video payload.  Probe SPS if we haven't yet learned
                // the dimensions for this PID — payloads from before
                // the probe completes carry size (0,0).
                if (au.randomAccess) probeVideoDimensions(au.pid, au.streamType, au.payload);
                ImageDesc imgDesc;
                auto it = _readerImageDesc.find(au.pid);
                if (it != _readerImageDesc.end()) {
                        imgDesc = it->second;
                } else {
                        imgDesc = ImageDesc(Size2Du32(0, 0), PixelFormat(pid));
                }
                Buffer payloadBuf(au.payload.size());
                if (au.payload.size() > 0) {
                        if (!payloadBuf.isValid()) return Error::NoMem;
                        payloadBuf.setSize(au.payload.size());
                        std::memcpy(payloadBuf.data(), au.payload.data(), au.payload.size());
                }
                auto p = CompressedVideoPayload::Ptr::create(imgDesc, std::move(payloadBuf));
                if (au.randomAccess) p.modify()->addFlag(MediaPayload::Keyframe);
                if (au.hasPts) p.modify()->setPts(ptsToMediaTimeStamp(au.pts90k));
                if (au.hasDts) p.modify()->setDts(ptsToMediaTimeStamp(au.dts90k));
                frame.addPayload(p);
        } else if (au.streamType == MpegTs::StreamTypePrivatePes &&
                   au.registrationFormat == MpegTs::RegFormatSmpte302M) {
                // SMPTE 302M — uncompressed PCM in a private PES.
                // Unpack to a PcmAudioPayload so the downstream
                // pipeline can consume linear PCM without going
                // through a decoder.
                Buffer    pcm;
                AudioDesc audDesc;
                size_t    samples = 0;
                Error     pe = Smpte302M::parse(au.payload, pcm, audDesc, samples);
                if (pe.isError()) {
                        promekiWarnThrottled(1000, "MpegTsFramer: 302M parse failed: %s",
                                             pe.name().cstr());
                        return Error::Ok;
                }
                BufferView pv(pcm, 0, pcm.size());
                auto       p = PcmAudioPayload::Ptr::create(audDesc, samples, pv);
                if (au.hasPts) p.modify()->setPts(ptsToMediaTimeStamp(au.pts90k));
                if (au.hasDts) p.modify()->setDts(ptsToMediaTimeStamp(au.dts90k));
                frame.addPayload(p);
        } else {
                const AudioFormat::ID afid =
                        audioFormatForStreamType(au.streamType, au.registrationFormat);
                if (afid == AudioFormat::Invalid) {
                        // Unknown stream — drop silently.
                        return Error::Ok;
                }
                // Sample rate / channel count aren't parsed from
                // ADTS / LATM here; the downstream decoder will
                // discover them.  Pass placeholders so the Frame is
                // structurally valid.
                AudioDesc audDesc(AudioFormat(afid), 48000.0f, 2);
                Buffer    payloadBuf(au.payload.size());
                if (au.payload.size() > 0) {
                        if (!payloadBuf.isValid()) return Error::NoMem;
                        payloadBuf.setSize(au.payload.size());
                        std::memcpy(payloadBuf.data(), au.payload.data(), au.payload.size());
                }
                auto p = CompressedAudioPayload::Ptr::create(audDesc, std::move(payloadBuf));
                if (au.hasPts) p.modify()->setPts(ptsToMediaTimeStamp(au.pts90k));
                if (au.hasDts) p.modify()->setDts(ptsToMediaTimeStamp(au.dts90k));
                frame.addPayload(p);
        }
        if (_frameCallback) return _frameCallback(std::move(frame));
        _readQueue.pushToBack(std::move(frame));
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
