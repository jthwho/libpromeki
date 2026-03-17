/**
 * @file      medialink.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/proav/medialink.h>
#include <promeki/proav/medianode.h>

using namespace promeki;

// ============================================================================
// Helper nodes for testing
// ============================================================================

class LinkTestSource : public MediaNode {
        PROMEKI_OBJECT(LinkTestSource, MediaNode)
        public:
                LinkTestSource(MediaPort::MediaType type = MediaPort::Frame) : MediaNode() {
                        auto port = MediaPort::Ptr::create("output", MediaPort::Output, type);
                        addOutputPort(port);
                }
                void process() override { }
};

class LinkTestSink : public MediaNode {
        PROMEKI_OBJECT(LinkTestSink, MediaNode)
        public:
                LinkTestSink(MediaPort::MediaType type = MediaPort::Frame) : MediaNode() {
                        auto port = MediaPort::Ptr::create("input", MediaPort::Input, type);
                        addInputPort(port);
                }
                void process() override { }
};

// ============================================================================
// Default construction
// ============================================================================

TEST_CASE("MediaLink_Default") {
    MediaLink link;
    CHECK(!link.isValid());
    CHECK(link.sourceNode() == nullptr);
    CHECK(link.sinkNode() == nullptr);
}

// ============================================================================
// Construction with ports
// ============================================================================

TEST_CASE("MediaLink_Construct") {
    LinkTestSource source;
    LinkTestSink sink;
    MediaLink link(source.outputPort(0), sink.inputPort(0));
    CHECK(link.isValid());
    CHECK(link.sourceNode() == &source);
    CHECK(link.sinkNode() == &sink);
}

// ============================================================================
// Frame-to-Frame delivery
// ============================================================================

TEST_CASE("MediaLink_DeliverFrameToFrame") {
    LinkTestSource source(MediaPort::Frame);
    LinkTestSink sink(MediaPort::Frame);
    MediaLink link(source.outputPort(0), sink.inputPort(0));

    Frame::Ptr frame = Frame::Ptr::create();
    Error err = link.deliver(frame);
    CHECK(err == Error::Ok);
    CHECK(sink.queuedFrameCount() == 1);
}

// ============================================================================
// Frame-to-Image extraction
// ============================================================================

TEST_CASE("MediaLink_DeliverFrameToImage") {
    LinkTestSource source(MediaPort::Frame);
    LinkTestSink sink(MediaPort::Image);
    MediaLink link(source.outputPort(0), sink.inputPort(0));
    CHECK(link.isValid());

    // Create a frame with an image
    Frame::Ptr frame = Frame::Ptr::create();
    Image::Ptr img = Image::Ptr::create();
    frame.modify()->imageList().pushToBack(img);

    Error err = link.deliver(frame);
    CHECK(err == Error::Ok);
    CHECK(sink.queuedFrameCount() == 1);
}

// ============================================================================
// Frame-to-Audio extraction
// ============================================================================

TEST_CASE("MediaLink_DeliverFrameToAudio") {
    LinkTestSource source(MediaPort::Frame);
    LinkTestSink sink(MediaPort::Audio);
    MediaLink link(source.outputPort(0), sink.inputPort(0));
    CHECK(link.isValid());

    Frame::Ptr frame = Frame::Ptr::create();
    Audio::Ptr audio = Audio::Ptr::create();
    frame.modify()->audioList().pushToBack(audio);

    Error err = link.deliver(frame);
    CHECK(err == Error::Ok);
    CHECK(sink.queuedFrameCount() == 1);
}

// ============================================================================
// Incompatible types
// ============================================================================

TEST_CASE("MediaLink_IncompatiblePorts") {
    LinkTestSource source(MediaPort::Image);
    LinkTestSink sink(MediaPort::Frame);
    MediaLink link(source.outputPort(0), sink.inputPort(0));
    CHECK(!link.isValid());
}

TEST_CASE("MediaLink_DeliverToInvalid") {
    MediaLink link;
    Frame::Ptr frame = Frame::Ptr::create();
    Error err = link.deliver(frame);
    CHECK(err == Error::Invalid);
}

// ============================================================================
// Multiple deliveries
// ============================================================================

TEST_CASE("MediaLink_MultipleDeliveries") {
    LinkTestSource source;
    LinkTestSink sink;
    MediaLink link(source.outputPort(0), sink.inputPort(0));

    for(int i = 0; i < 5; i++) {
        Error err = link.deliver(Frame::Ptr::create());
        CHECK(err == Error::Ok);
    }
    CHECK(sink.queuedFrameCount() == 5);
}

// ============================================================================
// Direct Image-to-Image connection
// ============================================================================

TEST_CASE("MediaLink_ImageToImage") {
    LinkTestSource source(MediaPort::Image);
    LinkTestSink sink(MediaPort::Image);
    MediaLink link(source.outputPort(0), sink.inputPort(0));
    CHECK(link.isValid());

    Frame::Ptr frame = Frame::Ptr::create();
    Error err = link.deliver(frame);
    CHECK(err == Error::Ok);
    CHECK(sink.queuedFrameCount() == 1);
}

// ============================================================================
// Direct Audio-to-Audio connection
// ============================================================================

TEST_CASE("MediaLink_AudioToAudio") {
    LinkTestSource source(MediaPort::Audio);
    LinkTestSink sink(MediaPort::Audio);
    MediaLink link(source.outputPort(0), sink.inputPort(0));
    CHECK(link.isValid());

    Frame::Ptr frame = Frame::Ptr::create();
    Error err = link.deliver(frame);
    CHECK(err == Error::Ok);
    CHECK(sink.queuedFrameCount() == 1);
}

// ============================================================================
// Direct Encoded-to-Encoded connection
// ============================================================================

TEST_CASE("MediaLink_EncodedToEncoded") {
    LinkTestSource source(MediaPort::Encoded);
    LinkTestSink sink(MediaPort::Encoded);
    MediaLink link(source.outputPort(0), sink.inputPort(0));
    CHECK(link.isValid());

    Frame::Ptr frame = Frame::Ptr::create();
    Error err = link.deliver(frame);
    CHECK(err == Error::Ok);
    CHECK(sink.queuedFrameCount() == 1);
}

// ============================================================================
// Metadata propagation during Frame->Image extraction
// ============================================================================

TEST_CASE("MediaLink_FrameToImageMetadataPropagation") {
    LinkTestSource source(MediaPort::Frame);
    LinkTestSink sink(MediaPort::Image);
    MediaLink link(source.outputPort(0), sink.inputPort(0));

    Frame::Ptr frame = Frame::Ptr::create();
    frame.modify()->metadata().set(Metadata::Artist, String("TestArtist"));
    Image::Ptr img = Image::Ptr::create();
    frame.modify()->imageList().pushToBack(img);

    Error err = link.deliver(frame);
    CHECK(err == Error::Ok);
    CHECK(sink.queuedFrameCount() == 1);
}

// ============================================================================
// Metadata propagation during Frame->Audio extraction
// ============================================================================

TEST_CASE("MediaLink_FrameToAudioMetadataPropagation") {
    LinkTestSource source(MediaPort::Frame);
    LinkTestSink sink(MediaPort::Audio);
    MediaLink link(source.outputPort(0), sink.inputPort(0));

    Frame::Ptr frame = Frame::Ptr::create();
    frame.modify()->metadata().set(Metadata::Artist, String("TestArtist"));
    Audio::Ptr audio = Audio::Ptr::create();
    frame.modify()->audioList().pushToBack(audio);

    Error err = link.deliver(frame);
    CHECK(err == Error::Ok);
    CHECK(sink.queuedFrameCount() == 1);
}

// ============================================================================
// Source and sink accessors
// ============================================================================

TEST_CASE("MediaLink_SourceAndSinkAccessors") {
    LinkTestSource source;
    LinkTestSink sink;
    MediaLink link(source.outputPort(0), sink.inputPort(0));

    CHECK(link.source()->name() == "output");
    CHECK(link.sink()->name() == "input");
    CHECK(link.source()->direction() == MediaPort::Output);
    CHECK(link.sink()->direction() == MediaPort::Input);
}
