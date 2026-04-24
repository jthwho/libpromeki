/**
 * @file      bufferview.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <limits>
#include <promeki/bufferview.h>

PROMEKI_NAMESPACE_BEGIN

// Sentinel for "no buffer" slices.  Stored in View::bufferIdx when the
// slice carries a null Buffer::Ptr — no entry is inserted into
// _buffers in that case.
static constexpr size_t kNoBuffer = std::numeric_limits<size_t>::max();

// ---------------------------------------------------------------------------
// Entry
// ---------------------------------------------------------------------------

const Buffer::Ptr &BufferView::Entry::buffer() const {
        static const Buffer::Ptr kNull;
        if(_list == nullptr) return kNull;
        const View &v = _list->_views[_idx];
        if(v.bufferIdx == kNoBuffer) return kNull;
        return _list->_buffers[v.bufferIdx];
}

size_t BufferView::Entry::offset() const {
        if(_list == nullptr) return 0;
        return _list->_views[_idx].offset;
}

size_t BufferView::Entry::size() const {
        if(_list == nullptr) return 0;
        return _list->_views[_idx].size;
}

uint8_t *BufferView::Entry::data() const {
        const Buffer::Ptr &b = buffer();
        if(!b.isValid()) return nullptr;
        return static_cast<uint8_t *>(b->data()) + offset();
}

bool BufferView::Entry::isValid() const {
        if(_list == nullptr) return false;
        const View &v = _list->_views[_idx];
        if(v.bufferIdx == kNoBuffer) return false;
        return _list->_buffers[v.bufferIdx].isValid();
}

// ---------------------------------------------------------------------------
// BufferView — construction
// ---------------------------------------------------------------------------

BufferView::BufferView(Buffer::Ptr buf, size_t offset, size_t size) {
        pushToBack(std::move(buf), offset, size);
}

BufferView::BufferView(std::initializer_list<BufferView> init) {
        for(const BufferView &bv : init) append(bv);
}

// ---------------------------------------------------------------------------
// BufferView — buffer interning
// ---------------------------------------------------------------------------

size_t BufferView::internBuffer(const Buffer::Ptr &buf) {
        if(!buf.isValid()) return kNoBuffer;
        const Buffer *key = buf.ptr();
        for(size_t i = 0; i < _buffers.size(); ++i) {
                if(_buffers[i].ptr() == key) return i;
        }
        _buffers.pushToBack(buf);
        return _buffers.size() - 1;
}

// ---------------------------------------------------------------------------
// BufferView — mutation
// ---------------------------------------------------------------------------

void BufferView::pushToBack(Buffer::Ptr buf, size_t offset, size_t size) {
        View v;
        v.bufferIdx = internBuffer(buf);
        v.offset    = offset;
        v.size      = size;
        _views.pushToBack(v);
}

void BufferView::append(const BufferView &other) {
        for(size_t i = 0; i < other._views.size(); ++i) {
                const View &ov = other._views[i];
                const Buffer::Ptr &ob = (ov.bufferIdx == kNoBuffer)
                        ? Buffer::Ptr()
                        : other._buffers[ov.bufferIdx];
                pushToBack(ob, ov.offset, ov.size);
        }
}

void BufferView::clear() {
        _buffers.clear();
        _views.clear();
}

// ---------------------------------------------------------------------------
// BufferView — domain operations
// ---------------------------------------------------------------------------

size_t BufferView::size() const {
        size_t total = 0;
        for(size_t i = 0; i < _views.size(); ++i) total += _views[i].size;
        return total;
}

// ---------------------------------------------------------------------------
// Single-slice convenience accessors
// ---------------------------------------------------------------------------
//
// These forward to slice 0 when the list holds at least one slice and
// return empty / zero / nullptr on an empty list.  They match the old
// per-view BufferView API so callers that only ever deal with a single
// slice stay readable after the list-ification.

const Buffer::Ptr &BufferView::buffer() const {
        static const Buffer::Ptr kNull;
        if(_views.isEmpty()) return kNull;
        const View &v = _views[0];
        if(v.bufferIdx == kNoBuffer) return kNull;
        return _buffers[v.bufferIdx];
}

size_t BufferView::offset() const {
        if(_views.isEmpty()) return 0;
        return _views[0].offset;
}

uint8_t *BufferView::data() const {
        const Buffer::Ptr &b = buffer();
        if(!b.isValid()) return nullptr;
        return static_cast<uint8_t *>(b->data()) + offset();
}

bool BufferView::isValid() const {
        if(_views.isEmpty()) return false;
        const View &v = _views[0];
        if(v.bufferIdx == kNoBuffer) return false;
        return _buffers[v.bufferIdx].isValid();
}

bool BufferView::isExclusive() const {
        // Each unique buffer appears exactly once in _buffers, and the
        // list holds exactly one Buffer::Ptr reference to it.  Any
        // refcount above 1 means an external holder exists.
        for(size_t i = 0; i < _buffers.size(); ++i) {
                const Buffer::Ptr &b = _buffers[i];
                if(!b.isValid()) continue;
                if(b.referenceCount() > 1) return false;
        }
        return true;
}

void BufferView::ensureExclusive() {
        // Clone each unique shared buffer once; the slice records
        // don't change because bufferIdx still resolves to the same
        // (now-exclusive) clone in _buffers.
        for(size_t i = 0; i < _buffers.size(); ++i) {
                Buffer::Ptr &b = _buffers[i];
                if(!b.isValid()) continue;
                // referenceCount() == 1 means this list is the only
                // holder; >1 means someone outside also has a ref,
                // so clone to get private storage.
                if(b.referenceCount() > 1) b.modify();
        }
}

PROMEKI_NAMESPACE_END
