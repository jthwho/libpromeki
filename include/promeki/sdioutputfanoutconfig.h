/**
 * @file      sdioutputfanoutconfig.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE

#include <promeki/datatype.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/namespace.h>
#include <promeki/result.h>
#include <promeki/sdisignalconfig.h>
#include <promeki/sharedptr.h>
#include <promeki/string.h>
#include <promeki/videoportref.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;

/**
 * @brief Pairs an SDI link standard with @em multiple physical
 *        destination port groups that all carry the same outbound
 *        signal.
 * @ingroup util
 *
 * Generalises @ref SdiSignalConfig from "one signal on one set of
 * ports" to "one signal fanned out across N sets of ports."  Each
 * group of @ref sdiCableCount consecutive
 * @ref VideoPortRef entries describes one full output destination
 * for the underlying SMPTE link standard — single-link standards
 * place one port per group, dual-link two, quad-link four.
 *
 * @par Use case
 *
 * Hardware SDI sinks (notably the AJA NTV2 boards) can drive
 * multiple output spigots from a single framestore via the
 * crosspoint fabric — one output crosspoint can fan out to any
 * number of input crosspoints.  @c SdiOutputFanoutConfig is the
 * carrier-level type that names "the signal goes out @em these
 * ports, and is mirrored to @em those ports."  The first group is
 * the primary destination; subsequent groups are mirrors.
 *
 * @par String form
 *
 * @ref toString emits
 * <code>standard:p1+p2+...,q1+q2+...,r1+r2+...</code> where each
 * comma-separated record is one destination group and each
 * @c +-separated entry inside a record is one
 * @ref VideoPortRef::toString form.  The standard prefix
 * matches @ref SdiSignalConfig::toString (lowercased registered
 * enum name).  Two examples:
 *
 *  - <code>"sl_hd:sdi1,sdi2,sdi3"</code>: a single-link HD signal
 *    primary on SDI 1, mirrored to SDI 2 and SDI 3.
 *  - <code>"dl_3g:sdi1+sdi2,sdi3+sdi4"</code>: a dual-link 3 Gb/s
 *    signal whose two component cables ride SDI 1+2 (primary) and
 *    SDI 3+4 (mirror).
 *
 * @par Validity
 *
 * @ref isValid returns @c true when every group has exactly
 * @ref sdiCableCount ports.  An empty group list is invalid
 * (use @c SdiSignalConfig + the standalone @c SdiOutputSignal
 * config key when no fanout is needed).  @c Auto standard accepts
 * any group size — useful for "let the open path infer" cases.
 *
 * @par Storage and copy semantics
 *
 * Internally-CoW value-type handle — copying bumps an internal
 * refcount; the mutators (@ref setStandard, @ref setGroups,
 * @ref appendGroup) detach via copy-on-write.  No @c ::Ptr alias;
 * pass by value.
 */
class SdiOutputFanoutConfig {
        public:
                PROMEKI_DATATYPE(SdiOutputFanoutConfig, DataTypeSdiOutputFanoutConfig, 1)

                /** @brief Plain-value list of @c VideoPortRef. */
                using PortList = ::promeki::List<VideoPortRef>;
                /** @brief Plain-value list of port groups. */
                using GroupList = ::promeki::List<PortList>;

                /**
                 * @brief Default-constructs an empty configuration
                 *        (Auto standard, no groups, @ref isValid
                 *        returns @c false).
                 */
                SdiOutputFanoutConfig();

                /**
                 * @brief Constructs a configuration with the given
                 *        standard and destination groups.
                 */
                SdiOutputFanoutConfig(SdiLinkStandard standard, GroupList groups);

                /** @brief Returns the SDI link standard. */
                SdiLinkStandard standard() const;

                /**
                 * @brief Returns the destination groups.
                 *
                 * Each entry is a port list whose size matches
                 * @ref sdiCableCount on the configured standard (verified
                 * by @ref isValid).
                 */
                const GroupList &groups() const;

                /**
                 * @brief Returns the number of destination groups.
                 *
                 * @c 1 means "single destination, no fanout"; @c >1
                 * means the primary plus that many mirrors.
                 */
                int groupCount() const;

                /** @brief Replaces the SDI link standard (CoW). */
                void setStandard(SdiLinkStandard standard);

                /** @brief Replaces the entire group list (CoW). */
                void setGroups(GroupList groups);

                /** @brief Appends one destination group (CoW). */
                void appendGroup(PortList group);

                /**
                 * @brief Returns the @em primary destination group as
                 *        a standalone @ref SdiSignalConfig.
                 *
                 * Convenience for legacy code paths that only know
                 * about @c SdiSignalConfig — the primary group is the
                 * first one in @ref groups, paired with the shared
                 * standard.  Returns a default @c SdiSignalConfig
                 * when @ref groups is empty.
                 */
                SdiSignalConfig primary() const;

                /**
                 * @brief Builds a single-group fanout that mirrors the
                 *        given @ref SdiSignalConfig.
                 *
                 * Inverse of @ref primary — produces a fanout with one
                 * group whose ports are @c sig.ports() and whose
                 * standard is @c sig.standard().  Useful for backends
                 * that uniformly consume the fanout shape but also
                 * accept the simpler @c SdiSignalConfig key
                 * (@ref MediaConfig::SdiOutputSignal): wrap the signal
                 * once and process every output the same way.
                 *
                 * Passing a default-constructed (empty) signal returns
                 * a default-constructed fanout — i.e. the round-trip
                 * @c fromSignal(SdiSignalConfig()).primary() yields
                 * the original default signal.
                 *
                 * @param sig  Source signal to wrap.
                 */
                static SdiOutputFanoutConfig fromSignal(const SdiSignalConfig &sig);

                /**
                 * @brief Returns the destination groups as a
                 *        @c List of @ref SdiSignalConfig instances.
                 *
                 * Each entry shares the same @ref standard; useful
                 * when applying group-by-group routing where each
                 * destination is handled independently.
                 */
                ::promeki::List<SdiSignalConfig> asSignalConfigs() const;

                /**
                 * @brief Returns @c true when every group has
                 *        @ref sdiCableCount ports.
                 *
                 * @c Auto standard accepts any group size.  An empty
                 * group list returns @c false — there's nothing to
                 * route.
                 */
                bool isValid() const;

                /**
                 * @brief Emits the string form
                 *        @c standard:g1ports,g2ports,...
                 *
                 * See the class doc for examples.
                 */
                String toString() const;

                /**
                 * @brief Parses the string form produced by @ref toString.
                 *
                 * The standard prefix is matched case-insensitively
                 * against the registered @ref SdiLinkStandard enum
                 * names.  Each comma-separated record is one group;
                 * each @c +-separated entry inside a record is a
                 * @ref VideoPortRef::fromString port spec.  Returns
                 * @c Error::InvalidArgument when the standard is
                 * unrecognised or any port fails to parse.
                 */
                static Result<SdiOutputFanoutConfig> fromString(const String &s);

                /** @brief Field-wise equality. */
                bool operator==(const SdiOutputFanoutConfig &other) const;

                /** @brief Inequality. */
                bool operator!=(const SdiOutputFanoutConfig &other) const { return !(*this == other); }

                /**
                 * @brief DataStream body writer for the
                 *        @ref PROMEKI_DATATYPE member-API path.
                 */
                Error writeToStream(DataStream &s) const;

                /**
                 * @brief DataStream body reader for the
                 *        @ref PROMEKI_DATATYPE member-API path.
                 */
                template <uint32_t V> static Result<SdiOutputFanoutConfig> readFromStream(DataStream &s);

                /**
                 * @brief Private @c Impl struct holding the
                 *        configuration's state.
                 */
                struct Impl {
                                PROMEKI_SHARED_FINAL(Impl)

                                SdiLinkStandard standard = SdiLinkStandard::Auto;
                                GroupList       groups;
                };

        private:
                SharedPtr<Impl> _d;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
