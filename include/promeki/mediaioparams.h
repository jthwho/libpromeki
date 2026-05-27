/**
 * @file      mediaioparams.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <promeki/namespace.h>
#include <promeki/variant.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/stringregistry.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief The action a single @ref MediaIOParamAction performs.
 * @ingroup mediaio_user
 */
enum class MediaIOParamOp {
        Get, ///< @brief Read the parameter's current value into the action.
        Set  ///< @brief Write the action's value to the parameter.
};

/**
 * @brief One entry in a @ref MediaIOParams block.
 * @ingroup mediaio_user
 *
 * Every action names a parameter (@ref id), an operation (@ref op),
 * and carries a @ref value that is used both as the input (for
 * @c Set) and the output slot (for @c Get).  After the framework
 * runs the block, @ref error holds the per-action outcome.
 *
 * @par value semantics
 *  - @c Set — @ref value is the value to write (input).  It is left
 *    unchanged on output, so it still echoes what was requested.
 *  - @c Get — @ref value is empty on input and is overwritten with
 *    the parameter's current value on output.  Whether the read
 *    succeeded is reported by @ref error.
 *
 * @par Composing read-modify-write
 * There are deliberately only two ops.  "Set then read back the value
 * the hardware actually accepted" is a @c Set immediately followed by
 * a @c Get of the same id; "capture the prior value, then change it"
 * is a @c Get followed by a @c Set.  Because the apply pass runs in
 * list order within a single command, the @c Get observes exactly the
 * state implied by its position relative to the @c Set.
 */
struct MediaIOParamAction {
        /** @brief Parameter identifier (backend-defined). */
        StringRegistry<"MediaIOParams">::Item id;
        /** @brief Whether this action reads or writes. */
        MediaIOParamOp op = MediaIOParamOp::Get;
        /** @brief Input value (Set) / output slot (Get). */
        Variant value;
        /** @brief Per-action outcome, filled by the apply pass. */
        Error error = Error::Ok;
};

/**
 * @brief Ordered, optionally-transactional get/set parameter block.
 * @ingroup mediaio_user
 *
 * A @ref MediaIOParams block is the payload of
 * @ref MediaIO::sendParams.  It is an ordered list of
 * @ref MediaIOParamAction entries — each a @c Get or @c Set against a
 * backend-defined parameter id — plus an @ref isAtomic flag.  Backends
 * never parse the block directly; the framework walks it and calls the
 * backend's @c getParam / @c setParam / @c validateParam hooks, writing
 * each entry's result back in place.
 *
 * @par Identifiers
 * Parameter ids are @ref ID values — entries in a dedicated
 * @ref StringRegistry so they never collide with config or stats keys.
 * Backends typically expose well-known ids as @c static @c const
 * members on their class.
 *
 * @par Result access
 * Because the framework mutates the block in place, results are read
 * from the resolved command, not from the caller's local object:
 * @code
 * MediaIOParams block;
 * block.set(GainKey, 0.5);     // write
 * block.get(GainKey);          // read back what was accepted
 *
 * auto req = io->sendParams(block);
 * req.wait();
 * const MediaIOParams &done = req.commandAs<MediaIOCommandParams>()->block;
 * Result<Variant> gain = done.result(1);   // {value, error}
 * @endcode
 *
 * @par Atomicity
 * With @ref setAtomic enabled the block is validate-then-apply:
 * every @c Set is validated up front, and if any validation fails
 * nothing is applied.  A failure that occurs once the apply pass has
 * started triggers a best-effort rollback of the @c Set actions
 * already committed.  Actions that never ran (or were rolled back)
 * report @ref Error::TransactionAborted.  Backends that cannot honor
 * atomic semantics reject an atomic block up front.
 */
class MediaIOParams {
        public:
                /** @brief Backend-defined parameter identifier. */
                using ID = StringRegistry<"MediaIOParams">::Item;
                /** @brief Action list type. */
                using ActionList = List<MediaIOParamAction>;

                /** @brief Constructs an empty, non-atomic block. */
                MediaIOParams() = default;

                /**
                 * @brief Appends a @c Get of @p id.
                 * @param id Parameter to read.
                 * @return @c *this, for chaining.
                 */
                MediaIOParams &get(ID id) {
                        _actions.pushToBack(MediaIOParamAction{id, MediaIOParamOp::Get, Variant(), Error::Ok});
                        return *this;
                }

                /**
                 * @brief Appends a @c Set of @p id to @p value.
                 * @param id    Parameter to write.
                 * @param value Value to write.
                 * @return @c *this, for chaining.
                 */
                MediaIOParams &set(ID id, const Variant &value) {
                        _actions.pushToBack(MediaIOParamAction{id, MediaIOParamOp::Set, value, Error::Ok});
                        return *this;
                }

                /** @brief Enables or disables all-or-nothing semantics. */
                void setAtomic(bool on) { _atomic = on; }

                /** @brief True when this block is all-or-nothing. */
                bool isAtomic() const { return _atomic; }

                /** @brief Number of actions in the block. */
                int count() const { return static_cast<int>(_actions.size()); }

                /** @brief True when the block holds no actions. */
                bool isEmpty() const { return _actions.isEmpty(); }

                /** @brief Read-only access to action @p i. */
                const MediaIOParamAction &action(int i) const { return _actions.at(i); }

                /** @brief Mutable access to action @p i (used by the apply pass). */
                MediaIOParamAction &action(int i) { return _actions[i]; }

                /** @brief Read-only access to the whole action list. */
                const ActionList &actions() const { return _actions; }

                /** @brief Mutable access to the whole action list (used by the apply pass). */
                ActionList &actions() { return _actions; }

                /**
                 * @brief Convenience: action @p i as a @c Result<Variant>.
                 *
                 * Pairs the action's @ref MediaIOParamAction::value with
                 * its @ref MediaIOParamAction::error so callers can use
                 * structured bindings without touching the struct.
                 *
                 * @param i Action index.
                 * @return @c {value, error} for the action.
                 */
                Result<Variant> result(int i) const {
                        const MediaIOParamAction &a = _actions.at(i);
                        return Result<Variant>(a.value, a.error);
                }

        private:
                ActionList _actions;
                bool       _atomic = false;
};

/**
 * @brief Parameter identifier type.
 * @ingroup mediaio_user
 *
 * Alias kept for call-site brevity; identical to
 * @ref MediaIOParams::ID.
 */
using MediaIOParamsID = MediaIOParams::ID;

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
