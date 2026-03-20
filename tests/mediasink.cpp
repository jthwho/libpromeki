/**
 * @file      mediasink.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/proav/mediasink.h>

using namespace promeki;

// ============================================================================
// Default construction
// ============================================================================

TEST_CASE("MediaSink_Default") {
    MediaSink sink;
    CHECK(sink.name().isEmpty());
    CHECK(sink.contentHint() == ContentNone);
    CHECK(sink.node() == nullptr);
    CHECK(sink.queueSize() == 0);
    CHECK(sink.maxQueueDepth() == 4);
    CHECK(sink.canAcceptFrame());
}

// ============================================================================
// Construction with parameters
// ============================================================================

TEST_CASE("MediaSink_Construct") {
    MediaSink sink("video_in", ContentVideo);
    CHECK(sink.name() == "video_in");
    CHECK(sink.contentHint() == ContentVideo);
}

// ============================================================================
// Push and pop
// ============================================================================

TEST_CASE("MediaSink_PushAndPop") {
    MediaSink sink("input", ContentNone);
    CHECK(sink.queueSize() == 0);

    sink.push(Frame::Ptr::create());
    sink.push(Frame::Ptr::create());
    CHECK(sink.queueSize() == 2);

    Frame::Ptr out;
    CHECK(sink.popOrFail(out));
    CHECK(out.isValid());
    CHECK(sink.queueSize() == 1);

    CHECK(sink.popOrFail(out));
    CHECK(sink.queueSize() == 0);

    CHECK(!sink.popOrFail(out));
}

// ============================================================================
// canAcceptFrame default behavior
// ============================================================================

TEST_CASE("MediaSink_CanAcceptFrame") {
    MediaSink sink("input", ContentNone);
    sink.setMaxQueueDepth(2);
    CHECK(sink.maxQueueDepth() == 2);

    CHECK(sink.canAcceptFrame());
    sink.push(Frame::Ptr::create());
    CHECK(sink.canAcceptFrame());
    sink.push(Frame::Ptr::create());
    CHECK(!sink.canAcceptFrame());

    // Pop one, should accept again
    Frame::Ptr out;
    sink.popOrFail(out);
    CHECK(sink.canAcceptFrame());
}

// ============================================================================
// canAcceptFrame override
// ============================================================================

class AlwaysFullSink : public MediaSink {
        public:
                AlwaysFullSink() : MediaSink("full", ContentNone) { }
                bool canAcceptFrame() const override { return false; }
};

TEST_CASE("MediaSink_CanAcceptFrameOverride") {
    AlwaysFullSink sink;
    CHECK(!sink.canAcceptFrame());
    CHECK(sink.queueSize() == 0);
}

// ============================================================================
// clearQueue
// ============================================================================

TEST_CASE("MediaSink_ClearQueue") {
    MediaSink sink("input", ContentNone);
    sink.push(Frame::Ptr::create());
    sink.push(Frame::Ptr::create());
    CHECK(sink.queueSize() == 2);
    sink.clearQueue();
    CHECK(sink.queueSize() == 0);
}

// ============================================================================
// SharedPtr construction
// ============================================================================

TEST_CASE("MediaSink_SharedPtr") {
    auto sink = MediaSink::Ptr::create("video", ContentVideo);
    CHECK(sink.isValid());
    CHECK(sink->name() == "video");
    CHECK(sink->contentHint() == ContentVideo);
}

// ============================================================================
// Setters
// ============================================================================

TEST_CASE("MediaSink_Setters") {
    MediaSink sink;
    sink.setName("audio");
    CHECK(sink.name() == "audio");
    sink.setContentHint(ContentAudio);
    CHECK(sink.contentHint() == ContentAudio);
    sink.setMaxQueueDepth(8);
    CHECK(sink.maxQueueDepth() == 8);
}

// ============================================================================
// ContentHint bitwise operators
// ============================================================================

TEST_CASE("ContentHint_BitwiseOperators") {
    ContentHint combined = ContentVideo | ContentAudio;
    CHECK(combined != ContentNone);
    CHECK((combined & ContentVideo) == ContentVideo);
    CHECK((combined & ContentAudio) == ContentAudio);
    CHECK((ContentVideo & ContentAudio) == ContentNone);
}
