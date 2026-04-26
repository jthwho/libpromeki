/**
 * @file      eventbroadcaster.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include "eventbroadcaster.h"

#include <promeki/eventloop.h>
#include <promeki/httprequest.h>
#include <promeki/httpserver.h>
#include <promeki/json.h>
#include <promeki/logger.h>
#include <promeki/objectbase.tpp>
#include <promeki/pipelineevent.h>
#include <promeki/string.h>
#include <promeki/websocket.h>

#include "pipelinemanager.h"

using promeki::EventLoop;
using promeki::HttpRequest;
using promeki::HttpServer;
using promeki::JsonObject;
using promeki::List;
using promeki::Logger;
using promeki::ObjectBase;
using promeki::PipelineEvent;
using promeki::String;
using promeki::WebSocket;

namespace promekipipeline {

const String EventBroadcaster::EventsRoute("/api/events");
const String EventBroadcaster::PipelineQueryKey("pipeline");

EventBroadcaster::EventBroadcaster(HttpServer &server,
                                   PipelineManager &mgr,
                                   ObjectBase *parent)
        : ObjectBase(parent), _server(server), _manager(mgr) {
        // Capture the WS-server's loop so the manager-event callback
        // (which fires on whichever thread the manager picked) can
        // marshal sendTextMessage back onto the right thread.
        EventLoop *wsLoop = EventLoop::current();

        _server.routeWebSocket(EventsRoute,
                [this](WebSocket *socket, const HttpRequest &req) {
                        if(socket == nullptr) return;
                        const String filter =
                                req.queryValue(PipelineQueryKey);
                        onUpgrade(socket, filter);
                });

        _subId = _manager.subscribe(
                [this, wsLoop](const String &id, const PipelineEvent &ev) {
                        // Build the wire envelope once per event.
                        // PipelineEvent::toJson already produces the
                        // documented shape (kind/stage/ts/...); we add
                        // the originating pipeline id so the client
                        // can demultiplex without inferring from
                        // anything else.
                        JsonObject env = ev.toJson();
                        env.set("pipeline", id);
                        const String wire = env.toString(0);

                        // Snapshot the subscriber list under the lock,
                        // then post a sendTextMessage callable for each
                        // matching socket onto the WS loop.  Posting
                        // (rather than calling sendTextMessage from
                        // here) is required because WebSocket frame
                        // serialization mutates loop state and must run
                        // on the owning loop.
                        List<WebSocket *> targets;
                        {
                                promeki::Mutex::Locker guard(_mutex);
                                for(auto it = _subs.cbegin();
                                    it != _subs.cend(); ++it) {
                                        const Subscriber &s = it->second;
                                        if(s.pipelineFilter.isEmpty()
                                           || s.pipelineFilter == id) {
                                                targets.pushToBack(s.socket);
                                        }
                                }
                        }
                        if(targets.isEmpty() || wsLoop == nullptr) return;
                        for(size_t i = 0; i < targets.size(); ++i) {
                                WebSocket *ws = targets[i];
                                wsLoop->postCallable([this, ws, wire]() {
                                        // Re-check membership on the
                                        // owning thread: the socket may
                                        // have disconnected between the
                                        // snapshot and the post.
                                        bool stillKnown = false;
                                        {
                                                promeki::Mutex::Locker g(_mutex);
                                                stillKnown =
                                                        _subs.find(ws) != _subs.end();
                                        }
                                        if(!stillKnown) return;
                                        if(!ws->isConnected()) return;
                                        ws->sendTextMessage(wire);
                                });
                        }
                });
}

EventBroadcaster::~EventBroadcaster() {
        if(_subId >= 0) {
                _manager.unsubscribe(_subId);
                _subId = -1;
        }
        // Snapshot the still-connected sockets and tear them down on
        // the owning thread.  We use the same disconnect path the
        // signal-driven dropSubscriber uses so closed sockets free
        // their resources cleanly.
        List<WebSocket *> remaining;
        {
                promeki::Mutex::Locker guard(_mutex);
                for(auto it = _subs.cbegin(); it != _subs.cend(); ++it) {
                        remaining.pushToBack(it->first);
                }
                _subs.clear();
        }
        EventLoop *loop = EventLoop::current();
        for(size_t i = 0; i < remaining.size(); ++i) {
                WebSocket *ws = remaining[i];
                if(ws == nullptr) continue;
                // Tear down on the owning loop.  Signal connections
                // we made at attach time may still fire during abort;
                // dropSubscriber finds an empty map (we cleared above)
                // and does nothing, so the path is idempotent.
                if(loop != nullptr) {
                        loop->postCallable([ws]() {
                                ws->abort();
                                delete ws;
                        });
                } else {
                        ws->abort();
                        delete ws;
                }
        }
}

int EventBroadcaster::subscriberCount() const {
        promeki::Mutex::Locker guard(_mutex);
        return static_cast<int>(_subs.size());
}

void EventBroadcaster::onUpgrade(WebSocket *socket, const String &filter) {
        {
                promeki::Mutex::Locker guard(_mutex);
                Subscriber s;
                s.socket = socket;
                s.pipelineFilter = filter;
                _subs.insert(socket, s);
        }
        // Drop the subscription when the socket disconnects (clean
        // close, peer reset, or transport error all collapse onto
        // disconnectedSignal).  errorOccurredSignal precedes
        // disconnect on the abort path, so listening to both gives us
        // an early-out without double-freeing — dropSubscriber is
        // idempotent.
        socket->disconnectedSignal.connect([this, socket]() {
                dropSubscriber(socket);
        });
        socket->errorOccurredSignal.connect([this, socket](promeki::Error) {
                dropSubscriber(socket);
        });
        promekiDebug("EventBroadcaster: subscriber attached%s%s",
                     filter.isEmpty() ? "" : " filter=",
                     filter.cstr());
}

void EventBroadcaster::dropSubscriber(WebSocket *socket) {
        if(socket == nullptr) return;
        bool wasMember = false;
        {
                promeki::Mutex::Locker guard(_mutex);
                auto it = _subs.find(socket);
                if(it != _subs.end()) {
                        _subs.remove(socket);
                        wasMember = true;
                }
        }
        if(!wasMember) return;
        // Defer the delete onto the WS loop so any outstanding
        // postCallable that ran before the membership check sees the
        // socket alive and is able to skip cleanly.  Mirrors the
        // pattern in debugmodules.cpp's logger streaming route.
        EventLoop *loop = EventLoop::current();
        if(loop == nullptr) {
                delete socket;
                return;
        }
        loop->postCallable([socket]() { delete socket; });
}

} // namespace promekipipeline
