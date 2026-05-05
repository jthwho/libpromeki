/**
 * @file      tests/unit/buffercommand.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <atomic>
#include <chrono>
#include <thread>
#include <doctest/doctest.h>
#include <promeki/buffercommand.h>
#include <promeki/bufferrequest.h>
#include <promeki/error.h>

using namespace promeki;

TEST_CASE("BufferCommand: kindName covers every Kind") {
        CHECK(std::string(BufferCommand::kindName(BufferCommand::Map)) == "Map");
        CHECK(std::string(BufferCommand::kindName(BufferCommand::Unmap)) == "Unmap");
        CHECK(std::string(BufferCommand::kindName(BufferCommand::Copy)) == "Copy");
}

TEST_CASE("BufferCommand: markCompleted is idempotent and unblocks waiters") {
        auto *cmd = new BufferMapCommand();
        cmd->result = Error::Ok;
        BufferCommand::Ptr cmdPtr = BufferCommand::Ptr::takeOwnership(cmd);
        CHECK_FALSE(cmd->isCompleted());

        cmd->markCompleted();
        CHECK(cmd->isCompleted());

        // Second markCompleted is a no-op (no double-fire of the
        // callback or CV broadcast).
        cmd->markCompleted();
        CHECK(cmd->isCompleted());
        CHECK(cmd->waitForCompletion(0) == Error::Ok);
}

TEST_CASE("BufferCommand: waitForCompletion respects timeout") {
        auto              *cmd = new BufferMapCommand();
        BufferCommand::Ptr cmdPtr = BufferCommand::Ptr::takeOwnership(cmd);
        CHECK(cmd->waitForCompletion(50) == Error::Timeout);
}

TEST_CASE("BufferCommand: setCompletionCallback fires on resolve") {
        auto              *cmd = new BufferMapCommand();
        cmd->result = Error::Invalid;
        BufferCommand::Ptr cmdPtr = BufferCommand::Ptr::takeOwnership(cmd);

        std::atomic<int> fireCount{0};
        Error            seen = Error::Ok;
        cmd->setCompletionCallback(
                [&](Error err) {
                        fireCount.fetch_add(1);
                        seen = err;
                },
                /*loop*/ nullptr);

        CHECK(fireCount.load() == 0);
        cmd->markCompleted();
        CHECK(fireCount.load() == 1);
        CHECK(seen == Error::Invalid);
}

TEST_CASE("BufferCommand: setCompletionCallback after resolve fires immediately") {
        auto              *cmd = new BufferMapCommand();
        cmd->result = Error::Ok;
        BufferCommand::Ptr cmdPtr = BufferCommand::Ptr::takeOwnership(cmd);
        cmd->markCompleted();

        std::atomic<int> fireCount{0};
        cmd->setCompletionCallback([&](Error) { fireCount.fetch_add(1); }, nullptr);
        CHECK(fireCount.load() == 1);
}

TEST_CASE("BufferRequest: resolved(Error) reports ready immediately") {
        BufferRequest req = BufferRequest::resolved(Error::NotSupported);
        CHECK(req.isReady());
        CHECK(req.wait() == Error::NotSupported);

        // commandAs returns nullptr when the request was built from
        // the Error sentinel path.
        CHECK(req.commandAs<BufferMapCommand>() == nullptr);
}

TEST_CASE("BufferRequest: resolved(cmd) marks the command completed") {
        auto *cmd = new BufferMapCommand();
        cmd->result = Error::Ok;
        cmd->hostPtr = nullptr;
        BufferCommand::Ptr cmdPtr = BufferCommand::Ptr::takeOwnership(cmd);
        BufferRequest      req = BufferRequest::resolved(cmdPtr);
        CHECK(req.isReady());
        CHECK(req.wait() == Error::Ok);

        const auto *typed = req.commandAs<BufferMapCommand>();
        REQUIRE(typed != nullptr);
        CHECK(typed->hostPtr == nullptr);
}

TEST_CASE("BufferRequest: empty request reports invalid wait") {
        BufferRequest req;
        CHECK_FALSE(req.isReady());
        CHECK(req.wait() == Error::Invalid);
}

TEST_CASE("BufferRequest: cancel sets the flag, both for sentinel and real commands") {
        BufferRequest sentinel = BufferRequest::resolved(Error::Ok);
        sentinel.cancel();
        CHECK(sentinel.isCancelled());

        auto              *cmd = new BufferMapCommand();
        BufferCommand::Ptr cmdPtr = BufferCommand::Ptr::takeOwnership(cmd);
        BufferRequest      req(cmdPtr);
        CHECK_FALSE(req.isCancelled());
        req.cancel();
        CHECK(req.isCancelled());
        CHECK(cmd->cancelled.value());
}
