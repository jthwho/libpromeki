/**
 * @file      cea708encoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <chrono>
#include <cstdint>
#include <promeki/buffer.h>
#include <promeki/cea708cdp.h>
#include <promeki/cea708encoder.h>
#include <promeki/cea708service.h>
#include <promeki/cea708windowstate.h>
#include <promeki/error.h>
#include <promeki/framenumber.h>
#include <promeki/framerate.h>
#include <promeki/logger.h>
#include <promeki/map.h>
#include <promeki/sharedptr.h>
#include <promeki/subtitle.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        int64_t timeStampToMs(const TimeStamp &ts) {
                return std::chrono::duration_cast<std::chrono::milliseconds>(ts.value().time_since_epoch()).count();
        }

        int64_t timeStampToFrame(const TimeStamp &ts, const FrameRate &fps) {
                if (!fps.isValid()) return 0;
                const int64_t ms = timeStampToMs(ts);
                const int64_t num = static_cast<int64_t>(fps.numerator());
                const int64_t den = static_cast<int64_t>(fps.denominator());
                const int64_t denom = 1000 * den;
                const int64_t numer = ms * num;
                if (numer >= 0) return (numer + denom / 2) / denom;
                return -((-numer + denom / 2) / denom);
        }

        /// @brief Builds the byte stream that defines window 0,
        ///        writes the cue's @p text, and displays the window.
        ///
        ///   Byte layout:
        ///     DF0 (0x98) + 6 args (priority/locks/visible, anchor,
        ///                          anchor_h, anchor_point/rows,
        ///                          col_count, style)
        ///     character bytes (G0 / G1)
        ///     DSW (0x89) + bitmap byte (0x01 for window 0)
        ///
        /// Out-of-range characters substitute with space (0x20).
        Buffer buildShowCueBytes(const Subtitle &cue, int windowCols) {
                if (windowCols < 1) windowCols = 1;
                if (windowCols > Cea708Window::MaxCols) windowCols = Cea708Window::MaxCols;
                // Make the window wide enough to fit the cue text up
                // to @p windowCols; fewer cols if the text is shorter.
                const String &text = cue.text();
                int cols = windowCols;
                if (static_cast<int>(text.byteCount()) < cols) {
                        cols = static_cast<int>(text.byteCount());
                        if (cols < 1) cols = 1;
                }
                const uint8_t colWire = static_cast<uint8_t>((cols - 1) & 0x3F);
                // DF0: visible (bit 6), locks (bits 4+5), priority 0.
                const uint8_t df0Arg1 = 0x40 | 0x30;
                const uint8_t df0Anchor = 0x00; // relativePos=0, anchor_v=0
                const uint8_t df0AnchorH = 0x00;
                const uint8_t df0Rows = 0x10; // anchor_point=1, row_count_wire=0 (1 row)
                const uint8_t df0Style = 0x00;

                std::vector<uint8_t> bytes;
                bytes.reserve(static_cast<size_t>(7 + text.byteCount() + 2));
                bytes.push_back(0x98); // DF0
                bytes.push_back(df0Arg1);
                bytes.push_back(df0Anchor);
                bytes.push_back(df0AnchorH);
                bytes.push_back(df0Rows);
                bytes.push_back(colWire);
                bytes.push_back(df0Style);
                // Character bytes: pass-through for G0 (0x20..0x7E),
                // substitute with space for anything else.  G1 chars
                // could pass-through too but the encoder is plain-text
                // for now.
                const char *cp = text.cstr();
                size_t      n = text.byteCount();
                for (size_t i = 0; i < n; ++i) {
                        const uint8_t b = static_cast<uint8_t>(cp[i]);
                        if (b >= 0x20 && b <= 0x7E) {
                                bytes.push_back(b);
                        } else {
                                bytes.push_back(0x20);
                        }
                }
                bytes.push_back(0x89); // DSW
                bytes.push_back(0x01); // window 0 bitmap

                Buffer buf(bytes.size());
                buf.setSize(bytes.size());
                if (!bytes.empty()) buf.copyFrom(bytes.data(), bytes.size(), 0);
                return buf;
        }

        /// @brief Builds the byte stream that hides window 0
        ///        (HDW + bitmap).
        Buffer buildHideWindowBytes() {
                uint8_t bytes[2] = {0x8A, 0x01};
                Buffer  buf(2);
                buf.setSize(2);
                buf.copyFrom(bytes, 2, 0);
                return buf;
        }

        /// @brief Wraps @p serviceBytes into a single-service
        ///        DTVCC packet and returns its cc_data triple list.
        Cea708Cdp::CcDataList wrapInDtvccPacket(uint8_t serviceNumber, Buffer serviceBytes,
                                                uint8_t sequenceNumber) {
                Cea708DtvccPacket pkt;
                pkt.setSequenceNumber(sequenceNumber);
                pkt.serviceBlocks().pushToBack(Cea708Service(serviceNumber, std::move(serviceBytes)));
                return pkt.toCcData();
        }

} // namespace

// ============================================================================
// Pimpl
// ============================================================================

struct Cea708EncoderImpl {
                PROMEKI_SHARED_FINAL(Cea708EncoderImpl)

                Cea708Encoder::Config cfg;
                /// @brief Per-frame schedule of cc_data triples.
                ///        Frames not present in the map emit no
                ///        DTVCC payload for the configured service.
                Map<int64_t, Cea708Cdp::CcDataList> schedule;
};

// ============================================================================
// Cea708Encoder
// ============================================================================

Cea708Encoder::Cea708Encoder() : _d(SharedPtr<Cea708EncoderImpl>::create()) {}

Cea708Encoder::Cea708Encoder(Config cfg) : _d(SharedPtr<Cea708EncoderImpl>::create()) {
        _d.modify()->cfg = std::move(cfg);
}

Cea708Encoder::~Cea708Encoder() = default;

const Cea708Encoder::Config &Cea708Encoder::config() const { return _d->cfg; }

void Cea708Encoder::reset() { _d.modify()->schedule.clear(); }

Error Cea708Encoder::setSubtitles(const SubtitleList &subs) {
        auto *d = _d.modify();
        d->schedule.clear();
        if (!d->cfg.frameRate.isValid()) {
                promekiWarn("Cea708Encoder::setSubtitles: invalid frame rate");
                return Error::Invalid;
        }
        if (d->cfg.serviceNumber < 1 || d->cfg.serviceNumber > Cea708Service::MaxServiceNumber) {
                promekiWarn("Cea708Encoder::setSubtitles: serviceNumber %u out of range",
                            static_cast<unsigned>(d->cfg.serviceNumber));
                return Error::Invalid;
        }

        uint8_t seq = 0;
        for (size_t i = 0; i < subs.size(); ++i) {
                const Subtitle &cue = subs[i];
                const int64_t   startFrame = timeStampToFrame(cue.start(), d->cfg.frameRate);
                const int64_t   endFrame = timeStampToFrame(cue.end(), d->cfg.frameRate);
                if (endFrame <= startFrame) continue;
                Buffer showBytes = buildShowCueBytes(cue, d->cfg.windowCols);
                // The combined wire packet (header + service header +
                // serviceBytes) must fit in 128 bytes per spec.
                if (showBytes.size() + 2 > Cea708DtvccPacket::MaxPayloadBytes) {
                        promekiWarn(
                                "Cea708Encoder::setSubtitles: cue %zu text too long (%zu service "
                                "bytes; max %zu)",
                                i, showBytes.size(), static_cast<size_t>(Cea708DtvccPacket::MaxPayloadBytes) - 2);
                        return Error::OutOfRange;
                }
                Cea708Cdp::CcDataList showTriples = wrapInDtvccPacket(d->cfg.serviceNumber, showBytes, seq);
                d->schedule.insert(startFrame, showTriples);
                seq = static_cast<uint8_t>((seq + 1) & 0x03);
                Buffer                hideBytes = buildHideWindowBytes();
                Cea708Cdp::CcDataList hideTriples = wrapInDtvccPacket(d->cfg.serviceNumber, hideBytes, seq);
                d->schedule.insert(endFrame, hideTriples);
                seq = static_cast<uint8_t>((seq + 1) & 0x03);
        }
        return Error::Ok;
}

Cea708Cdp::CcDataList Cea708Encoder::nextFrame(FrameNumber frame) const {
        Cea708Cdp::CcDataList out;
        if (!frame.isValid()) return out;
        auto it = _d->schedule.find(frame.value());
        if (it == _d->schedule.end()) return out;
        return it->second;
}

PROMEKI_NAMESPACE_END
