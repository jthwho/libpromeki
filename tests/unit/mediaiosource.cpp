/**
 * @file      tests/unit/mediaiosource.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <atomic>
#include <chrono>
#include <thread>

#include <doctest/doctest.h>
#include <promeki/elapsedtimer.h>
#include <promeki/eventloop.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediaiosource.h>
#include <promeki/objectbase.tpp>
#include <promeki/pixelformat.h>
#include <promeki/videoformat.h>

using namespace promeki;

// ============================================================================
// MediaIOSource error-path coverage that the cache and request tests
// don't already exercise.  Cache invariants live in
// tests/unit/mediaioreadcache.cpp.  The happy-path read pipeline is
// covered by tests/unit/mediaio.cpp.  These tests focus on:
//   - readFrame short-circuiting before going down to the cache,
//   - readFrame on a closing MediaIO,
//   - cancelPending behavior on an unopened MediaIO.
// ============================================================================

namespace {

MediaIO::Config tpgConfig() {
        MediaIO::Config cfg = MediaIOFactory::defaultConfig("TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte720p59_94));
        cfg.set(MediaConfig::VideoEnabled, true);
        cfg.set(MediaConfig::AudioEnabled, false);
        cfg.set(MediaConfig::TimecodeEnabled, false);
        cfg.set(MediaConfig::VideoBurnEnabled, false);
        return cfg;
}

} // namespace

// Note: there is no "pre-open" surface to test on the source itself —
// MediaIOSource ports are created by the backend during open, so
// `source(0)` returns nullptr before open completes.  The post-close
// path is the closest analogue: the port still exists, but isOpen()
// is false.

// ============================================================================
// cancelPending on a closed MediaIO returns 0 (no-op).
//
// The source-side cancel forwards to the cache, but only if the
// owning MediaIO is open.  This prevents accidentally walking into
// an in-flight Open command.
// ============================================================================

TEST_CASE("MediaIOSource::cancelPending returns 0 when MediaIO is closed") {
        MediaIO *io = MediaIO::create(tpgConfig());
        REQUIRE(io != nullptr);
        REQUIRE(io->open().wait().isOk());
        // Stash the source ptr while open, then close.
        MediaIOSource *src = io->source(0);
        REQUIRE(src != nullptr);
        REQUIRE(io->close().wait().isOk());

        // Drain anything the cache may still have so the post-close
        // call sees the same "no work pending" state across runs.
        for (int i = 0; i < 32; ++i) {
                if (src->readFrame().wait() == Error::EndOfFile) break;
        }

        size_t dropped = src->cancelPending();
        CHECK(dropped == 0);

        delete io;
}

// ============================================================================
// Cache-drain-before-isOpen-check: post-close, frames already in the
// cache must be observable via readFrame even though isOpen() now
// returns false.
//
// This is the bug-fix path that motivated the refactor — without
// the drain-first rule, a consumer racing close() would observe
// NotOpen and misclassify the shutdown as an error.  Here we don't
// race close (impossible to do reliably in a test), but we can
// confirm at least one valid frame can come out of a closed source.
// The synthetic EOS that close pushes counts as one such "frame".
// ============================================================================

TEST_CASE("MediaIOSource::readFrame drains cache before isOpen check (post-close)") {
        MediaIO *io = MediaIO::create(tpgConfig());
        REQUIRE(io != nullptr);
        REQUIRE(io->open().wait().isOk());

        // Pull one frame so the cache definitely had a chance to
        // top up.
        REQUIRE(io->source(0)->readFrame().wait().isOk());

        // Close — synthetic EOS gets pushed into the cache by
        // MediaIO::completeCommand for the Close command.
        REQUIRE(io->close().wait().isOk());
        REQUIRE_FALSE(io->isOpen());

        // Drain.  At least one of the resulting requests must
        // resolve with EndOfFile (the synthetic EOS).  Real reads
        // that completed before close also surface here, with Ok
        // or Cancelled.  We must NOT observe NotOpen until the
        // cache drains fully.
        bool sawEof = false;
        bool sawNotOpenWithFramesLeft = false;
        for (int i = 0; i < 32 && !sawEof; ++i) {
                MediaIORequest req = io->source(0)->readFrame();
                Error          e = req.wait();
                if (e == Error::EndOfFile) {
                        sawEof = true;
                } else if (e == Error::NotOpen) {
                        // NotOpen arrived before EOS — this would
                        // be the regression.
                        sawNotOpenWithFramesLeft = true;
                        break;
                }
                // Ok / Cancelled / TryAgain are all fine — they
                // mean we drained a real cmd.
        }
        CHECK(sawEof);
        CHECK_FALSE(sawNotOpenWithFramesLeft);

        delete io;
}

// ============================================================================
// On a freshly-opened source, frameAvailable / readyReads /
// pendingReads start at zero and only become non-zero after the
// first read submission.
//
// (We cannot test pre-open: the port doesn't exist before open.)
// ============================================================================

TEST_CASE("MediaIOSource: status accessors start at zero on a freshly-opened source") {
        MediaIO *io = MediaIO::create(tpgConfig());
        REQUIRE(io != nullptr);
        REQUIRE(io->open().wait().isOk());

        // Right after open the cache hasn't been touched yet.  No
        // prefetch is performed during open itself.
        CHECK_FALSE(io->source(0)->frameAvailable());
        CHECK(io->source(0)->readyReads() == 0);
        CHECK(io->source(0)->pendingReads() == 0);

        (void)io->close().wait();
        delete io;
}
