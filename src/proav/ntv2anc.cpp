/**
 * @file      ntv2anc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>
#if PROMEKI_ENABLE_NTV2

#include <promeki/ntv2anc.h>

#include <promeki/ancdesc.h>
#include <promeki/ancformat.h>
#include <promeki/ancmeta.h>
#include <promeki/ancpacket.h>
#include <promeki/ancpayload.h>
#include <promeki/enums.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/metadata.h>
#include <promeki/st291packet.h>

#include <ancillarydata.h>
#include <ancillarylist.h>
#include <ntv2publicinterface.h>

PROMEKI_NAMESPACE_BEGIN

namespace Ntv2Anc {

namespace {

        // Translate one parsed AJA packet to an St291Packet on our side
        // and append the underlying AncPacket to @p out.  fieldB tracks
        // which AJA buffer (F1 vs F2) this packet was decoded from so
        // the F-bit on the produced packet matches the source field.
        void appendAjaPacket(const AJAAncillaryData &src, bool fieldB, AncPacket::List &out) {
                const uint8_t did     = src.GetDID();
                const uint8_t sdid    = src.GetSID();
                const size_t  dc      = src.GetPayloadByteCount();
                const uint8_t *pPayload = src.GetPayloadData();

                List<uint16_t> udw;
                udw.reserve(dc);
                // Pass 8-bit data bytes; St291Packet::buildRaw computes
                // the 10-bit parity for each word.
                for (size_t i = 0; i < dc; ++i) {
                        udw.pushToBack(static_cast<uint16_t>(pPayload[i]));
                }

                const AJAAncDataLoc &loc        = src.GetDataLocation();
                const uint16_t       line       = loc.GetLineNumber();
                const uint16_t       hoff       = loc.GetHorizontalOffset();
                const bool           cBit       = loc.GetDataChannel() == AJAAncDataChannel_C;
                // AJAAncDataStream_1 == 0 in the SDK enum, so the cast
                // gives us a zero-based StreamNum.  Unknown collapses
                // to zero as well — matches our default sidecar value.
                uint8_t streamNum = 0;
                if (IS_VALID_AJAAncDataStream(loc.GetDataStream())) {
                        streamNum = static_cast<uint8_t>(loc.GetDataStream());
                }

                St291Packet p = St291Packet::buildRaw(did, sdid, udw, line, hoff, fieldB, cBit, streamNum);
                if (!p.isValid()) {
                        promekiWarn("Ntv2Anc: skipping malformed AJA packet (DID=0x%02x SDID=0x%02x DC=%zu)",
                                    did, sdid, dc);
                        return;
                }
                out.pushToBack(p.packet());
        }

        // Translate one of our St291 AncPackets back to an AJA packet
        // and add it to @p list.  Returns false on a fundamentally
        // malformed input so the caller can count drops.
        bool addLibPacketToAjaList(const AncPacket &pkt, AJAAncillaryList &list) {
                Result<St291Packet> rs = St291Packet::from(pkt);
                if (rs.second().isError()) {
                        // Non-St291 transport or empty packet — skip
                        // quietly so payloads that mix transports
                        // (HDMI InfoFrames etc.) still process their
                        // St291 packets without polluting the log.
                        return true;
                }
                const St291Packet &sp = rs.first();

                AJAAncillaryData dst;
                if (AJA_FAILURE(dst.SetDID(sp.did())) || AJA_FAILURE(dst.SetSID(sp.sdid()))) {
                        return false;
                }

                // Extract 8-bit data bytes; the parity bits in the upper
                // two bits of each UDW are recomputed by AJA on
                // GenerateTransmitData, so dropping them is intentional.
                List<uint16_t> udw = sp.udw();
                List<uint8_t>  bytes;
                bytes.reserve(udw.size());
                for (size_t i = 0; i < udw.size(); ++i) {
                        bytes.pushToBack(static_cast<uint8_t>(udw[i] & 0xFF));
                }
                if (!bytes.isEmpty()) {
                        if (AJA_FAILURE(
                                dst.SetPayloadData(bytes.data(), static_cast<uint32_t>(bytes.size())))) {
                                return false;
                        }
                }

                AJAAncDataStream  stream  = AJAAncDataStream_1;
                const uint8_t     sn      = sp.streamNum();
                if (sn < AJAAncDataStream_Unknown) {
                        stream = static_cast<AJAAncDataStream>(sn);
                }
                const AJAAncDataChannel ch = sp.cBit() ? AJAAncDataChannel_C : AJAAncDataChannel_Y;
                // Lines below 21 (typical CEA-708 / AFD VANC range) are
                // VANC for both NTSC and HD; the AJA encoder uses the
                // line number to decide F1 vs F2 placement based on the
                // f2StartLine argument we pass to GetTransmitData.
                AJAAncDataLoc loc(AJAAncDataLink_A, ch, AJAAncDataSpace_VANC, sp.line(), sp.hOffset(),
                                  stream);
                if (AJA_FAILURE(dst.SetDataLocation(loc))) return false;

                if (AJA_FAILURE(list.AddAncillaryData(&dst))) return false;
                return true;
        }

} // namespace

AncPayload::Ptr ntv2AncToPackets(const uint8_t *f1Buf, size_t f1Bytes, const uint8_t *f2Buf, size_t f2Bytes,
                                 const Size2Du32 &raster, const VideoScanMode &scanMode,
                                 const FrameRate &frameRate) {
        AncDesc desc(raster, scanMode, frameRate);

        AncPacket::List packets;

        // Parse each field separately so the F1 / F2 origin survives
        // as the FieldB sidecar.  AddFromDeviceAncBuffer (which would
        // be the natural one-shot per field) is protected on
        // AJAAncillaryList, so we lean on SetFromDeviceAncBuffers and
        // hand it an empty buffer for the field we're not interested
        // in on each pass.
        NTV2Buffer empty(static_cast<ULWord *>(nullptr), 0);
        if (f1Bytes > 0 && f1Buf != nullptr) {
                NTV2Buffer       f1(const_cast<uint8_t *>(f1Buf), static_cast<ULWord>(f1Bytes));
                AJAAncillaryList ajaList;
                if (AJA_SUCCESS(AJAAncillaryList::SetFromDeviceAncBuffers(f1, empty, ajaList))) {
                        const uint32_t n = ajaList.CountAncillaryData();
                        for (uint32_t i = 0; i < n; ++i) {
                                AJAAncillaryData *p = ajaList.GetAncillaryDataAtIndex(i);
                                if (p != nullptr) appendAjaPacket(*p, /*fieldB=*/false, packets);
                        }
                }
        }
        if (f2Bytes > 0 && f2Buf != nullptr) {
                NTV2Buffer       f2(const_cast<uint8_t *>(f2Buf), static_cast<ULWord>(f2Bytes));
                AJAAncillaryList ajaList;
                if (AJA_SUCCESS(AJAAncillaryList::SetFromDeviceAncBuffers(empty, f2, ajaList))) {
                        const uint32_t n = ajaList.CountAncillaryData();
                        for (uint32_t i = 0; i < n; ++i) {
                                AJAAncillaryData *p = ajaList.GetAncillaryDataAtIndex(i);
                                if (p != nullptr) appendAjaPacket(*p, /*fieldB=*/true, packets);
                        }
                }
        }

        return AncPayload::Ptr::create(desc, std::move(packets));
}

Error packetsToNtv2Anc(const AncPayload &payload, uint8_t *f1Buf, size_t f1BytesCapacity, uint8_t *f2Buf,
                       size_t f2BytesCapacity, bool isProgressive, uint16_t f2StartLine) {
        if (f1Buf == nullptr || f1BytesCapacity == 0) return Error::InvalidArgument;
        if (!isProgressive && (f2Buf == nullptr || f2BytesCapacity == 0)) return Error::InvalidArgument;

        AJAAncillaryList ajaList;
        const AncPacket::List &pkts = payload.packets();
        for (size_t i = 0; i < pkts.size(); ++i) {
                if (!addLibPacketToAjaList(pkts[i], ajaList)) {
                        // One packet failed to import — log and keep
                        // going so the rest of the frame's ANC still
                        // ships (matches the devplan's "warn, don't
                        // error on out-of-range lines" policy).
                        promekiWarn("Ntv2Anc: dropping malformed lib packet at index %zu", i);
                }
        }

        // AJAAncillaryList::GetTransmitData unconditionally calls
        // F2Buffer.Fill on its second argument, so we hand it a
        // pointed-at-the-f2-buffer NTV2Buffer even on progressive
        // streams (length=0 keeps GenerateTransmitData from writing
        // into it).  Passing a fully-null NTV2Buffer for F2 instead
        // causes AJA's Fill to fail and the whole call to error.
        // AJAAncillaryList::GetTransmitData unconditionally calls
        // F2Buffer.Fill on its second argument, so we hand it a
        // pointed-at-the-f2-buffer NTV2Buffer even on progressive
        // streams (length=0 keeps GenerateTransmitData from writing
        // into it).  Passing a fully-null NTV2Buffer for F2 instead
        // causes AJA's Fill to fail and the whole call to error.
        NTV2Buffer f1(f1Buf, static_cast<ULWord>(f1BytesCapacity));
        NTV2Buffer f2(f2Buf, static_cast<ULWord>(isProgressive ? 0 : f2BytesCapacity));
        AJAStatus  st = ajaList.GetTransmitData(f1, f2, isProgressive, f2StartLine);
        if (AJA_FAILURE(st)) {
                promekiErr("Ntv2Anc: GetTransmitData failed (AJAStatus=%d, %zu pkts)", int(st), pkts.size());
                return Error::Invalid;
        }
        return Error::Ok;
}

} // namespace Ntv2Anc

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NTV2
