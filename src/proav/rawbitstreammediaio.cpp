/**
 * @file      rawbitstreammediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/rawbitstreammediaio.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaiodescription.h>
#include <promeki/mediaiorequest.h>
#include <promeki/bufferview.h>
#include <promeki/frame.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/imagedesc.h>
#include <promeki/logger.h>
#include <promeki/pixelformat.h>
#include <promeki/videocodec.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO_FACTORY(RawBitstreamFactory)

MediaIOFactory::Config::SpecMap RawBitstreamFactory::configSpecs() const {
        Config::SpecMap    specs;
        const VariantSpec *gs = MediaConfig::spec(MediaConfig::Filename);
        if (gs) specs.insert(MediaConfig::Filename, *gs);
        return specs;
}

MediaIO *RawBitstreamFactory::create(const Config &config, ObjectBase *parent) const {
        auto *io = new RawBitstreamMediaIO(parent);
        io->setConfig(config);
        return io;
}

RawBitstreamMediaIO::RawBitstreamMediaIO(ObjectBase *parent) : SharedThreadMediaIO(parent) {}

RawBitstreamMediaIO::~RawBitstreamMediaIO() {
        if (isOpen()) (void)close().wait();
        if (_file.isOpen()) _file.close();
}

Error RawBitstreamMediaIO::executeCmd(MediaIOCommandOpen &cmd) {
        _filename = cmd.config.getAs<String>(MediaConfig::Filename);
        if (_filename.isEmpty()) {
                promekiErr("RawBitstreamMediaIO: Filename is required");
                return Error::InvalidArgument;
        }

        _file.setFilename(_filename);
        Error err = _file.open(IODevice::WriteOnly, File::Create | File::Truncate);
        if (err.isError()) {
                promekiErr("RawBitstreamMediaIO: open '%s' for write failed: %s", _filename.cstr(),
                           err.name().cstr());
                return err;
        }

        _packetsWritten = 0;
        _bytesWritten = 0;
        _warnedNoPackets = false;

        MediaIOPortGroup *group = addPortGroup("rawbitstream");
        if (group == nullptr) return Error::Invalid;
        group->setFrameRate(cmd.pendingMediaDesc.frameRate());
        group->setCanSeek(false);
        group->setFrameCount(MediaIO::FrameCountInfinite);
        if (addSink(group, cmd.pendingMediaDesc) == nullptr) return Error::Invalid;
        return Error::Ok;
}

Error RawBitstreamMediaIO::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        if (_file.isOpen()) _file.close();
        _filename.clear();
        _packetsWritten = 0;
        _bytesWritten = 0;
        _warnedNoPackets = false;
        return Error::Ok;
}

Error RawBitstreamMediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        if (!cmd.frame.isValid()) {
                return Error::InvalidArgument;
        }
        if (!_file.isOpen()) {
                return Error::NotSupported;
        }

        const Frame &frame = *cmd.frame;
        bool         anyPacket = false;
        for (const VideoPayload::Ptr &vp : frame.videoPayloads()) {
                if (!vp.isValid()) continue;
                const auto *cvp = vp->as<CompressedVideoPayload>();
                if (cvp == nullptr) continue;
                // Write every plane of the compressed payload — most
                // encoders emit a single contiguous bitstream but some
                // (NVENC SPS+PPS+IDR concatenation) split it across
                // multiple non-overlapping views of one buffer.
                for (size_t pi = 0; pi < cvp->planeCount(); ++pi) {
                        auto view = cvp->plane(pi);
                        if (view.size() == 0 || !view.isValid()) continue;
                        int64_t n = _file.write(view.data(), static_cast<int64_t>(view.size()));
                        if (n < 0 || static_cast<size_t>(n) != view.size()) {
                                promekiErr("RawBitstreamMediaIO: short write (%lld / %zu)", (long long)n,
                                           view.size());
                                return Error::IOError;
                        }
                        _packetsWritten++;
                        _bytesWritten += static_cast<int64_t>(view.size());
                        anyPacket = true;
                }
        }
        if (!anyPacket) {
                if (!_warnedNoPackets) {
                        promekiWarn("RawBitstreamMediaIO: Frame has no "
                                    "CompressedVideoPayload — is an encoder "
                                    "stage upstream of this sink?");
                        _warnedNoPackets = true;
                }
                return Error::Ok;
        }

        cmd.currentFrame = _packetsWritten;
        cmd.frameCount = _packetsWritten;
        return Error::Ok;
}

Error RawBitstreamMediaIO::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsPacketsWritten, _packetsWritten);
        cmd.stats.set(StatsBytesWritten, _bytesWritten);
        return Error::Ok;
}

// ---- Introspection / negotiation ----
//
// RawBitstream is a "dumb" sink — it copies CompressedVideoPayload
// bytes verbatim to the file.  The only hard constraint is that the
// input carries a CompressedVideoPayload; the file extension
// (.h264 / .h265 / .hevc / .bit) advertises which codec a consumer
// should assume, but the writer itself doesn't enforce it.
// So:
//  - describe(): every registered compressed PixelFormat is acceptable.
//  - proposeInput(): reject uncompressed input so the planner splices
//    in a VideoEncoder ahead of us instead of routing raw frames that
//    would hit the `no CompressedVideoPayload` warning at runtime.

Error RawBitstreamMediaIO::describe(MediaIODescription *out) const {
        if (out == nullptr) return Error::Invalid;
        // Let the base populate identity, role flags, cached state,
        // and per-port snapshots first.  We supplement with our
        // backend-specific @c acceptableFormats below.
        Error baseErr = MediaIO::describe(out);
        if (baseErr.isError()) return baseErr;
        for (VideoCodec::ID cid : VideoCodec::registeredIDs()) {
                VideoCodec codec(cid);
                if (!codec.isValid()) continue;
                for (const PixelFormat &pd : codec.compressedPixelFormats()) {
                        // A codec could in principle register a PixelFormat
                        // ID that is not in the well-known table (custom
                        // variant added by a plugin); skip those so we
                        // do not advertise a malformed MediaDesc from
                        // describe().
                        if (!pd.isValid()) continue;
                        MediaDesc accepted;
                        accepted.imageList().pushToBack(ImageDesc(Size2Du32(0, 0), pd));
                        out->acceptableFormats().pushToBack(accepted);
                }
        }
        return Error::Ok;
}

Error RawBitstreamMediaIO::proposeInput(const MediaDesc &offered, MediaDesc *preferred) const {
        if (preferred == nullptr) return Error::Invalid;
        if (offered.imageList().isEmpty()) return Error::NotSupported;
        const PixelFormat &pd = offered.imageList()[0].pixelFormat();
        if (!pd.isValid() || !pd.isCompressed()) {
                return Error::NotSupported;
        }
        *preferred = offered;
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
