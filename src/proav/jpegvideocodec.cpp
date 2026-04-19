/**
 * @file      jpegvideocodec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/jpegvideocodec.h>
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
// JpegVideoEncoder
// ---------------------------------------------------------------------------

JpegVideoEncoder::~JpegVideoEncoder() = default;

String JpegVideoEncoder::name() const { return "JPEG"; }

String JpegVideoEncoder::description() const {
        return "JPEG VideoEncoder (libjpeg-turbo via JpegImageCodec)";
}

PixelDesc JpegVideoEncoder::outputPixelDesc() const {
        // The actual emitted MediaPacket carries whatever JPEG variant
        // the underlying JpegImageCodec picked for the given input
        // (matching subsampling / matrix / range), so this accessor
        // just reports a representative member of the JPEG family.
        // Callers who want a specific subtype can override via
        // MediaConfig::OutputPixelDesc.
        if(_outputPd.isValid()) return _outputPd;
        return PixelDesc(PixelDesc::JPEG_RGB8_sRGB);
}

List<int> JpegVideoEncoder::supportedInputList() {
        // Mirror the input-format coverage classifyYCbCr / encodeXxx
        // paths in JpegImageCodec already implement.
        return {
                static_cast<int>(PixelDesc::RGB8_sRGB),
                static_cast<int>(PixelDesc::RGBA8_sRGB),
                static_cast<int>(PixelDesc::YUV8_422_Rec709),
                static_cast<int>(PixelDesc::YUV8_422_UYVY_Rec709),
                static_cast<int>(PixelDesc::YUV8_422_Planar_Rec709),
                static_cast<int>(PixelDesc::YUV8_420_Planar_Rec709),
                static_cast<int>(PixelDesc::YUV8_420_SemiPlanar_Rec709),
                static_cast<int>(PixelDesc::YUV8_422_Rec601),
                static_cast<int>(PixelDesc::YUV8_422_UYVY_Rec601),
                static_cast<int>(PixelDesc::YUV8_420_Planar_Rec601),
                static_cast<int>(PixelDesc::YUV8_420_SemiPlanar_Rec601),
                static_cast<int>(PixelDesc::YUV8_422_Rec709_Full),
                static_cast<int>(PixelDesc::YUV8_422_Rec601_Full),
                static_cast<int>(PixelDesc::YUV8_420_Planar_Rec709_Full),
                static_cast<int>(PixelDesc::YUV8_420_Planar_Rec601_Full),
        };
}

List<int> JpegVideoEncoder::supportedInputs() const {
        return supportedInputList();
}

void JpegVideoEncoder::configure(const MediaConfig &config) {
        _codec.configure(config);
        _outputPd = config.getAs<PixelDesc>(MediaConfig::OutputPixelDesc, PixelDesc());
        _capacity = config.getAs<int>(MediaConfig::Capacity, 8);
        if(_capacity < 1) _capacity = 1;
}

Error JpegVideoEncoder::submitFrame(const Image &frame, const MediaTimeStamp &pts) {
        clearError();
        if(!frame.isValid()) {
                setError(Error::Invalid, "JpegVideoEncoder: invalid frame");
                return _lastError;
        }
        if(static_cast<int>(_queue.size()) >= _capacity && !_capacityWarned) {
                promekiWarn("JpegVideoEncoder: output queue exceeded capacity (%d)",
                            _capacity);
                _capacityWarned = true;
        }

        Image encoded = _codec.encode(frame);
        if(!encoded.isValid()) {
                setError(_codec.lastError().isError() ? _codec.lastError()
                                                     : Error::ConversionFailed,
                         _codec.lastErrorMessage().isEmpty()
                                 ? String("JpegVideoEncoder: encode failed")
                                 : _codec.lastErrorMessage());
                return _lastError;
        }

        // Wrap the encoded plane buffer as a MediaPacket — same
        // BufferView semantics we use elsewhere.  Every JPEG bitstream
        // is independently decodable so the packet is always a
        // keyframe.
        auto pkt = MediaPacket::Ptr::create(encoded.plane(0), encoded.pixelDesc());
        pkt.modify()->setPts(pts);
        pkt.modify()->setDts(pts);
        pkt.modify()->addFlag(MediaPacket::Keyframe);
        _queue.push_back(std::move(pkt));
        return Error::Ok;
}

MediaPacket::Ptr JpegVideoEncoder::receivePacket() {
        if(_queue.empty()) return MediaPacket::Ptr();
        auto pkt = std::move(_queue.front());
        _queue.pop_front();
        return pkt;
}

Error JpegVideoEncoder::flush() {
        // JPEG is intra-only: every submitFrame already produced its
        // packet, so flush has nothing to do beyond returning Ok.
        return Error::Ok;
}

Error JpegVideoEncoder::reset() {
        _queue.clear();
        _capacityWarned = false;
        return Error::Ok;
}

void JpegVideoEncoder::requestKeyframe() {
        // No-op: every JPEG packet is a keyframe by definition.
}

// ---------------------------------------------------------------------------
// JpegVideoDecoder
// ---------------------------------------------------------------------------

JpegVideoDecoder::~JpegVideoDecoder() = default;

String JpegVideoDecoder::name() const { return "JPEG"; }

String JpegVideoDecoder::description() const {
        return "JPEG VideoDecoder (libjpeg-turbo via JpegImageCodec)";
}

PixelDesc JpegVideoDecoder::inputPixelDesc() const {
        // JPEG covers many compressed PixelDesc variants
        // (RGB / RGBA / YCbCr × matrix × range × subsampling).  Report
        // a representative entry; the actual MediaPacket carries the
        // exact variant via its own pixelDesc().
        return PixelDesc(PixelDesc::JPEG_RGB8_sRGB);
}

List<int> JpegVideoDecoder::supportedOutputs() const {
        return {
                static_cast<int>(PixelDesc::RGB8_sRGB),
                static_cast<int>(PixelDesc::RGBA8_sRGB),
                static_cast<int>(PixelDesc::YUV8_422_Rec709),
                static_cast<int>(PixelDesc::YUV8_422_UYVY_Rec709),
                static_cast<int>(PixelDesc::YUV8_422_Planar_Rec709),
                static_cast<int>(PixelDesc::YUV8_420_Planar_Rec709),
                static_cast<int>(PixelDesc::YUV8_420_SemiPlanar_Rec709),
        };
}

void JpegVideoDecoder::configure(const MediaConfig &config) {
        _codec.configure(config);
        _outputPd = config.getAs<PixelDesc>(MediaConfig::OutputPixelDesc, PixelDesc());
        _capacity = config.getAs<int>(MediaConfig::Capacity, 8);
        if(_capacity < 1) _capacity = 1;
}

Error JpegVideoDecoder::submitPacket(const MediaPacket &packet) {
        clearError();
        if(!packet.isValid() || packet.size() == 0) {
                setError(Error::Invalid, "JpegVideoDecoder: empty packet");
                return _lastError;
        }
        if(static_cast<int>(_queue.size()) >= _capacity && !_capacityWarned) {
                promekiWarn("JpegVideoDecoder: output queue exceeded capacity (%d)",
                            _capacity);
                _capacityWarned = true;
        }

        // Wrap the packet bytes as a compressed Image.  Width / height
        // here are placeholders — JpegImageCodec::decode parses the
        // real dimensions out of the JPEG header itself; we only need
        // a non-zero size so Image::fromCompressedData allocates the
        // backing buffer correctly.  The PixelDesc on the synthetic
        // input mostly matters for its decodeTargets list, which we
        // override with _outputPd.id() below anyway.
        const PixelDesc inputPd = packet.pixelDesc().isValid()
                ? packet.pixelDesc()
                : PixelDesc(PixelDesc::JPEG_RGB8_sRGB);
        Image jpegImage = Image::fromCompressedData(packet.view().data(),
                                                    packet.size(),
                                                    1, 1, inputPd,
                                                    packet.metadata());
        if(!jpegImage.isValid()) {
                setError(Error::IOError, "JpegVideoDecoder: fromCompressedData failed");
                return _lastError;
        }

        // _outputPd.id() of Invalid (== 0) tells JpegImageCodec to use
        // its first registered decodeTarget for the input PixelDesc,
        // which matches the codec's pre-VideoCodec behaviour.
        Image decoded = _codec.decode(jpegImage,
                                      static_cast<int>(_outputPd.id()));
        if(!decoded.isValid()) {
                setError(_codec.lastError().isError() ? _codec.lastError()
                                                     : Error::ConversionFailed,
                         _codec.lastErrorMessage().isEmpty()
                                 ? String("JpegVideoDecoder: decode failed")
                                 : _codec.lastErrorMessage());
                return _lastError;
        }
        _queue.push_back(std::move(decoded));
        return Error::Ok;
}

Image JpegVideoDecoder::receiveFrame() {
        if(_queue.empty()) return Image();
        Image img = std::move(_queue.front());
        _queue.pop_front();
        return img;
}

Error JpegVideoDecoder::flush() { return Error::Ok; }

Error JpegVideoDecoder::reset() {
        _queue.clear();
        _capacityWarned = false;
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Factory registration: VideoEncoder / VideoDecoder string registry +
// VideoCodec::JPEG factory hooks.
// ---------------------------------------------------------------------------

namespace {

struct JpegVideoCodecRegistrar {
        JpegVideoCodecRegistrar() {
                VideoEncoder::registerEncoder("JPEG",
                        []() -> VideoEncoder * { return new JpegVideoEncoder(); });
                VideoDecoder::registerDecoder("JPEG",
                        []() -> VideoDecoder * { return new JpegVideoDecoder(); });

                // Wire the typed VideoCodec::JPEG entry's factory hooks
                // by re-registering the existing Data record with the
                // factories filled in.  registerData() overwrites the
                // entry under the same ID so the well-known codec keeps
                // its name / desc / fourccs / compressedPixelDescs while
                // gaining the pointers our wrappers provide.
                VideoCodec base(VideoCodec::JPEG);
                if(base.isValid()) {
                        VideoCodec::Data d = *base.data();
                        d.createEncoder = []() -> VideoEncoder * { return new JpegVideoEncoder(); };
                        d.createDecoder = []() -> VideoDecoder * { return new JpegVideoDecoder(); };
                        d.encoderSupportedInputs = JpegVideoEncoder::supportedInputList();
                        VideoCodec::registerData(std::move(d));
                }
        }
};

static JpegVideoCodecRegistrar _jpegVideoCodecRegistrar;

} // namespace

PROMEKI_NAMESPACE_END
