/**
 * @file      mediasource.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/mediasource.h>

using namespace promeki;

// ============================================================================
// Default construction
// ============================================================================

TEST_CASE("MediaSource_Default") {
    MediaSource source;
    CHECK(source.name().isEmpty());
    CHECK(source.contentHint() == ContentNone);
    CHECK(source.node() == nullptr);
    CHECK(!source.isConnected());
    CHECK(source.connectedSinks().isEmpty());
}

// ============================================================================
// Construction with parameters
// ============================================================================

TEST_CASE("MediaSource_Construct") {
    MediaSource source("video_out", ContentVideo);
    CHECK(source.name() == "video_out");
    CHECK(source.contentHint() == ContentVideo);
}

// ============================================================================
// Connect and disconnect
// ============================================================================

TEST_CASE("MediaSource_ConnectDisconnect") {
    MediaSource source("output", ContentNone);
    auto sink = MediaSink::Ptr::create("input", ContentNone);

    source.connect(sink);
    CHECK(source.isConnected());
    CHECK(source.connectedSinks().size() == 1);

    source.disconnect(sink);
    CHECK(!source.isConnected());
    CHECK(source.connectedSinks().isEmpty());
}

TEST_CASE("MediaSource_ConnectMultiple") {
    MediaSource source("output", ContentNone);
    auto sink1 = MediaSink::Ptr::create("input1", ContentNone);
    auto sink2 = MediaSink::Ptr::create("input2", ContentNone);

    source.connect(sink1);
    source.connect(sink2);
    CHECK(source.connectedSinks().size() == 2);

    source.disconnectAll();
    CHECK(!source.isConnected());
}

// ============================================================================
// Deliver
// ============================================================================

TEST_CASE("MediaSource_Deliver") {
    MediaSource source("output", ContentNone);
    auto sink = MediaSink::Ptr::create("input", ContentNone);
    source.connect(sink);

    source.deliver(Frame::Ptr::create());
    CHECK(sink->queueSize() == 1);

    source.deliver(Frame::Ptr::create());
    CHECK(sink->queueSize() == 2);
}

TEST_CASE("MediaSource_DeliverFanOut") {
    MediaSource source("output", ContentNone);
    auto sink1 = MediaSink::Ptr::create("input1", ContentNone);
    auto sink2 = MediaSink::Ptr::create("input2", ContentNone);
    source.connect(sink1);
    source.connect(sink2);

    source.deliver(Frame::Ptr::create());
    CHECK(sink1->queueSize() == 1);
    CHECK(sink2->queueSize() == 1);
}

TEST_CASE("MediaSource_DeliverNoSinks") {
    MediaSource source("output", ContentNone);
    // Should not crash
    source.deliver(Frame::Ptr::create());
}

// ============================================================================
// sinksReadyForFrame
// ============================================================================

TEST_CASE("MediaSource_SinksReadyForFrame") {
    MediaSource source("output", ContentNone);
    auto sink = MediaSink::Ptr::create("input", ContentNone);
    sink->setMaxQueueDepth(2);
    source.connect(sink);

    CHECK(source.sinksReadyForFrame());

    sink->push(Frame::Ptr::create());
    CHECK(source.sinksReadyForFrame());

    sink->push(Frame::Ptr::create());
    CHECK(!source.sinksReadyForFrame());
}

TEST_CASE("MediaSource_SinksReadyNoSinks") {
    MediaSource source("output", ContentNone);
    // No sinks connected: not ready (prevents source nodes from spinning)
    CHECK(!source.sinksReadyForFrame());
}

// ============================================================================
// SharedPtr construction
// ============================================================================

TEST_CASE("MediaSource_SharedPtr") {
    auto source = MediaSource::Ptr::create("video", ContentVideo);
    CHECK(source.isValid());
    CHECK(source->name() == "video");
    CHECK(source->contentHint() == ContentVideo);
}

// ============================================================================
// Destructor disconnects sinks
// ============================================================================

TEST_CASE("MediaSource_DestructorDisconnects") {
    auto sink = MediaSink::Ptr::create("input", ContentNone);
    {
        MediaSource source("output", ContentNone);
        source.connect(sink);
    }
    // After source destruction, sink's back-pointer should be cleared
    // (no crash on further operations)
    sink->push(Frame::Ptr::create());
    CHECK(sink->queueSize() == 1);
}
