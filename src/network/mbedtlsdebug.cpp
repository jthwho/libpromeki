/**
 * @file      mbedtlsdebug.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Owns the bridge from mbedTLS's internal trace callback
 * (mbedtls_ssl_conf_dbg) into libpromeki's logger.  Lives in its own
 * translation unit so the @c PROMEKI_DEBUG registration does not
 * collide with the @c SslContext module (only one @c PROMEKI_DEBUG
 * per TU; the anonymous-namespace mechanism it uses declares static
 * @c _promeki_debug_enabled at file scope).
 *
 * Routing policy:
 *
 *  mbedTLS's debug levels are not a clean errors-vs-info split —
 *  level 1 contains both genuine errors ("Invalid record size",
 *  "bad server hello") and benign TLS 1.3 state transitions ("Switch
 *  to handshake keys").  Level-based routing alone would either spam
 *  warnings on every handshake or silently drop real errors.  So:
 *
 *  - The callback is **always installed**, so mbedTLS messages can
 *    reach the libpromeki log without any opt-in.
 *  - Each message is content-checked for error-vocabulary keywords
 *    (@c fail / @c error / @c fatal / @c invalid / @c bad /
 *    @c unsupport / @c reject / @c alert).  Matches are routed to
 *    @c promekiWarn so a real handshake / record / parse failure
 *    surfaces in field logs without anyone needing to enable a
 *    debug knob.
 *  - Non-matching messages (state transitions, key schedule
 *    progress, verbose dumps) are routed to @c promekiDebug, gated
 *    by the @c MbedTlsInternal debug module — chatty enough that
 *    default-off matters.  Enable with:
 *
 *    @code
 *    PROMEKI_DEBUG=MbedTlsInternal promeki-fetch-model tiny
 *    @endcode
 *
 * mbedTLS's process-global threshold is set just once (the first
 * SslContext to come up wins): 1 when @c MbedTlsInternal is off (only
 * level-1 messages are formatted; that's where the error vocabulary
 * lives), 4 when it's on (mbedTLS formats everything and the
 * callback decides what to drop).  Runtime cost is negligible in
 * normal operation, and we never miss an error.
 */

#include <promeki/config.h>
#if PROMEKI_ENABLE_TLS

#include <promeki/namespace.h>
#include <promeki/logger.h>
#include <promeki/string.h>
#include <promeki/once.h>
#include <mbedtls/ssl.h>
#include <mbedtls/debug.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(MbedTlsInternal);

namespace {

        // OnceFlag so mbedtls_debug_set_threshold is called exactly
        // once for the lifetime of the process even if many
        // SslContexts go through promekiMbedTlsConfigureDebug.
        // The threshold is process-global in mbedTLS, so a single
        // setpoint is correct.
        OnceFlag g_thresholdSet;

        // Substring keywords that promote a mbedTLS message from
        // the gated-debug stream to an always-on warn.  Matched
        // case-insensitively against the message body.  The list is
        // intentionally short — every entry has to genuinely signal
        // "something is wrong" in mbedTLS's own log vocabulary, so a
        // false-positive (an informational message that happens to
        // contain one of these words) is rare enough to ignore.
        static constexpr const char *kErrorKeywords[] = {
                "fail",      // "allocation failed", "handshake failed"
                "error",     // "X.509 error", "internal error"
                "fatal",     // "fatal alert received"
                "invalid",   // "Invalid key share", "Invalid PSK identity"
                "bad ",      // "bad server hello", "bad record mac" — note trailing space
                "unsupport", // "Unsupported version of TLS"
                "reject",    // "rejected the handshake"
                "alert",     // mbedTLS surfaces received alert types verbatim
                "no point format in common",
                "no group available",
                "non-matching",
        };
        static constexpr size_t kErrorKeywordCount =
                sizeof(kErrorKeywords) / sizeof(kErrorKeywords[0]);

        static bool looksLikeError(const String &s) {
                const String lower = s.toLower();
                for (size_t i = 0; i < kErrorKeywordCount; ++i) {
                        if (lower.contains(kErrorKeywords[i])) return true;
                }
                return false;
        }

        // mbedTLS internal trace callback.  Strips the trailing
        // newline/whitespace mbedTLS appends so the line composes
        // cleanly with the libpromeki logger's own formatting.
        // file/line are forwarded so the operator can grep the
        // mbedTLS source for context.
        static void mbedTlsDbgCallback(void * /*ctx*/, int level, const char *file, int line,
                                       const char *msg) {
                String s(msg);
                while (!s.isEmpty() &&
                       (s.right(1) == "\n" || s.right(1) == "\r" || s.right(1) == " ")) {
                        s = s.left(s.length() - 1);
                }
                if (s.isEmpty()) return;
                if (looksLikeError(s)) {
                        // Always-on warn for real errors — operator
                        // sees these even with debug off.  This is
                        // the message the operator actually wants
                        // to find in a field log.
                        Logger::defaultLogger().log(Logger::LogLevel::Warn, file, line,
                                                    String::sprintf("mbedtls: %s", s.cstr()));
                } else if (_promeki_debug_enabled) {
                        // State changes, key schedule progress,
                        // verbose record-level trace.  Gated by the
                        // MbedTlsInternal debug module so the
                        // default-off behavior keeps the log
                        // readable.
                        Logger::defaultLogger().log(
                                Logger::LogLevel::Debug, file, line,
                                String::sprintf("mbedtls[%d] %s", level, s.cstr()));
                }
        }

} // anonymous namespace

// Public entry point: called from SslContext::Impl::ensureConfig
// after mbedtls_ssl_config_defaults and our policy knobs run.  The
// callback is always installed so mbedTLS errors (level 1) always
// reach the libpromeki log; verbose levels are gated inside the
// callback by the MbedTlsInternal debug module.
void promekiMbedTlsConfigureDebug(mbedtls_ssl_config *conf) {
        callOnce(g_thresholdSet, []() {
                // 1 = errors only (keeps mbedTLS from formatting
                // verbose messages we'd just drop).  4 = verbose
                // record-level trace.  See file header for the
                // routing policy.
                mbedtls_debug_set_threshold(_promeki_debug_enabled ? 4 : 1);
        });
        mbedtls_ssl_conf_dbg(conf, &mbedTlsDbgCallback, nullptr);
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_TLS
