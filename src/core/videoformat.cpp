/**
 * @file      videoformat.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cctype>
#include <promeki/videoformat.h>
#include <promeki/structdatabase.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

// Bring the FormatFlag_* enumerators into the anonymous namespace so
// PROMEKI_WELL_KNOWN_VIDEO_FORMATS can reference them unqualified.
using enum VideoFormat::WellKnownFormatFlag;

struct RasterInfo {
        VideoFormat::WellKnownRaster    id;
        String                          name;
        uint32_t                        width;
        uint32_t                        height;
};

#define X(ID, NAME, W, H) {                                 \
                .id = VideoFormat::ID, .name = NAME,        \
                .width = (W), .height = (H)                 \
        },
StructDatabase<VideoFormat::WellKnownRaster, RasterInfo> rasterDb = {
        PROMEKI_WELL_KNOWN_RASTERS
};
#undef X

struct FormatInfo {
        VideoFormat::WellKnownFormat id;
        String                       name;
        VideoFormat::WellKnownRaster raster;
        FrameRate::WellKnownRate     rate;
        VideoScanMode                scan;
        uint32_t                     flags;
};

#define X(ID, RASTER, RATE, SCAN, FLAGS) {                          \
                .id     = VideoFormat::ID,                          \
                .name   = #ID,                                      \
                .raster = VideoFormat::RASTER,                      \
                .rate   = FrameRate::RATE,                          \
                .scan   = VideoScanMode::SCAN,                      \
                .flags  = (FLAGS)                                   \
        },
StructDatabase<VideoFormat::WellKnownFormat, FormatInfo> formatDb = {
        { .id     = VideoFormat::Invalid,
          .name   = "INV",
          .raster = VideoFormat::Raster_Invalid,
          .rate   = FrameRate::FPS_Invalid,
          .scan   = VideoScanMode::Unknown,
          .flags  = 0 },
        PROMEKI_WELL_KNOWN_VIDEO_FORMATS
};
#undef X

struct RasterAlias {
        const char *                    name;
        VideoFormat::WellKnownRaster    id;
};

// Additional aliases accepted by fromString (case-insensitive).  The
// canonical names defined in PROMEKI_WELL_KNOWN_RASTERS are already
// accepted automatically by the main lookup path.
const RasterAlias kRasterAliases[] = {
        { "ntsc",   VideoFormat::Raster_SD525 },
        { "pal",    VideoFormat::Raster_SD625 },
        { "576",    VideoFormat::Raster_SD625 },
        { "720",    VideoFormat::Raster_HD720 },
        { "1080",   VideoFormat::Raster_HD    },
        { "1440",   VideoFormat::Raster_QHD   },
        { "2160",   VideoFormat::Raster_UHD   },
        { "4kuhd",  VideoFormat::Raster_UHD   },
        { "uhd4k",  VideoFormat::Raster_UHD   },
        { "dci2k",  VideoFormat::Raster_2K    },
        { "dci4k",  VideoFormat::Raster_4K    },
        { "dci8k",  VideoFormat::Raster_8K    },
        { "4320",   VideoFormat::Raster_UHD8K },
        { "8kuhd",  VideoFormat::Raster_UHD8K },
};

struct SmpteHeight {
        uint32_t height;
        uint32_t width;   ///< Canonical broadcast width at this height.
};

// Heights that get the bare "HHH{p|i|psf}" SMPTE form in toString()
// when the raster matches the listed width.
const SmpteHeight kSmpteHeights[] = {
        { 486,  720  },
        { 576,  720  },
        { 720,  1280 },
        { 1080, 1920 },
        { 2160, 3840 },
        { 4320, 7680 },
};

bool isScanInterlaced(VideoScanMode m) {
        return m == VideoScanMode::Interlaced ||
               m == VideoScanMode::InterlacedEvenFirst ||
               m == VideoScanMode::InterlacedOddFirst;
}

// Any interlaced variant matches the generic Interlaced entry used in
// PROMEKI_WELL_KNOWN_VIDEO_FORMATS; otherwise require an exact match.
bool scanMatchesWellKnown(VideoScanMode actual, VideoScanMode canonical) {
        if(actual == canonical) return true;
        return isScanInterlaced(actual) && isScanInterlaced(canonical);
}

String scanSuffix(VideoScanMode m) {
        if(m == VideoScanMode::PsF) return "psf";
        if(isScanInterlaced(m)) return "i";
        return "p";
}

// Return the well-known rate's preferred display name (e.g. "29.97"),
// or the exact-rational form when the rate is not a registered
// well-known entry.
String formatFrameRate(const FrameRate &fr) {
        FrameRate::WellKnownRate wk = fr.wellKnownRate();
        switch(wk) {
#define X(ID, NAME, NUM, DEN) case FrameRate::ID: return String(NAME);
                PROMEKI_WELL_KNOWN_FRAME_RATES
#undef X
                default: break;
        }
        return fr.toString();
}

VideoFormat::WellKnownRaster lookupRasterByName(const String &lowered) {
        for(const auto &pair : rasterDb.database()) {
                if(pair.first == VideoFormat::Raster_Invalid) continue;
                if(pair.second.name.toLower() == lowered) return pair.first;
        }
        for(const auto &alias : kRasterAliases) {
                if(lowered == alias.name) return alias.id;
        }
        return VideoFormat::Raster_NotWellKnown;
}

// Match the whole input against a WellKnownFormat identifier (e.g.
// "Smpte1080p29_97" or "dci4kp24").  Returns VideoFormat::Invalid if
// no match — distinguishing "no match" from the NotWellKnown sentinel.
VideoFormat::WellKnownFormat lookupFormatByName(const String &lowered) {
        for(const auto &pair : formatDb.database()) {
                if(pair.first == VideoFormat::Invalid) continue;
                if(pair.second.name.toLower() == lowered) return pair.first;
        }
        return VideoFormat::Invalid;
}

// Parse "<width>x<height>" (case-insensitive 'x').  Returns true on success.
bool parseExplicitRaster(const String &text, Size2Du32 &out) {
        Result<Size2Du32> r = Size2Du32::fromString(text);
        if(r.second().isError()) return false;
        out = r.first();
        return true;
}

// Parse a SMPTE height-only prefix ("1080", "2160", ...), returning the
// raster with the canonical broadcast width on success.
bool parseSmpteHeight(const String &text, Size2Du32 &out) {
        const char *s = text.cstr();
        if(s == nullptr || *s == '\0') return false;
        for(const char *p = s; *p != '\0'; ++p) {
                if(!std::isdigit(static_cast<unsigned char>(*p))) return false;
        }
        uint32_t h = static_cast<uint32_t>(std::atoi(s));
        for(const auto &sh : kSmpteHeights) {
                if(sh.height == h) {
                        out.set(sh.width, sh.height);
                        return true;
                }
        }
        return false;
}

} // namespace

VideoFormat::VideoFormat(const Size2Du32 &raster, const FrameRate &rate,
                         VideoScanMode scanMode)
        : _raster(raster), _rate(rate), _scanMode(scanMode)
{
}

VideoFormat::VideoFormat(WellKnownRaster id, const FrameRate &rate,
                         VideoScanMode scanMode)
        : _rate(rate), _scanMode(scanMode)
{
        const RasterInfo &info = rasterDb.get(id);
        if(info.id != Raster_Invalid) _raster.set(info.width, info.height);
}

VideoFormat::VideoFormat(WellKnownFormat fmt) {
        if(fmt == Invalid || fmt == NotWellKnown) return;
        const FormatInfo &info = formatDb.get(fmt);
        if(info.id == Invalid) return;
        const RasterInfo &r = rasterDb.get(info.raster);
        if(r.id == Raster_Invalid) return;
        _raster.set(r.width, r.height);
        _rate = FrameRate(info.rate);
        _scanMode = info.scan;
}

VideoFormat::WellKnownRaster VideoFormat::wellKnownRaster() const {
        if(!_raster.isValid()) return Raster_NotWellKnown;
        for(const auto &pair : rasterDb.database()) {
                if(pair.first == Raster_Invalid) continue;
                if(pair.second.width  == _raster.width() &&
                   pair.second.height == _raster.height()) {
                        return pair.first;
                }
        }
        return Raster_NotWellKnown;
}

VideoFormat::WellKnownFormat VideoFormat::wellKnownFormat() const {
        if(!isValid()) return Invalid;
        WellKnownRaster         wkRaster = wellKnownRaster();
        if(wkRaster == Raster_NotWellKnown) return NotWellKnown;
        FrameRate::WellKnownRate wkRate = _rate.wellKnownRate();
        if(wkRate == FrameRate::FPS_NotWellKnown) return NotWellKnown;
        for(const auto &pair : formatDb.database()) {
                if(pair.first == Invalid) continue;
                const FormatInfo &info = pair.second;
                if(info.raster == wkRaster &&
                   info.rate   == wkRate   &&
                   scanMatchesWellKnown(_scanMode, info.scan)) {
                        return pair.first;
                }
        }
        return NotWellKnown;
}

uint32_t VideoFormat::wellKnownFormatFlags() const {
        return formatFlags(wellKnownFormat());
}

uint32_t VideoFormat::formatFlags(WellKnownFormat fmt) {
        if(fmt == Invalid || fmt == NotWellKnown) return 0;
        return formatDb.get(fmt).flags;
}

VideoFormat::WellKnownFormatList
VideoFormat::allWellKnownFormats(uint32_t requiredFlags) {
        // Preserve the declaration order from
        // PROMEKI_WELL_KNOWN_VIDEO_FORMATS rather than the Map's
        // numerical-id order, so callers get a stable, human-ordered
        // listing.
        static const WellKnownFormatList kAll = [] {
                WellKnownFormatList list;
#define X(ID, RASTER, RATE, SCAN, FLAGS) list.pushToBack(VideoFormat::ID);
                PROMEKI_WELL_KNOWN_VIDEO_FORMATS
#undef X
                return list;
        }();

        if(requiredFlags == 0) return kAll;

        WellKnownFormatList filtered;
        for(WellKnownFormat f : kAll) {
                if((formatDb.get(f).flags & requiredFlags) == requiredFlags) {
                        filtered.pushToBack(f);
                }
        }
        return filtered;
}

String VideoFormat::rasterString(const StringOptions &opts) const {
        if(!_raster.isValid()) return String();
        const String scan = scanSuffix(_scanMode);

        if(opts.useNamedRaster) {
                WellKnownRaster id = wellKnownRaster();
                if(id != Raster_NotWellKnown) {
                        return rasterDb.get(id).name + scan;
                }
                return _raster.toString() + scan;
        }
        for(const auto &sh : kSmpteHeights) {
                if(_raster.height() == sh.height && _raster.width() == sh.width) {
                        return String::number(_raster.height()) + scan;
                }
        }
        return _raster.toString() + scan;
}

String VideoFormat::frameRateString() const {
        if(!_rate.isValid()) return String();
        if(isScanInterlaced(_scanMode)) {
                // SMPTE field rate = 2 × frame rate.  Doubling the
                // numerator keeps Rational's simplification happy
                // regardless of parity.
                FrameRate fieldRate(FrameRate::RationalType(
                        _rate.numerator() * 2, _rate.denominator()));
                return formatFrameRate(fieldRate);
        }
        return formatFrameRate(_rate);
}

String VideoFormat::toString(const StringOptions &opts) const {
        if(!isValid()) return String();
        return rasterString(opts) + frameRateString();
}

Result<VideoFormat> VideoFormat::fromString(const String &str,
                                            const ParseOptions &opts) {
        if(str.isEmpty()) return makeError<VideoFormat>(Error::Invalid);

        // Lex into tokens.  Treat whitespace and a small set of
        // "cosmetic" separator characters ('@', ',', ';') as token
        // breaks so inputs like "1920x1080 @ 29.97" or
        // "HD, 29.97" parse the same as "1920x1080p29.97".
        const String lowered = str.toLower();

        // Fast path: match the entire input against a well-known format
        // identifier ("Smpte1080p29_97", "Dci4Kp24", ...).  Case-
        // insensitive because the whole parser is.
        {
                WellKnownFormat f = lookupFormatByName(lowered);
                if(f != Invalid) return makeResult(VideoFormat(f));
        }
        const char *src = lowered.cstr();
        const size_t srcLen = lowered.length();
        StringList tokens;
        String current;
        auto isSep = [](char c) {
                return c == '@' || c == ',' || c == ';' ||
                       std::isspace(static_cast<unsigned char>(c));
        };
        for(size_t i = 0; i < srcLen; ++i) {
                const char c = src[i];
                if(isSep(c)) {
                        if(!current.isEmpty()) {
                                tokens.pushToBack(current);
                                current = String();
                        }
                } else {
                        current += c;
                }
        }
        if(!current.isEmpty()) tokens.pushToBack(current);
        if(tokens.isEmpty()) return makeError<VideoFormat>(Error::Invalid);

        // A scan marker may appear in any of the recognised positions:
        //
        //   - between raster and rate:      "1080p29.97", "1080 p 29.97"
        //   - attached to front of rate:    "1080 p29.97"
        //   - attached to end of rate:      "1080 23.98p"
        //   - as a trailing standalone:     "1080 29.97 p"
        //
        // We track the detected scan separately so later peeling
        // steps can be independent of which position we found it in.
        bool scanFound = false;
        VideoScanMode scanMode = VideoScanMode::Progressive;
        auto recordScan = [&](VideoScanMode m) {
                if(!scanFound) { scanMode = m; scanFound = true; }
        };

        // Step: strip a trailing standalone scan token if present.
        if(tokens.size() >= 2) {
                const String &last = tokens.back();
                if(last == "psf")      { recordScan(VideoScanMode::PsF);         tokens.popFromBack(); }
                else if(last == "p")   { recordScan(VideoScanMode::Progressive); tokens.popFromBack(); }
                else if(last == "i")   { recordScan(VideoScanMode::Interlaced);  tokens.popFromBack(); }
                if(tokens.isEmpty()) return makeError<VideoFormat>(Error::Invalid);
        }

        String rateToken;
        String rasterScanToken;
        if(tokens.size() == 1) {
                // Single compact token.  Walk back from the end to
                // pick up the trailing rate expression (digits, '.',
                // '/').  Everything before is the raster+scan, which
                // may still contain an inline scan marker
                // ("1080p29.97").  If a trailing standalone scan was
                // stripped above there will instead be no scan in
                // the token.
                const String &t = tokens.front();
                const char *ts = t.cstr();
                size_t tn = t.length();
                size_t rateStart = tn;
                while(rateStart > 0) {
                        char c = ts[rateStart - 1];
                        if(std::isdigit(static_cast<unsigned char>(c)) ||
                           c == '.' || c == '/') {
                                --rateStart;
                        } else {
                                break;
                        }
                }
                if(rateStart == 0 || rateStart == tn) {
                        return makeError<VideoFormat>(Error::Invalid);
                }
                rasterScanToken = t.left(rateStart);
                rateToken       = t.mid(rateStart);
        } else {
                rateToken = tokens.back();
                String prefix;
                for(size_t i = 0; i < tokens.size() - 1; ++i) prefix += tokens[i];
                if(prefix.isEmpty()) return makeError<VideoFormat>(Error::Invalid);
                rasterScanToken = prefix;

                // The rate token may carry a scan letter at its
                // front ("1080 p29.97", "2048x1080 psf24") or at its
                // tail ("1080 23.98p", "1080 24psf").  Peel whichever
                // is present.
                const char *rt  = rateToken.cstr();
                size_t       rtN = rateToken.length();
                if(rtN >= 4 && rt[0] == 'p' && rt[1] == 's' && rt[2] == 'f' &&
                   (std::isdigit(static_cast<unsigned char>(rt[3])) || rt[3] == '.')) {
                        recordScan(VideoScanMode::PsF);
                        rateToken = rateToken.mid(3);
                } else if(rtN >= 2 && (rt[0] == 'p' || rt[0] == 'i') &&
                          (std::isdigit(static_cast<unsigned char>(rt[1])) || rt[1] == '.')) {
                        recordScan(rt[0] == 'p' ? VideoScanMode::Progressive
                                                : VideoScanMode::Interlaced);
                        rateToken = rateToken.mid(1);
                } else if(rtN >= 4 && rt[rtN - 1] == 'f' && rt[rtN - 2] == 's' &&
                          rt[rtN - 3] == 'p' &&
                          (std::isdigit(static_cast<unsigned char>(rt[rtN - 4])) ||
                           rt[rtN - 4] == '.' || rt[rtN - 4] == '/')) {
                        recordScan(VideoScanMode::PsF);
                        rateToken = rateToken.left(rtN - 3);
                } else if(rtN >= 2 && (rt[rtN - 1] == 'p' || rt[rtN - 1] == 'i') &&
                          (std::isdigit(static_cast<unsigned char>(rt[rtN - 2])) ||
                           rt[rtN - 2] == '.' || rt[rtN - 2] == '/')) {
                        recordScan(rt[rtN - 1] == 'p' ? VideoScanMode::Progressive
                                                     : VideoScanMode::Interlaced);
                        rateToken = rateToken.left(rtN - 1);
                }
        }

        // Extract an inline scan suffix from the raster+scan token
        // (the canonical "1080p" / "1080i" / "1080psf" placement).
        const char *rst   = rasterScanToken.cstr();
        const size_t rstN = rasterScanToken.length();
        String rasterPart = rasterScanToken;
        if(rstN >= 3 && rst[rstN - 1] == 'f' &&
                        rst[rstN - 2] == 's' &&
                        rst[rstN - 3] == 'p') {
                recordScan(VideoScanMode::PsF);
                rasterPart = rasterScanToken.left(rstN - 3);
        } else if(rstN >= 1 && rst[rstN - 1] == 'p') {
                recordScan(VideoScanMode::Progressive);
                rasterPart = rasterScanToken.left(rstN - 1);
        } else if(rstN >= 1 && rst[rstN - 1] == 'i') {
                recordScan(VideoScanMode::Interlaced);
                rasterPart = rasterScanToken.left(rstN - 1);
        }
        if(rasterPart.isEmpty()) return makeError<VideoFormat>(Error::Invalid);

        // Final scan mode: detected placement, or Progressive if the
        // input carried no scan marker anywhere.
        const VideoScanMode mode = scanFound ? scanMode : VideoScanMode::Progressive;

        // Resolve the raster.
        Size2Du32 raster;
        WellKnownRaster wellKnown = lookupRasterByName(rasterPart);
        if(wellKnown != Raster_NotWellKnown) {
                const RasterInfo &info = rasterDb.get(wellKnown);
                raster.set(info.width, info.height);
        } else if(!parseExplicitRaster(rasterPart, raster) &&
                  !parseSmpteHeight(rasterPart, raster)) {
                return makeError<VideoFormat>(Error::Invalid);
        }

        // Parse the rate suffix.
        auto rateResult = FrameRate::fromString(rateToken);
        if(rateResult.second().isError()) {
                return makeError<VideoFormat>(Error::Invalid);
        }
        FrameRate parsedRate = rateResult.first();
        FrameRate finalRate  = parsedRate;

        if(mode == VideoScanMode::Interlaced) {
                // Halve to obtain the frame rate from the SMPTE field rate.
                FrameRate halved(FrameRate::RationalType(
                        parsedRate.numerator(), parsedRate.denominator() * 2));
                if(opts.strictInterlacedFieldRate) {
                        finalRate = halved;
                } else {
                        if(halved.isWellKnownRate()) {
                                finalRate = halved;
                        } else if(parsedRate.isWellKnownRate()) {
                                finalRate = parsedRate;
                        } else {
                                finalRate = halved;
                        }
                }
        }

        return makeResult(VideoFormat(raster, finalRate, mode));
}

PROMEKI_NAMESPACE_END

