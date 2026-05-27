/**
 * @file      tests/unit/mediaioparams.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/map.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaioparams.h>
#include <promeki/mediaiorequest.h>
#include <promeki/variant.h>

#include "mediaio_test_helpers.h"

using namespace promeki;
using promeki::tests::InlineTestMediaIO;

namespace {

        // Well-known param ids used across the cases below.
        const MediaIOParamsID Gain{"Gain"};
        const MediaIOParamsID Mode{"Mode"};
        const MediaIOParamsID Unknown{"Unknown"};

        // Builds an InlineTestMediaIO whose getParam/setParam hooks are
        // backed by a simple in-memory store, so set/get ordering is
        // observable.  Unknown ids resolve NotSupported (the default
        // hook fall-through).
        class StoreBackedIO : public InlineTestMediaIO {
                        PROMEKI_OBJECT(StoreBackedIO, InlineTestMediaIO)
                public:
                        Map<uint64_t, Variant> store;

                        StoreBackedIO() {
                                onGetParam = [this](MediaIOParamsID id, Variant &out) -> Error {
                                        if (!store.contains(id.id())) return Error::NotSupported;
                                        out = store.value(id.id());
                                        return Error::Ok;
                                };
                                onSetParam = [this](MediaIOParamsID id, const Variant &value) -> Error {
                                        if (id == Unknown) return Error::NotSupported;
                                        store.insert(id.id(), value);
                                        return Error::Ok;
                                };
                        }
        };

} // namespace

// ============================================================================
// Block builders are pure value plumbing — order, op, value, and the
// atomic flag round-trip exactly as appended.
// ============================================================================

TEST_CASE("MediaIOParams builders preserve order, op, and value") {
        MediaIOParams block;
        CHECK(block.isEmpty());
        CHECK(block.count() == 0);
        CHECK_FALSE(block.isAtomic());

        block.get(Gain).set(Mode, 7).get(Mode);
        REQUIRE(block.count() == 3);

        CHECK(block.action(0).id == Gain);
        CHECK(block.action(0).op == MediaIOParamOp::Get);

        CHECK(block.action(1).id == Mode);
        CHECK(block.action(1).op == MediaIOParamOp::Set);
        CHECK(block.action(1).value.get<int32_t>() == 7);

        CHECK(block.action(2).op == MediaIOParamOp::Get);

        block.setAtomic(true);
        CHECK(block.isAtomic());
}

// ============================================================================
// A Get fills the action's value; the result() pairs value with error.
// ============================================================================

TEST_CASE("MediaIOParams Get fills the value slot") {
        StoreBackedIO io;
        io.store.insert(Gain.id(), Variant(42));
        REQUIRE(io.open().wait().isOk());

        MediaIOParams block;
        block.get(Gain);
        auto req = io.sendParams(block);
        REQUIRE(req.wait().isOk());

        const MediaIOParams &done = req.commandAs<MediaIOCommandParams>()->block;
        CHECK(done.action(0).error.isOk());
        CHECK(done.action(0).value.get<int32_t>() == 42);

        Result<Variant> r = done.result(0);
        CHECK(r.second().isOk());
        CHECK(r.first().get<int32_t>() == 42);
}

// ============================================================================
// "set then get" of the same id observes the post-set value; "get then
// set" observes the prior value.  This is the whole reason the block is
// an ordered list and the apply pass runs in list order.
// ============================================================================

TEST_CASE("MediaIOParams set-then-get sees the written value") {
        StoreBackedIO io;
        io.store.insert(Gain.id(), Variant(1));
        REQUIRE(io.open().wait().isOk());

        MediaIOParams block;
        block.set(Gain, 99).get(Gain);
        auto req = io.sendParams(block);
        REQUIRE(req.wait().isOk());

        const MediaIOParams &done = req.commandAs<MediaIOCommandParams>()->block;
        CHECK(done.action(1).value.get<int32_t>() == 99);
}

TEST_CASE("MediaIOParams get-then-set sees the prior value") {
        StoreBackedIO io;
        io.store.insert(Gain.id(), Variant(1));
        REQUIRE(io.open().wait().isOk());

        MediaIOParams block;
        block.get(Gain).set(Gain, 99);
        auto req = io.sendParams(block);
        REQUIRE(req.wait().isOk());

        const MediaIOParams &done = req.commandAs<MediaIOCommandParams>()->block;
        CHECK(done.action(0).value.get<int32_t>() == 1);  // prior
        CHECK(io.store.value(Gain.id()).get<int32_t>() == 99);  // written
}

// ============================================================================
// Non-atomic: actions are independent.  A failing action records its own
// error, the aggregate is the first error, and later actions still run.
// ============================================================================

TEST_CASE("MediaIOParams non-atomic continues past a failed action") {
        StoreBackedIO io;
        REQUIRE(io.open().wait().isOk());

        MediaIOParams block;
        block.get(Unknown).set(Gain, 5);  // unknown get fails, set still applies
        auto req = io.sendParams(block);
        Error agg = req.wait();
        CHECK(agg == Error::NotSupported);  // aggregate = first error

        const MediaIOParams &done = req.commandAs<MediaIOCommandParams>()->block;
        CHECK(done.action(0).error == Error::NotSupported);
        CHECK(done.action(1).error.isOk());
        CHECK(io.store.value(Gain.id()).get<int32_t>() == 5);  // set committed
}

// ============================================================================
// Atomic validation: a rejected Set aborts the whole block up front —
// nothing is applied and siblings report TransactionAborted.
// ============================================================================

TEST_CASE("MediaIOParams atomic validation aborts with nothing applied") {
        StoreBackedIO io;
        io.onValidateParam = [](MediaIOParamsID id, const Variant &) -> Error {
                return id == Mode ? Error::InvalidArgument : Error::Ok;
        };
        REQUIRE(io.open().wait().isOk());

        MediaIOParams block;
        block.set(Gain, 5).set(Mode, 9);  // Mode fails validation
        block.setAtomic(true);
        auto req = io.sendParams(block);
        Error agg = req.wait();
        CHECK(agg == Error::InvalidArgument);

        const MediaIOParams &done = req.commandAs<MediaIOCommandParams>()->block;
        CHECK(done.action(0).error == Error::TransactionAborted);
        CHECK(done.action(1).error == Error::InvalidArgument);
        // Nothing applied — the valid Set never ran.
        CHECK_FALSE(io.store.contains(Gain.id()));
}

// ============================================================================
// Atomic apply: a mid-apply setParam failure rolls back the Sets already
// committed and reports the rest as TransactionAborted.
// ============================================================================

TEST_CASE("MediaIOParams atomic mid-apply failure rolls back committed sets") {
        StoreBackedIO io;
        io.store.insert(Gain.id(), Variant(1));  // prior value to roll back to

        // setParam succeeds for Gain but fails for Mode, simulating a
        // hardware reject discovered only at apply time (validation, which
        // is not overridden here, accepts everything).
        io.onSetParam = [&io](MediaIOParamsID id, const Variant &value) -> Error {
                if (id == Mode) return Error::DeviceError;
                io.store.insert(id.id(), value);
                return Error::Ok;
        };
        REQUIRE(io.open().wait().isOk());

        MediaIOParams block;
        block.set(Gain, 99).set(Mode, 7);
        block.setAtomic(true);
        auto req = io.sendParams(block);
        Error agg = req.wait();
        CHECK(agg == Error::DeviceError);

        const MediaIOParams &done = req.commandAs<MediaIOCommandParams>()->block;
        CHECK(done.action(0).error == Error::TransactionAborted);  // rolled back
        CHECK(done.action(1).error == Error::DeviceError);         // real cause
        // Gain was committed then rolled back to its prior value.
        CHECK(io.store.value(Gain.id()).get<int32_t>() == 1);
}

// ============================================================================
// Atomic happy path: every action applies and reports Ok.
// ============================================================================

TEST_CASE("MediaIOParams atomic success applies every action") {
        StoreBackedIO io;
        REQUIRE(io.open().wait().isOk());

        MediaIOParams block;
        block.set(Gain, 3).set(Mode, 4);
        block.setAtomic(true);
        auto req = io.sendParams(block);
        REQUIRE(req.wait().isOk());

        const MediaIOParams &done = req.commandAs<MediaIOCommandParams>()->block;
        CHECK(done.action(0).error.isOk());
        CHECK(done.action(1).error.isOk());
        CHECK(io.store.value(Gain.id()).get<int32_t>() == 3);
        CHECK(io.store.value(Mode.id()).get<int32_t>() == 4);
}

// ============================================================================
// Non-atomic: a Set rejected by validateParam skips only that write; the
// rest of the block still runs.  (Validation runs for every Set, not just
// atomic blocks.)
// ============================================================================

TEST_CASE("MediaIOParams non-atomic Set rejected by validateParam skips only that write") {
        StoreBackedIO io;
        io.onValidateParam = [](MediaIOParamsID id, const Variant &) -> Error {
                return id == Mode ? Error::InvalidArgument : Error::Ok;
        };
        REQUIRE(io.open().wait().isOk());

        MediaIOParams block;
        block.set(Mode, 9).set(Gain, 5);  // Mode rejected, Gain still applies
        auto  req = io.sendParams(block);
        Error agg = req.wait();
        CHECK(agg == Error::InvalidArgument);

        const MediaIOParams &done = req.commandAs<MediaIOCommandParams>()->block;
        CHECK(done.action(0).error == Error::InvalidArgument);
        CHECK(done.action(1).error.isOk());
        CHECK_FALSE(io.store.contains(Mode.id()));              // rejected write never ran
        CHECK(io.store.value(Gain.id()).get<int32_t>() == 5);   // other write applied
}

// ============================================================================
// Atomic rollback when a committed Set's prior value was unreadable: that
// Set cannot be reverted, so its written value persists after the abort.
// ============================================================================

TEST_CASE("MediaIOParams atomic rollback skips a Set whose prior value was unreadable") {
        StoreBackedIO io;
        // Gain is absent from the store, so its pre-write snapshot read
        // fails (hadPrior == false).  Mode's write then fails and triggers
        // rollback.
        io.onSetParam = [&io](MediaIOParamsID id, const Variant &value) -> Error {
                if (id == Mode) return Error::DeviceError;
                io.store.insert(id.id(), value);
                return Error::Ok;
        };
        REQUIRE(io.open().wait().isOk());

        MediaIOParams block;
        block.set(Gain, 99).set(Mode, 7);
        block.setAtomic(true);
        auto req = io.sendParams(block);
        CHECK(req.wait() == Error::DeviceError);

        const MediaIOParams &done = req.commandAs<MediaIOCommandParams>()->block;
        CHECK(done.action(0).error == Error::TransactionAborted);
        CHECK(done.action(1).error == Error::DeviceError);
        // Committed but not rollback-able (no prior value), so it persists.
        CHECK(io.store.value(Gain.id()).get<int32_t>() == 99);
}

// ============================================================================
// Atomic rollback tolerates a failing restore: the rollback setParam itself
// errors (logged), and the value is left as the failed forward write.
// ============================================================================

TEST_CASE("MediaIOParams atomic rollback tolerates a failing restore") {
        StoreBackedIO io;
        io.store.insert(Gain.id(), Variant(1));  // readable prior value

        int gainSets = 0;
        io.onSetParam = [&io, &gainSets](MediaIOParamsID id, const Variant &value) -> Error {
                if (id == Mode) return Error::DeviceError;
                if (id == Gain && ++gainSets >= 2) return Error::DeviceError;  // restore fails
                io.store.insert(id.id(), value);
                return Error::Ok;
        };
        REQUIRE(io.open().wait().isOk());

        MediaIOParams block;
        block.set(Gain, 99).set(Mode, 7);
        block.setAtomic(true);
        auto req = io.sendParams(block);
        CHECK(req.wait() == Error::DeviceError);

        const MediaIOParams &done = req.commandAs<MediaIOCommandParams>()->block;
        CHECK(done.action(0).error == Error::TransactionAborted);
        CHECK(done.action(1).error == Error::DeviceError);
        CHECK(gainSets == 2);  // forward write + one rollback attempt
        // Restore failed, so the forward value was never reverted to 1.
        CHECK(io.store.value(Gain.id()).get<int32_t>() == 99);
}

// ============================================================================
// An empty block is a successful no-op.
// ============================================================================

TEST_CASE("MediaIOParams empty block resolves Ok") {
        StoreBackedIO io;
        REQUIRE(io.open().wait().isOk());

        auto req = io.sendParams(MediaIOParams());
        CHECK(req.wait().isOk());
        CHECK(req.commandAs<MediaIOCommandParams>()->block.count() == 0);
}

// ============================================================================
// sendParams on a closed MediaIO short-circuits to NotOpen.
// ============================================================================

TEST_CASE("MediaIOParams sendParams before open returns NotOpen") {
        StoreBackedIO io;
        auto          req = io.sendParams(MediaIOParams().get(Gain));
        CHECK(req.wait() == Error::NotOpen);
}
