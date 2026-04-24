/**
 * @file      bufferview.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <initializer_list>
#include <promeki/buffer.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Ordered list of @c (offset, size) views over a deduplicated
 *        set of backing @ref Buffer objects.
 * @ingroup media
 *
 * A @c BufferView is @em natively a list.  Each element of the list
 * is a slice @c (buffer, offset, size) describing a region of a
 * shared @ref Buffer.  When multiple slices reference the same
 * underlying buffer (planar video stored in a single allocation with
 * distinct offsets, for example) the buffer is stored @em once and
 * every slice records an index into the buffer table.  This keeps
 * the buffer's reference count reflecting outside sharing only — the
 * list itself never inflates refcount N× for an N-plane payload.
 *
 * Construction covers both common shapes:
 *
 *   - @c BufferView() — empty list.
 *   - @c BufferView(buf, offset, size) — single-slice convenience
 *     form, equivalent to the old per-view @c BufferView.
 *   - Repeated @c pushToBack calls to accumulate multiple slices.
 *
 * Per-slice access goes through the @ref Entry proxy returned by
 * @c operator[] and the iterator.  An @ref Entry is a lightweight
 * non-owning handle carrying only a parent pointer and an index;
 * @c Entry::buffer() / @c offset() / @c size() / @c data() match the
 * old per-view @c BufferView API so call sites that held a single
 * view keep working.
 *
 * The exclusive-ownership operations are dedup-aware: @ref
 * ensureExclusive clones each unique backing @ref Buffer at most
 * once and redirects every slice that referenced the original to
 * the same clone, instead of cloning N times and splitting the
 * slices across N unrelated buffers.
 *
 * @par Example
 * @code
 * // Single-slice view over a buffer:
 * auto buf = Buffer::Ptr::create(65536);
 * BufferView slice(buf, 0, 1400);
 * CHECK(slice.count() == 1);
 * CHECK(slice[0].buffer() == buf);
 *
 * // Multiple slices over the same backing buffer:
 * BufferView slices;
 * slices.pushToBack(buf, 0,     1400);
 * slices.pushToBack(buf, 1400,  1400);
 * CHECK(slices.count() == 2);
 * CHECK(slices[0].buffer().ptr() == slices[1].buffer().ptr());
 * @endcode
 */
class BufferView {
        public:
                // ---- Per-slice proxy ------------------------------

                /**
                 * @brief Non-owning proxy for a single slice in the list.
                 *
                 * Entry carries a parent pointer and an index; it never
                 * owns a @ref Buffer::Ptr.  All accessors go back
                 * through the parent's buffer table and view record.
                 * Entry is cheap to copy and safe to pass by value
                 * within a single expression; do not store an Entry
                 * beyond the lifetime of its parent @ref BufferView.
                 */
                class Entry {
                                friend class BufferView;
                        public:
                                /** @brief Default-constructs a null Entry. */
                                Entry() = default;

                                /** @brief Returns the shared backing buffer. */
                                const Buffer::Ptr &buffer() const;

                                /** @brief Returns the byte offset into the buffer. */
                                size_t offset() const;

                                /** @brief Returns the byte size of this slice. */
                                size_t size() const;

                                /**
                                 * @brief Returns a pointer to this slice's data.
                                 * @return Pointer to the data, or @c nullptr if no buffer.
                                 */
                                uint8_t *data() const;

                                /** @brief Returns @c true when the slice has a non-null buffer. */
                                bool isValid() const;

                                /** @brief Returns @c true when the slice has no buffer. */
                                bool isNull() const { return !isValid(); }

                        private:
                                const BufferView *_list = nullptr;
                                size_t            _idx  = 0;

                                Entry(const BufferView *list, size_t idx)
                                        : _list(list), _idx(idx) { }
                };

                // ---- Iteration -----------------------------------

                /**
                 * @brief Random-access iterator yielding @ref Entry
                 *        proxies.
                 */
                class Iterator {
                                friend class BufferView;
                        public:
                                Iterator() = default;
                                Entry operator*() const { return Entry(_list, _idx); }
                                Iterator &operator++() { ++_idx; return *this; }
                                Iterator  operator++(int) { Iterator t(*this); ++_idx; return t; }
                                bool operator==(const Iterator &o) const {
                                        return _list == o._list && _idx == o._idx;
                                }
                                bool operator!=(const Iterator &o) const { return !(*this == o); }

                        private:
                                const BufferView *_list = nullptr;
                                size_t            _idx  = 0;

                                Iterator(const BufferView *list, size_t idx)
                                        : _list(list), _idx(idx) { }
                };

                // ---- Construction --------------------------------

                /** @brief Constructs an empty list. */
                BufferView() = default;

                /**
                 * @brief Constructs a single-slice list over @p buf.
                 *
                 * @param buf    The shared backing buffer.
                 * @param offset Byte offset into the buffer.
                 * @param size   Byte size of the slice.
                 *
                 * @note The caller must ensure @c offset + @c size
                 *       does not exceed the buffer's allocated size.
                 *       No bounds checking is performed.
                 */
                BufferView(Buffer::Ptr buf, size_t offset, size_t size);

                /**
                 * @brief Constructs a list by concatenating every
                 *        sub-list in @p init.
                 *
                 * Each @ref BufferView in the initializer list has
                 * all of its slices appended — typically each input
                 * is a single-slice @ref BufferView, giving the
                 * "build a multi-plane payload from several
                 * independent slices" shape.
                 */
                BufferView(std::initializer_list<BufferView> init);

                // ---- List operations -----------------------------

                /** @brief Returns the number of slices in the list. */
                size_t count() const { return _views.size(); }

                /** @brief Returns @c true when the list holds no slices. */
                bool isEmpty() const { return _views.isEmpty(); }

                /** @brief Returns a proxy for the slice at @p i. */
                Entry operator[](size_t i) const { return Entry(this, i); }

                Iterator begin() const { return Iterator(this, 0); }
                Iterator end()   const { return Iterator(this, _views.size()); }

                // ---- Single-slice convenience accessors ----------
                //
                // These treat the list as a single byte blob.  When
                // the list holds exactly one slice they forward to
                // that slice; on an empty list they return empty /
                // zero / nullptr values.  Call sites that manipulate
                // multi-slice lists should use @c operator[] or
                // iteration rather than these helpers.

                /** @brief Returns the backing buffer of the first slice. */
                const Buffer::Ptr &buffer() const;

                /** @brief Returns the byte offset of the first slice. */
                size_t offset() const;

                /** @brief Returns a pointer to the first slice's data. */
                uint8_t *data() const;

                /**
                 * @brief Returns @c true when the list holds at least
                 *        one valid slice.
                 */
                bool isValid() const;

                /** @brief Returns @c true when no valid slice is present. */
                bool isNull() const { return !isValid(); }

                /**
                 * @brief Appends a slice to the back of the list.
                 *
                 * The backing @ref Buffer is deduplicated against
                 * slices already in the list; pushing N slices that
                 * all share the same buffer stores the buffer once.
                 */
                void pushToBack(Buffer::Ptr buf, size_t offset, size_t size);

                /**
                 * @brief Appends every slice from @p other to the end
                 *        of this list.
                 */
                void append(const BufferView &other);

                /** @brief Removes every slice from the list. */
                void clear();

                // ---- Domain operations ---------------------------

                /**
                 * @brief Returns the total byte size summed across
                 *        every slice in the list.
                 *
                 * For the common single-slice case this equals the
                 * byte size of that slice, matching the old per-view
                 * @c BufferView::size() contract.
                 */
                size_t size() const;

                /** @brief Alias for @ref size — total bytes across all slices. */
                size_t totalSize() const { return size(); }

                /**
                 * @brief Returns @c true when every unique backing
                 *        @ref Buffer is held only by this list — no
                 *        external holder.
                 *
                 * A buffer's reference count is compared against the
                 * number of entries in the internal buffer table
                 * (each buffer appears exactly once).  When the
                 * reference count exceeds 1, an external holder
                 * exists and the list is not exclusive.
                 */
                bool isExclusive() const;

                /**
                 * @brief Detaches every unique backing @ref Buffer
                 *        that has external holders.
                 *
                 * Clones each shared buffer at most once and leaves
                 * the slice records untouched — each slice keeps the
                 * same @c (bufferIdx, offset, size) and points at the
                 * new clone via the rewritten buffer table entry.
                 */
                void ensureExclusive();

        private:
                struct View {
                        size_t bufferIdx = 0;
                        size_t offset    = 0;
                        size_t size      = 0;
                };

                Buffer::PtrList         _buffers;
                List<View>              _views;

                // Returns the index of @p buf in @c _buffers, inserting if not found.
                // When @p buf is null, returns the largest representable size_t
                // (conceptually "no buffer"); no null entry is stored.
                size_t internBuffer(const Buffer::Ptr &buf);
};

PROMEKI_NAMESPACE_END
