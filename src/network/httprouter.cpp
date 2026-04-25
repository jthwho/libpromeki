/**
 * @file      httprouter.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/httprouter.h>
#include <promeki/url.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

HttpRouter::HttpRouter() = default;
HttpRouter::~HttpRouter() = default;

void HttpRouter::route(const String &pattern,
                       const HttpMethod &method,
                       HttpHandlerFunc handler) {
        route(pattern, method,
              HttpHandler::Ptr::takeOwnership(
                      new HttpFunctionHandler(std::move(handler))));
}

void HttpRouter::route(const String &pattern,
                       const HttpMethod &method,
                       HttpHandler::Ptr handler) {
        Route r;
        r.pattern     = compilePattern(pattern);
        r.methodValue = method.value();
        r.handler     = std::move(handler);
        _routes.pushToBack(std::move(r));
}

void HttpRouter::any(const String &pattern, HttpHandlerFunc handler) {
        any(pattern,
            HttpHandler::Ptr::takeOwnership(
                    new HttpFunctionHandler(std::move(handler))));
}

void HttpRouter::any(const String &pattern, HttpHandler::Ptr handler) {
        Route r;
        r.pattern     = compilePattern(pattern);
        r.methodValue = -1;       // wildcard
        r.handler     = std::move(handler);
        _routes.pushToBack(std::move(r));
}

void HttpRouter::use(HttpMiddleware middleware) {
        _middleware.pushToBack(std::move(middleware));
}

void HttpRouter::setNotFoundHandler(HttpHandlerFunc handler) {
        _notFound = std::move(handler);
}

void HttpRouter::setMethodNotAllowedHandler(HttpHandlerFunc handler) {
        _methodNotAllowed = std::move(handler);
}

int HttpRouter::routeCount() const {
        return static_cast<int>(_routes.size());
}

void HttpRouter::clear() {
        _routes.clear();
        _middleware.clear();
        _notFound = HttpHandlerFunc{};
        _methodNotAllowed = HttpHandlerFunc{};
}

// ============================================================
// Pattern compilation and matching
// ============================================================

HttpRouter::Pattern HttpRouter::compilePattern(const String &source) {
        Pattern out;
        out.source = source;

        // Treat empty pattern as "/"; both indicate the site root.
        const String s = source.isEmpty() ? String("/") : source;
        out.trailingSlash = (s.byteCount() > 1) &&
                            (s.cstr()[s.byteCount() - 1] == '/');

        // Split on '/'; the leading slash is significant — it produces
        // an empty-named "root" that we drop, so segments[] only holds
        // real path components.
        StringList parts = s.split("/");
        for(size_t i = 0; i < parts.size(); ++i) {
                const String &p = parts[i];
                if(p.isEmpty()) continue;        // empty between // or trailing /

                PatternSegment seg;
                if(p.cstr()[0] == '{' && p.cstr()[p.byteCount() - 1] == '}') {
                        // {name} or {name:*}
                        const String inner = p.mid(1, p.byteCount() - 2);
                        const auto colonIdx = inner.find(':');
                        if(colonIdx != String::npos) {
                                const String name = inner.left(colonIdx);
                                const String tail = inner.mid(colonIdx + 1);
                                if(tail == "*") {
                                        seg.kind = PatternSegment::Greedy;
                                        seg.text = name;
                                        out.isExact = false;
                                        out.segments.pushToBack(seg);
                                        // Greedy must be the last segment;
                                        // anything after it is ignored.
                                        break;
                                }
                                // Unknown qualifier: fall through to a
                                // plain Param rather than fail loudly.
                                seg.kind = PatternSegment::Param;
                                seg.text = name;
                        } else {
                                seg.kind = PatternSegment::Param;
                                seg.text = inner;
                        }
                        out.isExact = false;
                } else {
                        seg.kind = PatternSegment::Literal;
                        seg.text = p;
                }
                out.segments.pushToBack(seg);
        }
        return out;
}

bool HttpRouter::matchPattern(const Pattern &pattern,
                              const String &path,
                              HashMap<String, String> &paramsOut) {
        // Split the request path into components.  The split rule
        // mirrors compilePattern: leading and trailing empties (from
        // the edge slashes) are dropped so segment indices line up.
        const String p = path.isEmpty() ? String("/") : path;
        const bool requestTrailing = (p.byteCount() > 1) &&
                                     (p.cstr()[p.byteCount() - 1] == '/');
        StringList parts = p.split("/");
        StringList segments;
        for(size_t i = 0; i < parts.size(); ++i) {
                if(!parts[i].isEmpty()) segments.pushToBack(parts[i]);
        }

        size_t pi = 0;          // pattern segment index
        size_t si = 0;          // path segment index
        for(; pi < pattern.segments.size(); ++pi) {
                const PatternSegment &seg = pattern.segments[pi];

                if(seg.kind == PatternSegment::Greedy) {
                        // Capture the rest verbatim, including slashes.
                        String tail;
                        for(size_t i = si; i < segments.size(); ++i) {
                                if(!tail.isEmpty()) tail += "/";
                                tail += segments[i];
                        }
                        // Preserve trailing slash if the request had
                        // one — useful for filesystem handlers that
                        // care about directory-vs-file intent.
                        if(requestTrailing && !tail.isEmpty()) tail += "/";
                        paramsOut.insert(seg.text, tail);
                        si = segments.size();
                        ++pi;
                        break;
                }

                if(si >= segments.size()) return false;

                if(seg.kind == PatternSegment::Param) {
                        paramsOut.insert(seg.text, segments[si]);
                } else {
                        // Literal: byte-exact compare.  Case-sensitive
                        // matches RFC 9110: path segments are case-
                        // sensitive on the wire even though the
                        // host/scheme are not.
                        if(seg.text != segments[si]) return false;
                }
                ++si;
        }

        // Reject extra path segments unless we matched a Greedy.
        if(si != segments.size()) return false;

        // Trailing-slash discipline: a literal pattern with a
        // trailing slash matches only requests with the same
        // trailing-slash shape.  Patterns that end in a parameter
        // (greedy or not) inherit the request's slash by virtue of
        // the segment-only comparison above.
        if(pattern.isExact && pattern.trailingSlash != requestTrailing) {
                return false;
        }
        return true;
}

int HttpRouter::patternScore(const Pattern &pattern) {
        // Higher score wins: literal segments beat parameterized ones,
        // exact patterns beat greedy ones.
        int score = 0;
        for(size_t i = 0; i < pattern.segments.size(); ++i) {
                switch(pattern.segments[i].kind) {
                        case PatternSegment::Literal: score += 1000; break;
                        case PatternSegment::Param:   score += 10;   break;
                        case PatternSegment::Greedy:  score += 1;    break;
                }
        }
        if(pattern.isExact) score += 5000;
        return score;
}

// ============================================================
// Dispatch
// ============================================================

void HttpRouter::runChain(HttpRequest &request, HttpResponse &response,
                          HttpHandlerFunc terminal) const {
        // Build the chain from the inside out: the terminal handler
        // is the innermost lambda, each middleware wraps the previous
        // step.  Captures are by value (the HttpMiddleware copy stays
        // cheap because std::function shares the underlying target).
        //
        // We construct the chain on the stack via a recursive lambda
        // index `i` so middleware can call next() multiple times only
        // if it really wants to (each next() invocation re-enters
        // the same frame's continuation).
        std::function<void(size_t)> step;
        step = [&](size_t i) {
                if(i >= _middleware.size()) {
                        if(terminal) terminal(request, response);
                        return;
                }
                _middleware[i](request, response, [&, i]() { step(i + 1); });
        };
        step(0);
}

void HttpRouter::dispatch(HttpRequest &request, HttpResponse &response) const {
        // Run middleware chain, then route-match at the terminus.
        runChain(request, response, [&](const HttpRequest &, HttpResponse &res) {
                // Find best route by score; track method-mismatches
                // separately so we can report 405 + Allow correctly.
                int                     bestScore = -1;
                const Route             *best = nullptr;
                HashMap<String, String> bestParams;

                StringList              allowMethods;
                bool                    pathMatched = false;

                for(size_t i = 0; i < _routes.size(); ++i) {
                        const Route &r = _routes[i];
                        HashMap<String, String> params;
                        if(!matchPattern(r.pattern, request.path(), params)) {
                                continue;
                        }
                        pathMatched = true;

                        // Track Allow header info regardless of method.
                        if(r.methodValue >= 0) {
                                HttpMethod m{r.methodValue};
                                const String name = m.wireName();
                                if(!allowMethods.contains(name)) {
                                        allowMethods.pushToBack(name);
                                }
                        }

                        // Method filter.  -1 means "any" and matches
                        // every request method.
                        if(r.methodValue >= 0 &&
                           r.methodValue != request.method().value()) {
                                continue;
                        }

                        const int score = patternScore(r.pattern);
                        if(score > bestScore) {
                                bestScore  = score;
                                best       = &r;
                                bestParams = std::move(params);
                        }
                }

                if(best != nullptr) {
                        request.setPathParams(bestParams);
                        if(best->handler.isValid()) {
                                // Cast away const for serve() — the
                                // handler ptr is a SharedPtr<HttpHandler, false>
                                // whose const operator-> exposes only
                                // const methods.  serve() is non-const
                                // because subclasses may legitimately
                                // need to mutate state; route storage
                                // is logically a mutable cache.
                                const_cast<HttpHandler *>(best->handler.ptr())
                                        ->serve(request, res);
                        }
                        return;
                }

                if(pathMatched) {
                        // 405: stuff Allow header (joined here for the
                        // default handler; custom handlers can read the
                        // same value via the response header before
                        // overwriting).
                        if(_methodNotAllowed) {
                                res.setHeader("Allow", allowMethods.join(", "));
                                _methodNotAllowed(request, res);
                        } else {
                                res = HttpResponse::methodNotAllowed(allowMethods.join(", "));
                        }
                        return;
                }

                // 404
                if(_notFound) _notFound(request, res);
                else          defaultNotFound(request, res);
        });
}

// ============================================================
// Default error handlers
// ============================================================

void HttpRouter::defaultNotFound(const HttpRequest &request,
                                 HttpResponse &response) {
        response = HttpResponse::notFound(
                String::sprintf("404 Not Found: %s", request.path().cstr()));
}

void HttpRouter::defaultMethodNotAllowed(const HttpRequest &,
                                         HttpResponse &response) {
        // Caller pre-sets the Allow header before invoking us.
        response.setStatus(HttpStatus::MethodNotAllowed);
        response.setText(response.status().reasonPhrase());
}

PROMEKI_NAMESPACE_END
