/**
 * @file      packetdemux.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/packetdemux.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstring>
#include <utility>

PROMEKI_NAMESPACE_BEGIN

namespace {

// IP / UDP wire fields are big-endian regardless of the capture file's
// byte order, so these read network order unconditionally.
uint16_t rdbe16(const uint8_t *p) {
        return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

uint32_t rdbe32(const uint8_t *p) {
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

// IPv6 extension headers the demux walks through to reach the upper layer.
constexpr uint8_t IpProtoHopByHop = 0;
constexpr uint8_t IpProtoRouting = 43;
constexpr uint8_t IpProtoFragment = 44;
constexpr uint8_t IpProtoDestOpts = 60;

} // namespace

DemuxResult PacketDemux::demux(PcapLinkType linkType, const BufferView &frame) {
        DemuxResult res;
        // PcapReader hands single-slice frames; a scatter-gather view is
        // not something this demux is asked to handle.
        if(frame.count() != 1) {
                res.status = DemuxStatus::Unsupported;
                return res;
        }
        const uint8_t *p = frame.data();
        const size_t n = frame.size();
        if(p == nullptr || n == 0) {
                res.status = DemuxStatus::Malformed;
                return res;
        }
        size_t l3off = 0;
        switch(resolveL3(linkType, p, n, l3off)) {
                case L3::Ipv4: return demuxIpv4(frame, p, n, l3off);
                case L3::Ipv6: return demuxIpv6(frame, p, n, l3off);
                case L3::NotIp: res.status = DemuxStatus::NotUdp; return res;
                case L3::TooShort: res.status = DemuxStatus::Truncated; return res;
                case L3::Unsupported:
                default: res.status = DemuxStatus::Unsupported; return res;
        }
}

void PacketDemux::reset() {
        _reasm.clear();
        _tick = 0;
}

PacketDemux::L3 PacketDemux::resolveL3(PcapLinkType linkType, const uint8_t *p, size_t n, size_t &l3off) const {
        const int lt = linkType.value();

        if(lt == PcapLinkType::Ethernet.value()) {
                if(n < 14) return L3::TooShort;
                size_t pos = 12;
                uint16_t et = rdbe16(p + pos);
                pos += 2;
                // Strip any stack of 802.1Q / 802.1ad VLAN tags.
                int guard = 0;
                while((et == EtherTypeVlan || et == EtherTypeVlanS) && guard++ < 8) {
                        if(pos + 4 > n) return L3::TooShort;
                        pos += 2; // skip the 2-byte TCI
                        et = rdbe16(p + pos);
                        pos += 2;
                }
                l3off = pos;
                if(et == EtherTypeIpv4) return L3::Ipv4;
                if(et == EtherTypeIpv6) return L3::Ipv6;
                return L3::NotIp;
        }
        if(lt == PcapLinkType::LinuxSll.value()) {
                if(n < 16) return L3::TooShort;
                const uint16_t et = rdbe16(p + 14);
                l3off = 16;
                if(et == EtherTypeIpv4) return L3::Ipv4;
                if(et == EtherTypeIpv6) return L3::Ipv6;
                return L3::NotIp;
        }
        if(lt == PcapLinkType::LinuxSll2.value()) {
                if(n < 20) return L3::TooShort;
                const uint16_t et = rdbe16(p + 0);
                l3off = 20;
                if(et == EtherTypeIpv4) return L3::Ipv4;
                if(et == EtherTypeIpv6) return L3::Ipv6;
                return L3::NotIp;
        }
        if(lt == PcapLinkType::Raw.value()) {
                if(n < 1) return L3::TooShort;
                l3off = 0;
                const uint8_t ver = p[0] >> 4;
                if(ver == 4) return L3::Ipv4;
                if(ver == 6) return L3::Ipv6;
                return L3::NotIp;
        }
        if(lt == PcapLinkType::Ipv4.value()) {
                l3off = 0;
                return L3::Ipv4;
        }
        if(lt == PcapLinkType::Ipv6.value()) {
                l3off = 0;
                return L3::Ipv6;
        }
        if(lt == PcapLinkType::Null.value() || lt == PcapLinkType::Loop.value()) {
                // 4-byte address-family word precedes the IP packet.  Rather
                // than decode the (host-order, OS-specific) family value, read
                // the IP version nibble of the packet that follows — it is
                // unambiguous.
                if(n < 5) return L3::TooShort;
                l3off = 4;
                const uint8_t ver = p[4] >> 4;
                if(ver == 4) return L3::Ipv4;
                if(ver == 6) return L3::Ipv6;
                return L3::NotIp;
        }
        return L3::Unsupported;
}

DemuxResult PacketDemux::demuxIpv4(const BufferView &frame, const uint8_t *p, size_t n, size_t l3off) {
        DemuxResult res;
        if(l3off + 20 > n) {
                res.status = DemuxStatus::Truncated;
                return res;
        }
        const uint8_t *ip = p + l3off;
        const size_t ihl = static_cast<size_t>(ip[0] & 0x0f) * 4;
        if(ihl < 20) {
                res.status = DemuxStatus::Malformed;
                return res;
        }
        if(l3off + ihl > n) {
                res.status = DemuxStatus::Truncated;
                return res;
        }
        const uint16_t totalLen = rdbe16(ip + 2);
        if(totalLen != 0 && totalLen < ihl) {
                res.status = DemuxStatus::Malformed;
                return res;
        }
        const uint16_t id = rdbe16(ip + 4);
        const uint16_t fragField = rdbe16(ip + 6);
        const bool moreFragments = (fragField & 0x2000) != 0;
        const size_t fragOffset = static_cast<size_t>(fragField & 0x1fff) * 8;
        const uint8_t protocol = ip[9];
        const SocketAddress src(Ipv4Address(ip[12], ip[13], ip[14], ip[15]), 0);
        const SocketAddress dst(Ipv4Address(ip[16], ip[17], ip[18], ip[19]), 0);

        const size_t l4abs = l3off + ihl;
        const size_t ipPayloadLen = (totalLen > ihl) ? (totalLen - ihl) : 0;
        const size_t avail = (n > l4abs) ? (n - l4abs) : 0;
        const bool snapClipped = ipPayloadLen > avail;
        const size_t l4len = snapClipped ? avail : ipPayloadLen;
        const uint8_t *l4 = p + l4abs;

        if(moreFragments || fragOffset > 0) {
                if(snapClipped) {
                        res.status = DemuxStatus::Truncated; // can't reassemble a clipped fragment
                        return res;
                }
                return reassemble(frame, false, src, dst, id, protocol, l4, l4len, fragOffset, moreFragments);
        }
        if(protocol != ProtocolUdp) {
                res.status = DemuxStatus::NotUdp;
                res.datagram.src = src;
                res.datagram.dst = dst;
                res.datagram.ipProtocol = protocol;
                return res;
        }
        if(snapClipped) {
                res.status = DemuxStatus::Truncated;
                return res;
        }
        return finishUdp(frame, l4, l4len, src, dst, Buffer(), l4abs, false);
}

DemuxResult PacketDemux::demuxIpv6(const BufferView &frame, const uint8_t *p, size_t n, size_t l3off) {
        DemuxResult res;
        if(l3off + 40 > n) {
                res.status = DemuxStatus::Truncated;
                return res;
        }
        const uint8_t *ip = p + l3off;
        const uint16_t payloadLen = rdbe16(ip + 4);
        uint8_t nextHdr = ip[6];
        const SocketAddress src(Ipv6Address(ip + 8), 0);
        const SocketAddress dst(Ipv6Address(ip + 24), 0);

        size_t off = l3off + 40;
        bool isFragment = false;
        size_t fragOffset = 0;
        bool moreFragments = false;
        uint32_t fragId = 0;

        int guard = 0;
        while(guard++ < 16) {
                if(nextHdr == IpProtoFragment) {
                        if(off + 8 > n) {
                                res.status = DemuxStatus::Truncated;
                                return res;
                        }
                        const uint8_t fnext = p[off];
                        const uint16_t fo = rdbe16(p + off + 2);
                        fragOffset = static_cast<size_t>(fo & 0xfff8);
                        moreFragments = (fo & 0x0001) != 0;
                        fragId = rdbe32(p + off + 4);
                        isFragment = true;
                        nextHdr = fnext;
                        off += 8;
                        break; // the fragmentable part begins here
                }
                if(nextHdr == IpProtoHopByHop || nextHdr == IpProtoRouting || nextHdr == IpProtoDestOpts) {
                        if(off + 2 > n) {
                                res.status = DemuxStatus::Truncated;
                                return res;
                        }
                        const size_t extLen = (static_cast<size_t>(p[off + 1]) + 1) * 8;
                        nextHdr = p[off];
                        off += extLen;
                        if(off > n) {
                                res.status = DemuxStatus::Truncated;
                                return res;
                        }
                        continue;
                }
                break; // reached the upper-layer header
        }

        const uint8_t protocol = nextHdr;
        const size_t extConsumed = off - (l3off + 40);
        const size_t l4declared = (payloadLen > extConsumed) ? (payloadLen - extConsumed) : 0;
        const size_t avail = (n > off) ? (n - off) : 0;
        const bool snapClipped = l4declared > avail;
        const size_t l4len = snapClipped ? avail : l4declared;
        const uint8_t *l4 = p + off;

        if(isFragment) {
                if(snapClipped) {
                        res.status = DemuxStatus::Truncated;
                        return res;
                }
                return reassemble(frame, true, src, dst, fragId, protocol, l4, l4len, fragOffset, moreFragments);
        }
        if(protocol != ProtocolUdp) {
                res.status = DemuxStatus::NotUdp;
                res.datagram.src = src;
                res.datagram.dst = dst;
                res.datagram.ipProtocol = protocol;
                return res;
        }
        if(snapClipped) {
                res.status = DemuxStatus::Truncated;
                return res;
        }
        return finishUdp(frame, l4, l4len, src, dst, Buffer(), off, false);
}

DemuxResult PacketDemux::finishUdp(const BufferView &frame, const uint8_t *l4, size_t l4len,
                                   const SocketAddress &srcNoPort, const SocketAddress &dstNoPort,
                                   const Buffer &ownerBuf, size_t l4abs, bool reassembled) {
        DemuxResult res;
        if(l4len < 8) {
                res.status = DemuxStatus::Truncated; // no room for the UDP header
                return res;
        }
        const uint16_t sport = rdbe16(l4 + 0);
        const uint16_t dport = rdbe16(l4 + 2);
        const uint16_t udpLen = rdbe16(l4 + 4);
        const size_t declaredPayload = (udpLen >= 8) ? (udpLen - 8) : (l4len - 8);
        if(declaredPayload > l4len - 8) {
                res.status = DemuxStatus::Truncated; // UDP length runs past captured bytes
                return res;
        }

        UdpDatagram dg;
        dg.src = SocketAddress(srcNoPort.address(), sport);
        dg.dst = SocketAddress(dstNoPort.address(), dport);
        dg.ipProtocol = ProtocolUdp;
        dg.reassembled = reassembled;
        if(ownerBuf.isValid()) {
                const size_t off =
                        static_cast<size_t>(l4 - static_cast<const uint8_t *>(ownerBuf.data())) + 8;
                dg.payload = BufferView(ownerBuf, off, declaredPayload);
        } else {
                const Buffer &backing = frame[0].buffer();
                const size_t base = frame[0].offset();
                dg.payload = BufferView(backing, base + l4abs + 8, declaredPayload);
        }
        res.status = DemuxStatus::Ok;
        res.datagram = dg;
        return res;
}

DemuxResult PacketDemux::reassemble(const BufferView &frame, bool ipv6, const SocketAddress &srcNoPort,
                                    const SocketAddress &dstNoPort, uint32_t id, uint8_t upperProtocol,
                                    const uint8_t *fragData, size_t fragLen, size_t fragOffset, bool moreFragments) {
        DemuxResult res;
        if(fragOffset + fragLen > MaxDatagramSize) {
                res.status = DemuxStatus::Malformed;
                return res;
        }

        Reasm *r = nullptr;
        for(size_t i = 0; i < _reasm.size(); ++i) {
                Reasm &e = _reasm[i];
                if(e.ipv6 == ipv6 && e.id == id && e.protocol == upperProtocol &&
                   e.src.address() == srcNoPort.address() && e.dst.address() == dstNoPort.address()) {
                        r = &e;
                        break;
                }
        }
        if(r == nullptr) {
                if(_reasm.size() >= MaxInFlight) {
                        // Evict the least-recently-touched partial datagram.
                        size_t oldest = 0;
                        uint64_t best = UINT64_MAX;
                        for(size_t i = 0; i < _reasm.size(); ++i) {
                                if(_reasm[i].tick < best) {
                                        best = _reasm[i].tick;
                                        oldest = i;
                                }
                        }
                        _reasm.remove(oldest);
                }
                Reasm ne;
                ne.ipv6 = ipv6;
                ne.id = id;
                ne.protocol = upperProtocol;
                ne.src = srcNoPort;
                ne.dst = dstNoPort;
                ne.data = Buffer(MaxDatagramSize);
                ne.filled.resize(MaxDatagramSize, 0);
                _reasm.pushToBack(std::move(ne));
                r = &_reasm.back();
        }
        r->tick = ++_tick;

        if(fragLen > 0) {
                std::memcpy(static_cast<uint8_t *>(r->data.data()) + fragOffset, fragData, fragLen);
                for(size_t i = 0; i < fragLen; ++i) r->filled[fragOffset + i] = 1;
        }
        if(!moreFragments) {
                r->haveTotal = true;
                r->totalLength = fragOffset + fragLen;
        }

        if(!r->haveTotal) {
                res.status = DemuxStatus::Fragment;
                return res;
        }
        for(size_t i = 0; i < r->totalLength; ++i) {
                if(r->filled[i] == 0) {
                        res.status = DemuxStatus::Fragment; // still missing pieces
                        return res;
                }
        }

        // Complete: take what we need, then drop the cache entry.
        Buffer owner = r->data;
        const SocketAddress s = r->src;
        const SocketAddress d = r->dst;
        const size_t total = r->totalLength;
        for(size_t i = 0; i < _reasm.size(); ++i) {
                if(&_reasm[i] == r) {
                        _reasm.remove(i);
                        break;
                }
        }
        if(upperProtocol != ProtocolUdp) {
                res.status = DemuxStatus::NotUdp;
                res.datagram.src = s;
                res.datagram.dst = d;
                res.datagram.ipProtocol = upperProtocol;
                return res;
        }
        owner.setSize(total);
        return finishUdp(frame, static_cast<const uint8_t *>(owner.data()), total, s, d, owner, 0, true);
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
