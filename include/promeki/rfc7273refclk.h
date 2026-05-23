/**
 * @file      rfc7273refclk.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstdint>
#include <promeki/eui64.h>
#include <promeki/macaddress.h>
#include <promeki/namespace.h>
#include <promeki/result.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Parsed RFC 7273 @c ts-refclk SDP attribute value.
 * @ingroup network
 *
 * RFC 7273 §4 defines the @c ts-refclk attribute that an SDP session
 * uses to identify which clock the sender's RTP timestamps are slaved
 * to.  SMPTE ST 2110-10 §8.2 narrows the choice to PTP-derived clocks
 * (§4.5) and the per-sender local clock (§4.4); this class is the
 * structured form of those values and round-trips with the on-wire
 * attribute string.
 *
 * The @c "ts-refclk:" prefix is **not** included in the value
 * @ref toSdpValue() returns / @ref fromSdpValue() consumes — the
 * caller is the SDP layer that already owns the attribute name.
 *
 * @par Supported forms
 * - @c "ptp=<profile>:<gmid>:<domain>" (§4.5, ST 2110-10 default).
 *   The profile is normally @c "IEEE1588-2008".  Older RFC 7273
 *   senders may emit just @c "ptp=<profile>" or
 *   @c "ptp=<profile>:<gmid>" — both round-trip through @ref Kind::Ptp
 *   with @ref grandmasterId / @ref domain absent or partial.
 * - @c "ptp=<profile>:traceable" (RFC 7273 §4.7).  ST 2110-10 §8.2
 *   uses this when the slave reports a traceable grandmaster
 *   (@c clockAccuracy ≤ 250 ns and @c timeTraceable asserted) but the
 *   specific GMID is intentionally elided.
 * - @c "localmac=<EUI-48>" (§4.4).  Identifies the sender's local
 *   clock by MAC.  Receivers can correlate streams from the same
 *   sender without trusting a shared grandmaster.
 * - @c "local" (§4.3).  The legacy unqualified form of @c localmac;
 *   produced by some older senders.  Treated as a degenerate
 *   @c Kind::LocalMac with an empty MAC.
 *
 * @par Example
 * @code
 * auto refClk = Rfc7273RefClk::ptpTraceable("IEEE1588-2008");
 * String attr = refClk.toSdpValue();  // "ptp=IEEE1588-2008:traceable"
 *
 * auto [parsed, err] = Rfc7273RefClk::fromSdpValue(attr);
 * assert(err.isOk());
 * assert(parsed.kind() == Rfc7273RefClk::Kind::Ptp);
 * assert(parsed.isTraceable());
 * @endcode
 *
 * @par Thread Safety
 * Plain value type.  Distinct instances may be used concurrently; a
 * single instance is conditionally thread-safe.
 */
class Rfc7273RefClk {
        public:
                /** @brief Wire-form kind. */
                enum class Kind {
                        None,     ///< @brief Invalid / empty value.
                        Ptp,      ///< @brief @c "ptp=..." form (§4.5 / §4.7).
                        LocalMac, ///< @brief @c "localmac=..." or @c "local" form (§4.4 / §4.3).
                };

                /** @brief Default PTP profile string for ST 2110 (RFC 7273 §4.5). */
                static constexpr const char *DefaultPtpProfile = "IEEE1588-2008";

                /** @brief Default-constructs an empty / invalid value. */
                Rfc7273RefClk() = default;

                /**
                 * @brief Builds a @c ptp=<profile>[:<gmid>[:<domain>]] form.
                 *
                 * Pass a null @c gmid to emit just @c "ptp=<profile>"
                 * (no grandmaster identifier).  Pass a non-null
                 * @c gmid to emit @c "ptp=<profile>:<gmid>:<domain>" —
                 * RFC 7273 requires the domain alongside the gmid
                 * because @c ":<domain>" without a gmid is malformed.
                 *
                 * @param profile PTP profile string (e.g. @c "IEEE1588-2008").
                 * @param gmid    Grandmaster identifier, or null for no gmid.
                 * @param domain  PTP domain (0-127); ignored if @c gmid is null.
                 * @return The constructed value.
                 */
                static Rfc7273RefClk ptp(const String &profile, const EUI64 &gmid = EUI64(),
                                         uint8_t domain = 0);

                /**
                 * @brief Builds a @c ptp=<profile>:traceable form (§4.7).
                 *
                 * Use when the slave reports a traceable grandmaster
                 * but the specific GMID is deliberately omitted.
                 *
                 * @param profile PTP profile string.
                 * @return The constructed value.
                 */
                static Rfc7273RefClk ptpTraceable(const String &profile);

                /**
                 * @brief Builds a @c localmac=<EUI-48> form (§4.4).
                 * @param mac Sender's local MAC address.
                 * @return The constructed value.
                 */
                static Rfc7273RefClk localMac(const MacAddress &mac);

                /**
                 * @brief Builds a bare @c local form (§4.3).
                 *
                 * Receivers correlate by SDP origin only.  Equivalent
                 * to a @c LocalMac with a null MAC.
                 *
                 * @return The constructed value.
                 */
                static Rfc7273RefClk local();

                /**
                 * @brief Parses an RFC 7273 @c ts-refclk attribute value.
                 *
                 * The leading @c "ts-refclk:" is **not** expected —
                 * callers must strip it before passing the value.
                 *
                 * @param value The attribute value (e.g. @c "ptp=IEEE1588-2008:...:127").
                 * @return The parsed value, or an error if the value
                 *         does not match any recognised RFC 7273 form.
                 */
                static Result<Rfc7273RefClk> fromSdpValue(const String &value);

                /** @brief Returns the wire-form kind. */
                Kind kind() const { return _kind; }

                /** @brief Returns true when @ref kind() is not @c Kind::None. */
                bool isValid() const { return _kind != Kind::None; }

                /** @brief Returns true when this is a @c ptp=...:traceable form. */
                bool isTraceable() const { return _traceable; }

                /** @brief Returns the PTP profile string (empty if not a PTP form). */
                const String &profile() const { return _profile; }

                /** @brief Returns the grandmaster identity (null if absent). */
                const EUI64 &grandmasterId() const { return _gmid; }

                /** @brief Returns the PTP domain number (0 by default; ignored for non-PTP forms). */
                uint8_t domain() const { return _domain; }

                /** @brief Returns the sender's local MAC (null for the bare @c local form). */
                const MacAddress &localMacAddress() const { return _mac; }

                /**
                 * @brief Serializes to the SDP attribute value form.
                 *
                 * Returns the empty string for @c Kind::None; the
                 * caller should skip emitting the attribute entirely
                 * in that case.
                 *
                 * @return The on-wire value (without @c "ts-refclk:" prefix).
                 */
                String toSdpValue() const;

                /** @brief Equality compares every field. */
                bool operator==(const Rfc7273RefClk &other) const;

                /** @brief Inequality. */
                bool operator!=(const Rfc7273RefClk &other) const { return !(*this == other); }

        private:
                Kind       _kind = Kind::None;
                bool       _traceable = false;
                uint8_t    _domain = 0;
                String     _profile;
                EUI64      _gmid;
                MacAddress _mac;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
