/**
 * @file      anccodec_vpid.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * SMPTE ST 352 Video Payload Identifier (VPID) parser and builder for
 * @c AncTransport::St291.  VPID is a fixed 4-byte ANC payload at
 * DID @c 0x41 / SDID @c 0x01 (Type-2 packet, DC=4) carrying the
 * raster, scan, transport, and colourimetry of the underlying SDI
 * link.  The full byte layout lives on the @ref SdiVpid value type
 * (see @c sdivpid.h); this codec is a thin wrapper that bridges the
 * value type into the @ref AncTranslator dispatch framework.
 *
 * Round-trip contract: a @c Variant<SdiVpid> goes out as one
 * @c AncPacket and parses back into an identical @c SdiVpid.  The
 * codec delegates wire-level work to
 * @ref SdiVpid::toSt291Packet and @ref SdiVpid::fromSt291Packet so
 * the byte order, checksum, and DID/SDID enforcement are shared
 * with direct callers of those helpers.
 *
 * @par Line / field threading
 *
 * The build path honours @c AncTranslateConfig::St291BuildLine and
 * @c AncTranslateConfig::St291FieldB.  ST 352:2013 §6.2 gives a
 * recommended VANC line per raster / scan; callers wanting that
 * placement should query @ref SdiVpid::recommendedAncLine for the
 * surrounding @ref VideoFormat and stamp the result into the cfg
 * before calling @ref AncTranslator::build.  When no line is
 * supplied the codec falls back to @c St291Packet::UnspecifiedLine
 * (0x7FE), matching every other built-in codec.
 *
 * @par Frame-sync policy
 *
 * VPID is a steady-state description of the SDI link; the bytes are
 * the same on every frame until the link reconfigures.  No
 * sequence counter or per-frame state lives inside the packet.
 * Repeating a VPID under @c FrameSyncDisposition::Repeat is the
 * correct way to hold a stalled output frame (downstream consumers
 * keep the same payload-id assumption); dropping it under
 * @c FrameSyncDisposition::Drop is also fine (the next surviving
 * VPID re-establishes the assumption).  Same shape as the AFD
 * sync policy.
 */

#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslator.h>
#include <promeki/datatype.h>
#include <promeki/enums.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/sdivpid.h>
#include <promeki/st291packet.h>
#include <promeki/variant.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        AncTranslator::ParseResult parseVpidSt291(const AncPacket &pkt, const AncTranslateConfig & /*cfg*/) {
                Result<St291Packet> rp = St291Packet::from(pkt);
                if (rp.second().isError()) return makeError<Variant>(rp.second());

                Result<SdiVpid> rv = SdiVpid::fromSt291Packet(rp.first());
                if (rv.second().isError()) return makeError<Variant>(rv.second());

                return makeResult<Variant>(Variant(rv.first()));
        }

        AncTranslator::PacketsResult buildVpidSt291(const Variant &v, const AncTranslateConfig &cfg) {
                if (v.type() != DataTypeSdiVpid) {
                        return makeError<AncPacket::List>(Error::InvalidArgument);
                }
                SdiVpid vpid = v.get<SdiVpid>();

                uint16_t line = cfg.getAs<uint16_t>(AncTranslateConfig::St291BuildLine,
                                                    St291Packet::UnspecifiedLine);
                bool     fieldB = cfg.getAs<bool>(AncTranslateConfig::St291FieldB, false);

                St291Packet     p = vpid.toSt291Packet(line, fieldB);
                AncPacket::List out;
                out.pushToBack(p.packet());
                return makeResult<AncPacket::List>(std::move(out));
        }

        AncTranslator::PacketsResult syncPolicyVpid(const AncPacket &pkt, FrameSyncDisposition d,
                                                uint8_t /*repeatIndex*/, const AncTranslateConfig & /*cfg*/) {
                AncPacket::List out;
                if (d.kind() != FrameSyncDisposition::Drop) {
                        out.pushToBack(pkt);
                }
                return makeResult<AncPacket::List>(std::move(out));
        }

} // namespace

PROMEKI_NAMESPACE_END

PROMEKI_REGISTER_ANC_PARSER(Vpid_St291, Vpid, ::promeki::AncTransport::St291, ::promeki::parseVpidSt291)
PROMEKI_REGISTER_ANC_BUILDER(Vpid_St291, Vpid, ::promeki::AncTransport::St291, ::promeki::buildVpidSt291)
PROMEKI_REGISTER_ANC_SYNC_POLICY(Vpid, Vpid, ::promeki::syncPolicyVpid)
