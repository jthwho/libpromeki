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
        _health.clear();
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

        // RFC 3550 health tracking for every RTP flow, labelled or not — runs
        // before the ANC payload-type gate so the flow table sees loss /
        // duplicate / reorder / jitter / SSRC-change signals on all flows.
        trackRtpHealth(dg.dst, kind, pkt, st, captureTime);

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

PcapFlowRouter::FlowHealth &PcapFlowRouter::healthFor(const SocketAddress &dst) {
        for(UniquePtr<FlowHealth> &h : _health) {
                if(h->dst.port() == dst.port() && h->dst.address() == dst.address()) return *h;
        }
        UniquePtr<FlowHealth> nh = UniquePtr<FlowHealth>::create();
        nh->dst = dst;
        _health.pushToBack(std::move(nh));
        return *_health.back();
}

void PcapFlowRouter::emitAnomaly(RtpAnomaly::Kind kind, PcapFlowKind flowKind, const SocketAddress &dst,
                                 uint32_t ssrc, uint32_t previous, uint32_t count, const DateTime &captureTime,
                                 const Duration &jitter) {
        if(!_anomalyCb) return;
        RtpAnomaly a;
        a.kind        = kind;
        a.flowKind    = flowKind;
        a.dst         = dst;
        a.ssrc        = ssrc;
        a.previous    = previous;
        a.count       = count;
        a.jitter      = jitter;
        a.captureTime = captureTime;
        _anomalyCb(a);
}

void PcapFlowRouter::trackRtpHealth(const SocketAddress &dst, PcapFlowKind kind, const RtpPacket &pkt,
                                    FlowStat &st, const DateTime &captureTime) {
        FlowHealth    &h    = healthFor(dst);
        const uint32_t ssrc = pkt.ssrc();
        const uint8_t  pt   = pkt.payloadType();
        const uint16_t seq  = pkt.sequenceNumber();
        const uint32_t ts   = pkt.timestamp();

        // SSRC change on this destination — RFC 3550 §A.1 treats a new
        // source as a fresh stream, so reset the tracker and per-source
        // priors.  The seq discontinuity across the boundary is the SSRC
        // change, not loss, so it must not feed the gap detector below.
        if(h.haveSsrc && h.ssrc != ssrc) {
                emitAnomaly(RtpAnomaly::Kind::SsrcChange, kind, dst, ssrc, h.ssrc, 0, captureTime);
                h.tracker.reset();
                h.havePt              = false;
                h.haveTs              = false;
                h.haveSeq             = false;
                h.haveJitterPrev      = false;
                h.jitterNs            = 0;
                h.maxJitter           = Duration::zero();
                h.jitterOver          = false;
                h.timestampRegressions = 0;
        }
        h.ssrc     = ssrc;
        h.haveSsrc = true;

        // Payload-type change on this SSRC.
        if(h.havePt && h.payloadType != pt) {
                emitAnomaly(RtpAnomaly::Kind::PayloadTypeChange, kind, dst, ssrc, h.payloadType, 0, captureTime);
        }
        h.payloadType = pt;
        h.havePt      = true;

        // RTP timestamp regression: an unsigned wrap-forward delta in the
        // top half of the 32-bit space means the clock actually moved back.
        if(h.haveTs) {
                const uint32_t fwd = ts - h.lastTimestamp;
                if(fwd != 0u && fwd > 0x80000000u) {
                        emitAnomaly(RtpAnomaly::Kind::TimestampRegression, kind, dst, ssrc, h.lastTimestamp, 0,
                                    captureTime);
                        h.timestampRegressions++;
                }
        }
        h.lastTimestamp = ts;
        h.haveTs        = true;

        // Sequence accounting via the RFC 3550 tracker (loss / duplicate /
        // reorder).  Jitter is computed separately below — the tracker's
        // §A.8 EWMA runs in 32-bit RTP-tick modular arithmetic tuned for a
        // live steady arrival clock, which misbehaves when fed offline
        // capture wall-clock anchors.
        const RtpSeqTracker::ObserveResult r =
                h.tracker.observe(seq, ts, TimeStamp(captureTime.nanoseconds()));
        if(r.duplicate) {
                emitAnomaly(RtpAnomaly::Kind::Duplicate, kind, dst, ssrc, 0, 0, captureTime);
        } else if(h.haveSeq) {
                const uint32_t expected = h.lastExtendedSeq + 1u;
                if(r.extendedSeq > expected) {
                        // A forward gap below the §A.1 dropout threshold is
                        // real loss; a larger jump is a sender restart the
                        // tracker absorbs via bad_seq, so don't report it as a
                        // huge loss burst.
                        const uint32_t gap = r.extendedSeq - expected;
                        if(gap < RtpSeqTracker::MaxDropout) {
                                emitAnomaly(RtpAnomaly::Kind::PacketLoss, kind, dst, ssrc, 0, gap, captureTime);
                        }
                } else if(r.extendedSeq < expected) {
                        emitAnomaly(RtpAnomaly::Kind::Reorder, kind, dst, ssrc, 0, 0, captureTime);
                }
        }
        if(!r.duplicate && (!h.haveSeq || r.extendedSeq > h.lastExtendedSeq)) {
                h.lastExtendedSeq = r.extendedSeq;
                h.haveSeq         = true;
        }

        // RFC 3550 §A.8 interarrival jitter, computed directly in
        // nanoseconds from the exact capture + RTP timestamps:
        //   D = (arrival_j - arrival_i) - (rtp_j - rtp_i)
        //   J += (|D| - J) / 16
        // Done in signed 64-bit ns (no 32-bit tick wrap), with the RTP
        // delta scaled to ns via the media clock.  Only the 90 kHz flows
        // (ANC / video) have a known clock here; audio rtpmap rates aren't
        // parsed, so their jitter is left unmeasured rather than wrong.
        const uint32_t clockHz =
                (kind == PcapFlowKind::Anc || kind == PcapFlowKind::Video) ? RtpPayloadAnc::ClockRate : 0u;
        if(clockHz != 0u && !r.duplicate) {
                const int64_t arrivalNs = captureTime.nanoseconds();
                if(h.haveJitterPrev) {
                        const int64_t arrivalDeltaNs = arrivalNs - h.prevArrivalNs;
                        const int64_t rtpDeltaTicks  = static_cast<int32_t>(ts - h.prevRtpTs); // signed, wrap-aware
                        const int64_t rtpDeltaNs     = rtpDeltaTicks * 1'000'000'000LL / static_cast<int64_t>(clockHz);
                        int64_t       d              = arrivalDeltaNs - rtpDeltaNs;
                        if(d < 0) d = -d;
                        h.jitterNs += (d - h.jitterNs) / 16;
                        const Duration jitter = Duration::fromNanoseconds(h.jitterNs);
                        if(jitter.nanoseconds() > h.maxJitter.nanoseconds()) h.maxJitter = jitter;
                        if(_jitterWarn.isValid() && _jitterWarn.nanoseconds() > 0) {
                                if(!h.jitterOver && jitter.nanoseconds() > _jitterWarn.nanoseconds()) {
                                        h.jitterOver = true;
                                        emitAnomaly(RtpAnomaly::Kind::JitterExceeded, kind, dst, ssrc, 0, 0,
                                                    captureTime, jitter);
                                } else if(h.jitterOver && jitter.nanoseconds() <= _jitterWarn.nanoseconds()) {
                                        h.jitterOver = false;
                                }
                        }
                }
                h.prevArrivalNs   = arrivalNs;
                h.prevRtpTs       = ts;
                h.haveJitterPrev  = true;
        }

        // Roll the cumulative tracker stats into the public flow row.
        const RtpSeqTracker::Stats s = h.tracker.snapshot();
        st.lostPackets         = s.cumulativeLost > 0 ? static_cast<uint64_t>(s.cumulativeLost) : uint64_t{0};
        st.duplicatePackets    = s.duplicatePackets;
        st.reorderedPackets    = s.reorderedPackets;
        st.maxJitter           = h.maxJitter;
        st.timestampRegressions = h.timestampRegressions;
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
