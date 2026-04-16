/**
 * @file      mediaiotask_videodecoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/mediaiotask_videodecoder.h>
#include <promeki/mediaconfig.h>
#include <promeki/enums.h>
#include <promeki/frame.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/mediadesc.h>
#include <promeki/metadata.h>
#include <promeki/pixeldesc.h>
#include <promeki/mediapacket.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO(MediaIOTask_VideoDecoder)

MediaIO::FormatDesc MediaIOTask_VideoDecoder::formatDesc() {
        return {
                "VideoDecoder",
                "Generic video decoder stage (picks a VideoDecoder via VideoCodec)",
                {},     // No file extensions — this is a transform filter
                false,  // canOutput
                false,  // canInput
                true,   // canInputAndOutput
                []() -> MediaIOTask * {
                        return new MediaIOTask_VideoDecoder();
                },
                []() -> MediaIO::Config::SpecMap {
                        MediaIO::Config::SpecMap specs;
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
                        s(MediaConfig::OutputPixelDesc);
                        sWithDefault(MediaConfig::Capacity, int32_t(8));
                        return specs;
                }
        };
}

MediaIOTask_VideoDecoder::~MediaIOTask_VideoDecoder() {
        delete _decoder;
}

void MediaIOTask_VideoDecoder::configChanged(const MediaConfig &delta) {
        _config.merge(delta);
        if(_decoder != nullptr) _decoder->configure(_config);
}

Error MediaIOTask_VideoDecoder::createDecoder(const VideoCodec &codec) {
        if(!codec.canDecode()) {
                promekiErr("MediaIOTask_VideoDecoder: codec '%s' has no "
                           "registered decoder factory",
                           codec.name().cstr());
                return Error::NotSupported;
        }
        VideoDecoder *dec = codec.createDecoder();
        if(dec == nullptr) {
                promekiErr("MediaIOTask_VideoDecoder: createDecoder('%s') returned null",
                           codec.name().cstr());
                return Error::NotSupported;
        }
        dec->configure(_config);

        if(!_outputPixelDescSet) {
                List<int> supported = dec->supportedOutputs();
                if(!supported.isEmpty()) {
                        _outputPixelDesc = PixelDesc(
                                static_cast<PixelDesc::ID>(supported[0]));
                        _outputPixelDescSet = _outputPixelDesc.isValid();
                }
        }

        _codec   = codec;
        _decoder = dec;
        return Error::Ok;
}

Error MediaIOTask_VideoDecoder::executeCmd(MediaIOCommandOpen &cmd) {
        if(cmd.mode != MediaIO::InputAndOutput) {
                promekiErr("MediaIOTask_VideoDecoder: only InputAndOutput mode is supported");
                return Error::NotSupported;
        }

        const MediaIO::Config &cfg = cmd.config;
        _config = cfg;

        _outputPixelDesc = cfg.getAs<PixelDesc>(MediaConfig::OutputPixelDesc, PixelDesc());
        _outputPixelDescSet = _outputPixelDesc.isValid();

        _capacity = cfg.getAs<int>(MediaConfig::Capacity, 8);
        if(_capacity < 1) _capacity = 1;

        _codec            = VideoCodec();
        _decoder          = nullptr;
        _frameCount       = 0;
        _readCount        = 0;
        _packetsDecoded   = 0;
        _imagesOut        = 0;
        _capacityWarned   = false;
        _closed           = false;
        _outputQueue.clear();
        _pendingSrcFrames.clear();

        VideoCodec codec = cfg.getAs<VideoCodec>(MediaConfig::VideoCodec);
        if(codec.isValid()) {
                Error err = createDecoder(codec);
                if(err.isError()) return err;
        } else {
                promekiInfo("MediaIOTask_VideoDecoder: no VideoCodec configured, "
                            "will auto-detect from first packet");
        }

        MediaDesc outDesc;
        outDesc.setFrameRate(cmd.pendingMediaDesc.frameRate());
        for(const auto &srcImg : cmd.pendingMediaDesc.imageList()) {
                ImageDesc id = srcImg;
                if(_outputPixelDescSet) id = ImageDesc(srcImg.size(), _outputPixelDesc);
                outDesc.imageList().pushToBack(id);
        }
        for(const auto &srcAudio : cmd.pendingMediaDesc.audioList()) {
                outDesc.audioList().pushToBack(srcAudio);
        }

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

Error MediaIOTask_VideoDecoder::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        if(_decoder != nullptr) {
                _decoder->flush();
                drainDecoderInto();
                delete _decoder;
                _decoder = nullptr;
        }
        _pendingSrcFrames.clear();
        _config = MediaConfig();
        _codec = VideoCodec();
        _outputPixelDesc = PixelDesc();
        _outputPixelDescSet = false;
        _capacity = 0;
        _frameCount = 0;
        _readCount = 0;
        _packetsDecoded = 0;
        _imagesOut = 0;
        _capacityWarned = false;
        _closed = true;
        return Error::Ok;
}

Error MediaIOTask_VideoDecoder::executeCmd(MediaIOCommandWrite &cmd) {
        if(!cmd.frame.isValid()) {
                promekiErr("MediaIOTask_VideoDecoder: write with null frame");
                return Error::InvalidArgument;
        }
        stampWorkBegin();

        const Frame &frame = *cmd.frame;

        if(_decoder == nullptr) {
                const MediaPacket::PtrList &pkts = frame.packetList();
                bool found = false;
                for(const auto &pktPtr : pkts) {
                        if(!pktPtr || !pktPtr->isValid()) continue;
                        VideoCodec codec = VideoCodec::fromPixelDesc(pktPtr->pixelDesc());
                        if(!codec.isValid()) {
                                promekiErr("MediaIOTask_VideoDecoder: cannot resolve "
                                           "VideoCodec from PixelDesc '%s'",
                                           pktPtr->pixelDesc().name().cstr());
                                stampWorkEnd();
                                return Error::NotSupported;
                        }
                        Error err = createDecoder(codec);
                        if(err.isError()) {
                                stampWorkEnd();
                                return err;
                        }
                        promekiInfo("MediaIOTask_VideoDecoder: auto-detected codec '%s' "
                                    "from packet PixelDesc '%s'",
                                    codec.name().cstr(),
                                    pktPtr->pixelDesc().name().cstr());
                        found = true;
                        break;
                }
                if(!found) {
                        promekiErr("MediaIOTask_VideoDecoder: no valid packet to "
                                   "detect codec from");
                        stampWorkEnd();
                        return Error::InvalidArgument;
                }
        }

        if(static_cast<int>(_outputQueue.size()) >= _capacity && !_capacityWarned) {
                promekiWarn("MediaIOTask_VideoDecoder: output queue exceeded capacity "
                            "(%d >= %d)",
                            static_cast<int>(_outputQueue.size()), _capacity);
                _capacityWarned = true;
        }

        for(const auto &pktPtr : frame.packetList()) {
                if(!pktPtr || !pktPtr->isValid()) continue;
                _pendingSrcFrames.pushToBack(cmd.frame);
                Error err = _decoder->submitPacket(*pktPtr);
                if(err.isError()) {
                        promekiErr("MediaIOTask_VideoDecoder: submitPacket failed: %s",
                                   _decoder->lastErrorMessage().cstr());
                        stampWorkEnd();
                        return err;
                }
                _packetsDecoded++;
                drainDecoderInto();
        }

        _frameCount++;
        cmd.currentFrame = _frameCount;
        cmd.frameCount   = _frameCount;
        stampWorkEnd();
        return Error::Ok;
}

void MediaIOTask_VideoDecoder::drainDecoderInto() {
        if(_decoder == nullptr) return;
        while(true) {
                Image img = _decoder->receiveFrame();
                if(!img.isValid()) break;

                // Pair this image with the oldest queued source Frame
                // — that's the packet Frame that produced it even when
                // DPB warmup delays a few frames.  Empty queue is a
                // best-effort fallback (shouldn't happen for I/P-only
                // streams but leaves the image intact for any other).
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
                out->imageList().pushToBack(Image::Ptr::create(std::move(img)));
                _outputQueue.pushToBack(std::move(outFrame));
                _imagesOut++;
        }
}

Error MediaIOTask_VideoDecoder::executeCmd(MediaIOCommandRead &cmd) {
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

Error MediaIOTask_VideoDecoder::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsPacketsDecoded, _packetsDecoded);
        cmd.stats.set(StatsImagesOut, _imagesOut);
        cmd.stats.set(MediaIOStats::QueueDepth,
                      static_cast<int64_t>(_outputQueue.size()));
        cmd.stats.set(MediaIOStats::QueueCapacity,
                      static_cast<int64_t>(_capacity));
        return Error::Ok;
}

int MediaIOTask_VideoDecoder::pendingOutput() const {
        return static_cast<int>(_outputQueue.size());
}

PROMEKI_NAMESPACE_END
