/**
 * @file      framebridge.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <atomic>
#include <cstring>
#include <thread>
#include <chrono>
#include <doctest/doctest.h>
#include <promeki/framebridge.h>
#include <promeki/framebridgemediaio.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaiofactory.h>
#include <promeki/url.h>

using namespace promeki;

// ============================================================================
// FrameBridge::isAborted reflects the cancel flag exposed to the
// FrameBridgeMediaIO read loop.
//
// The accessor exists so the MediaIO wrapper can break out of its
// poll-based read wait when MediaIO::close() trips abort() on the
// bridge.  Verify the basic contract: a fresh instance reports
// not-aborted, and abort() raises the flag observably.  The
// "openOutput / openInput clears the flag" semantic at the bottom of
// FrameBridge::openOutput / openInput is exercised separately by
// integration paths that stand up a full bridge; testing it here
// would require constructing a valid MediaDesc + standing up real
// shm / unix-socket peers, which is out of scope for this unit test.
// ============================================================================

TEST_CASE("FrameBridge::isAborted lifecycle on an unopened bridge") {
        FrameBridge br;
        // A fresh, never-opened bridge starts un-aborted; this is the
        // state @ref FrameBridgeMediaIO observes between construction
        // and the first open.
        CHECK_FALSE(br.isAborted());

        // abort() is documented as safe to call from any thread at any
        // time — including before openOutput / openInput.  Once raised
        // the flag latches until the next open clears it.
        br.abort();
        CHECK(br.isAborted());

        // Idempotent: a second abort() call leaves the flag set.
        br.abort();
        CHECK(br.isAborted());
}

// ============================================================================
// FrameBridgeFactory::urlToConfig accepts both pmfb://name and
// pmfb:///name authority forms.
//
// Two forms are valid because the second one is a common typo by users
// who reach for it by analogy with file:///.  Both should yield the
// same FrameBridgeName; deeper paths and the empty-name corner cases
// must be rejected so the bridge name stays a flat identifier safe to
// embed in shm names and socket basenames.
// ============================================================================

TEST_CASE("FrameBridgeFactory::urlToConfig accepts both authority forms") {
        const MediaIOFactory *f = MediaIOFactory::findByName(String("FrameBridge"));
        REQUIRE(f != nullptr);

        // Canonical: pmfb://test → FrameBridgeName="test"
        {
                Result<Url> parsed = Url::fromString(String("pmfb://test"));
                REQUIRE(parsed.second().isOk());
                MediaIO::Config cfg;
                Error           err = f->urlToConfig(parsed.first(), &cfg);
                CHECK(err.isOk());
                CHECK(cfg.getAs<String>(MediaConfig::FrameBridgeName, String()) == String("test"));
        }
        // file:///-style: pmfb:///test → FrameBridgeName="test"
        {
                Result<Url> parsed = Url::fromString(String("pmfb:///test"));
                REQUIRE(parsed.second().isOk());
                MediaIO::Config cfg;
                Error           err = f->urlToConfig(parsed.first(), &cfg);
                CHECK(err.isOk());
                CHECK(cfg.getAs<String>(MediaConfig::FrameBridgeName, String()) == String("test"));
        }
        // Trailing slash on the host form: pmfb://test/ → "test"
        {
                Result<Url> parsed = Url::fromString(String("pmfb://test/"));
                REQUIRE(parsed.second().isOk());
                MediaIO::Config cfg;
                Error           err = f->urlToConfig(parsed.first(), &cfg);
                CHECK(err.isOk());
                CHECK(cfg.getAs<String>(MediaConfig::FrameBridgeName, String()) == String("test"));
        }
        // Empty everywhere: pmfb:// → reject (no name).
        {
                Result<Url> parsed = Url::fromString(String("pmfb://"));
                REQUIRE(parsed.second().isOk());
                MediaIO::Config cfg;
                Error           err = f->urlToConfig(parsed.first(), &cfg);
                CHECK(err == Error::InvalidArgument);
        }
        // Nested path on the empty-host form: pmfb:///a/b → reject
        // (bridge names are flat identifiers, no slashes allowed).
        {
                Result<Url> parsed = Url::fromString(String("pmfb:///a/b"));
                REQUIRE(parsed.second().isOk());
                MediaIO::Config cfg;
                Error           err = f->urlToConfig(parsed.first(), &cfg);
                CHECK(err == Error::InvalidArgument);
        }
}
