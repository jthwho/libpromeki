/**
 * @file      httprouter.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/list.h>
#include <promeki/hashmap.h>
#include <promeki/httpmethod.h>
#include <promeki/httprequest.h>
#include <promeki/httpresponse.h>
#include <promeki/httphandler.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Pattern-based dispatcher for HTTP requests.
 * @ingroup network
 *
 * Models Go's @c http.ServeMux: route patterns map to handlers, with
 * exact matches preferred over parameterized matches and longer
 * literal segments preferred over shorter ones.  Method-aware: a
 * pattern registered for @ref HttpMethod::Get does not respond to
 * @c POST, and the default 405 handler fills in the @c Allow header
 * automatically based on the methods that *did* match the path.
 *
 * @par Pattern syntax
 *  - @c "/items"            — literal path, exact match.
 *  - @c "/items/{id}"       — single-segment placeholder; matches
 *                             @c "/items/42" but not @c "/items/42/edit".
 *  - @c "/files/{path:*}"   — greedy tail placeholder; matches the rest
 *                             of the path (zero or more segments).
 *  - Trailing slash on a literal pattern is significant: @c "/api/"
 *    only matches @c "/api/" exactly, not @c "/api".
 *
 * Path parameters captured during matching are stuffed into the
 * @ref HttpRequest::pathParams map (the request is mutated in-place
 * inside @ref dispatch); handlers read them via
 * @ref HttpRequest::pathParam.
 *
 * @par Middleware
 * Middleware registered via @ref use runs before the matched handler
 * in registration order.  Each middleware receives a @c next callable
 * it must invoke (synchronously) to continue the chain — short-circuit
 * by simply returning without calling @c next.
 *
 * @par Example
 * @code
 * HttpRouter mux;
 * mux.use([](const HttpRequest &req, HttpResponse &res, auto next) {
 *     promekiInfo("%s %s", req.method().wireName().cstr(), req.path().cstr());
 *     next();
 * });
 * mux.route("/api/items", HttpMethod::Get, [](const auto &, auto &res) {
 *     res.setJson(allItems());
 * });
 * mux.route("/api/items/{id}", HttpMethod::Get, [](const auto &req, auto &res) {
 *     auto id = req.pathParam("id");
 *     // ...
 * });
 * @endcode
 */
class HttpRouter {
        public:
                /** @brief Constructs an empty router. */
                HttpRouter();

                /** @brief Destructor. */
                ~HttpRouter();

                /**
                 * @brief Registers a function-style handler for @p pattern + @p method.
                 *
                 * Multiple methods can be registered against the same
                 * pattern; mismatched-method requests yield 405 with
                 * the @c Allow header reporting all registered methods
                 * for that pattern.
                 */
                void route(const String &pattern,
                           const HttpMethod &method,
                           HttpHandlerFunc handler);

                /**
                 * @brief Registers a typed handler for @p pattern + @p method.
                 *
                 * Shareable: the same @ref HttpHandler::Ptr can be
                 * mounted on multiple patterns.
                 */
                void route(const String &pattern,
                           const HttpMethod &method,
                           HttpHandler::Ptr handler);

                /**
                 * @brief Registers a handler for any HTTP method.
                 *
                 * Useful for catch-all middleware-style endpoints
                 * (CORS, health, observability) that want to inspect
                 * the request method themselves.
                 */
                void any(const String &pattern, HttpHandlerFunc handler);
                /// @copydoc any
                void any(const String &pattern, HttpHandler::Ptr handler);

                /**
                 * @brief Adds a middleware to the chain.
                 *
                 * Middleware runs before any matched route handler, in
                 * the order it was registered.
                 */
                void use(HttpMiddleware middleware);

                /**
                 * @brief Replaces the handler invoked when no route matches.
                 *
                 * Defaults to a 404 response with a plain-text body.
                 */
                void setNotFoundHandler(HttpHandlerFunc handler);

                /**
                 * @brief Replaces the handler invoked when a route matches
                 *        but no method does.
                 *
                 * The default handler reports a 405 with the @c Allow
                 * header set to the comma-separated list of methods
                 * registered for the matched pattern.
                 */
                void setMethodNotAllowedHandler(HttpHandlerFunc handler);

                /**
                 * @brief Dispatches @p request, populating @p response.
                 *
                 * Walks the middleware chain; the chain terminus
                 * matches the request against the registered routes
                 * and either invokes the matched handler or one of the
                 * default error handlers (404 / 405).
                 */
                void dispatch(HttpRequest &request, HttpResponse &response) const;

                /** @brief Number of routes registered. */
                int routeCount() const;

                /** @brief Whether at least one route is registered. */
                bool hasRoutes() const { return routeCount() > 0; }

                /** @brief Removes every route, middleware, and override handler. */
                void clear();

        private:
                struct PatternSegment {
                        using List = promeki::List<PatternSegment>;
                        enum Kind { Literal, Param, Greedy };
                        Kind    kind = Literal;
                        String  text;          ///< Literal text or param name.
                };

                struct Pattern {
                        using List = promeki::List<Pattern>;
                        String                  source;
                        PatternSegment::List    segments;
                        bool                    trailingSlash = false;
                        bool                    isExact = true;  ///< true when only Literal segments
                };

                struct Route {
                        using List = promeki::List<Route>;
                        Pattern                 pattern;
                        int                     methodValue = -1;  ///< -1 == any
                        HttpHandler::Ptr        handler;
                };

                static Pattern compilePattern(const String &source);
                static bool matchPattern(const Pattern &pattern,
                                         const String &path,
                                         HashMap<String, String> &paramsOut);
                static int patternScore(const Pattern &pattern);

                void runChain(HttpRequest &request, HttpResponse &response,
                              HttpHandlerFunc terminal) const;

                static void defaultNotFound(const HttpRequest &request,
                                            HttpResponse &response);
                static void defaultMethodNotAllowed(const HttpRequest &request,
                                                    HttpResponse &response);

                Route::List             _routes;
                HttpMiddlewareList      _middleware;
                HttpHandlerFunc         _notFound;
                HttpHandlerFunc         _methodNotAllowed;
};

PROMEKI_NAMESPACE_END
