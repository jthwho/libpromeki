/**
 * @file      mediaiotask_videoencoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/mediaiotask_videoencoder.h>
#include <promeki/mediaconfig.h>
#include <promeki/enums.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/mediadesc.h>
#include <promeki/metadata.h>
#include <promeki/pixeldesc.h>
#include <promeki/logger.h>
#include <promeki/mediatimestamp.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO(MediaIOTask_VideoEncoder)

MediaIO::FormatDesc MediaIOTask_VideoEncoder::formatDesc() {
        return {
                "VideoEncoder",
                "Generic video encoder stage (picks a VideoEncoder via VideoCodec)",
                {},     // No file extensions — this is a transform filter
                false,  // canOutput
                false,  // canInput
                true,   // canInputAndOutput
                []() -> MediaIOTask * {
                        return new MediaIOTask_VideoEncoder();
                },
                []() -> MediaIO::Config::SpecMap {
                        MediaIO::Config::SpecMap specs;
                        // Inherit the library-wide spec for each key so
                        // descriptions, types, ranges, and enum types
                        // stay consistent across every backend that
                        // references them.  Local defaults only override
                        // when we want a backend-specific preferred
                        // value different from the MediaConfig library
                        // default.
                        auto s = [&specs](MediaConfig::ID id) {
                                const VariantSpec *gs = MediaConfig::spec(id);
                                if(gs) specs.insert(id, *gs);
                        };
                        auto sWithDefault =
                                [&specs](MediaConfig::ID id, const Variant &def) {
                                const VariantSpec *gs = MediaConfig::spec(id);
                                specs.insert(id, gs
                                        ? VariantSpec(*gs).setDefault(def)
                                        : VariantSpec().setDefault(def));
                        };
                        s(MediaConfig::VideoCodec);
                        s(MediaConfig::FrameRate);
                        s(MediaConfig::BitrateKbps);
                        s(MediaConfig::MaxBitrateKbps);
                        s(MediaConfig::VideoRcMode);
                        s(MediaConfig::GopLength);
                        s(MediaConfig::IdrInterval);
                        s(MediaConfig::BFrames);
                        s(MediaConfig::LookaheadFrames);
                        s(MediaConfig::VideoPreset);
                        s(MediaConfig::VideoProfile);
                        s(MediaConfig::VideoLevel);
                        s(MediaConfig::VideoQp);
                        sWithDefault(MediaConfig::Capacity, int32_t(8));
                        return specs;
                }
        };
}

MediaIOTask_VideoEncoder::~MediaIOTask_VideoEncoder() {
        delete _encoder;
}

Error MediaIOTask_VideoEncoder::executeCmd(MediaIOCommandOpen &cmd) {
        if(cmd.mode != MediaIO::InputAndOutput) {
                promekiErr("MediaIOTask_VideoEncoder: only InputAndOutput mode is supported");
                return Error::NotSupported;
        }

        const MediaIO::Config &cfg = cmd.config;

        _codec = cfg.getAs<VideoCodec>(MediaConfig::VideoCodec);
        if(!_codec.isValid()) {
                promekiErr("MediaIOTask_VideoEncoder: VideoCodec is required "
                           "(e.g. \"H264\", \"HEVC\", \"JPEG\")");
                return Error::InvalidArgument;
        }
        if(!_codec.canEncode()) {
                promekiErr("MediaIOTask_VideoEncoder: codec '%s' has no "
                           "registered encoder factory",
                           _codec.name().cstr());
                return Error::NotSupported;
        }

        // Typed factory lookup; the registered VideoEncoder subclass
        // owns any codec-specific state (NVENC session, CUDA context, …).
        VideoEncoder *enc = _codec.createEncoder();
        if(enc == nullptr) {
                promekiErr("MediaIOTask_VideoEncoder: createEncoder('%s') returned null",
                           _codec.name().cstr());
                return Error::NotSupported;
        }

        // The entire caller-visible MediaConfig flows unfiltered into
        // the encoder so keys it understands are honoured and ones it
        // doesn't are quietly ignored (the VideoEncoder contract).
        // Stamp the effective FrameRate from the upstream MediaDesc so
        // the encoder's rate-control math (bits-per-frame, HRD buffer,
        // H.264 VUI timing) uses the real stream rate rather than
        // whatever default the backend picked — callers rarely set
        // MediaConfig::FrameRate explicitly on a transform stage.
        MediaIO::Config encCfg = cfg;
        if(cmd.pendingMediaDesc.frameRate().isValid()) {
                encCfg.set(MediaConfig::FrameRate, cmd.pendingMediaDesc.frameRate());
        }
        enc->configure(encCfg);

        _capacity = cfg.getAs<int>(MediaConfig::Capacity, 8);
        if(_capacity < 1) _capacity = 1;

        // Build the downstream-visible MediaDesc: each source image is
        // replaced by one at the encoder's compressed PixelDesc so the
        // next stage picks up the right format before the first frame
        // arrives.
        MediaDesc outDesc;
        outDesc.setFrameRate(cmd.pendingMediaDesc.frameRate());

        const PixelDesc encOutPd = enc->outputPixelDesc();
        for(const auto &srcImg : cmd.pendingMediaDesc.imageList()) {
                outDesc.imageList().pushToBack(ImageDesc(srcImg.size(), encOutPd));
        }
        for(const auto &srcAudio : cmd.pendingMediaDesc.audioList()) {
                outDesc.audioList().pushToBack(srcAudio);
        }

        _encoder          = enc;
        _frameCount       = 0;
        _readCount        = 0;
        _framesEncoded    = 0;
        _packetsOut       = 0;
        _capacityWarned   = false;
        _multiImageWarned = false;
        _closed           = false;
        _outputQueue.clear();
        _pendingSrcFrames.clear();

        cmd.mediaDesc     = outDesc;
        if(!outDesc.audioList().isEmpty()) cmd.audioDesc = outDesc.audioList()[0];
        cmd.metadata      = cmd.pendingMetadata;
        cmd.frameRate     = outDesc.frameRate();
        cmd.canSeek       = false;
        cmd.frameCount    = MediaIO::FrameCountInfinite;
        cmd.defaultStep   = 1;
        cmd.defaultPrefetchDepth = 1;
        cmd.defaultWriteDepth    = _capacity;
        return Error::Ok;
}

Error MediaIOTask_VideoEncoder::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        if(_encoder != nullptr) {
                // Best-effort flush so anything the encoder has buffered
                // makes it out before we tear the session down.  Because
                // pipelines close us only after all reads have been
                // consumed, any packets emitted here are stored in
                // _outputQueue for a subsequent read if the caller cares
                // to drain again — most consumers won't, but the
                // contract stays honest either way.
                _encoder->flush();
                drainEncoderInto();
                delete _encoder;
                _encoder = nullptr;
        }
        _pendingSrcFrames.clear();
        _codec = VideoCodec();
        _capacity = 0;
        _frameCount = 0;
        _readCount = 0;
        _framesEncoded = 0;
        _packetsOut = 0;
        _capacityWarned = false;
        _multiImageWarned = false;
        _closed = true;
        return Error::Ok;
}

Error MediaIOTask_VideoEncoder::executeCmd(MediaIOCommandWrite &cmd) {
        if(!cmd.frame.isValid()) {
                promekiErr("MediaIOTask_VideoEncoder: write with null frame");
                return Error::InvalidArgument;
        }
        if(_encoder == nullptr) {
                return Error::NotSupported;
        }
        stampWorkBegin();

        if(static_cast<int>(_outputQueue.size()) >= _capacity && !_capacityWarned) {
                promekiWarn("MediaIOTask_VideoEncoder: output queue exceeded capacity "
                            "(%d >= %d) — downstream is not draining packets fast enough",
                            static_cast<int>(_outputQueue.size()), _capacity);
                _capacityWarned = true;
        }

        const Frame &frame = *cmd.frame;

        // Read the first image's MediaTimeStamp from metadata so the
        // encoder can thread PTS back out on the matching packet.  If
        // the upstream didn't set one we pass an invalid MediaTimeStamp
        // and let the encoder invent something (most codecs will use
        // internal frame indexing).
        MediaTimeStamp pts;
        // Per-essence MediaTimeStamp lives on the Image (an essence
        // object), not on the Frame; the Image::metadata() lookup
        // succeeds transparently when the producer stamped the frame.
        if(!frame.imageList().isEmpty()) {
                const Image::Ptr &imgPtr = frame.imageList()[0];
                if(imgPtr) {
                        const Metadata &md = imgPtr->metadata();
                        pts = md.getAs<MediaTimeStamp>(Metadata::MediaTimeStamp);
                }
        }

        if(frame.imageList().isEmpty()) {
                // No image to encode — let the frame pass through so
                // audio / metadata-only inputs aren't lost.
                Frame::Ptr outFrame = Frame::Ptr::create();
                outFrame.modify()->metadata() = frame.metadata();
                for(const auto &a : frame.audioList()) {
                        outFrame.modify()->audioList().pushToBack(a);
                }
                _outputQueue.pushToBack(std::move(outFrame));
                stampWorkEnd();
                return Error::Ok;
        }

        if(frame.imageList().size() > 1 && !_multiImageWarned) {
                promekiWarn("MediaIOTask_VideoEncoder: Frame carries %zu images; "
                            "only image[0] will be encoded in this cut",
                            (size_t)frame.imageList().size());
                _multiImageWarned = true;
        }

        const Image::Ptr &srcImgPtr = frame.imageList()[0];
        if(!srcImgPtr || !srcImgPtr->isValid()) {
                stampWorkEnd();
                return Error::InvalidArgument;
        }
        Error err = _encoder->submitFrame(*srcImgPtr, pts);
        if(err.isError()) {
                promekiErr("MediaIOTask_VideoEncoder: submitFrame failed: %s",
                           _encoder->lastErrorMessage().cstr());
                stampWorkEnd();
                return err;
        }
        // Record the source frame so the matching packet — which may
        // not emerge until a later submit if the encoder returned
        // NEED_MORE_INPUT on this one — can be paired back up with its
        // original metadata and audio in drainEncoderInto.
        _pendingSrcFrames.pushToBack(cmd.frame);
        _frameCount++;
        _framesEncoded++;
        drainEncoderInto();

        cmd.currentFrame = _frameCount;
        cmd.frameCount   = _frameCount;
        stampWorkEnd();
        return Error::Ok;
}

void MediaIOTask_VideoEncoder::drainEncoderInto() {
        if(_encoder == nullptr) return;
        while(true) {
                MediaPacket::Ptr pkt = _encoder->receivePacket();
                if(!pkt) break;
                if(pkt->isEndOfStream()) {
                        // EOS is an encoder-internal signal that the
                        // session is drained; no need to propagate it
                        // as its own Frame (the pipeline uses the
                        // MediaIO close/EOF path for that).
                        continue;
                }

                // Pair the packet with the oldest queued source Frame
                // — that's the one that produced this packet even if
                // an intervening submit called drainEncoderInto too.
                // The queue can legitimately be empty on a late flush
                // if the caller already drained everything previously;
                // in that case the output still carries the packet
                // but with no audio / frame metadata.
                Frame::Ptr origin;
                if(!_pendingSrcFrames.isEmpty()) {
                        origin = _pendingSrcFrames.front();
                        _pendingSrcFrames.remove(0);
                }

                Frame::Ptr outFrame = Frame::Ptr::create();
                Frame     *out = outFrame.modify();
                if(origin.isValid()) {
                        out->metadata() = origin->metadata();
                        for(const auto &a : origin->audioList()) {
                                out->audioList().pushToBack(a);
                        }
                }
                out->packetList().pushToBack(pkt);
                _outputQueue.pushToBack(std::move(outFrame));
                _packetsOut++;
        }
}

Error MediaIOTask_VideoEncoder::executeCmd(MediaIOCommandRead &cmd) {
        if(_outputQueue.isEmpty()) {
                return _closed ? Error::EndOfFile : Error::TryAgain;
        }
        Frame::Ptr frame = std::move(_outputQueue.front());
        _outputQueue.remove(0);
        _readCount++;
        cmd.frame = std::move(frame);
        cmd.currentFrame = _readCount;
        return Error::Ok;
}

Error MediaIOTask_VideoEncoder::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsFramesEncoded, _framesEncoded);
        cmd.stats.set(StatsPacketsOut, _packetsOut);
        cmd.stats.set(MediaIOStats::QueueDepth,
                      static_cast<int64_t>(_outputQueue.size()));
        cmd.stats.set(MediaIOStats::QueueCapacity,
                      static_cast<int64_t>(_capacity));
        return Error::Ok;
}

int MediaIOTask_VideoEncoder::pendingOutput() const {
        return static_cast<int>(_outputQueue.size());
}

PROMEKI_NAMESPACE_END
