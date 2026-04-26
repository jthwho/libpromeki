/**
 * @file      eventbroadcaster.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/map.h>
#include <promeki/mutex.h>
#include <promeki/objectbase.h>
#include <promeki/string.h>

namespace promeki {
        class HttpServer;
        class WebSocket;
}

namespace promekipipeline {

        class PipelineManager;

        /**
 * @brief Bridges per-pipeline events from @ref PipelineManager out to
 *        connected WebSocket clients.
 *
 * The broadcaster registers @c WS @c /api/events on construction.
 * Each connecting socket optionally carries a @c ?pipeline=&lt;id&gt;
 * query parameter; when present, only events whose originating
 * pipeline id matches are forwarded.  Otherwise the socket receives
 * the firehose.  Each event is serialized with the pipeline id
 * stamped in and shipped as one WebSocket text frame.
 *
 * Lifetime: holds references to the @c HttpServer and
 * @ref PipelineManager.  Both must outlive the broadcaster.  All
 * subscribers are torn down (and their WebSockets deleted on the
 * server's loop) in the destructor.
 */
        class EventBroadcaster : public promeki::ObjectBase {
                        PROMEKI_OBJECT(EventBroadcaster, ObjectBase)
                public:
                        /** @brief WebSocket route mounted by this broadcaster. */
                        static const promeki::String EventsRoute;

                        /** @brief Query-parameter key carrying the optional pipeline filter. */
                        static const promeki::String PipelineQueryKey;

                        /**
                 * @brief Constructs a broadcaster bound to @p server / @p mgr.
                 *
                 * @param server HTTP server to attach the WS route to.
                 * @param mgr    Pipeline manager whose events will be
                 *               relayed to subscribers.
                 * @param parent Optional ObjectBase parent.
                 */
                        EventBroadcaster(promeki::HttpServer &server, PipelineManager &mgr,
                                         promeki::ObjectBase *parent = nullptr);

                        /** @brief Destructor.  Unsubscribes from the manager and tears down sockets. */
                        ~EventBroadcaster() override;

                        EventBroadcaster(const EventBroadcaster &) = delete;
                        EventBroadcaster(EventBroadcaster &&) = delete;
                        EventBroadcaster &operator=(const EventBroadcaster &) = delete;
                        EventBroadcaster &operator=(EventBroadcaster &&) = delete;

                        /** @brief Returns the number of currently connected WS subscribers. */
                        int subscriberCount() const;

                private:
                        struct Subscriber {
                                        promeki::WebSocket *socket = nullptr;
                                        promeki::String     pipelineFilter; ///< Empty = no filter.
                        };

                        /** @brief Adopts a freshly-upgraded WS into the subscriber map. */
                        void onUpgrade(promeki::WebSocket *socket, const promeki::String &filter);

                        /** @brief Drops a subscriber and schedules its WebSocket for deletion. */
                        void dropSubscriber(promeki::WebSocket *socket);

                        promeki::HttpServer &_server;
                        PipelineManager     &_manager;
                        int                  _subId = -1;

                        mutable promeki::Mutex                         _mutex;
                        promeki::Map<promeki::WebSocket *, Subscriber> _subs;
        };

} // namespace promekipipeline
