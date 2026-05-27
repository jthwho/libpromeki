/**
 * @file      ancst2020audio.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/ancst2020audio.h>
#include <promeki/buffer.h>
#include <promeki/datastream.h>
#include <promeki/json.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Diagnostics
// ============================================================================

String AncSt2020Audio::toString() const {
        String s = "AncSt2020Audio(sdid=0x";
        s += String::number(static_cast<int>(_channelPair), 16);
        s += ", duplicate=";
        s += _duplicate ? "1" : "0";
        s += ", mdf=";
        s += String::number(static_cast<int>(_metadataFrame.size()));
        s += "B)";
        return s;
}

JsonObject AncSt2020Audio::toJson() const {
        JsonObject obj;
        obj.set("channelPair", static_cast<int64_t>(_channelPair));
        obj.set("duplicate", _duplicate);
        obj.set("metadataFrameSize", static_cast<int64_t>(_metadataFrame.size()));
        return obj;
}

// ============================================================================
// DataStream wire format (v1).
//
// Bundled into a single Buffer so the per-element DataStream tag
// header overhead does not multiply with the metadata-frame payload.
// Layout:
//   byte 0    : channel-pair / SDID
//   byte 1    : duplicate flag (0 or 1)
//   bytes 2.. : metadata-frame bytes (rest of the bundled buffer)
// ============================================================================

Error AncSt2020Audio::writeToStream(DataStream &s) const {
        const size_t mdfSize = _metadataFrame.size();
        const size_t total = 2 + mdfSize;
        Buffer       buf(total);
        buf.setSize(total);
        uint8_t *    p = static_cast<uint8_t *>(buf.data());
        p[0] = _channelPair;
        p[1] = _duplicate ? uint8_t(1) : uint8_t(0);
        if (mdfSize > 0) {
                const uint8_t *src = static_cast<const uint8_t *>(_metadataFrame.data());
                for (size_t i = 0; i < mdfSize; ++i) p[2 + i] = src[i];
        }
        s << buf;
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<AncSt2020Audio> AncSt2020Audio::readFromStream<1>(DataStream &s) {
        Buffer buf;
        s >> buf;
        if (s.status() != DataStream::Ok) return makeError<AncSt2020Audio>(s.toError());
        if (buf.size() < 2) return makeError<AncSt2020Audio>(Error::CorruptData);

        const uint8_t *p = static_cast<const uint8_t *>(buf.data());
        AncSt2020Audio out;
        out.setChannelPair(p[0]);
        out.setDuplicate(p[1] != 0);
        const size_t mdfSize = buf.size() - 2;
        if (mdfSize > 0) {
                Buffer mdf(mdfSize);
                mdf.setSize(mdfSize);
                uint8_t *dst = static_cast<uint8_t *>(mdf.data());
                for (size_t i = 0; i < mdfSize; ++i) dst[i] = p[2 + i];
                out.setMetadataFrame(std::move(mdf));
        }
        return makeResult<AncSt2020Audio>(std::move(out));
}

PROMEKI_NAMESPACE_END
