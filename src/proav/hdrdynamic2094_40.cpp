/**
 * @file      hdrdynamic2094_40.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <promeki/datastream.h>
#include <promeki/hdrdynamic2094_40.h>
#include <promeki/json.h>
#include <promeki/logger.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // MSB-first bit packer driving a backing List<uint8_t>.
        class BitWriter {
                public:
                        void writeBits(uint32_t value, uint8_t bits) {
                                // bits is at most 27 — every wire field stays within a uint32_t.
                                value &= bits >= 32 ? 0xFFFFFFFFu : ((1u << bits) - 1u);
                                while (bits > 0) {
                                        if (_bitsInCur == 0) {
                                                _bytes.pushToBack(0);
                                                _bitsInCur = 8;
                                        }
                                        uint8_t take = bits < _bitsInCur ? bits : _bitsInCur;
                                        uint8_t shift = static_cast<uint8_t>(bits - take);
                                        uint32_t chunk = (value >> shift) & ((1u << take) - 1u);
                                        uint8_t  destShift = static_cast<uint8_t>(_bitsInCur - take);
                                        _bytes.back() = static_cast<uint8_t>(_bytes.back() | (chunk << destShift));
                                        _bitsInCur = static_cast<uint8_t>(_bitsInCur - take);
                                        bits = static_cast<uint8_t>(bits - take);
                                }
                        }

                        Buffer take() {
                                Buffer buf(_bytes.size());
                                if (!_bytes.isEmpty()) {
                                        buf.copyFrom(_bytes.data(), _bytes.size(), 0);
                                        buf.setSize(_bytes.size());
                                }
                                return buf;
                        }

                private:
                        List<uint8_t> _bytes;
                        uint8_t       _bitsInCur = 0;
        };

        // MSB-first bit reader.  Returns false (and stops advancing) on
        // underflow so the caller can surface Error::CorruptData.
        class BitReader {
                public:
                        BitReader(const uint8_t *data, size_t size) : _data(data), _size(size) {}

                        bool read(uint8_t bits, uint32_t &out) {
                                uint32_t value = 0;
                                while (bits > 0) {
                                        if (_byteIdx >= _size) {
                                                _ok = false;
                                                out = 0;
                                                return false;
                                        }
                                        uint8_t bitInByte = static_cast<uint8_t>(8 - _bitOffset);
                                        uint8_t take = bits < bitInByte ? bits : bitInByte;
                                        uint8_t shift = static_cast<uint8_t>(bitInByte - take);
                                        uint8_t mask = static_cast<uint8_t>(((1u << take) - 1u) << shift);
                                        uint8_t chunk = static_cast<uint8_t>((_data[_byteIdx] & mask) >> shift);
                                        value = (value << take) | chunk;
                                        _bitOffset = static_cast<uint8_t>(_bitOffset + take);
                                        if (_bitOffset >= 8) {
                                                _bitOffset = 0;
                                                _byteIdx++;
                                        }
                                        bits = static_cast<uint8_t>(bits - take);
                                }
                                out = value;
                                return true;
                        }

                        bool ok() const { return _ok; }

                private:
                        const uint8_t *_data;
                        size_t         _size;
                        size_t         _byteIdx = 0;
                        uint8_t        _bitOffset = 0;
                        bool           _ok = true;
        };

} // namespace

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

bool HdrDynamic2094_40::Window::operator==(const Window &o) const {
        return upperLeftCornerX == o.upperLeftCornerX && upperLeftCornerY == o.upperLeftCornerY &&
               lowerRightCornerX == o.lowerRightCornerX && lowerRightCornerY == o.lowerRightCornerY &&
               centerOfEllipseX == o.centerOfEllipseX && centerOfEllipseY == o.centerOfEllipseY &&
               rotationAngle == o.rotationAngle &&
               semimajorAxisInternalEllipse == o.semimajorAxisInternalEllipse &&
               semimajorAxisExternalEllipse == o.semimajorAxisExternalEllipse &&
               semiminorAxisExternalEllipse == o.semiminorAxisExternalEllipse &&
               overlapProcessOption == o.overlapProcessOption;
}

bool HdrDynamic2094_40::ActualPeakLuminance::operator==(const ActualPeakLuminance &o) const {
        if (numRows != o.numRows || numCols != o.numCols) return false;
        const size_t n = static_cast<size_t>(numRows) * static_cast<size_t>(numCols);
        if (values.size() < n || o.values.size() < n) return values.size() == o.values.size();
        for (size_t i = 0; i < n; ++i) {
                if (values[i] != o.values[i]) return false;
        }
        return true;
}

bool HdrDynamic2094_40::ToneMapping::operator==(const ToneMapping &o) const {
        if (kneePointX != o.kneePointX || kneePointY != o.kneePointY) return false;
        if (bezierCurveAnchors.size() != o.bezierCurveAnchors.size()) return false;
        for (size_t i = 0; i < bezierCurveAnchors.size(); ++i) {
                if (bezierCurveAnchors[i] != o.bezierCurveAnchors[i]) return false;
        }
        return true;
}

bool HdrDynamic2094_40::WindowProcessing::operator==(const WindowProcessing &o) const {
        for (int i = 0; i < 3; ++i)
                if (maxScl[i] != o.maxScl[i]) return false;
        if (averageMaxRgb != o.averageMaxRgb) return false;
        if (distribution.size() != o.distribution.size()) return false;
        for (size_t i = 0; i < distribution.size(); ++i) {
                if (distribution[i] != o.distribution[i]) return false;
        }
        if (fractionBrightPixels != o.fractionBrightPixels) return false;
        if (hasToneMapping != o.hasToneMapping) return false;
        if (hasToneMapping && !(toneMapping == o.toneMapping)) return false;
        if (hasColorSaturationMapping != o.hasColorSaturationMapping) return false;
        if (hasColorSaturationMapping && colorSaturationWeight != o.colorSaturationWeight) return false;
        return true;
}

// ---------------------------------------------------------------------------
// Construction / shape
// ---------------------------------------------------------------------------

HdrDynamic2094_40::HdrDynamic2094_40() {
        _windowProcessing.resize(1); // window 0 always present
}

void HdrDynamic2094_40::setNumWindows(uint8_t n) {
        if (n < 1) n = 1;
        if (n > MaxWindows) n = MaxWindows;
        _numWindows = n;
        _windowProcessing.resize(n);
        _extraWindows.resize(n > 0 ? static_cast<size_t>(n - 1) : 0);
}

// ---------------------------------------------------------------------------
// Wire round-trip
// ---------------------------------------------------------------------------

Buffer HdrDynamic2094_40::toBuffer() const {
        BitWriter w;

        const uint8_t numWindows = _numWindows < 1 ? 1 : (_numWindows > MaxWindows ? MaxWindows : _numWindows);

        w.writeBits(_applicationVersion, 8);
        w.writeBits(numWindows, 2);

        // Extra windows 1..numWindows-1.
        for (uint8_t i = 1; i < numWindows; ++i) {
                const Window &win = static_cast<size_t>(i - 1) < _extraWindows.size() ? _extraWindows[i - 1] : Window();
                w.writeBits(win.upperLeftCornerX, 16);
                w.writeBits(win.upperLeftCornerY, 16);
                w.writeBits(win.lowerRightCornerX, 16);
                w.writeBits(win.lowerRightCornerY, 16);
                w.writeBits(win.centerOfEllipseX, 16);
                w.writeBits(win.centerOfEllipseY, 16);
                w.writeBits(win.rotationAngle, 8);
                w.writeBits(win.semimajorAxisInternalEllipse, 16);
                w.writeBits(win.semimajorAxisExternalEllipse, 16);
                w.writeBits(win.semiminorAxisExternalEllipse, 16);
                w.writeBits(win.overlapProcessOption ? 1u : 0u, 1);
        }

        w.writeBits(_targetedSystemDisplayMaximumLuminance & 0x07FFFFFFu, 27);

        const bool targetedPresent = _targetedSystemDisplayActualPeakLuminance.isPresent();
        w.writeBits(targetedPresent ? 1u : 0u, 1);
        if (targetedPresent) {
                const ActualPeakLuminance &grid = _targetedSystemDisplayActualPeakLuminance;
                w.writeBits(grid.numRows, 5);
                w.writeBits(grid.numCols, 5);
                const size_t cells = static_cast<size_t>(grid.numRows) * static_cast<size_t>(grid.numCols);
                for (size_t i = 0; i < cells; ++i) {
                        uint8_t v = i < grid.values.size() ? grid.values[i] : 0;
                        w.writeBits(v, 4);
                }
        }

        for (uint8_t wi = 0; wi < numWindows; ++wi) {
                const WindowProcessing &wp =
                        wi < _windowProcessing.size() ? _windowProcessing[wi] : WindowProcessing();
                for (int c = 0; c < 3; ++c) w.writeBits(wp.maxScl[c], 17);
                w.writeBits(wp.averageMaxRgb, 17);

                size_t numPerc = wp.distribution.size();
                if (numPerc > MaxDistributionPercentiles) numPerc = MaxDistributionPercentiles;
                w.writeBits(static_cast<uint32_t>(numPerc), 4);
                for (size_t i = 0; i < numPerc; ++i) {
                        w.writeBits(wp.distribution[i].percentage, 7);
                        w.writeBits(wp.distribution[i].percentile, 17);
                }
                w.writeBits(wp.fractionBrightPixels, 10);
        }

        const bool masteringPresent = _masteringDisplayActualPeakLuminance.isPresent();
        w.writeBits(masteringPresent ? 1u : 0u, 1);
        if (masteringPresent) {
                const ActualPeakLuminance &grid = _masteringDisplayActualPeakLuminance;
                w.writeBits(grid.numRows, 5);
                w.writeBits(grid.numCols, 5);
                const size_t cells = static_cast<size_t>(grid.numRows) * static_cast<size_t>(grid.numCols);
                for (size_t i = 0; i < cells; ++i) {
                        uint8_t v = i < grid.values.size() ? grid.values[i] : 0;
                        w.writeBits(v, 4);
                }
        }

        for (uint8_t wi = 0; wi < numWindows; ++wi) {
                const WindowProcessing &wp =
                        wi < _windowProcessing.size() ? _windowProcessing[wi] : WindowProcessing();
                w.writeBits(wp.hasToneMapping ? 1u : 0u, 1);
                if (wp.hasToneMapping) {
                        w.writeBits(wp.toneMapping.kneePointX, 12);
                        w.writeBits(wp.toneMapping.kneePointY, 12);
                        size_t numAnchors = wp.toneMapping.bezierCurveAnchors.size();
                        if (numAnchors > MaxBezierCurveAnchors) numAnchors = MaxBezierCurveAnchors;
                        w.writeBits(static_cast<uint32_t>(numAnchors), 4);
                        for (size_t i = 0; i < numAnchors; ++i) {
                                w.writeBits(wp.toneMapping.bezierCurveAnchors[i], 10);
                        }
                }
                w.writeBits(wp.hasColorSaturationMapping ? 1u : 0u, 1);
                if (wp.hasColorSaturationMapping) {
                        w.writeBits(wp.colorSaturationWeight, 6);
                }
        }

        // BitWriter pads the final byte with trailing zero bits automatically.
        return w.take();
}

Result<HdrDynamic2094_40> HdrDynamic2094_40::fromBuffer(const void *data, size_t size) {
        if (data == nullptr || size == 0) {
                return makeError<HdrDynamic2094_40>(Error::CorruptData);
        }
        BitReader         r(static_cast<const uint8_t *>(data), size);
        HdrDynamic2094_40 m;
        uint32_t          v = 0;

        if (!r.read(8, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
        m._applicationVersion = static_cast<uint8_t>(v);

        if (!r.read(2, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
        uint8_t numWindows = static_cast<uint8_t>(v);
        if (numWindows < 1) numWindows = 1;
        if (numWindows > MaxWindows) return makeError<HdrDynamic2094_40>(Error::CorruptData);
        m._numWindows = numWindows;
        m._windowProcessing.resize(numWindows);
        m._extraWindows.resize(numWindows > 0 ? static_cast<size_t>(numWindows - 1) : 0);

        for (uint8_t i = 1; i < numWindows; ++i) {
                Window &win = m._extraWindows[i - 1];
                if (!r.read(16, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                win.upperLeftCornerX = static_cast<uint16_t>(v);
                if (!r.read(16, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                win.upperLeftCornerY = static_cast<uint16_t>(v);
                if (!r.read(16, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                win.lowerRightCornerX = static_cast<uint16_t>(v);
                if (!r.read(16, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                win.lowerRightCornerY = static_cast<uint16_t>(v);
                if (!r.read(16, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                win.centerOfEllipseX = static_cast<uint16_t>(v);
                if (!r.read(16, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                win.centerOfEllipseY = static_cast<uint16_t>(v);
                if (!r.read(8, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                win.rotationAngle = static_cast<uint8_t>(v);
                if (!r.read(16, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                win.semimajorAxisInternalEllipse = static_cast<uint16_t>(v);
                if (!r.read(16, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                win.semimajorAxisExternalEllipse = static_cast<uint16_t>(v);
                if (!r.read(16, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                win.semiminorAxisExternalEllipse = static_cast<uint16_t>(v);
                if (!r.read(1, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                win.overlapProcessOption = (v != 0);
        }

        if (!r.read(27, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
        m._targetedSystemDisplayMaximumLuminance = v;

        if (!r.read(1, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
        if (v) {
                ActualPeakLuminance &grid = m._targetedSystemDisplayActualPeakLuminance;
                if (!r.read(5, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                grid.numRows = static_cast<uint8_t>(v);
                if (!r.read(5, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                grid.numCols = static_cast<uint8_t>(v);
                const size_t cells = static_cast<size_t>(grid.numRows) * static_cast<size_t>(grid.numCols);
                grid.values.resize(cells);
                for (size_t i = 0; i < cells; ++i) {
                        if (!r.read(4, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                        grid.values[i] = static_cast<uint8_t>(v);
                }
        }

        for (uint8_t wi = 0; wi < numWindows; ++wi) {
                WindowProcessing &wp = m._windowProcessing[wi];
                for (int c = 0; c < 3; ++c) {
                        if (!r.read(17, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                        wp.maxScl[c] = v;
                }
                if (!r.read(17, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                wp.averageMaxRgb = v;

                if (!r.read(4, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                uint8_t numPerc = static_cast<uint8_t>(v);
                if (numPerc > MaxDistributionPercentiles) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                wp.distribution.resize(numPerc);
                for (uint8_t i = 0; i < numPerc; ++i) {
                        if (!r.read(7, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                        wp.distribution[i].percentage = static_cast<uint8_t>(v);
                        if (!r.read(17, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                        wp.distribution[i].percentile = v;
                }
                if (!r.read(10, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                wp.fractionBrightPixels = static_cast<uint16_t>(v);
        }

        if (!r.read(1, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
        if (v) {
                ActualPeakLuminance &grid = m._masteringDisplayActualPeakLuminance;
                if (!r.read(5, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                grid.numRows = static_cast<uint8_t>(v);
                if (!r.read(5, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                grid.numCols = static_cast<uint8_t>(v);
                const size_t cells = static_cast<size_t>(grid.numRows) * static_cast<size_t>(grid.numCols);
                grid.values.resize(cells);
                for (size_t i = 0; i < cells; ++i) {
                        if (!r.read(4, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                        grid.values[i] = static_cast<uint8_t>(v);
                }
        }

        for (uint8_t wi = 0; wi < numWindows; ++wi) {
                WindowProcessing &wp = m._windowProcessing[wi];
                if (!r.read(1, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                wp.hasToneMapping = (v != 0);
                if (wp.hasToneMapping) {
                        if (!r.read(12, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                        wp.toneMapping.kneePointX = static_cast<uint16_t>(v);
                        if (!r.read(12, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                        wp.toneMapping.kneePointY = static_cast<uint16_t>(v);
                        if (!r.read(4, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                        uint8_t numAnchors = static_cast<uint8_t>(v);
                        if (numAnchors > MaxBezierCurveAnchors)
                                return makeError<HdrDynamic2094_40>(Error::CorruptData);
                        wp.toneMapping.bezierCurveAnchors.resize(numAnchors);
                        for (uint8_t i = 0; i < numAnchors; ++i) {
                                if (!r.read(10, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                                wp.toneMapping.bezierCurveAnchors[i] = static_cast<uint16_t>(v);
                        }
                }
                if (!r.read(1, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                wp.hasColorSaturationMapping = (v != 0);
                if (wp.hasColorSaturationMapping) {
                        if (!r.read(6, v)) return makeError<HdrDynamic2094_40>(Error::CorruptData);
                        wp.colorSaturationWeight = static_cast<uint8_t>(v);
                }
        }

        return makeResult<HdrDynamic2094_40>(std::move(m));
}

Result<HdrDynamic2094_40> HdrDynamic2094_40::fromBuffer(const Buffer &buf) {
        return fromBuffer(buf.data(), buf.size());
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

namespace {

        JsonObject jsonWindow(const HdrDynamic2094_40::Window &w) {
                JsonObject o;
                o.set("upperLeftCornerX", static_cast<int64_t>(w.upperLeftCornerX));
                o.set("upperLeftCornerY", static_cast<int64_t>(w.upperLeftCornerY));
                o.set("lowerRightCornerX", static_cast<int64_t>(w.lowerRightCornerX));
                o.set("lowerRightCornerY", static_cast<int64_t>(w.lowerRightCornerY));
                o.set("centerOfEllipseX", static_cast<int64_t>(w.centerOfEllipseX));
                o.set("centerOfEllipseY", static_cast<int64_t>(w.centerOfEllipseY));
                o.set("rotationAngle", static_cast<int64_t>(w.rotationAngle));
                o.set("semimajorAxisInternalEllipse", static_cast<int64_t>(w.semimajorAxisInternalEllipse));
                o.set("semimajorAxisExternalEllipse", static_cast<int64_t>(w.semimajorAxisExternalEllipse));
                o.set("semiminorAxisExternalEllipse", static_cast<int64_t>(w.semiminorAxisExternalEllipse));
                o.set("overlapProcessOption", w.overlapProcessOption);
                return o;
        }

        JsonObject jsonActualPeak(const HdrDynamic2094_40::ActualPeakLuminance &g) {
                JsonObject o;
                if (!g.isPresent()) {
                        o.set("present", false);
                        return o;
                }
                o.set("present", true);
                o.set("numRows", static_cast<int64_t>(g.numRows));
                o.set("numCols", static_cast<int64_t>(g.numCols));
                JsonArray arr;
                const size_t cells = static_cast<size_t>(g.numRows) * static_cast<size_t>(g.numCols);
                for (size_t i = 0; i < cells && i < g.values.size(); ++i) {
                        arr.add(static_cast<int64_t>(g.values[i]));
                }
                o.set("values", arr);
                return o;
        }

        JsonObject jsonWindowProcessing(const HdrDynamic2094_40::WindowProcessing &wp) {
                JsonObject o;
                JsonArray  maxScl;
                maxScl.add(static_cast<int64_t>(wp.maxScl[0]));
                maxScl.add(static_cast<int64_t>(wp.maxScl[1]));
                maxScl.add(static_cast<int64_t>(wp.maxScl[2]));
                o.set("maxScl", maxScl);
                o.set("averageMaxRgb", static_cast<int64_t>(wp.averageMaxRgb));
                JsonArray dist;
                for (size_t i = 0; i < wp.distribution.size(); ++i) {
                        JsonObject d;
                        d.set("percentage", static_cast<int64_t>(wp.distribution[i].percentage));
                        d.set("percentile", static_cast<int64_t>(wp.distribution[i].percentile));
                        dist.add(d);
                }
                o.set("distribution", dist);
                o.set("fractionBrightPixels", static_cast<int64_t>(wp.fractionBrightPixels));

                JsonObject tm;
                tm.set("present", wp.hasToneMapping);
                if (wp.hasToneMapping) {
                        tm.set("kneePointX", static_cast<int64_t>(wp.toneMapping.kneePointX));
                        tm.set("kneePointY", static_cast<int64_t>(wp.toneMapping.kneePointY));
                        JsonArray anchors;
                        for (size_t i = 0; i < wp.toneMapping.bezierCurveAnchors.size(); ++i) {
                                anchors.add(static_cast<int64_t>(wp.toneMapping.bezierCurveAnchors[i]));
                        }
                        tm.set("bezierCurveAnchors", anchors);
                }
                o.set("toneMapping", tm);

                JsonObject csm;
                csm.set("present", wp.hasColorSaturationMapping);
                if (wp.hasColorSaturationMapping) {
                        csm.set("weight", static_cast<int64_t>(wp.colorSaturationWeight));
                }
                o.set("colorSaturationMapping", csm);
                return o;
        }

} // namespace

JsonObject HdrDynamic2094_40::toJson() const {
        JsonObject o;
        o.set("applicationVersion", static_cast<int64_t>(_applicationVersion));
        o.set("numWindows", static_cast<int64_t>(_numWindows));
        JsonArray extra;
        for (size_t i = 0; i < _extraWindows.size(); ++i) extra.add(jsonWindow(_extraWindows[i]));
        o.set("extraWindows", extra);
        o.set("targetedSystemDisplayMaximumLuminance",
              static_cast<int64_t>(_targetedSystemDisplayMaximumLuminance));
        o.set("targetedSystemDisplayActualPeakLuminance",
              jsonActualPeak(_targetedSystemDisplayActualPeakLuminance));
        JsonArray wps;
        for (size_t i = 0; i < _windowProcessing.size(); ++i) wps.add(jsonWindowProcessing(_windowProcessing[i]));
        o.set("windowProcessing", wps);
        o.set("masteringDisplayActualPeakLuminance",
              jsonActualPeak(_masteringDisplayActualPeakLuminance));
        return o;
}

// ---------------------------------------------------------------------------
// Comparison / diagnostics
// ---------------------------------------------------------------------------

bool HdrDynamic2094_40::operator==(const HdrDynamic2094_40 &o) const {
        if (_applicationVersion != o._applicationVersion) return false;
        if (_numWindows != o._numWindows) return false;
        if (_extraWindows.size() != o._extraWindows.size()) return false;
        for (size_t i = 0; i < _extraWindows.size(); ++i) {
                if (!(_extraWindows[i] == o._extraWindows[i])) return false;
        }
        if (_targetedSystemDisplayMaximumLuminance != o._targetedSystemDisplayMaximumLuminance) return false;
        if (!(_targetedSystemDisplayActualPeakLuminance == o._targetedSystemDisplayActualPeakLuminance))
                return false;
        if (_windowProcessing.size() != o._windowProcessing.size()) return false;
        for (size_t i = 0; i < _windowProcessing.size(); ++i) {
                if (!(_windowProcessing[i] == o._windowProcessing[i])) return false;
        }
        if (!(_masteringDisplayActualPeakLuminance == o._masteringDisplayActualPeakLuminance)) return false;
        return true;
}

String HdrDynamic2094_40::toString() const {
        return String::sprintf("HdrDynamic2094_40{appVer=%u, numWindows=%u, targetedMaxLum=%u, windows=%u}",
                               static_cast<unsigned>(_applicationVersion),
                               static_cast<unsigned>(_numWindows),
                               static_cast<unsigned>(_targetedSystemDisplayMaximumLuminance),
                               static_cast<unsigned>(_windowProcessing.size()));
}

// ---------------------------------------------------------------------------
// DataStream serialization (member-API path for PROMEKI_DATATYPE)
// ---------------------------------------------------------------------------

Error HdrDynamic2094_40::writeToStream(DataStream &s) const {
        Buffer buf = toBuffer();
        s << buf;
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<HdrDynamic2094_40> HdrDynamic2094_40::readFromStream<1>(DataStream &s) {
        Buffer buf;
        s >> buf;
        if (s.status() != DataStream::Ok) return makeError<HdrDynamic2094_40>(s.toError());
        Result<HdrDynamic2094_40> r = HdrDynamic2094_40::fromBuffer(buf);
        if (r.second().isError()) {
                s.setError(DataStream::ReadCorruptData,
                           String("HdrDynamic2094_40::fromBuffer failed: ") + r.second().name());
                return makeError<HdrDynamic2094_40>(r.second());
        }
        return r;
}

PROMEKI_NAMESPACE_END
