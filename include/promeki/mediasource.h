/**
 * @file      mediasource.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cassert>
#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/string.h>
#include <promeki/list.h>
#include <promeki/mediasink.h>
#include <promeki/frame.h>

PROMEKI_NAMESPACE_BEGIN

class MediaNode;

/**
 * @brief Output endpoint on a MediaNode that delivers Frame::Ptr to connected sinks.
 * @ingroup pipeline
 *
 * Each MediaSource maintains a list of connected MediaSink::Ptr objects.
 * When deliver() is called, the frame is pushed to every connected sink.
 * sinksReadyForFrame() checks backpressure across all connected sinks.
 *
 * MediaSource is managed via SharedPtr for lifetime management. It is an
 * identity object (non-copyable) -- the clone method should never be called.
 */
class MediaSource {
        public:
                RefCount _promeki_refct;
                MediaSource *_promeki_clone() const {
                        assert(false && "MediaSource is not copyable");
                        return nullptr;
                }

                /** @brief Shared pointer type for MediaSource. */
                using Ptr = SharedPtr<MediaSource>;

                /** @brief List of shared pointers to MediaSource. */
                using PtrList = promeki::List<Ptr>;

                /**
                 * @brief Constructs a MediaSource.
                 * @param name Human-readable name for this source.
                 * @param hint Content hint flags.
                 */
                MediaSource(const String &name = String(), ContentHint hint = ContentNone) :
                        _name(name), _contentHint(hint) { }

                /** @brief Destructor. Disconnects all sinks. */
                ~MediaSource();

                /** @brief Returns the source name. */
                const String &name() const { return _name; }

                /** @brief Sets the source name. */
                void setName(const String &name) { _name = name; return; }

                /** @brief Returns the content hint flags. */
                ContentHint contentHint() const { return _contentHint; }

                /** @brief Sets the content hint flags. */
                void setContentHint(ContentHint hint) { _contentHint = hint; return; }

                /** @brief Returns the node that owns this source, or nullptr. */
                MediaNode *node() const { return _node; }

                /** @brief Sets the owning node. */
                void setNode(MediaNode *node) const { _node = node; return; }

                // ---- Connection management ----

                /**
                 * @brief Connects a sink to this source.
                 *
                 * The sink's back-pointer to this source is also registered.
                 *
                 * @param sink The sink to connect.
                 */
                void connect(MediaSink::Ptr sink) const;

                /**
                 * @brief Disconnects a sink from this source.
                 * @param sink The sink to disconnect.
                 */
                void disconnect(MediaSink::Ptr sink) const;

                /**
                 * @brief Disconnects all sinks from this source.
                 */
                void disconnectAll() const;

                /** @brief Returns the list of connected sinks. */
                const MediaSink::PtrList &connectedSinks() const { return _connectedSinks; }

                /** @brief Returns true if at least one sink is connected. */
                bool isConnected() const { return !_connectedSinks.isEmpty(); }

                // ---- Delivery ----

                /**
                 * @brief Delivers a frame to all connected sinks.
                 *
                 * Pushes the frame to each connected sink's queue.
                 *
                 * @param frame The frame to deliver.
                 */
                void deliver(Frame::Ptr frame) const;

                /**
                 * @brief Returns true if all connected sinks can accept a frame.
                 *
                 * Checks canAcceptFrame() on every connected sink. Returns
                 * true if there are no connected sinks.
                 *
                 * @return true if all sinks are ready.
                 */
                bool sinksReadyForFrame() const;

        private:
                String                  _name;
                ContentHint             _contentHint = ContentNone;
                mutable MediaNode       *_node = nullptr;
                mutable MediaSink::PtrList _connectedSinks;
};

PROMEKI_NAMESPACE_END
