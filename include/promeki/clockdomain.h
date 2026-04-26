/**
 * @file      clockdomain.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/stringregistry.h>
#include <promeki/list.h>
#include <promeki/enums.h>

PROMEKI_NAMESPACE_BEGIN

class Metadata;

/**
 * @brief Named identity for a clock source.
 * @ingroup time
 *
 * ClockDomain identifies the timing authority that produced a set of
 * timestamps.  Two timestamps sharing the same ClockDomain are
 * directly comparable; timestamps from different domains may drift
 * relative to each other.
 *
 * ClockDomain follows the TypeRegistry pattern: each instance is a
 * lightweight pointer to an immutable Data record held in a static
 * registry.  Identity is managed by a StringRegistry so domain names
 * are automatically deduplicated.  Well-known domains are registered
 * at library startup; backend-specific domains are registered at
 * runtime via registerDomain().
 *
 * The @ref epoch() property (@ref ClockEpoch) describes whether
 * timestamps from independent streams in this domain share a common
 * time origin:
 * - @c ClockEpoch::PerStream  — each stream starts from its own
 *   origin (e.g. Synthetic, ALSA hardware).
 * - @c ClockEpoch::Correlated — cross-stream subtraction is valid
 *   within a machine (e.g. CLOCK_MONOTONIC).
 * - @c ClockEpoch::Absolute   — cross-machine subtraction is valid
 *   (e.g. PTP, GPS).
 *
 * Each domain carries a @ref Metadata container for domain-specific
 * details (PTP grandmaster ID, domain number, GPS lock status, etc.).
 * Include @c promeki/metadata.h to use the returned reference.
 *
 * @par Thread Safety
 * Distinct ClockDomain instances may be used concurrently — each is
 * just a small handle.  The static registry (@c registerDomain,
 * @c lookup, well-known IDs) is internally synchronized and safe
 * to call from any thread.
 *
 * @par Example
 * @code
 * // Well-known domains
 * ClockDomain syn(ClockDomain::Synthetic);
 * assert(syn.isValid());
 * assert(syn.name() == "Synthetic");
 * assert(syn.epoch() == ClockEpoch::PerStream);
 *
 * // Lookup by name (must be previously registered)
 * ClockDomain found = ClockDomain::lookup("SystemMonotonic");
 * assert(found == ClockDomain(ClockDomain::SystemMonotonic));
 *
 * // Dynamic registration
 * ClockDomain::ID ptpId = ClockDomain::registerDomain(
 *         "ptp.domain.0", "PTP domain 0", ClockEpoch::Absolute);
 * ClockDomain ptp(ptpId);
 * assert(ptp.isCrossMachineComparable());
 * @endcode
 */
class ClockDomain {
        public:
                /** @brief Registered string identity type for clock domains. */
                using ID = StringRegistry<"ClockDomain">::Item;

                /** @brief List of clock domain IDs. */
                using IDList = List<ID>;

                /**
                 * @brief Opaque data record for a registered clock domain.
                 *
                 * Defined in clockdomain.cpp.  Access fields through
                 * ClockDomain accessors or via data() when the full
                 * definition is available.
                 */
                struct Data;

                /** @brief ID for the Synthetic clock domain (frame-rate derived, per-stream). */
                static const ID Synthetic;

                /** @brief ID for the SystemMonotonic clock domain (steady_clock, correlated). */
                static const ID SystemMonotonic;

                /**
                 * @brief Registers a new clock domain.
                 *
                 * Creates a Data record in the registry with the given
                 * properties and an empty Metadata container.  If a domain
                 * with the same name is already registered, the existing
                 * ID is returned and the new description/epoch are ignored.
                 *
                 * @param name        Short identifier (must be unique).
                 * @param description Human-readable description.
                 * @param epoch       Epoch behaviour (default: Correlated).
                 * @return The ID for the registered domain.
                 */
                static ID registerDomain(const String &name,
                                         const String &description,
                                         const ClockEpoch &epoch = ClockEpoch::Correlated);

                /**
                 * @brief Replaces the metadata for a registered domain.
                 *
                 * Include @c promeki/metadata.h before calling.
                 *
                 * @param id       The domain to update.
                 * @param metadata The new metadata.
                 */
                static void setDomainMetadata(const ID &id,
                                              const Metadata &metadata);

                /**
                 * @brief Returns the IDs of all registered clock domains.
                 * @return A list of valid IDs.
                 */
                static IDList registeredIDs();

                /**
                 * @brief Looks up a clock domain by name.
                 * @param name The registered name to find.
                 * @return The ClockDomain if found, or an invalid ClockDomain.
                 */
                static ClockDomain lookup(const String &name);

                /** @brief Constructs an invalid clock domain. */
                ClockDomain() = default;

                /**
                 * @brief Constructs a ClockDomain from a registered ID.
                 * @param id The ID to look up.
                 */
                ClockDomain(const ID &id);

                /**
                 * @brief Returns true if this domain refers to a valid registry entry.
                 * @return True if valid.
                 */
                bool isValid() const;

                /**
                 * @brief Returns the registered ID.
                 * @return The ID.
                 */
                ID id() const;

                /**
                 * @brief Returns the domain name.
                 * @return The registered name, or an empty string if invalid.
                 */
                const String &name() const;

                /**
                 * @brief Returns the human-readable description.
                 * @return The description string.
                 */
                const String &description() const;

                /**
                 * @brief Returns the epoch behaviour for this domain.
                 * @return ClockEpoch::PerStream, ClockEpoch::Correlated,
                 *         or ClockEpoch::Absolute.
                 */
                const ClockEpoch &epoch() const;

                /**
                 * @brief Returns the domain's metadata container.
                 *
                 * Include @c promeki/metadata.h to use the returned reference.
                 *
                 * @return A const reference to the Metadata.
                 */
                const Metadata &metadata() const;

                /**
                 * @brief Returns true if timestamps from different streams
                 *        in this domain can be compared on the same machine.
                 *
                 * True for Correlated and Absolute epochs.
                 */
                bool isCrossStreamComparable() const;

                /**
                 * @brief Returns true if timestamps from this domain can
                 *        be compared across different machines.
                 *
                 * True only for Absolute epochs (e.g. PTP, GPS).
                 */
                bool isCrossMachineComparable() const;

                /**
                 * @brief Returns the domain name.
                 *
                 * Serialization is by name only.  Use lookup() to
                 * deserialize — the domain must already be registered.
                 *
                 * @return The registered name, or an empty string if invalid.
                 */
                String toString() const;

                /**
                 * @brief Returns the underlying Data record.
                 *
                 * The Data struct is defined in clockdomain.cpp.  Include
                 * that translation unit's header to inspect fields directly.
                 *
                 * @return Pointer to the Data, or nullptr if invalid.
                 */
                const Data *data() const { return d; }

                /** @brief Equality comparison by registry identity. */
                bool operator==(const ClockDomain &other) const { return d == other.d; }

                /** @brief Inequality comparison by registry identity. */
                bool operator!=(const ClockDomain &other) const { return d != other.d; }

        private:
                const Data *d = nullptr;
                static const Data *lookupData(const ID &id);
};

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::ClockDomain);
