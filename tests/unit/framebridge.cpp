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
