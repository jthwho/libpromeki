/**
 * @file      pcapflowrouter.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/pcapflowrouter.h>
#if PROMEKI_ENABLE_NETWORK
#include <utility>
#include <promeki/pcapreader.h>
#include <promeki/rtppayloadanc.h>
#include <promeki/sdpsession.h>

PROMEKI_NAMESPACE_BEGIN

Error PcapFlowRouter::setSdp(const SdpSession &sdp) {
        return _map.ingest(sdp);
}

void PcapFlowRouter::reset() {
        _stats.clear();
        _ancFlows.clear();
        _demux.reset();
}

Error PcapFlowRouter::processFile(const String &path) {
        PcapReader reader;
        const Error err = reader.openFile(path);
        if(err.isError()) return err;
        return runReader(reader);
}

Error PcapFlowRouter::processBuffer(const Buffer &buf) {
        PcapReader reader;
        const Error err = reader.openBuffer(buf);
        if(err.isError()) return err;
        return runReader(reader);
}

Error PcapFlowRouter::runReader(PcapReader &reader) {
        for(;;) {
                auto [rec, err] = reader.next();
                if(err == Error::EndOfFile) break;
                if(err.isError()) break; // truncated / corrupt tail — stop gracefully
                const DemuxResult dr = _demux.demux(rec.linkType, rec.frame);
                if(dr.status == DemuxStatus::Ok) handleDatagram(dr.datagram, rec.captureTime);
        }
        // Flush any frame whose marker packet never arrived (e.g. a capture
        // that stops mid-frame).
        for(AncReasm &r : _ancFlows) flushAnc(r);
        return Error::Ok;
}

void PcapFlowRouter::handleDatagram(const UdpDatagram &dg, const DateTime &captureTime) {
        const BufferView &pl = dg.payload;
        if(pl.count() != 1 || pl.size() < RtpPacket::HeaderSize) return;
        const RtpPacket pkt(pl[0].buffer(), pl[0].offset(), pl.size());
        // Only treat version-2 datagrams as RTP.
        if((pkt.data()[0] >> 6) != 2) return;

        const uint8_t pt = pkt.payloadType();
        const uint32_t ssrc = pkt.ssrc();
        const PcapFlow *flow = _map.find(dg.dst);
        const PcapFlowKind kind = flow != nullptr ? flow->kind : PcapFlowKind::Unknown;

        FlowStat &st = statFor(dg.dst, ssrc, pt, kind);
        st.packets++;
        st.bytes += pl.size();

        if(flow != nullptr && flow->kind == PcapFlowKind::Anc) {
                if(flow->hasPayloadType && pt != flow->payloadType) return; // not this flow's PT
                routeAnc(dg, *flow, pkt, captureTime);
        }
        // Video / audio decode is a later phase; the seam is here.
}

void PcapFlowRouter::routeAnc(const UdpDatagram &dg, const PcapFlow &flow, const RtpPacket &pkt,
                              const DateTime &captureTime) {
        AncReasm &r = ancReasmFor(dg, flow);
        const uint32_t ssrc = pkt.ssrc();
        const uint32_t ts = pkt.timestamp();

        if(r.haveSsrc && r.ssrc != ssrc) flushAnc(r);                          // source changed
        if(r.haveTs && !r.packets.isEmpty() && r.timestamp != ts) flushAnc(r); // new frame (timestamp moved)

        r.dst = dg.dst;
        r.src = dg.src;
        r.ssrc = ssrc;
        r.haveSsrc = true;
        r.timestamp = ts;
        r.haveTs = true;
        r.payloadType = flow.hasPayloadType ? flow.payloadType : pkt.payloadType();
        r.desc = flow.anc;
        r.packets.pushToBack(pkt);
        r.captureTime = captureTime;

        if(pkt.marker()) flushAnc(r);
}

void PcapFlowRouter::flushAnc(AncReasm &r) {
        if(r.packets.isEmpty()) {
                r.haveTs = false;
                return;
        }
        RtpPayloadAnc payload(r.payloadType);
        AncPacket::List out;
        // Best-effort: a checksum / truncation error still leaves whatever
        // well-framed records were extracted in `out`.
        payload.unpackAncPackets(r.packets, out);

        RoutedAncFrame f;
        f.src = r.src;
        f.dst = r.dst;
        f.ssrc = r.ssrc;
        f.captureTime = r.captureTime;
        f.anc.desc = r.desc;
        f.anc.packets = out;
        f.anc.rtpTimestamp = r.timestamp;
        f.anc.packetCount = static_cast<int32_t>(r.packets.size());
        f.anc.keepAlive = out.isEmpty();
        if(_ancCb) _ancCb(f);

        r.packets.clear();
        r.haveTs = false;
}

PcapFlowRouter::FlowStat &PcapFlowRouter::statFor(const SocketAddress &dst, uint32_t ssrc, uint8_t pt,
                                                  PcapFlowKind kind) {
        for(FlowStat &s : _stats) {
                if(s.ssrc == ssrc && s.payloadType == pt && s.dst.port() == dst.port() &&
                   s.dst.address() == dst.address()) {
                        return s;
                }
        }
        FlowStat ns;
        ns.dst = dst;
        ns.ssrc = ssrc;
        ns.payloadType = pt;
        ns.kind = kind;
        _stats.pushToBack(ns);
        return _stats.back();
}

PcapFlowRouter::AncReasm &PcapFlowRouter::ancReasmFor(const UdpDatagram &dg, const PcapFlow &flow) {
        for(AncReasm &r : _ancFlows) {
                if(r.dst.port() == dg.dst.port() && r.dst.address() == dg.dst.address()) return r;
        }
        AncReasm nr;
        nr.dst = dg.dst;
        nr.payloadType = flow.hasPayloadType ? flow.payloadType : 0;
        nr.desc = flow.anc;
        _ancFlows.pushToBack(nr);
        return _ancFlows.back();
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
