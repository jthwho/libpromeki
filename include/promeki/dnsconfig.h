/**
 * @file      dnsconfig.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/socketaddress.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Stub-resolver configuration: which servers to talk to,
 *        what search domains to try, how to retry.
 * @ingroup network
 *
 * Mirrors the @c /etc/resolv.conf knobs documented in
 * @c resolv.conf(5).  Populated from one of three sources, in order of
 * preference:
 *  1. Caller-supplied via @ref DnsResolver::setNameservers / friends.
 *  2. Auto-loaded from the host:
 *      - POSIX (Linux, macOS, BSD): @c /etc/resolv.conf.
 *      - Windows: @c GetAdaptersAddresses() per-adapter DNS list.
 *  3. Fallback defaults — @c 127.0.0.53 (systemd-resolved stub),
 *     @c 127.0.0.1 — used when neither of the above yields any
 *     servers.  An empty list after fallback means the host has no
 *     usable resolver; @ref DnsResolver returns
 *     @ref Error::NotReady from queries in that state.
 *
 * Simple value class — cheap to copy, no @c PROMEKI_SHARED_FINAL.
 * Not registered with the @ref Variant system.
 *
 * @par Refresh policy
 * The class is a snapshot.  The resolver loads one at construction
 * via @ref loadSystem and re-loads on demand when a caller asks for
 * @ref DnsResolver::reloadSystemConfig — the watchdog cost of
 * polling @c stat() every query is not worth saving the
 * occasional mtime check for the resolver's lifetime, which usually
 * spans the entire process.
 */
class DnsConfig {
        public:
                /** @brief Default nameserver fallback when no system config is found. */
                static const char *DefaultFallbackServerCstr;

                /** @brief Default per-server UDP query timeout (RFC-typical: 5 s). */
                static constexpr int64_t DefaultTimeoutMs = 5000;

                /** @brief Default number of attempts before giving up on the whole resolver. */
                static constexpr int DefaultAttempts = 2;

                /** @brief Default RFC 1034 @c ndots value (forces search-list expansion below this dot count). */
                static constexpr int DefaultNdots = 1;

                /** @brief Default UDP port DNS speaks on (RFC 1035). */
                static constexpr uint16_t DefaultPort = 53;

                /** @brief Constructs an empty config with defaults. */
                DnsConfig() = default;

                /**
                 * @brief Reads the host's stub-resolver configuration.
                 *
                 * On POSIX hosts parses @c /etc/resolv.conf and returns
                 * the resulting config; on Windows uses
                 * @c GetAdaptersAddresses to walk every adapter and
                 * collect every DNS server it lists.
                 *
                 * Hosts with no detectable resolver fall back to
                 * @ref DefaultFallbackServerCstr.  The returned
                 * config has at least one server unless the platform
                 * does not implement system enumeration (Emscripten:
                 * returns @ref Error::NotSupported and an empty config).
                 *
                 * @return The configuration, or an error if the
                 *         platform cannot enumerate it.
                 */
                static Result<DnsConfig> loadSystem();

                /**
                 * @brief Parses the text form of a @c /etc/resolv.conf file.
                 *
                 * Exposed for unit tests and for callers that hold the
                 * file contents in memory (containers, custom mounts).
                 * Lines starting with @c '#' or @c ';' are comments.
                 * The supported directives are:
                 *  - @c nameserver \<ip\>
                 *  - @c search \<dom1\> \<dom2\> ...
                 *  - @c domain \<dom\>
                 *  - @c options ndots:N timeout:N attempts:N
                 */
                static DnsConfig fromResolvConfText(const String &text);

                /** @brief Returns @c true when the config has no usable nameservers. */
                bool isEmpty() const { return _nameservers.isEmpty(); }

                /** @brief Returns the configured nameserver list (in order). */
                const List<SocketAddress> &nameservers() const { return _nameservers; }
                /** @brief Replaces the nameserver list. */
                void setNameservers(const List<SocketAddress> &servers) { _nameservers = servers; }

                /** @brief Returns the search domain list (RFC 1034 §3.1). */
                const StringList &searchDomains() const { return _searchDomains; }
                /** @brief Replaces the search domain list. */
                void setSearchDomains(const StringList &domains) { _searchDomains = domains; }

                /** @brief Returns the per-server UDP timeout. */
                Duration timeout() const { return _timeout; }
                /** @brief Sets the per-server UDP timeout. */
                void setTimeout(const Duration &t) { _timeout = t; }

                /** @brief Returns the per-server retry count. */
                int attempts() const { return _attempts; }
                /** @brief Sets the per-server retry count. */
                void setAttempts(int n) { _attempts = (n > 0) ? n : 1; }

                /** @brief Returns the @c ndots threshold for search-list expansion. */
                int ndots() const { return _ndots; }
                /** @brief Sets the @c ndots threshold. */
                void setNdots(int n) { _ndots = (n >= 0) ? n : 0; }

        private:
                List<SocketAddress> _nameservers;
                StringList          _searchDomains;
                Duration            _timeout  = Duration::fromMilliseconds(DefaultTimeoutMs);
                int                 _attempts = DefaultAttempts;
                int                 _ndots    = DefaultNdots;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
