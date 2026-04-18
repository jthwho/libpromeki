/**
 * @file      mediaiotask_rawbitstream.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/mediaiotask_rawbitstream.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediapacket.h>
#include <promeki/bufferview.h>
#include <promeki/frame.h>
#include <promeki/image.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO(MediaIOTask_RawBitstream)

MediaIO::FormatDesc MediaIOTask_RawBitstream::formatDesc() {
        return {
                "RawBitstream",
                "Raw elementary-stream sink (H.264 / HEVC Annex-B — writes packet payloads verbatim)",
                {"h264", "h265", "hevc", "bit"},
                false,  // canOutput (reading needs a NAL parser; follow-up)
                true,   // canInput — this is a sink
                false,  // canInputAndOutput
                []() -> MediaIOTask * {
                        return new MediaIOTask_RawBitstream();
                },
                []() -> MediaIO::Config::SpecMap {
                        MediaIO::Config::SpecMap specs;
                        const VariantSpec *gs = MediaConfig::spec(MediaConfig::Filename);
                        if(gs) specs.insert(MediaConfig::Filename, *gs);
                        return specs;
                }
        };
}

MediaIOTask_RawBitstream::~MediaIOTask_RawBitstream() {
        if(_file.isOpen()) _file.close();
}

Error MediaIOTask_RawBitstream::executeCmd(MediaIOCommandOpen &cmd) {
        if(cmd.mode != MediaIO::Input) {
                promekiErr("MediaIOTask_RawBitstream: only sink mode is supported "
                           "(mediaplay open-as-source for raw elementary streams "
                           "needs a codec-aware NAL parser, added later)");
                return Error::NotSupported;
        }

        _filename = cmd.config.getAs<String>(MediaConfig::Filename);
        if(_filename.isEmpty()) {
                promekiErr("MediaIOTask_RawBitstream: Filename is required");
                return Error::InvalidArgument;
        }

        _file.setFilename(_filename);
        Error err = _file.open(IODevice::WriteOnly,
                               File::Create | File::Truncate);
        if(err.isError()) {
                promekiErr("MediaIOTask_RawBitstream: open '%s' for write failed: %s",
                           _filename.cstr(), err.name().cstr());
                return err;
        }

        _packetsWritten  = 0;
        _bytesWritten    = 0;
        _warnedNoPackets = false;

        // No decode-side MediaDesc to publish — we're a sink — but the
        // MediaIO framework still expects cmd.mediaDesc / frameRate to
        // be populated so downstream introspection works.  Forward the
        // upstream's view unchanged.
        cmd.mediaDesc  = cmd.pendingMediaDesc;
        cmd.audioDesc  = cmd.pendingAudioDesc;
        cmd.metadata   = cmd.pendingMetadata;
        cmd.frameRate  = cmd.pendingMediaDesc.frameRate();
        cmd.canSeek    = false;
        cmd.frameCount = MediaIO::FrameCountInfinite;
        cmd.defaultStep          = 1;
        cmd.defaultPrefetchDepth = 0;
        cmd.defaultWriteDepth    = 4;
        return Error::Ok;
}

Error MediaIOTask_RawBitstream::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        if(_file.isOpen()) _file.close();
        _filename.clear();
        _packetsWritten  = 0;
        _bytesWritten    = 0;
        _warnedNoPackets = false;
        return Error::Ok;
}

Error MediaIOTask_RawBitstream::executeCmd(MediaIOCommandWrite &cmd) {
        if(!cmd.frame.isValid()) {
                return Error::InvalidArgument;
        }
        if(!_file.isOpen()) {
                return Error::NotSupported;
        }
        stampWorkBegin();

        const Frame &frame = *cmd.frame;
        bool anyPacket = false;
        for(const Image::Ptr &imgPtr : frame.imageList()) {
                if(!imgPtr.isValid() || !imgPtr->isCompressed()) continue;
                const MediaPacket::Ptr &pktPtr = imgPtr->packet();
                if(!pktPtr.isValid()) continue;
                const MediaPacket &pkt = *pktPtr;
                const BufferView &view = pkt.view();
                if(view.size() == 0 || !view.isValid()) continue;
                int64_t n = _file.write(view.data(),
                                        static_cast<int64_t>(view.size()));
                if(n < 0 || static_cast<size_t>(n) != view.size()) {
                        promekiErr("MediaIOTask_RawBitstream: short write (%lld / %zu)",
                                   (long long)n, view.size());
                        stampWorkEnd();
                        return Error::IOError;
                }
                _packetsWritten++;
                _bytesWritten += static_cast<int64_t>(view.size());
                anyPacket = true;
        }
        if(!anyPacket) {
                if(!_warnedNoPackets) {
                        promekiWarn("MediaIOTask_RawBitstream: Frame has no compressed "
                                    "Image with an attached MediaPacket — is an encoder "
                                    "stage upstream of this sink?");
                        _warnedNoPackets = true;
                }
                stampWorkEnd();
                return Error::Ok;
        }

        cmd.currentFrame = _packetsWritten;
        cmd.frameCount   = _packetsWritten;
        stampWorkEnd();
        return Error::Ok;
}

Error MediaIOTask_RawBitstream::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsPacketsWritten, _packetsWritten);
        cmd.stats.set(StatsBytesWritten, _bytesWritten);
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
