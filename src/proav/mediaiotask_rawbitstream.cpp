/**
 * @file      mediaiotask_rawbitstream.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/mediaiotask_rawbitstream.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaiodescription.h>
#include <promeki/mediapacket.h>
#include <promeki/bufferview.h>
#include <promeki/frame.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/logger.h>
#include <promeki/pixeldesc.h>
#include <promeki/videocodec.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO(MediaIOTask_RawBitstream)

MediaIO::FormatDesc MediaIOTask_RawBitstream::formatDesc() {
        return {
                "RawBitstream",
                "Raw elementary-stream sink (H.264 / HEVC Annex-B — writes packet payloads verbatim)",
                {"h264", "h265", "hevc", "bit"},
                false,  // canBeSource (reading needs a NAL parser; follow-up)
                true,   // canBeSink — this is a sink
                false,  // canBeTransform
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
        if(cmd.mode != MediaIO::Sink) {
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

// ---- Introspection / negotiation ----
//
// RawBitstream is a "dumb" sink — it copies MediaPacket payload bytes
// verbatim to the file.  The only hard constraint is that the input
// carries a compressed Image with an attached MediaPacket; the file
// extension (.h264 / .h265 / .hevc / .bit) advertises which codec a
// consumer should assume, but the writer itself doesn't enforce it.
// So:
//  - describe(): every registered compressed PixelDesc is acceptable.
//  - proposeInput(): reject uncompressed input so the planner splices
//    in a VideoEncoder ahead of us instead of routing raw frames that
//    would hit the `no compressed Image with an attached MediaPacket`
//    warning at runtime.

Error MediaIOTask_RawBitstream::describe(MediaIODescription *out) const {
        if(out == nullptr) return Error::Invalid;
        for(VideoCodec::ID cid : VideoCodec::registeredIDs()) {
                VideoCodec codec(cid);
                if(!codec.isValid()) continue;
                for(int pdId : codec.compressedPixelDescs()) {
                        // A codec could in principle register a PixelDesc
                        // ID that is not in the well-known table (custom
                        // variant added by a plugin); skip those so we
                        // do not advertise a malformed MediaDesc from
                        // describe().
                        PixelDesc pd(static_cast<PixelDesc::ID>(pdId));
                        if(!pd.isValid()) continue;
                        MediaDesc accepted;
                        accepted.imageList().pushToBack(
                                ImageDesc(Size2Du32(0, 0), pd));
                        out->acceptableFormats().pushToBack(accepted);
                }
        }
        return Error::Ok;
}

Error MediaIOTask_RawBitstream::proposeInput(const MediaDesc &offered,
                                             MediaDesc *preferred) const {
        if(preferred == nullptr) return Error::Invalid;
        if(offered.imageList().isEmpty()) return Error::NotSupported;
        const PixelDesc &pd = offered.imageList()[0].pixelDesc();
        if(!pd.isValid() || !pd.isCompressed()) {
                return Error::NotSupported;
        }
        *preferred = offered;
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
