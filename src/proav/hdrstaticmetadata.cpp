/**
 * @file      hdrstaticmetadata.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <algorithm>
#include <cmath>
#include <cstring>
#include <promeki/datastream.h>
#include <promeki/hdrstaticmetadata.h>
#include <promeki/json.h>
#include <promeki/logger.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // Chromaticity scale per CTA-861.3 Static Metadata Descriptor Type 1.
        constexpr double kChromaticityScale = 50000.0;
        // 1 wire unit on min_display_mastering_luminance is 0.0001 cd/m².
        constexpr double kMinLumScale = 10000.0;

        uint16_t encodeChromaticity(double v) {
                if (!std::isfinite(v) || v < 0.0) return 0;
                double scaled = std::round(v * kChromaticityScale);
                if (scaled > 65535.0) return 65535;
                return static_cast<uint16_t>(scaled);
        }

        double decodeChromaticity(uint16_t raw) { return static_cast<double>(raw) / kChromaticityScale; }

        uint16_t encodeU16Clamped(double v) {
                if (!std::isfinite(v) || v < 0.0) return 0;
                if (v > 65535.0) return 65535;
                return static_cast<uint16_t>(std::round(v));
        }

        void writeU16Le(uint8_t *p, uint16_t v) {
                p[0] = static_cast<uint8_t>(v & 0xFF);
                p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        }

        uint16_t readU16Le(const uint8_t *p) {
                return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8);
        }

} // namespace

// ----------------------------------------------------------------------------
// EOTF mapping
// ----------------------------------------------------------------------------

uint8_t HdrStaticMetadata::wireEotfFor(TransferCharacteristics tc) {
        if (tc == TransferCharacteristics::SMPTE2084) return EotfSmpte2084;
        if (tc == TransferCharacteristics::ARIB_STD_B67) return EotfHlg;
        return EotfSdrGamma;
}

TransferCharacteristics HdrStaticMetadata::transferFromWireEotf(uint8_t wire) {
        switch (wire & 0x07) {
                case EotfSmpte2084:
                        return TransferCharacteristics::SMPTE2084;
                case EotfHlg:
                        return TransferCharacteristics::ARIB_STD_B67;
                case EotfSdrGamma:
                case EotfHdrGamma:
                default:
                        return TransferCharacteristics::Unspecified;
        }
}

// ----------------------------------------------------------------------------
// Wire round-trip (CTA-861-G DRM InfoFrame body, Static Metadata Type 1)
// ----------------------------------------------------------------------------

Buffer HdrStaticMetadata::toBuffer() const {
        Buffer   buf(Type1BodySize);
        buf.setSize(Type1BodySize);
        uint8_t *p = static_cast<uint8_t *>(buf.data());

        p[0] = wireEotfFor(_eotf) & 0x07;
        p[1] = DescriptorIdType1;

        // Mastering display: chromaticities in display_primaries[0..2] +
        // white_point, then min/max luminance.  An invalid MasteringDisplay
        // emits zero bytes — CTA-861.3 "unspecified" semantics.
        writeU16Le(p + 2,  encodeChromaticity(_md.red().x()));
        writeU16Le(p + 4,  encodeChromaticity(_md.red().y()));
        writeU16Le(p + 6,  encodeChromaticity(_md.green().x()));
        writeU16Le(p + 8,  encodeChromaticity(_md.green().y()));
        writeU16Le(p + 10, encodeChromaticity(_md.blue().x()));
        writeU16Le(p + 12, encodeChromaticity(_md.blue().y()));
        writeU16Le(p + 14, encodeChromaticity(_md.whitePoint().x()));
        writeU16Le(p + 16, encodeChromaticity(_md.whitePoint().y()));
        writeU16Le(p + 18, encodeU16Clamped(_md.maxLuminance()));
        writeU16Le(p + 20, encodeU16Clamped(_md.minLuminance() * kMinLumScale));

        // Content light level.  Zero == unspecified per CTA-861.3.
        writeU16Le(p + 22, encodeU16Clamped(static_cast<double>(_cll.maxCLL())));
        writeU16Le(p + 24, encodeU16Clamped(static_cast<double>(_cll.maxFALL())));

        return buf;
}

Result<HdrStaticMetadata> HdrStaticMetadata::fromBuffer(const void *data, size_t size) {
        if (data == nullptr || size < Type1BodySize) {
                return makeError<HdrStaticMetadata>(Error::CorruptData);
        }
        const uint8_t *p = static_cast<const uint8_t *>(data);

        const uint8_t descriptorId = p[1];
        if (descriptorId != DescriptorIdType1) {
                // Only Type 1 is defined as of CTA-861-G; future descriptor
                // IDs would carry a different body layout we can't decode.
                return makeError<HdrStaticMetadata>(Error::CorruptData);
        }

        const TransferCharacteristics eotf = transferFromWireEotf(p[0]);

        const CIEPoint red  (decodeChromaticity(readU16Le(p + 2)),  decodeChromaticity(readU16Le(p + 4)));
        const CIEPoint green(decodeChromaticity(readU16Le(p + 6)),  decodeChromaticity(readU16Le(p + 8)));
        const CIEPoint blue (decodeChromaticity(readU16Le(p + 10)), decodeChromaticity(readU16Le(p + 12)));
        const CIEPoint wp   (decodeChromaticity(readU16Le(p + 14)), decodeChromaticity(readU16Le(p + 16)));
        const double   maxL = static_cast<double>(readU16Le(p + 18));
        const double   minL = static_cast<double>(readU16Le(p + 20)) / kMinLumScale;

        MasteringDisplay md(red, green, blue, wp, minL, maxL);

        const uint16_t maxCll  = readU16Le(p + 22);
        const uint16_t maxFall = readU16Le(p + 24);
        ContentLightLevel cll(maxCll, maxFall);

        return makeResult<HdrStaticMetadata>(HdrStaticMetadata(eotf, std::move(md), std::move(cll)));
}

Result<HdrStaticMetadata> HdrStaticMetadata::fromBuffer(const Buffer &buf) {
        return fromBuffer(buf.data(), buf.size());
}

// ----------------------------------------------------------------------------
// JSON dump
// ----------------------------------------------------------------------------

JsonObject HdrStaticMetadata::toJson() const {
        JsonObject obj;
        obj.set("eotf", _eotf.toString());
        obj.set("wireEotf", static_cast<int64_t>(wireEotfFor(_eotf)));

        JsonObject mdObj;
        JsonObject redObj;
        redObj.set("x", _md.red().x());
        redObj.set("y", _md.red().y());
        mdObj.set("red", redObj);
        JsonObject greenObj;
        greenObj.set("x", _md.green().x());
        greenObj.set("y", _md.green().y());
        mdObj.set("green", greenObj);
        JsonObject blueObj;
        blueObj.set("x", _md.blue().x());
        blueObj.set("y", _md.blue().y());
        mdObj.set("blue", blueObj);
        JsonObject wpObj;
        wpObj.set("x", _md.whitePoint().x());
        wpObj.set("y", _md.whitePoint().y());
        mdObj.set("whitePoint", wpObj);
        mdObj.set("minLuminance", _md.minLuminance());
        mdObj.set("maxLuminance", _md.maxLuminance());
        obj.set("masteringDisplay", mdObj);

        JsonObject cllObj;
        cllObj.set("maxCLL", static_cast<int64_t>(_cll.maxCLL()));
        cllObj.set("maxFALL", static_cast<int64_t>(_cll.maxFALL()));
        obj.set("contentLightLevel", cllObj);

        return obj;
}

// ----------------------------------------------------------------------------
// Comparison + diagnostics
// ----------------------------------------------------------------------------

bool HdrStaticMetadata::operator==(const HdrStaticMetadata &o) const {
        return _eotf == o._eotf && _md == o._md && _cll == o._cll;
}

String HdrStaticMetadata::toString() const {
        return String::sprintf("HdrStaticMetadata{eotf=%s, maxLum=%.1f, minLum=%.4f, "
                               "maxCLL=%u, maxFALL=%u}",
                               _eotf.toString().cstr(), _md.maxLuminance(), _md.minLuminance(),
                               static_cast<unsigned>(_cll.maxCLL()), static_cast<unsigned>(_cll.maxFALL()));
}

// ----------------------------------------------------------------------------
// DataStream operators (also reachable through Variant's payload dispatch)
// ----------------------------------------------------------------------------

void writeHdrStaticMetadataData(DataStream &stream, const HdrStaticMetadata &md) {
        Buffer buf = md.toBuffer();
        stream << buf;
}

HdrStaticMetadata readHdrStaticMetadataData(DataStream &stream) {
        Buffer buf;
        stream >> buf;
        Result<HdrStaticMetadata> r = HdrStaticMetadata::fromBuffer(buf);
        if (r.second().isError()) {
                promekiWarn("HdrStaticMetadata DataStream read failed: %s", r.second().name().cstr());
                return HdrStaticMetadata();
        }
        return r.first();
}

DataStream &operator<<(DataStream &stream, const HdrStaticMetadata &md) {
        stream.beginFrame(DataStream::TypeHdrStaticMetadata, 1);
        writeHdrStaticMetadataData(stream, md);
        stream.endFrame();
        return stream;
}

DataStream &operator>>(DataStream &stream, HdrStaticMetadata &md) {
        if (!stream.readFrame(DataStream::TypeHdrStaticMetadata)) {
                md = HdrStaticMetadata();
                return stream;
        }
        md = readHdrStaticMetadataData(stream);
        return stream;
}

PROMEKI_NAMESPACE_END
