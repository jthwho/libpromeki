/**
 * @file      jpegencodernode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/jpegencodernode.h>
#include <promeki/jpegimagecodec.h>
#include <promeki/medianodeconfig.h>
#include <promeki/frame.h>
#include <promeki/image.h>
#include <promeki/metadata.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_NODE(JpegEncoderNode)

JpegEncoderNode::JpegEncoderNode(ObjectBase *parent) : MediaNode(parent) {
        setName("JpegEncoderNode");
        addSink(MediaSink::Ptr::create("input", ContentVideo));
        addSource(MediaSource::Ptr::create("output", ContentVideo));
}

JpegEncoderNode::~JpegEncoderNode() {
        delete _codec;
}

MediaNodeConfig JpegEncoderNode::defaultConfig() const {
        MediaNodeConfig cfg("JpegEncoderNode", "");
        cfg.set("Quality", 85);
        cfg.set("Subsampling", "422");
        return cfg;
}

BuildResult JpegEncoderNode::build(const MediaNodeConfig &config) {
        BuildResult result;
        if(state() != Idle) {
                result.addError("Node is not in Idle state");
                return result;
        }

        delete _codec;
        _codec = new JpegImageCodec();

        int quality = config.get("Quality", 85).get<int>();
        if(quality < 1 || quality > 100) {
                result.addWarning("Quality clamped to 1-100 range");
        }
        _codec->setQuality(quality);

        String subsampStr = config.get("Subsampling", "422").get<String>();
        if(subsampStr == "444") {
                _codec->setSubsampling(JpegImageCodec::Subsampling444);
        } else if(subsampStr == "420") {
                _codec->setSubsampling(JpegImageCodec::Subsampling420);
        } else {
                _codec->setSubsampling(JpegImageCodec::Subsampling422);
        }

        _framesEncoded = 0;
        _totalCompressedBytes = 0;
        _totalUncompressedBytes = 0;

        setState(Configured);
        return result;
}

void JpegEncoderNode::processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) {
        (void)inputIndex;
        (void)deliveries;

        if(!frame.isValid()) return;
        if(frame->imageList().isEmpty()) return;

        Image::Ptr img = frame->imageList()[0];
        Image encoded = _codec->encode(*img);
        if(!encoded.isValid()) {
                emitError(_codec->lastErrorMessage());
                return;
        }

        // Track stats
        size_t uncompressedSize = img->lineStride() * img->height();
        size_t compressedSize = encoded.compressedSize();
        _framesEncoded++;
        _totalCompressedBytes += compressedSize;
        _totalUncompressedBytes += uncompressedSize;

        // Update thread-safe stats snapshot
        {
                Mutex::Locker lock(_statsMutex);
                _statsFramesEncoded = _framesEncoded;
                _statsTotalCompressedBytes = _totalCompressedBytes;
                _statsTotalUncompressedBytes = _totalUncompressedBytes;
        }

        // Replace frame contents with encoded output, preserving metadata
        Metadata md = frame->metadata();
        frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(encoded));
        frame.modify()->metadata() = md;
}

Map<String, Variant> JpegEncoderNode::extendedStats() const {
        Mutex::Locker lock(_statsMutex);
        Map<String, Variant> ret;
        ret.insert("FramesEncoded", Variant(_statsFramesEncoded));
        if(_statsFramesEncoded > 0) {
                ret.insert("AvgCompressedSize", Variant(_statsTotalCompressedBytes / _statsFramesEncoded));
                if(_statsTotalCompressedBytes > 0) {
                        double ratio = (double)_statsTotalUncompressedBytes / (double)_statsTotalCompressedBytes;
                        ret.insert("CompressionRatio", Variant(ratio));
                }
        }
        return ret;
}

PROMEKI_NAMESPACE_END
