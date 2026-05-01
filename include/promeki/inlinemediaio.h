/**
 * @file      inlinemediaio.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/commandmediaio.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Strategy class that runs commands inline on the calling thread.
 * @ingroup mediaio_backend
 *
 * Useful for tests, deterministic harnesses, and exotic hosts that
 * drive their own loop.  Not intended for production backends —
 * blocking inside @c executeCmd will block whatever thread called
 * the public API.
 *
 * @par Behavior
 * @ref submit synchronously runs @ref dispatch followed by
 * @ref MediaIO::completeCommand on the calling thread.  By the time
 * the @ref MediaIORequest returned to the caller is constructed, the
 * underlying command has already resolved — @c req.wait() is a
 * no-op.
 *
 * @par Cancellation
 * Pre-dispatch cancellation has no opportunity to fire in this
 * strategy: the cancel check happens inside @c submit and there is
 * no asynchronous gap between submit and dispatch for a caller to
 * race.  In-flight cancellation is not supported.
 */
class InlineMediaIO : public CommandMediaIO {
                PROMEKI_OBJECT(InlineMediaIO, CommandMediaIO)
        public:
                /** @brief Constructs with optional parent. */
                InlineMediaIO(ObjectBase *parent = nullptr);

                /** @brief Destructor. */
                ~InlineMediaIO() override;

                /**
                 * @brief Always true — there is no async queue.
                 *
                 * @c submit returns only after the cmd has resolved,
                 * so the instance is always idle between calls.
                 */
                bool isIdle() const override { return true; }

        protected:
                /**
                 * @brief Runs @ref dispatch + @ref MediaIO::completeCommand
                 *        synchronously on the calling thread.
                 */
                void submit(MediaIOCommand::Ptr cmd) override;
};

PROMEKI_NAMESPACE_END
