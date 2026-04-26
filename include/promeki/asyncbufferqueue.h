/**
 * @file      asyncbufferqueue.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/buffer.h>
#include <promeki/iodevice.h>
#include <promeki/list.h>
#include <promeki/mutex.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Sequential @ref IODevice that ferries shared @ref Buffer
 *        segments from a producer to a consumer with async-read
 *        semantics.
 * @ingroup io
 *
 * @c AsyncBufferQueue holds a thread-safe queue of @ref Buffer::Ptr
 * segments alongside a "writer side closed" latch.  The producer enqueues
 * @ref Buffer::Ptr segments via @ref enqueue and signals end-of-stream via
 * @ref closeWriting.  The consumer drains via @ref read on whichever
 * thread owns the device.
 *
 * The device is purposely shaped so that a parked HTTP body writer can
 * tell "no bytes yet, wake me later" apart from "stream is finished":
 *
 *  - When the queue is empty and @ref closeWriting has @b not been
 *    called, @ref read returns @c 0 and @ref atEnd returns @c false.
 *    The consumer should park on @ref readyReadSignal — every
 *    @ref enqueue emits it from the producer's thread.
 *  - When the queue is empty and @ref closeWriting @b has been called,
 *    @ref read returns @c 0 and @ref atEnd returns @c true.  This is
 *    the standard end-of-stream condition.
 *
 * Threading:
 *  - @ref enqueue and @ref closeWriting are safe to call from any
 *    thread.  Both emit @ref readyRead so a consumer hosted on an
 *    @ref EventLoop different from the producer will be marshalled
 *    awake by the existing cross-thread connect machinery.
 *  - @ref read, @ref atEnd, @ref bytesAvailable, @ref open, and
 *    @ref close should be called only from the consumer's thread.
 *
 * @par Example
 * @code
 * AsyncBufferQueue queue;
 * queue.open(IODevice::ReadOnly);
 *
 * queue.readyReadSignal.connect([&]() {
 *         char tmp[1024];
 *         int64_t got = queue.read(tmp, sizeof(tmp));
 *         if(got > 0) consume(tmp, got);
 * });
 *
 * // From a producer thread:
 * queue.enqueue(jpegPtr);
 * queue.enqueue(boundaryPtr);
 * queue.closeWriting();
 * @endcode
 */
class AsyncBufferQueue : public IODevice {
                PROMEKI_OBJECT(AsyncBufferQueue, IODevice)
        public:
                /** @brief Constructs an empty queue, not yet open. */
                explicit AsyncBufferQueue(ObjectBase *parent = nullptr);

                /** @brief Destructor.  Closes the device if still open. */
                ~AsyncBufferQueue() override;

                /**
                 * @brief Opens the device.
                 *
                 * Only @ref ReadOnly is supported — production data
                 * arrives via @ref enqueue, not via @ref write.
                 */
                Error open(OpenMode mode) override;

                /**
                 * @brief Closes the device.
                 *
                 * Drops any still-queued segments and emits
                 * @ref aboutToClose.  Safe to call from the consumer
                 * thread; not safe from a producer (use
                 * @ref closeWriting from the producer).
                 */
                Error close() override;

                /** @copydoc IODevice::isOpen */
                bool isOpen() const override;

                /** @copydoc IODevice::read */
                int64_t read(void *data, int64_t maxSize) override;

                /**
                 * @brief Writes are not supported on the consumer side.
                 *
                 * The producer enqueues whole @ref Buffer::Ptr segments
                 * via @ref enqueue.  A direct @ref write would force a
                 * copy into a freshly-allocated buffer for every byte
                 * range, defeating the share-by-pointer design.
                 *
                 * @return Always @c -1; sets @ref Error::NotSupported.
                 */
                int64_t write(const void *data, int64_t maxSize) override;

                /** @copydoc IODevice::bytesAvailable */
                int64_t bytesAvailable() const override;

                /** @copydoc IODevice::isSequential */
                bool isSequential() const override;

                /**
                 * @copydoc IODevice::seek
                 *
                 * Always returns @ref Error::NotSupported — the queue
                 * is sequential and segments are released as soon as
                 * they drain.
                 */
                Error seek(int64_t pos) override;

                /** @copydoc IODevice::pos */
                int64_t pos() const override;

                /**
                 * @copydoc IODevice::size
                 *
                 * Returns the total bytes currently queued (the same
                 * value as @ref bytesAvailable).  Streaming bodies
                 * have no a-priori size, so the connection-side
                 * pump treats this device as chunked.
                 */
                Result<int64_t> size() const override;

                /**
                 * @copydoc IODevice::atEnd
                 *
                 * @c true only after @ref closeWriting has been called
                 * @b and the queue has drained.  While the writer side
                 * is still open the device is never @c atEnd, even if
                 * the queue is momentarily empty.
                 */
                bool atEnd() const override;

                // ----------------------------------------------------
                // Producer-side API (thread-safe)
                // ----------------------------------------------------

                /**
                 * @brief Appends a segment to the back of the queue.
                 *
                 * Empty / null pointers are ignored.  Emits
                 * @ref readyRead from the calling thread; cross-thread
                 * consumers are woken via the standard signal/slot
                 * marshalling path when their connection captured an
                 * @ref ObjectBase context.
                 *
                 * @return @ref Error::Ok on success,
                 *         @ref Error::NotOpen if the device has been
                 *         closed (or @ref closeWriting was already
                 *         called).
                 */
                Error enqueue(const Buffer::Ptr &segment);

                /**
                 * @brief Latches the writer side closed.
                 *
                 * After this call @ref enqueue returns
                 * @ref Error::NotOpen, and @ref atEnd flips to @c true
                 * once the consumer drains the remaining segments.
                 * Emits @ref readyRead one final time so a parked
                 * consumer wakes and observes the new end-of-stream
                 * condition.  Idempotent.
                 */
                void closeWriting();

                /** @brief Returns @c true after @ref closeWriting. */
                bool isWritingClosed() const;

                /** @brief Returns the number of queued segments. */
                size_t segmentCount() const;

        private:
                struct Segment {
                                Buffer::Ptr buffer;
                                size_t      offset = 0; ///< Bytes already drained from this segment.
                };

                mutable Mutex _mutex;
                List<Segment> _segments;
                int64_t       _queuedBytes = 0;
                int64_t       _readPos = 0;
                bool          _writingClosed = false;
};

PROMEKI_NAMESPACE_END
