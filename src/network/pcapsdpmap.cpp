/**
 * @file      pcapsdpmap.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/pcapsdpmap.h>
#if PROMEKI_ENABLE_NETWORK
#include <promeki/ipv4address.h>
#include <promeki/ipv6address.h>
#include <promeki/sdpsession.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

// Parse an SDP connection address (with any "/ttl" or "/count" suffix
// stripped) into a numeric NetworkAddress.  Returns false when the
// address is empty or not a numeric IPv4 / IPv6 literal.
bool parseConnectionAddress(const String &raw, NetworkAddress &out) {
        String addr = raw;
        const size_t slash = addr.find('/');
        if(slash != String::npos) addr = addr.left(slash);
        if(addr.isEmpty()) return false;
        auto [v4, e4] = Ipv4Address::fromString(addr);
        if(e4.isOk()) {
                out = NetworkAddress(v4);
                return true;
        }
        auto [v6, e6] = Ipv6Address::fromString(addr);
        if(e6.isOk()) {
                out = NetworkAddress(v6);
                return true;
        }
        return false;
}

} // namespace

PcapFlowKind PcapSdpMap::classify(const SdpMediaDescription &md) {
        const SdpMediaDescription::RtpMap rtpmap = md.rtpMap();
        if(rtpmap.valid && rtpmap.encoding.toLower() == String("smpte291")) {
                return PcapFlowKind::Anc;
        }
        const String mt = md.mediaType().toLower();
        if(mt == String("audio")) return PcapFlowKind::Audio;
        if(mt == String("video")) return PcapFlowKind::Video;
        if(mt == String("application")) return PcapFlowKind::Data;
        return PcapFlowKind::Unknown;
}

Error PcapSdpMap::ingest(const SdpSession &sdp) {
        _flows.clear();
        const String sessionAddr = sdp.connectionAddress();
        for(const SdpMediaDescription &md : sdp.mediaDescriptions()) {
                const String addrStr = md.connectionAddress().isEmpty() ? sessionAddr : md.connectionAddress();
                NetworkAddress address;
                if(!parseConnectionAddress(addrStr, address)) {
                        // No matchable destination — skip this media line.
                        continue;
                }
                PcapFlow flow;
                flow.address = address;
                flow.port = md.port();
                flow.kind = classify(md);
                flow.label = md.mediaType();
                const SdpMediaDescription::RtpMap rtpmap = md.rtpMap();
                if(rtpmap.valid) {
                        flow.payloadType = rtpmap.payloadType;
                        flow.hasPayloadType = true;
                        flow.encoding = rtpmap.encoding;
                        flow.clockRate = rtpmap.clockRate;
                } else if(!md.payloadTypes().isEmpty()) {
                        flow.payloadType = md.payloadTypes()[0];
                        flow.hasPayloadType = true;
                }
                if(flow.kind == PcapFlowKind::Anc) {
                        flow.anc = AncDesc::fromSdp(md);
                }
                _flows.pushToBack(flow);
        }
        return Error::Ok;
}

void PcapSdpMap::addAncFlow(const NetworkAddress &address, uint16_t port, int payloadType) {
        PcapFlow flow;
        flow.address = address;
        flow.port = port;
        flow.kind = PcapFlowKind::Anc;
        flow.encoding = String("smpte291");
        flow.label = String("video");
        if(payloadType >= 0) {
                flow.payloadType = static_cast<uint8_t>(payloadType);
                flow.hasPayloadType = true;
        }
        _flows.pushToBack(flow);
}

const PcapFlow *PcapSdpMap::find(const NetworkAddress &address, uint16_t port) const {
        for(const PcapFlow &f : _flows) {
                if(f.port == port && f.address == address) return &f;
        }
        return nullptr;
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
