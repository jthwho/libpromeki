/**
 * @file      pipelinestats.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/pipelinestats.h>

#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

String PipelineStats::toString() const {
        // Single-line renderer matching MediaIOStats::toString's
        // conventions — elide zero counters and empty strings so the
        // common case reads as "state=Running fanout=N" instead of
        // "state=Running fanout=N retries=0 errors=0 eof=0 paused=0".
        StringList parts;

        if(contains(State)) {
                const String st = getAs<String>(State);
                if(!st.isEmpty()) {
                        parts.pushToBack(String::format("state={}", st));
                }
        }
        if(contains(FramesProduced)) {
                parts.pushToBack(String::format("fanout={}",
                        getAs<int64_t>(FramesProduced)));
        }
        if(contains(WriteRetries)) {
                const int64_t v = getAs<int64_t>(WriteRetries);
                if(v > 0) parts.pushToBack(String::format("retries={}", v));
        }
        if(contains(PipelineErrors)) {
                const int64_t v = getAs<int64_t>(PipelineErrors);
                if(v > 0) parts.pushToBack(String::format("errors={}", v));
        }
        if(contains(SourcesAtEof)) {
                const int64_t v = getAs<int64_t>(SourcesAtEof);
                if(v > 0) parts.pushToBack(String::format("eof={}", v));
        }
        if(contains(PausedEdges)) {
                const int64_t v = getAs<int64_t>(PausedEdges);
                if(v > 0) parts.pushToBack(String::format("paused={}", v));
        }
        if(contains(UptimeMs)) {
                const int64_t ms = getAs<int64_t>(UptimeMs);
                if(ms > 0) {
                        parts.pushToBack(String::format("up={:.2f}s",
                                static_cast<double>(ms) / 1000.0));
                }
        }

        return parts.join(String("  "));
}

PROMEKI_NAMESPACE_END
