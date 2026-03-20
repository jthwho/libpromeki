/**
 * @file      proav/mediasink.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cassert>
#include <promeki/core/namespace.h>
#include <promeki/core/sharedptr.h>
#include <promeki/core/string.h>
#include <promeki/core/list.h>
#include <promeki/core/queue.h>
#include <promeki/proav/frame.h>

PROMEKI_NAMESPACE_BEGIN

class MediaNode;
class MediaSource;

/**
 * @brief Content hint flags describing what media a sink or source expects.
 * @ingroup proav_pipeline
 */
enum ContentHint : unsigned {
        ContentNone  = 0,       ///< @brief No content hint.
        ContentVideo = 1 << 0,  ///< @brief Carries video data.
        ContentAudio = 1 << 1   ///< @brief Carries audio data.
};

/** @brief Bitwise OR for ContentHint flags. */
inline ContentHint operator|(ContentHint a, ContentHint b) {
        return static_cast<ContentHint>(
                static_cast<unsigned>(a) | static_cast<unsigned>(b));
}

/** @brief Bitwise AND for ContentHint flags. */
inline ContentHint operator&(ContentHint a, ContentHint b) {
        return static_cast<ContentHint>(
                static_cast<unsigned>(a) & static_cast<unsigned>(b));
}

/**
 * @brief Input endpoint on a MediaNode that receives Frame::Ptr.
 * @ingroup proav_pipeline
 *
 * Each MediaSink owns a thread-safe Queue for buffering incoming frames
 * and reports backpressure via the virtual canAcceptFrame() method.
 * When a frame is pushed, the owning node is woken so it can process.
 *
 * MediaSink is managed via SharedPtr for lifetime management. It is an
 * identity object (non-copyable) -- the clone method should never be called.
 */
class MediaSink {
        public:
                RefCount _promeki_refct;
                virtual MediaSink *_promeki_clone() const {
                        assert(false && "MediaSink is not copyable");
                        return nullptr;
                }

                /** @brief Shared pointer type for MediaSink. */
                using Ptr = SharedPtr<MediaSink>;

                /** @brief List of shared pointers to MediaSink. */
                using PtrList = promeki::List<Ptr>;

                /**
                 * @brief Constructs a MediaSink.
                 * @param name Human-readable name for this sink.
                 * @param hint Content hint flags.
                 */
                MediaSink(const String &name = String(), ContentHint hint = ContentNone) :
                        _name(name), _contentHint(hint) { }

                /** @brief Virtual destructor. */
                virtual ~MediaSink() = default;

                /** @brief Returns the sink name. */
                const String &name() const { return _name; }

                /** @brief Sets the sink name. */
                void setName(const String &name) { _name = name; return; }

                /** @brief Returns the content hint flags. */
                ContentHint contentHint() const { return _contentHint; }

                /** @brief Sets the content hint flags. */
                void setContentHint(ContentHint hint) { _contentHint = hint; return; }

                /** @brief Returns the node that owns this sink, or nullptr. */
                MediaNode *node() const { return _node; }

                /** @brief Sets the owning node. */
                void setNode(MediaNode *node) const { _node = node; return; }

                // ---- Queue operations ----

                /**
                 * @brief Pushes a frame into this sink's queue.
                 *
                 * After pushing, wakes the owning node so it can process.
                 *
                 * @param frame The frame to push.
                 */
                void push(Frame::Ptr frame) const;

                /**
                 * @brief Non-blocking dequeue from this sink's queue.
                 * @param[out] frame Receives the dequeued frame on success.
                 * @return true if a frame was dequeued, false if the queue was empty.
                 */
                bool popOrFail(Frame::Ptr &frame) const;

                /** @brief Returns the current queue depth. */
                size_t queueSize() const { return _queue.size(); }

                /** @brief Removes all frames from the queue. */
                void clearQueue() const { _queue.clear(); return; }

                // ---- Backpressure ----

                /**
                 * @brief Returns true if this sink can accept another frame.
                 *
                 * Default implementation checks queueSize() < maxQueueDepth().
                 * Override for custom backpressure logic.
                 *
                 * @return true if the sink can accept a frame.
                 */
                virtual bool canAcceptFrame() const;

                /**
                 * @brief Sets the maximum queue depth for default backpressure.
                 * @param depth Maximum number of frames to buffer.
                 */
                void setMaxQueueDepth(int depth) const { _maxQueueDepth = depth; return; }

                /** @brief Returns the maximum queue depth. */
                int maxQueueDepth() const { return _maxQueueDepth; }

                // ---- Source back-pointers ----

                /**
                 * @brief Registers a source as connected to this sink.
                 *
                 * Called by MediaSource::connect(). Enables backpressure
                 * relief notification.
                 *
                 * @param source The source to register.
                 */
                void addConnectedSource(MediaSource *source) const;

                /**
                 * @brief Unregisters a source from this sink.
                 * @param source The source to unregister.
                 */
                void removeConnectedSource(MediaSource *source) const;

        private:
                String                  _name;
                ContentHint             _contentHint = ContentNone;
                mutable MediaNode       *_node = nullptr;
                mutable Queue<Frame::Ptr> _queue;
                mutable int             _maxQueueDepth = 4;
                mutable List<MediaSource *> _connectedSources;

                void notifySources() const;
};

PROMEKI_NAMESPACE_END
