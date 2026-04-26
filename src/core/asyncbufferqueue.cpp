/**
 * @file      asyncbufferqueue.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>

#include <promeki/asyncbufferqueue.h>

PROMEKI_NAMESPACE_BEGIN

AsyncBufferQueue::AsyncBufferQueue(ObjectBase *parent) :
        IODevice(parent) { }

AsyncBufferQueue::~AsyncBufferQueue() {
        if(isOpen()) close();
}

Error AsyncBufferQueue::open(OpenMode mode) {
        if(isOpen()) return Error(Error::AlreadyOpen);
        // Only ReadOnly makes sense — bytes arrive via enqueue, not
        // via write().  Reject everything else loudly so the
        // consumer side can tell intent at construction time.
        if((mode & WriteOnly) != 0) return Error(Error::NotSupported);
        if((mode & ReadOnly)  == 0) return Error(Error::Invalid);
        setOpenMode(mode);
        {
                Mutex::Locker lk(_mutex);
                _segments.clear();
                _queuedBytes   = 0;
                _readPos       = 0;
                _writingClosed = false;
        }
        return Error();
}

Error AsyncBufferQueue::close() {
        if(!isOpen()) return Error(Error::NotOpen);
        aboutToCloseSignal.emit();
        setOpenMode(NotOpen);
        {
                Mutex::Locker lk(_mutex);
                _segments.clear();
                _queuedBytes   = 0;
                _writingClosed = true;
        }
        return Error();
}

bool AsyncBufferQueue::isOpen() const {
        return openMode() != NotOpen;
}

int64_t AsyncBufferQueue::read(void *data, int64_t maxSize) {
        if(!isOpen() || !isReadable()) return -1;
        if(data == nullptr || maxSize <= 0) return 0;

        uint8_t *dst   = static_cast<uint8_t *>(data);
        int64_t  total = 0;

        Mutex::Locker lk(_mutex);
        // Drain whole / partial segments until we either fill the
        // caller's buffer or the queue empties.  If we exhaust before
        // filling, we return what we have — the caller must look at
        // atEnd() to disambiguate "more coming" vs "stream ended".
        while(total < maxSize && !_segments.isEmpty()) {
                Segment &front = _segments[0];
                const Buffer *buf = front.buffer.ptr();
                if(buf == nullptr || !buf->isValid() || buf->size() == 0) {
                        // Defensively drop any empty / invalid head — the
                        // public enqueue() filters these but a future
                        // sub-class might bypass that check.
                        _segments.remove(static_cast<size_t>(0));
                        continue;
                }
                const size_t bufSize = buf->size();
                const size_t avail   = bufSize - front.offset;
                const size_t take    = (avail >
                                static_cast<size_t>(maxSize - total))
                        ? static_cast<size_t>(maxSize - total)
                        : avail;
                const uint8_t *src = static_cast<const uint8_t *>(buf->data())
                        + front.offset;
                std::memcpy(dst + total, src, take);
                total += static_cast<int64_t>(take);
                front.offset += take;
                _queuedBytes -= static_cast<int64_t>(take);
                if(front.offset >= bufSize) {
                        _segments.remove(static_cast<size_t>(0));
                }
        }
        _readPos += total;
        return total;
}

int64_t AsyncBufferQueue::write(const void *data, int64_t maxSize) {
        (void)data;
        (void)maxSize;
        // The whole point of the queue is to share Buffer::Ptr by
        // reference; a generic write() would force a copy.  Producers
        // must use enqueue().
        return -1;
}

int64_t AsyncBufferQueue::bytesAvailable() const {
        Mutex::Locker lk(_mutex);
        return _queuedBytes;
}

bool AsyncBufferQueue::isSequential() const {
        return true;
}

Error AsyncBufferQueue::seek(int64_t pos) {
        (void)pos;
        return Error(Error::NotSupported);
}

int64_t AsyncBufferQueue::pos() const {
        Mutex::Locker lk(_mutex);
        return _readPos;
}

Result<int64_t> AsyncBufferQueue::size() const {
        Mutex::Locker lk(_mutex);
        return makeResult<int64_t>(_queuedBytes);
}

bool AsyncBufferQueue::atEnd() const {
        Mutex::Locker lk(_mutex);
        return _writingClosed && _queuedBytes == 0;
}

// ----------------------------------------------------------------------------
// Producer-side API
// ----------------------------------------------------------------------------

Error AsyncBufferQueue::enqueue(const Buffer::Ptr &segment) {
        if(!segment.isValid()) return Error::Ok;     // tolerate empty pushes
        if(segment->size() == 0) return Error::Ok;
        {
                Mutex::Locker lk(_mutex);
                if(_writingClosed) return Error(Error::NotOpen);
                Segment s;
                s.buffer = segment;
                s.offset = 0;
                _segments.pushToBack(s);
                _queuedBytes += static_cast<int64_t>(segment->size());
        }
        readyReadSignal.emit();
        return Error::Ok;
}

void AsyncBufferQueue::closeWriting() {
        bool wasOpen = false;
        {
                Mutex::Locker lk(_mutex);
                if(!_writingClosed) {
                        _writingClosed = true;
                        wasOpen = true;
                }
        }
        // Wake any parked consumer one last time so it observes the
        // new end-of-stream and unparks instead of hanging on an
        // empty queue.  Only do it on the transition so we don't
        // spam consumers with redundant signals.
        if(wasOpen) readyReadSignal.emit();
}

bool AsyncBufferQueue::isWritingClosed() const {
        Mutex::Locker lk(_mutex);
        return _writingClosed;
}

size_t AsyncBufferQueue::segmentCount() const {
        Mutex::Locker lk(_mutex);
        return _segments.size();
}

PROMEKI_NAMESPACE_END
