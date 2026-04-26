/**
 * @file      httphandler.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/list.h>
#include <promeki/httprequest.h>
#include <promeki/httpresponse.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Lambda-style HTTP request handler.
 * @ingroup network
 *
 * Signature mirrors Go's @c http.HandlerFunc: the handler receives the
 * request by const reference and writes its reply into the supplied
 * @ref HttpResponse, which the connection then serializes to the wire.
 *
 * Use this directly with @ref HttpRouter::route and
 * @ref HttpServer::route for the common case where a tiny lambda is
 * the entire handler.  When a handler needs explicit state, derive
 * from @ref HttpHandler instead.
 */
using HttpHandlerFunc = std::function<void(const HttpRequest &request,
                                           HttpResponse &response)>;

/** @brief Convenience list of @ref HttpHandlerFunc values. */
using HttpHandlerFuncList = List<HttpHandlerFunc>;

/**
 * @brief Middleware that wraps the next handler in the chain.
 * @ingroup network
 *
 * Middleware runs before the matched handler, in registration order.
 * A middleware that wants the wrapped handler to run calls @p next();
 * a middleware that wants to short-circuit (e.g. an auth check that
 * sets a 401 response) simply returns without calling @p next.
 *
 * @par Example
 * @code
 * server.use([](const HttpRequest &req, HttpResponse &res, auto next) {
 *     if(!isAuthenticated(req)) {
 *         res.setStatus(HttpStatus::Unauthorized);
 *         return;
 *     }
 *     next();
 * });
 * @endcode
 */
using HttpMiddleware = std::function<void(const HttpRequest &request,
                                          HttpResponse &response,
                                          std::function<void()> next)>;

/** @brief Convenience list of @ref HttpMiddleware values. */
using HttpMiddlewareList = List<HttpMiddleware>;

/**
 * @brief Abstract base class for HTTP request handlers.
 * @ingroup network
 *
 * Use this when a handler holds non-trivial state (a database
 * connection, a configuration snapshot, a backing data store) that's
 * cleaner to express as a class than to capture in a lambda.  For
 * lambda-style handlers, prefer @ref HttpHandlerFunc directly.
 *
 * @ref HttpFileHandler and the auto-mounted reflection adapters
 * (@c HttpServer::exposeDatabase, @c HttpServer::exposeLookup) are the
 * primary consumers in-tree.
 *
 * @par Thread Safety
 * Handlers are typically invoked on the @ref HttpServer's owning
 * EventLoop thread.  Concurrent invocation of @c serve on the
 * same handler instance is only safe if the concrete subclass
 * documents that it is — most handlers can assume single-threaded
 * dispatch.
 */
class HttpHandler {
        public:
                // Hand-rolled SHARED pattern instead of PROMEKI_SHARED:
                // the base must stay abstract so concrete leaves (with
                // their own state) can implement serve() — and the
                // PROMEKI_SHARED macro tries to instantiate
                // `new HttpHandler(*this)` in its default _promeki_clone,
                // which fails on an abstract type.  Same approach used
                // by MediaPayload.
                RefCount _promeki_refct;
                virtual HttpHandler *_promeki_clone() const = 0;

                /** @brief Shared pointer type (CoW disabled — handlers carry identity). */
                using Ptr = SharedPtr<HttpHandler, false>;

                virtual ~HttpHandler() = default;

                /**
                 * @brief Handles a request, writing its reply into @p response.
                 *
                 * @param request  The parsed inbound request.
                 * @param response The response object to populate.
                 */
                virtual void serve(const HttpRequest &request,
                                   HttpResponse &response) = 0;
};

/**
 * @brief Adapter that turns an @ref HttpHandlerFunc into an @ref HttpHandler.
 * @ingroup network
 *
 * Used internally by @ref HttpRouter when a route is registered with
 * a lambda-style handler.  Available publicly so callers can store
 * a function-style handler in any container that expects an
 * @ref HttpHandler::Ptr.
 */
class HttpFunctionHandler : public HttpHandler {
        PROMEKI_SHARED_DERIVED(HttpHandler, HttpFunctionHandler)
        public:
                /**
                 * @brief Wraps @p func so it can be stored as an HttpHandler.
                 * @param func The function-style handler to invoke.
                 */
                explicit HttpFunctionHandler(HttpHandlerFunc func) :
                        _func(std::move(func)) {}

                void serve(const HttpRequest &request,
                           HttpResponse &response) override {
                        if(_func) _func(request, response);
                }

        private:
                HttpHandlerFunc _func;
};

PROMEKI_NAMESPACE_END
