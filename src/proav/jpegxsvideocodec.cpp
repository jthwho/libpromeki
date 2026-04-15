/**
 * @file      jpegxsvideocodec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/jpegxsvideocodec.h>
#include <promeki/mediapacket.h>
#include <promeki/mediaconfig.h>
#include <promeki/buffer.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/pixeldesc.h>
#include <promeki/videocodec.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// JpegXsVideoEncoder
// ---------------------------------------------------------------------------

JpegXsVideoEncoder::~JpegXsVideoEncoder() = default;

String JpegXsVideoEncoder::name() const { return "JPEG_XS"; }

String JpegXsVideoEncoder::description() const {
        return "JPEG XS VideoEncoder (SVT-JPEG-XS via JpegXsImageCodec)";
}

PixelDesc JpegXsVideoEncoder::outputPixelDesc() const {
        // Representative member of the JPEG XS family — actual emitted
        // packets carry the variant the underlying JpegXsImageCodec
        // selected per-input (matrix / range / subsampling).
        if(_outputPd.isValid()) return _outputPd;
        return PixelDesc(PixelDesc::JPEG_XS_YUV10_422_Rec709);
}

List<int> JpegXsVideoEncoder::supportedInputs() const {
        return {
                static_cast<int>(PixelDesc::YUV8_422_Rec709),
                static_cast<int>(PixelDesc::YUV10_422_Rec709),
                static_cast<int>(PixelDesc::YUV12_422_UYVY_LE_Rec709),
                static_cast<int>(PixelDesc::YUV8_420_Planar_Rec709),
                static_cast<int>(PixelDesc::YUV10_420_Planar_LE_Rec709),
                static_cast<int>(PixelDesc::YUV12_420_Planar_LE_Rec709),
                static_cast<int>(PixelDesc::RGB8_sRGB),
        };
}

void JpegXsVideoEncoder::configure(const MediaConfig &config) {
        _codec.configure(config);
        _outputPd = config.getAs<PixelDesc>(MediaConfig::OutputPixelDesc, PixelDesc());
        _capacity = config.getAs<int>(MediaConfig::Capacity, 8);
        if(_capacity < 1) _capacity = 1;
}

Error JpegXsVideoEncoder::submitFrame(const Image &frame, const MediaTimeStamp &pts) {
        clearError();
        if(!frame.isValid()) {
                setError(Error::Invalid, "JpegXsVideoEncoder: invalid frame");
                return _lastError;
        }
        if(static_cast<int>(_queue.size()) >= _capacity && !_capacityWarned) {
                promekiWarn("JpegXsVideoEncoder: output queue exceeded capacity (%d)",
                            _capacity);
                _capacityWarned = true;
        }

        Image encoded = _codec.encode(frame);
        if(!encoded.isValid()) {
                setError(_codec.lastError().isError() ? _codec.lastError()
                                                     : Error::ConversionFailed,
                         _codec.lastErrorMessage().isEmpty()
                                 ? String("JpegXsVideoEncoder: encode failed")
                                 : _codec.lastErrorMessage());
                return _lastError;
        }

        auto pkt = MediaPacket::Ptr::create(encoded.plane(0), encoded.pixelDesc());
        pkt.modify()->setPts(pts);
        pkt.modify()->setDts(pts);
        pkt.modify()->addFlag(MediaPacket::Keyframe);
        _queue.push_back(std::move(pkt));
        return Error::Ok;
}

MediaPacket::Ptr JpegXsVideoEncoder::receivePacket() {
        if(_queue.empty()) return MediaPacket::Ptr();
        auto pkt = std::move(_queue.front());
        _queue.pop_front();
        return pkt;
}

Error JpegXsVideoEncoder::flush() { return Error::Ok; }

Error JpegXsVideoEncoder::reset() {
        _queue.clear();
        _capacityWarned = false;
        return Error::Ok;
}

void JpegXsVideoEncoder::requestKeyframe() {
        // Every JPEG XS access unit is a keyframe by definition.
}

// ---------------------------------------------------------------------------
// JpegXsVideoDecoder
// ---------------------------------------------------------------------------

JpegXsVideoDecoder::~JpegXsVideoDecoder() = default;

String JpegXsVideoDecoder::name() const { return "JPEG_XS"; }

String JpegXsVideoDecoder::description() const {
        return "JPEG XS VideoDecoder (SVT-JPEG-XS via JpegXsImageCodec)";
}

PixelDesc JpegXsVideoDecoder::inputPixelDesc() const {
        return PixelDesc(PixelDesc::JPEG_XS_YUV10_422_Rec709);
}

List<int> JpegXsVideoDecoder::supportedOutputs() const {
        // Mirror the decodeTargets actually populated on each JPEG XS
        // compressed PixelDesc — the SVT-JPEG-XS decoder only emits
        // planar YUV at matching bit depth + subsampling (or planar
        // RGB for the JPEG_XS_RGB8_sRGB variant).  Asking for anything
        // else trips the "Requested decode target does not match"
        // validation inside JpegXsImageCodec::decode.
        return {
                static_cast<int>(PixelDesc::YUV8_422_Planar_Rec709),
                static_cast<int>(PixelDesc::YUV10_422_Planar_LE_Rec709),
                static_cast<int>(PixelDesc::YUV12_422_Planar_LE_Rec709),
                static_cast<int>(PixelDesc::YUV8_420_Planar_Rec709),
                static_cast<int>(PixelDesc::YUV10_420_Planar_LE_Rec709),
                static_cast<int>(PixelDesc::YUV12_420_Planar_LE_Rec709),
                static_cast<int>(PixelDesc::RGB8_Planar_sRGB),
        };
}

void JpegXsVideoDecoder::configure(const MediaConfig &config) {
        _codec.configure(config);
        _outputPd = config.getAs<PixelDesc>(MediaConfig::OutputPixelDesc, PixelDesc());
        _capacity = config.getAs<int>(MediaConfig::Capacity, 8);
        if(_capacity < 1) _capacity = 1;
}

Error JpegXsVideoDecoder::submitPacket(const MediaPacket &packet) {
        clearError();
        if(!packet.isValid() || packet.size() == 0) {
                setError(Error::Invalid, "JpegXsVideoDecoder: empty packet");
                return _lastError;
        }
        if(static_cast<int>(_queue.size()) >= _capacity && !_capacityWarned) {
                promekiWarn("JpegXsVideoDecoder: output queue exceeded capacity (%d)",
                            _capacity);
                _capacityWarned = true;
        }

        // Same wedge as JpegVideoDecoder: wrap the packet bytes as a
        // compressed Image with placeholder dims (the SVT-JPEG-XS
        // decoder reads the real dimensions from the bitstream itself
        // on first call).
        const PixelDesc inputPd = packet.pixelDesc().isValid()
                ? packet.pixelDesc()
                : PixelDesc(PixelDesc::JPEG_XS_YUV10_422_Rec709);
        Image jxsImage = Image::fromCompressedData(packet.view().data(),
                                                   packet.size(),
                                                   1, 1, inputPd,
                                                   packet.metadata());
        if(!jxsImage.isValid()) {
                setError(Error::IOError, "JpegXsVideoDecoder: fromCompressedData failed");
                return _lastError;
        }

        Image decoded = _codec.decode(jxsImage,
                                      static_cast<int>(_outputPd.id()));
        if(!decoded.isValid()) {
                setError(_codec.lastError().isError() ? _codec.lastError()
                                                     : Error::ConversionFailed,
                         _codec.lastErrorMessage().isEmpty()
                                 ? String("JpegXsVideoDecoder: decode failed")
                                 : _codec.lastErrorMessage());
                return _lastError;
        }
        _queue.push_back(std::move(decoded));
        return Error::Ok;
}

Image JpegXsVideoDecoder::receiveFrame() {
        if(_queue.empty()) return Image();
        Image img = std::move(_queue.front());
        _queue.pop_front();
        return img;
}

Error JpegXsVideoDecoder::flush() { return Error::Ok; }

Error JpegXsVideoDecoder::reset() {
        _queue.clear();
        _capacityWarned = false;
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Factory registration
// ---------------------------------------------------------------------------

namespace {

struct JpegXsVideoCodecRegistrar {
        JpegXsVideoCodecRegistrar() {
                VideoEncoder::registerEncoder("JPEG_XS",
                        []() -> VideoEncoder * { return new JpegXsVideoEncoder(); });
                VideoDecoder::registerDecoder("JPEG_XS",
                        []() -> VideoDecoder * { return new JpegXsVideoDecoder(); });

                VideoCodec base(VideoCodec::JPEG_XS);
                if(base.isValid()) {
                        VideoCodec::Data d = *base.data();
                        d.createEncoder = []() -> VideoEncoder * { return new JpegXsVideoEncoder(); };
                        d.createDecoder = []() -> VideoDecoder * { return new JpegXsVideoDecoder(); };
                        VideoCodec::registerData(std::move(d));
                }
        }
};

static JpegXsVideoCodecRegistrar _jpegXsVideoCodecRegistrar;

} // namespace

PROMEKI_NAMESPACE_END
