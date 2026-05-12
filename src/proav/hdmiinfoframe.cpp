/**
 * @file      hdmiinfoframe.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/hdmiinfoframe.h>
#include <promeki/ancmeta.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // CEA-861-G §6.1: the checksum makes the mod-256 sum of every
        // byte in the InfoFrame (header + body) equal zero.  Caller
        // passes the byte stream containing the four-byte header with
        // checksum byte cleared followed by the body.
        uint8_t computeInfoFrameChecksum(const uint8_t *bytes, size_t size) {
                uint8_t sum = 0;
                for (size_t i = 0; i < size; ++i) sum = static_cast<uint8_t>(sum + bytes[i]);
                return static_cast<uint8_t>(0 - sum);
        }

        const uint8_t *hostBytes(const Buffer &buf) { return static_cast<const uint8_t *>(buf.data()); }

        // Assembles the four-byte header + body into a fresh Buffer
        // with the checksum byte computed and stamped.
        Buffer buildWireBytes(uint8_t type, uint8_t version, const Buffer &body) {
                const size_t bodyBytes = body.size();
                const size_t total = HdmiInfoFrame::HeaderSize + bodyBytes;
                Buffer       buf(total);
                if (buf.data() == nullptr) return Buffer();

                List<uint8_t> tmp(total, uint8_t(0));
                tmp.data()[0] = type;
                tmp.data()[1] = version;
                tmp.data()[2] = static_cast<uint8_t>(bodyBytes);
                tmp.data()[3] = 0; // placeholder for checksum
                if (bodyBytes > 0) {
                        const uint8_t *bodyPtr = hostBytes(body);
                        if (bodyPtr == nullptr) return Buffer();
                        for (size_t i = 0; i < bodyBytes; ++i) {
                                tmp.data()[HdmiInfoFrame::HeaderSize + i] = bodyPtr[i];
                        }
                }
                tmp.data()[3] = computeInfoFrameChecksum(tmp.data(), total);

                Error err = buf.copyFrom(tmp.data(), total, 0);
                if (err.isError()) return Buffer();
                buf.setSize(total);
                return buf;
        }

} // namespace

// ---------------------------------------------------------------------------
// Static factories
// ---------------------------------------------------------------------------

Result<HdmiInfoFrame> HdmiInfoFrame::from(const AncPacket &pkt) {
        if (pkt.transport() != AncTransport::HdmiInfoFrame) {
                return makeError<HdmiInfoFrame>(Error::InvalidArgument);
        }
        if (pkt.data().size() < HeaderSize) {
                return makeError<HdmiInfoFrame>(Error::InvalidArgument);
        }
        return makeResult(HdmiInfoFrame(pkt));
}

HdmiInfoFrame HdmiInfoFrame::build(const AncFormat &fmt, uint8_t version, Buffer body) {
        return buildRaw(fmt.hdmiInfoFrameType(), version, std::move(body));
}

HdmiInfoFrame HdmiInfoFrame::buildRaw(uint8_t type, uint8_t version, Buffer body) {
        Buffer    wire = buildWireBytes(type, version, body);
        AncFormat fmt = AncFormat::fromHdmiInfoFrameType(type);

        Metadata meta;
        meta.set(AncMeta::HdmiInfoFrame::Type, type);
        meta.set(AncMeta::HdmiInfoFrame::Version, version);
        meta.set(AncMeta::HdmiInfoFrame::Length, static_cast<uint8_t>(body.size()));

        AncPacket pkt(fmt, AncTransport::HdmiInfoFrame, std::move(wire), std::move(meta));
        return HdmiInfoFrame(pkt);
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

uint8_t HdmiInfoFrame::type() const {
        const uint8_t *bytes = hostBytes(_pkt.data());
        return (bytes != nullptr && _pkt.data().size() >= 1) ? bytes[0] : 0;
}

uint8_t HdmiInfoFrame::version() const {
        const uint8_t *bytes = hostBytes(_pkt.data());
        return (bytes != nullptr && _pkt.data().size() >= 2) ? bytes[1] : 0;
}

uint8_t HdmiInfoFrame::length() const {
        const uint8_t *bytes = hostBytes(_pkt.data());
        return (bytes != nullptr && _pkt.data().size() >= 3) ? bytes[2] : 0;
}

uint8_t HdmiInfoFrame::checksum() const {
        const uint8_t *bytes = hostBytes(_pkt.data());
        return (bytes != nullptr && _pkt.data().size() >= 4) ? bytes[3] : 0;
}

Buffer HdmiInfoFrame::body() const {
        const uint8_t *bytes = hostBytes(_pkt.data());
        if (bytes == nullptr || _pkt.data().size() <= HeaderSize) return Buffer();
        const size_t   bodyLen = _pkt.data().size() - HeaderSize;
        Buffer         out(bodyLen);
        Error          err = out.copyFrom(bytes + HeaderSize, bodyLen, 0);
        if (err.isError()) return Buffer();
        out.setSize(bodyLen);
        return out;
}

bool HdmiInfoFrame::checksumValid() const {
        const uint8_t *bytes = hostBytes(_pkt.data());
        if (bytes == nullptr || _pkt.data().size() < HeaderSize) return false;
        uint8_t sum = 0;
        for (size_t i = 0; i < _pkt.data().size(); ++i) sum = static_cast<uint8_t>(sum + bytes[i]);
        return sum == 0;
}

bool HdmiInfoFrame::isValid() const {
        return _pkt.transport() == AncTransport::HdmiInfoFrame && _pkt.data().size() >= HeaderSize;
}

PROMEKI_NAMESPACE_END
