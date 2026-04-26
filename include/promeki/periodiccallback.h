/**
 * @file      periodiccallback.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <promeki/namespace.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Calls a function at a fixed interval when serviced.
 * @ingroup core
 *
 * PeriodicCallback is a passive timer: it does not spawn threads or
 * use system timers.  The caller drives it by calling service()
 * from whatever loop or thread it already runs on.  When the
 * configured interval has elapsed since the last invocation,
 * service() calls the stored function and resets the clock.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently; concurrent calls to @c service / setters on a
 * single instance must be externally synchronized — typically by
 * confining the instance to one driver thread.
 *
 * @par Example
 * @code
 * PeriodicCallback pc(1.0, []{ promekiDebug("tick"); });
 * while(running) {
 *         doWork();
 *         pc.service();
 * }
 * @endcode
 */
class PeriodicCallback {
        public:
                /** @brief Callable type. */
                using Function = std::function<void()>;

                /** @brief Default constructor — invalid, service() is a no-op. */
                PeriodicCallback() = default;

                /**
                 * @brief Constructs a PeriodicCallback.
                 * @param intervalSeconds Minimum seconds between invocations.
                 * @param func           Function to call.
                 */
                PeriodicCallback(double intervalSeconds, Function func);

                /**
                 * @brief Calls the function if the interval has elapsed.
                 * @return True if the function was called.
                 *
                 * The first call after construction (or after reset())
                 * starts the clock — the function is not called until
                 * @a intervalSeconds have elapsed from that first service().
                 */
                bool service();

                /**
                 * @brief Resets the timer so the next interval starts now.
                 *
                 * The function will not be called until a full interval
                 * has elapsed after this call.
                 */
                void reset();

                /** @brief Sets the interval in seconds. */
                void setInterval(double seconds) { _intervalSeconds = seconds; }

                /** @brief Returns the interval in seconds. */
                double interval() const { return _intervalSeconds; }

                /** @brief Sets the callback function. */
                void setCallback(Function func) { _func = std::move(func); }

                /** @brief Returns true if a callback and a positive interval are set. */
                bool isValid() const { return _func && _intervalSeconds > 0.0; }

        private:
                Function        _func;
                double          _intervalSeconds = 0.0;
                TimeStamp       _stamp;
                bool            _started = false;
};

PROMEKI_NAMESPACE_END
