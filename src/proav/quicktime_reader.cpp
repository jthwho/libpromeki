/**
 * @file      quicktime_reader.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include "quicktime_reader.h"
#include "quicktime_atom.h"

#include <promeki/file.h>
#include <promeki/iodevice.h>
#include <promeki/logger.h>
#include <promeki/rational.h>
#include <promeki/umid.h>

PROMEKI_NAMESPACE_BEGIN

using namespace quicktime_atom;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

/**
 * @brief Look up a PixelDesc by QuickTime FourCC.
 *
 * Walks the registered PixelDesc list and returns the first entry whose
 * fourccList contains @p code. Returns an invalid PixelDesc if no match.
 */
PixelDesc pixelDescForQuickTimeFourCC(FourCC code) {
        for(PixelDesc::ID id : PixelDesc::registeredIDs()) {
                PixelDesc pd(id);
                for(const FourCC &f : pd.fourccList()) {
                        if(f == code) return pd;
                }
        }
        return PixelDesc();
}

/**
 * @brief Translate a QuickTime PCM audio sample-entry FourCC into an
 *        AudioDesc::DataType.
 *
 * Returns Invalid for non-PCM or unknown FourCCs — the reader surfaces
 * this via an invalid AudioDesc so the MediaIOTask layer can decide
 * whether to refuse the track.
 */
AudioDesc::DataType pcmDataTypeForFourCC(FourCC code, uint16_t bitsPerSample) {
        if(code == FourCC("sowt")) return AudioDesc::PCMI_S16LE;
        if(code == FourCC("twos")) return AudioDesc::PCMI_S16BE;
        if(code == FourCC("in24")) return AudioDesc::PCMI_S24BE;
        if(code == FourCC("in32")) return AudioDesc::PCMI_S32BE;
        if(code == FourCC("fl32")) return AudioDesc::PCMI_Float32BE;
        if(code == FourCC("raw ")) return AudioDesc::PCMI_U8;
        if(code == FourCC("lpcm")) {
                // Best-effort guess based on bit depth; real flag handling
                // will land with Phase 2's full stsd parser.
                switch(bitsPerSample) {
                        case 8:  return AudioDesc::PCMI_S8;
                        case 16: return AudioDesc::PCMI_S16LE;
                        case 24: return AudioDesc::PCMI_S24LE;
                        case 32: return AudioDesc::PCMI_S32LE;
                        default: break;
                }
        }
        return AudioDesc::Invalid;
}

/**
 * @brief Read a Pascal-style string (length-prefix, fixed buffer total).
 *
 * QuickTime frequently stores fixed-length strings where the first byte
 * is a length and the remaining bytes are content, padded. Returns the
 * decoded string (possibly empty).
 */
String readPascalString(ReadStream &stream, int totalBytes) {
        if(totalBytes <= 0) return String();
        uint8_t lenByte = stream.readU8();
        int len = lenByte;
        if(len > totalBytes - 1) len = totalBytes - 1;
        String out;
        if(len > 0) {
                char buf[256] = {};
                if(stream.readBytes(buf, len).isError()) return String();
                out = String(buf, static_cast<size_t>(len));
        }
        // Skip remaining padding bytes.
        int remain = (totalBytes - 1) - len;
        if(remain > 0) stream.skip(remain);
        return out;
}

} // namespace

// ---------------------------------------------------------------------------
// QuickTimeReader
// ---------------------------------------------------------------------------

QuickTimeReader::QuickTimeReader() : QuickTime::Impl(QuickTime::Reader) {}

QuickTimeReader::~QuickTimeReader() {
        if(_isOpen) close();
        delete _metaFile;
        _metaFile = nullptr;
}

IODevice *QuickTimeReader::activeDevice() const {
        if(_device != nullptr) return _device;
        return static_cast<IODevice *>(_metaFile);
}

Error QuickTimeReader::open() {
        if(_isOpen) return Error::AlreadyOpen;
        if(_filename.isEmpty() && _device == nullptr) {
                return Error::InvalidArgument;
        }

        // Open the metadata file handle. IODevice-based operation is
        // reserved for future use; Phase 1 exercises the file path only.
        if(_device == nullptr) {
                if(_metaFile == nullptr) _metaFile = new File(_filename);
                else                     _metaFile->setFilename(_filename);
                Error err = _metaFile->open(IODevice::ReadOnly);
                if(err.isError()) {
                        promekiWarn("QuickTime: open '%s': %s",
                                    _filename.cstr(), err.name().cstr());
                        return err;
                }
        }

        IODevice *dev = activeDevice();
        auto [fileSize, sizeErr] = dev->size();
        if(sizeErr.isError()) {
                if(_metaFile != nullptr) _metaFile->close();
                return sizeErr;
        }

        Error err = parseTopLevel(fileSize);
        if(err.isError()) {
                if(_metaFile != nullptr) _metaFile->close();
                return err;
        }

        buildMediaDesc();
        _isOpen = true;

        // Anchor timecode resolution requires readSample() to work, which
        // requires _isOpen and the sample tables to be in place.
        resolveStartTimecode();

        return Error::Ok;
}

void QuickTimeReader::close() {
        if(!_isOpen) return;
        _isOpen = false;
        if(_metaFile != nullptr) {
                _metaFile->close();
        }
        _tracks.clear();
        _sampleIndices.clear();
        _mediaDesc = MediaDesc();
        _containerMetadata = Metadata();
        _startTimecode = Timecode();
        _tmcdInfo = TimecodeTrackInfo{};
        _movieTimescale = 0;
        _movieDuration = 0;
}

// ---------------------------------------------------------------------------
// Top-level atom walk
// ---------------------------------------------------------------------------

Error QuickTimeReader::parseTopLevel(int64_t fileSize) {
        IODevice *dev = activeDevice();
        ReadStream stream(dev);

        // 1. Require 'ftyp' as the first top-level box.
        Error err = stream.seek(0);
        if(err.isError()) return err;

        Box ftypBox;
        err = readBoxHeader(stream, ftypBox, fileSize);
        if(err.isError()) {
                promekiWarn("QuickTime '%s': failed to read initial box header", _filename.cstr());
                return Error::CorruptData;
        }
        if(ftypBox.type != kFtyp) {
                promekiWarn("QuickTime '%s': first box is not 'ftyp' (got '%c%c%c%c')",
                            _filename.cstr(),
                            (ftypBox.type.value() >> 24) & 0xff,
                            (ftypBox.type.value() >> 16) & 0xff,
                            (ftypBox.type.value() >>  8) & 0xff,
                            (ftypBox.type.value() >>  0) & 0xff);
                return Error::CorruptData;
        }

        // ftyp payload: major_brand (4) + minor_version (4) + N x compat brand
        FourCC majorBrand = stream.readFourCC();
        (void)stream.readU32(); // minor version, ignored
        int64_t compatBytes = ftypBox.payloadSize - 8;
        FourCC compatFirst{'\0','\0','\0','\0'};
        if(compatBytes >= 4) {
                compatFirst = stream.readFourCC();
        }
        (void)compatFirst;
        if(stream.isError()) return Error::CorruptData;

        // Record the major brand in container metadata (for diagnostics).
        _containerMetadata.set(Metadata::Software,
                String::sprintf("QuickTime/ISO-BMFF brand '%c%c%c%c'",
                        (majorBrand.value() >> 24) & 0xff,
                        (majorBrand.value() >> 16) & 0xff,
                        (majorBrand.value() >>  8) & 0xff,
                        (majorBrand.value() >>  0) & 0xff));

        // 2. Find the 'moov' box.
        Box moovBox;
        err = findTopLevelBox(stream, kMoov, ftypBox.endOffset, fileSize, moovBox);
        if(err.isError()) {
                promekiWarn("QuickTime '%s': no 'moov' box found", _filename.cstr());
                return Error::CorruptData;
        }

        err = parseMoov(moovBox.payloadOffset, moovBox.endOffset);
        if(err.isError()) return err;

        // 3. Walk for moof boxes (fragmented MP4). Each fragment appends
        //    samples to the matching track's sample index. For purely
        //    fragmented files (no samples in moov.trak.stbl) the per-track
        //    indices start empty and are filled here.
        parseFragments(moovBox.endOffset, fileSize);

        // For tracks whose frame rate could not be computed during parseTrak
        // (typically pure-fragmented files where moov.trak has no samples),
        // derive the rate from per-sample durations now that fragments have
        // been ingested. Uses the first sample's duration as the canonical
        // delta — all video samples in a single fragmented file should have
        // the same duration.
        for(size_t i = 0; i < _tracks.size(); ++i) {
                QuickTime::Track &t = _tracks[i];
                if(t.frameRate().isValid()) continue;
                if(t.type() != QuickTime::Video && t.type() != QuickTime::TimecodeTrack) continue;
                if(t.timescale() == 0) continue;
                if(_sampleIndices[i].duration.isEmpty()) continue;
                uint32_t d0 = _sampleIndices[i].duration[0];
                if(d0 == 0) continue;
                uint64_t num = t.timescale();
                uint64_t den = d0;
                uint64_t a = num, b = den;
                while(b != 0) { uint64_t tmp = b; b = a % b; a = tmp; }
                uint64_t g = a > 0 ? a : 1;
                num /= g; den /= g;
                if(num > 0 && den > 0 && num <= UINT32_MAX && den <= UINT32_MAX) {
                        t.setFrameRate(FrameRate(FrameRate::RationalType(
                                static_cast<unsigned int>(num),
                                static_cast<unsigned int>(den))));
                }
        }

        // 4. udta is commonly inside moov (handled in parseMoov), but can
        //    also appear at the top level on older files. Scan the top
        //    level for it as a fallback.
        Box udtaBox;
        err = findTopLevelBox(stream, kUdta, 0, fileSize, udtaBox);
        if(!err.isError()) {
                parseUdta(udtaBox.payloadOffset, udtaBox.endOffset);
        }

        return Error::Ok;
}

// ---------------------------------------------------------------------------
// moov / trak
// ---------------------------------------------------------------------------

Error QuickTimeReader::parseMoov(int64_t payloadOffset, int64_t payloadEnd) {
        IODevice *dev = activeDevice();
        ReadStream stream(dev);
        Error err = stream.seek(payloadOffset);
        if(err.isError()) return err;

        while(true) {
                Box box;
                err = readBoxHeader(stream, box, payloadEnd);
                if(err == Error::EndOfFile) break;
                if(err.isError()) return err;

                if(box.type == kMvhd) {
                        // version/flags
                        uint8_t version = stream.readU8();
                        stream.skip(3); // flags
                        if(version == 1) {
                                stream.skip(8); // creation_time (u64)
                                stream.skip(8); // modification_time (u64)
                                _movieTimescale = stream.readU32();
                                _movieDuration  = stream.readU64();
                        } else {
                                stream.skip(4); // creation_time (u32)
                                stream.skip(4); // modification_time (u32)
                                _movieTimescale = stream.readU32();
                                _movieDuration  = stream.readU32();
                        }
                        if(stream.isError()) return Error::CorruptData;
                        // Leave the rest (rate, volume, matrix, next_track_id)
                        // for Phase 2 or later.
                } else if(box.type == kTrak) {
                        Error tErr = parseTrak(box.payloadOffset, box.endOffset);
                        if(tErr.isError()) return tErr;
                } else if(box.type == kUdta) {
                        parseUdta(box.payloadOffset, box.endOffset);
                }

                err = advanceToSibling(stream, box);
                if(err.isError()) return err;
        }
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// trak -> tkhd / mdia / mdhd / hdlr / minf / stbl / stsd
// ---------------------------------------------------------------------------

Error QuickTimeReader::parseTrak(int64_t payloadOffset, int64_t payloadEnd) {
        IODevice *dev = activeDevice();
        ReadStream stream(dev);

        QuickTime::Track track;

        // tkhd: get track id, duration, and video display dimensions.
        Box tkhdBox;
        Error err = findTopLevelBox(stream, kTkhd, payloadOffset, payloadEnd, tkhdBox);
        if(!err.isError()) {
                uint8_t version = stream.readU8();
                stream.skip(3); // flags
                if(version == 1) {
                        stream.skip(8); // creation
                        stream.skip(8); // modification
                        track.setId(stream.readU32());
                        stream.skip(4); // reserved
                        stream.skip(8); // duration (movie timescale)
                } else {
                        stream.skip(4); // creation
                        stream.skip(4); // modification
                        track.setId(stream.readU32());
                        stream.skip(4); // reserved
                        stream.skip(4); // duration
                }
                stream.skip(8);         // reserved[2]
                stream.skip(2);         // layer
                stream.skip(2);         // alternate_group
                stream.skip(2);         // volume
                stream.skip(2);         // reserved
                stream.skip(9 * 4);     // matrix[9]
                double dispW = stream.readFixed16_16();
                double dispH = stream.readFixed16_16();
                if(!stream.isError() && dispW > 0 && dispH > 0) {
                        track.setSize(Size2Du32(static_cast<uint32_t>(dispW),
                                                static_cast<uint32_t>(dispH)));
                }
        }

        // mdia is the container for the handler + stbl.
        Box mdiaBox;
        err = findTopLevelBox(stream, kMdia, payloadOffset, payloadEnd, mdiaBox);
        if(err.isError()) {
                promekiWarn("QuickTime: trak missing mdia box");
                return Error::Ok; // Tolerate — skip this track.
        }

        // mdhd: timescale, duration, language
        Box mdhdBox;
        err = findTopLevelBox(stream, kMdhd, mdiaBox.payloadOffset, mdiaBox.endOffset, mdhdBox);
        if(!err.isError()) {
                uint8_t version = stream.readU8();
                stream.skip(3);
                uint32_t ts = 0;
                uint64_t dur = 0;
                if(version == 1) {
                        stream.skip(8); // creation
                        stream.skip(8); // modification
                        ts = stream.readU32();
                        dur = stream.readU64();
                } else {
                        stream.skip(4);
                        stream.skip(4);
                        ts = stream.readU32();
                        dur = stream.readU32();
                }
                track.setTimescale(ts);
                track.setDuration(dur);
                uint16_t lang = stream.readU16();
                stream.skip(2); // pre_defined
                track.setLanguage(decodeLanguage(lang));
        }

        // hdlr: handler type tells us whether this is video/audio/tmcd/etc.
        Box hdlrBox;
        FourCC handlerType{'\0','\0','\0','\0'};
        err = findTopLevelBox(stream, kHdlr, mdiaBox.payloadOffset, mdiaBox.endOffset, hdlrBox);
        if(!err.isError()) {
                stream.skip(4);                 // version+flags
                stream.skip(4);                 // pre_defined (ISO) / component_type (QT)
                handlerType = stream.readFourCC();
                stream.skip(12);                // reserved[3] / component_manufacturer+flags+mask
                // Remaining: name (null-terminated for ISO, pascal for QT).
                // Leave name parsing for Phase 2 — we only need the track
                // kind at this level.
        }

        if(handlerType == kHdlrVide) track.setType(QuickTime::Video);
        else if(handlerType == kHdlrSoun) track.setType(QuickTime::Audio);
        else if(handlerType == kHdlrTmcd) track.setType(QuickTime::TimecodeTrack);
        else if(handlerType == kHdlrSbtl || handlerType == kHdlrText) track.setType(QuickTime::Subtitle);
        else track.setType(QuickTime::Data);

        // minf -> stbl -> stsd / stsz
        Box minfBox;
        err = findTopLevelBox(stream, kMinf, mdiaBox.payloadOffset, mdiaBox.endOffset, minfBox);
        if(err.isError()) {
                promekiWarn("QuickTime: mdia missing minf box");
                _tracks.pushToBack(track);
                return Error::Ok;
        }

        Box stblBox;
        err = findTopLevelBox(stream, kStbl, minfBox.payloadOffset, minfBox.endOffset, stblBox);
        if(err.isError()) {
                promekiWarn("QuickTime: minf missing stbl box");
                _tracks.pushToBack(track);
                return Error::Ok;
        }

        // stsd: sample description. First entry carries the codec FourCC
        // and format-specific fields.
        Box stsdBox;
        err = findTopLevelBox(stream, kStsd, stblBox.payloadOffset, stblBox.endOffset, stsdBox);
        if(!err.isError()) {
                stream.skip(4); // version+flags
                uint32_t entryCount = stream.readU32();
                if(entryCount > 0) {
                        uint32_t entrySize = stream.readU32();
                        FourCC   entryType = stream.readFourCC();
                        stream.skip(6);                // reserved[6]
                        stream.skip(2);                // data_reference_index
                        int64_t entryHeaderBytes = 16; // size+type+reserved+data_ref
                        int64_t entryRemain = static_cast<int64_t>(entrySize) - entryHeaderBytes;

                        if(track.type() == QuickTime::Video) {
                                // Visual sample entry (QuickTime form)
                                stream.skip(2);  // version
                                stream.skip(2);  // revision
                                stream.skip(4);  // vendor
                                stream.skip(4);  // temporal quality
                                stream.skip(4);  // spatial quality
                                uint16_t w = stream.readU16();
                                uint16_t h = stream.readU16();
                                stream.skip(4);  // horiz res
                                stream.skip(4);  // vert res
                                stream.skip(4);  // data size
                                stream.skip(2);  // frame count
                                String compName = readPascalString(stream, 32);
                                stream.skip(2);  // depth
                                stream.skip(2);  // pre-defined
                                entryRemain -= 2+2+4+4+4+2+2+4+4+4+2+32+2+2;
                                if(!stream.isError()) {
                                        if(w > 0 && h > 0) {
                                                track.setSize(Size2Du32(w, h));
                                        }
                                        PixelDesc pd = pixelDescForQuickTimeFourCC(entryType);
                                        if(pd.isValid()) {
                                                track.setPixelDesc(pd);
                                        } else {
                                                promekiWarn("QuickTime: unknown video codec FourCC '%c%c%c%c'",
                                                        (entryType.value() >> 24) & 0xff,
                                                        (entryType.value() >> 16) & 0xff,
                                                        (entryType.value() >>  8) & 0xff,
                                                        (entryType.value() >>  0) & 0xff);
                                        }
                                        if(!compName.isEmpty()) {
                                                track.metadata().set(Metadata::Software, compName);
                                        }
                                }
                        } else if(track.type() == QuickTime::Audio) {
                                // Audio sample entry (QuickTime v0/v1)
                                uint16_t sampleEntryVersion = stream.readU16();
                                stream.skip(2);  // revision
                                stream.skip(4);  // vendor
                                uint16_t channels = stream.readU16();
                                uint16_t sampleSize = stream.readU16();
                                stream.skip(2);  // pre-defined / compression ID
                                stream.skip(2);  // reserved
                                uint32_t srFixed = stream.readU32(); // 16.16 fixed
                                double sr = static_cast<double>(srFixed) / 65536.0;
                                entryRemain -= 2+2+4+2+2+2+2+4;
                                if(sampleEntryVersion == 1) {
                                        stream.skip(4); // samples per packet
                                        stream.skip(4); // bytes per packet
                                        stream.skip(4); // bytes per frame
                                        stream.skip(4); // bytes per sample
                                        entryRemain -= 16;
                                }
                                if(!stream.isError() && channels > 0 && sr > 0) {
                                        AudioDesc::DataType dt = pcmDataTypeForFourCC(entryType, sampleSize);
                                        if(dt != AudioDesc::Invalid) {
                                                // Recognized PCM format.
                                                track.setAudioDesc(AudioDesc(dt, static_cast<float>(sr), channels));
                                        } else {
                                                // Unrecognized FourCC — assume it's a compressed
                                                // codec (AAC mp4a, Opus Opus, AC-3 ac-3, MP3,
                                                // etc.) and build an AudioDesc that carries the
                                                // codec tag as-is. The MediaIOTask layer surfaces
                                                // this via Audio::fromBuffer so consumers can
                                                // decode through their own codec subsystem.
                                                AudioDesc adesc;
                                                adesc.setCodecFourCC(entryType);
                                                // Channels and sample rate still apply to the
                                                // decoded output; stash them via the internal
                                                // fields — AudioDesc's only public setters
                                                // take them through the PCM constructor, so we
                                                // build via setSampleRate/setChannels.
                                                adesc.setSampleRate(static_cast<float>(sr));
                                                adesc.setChannels(channels);
                                                track.setAudioDesc(adesc);
                                        }
                                }
                        }
                        (void)entryRemain;
                }
        }

        // For timecode tracks, capture the tmcd sample-entry parameters so
        // we can later read the single sample and build a Timecode.
        if(track.type() == QuickTime::TimecodeTrack) {
                // Re-walk stsd to access the entry payload after the base header.
                Box stsdBox2;
                Error te = findTopLevelBox(stream, kStsd, stblBox.payloadOffset,
                                           stblBox.endOffset, stsdBox2);
                if(!te.isError()) {
                        stream.skip(4); // version+flags
                        uint32_t cnt = stream.readU32();
                        if(cnt > 0) {
                                int64_t entryStart = stream.pos();
                                uint32_t esize = stream.readU32();
                                FourCC etype = stream.readFourCC();
                                stream.skip(6);  // reserved
                                stream.skip(2);  // data_reference_index
                                int64_t entryPayloadEnd = entryStart + esize;
                                if(etype == FourCC("tmcd")) {
                                        TimecodeTrackInfo info;
                                        parseTimecodeSampleEntry(stream.pos(), entryPayloadEnd, info);
                                        info.present = true;
                                        info.trackIndex = _tracks.size(); // track being added below
                                        _tmcdInfo = info;
                                }
                        }
                }
        }

        // edts/elst: honor a single-entry edit list as a media-time start offset.
        Box edtsBox;
        err = findTopLevelBox(stream, FourCC("edts"), payloadOffset, payloadEnd, edtsBox);
        if(!err.isError()) {
                int64_t startOffset = 0;
                if(parseEditList(edtsBox.payloadOffset, edtsBox.endOffset, startOffset) == Error::Ok) {
                        track.setEditStartOffset(startOffset);
                }
        }

        // Build the full per-sample index. For audio tracks we use the
        // compact chunk-level representation — keeps memory bounded for
        // long captures.
        QuickTimeSampleIndex sampleIndex;
        bool isAudio = (track.type() == QuickTime::Audio);
        Error sErr = parseSampleTable(stblBox.payloadOffset, stblBox.endOffset,
                                      sampleIndex, isAudio);
        if(sErr.isError()) {
                promekiWarn("QuickTime: failed to parse sample table for track id=%u", track.id());
                _tracks.pushToBack(track);
                _sampleIndices.pushToBack(QuickTimeSampleIndex{});
                return Error::Ok;
        }
        track.setSampleCount(sampleCount(sampleIndex));

        // Compute a framerate for video/timecode tracks from duration/sampleCount.
        if(track.timescale() > 0 && track.sampleCount() > 0 && track.duration() > 0) {
                if(track.type() == QuickTime::Video || track.type() == QuickTime::TimecodeTrack) {
                        // avg = sampleCount * timescale / duration
                        uint64_t num = static_cast<uint64_t>(track.sampleCount()) *
                                       static_cast<uint64_t>(track.timescale());
                        if(num > 0) {
                                // Express as Rational by dividing by gcd; FrameRate
                                // expects integer num/den. Use duration as denominator.
                                uint64_t den = track.duration();
                                // Reduce.
                                uint64_t a = num, b = den;
                                while(b != 0) { uint64_t t = b; b = a % b; a = t; }
                                uint64_t g = a > 0 ? a : 1;
                                num /= g; den /= g;
                                if(num > 0 && den > 0 &&
                                   num <= UINT32_MAX && den <= UINT32_MAX) {
                                        track.setFrameRate(FrameRate(FrameRate::RationalType(
                                                static_cast<unsigned int>(num),
                                                static_cast<unsigned int>(den))));
                                }
                        }
                }
        }

        _tracks.pushToBack(track);
        _sampleIndices.pushToBack(std::move(sampleIndex));
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Sample table parsing (stts / ctts / stsc / stsz / stco / co64 / stss)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Sample index accessors — uniform interface for both the per-sample
// (video) and compact chunk-level (audio) storage forms.
// ---------------------------------------------------------------------------

uint64_t QuickTimeReader::sampleCount(const QuickTimeSampleIndex &idx) const {
        if(idx.audioCompact) return idx.audioTotalSamples;
        return idx.offset.size();
}

int64_t QuickTimeReader::sampleOffset(const QuickTimeSampleIndex &idx,
                                      uint64_t sampleIndex) const {
        if(!idx.audioCompact) return idx.offset[sampleIndex];
        // Binary search the chunk whose first-sample index is <= sampleIndex
        // and whose first-sample + samples-per-chunk > sampleIndex.
        const List<uint64_t> &starts = idx.audioChunkFirstSample;
        size_t lo = 0, hi = starts.size();
        while(lo + 1 < hi) {
                size_t mid = lo + (hi - lo) / 2;
                if(starts[mid] <= sampleIndex) lo = mid;
                else                           hi = mid;
        }
        uint64_t offsetInChunk = sampleIndex - starts[lo];
        return idx.audioChunkOffsets[lo] +
               static_cast<int64_t>(offsetInChunk) * static_cast<int64_t>(idx.audioSampleSize);
}

uint32_t QuickTimeReader::sampleSize(const QuickTimeSampleIndex &idx,
                                     uint64_t sampleIndex) const {
        if(!idx.audioCompact) return idx.size[sampleIndex];
        (void)sampleIndex;
        return idx.audioSampleSize;
}

int64_t QuickTimeReader::sampleDts(const QuickTimeSampleIndex &idx,
                                   uint64_t sampleIndex) const {
        if(!idx.audioCompact) return idx.dts[sampleIndex];
        return static_cast<int64_t>(sampleIndex) * static_cast<int64_t>(idx.audioSampleDelta);
}

Error QuickTimeReader::parseSampleTable(int64_t stblPayloadOffset, int64_t stblPayloadEnd,
                                        QuickTimeSampleIndex &out, bool isAudio) {
        IODevice *dev = activeDevice();
        ReadStream stream(dev);

        // 1. stsz — per-sample sizes (or constant size + count)
        uint32_t constSize = 0;
        uint32_t sampleCount = 0;
        List<uint32_t> sizes;
        {
                Box stszBox;
                Error err = findTopLevelBox(stream, kStsz, stblPayloadOffset, stblPayloadEnd, stszBox);
                if(err.isError()) return err;
                stream.skip(4);                          // version+flags
                constSize    = stream.readU32();
                sampleCount  = stream.readU32();
                if(stream.isError()) return Error::CorruptData;
                if(constSize == 0) {
                        sizes.reserve(sampleCount);
                        for(uint32_t i = 0; i < sampleCount; ++i) {
                                sizes.pushToBack(stream.readU32());
                        }
                        if(stream.isError()) return Error::CorruptData;
                }
        }

        // 2. stsc — sample-to-chunk mapping (run-length)
        struct StscEntry { uint32_t firstChunk; uint32_t samplesPerChunk; uint32_t descIndex; };
        List<StscEntry> stsc;
        {
                Box stscBox;
                Error err = findTopLevelBox(stream, kStsc, stblPayloadOffset, stblPayloadEnd, stscBox);
                if(err.isError()) return err;
                stream.skip(4); // version+flags
                uint32_t entryCount = stream.readU32();
                stsc.reserve(entryCount);
                for(uint32_t i = 0; i < entryCount; ++i) {
                        StscEntry e;
                        e.firstChunk      = stream.readU32();
                        e.samplesPerChunk = stream.readU32();
                        e.descIndex       = stream.readU32();
                        stsc.pushToBack(e);
                }
                if(stream.isError()) return Error::CorruptData;
        }

        // 3. stco / co64 — per-chunk file offsets
        List<int64_t> chunkOffsets;
        {
                Box stcoBox;
                Error err = findTopLevelBox(stream, kStco, stblPayloadOffset, stblPayloadEnd, stcoBox);
                bool is64 = false;
                if(err.isError()) {
                        err = findTopLevelBox(stream, kCo64, stblPayloadOffset, stblPayloadEnd, stcoBox);
                        if(err.isError()) return err;
                        is64 = true;
                }
                stream.skip(4); // version+flags
                uint32_t entryCount = stream.readU32();
                chunkOffsets.reserve(entryCount);
                for(uint32_t i = 0; i < entryCount; ++i) {
                        if(is64) chunkOffsets.pushToBack(static_cast<int64_t>(stream.readU64()));
                        else     chunkOffsets.pushToBack(static_cast<int64_t>(stream.readU32()));
                }
                if(stream.isError()) return Error::CorruptData;
        }

        // 4. stts — time-to-sample (run-length, decode delta per sample)
        struct SttsEntry { uint32_t count; uint32_t delta; };
        List<SttsEntry> stts;
        {
                Box sttsBox;
                Error err = findTopLevelBox(stream, kStts, stblPayloadOffset, stblPayloadEnd, sttsBox);
                if(!err.isError()) {
                        stream.skip(4);
                        uint32_t ec = stream.readU32();
                        stts.reserve(ec);
                        for(uint32_t i = 0; i < ec; ++i) {
                                SttsEntry e;
                                e.count = stream.readU32();
                                e.delta = stream.readU32();
                                stts.pushToBack(e);
                        }
                        if(stream.isError()) return Error::CorruptData;
                }
        }

        // 5. ctts — composition offset (optional, run-length)
        struct CttsEntry { uint32_t count; int32_t offset; };
        List<CttsEntry> ctts;
        bool cttsV1 = false;
        {
                Box cttsBox;
                Error err = findTopLevelBox(stream, FourCC("ctts"), stblPayloadOffset, stblPayloadEnd, cttsBox);
                if(!err.isError()) {
                        uint8_t version = stream.readU8();
                        cttsV1 = (version == 1);
                        stream.skip(3); // flags
                        uint32_t ec = stream.readU32();
                        ctts.reserve(ec);
                        for(uint32_t i = 0; i < ec; ++i) {
                                CttsEntry e;
                                e.count  = stream.readU32();
                                e.offset = static_cast<int32_t>(stream.readU32());
                                ctts.pushToBack(e);
                        }
                        if(stream.isError()) return Error::CorruptData;
                }
        }

        // 6. stss — sync samples (optional; absent → all sync)
        List<uint32_t> stss;
        {
                Box stssBox;
                Error err = findTopLevelBox(stream, kStss, stblPayloadOffset, stblPayloadEnd, stssBox);
                if(!err.isError()) {
                        stream.skip(4); // version+flags
                        uint32_t ec = stream.readU32();
                        stss.reserve(ec);
                        for(uint32_t i = 0; i < ec; ++i) {
                                stss.pushToBack(stream.readU32());
                        }
                        if(stream.isError()) return Error::CorruptData;
                }
        }

        // ---- Compact audio path ----
        //
        // For audio tracks with a canonical PCM layout — constant stsz
        // sample_size AND single-entry stts — we avoid expanding to
        // per-sample arrays and instead keep chunk-level metadata.
        // This caps memory usage for long captures at O(chunks) rather
        // than O(PCM frames), which matters dramatically: one hour of
        // 48 kHz stereo would otherwise consume ~7 GB of per-sample
        // arrays alone.
        //
        // ctts and stss are not meaningful for PCM audio (no
        // composition offsets, every sample is "sync"), so we only
        // take this path when they're empty.
        bool canCompact = isAudio
                       && constSize != 0
                       && stts.size() == 1
                       && ctts.isEmpty()
                       && stss.isEmpty();
        if(canCompact) {
                out.audioCompact        = true;
                out.audioSampleSize     = constSize;
                out.audioSampleDelta    = stts[0].delta;
                out.audioTotalSamples   = sampleCount;
                out.audioChunkOffsets.reserve(chunkOffsets.size());
                out.audioChunkSamplesPerChunk.reserve(chunkOffsets.size());
                out.audioChunkFirstSample.reserve(chunkOffsets.size());

                uint32_t numChunks = static_cast<uint32_t>(chunkOffsets.size());
                uint32_t sIdx = 0;
                size_t   stscI = 0;
                for(uint32_t chunk = 1; chunk <= numChunks && sIdx < sampleCount; ++chunk) {
                        while(stscI + 1 < stsc.size() && stsc[stscI + 1].firstChunk <= chunk) {
                                stscI++;
                        }
                        uint32_t spc = stsc[stscI].samplesPerChunk;
                        uint32_t take = spc;
                        if(sIdx + take > sampleCount) take = sampleCount - sIdx;
                        out.audioChunkOffsets.pushToBack(chunkOffsets[chunk - 1]);
                        out.audioChunkSamplesPerChunk.pushToBack(take);
                        out.audioChunkFirstSample.pushToBack(sIdx);
                        sIdx += take;
                }
                if(sIdx != sampleCount) {
                        promekiWarn("QuickTime: compact audio stsc/stco produced %u samples, "
                                    "stsz says %u", sIdx, sampleCount);
                        return Error::CorruptData;
                }
                return Error::Ok;
        }

        // ---- Per-sample expansion path (video, timecode, non-canonical audio) ----
        // Per-sample size
        out.size.reserve(sampleCount);
        if(constSize != 0) {
                for(uint32_t i = 0; i < sampleCount; ++i) out.size.pushToBack(constSize);
        } else {
                for(uint32_t i = 0; i < sampleCount; ++i) out.size.pushToBack(sizes[i]);
        }

        // Per-sample offset (chunk_offset + cumulative size of preceding samples in same chunk)
        out.offset.reserve(sampleCount);
        {
                uint32_t numChunks = static_cast<uint32_t>(chunkOffsets.size());
                uint32_t sIdx = 0;
                size_t   stscI = 0;
                for(uint32_t chunk = 1; chunk <= numChunks && sIdx < sampleCount; ++chunk) {
                        // Advance stsc index while next entry's first_chunk applies.
                        while(stscI + 1 < stsc.size() && stsc[stscI + 1].firstChunk <= chunk) {
                                stscI++;
                        }
                        uint32_t spc = stsc[stscI].samplesPerChunk;
                        int64_t  off = chunkOffsets[chunk - 1];
                        for(uint32_t k = 0; k < spc && sIdx < sampleCount; ++k) {
                                out.offset.pushToBack(off);
                                off += static_cast<int64_t>(out.size[sIdx]);
                                sIdx++;
                        }
                }
                if(sIdx != sampleCount) {
                        promekiWarn("QuickTime: stsc/stco produced %u samples, stsz says %u",
                                    sIdx, sampleCount);
                        return Error::CorruptData;
                }
        }

        // Per-sample duration + dts (cumulative)
        out.duration.reserve(sampleCount);
        out.dts.reserve(sampleCount);
        {
                int64_t  dts = 0;
                uint32_t sIdx = 0;
                for(const SttsEntry &e : stts) {
                        for(uint32_t k = 0; k < e.count && sIdx < sampleCount; ++k) {
                                out.duration.pushToBack(e.delta);
                                out.dts.pushToBack(dts);
                                dts += e.delta;
                                sIdx++;
                        }
                }
                // If stts under-fills, pad with zeros (defensive).
                while(sIdx < sampleCount) {
                        out.duration.pushToBack(0);
                        out.dts.pushToBack(dts);
                        sIdx++;
                }
        }

        // Per-sample pts = dts + ctts offset (if any)
        out.pts.reserve(sampleCount);
        {
                if(ctts.isEmpty()) {
                        for(uint32_t i = 0; i < sampleCount; ++i) out.pts.pushToBack(out.dts[i]);
                } else {
                        uint32_t sIdx = 0;
                        for(const CttsEntry &e : ctts) {
                                for(uint32_t k = 0; k < e.count && sIdx < sampleCount; ++k) {
                                        int64_t cttsOff = cttsV1 ? static_cast<int64_t>(e.offset)
                                                                 : static_cast<int64_t>(static_cast<uint32_t>(e.offset));
                                        out.pts.pushToBack(out.dts[sIdx] + cttsOff);
                                        sIdx++;
                                }
                        }
                        while(sIdx < sampleCount) {
                                out.pts.pushToBack(out.dts[sIdx]);
                                sIdx++;
                        }
                }
        }

        // Per-sample keyframe flag
        out.keyframe.reserve(sampleCount);
        if(stss.isEmpty()) {
                for(uint32_t i = 0; i < sampleCount; ++i) out.keyframe.pushToBack(1);
        } else {
                size_t kfIdx = 0;
                for(uint32_t i = 0; i < sampleCount; ++i) {
                        bool isKey = false;
                        if(kfIdx < stss.size() && stss[kfIdx] == i + 1) {
                                isKey = true;
                                kfIdx++;
                        }
                        out.keyframe.pushToBack(isKey ? 1 : 0);
                }
        }

        return Error::Ok;
}

// ---------------------------------------------------------------------------
// edts/elst — single-entry edit list start offset
// ---------------------------------------------------------------------------

Error QuickTimeReader::parseEditList(int64_t edtsPayloadOffset, int64_t edtsPayloadEnd,
                                     int64_t &outStartOffset) {
        outStartOffset = 0;
        IODevice *dev = activeDevice();
        ReadStream stream(dev);

        Box elstBox;
        Error err = findTopLevelBox(stream, FourCC("elst"), edtsPayloadOffset, edtsPayloadEnd, elstBox);
        if(err.isError()) return err;

        uint8_t version = stream.readU8();
        stream.skip(3); // flags
        uint32_t entryCount = stream.readU32();
        if(stream.isError()) return Error::CorruptData;
        if(entryCount == 0) return Error::Ok;
        if(entryCount > 1) {
                promekiWarn("QuickTime: multi-entry elst (%u entries) treated as identity",
                            entryCount);
                return Error::Ok;
        }

        int64_t mediaTime = 0;
        if(version == 1) {
                stream.skip(8);                                 // segment_duration
                mediaTime = static_cast<int64_t>(stream.readU64());
                stream.skip(4);                                 // media_rate
        } else {
                stream.skip(4);                                 // segment_duration
                mediaTime = static_cast<int32_t>(stream.readU32());
                stream.skip(4);                                 // media_rate
        }
        if(stream.isError()) return Error::CorruptData;
        if(mediaTime < 0) {
                // Empty edit (used to insert silence). Treat as identity.
                return Error::Ok;
        }
        outStartOffset = mediaTime;
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// tmcd — timecode sample entry capture
// ---------------------------------------------------------------------------

Error QuickTimeReader::parseTimecodeSampleEntry(int64_t entryPayloadOffset,
                                                int64_t entryPayloadEnd,
                                                TimecodeTrackInfo &info) {
        IODevice *dev = activeDevice();
        ReadStream stream(dev);
        Error err = stream.seek(entryPayloadOffset);
        if(err.isError()) return err;

        // tmcd entry payload (after the standard 8-byte sample-entry header that
        // the caller has already consumed): 4 bytes reserved, 4 bytes flags,
        // 4 bytes timescale, 4 bytes frame_duration, 1 byte number_of_frames,
        // 1 byte reserved, then optional name atom.
        stream.skip(4);                          // reserved
        info.flags         = stream.readU32();
        info.timescale     = stream.readU32();
        info.frameDuration = stream.readU32();
        info.numberOfFrames = stream.readU8();
        stream.skip(1);                          // reserved
        if(stream.isError()) return Error::CorruptData;
        (void)entryPayloadEnd;
        return Error::Ok;
}

Error QuickTimeReader::resolveStartTimecode() {
        if(!_tmcdInfo.present) return Error::Ok;
        if(_tmcdInfo.trackIndex >= _tracks.size()) return Error::Ok;

        // Read the single 4-byte sample of the timecode track.
        QuickTime::Sample s;
        Error err = readSample(_tmcdInfo.trackIndex, 0, s);
        if(err.isError()) return err;
        if(!s.data.isValid() || s.data->size() < 4) return Error::CorruptData;

        const uint8_t *bytes = static_cast<const uint8_t *>(s.data->data());
        uint32_t startFrame = (static_cast<uint32_t>(bytes[0]) << 24) |
                              (static_cast<uint32_t>(bytes[1]) << 16) |
                              (static_cast<uint32_t>(bytes[2]) <<  8) |
                               static_cast<uint32_t>(bytes[3]);

        // Map (numberOfFrames, drop-frame) to a TimecodeType.
        bool dropFrame = (_tmcdInfo.flags & 0x01) != 0;
        Timecode::Mode mode;
        switch(_tmcdInfo.numberOfFrames) {
                case 24: mode = Timecode::Mode(Timecode::NDF24); break;
                case 25: mode = Timecode::Mode(Timecode::NDF25); break;
                case 30: mode = Timecode::Mode(dropFrame ? Timecode::DF30 : Timecode::NDF30); break;
                default:
                        promekiWarn("QuickTime: tmcd unsupported frame count %u (df=%d)",
                                    _tmcdInfo.numberOfFrames, dropFrame ? 1 : 0);
                        return Error::Ok;
        }

        Timecode tc = Timecode::fromFrameNumber(mode, startFrame);
        if(tc.isValid()) _startTimecode = tc;
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Image-file handle. We share the metadata file for both atom parsing
// and bulk video reads. File::readBulk() toggles direct I/O on/off
// internally for the aligned interior of large reads, so the file is
// opened in normal (non-DIO) mode and readBulk handles the rest.
// This matches the established DPX/CIN reader pattern.
// ---------------------------------------------------------------------------

Error QuickTimeReader::ensureImageFile() {
        return _metaFile != nullptr ? Error::Ok : Error::NotOpen;
}

// ---------------------------------------------------------------------------
// readSample
// ---------------------------------------------------------------------------

Error QuickTimeReader::readSample(size_t trackIndex, uint64_t sampleIndex,
                                  QuickTime::Sample &out) {
        if(!_isOpen) return Error::NotOpen;
        if(trackIndex >= _tracks.size()) return Error::OutOfRange;
        const QuickTime::Track  &track = _tracks[trackIndex];
        const QuickTimeSampleIndex &idx = _sampleIndices[trackIndex];
        if(sampleIndex >= sampleCount(idx)) return Error::OutOfRange;

        int64_t  off  = sampleOffset(idx, sampleIndex);
        uint32_t sz   = sampleSize(idx, sampleIndex);
        int64_t  dts  = sampleDts(idx, sampleIndex);
        out.trackId   = track.id();
        out.index     = sampleIndex;
        out.dts       = dts;
        // Audio compact path has no per-sample ctts and every sample is sync.
        out.pts       = idx.audioCompact ? dts                     : idx.pts[sampleIndex];
        out.duration  = idx.audioCompact ? idx.audioSampleDelta    : idx.duration[sampleIndex];
        out.keyframe  = idx.audioCompact ? true                    : (idx.keyframe[sampleIndex] != 0);

        if(track.type() == QuickTime::Video && _metaFile != nullptr) {
                File *f = _metaFile;
                size_t align = Buffer::DefaultAlign;
                {
                        auto [a, aerr] = f->directIOAlignment();
                        if(!aerr.isError() && a > 0) align = a;
                }
                // readBulk() shifts the buffer view to land on the requested
                // payload, so the underlying allocation needs an extra
                // alignment block of headroom.
                Buffer buf(static_cast<size_t>(sz) + align, align);
                Error e = f->seek(off);
                if(e.isError()) return e;
                e = f->readBulk(buf, static_cast<int64_t>(sz));
                if(e.isError()) return e;
                if(buf.size() != sz) {
                        promekiWarn("QuickTime: short read %llu of %u for video sample %llu",
                                    static_cast<unsigned long long>(buf.size()), sz,
                                    static_cast<unsigned long long>(sampleIndex));
                        return Error::IOError;
                }
                out.data = Buffer::Ptr::create(std::move(buf));
                return Error::Ok;
        }

        // Audio / timecode / other: normal buffered read via the metadata handle.
        IODevice *dev = activeDevice();
        Error e = dev->seek(off);
        if(e.isError()) return e;
        Buffer buf(static_cast<size_t>(sz));
        int64_t got = dev->read(buf.data(), static_cast<int64_t>(sz));
        if(got != static_cast<int64_t>(sz)) {
                promekiWarn("QuickTime: short read %lld of %u for sample %llu",
                            static_cast<long long>(got), sz,
                            static_cast<unsigned long long>(sampleIndex));
                return Error::IOError;
        }
        buf.setSize(sz);
        out.data = Buffer::Ptr::create(std::move(buf));
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// readSampleRange — optimized contiguous-run batch read
//
// Groups consecutive samples that are already adjacent on disk into a
// single read operation. For audio written with canonical PCM chunking
// this collapses a video-frame's worth of samples into one (or a few)
// read() calls instead of N.
// ---------------------------------------------------------------------------

Error QuickTimeReader::readSampleRange(size_t trackIndex, uint64_t startSampleIndex,
                                       uint64_t count, QuickTime::Sample &out) {
        if(!_isOpen) return Error::NotOpen;
        if(trackIndex >= _tracks.size()) return Error::OutOfRange;
        if(count == 0) return Error::InvalidArgument;
        const QuickTime::Track        &track = _tracks[trackIndex];
        const QuickTimeSampleIndex    &idx   = _sampleIndices[trackIndex];
        if(startSampleIndex + count > sampleCount(idx)) return Error::OutOfRange;

        // Compute total byte count for the range.
        uint64_t totalBytes = 0;
        for(uint64_t i = 0; i < count; ++i) {
                totalBytes += sampleSize(idx, startSampleIndex + i);
        }
        if(totalBytes == 0) return Error::InvalidArgument;

        Buffer out_buf(static_cast<size_t>(totalBytes));
        uint8_t *dst = static_cast<uint8_t *>(out_buf.data());
        size_t dstPos = 0;

        IODevice *dev = activeDevice();

        // Walk the range, coalescing samples that are adjacent on disk
        // (sample[i+1].offset == sample[i].offset + sample[i].size) into
        // a single read. For the compact audio path this collapses each
        // chunk into one read at the chunk boundary.
        uint64_t i = 0;
        while(i < count) {
                int64_t  startOff  = sampleOffset(idx, startSampleIndex + i);
                uint64_t runBytes  = sampleSize(idx, startSampleIndex + i);
                uint64_t j         = i + 1;
                while(j < count) {
                        int64_t expected = startOff + static_cast<int64_t>(runBytes);
                        if(sampleOffset(idx, startSampleIndex + j) != expected) break;
                        runBytes += sampleSize(idx, startSampleIndex + j);
                        j++;
                }
                Error e = dev->seek(startOff);
                if(e.isError()) return e;
                int64_t got = dev->read(dst + dstPos, static_cast<int64_t>(runBytes));
                if(got != static_cast<int64_t>(runBytes)) {
                        promekiWarn("QuickTime: readSampleRange short read %lld of %llu",
                                    static_cast<long long>(got),
                                    static_cast<unsigned long long>(runBytes));
                        return Error::IOError;
                }
                dstPos += runBytes;
                i = j;
        }
        out_buf.setSize(dstPos);

        // Populate the Sample with the first sample's metadata.
        int64_t firstDts = sampleDts(idx, startSampleIndex);
        out.trackId  = track.id();
        out.index    = startSampleIndex;
        out.dts      = firstDts;
        out.pts      = idx.audioCompact ? firstDts                 : idx.pts[startSampleIndex];
        out.duration = idx.audioCompact ? idx.audioSampleDelta     : idx.duration[startSampleIndex];
        out.keyframe = idx.audioCompact ? true                     : (idx.keyframe[startSampleIndex] != 0);
        out.data     = Buffer::Ptr::create(std::move(out_buf));
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Fragmented MP4 (moof / traf / trun)
//
// Walks the rest of the file looking for moof boxes after moov has been
// processed. Each fragment's samples are appended to the matching track's
// existing sample index. For pure fragmented files (where moov.trak.stbl
// is empty), the per-track index starts empty and grows with each moof.
// ---------------------------------------------------------------------------

size_t QuickTimeReader::findTrackIndexById(uint32_t trackId) const {
        for(size_t i = 0; i < _tracks.size(); ++i) {
                if(_tracks[i].id() == trackId) return i;
        }
        return SIZE_MAX;
}

Error QuickTimeReader::parseFragments(int64_t startOffset, int64_t fileSize) {
        IODevice *dev = activeDevice();
        ReadStream stream(dev);
        Error err = stream.seek(startOffset);
        if(err.isError()) return err;

        int64_t pos = startOffset;
        while(pos < fileSize) {
                if(stream.seek(pos).isError()) break;
                Box box;
                Error e = readBoxHeader(stream, box, fileSize);
                if(e == Error::EndOfFile) break;
                if(e.isError()) {
                        promekiWarn("QuickTime: bad box header at offset %lld during fragment scan",
                                    static_cast<long long>(pos));
                        break;
                }

                if(box.type == kMoof) {
                        Error me = parseMoof(box.payloadOffset, box.endOffset, box.headerOffset);
                        if(me.isError()) {
                                promekiWarn("QuickTime: parseMoof failed at offset %lld: %s",
                                            static_cast<long long>(box.headerOffset), me.name().cstr());
                                // Continue scanning — a corrupt fragment shouldn't kill open().
                        }
                }
                // Other top-level boxes (mdat, sidx, mfra, free, ...) are skipped
                // — Phase 3 only consumes the moof index.

                if(box.payloadSize < 0) break;
                pos = box.endOffset;
        }
        return Error::Ok;
}

Error QuickTimeReader::parseMoof(int64_t moofPayloadOffset, int64_t moofPayloadEnd, int64_t moofStart) {
        IODevice *dev = activeDevice();
        ReadStream stream(dev);
        Error err = stream.seek(moofPayloadOffset);
        if(err.isError()) return err;

        while(true) {
                Box box;
                Error e = readBoxHeader(stream, box, moofPayloadEnd);
                if(e == Error::EndOfFile) break;
                if(e.isError()) return e;

                if(box.type == kMfhd) {
                        // mfhd: version+flags(4) + sequence_number(4)
                        // Sequence number is informational only.
                } else if(box.type == kTraf) {
                        Error te = parseTraf(box.payloadOffset, box.endOffset, moofStart);
                        if(te.isError()) {
                                promekiWarn("QuickTime: parseTraf failed: %s", te.name().cstr());
                        }
                }

                e = advanceToSibling(stream, box);
                if(e.isError()) return e;
        }
        return Error::Ok;
}

Error QuickTimeReader::parseTraf(int64_t trafPayloadOffset, int64_t trafPayloadEnd, int64_t moofStart) {
        IODevice *dev = activeDevice();
        ReadStream stream(dev);

        // tfhd is required and supplies the track_ID + per-track defaults.
        Box tfhdBox;
        Error err = findTopLevelBox(stream, kTfhd, trafPayloadOffset, trafPayloadEnd, tfhdBox);
        if(err.isError()) {
                promekiWarn("QuickTime: traf missing tfhd");
                return Error::CorruptData;
        }

        stream.skip(1); // version
        uint8_t f0 = stream.readU8();
        uint8_t f1 = stream.readU8();
        uint8_t f2 = stream.readU8();
        uint32_t flags = (static_cast<uint32_t>(f0) << 16) |
                         (static_cast<uint32_t>(f1) <<  8) |
                          static_cast<uint32_t>(f2);
        uint32_t trackId = stream.readU32();

        bool     baseDataOffsetPresent = (flags & 0x000001) != 0;
        bool     sampleDescIndexPresent = (flags & 0x000002) != 0;
        bool     defSampleDurationPresent = (flags & 0x000008) != 0;
        bool     defSampleSizePresent = (flags & 0x000010) != 0;
        bool     defSampleFlagsPresent = (flags & 0x000020) != 0;
        bool     defaultBaseIsMoof = (flags & 0x020000) != 0;

        uint64_t baseDataOffset = 0;
        if(baseDataOffsetPresent)    baseDataOffset = stream.readU64();
        if(sampleDescIndexPresent)   stream.skip(4);
        uint32_t defSampleDuration = defSampleDurationPresent ? stream.readU32() : 0;
        uint32_t defSampleSize     = defSampleSizePresent     ? stream.readU32() : 0;
        uint32_t defSampleFlags    = defSampleFlagsPresent    ? stream.readU32() : 0;
        if(stream.isError()) return Error::CorruptData;

        size_t trackIdx = findTrackIndexById(trackId);
        if(trackIdx == SIZE_MAX) {
                // Unknown track — silently skip. The traf will be advanced
                // past by the caller's box walker.
                return Error::Ok;
        }

        // Optional tfdt: base media decode time for the first sample of this fragment.
        int64_t cursorDts = 0;
        Box tfdtBox;
        if(findTopLevelBox(stream, kTfdt, trafPayloadOffset, trafPayloadEnd, tfdtBox) == Error::Ok) {
                uint8_t tfdtVersion = stream.readU8();
                stream.skip(3); // flags
                if(tfdtVersion == 1) cursorDts = static_cast<int64_t>(stream.readU64());
                else                 cursorDts = static_cast<int64_t>(stream.readU32());
                if(stream.isError()) return Error::CorruptData;
        } else {
                // No tfdt: continue from the previous fragment's last dts on this track.
                if(!_sampleIndices[trackIdx].dts.isEmpty()) {
                        size_t last = _sampleIndices[trackIdx].dts.size() - 1;
                        cursorDts = _sampleIndices[trackIdx].dts[last] +
                                    static_cast<int64_t>(_sampleIndices[trackIdx].duration[last]);
                }
        }

        // Walk the traf children for trun boxes (there can be more than one).
        if(stream.seek(trafPayloadOffset).isError()) return Error::IOError;
        int64_t prevDataEnd = -1;
        while(true) {
                Box box;
                Error e = readBoxHeader(stream, box, trafPayloadEnd);
                if(e == Error::EndOfFile) break;
                if(e.isError()) return e;

                if(box.type == kTrun) {
                        Error re = parseTrun(box.payloadOffset, box.endOffset, trackIdx, moofStart,
                                             baseDataOffsetPresent, baseDataOffset, defaultBaseIsMoof,
                                             defSampleDuration, defSampleSize, defSampleFlags,
                                             cursorDts, prevDataEnd);
                        if(re.isError()) {
                                promekiWarn("QuickTime: parseTrun failed: %s", re.name().cstr());
                        }
                }

                e = advanceToSibling(stream, box);
                if(e.isError()) return e;
        }

        // Update the track's sample count after appending.
        _tracks[trackIdx].setSampleCount(_sampleIndices[trackIdx].offset.size());
        return Error::Ok;
}

Error QuickTimeReader::parseTrun(int64_t trunPayloadOffset, int64_t trunPayloadEnd,
                                 size_t trackIdx, int64_t moofStart,
                                 bool baseDataOffsetPresent, uint64_t baseDataOffset,
                                 bool defaultBaseIsMoof,
                                 uint32_t defSampleDuration, uint32_t defSampleSize,
                                 uint32_t defSampleFlags,
                                 int64_t &cursorDts, int64_t &prevDataEnd) {
        IODevice *dev = activeDevice();
        ReadStream stream(dev);
        Error err = stream.seek(trunPayloadOffset);
        if(err.isError()) return err;

        uint8_t version = stream.readU8();
        uint8_t f0 = stream.readU8();
        uint8_t f1 = stream.readU8();
        uint8_t f2 = stream.readU8();
        uint32_t flags = (static_cast<uint32_t>(f0) << 16) |
                         (static_cast<uint32_t>(f1) <<  8) |
                          static_cast<uint32_t>(f2);
        uint32_t sampleCount = stream.readU32();
        if(stream.isError()) return Error::CorruptData;

        bool     dataOffsetPresent      = (flags & 0x000001) != 0;
        bool     firstSampleFlagsPresent = (flags & 0x000004) != 0;
        bool     perSampleDuration      = (flags & 0x000100) != 0;
        bool     perSampleSize          = (flags & 0x000200) != 0;
        bool     perSampleFlags         = (flags & 0x000400) != 0;
        bool     perSampleCts           = (flags & 0x000800) != 0;

        int32_t  dataOffset = 0;
        uint32_t firstSampleFlags = 0;
        if(dataOffsetPresent)       dataOffset       = static_cast<int32_t>(stream.readU32());
        if(firstSampleFlagsPresent) firstSampleFlags = stream.readU32();
        if(stream.isError()) return Error::CorruptData;

        // Compute the byte offset of the first sample in this run.
        int64_t base;
        if(defaultBaseIsMoof)         base = moofStart;
        else if(baseDataOffsetPresent) base = static_cast<int64_t>(baseDataOffset);
        else if(prevDataEnd >= 0)     base = prevDataEnd;
        else                          base = moofStart; // ISO-BMFF default

        int64_t curOffset = base + (dataOffsetPresent ? dataOffset : 0);

        QuickTimeSampleIndex &idx = _sampleIndices[trackIdx];
        idx.offset.reserve(idx.offset.size() + sampleCount);

        for(uint32_t i = 0; i < sampleCount; ++i) {
                uint32_t dur = perSampleDuration ? stream.readU32() : defSampleDuration;
                uint32_t sz  = perSampleSize     ? stream.readU32() : defSampleSize;
                uint32_t sfl;
                if(perSampleFlags) {
                        sfl = stream.readU32();
                } else if(i == 0 && firstSampleFlagsPresent) {
                        sfl = firstSampleFlags;
                } else {
                        sfl = defSampleFlags;
                }
                int32_t cts = 0;
                if(perSampleCts) {
                        uint32_t raw = stream.readU32();
                        cts = (version == 1) ? static_cast<int32_t>(raw)
                                             : static_cast<int32_t>(raw); // both interpretations
                }
                if(stream.isError()) return Error::CorruptData;

                idx.offset.pushToBack(curOffset);
                idx.size.pushToBack(sz);
                idx.dts.pushToBack(cursorDts);
                idx.pts.pushToBack(cursorDts + cts);
                idx.duration.pushToBack(dur);
                // sample_is_non_sync_sample is bit 16 of the sample_flags field.
                idx.keyframe.pushToBack(((sfl >> 16) & 1) == 0 ? 1 : 0);

                curOffset += sz;
                cursorDts += dur;
        }
        prevDataEnd = curOffset;
        (void)trunPayloadEnd;
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// XMP packet parsing — minimal extractor for Adobe bext: namespace fields
// ---------------------------------------------------------------------------

// Unescapes the XML entities we emit ourselves in the writer — &amp;,
// &lt;, &gt; — plus &quot; and &apos; as a courtesy for third-party
// XMP that might use them.  Numeric character references (&#x...)
// are not expanded; none of the bext fields we care about use them
// in practice.
static String unescapeXmlEntities(const String &in) {
        return in
                .replace(String("&lt;"),   String("<"))
                .replace(String("&gt;"),   String(">"))
                .replace(String("&quot;"), String("\""))
                .replace(String("&apos;"), String("'"))
                .replace(String("&amp;"),  String("&"));  // must be last
}

// Extracts the text content of the first <bext:localName ...> element
// found in @p xmp.  Returns an empty String if the element is absent,
// self-closing, or malformed.  This is deliberately minimal — it only
// needs to handle XMP packets that libpromeki itself writes, plus the
// conventional form used by other media tools.  Attributes and
// whitespace on the opening tag are tolerated.
static String extractBextElement(const String &xmp, const char *localName) {
        String openPrefix = String("<bext:") + localName;
        size_t start = xmp.find(openPrefix);
        if(start == String::npos) return String();

        // The character immediately after the local name must be one of
        // `>`, `/`, or whitespace — otherwise we matched a prefix of
        // something longer (e.g. <bext:umidX>).
        size_t afterName = start + openPrefix.size();
        if(afterName >= xmp.size()) return String();
        char next = static_cast<char>(xmp.charAt(afterName).codepoint());
        if(next != '>' && next != '/' && next != ' ' && next != '\t' &&
           next != '\r' && next != '\n') {
                return String();
        }

        // Walk forward to the end of the opening tag.
        size_t openEnd = xmp.find('>', afterName);
        if(openEnd == String::npos) return String();
        // Self-closing tag? Content is empty.
        if(openEnd > 0 && xmp.charAt(openEnd - 1) == '/') return String();

        size_t contentStart = openEnd + 1;
        String closeMarker = String("</bext:") + localName + ">";
        size_t closeStart = xmp.find(closeMarker, contentStart);
        if(closeStart == String::npos) return String();

        String raw = xmp.mid(contentStart, closeStart - contentStart).trim();
        return unescapeXmlEntities(raw);
}

// ---------------------------------------------------------------------------
// udta: container-level metadata (simple subset)
// ---------------------------------------------------------------------------

Error QuickTimeReader::parseUdta(int64_t payloadOffset, int64_t payloadEnd) {
        IODevice *dev = activeDevice();
        ReadStream stream(dev);
        Error err = stream.seek(payloadOffset);
        if(err.isError()) return err;

        // Iterate child atoms. Recognized ©-prefixed atoms map to Metadata IDs.
        while(true) {
                Box child;
                err = readBoxHeader(stream, child, payloadEnd);
                if(err == Error::EndOfFile) break;
                if(err.isError()) return err;

                uint32_t type = child.type.value();
                // QuickTime metadata atoms start with 0xA9 ('©') then three ASCII.
                if((type >> 24) == 0xA9 && child.payloadSize >= 4) {
                        // Legacy ©nnn layout: [u16 size][u16 language][char[size]]
                        uint16_t textLen = stream.readU16();
                        stream.skip(2); // language
                        if(textLen > 0 && textLen <= child.payloadSize - 4) {
                                char buf[512] = {};
                                int toRead = textLen < sizeof(buf) ? textLen : sizeof(buf) - 1;
                                if(stream.readBytes(buf, toRead).isError()) {
                                        advanceToSibling(stream, child);
                                        continue;
                                }
                                String value(buf, static_cast<size_t>(toRead));
                                char c1 = static_cast<char>((type >> 16) & 0xff);
                                char c2 = static_cast<char>((type >>  8) & 0xff);
                                char c3 = static_cast<char>((type >>  0) & 0xff);
                                // Map to known Metadata IDs.
                                if(c1 == 'n' && c2 == 'a' && c3 == 'm') {
                                        _containerMetadata.set(Metadata::Title, value);
                                } else if(c1 == 'c' && c2 == 'm' && c3 == 't') {
                                        _containerMetadata.set(Metadata::Comment, value);
                                } else if(c1 == 'd' && c2 == 'a' && c3 == 'y') {
                                        _containerMetadata.set(Metadata::Date, value);
                                } else if(c1 == 'a' && c2 == 'r' && c3 == 't') {
                                        _containerMetadata.set(Metadata::Artist, value);
                                } else if(c1 == 'A' && c2 == 'R' && c3 == 'T') {
                                        _containerMetadata.set(Metadata::Artist, value);
                                } else if(c1 == 'c' && c2 == 'p' && c3 == 'y') {
                                        _containerMetadata.set(Metadata::Copyright, value);
                                } else if(c1 == 't' && c2 == 'o' && c3 == 'o') {
                                        _containerMetadata.set(Metadata::Software, value);
                                } else if(c1 == 'a' && c2 == 'l' && c3 == 'b') {
                                        _containerMetadata.set(Metadata::Album, value);
                                } else if(c1 == 'g' && c2 == 'e' && c3 == 'n') {
                                        _containerMetadata.set(Metadata::Genre, value);
                                } else if(c1 == 'd' && c2 == 'e' && c3 == 's') {
                                        _containerMetadata.set(Metadata::Description, value);
                                }
                        }
                } else if(child.type == FourCC('X', 'M', 'P', '_')) {
                        // Adobe XMP packet — parse for BWF bext: fields.
                        // Refuse absurdly large packets to avoid wild allocations.
                        constexpr int64_t kMaxXmpSize = 1024 * 1024;  // 1 MiB
                        if(child.payloadSize > 0 && child.payloadSize <= kMaxXmpSize) {
                                List<char> buf(static_cast<size_t>(child.payloadSize));
                                if(stream.readBytes(buf.data(), static_cast<int>(child.payloadSize)).isOk()) {
                                        String xmp(buf.data(), static_cast<size_t>(child.payloadSize));
                                        String originator  = extractBextElement(xmp, "originator");
                                        String origRef     = extractBextElement(xmp, "originatorReference");
                                        String origDate    = extractBextElement(xmp, "originationDate");
                                        String origTime    = extractBextElement(xmp, "originationTime");
                                        String umidHex     = extractBextElement(xmp, "umid");

                                        if(!originator.isEmpty()) {
                                                _containerMetadata.set(Metadata::Originator, originator);
                                        }
                                        if(!origRef.isEmpty()) {
                                                _containerMetadata.set(Metadata::OriginatorReference, origRef);
                                        }
                                        if(!origDate.isEmpty() || !origTime.isEmpty()) {
                                                // Recombine date + time into the
                                                // libpromeki ISO-8601 format.
                                                String combined = origDate;
                                                if(!origTime.isEmpty()) {
                                                        if(!combined.isEmpty()) combined += "T";
                                                        combined += origTime;
                                                }
                                                _containerMetadata.set(Metadata::OriginationDateTime, combined);
                                        }
                                        if(!umidHex.isEmpty()) {
                                                Error umidErr;
                                                UMID umid = UMID::fromString(umidHex, &umidErr);
                                                if(umidErr.isOk() && umid.isValid()) {
                                                        _containerMetadata.set(Metadata::UMID, umid);
                                                } else {
                                                        // Fall back to the raw hex so callers
                                                        // can still inspect it.
                                                        _containerMetadata.set(Metadata::UMID, umidHex);
                                                }
                                        }
                                }
                        }
                }

                err = advanceToSibling(stream, child);
                if(err.isError()) return err;
        }
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Build MediaDesc from track list
// ---------------------------------------------------------------------------

void QuickTimeReader::buildMediaDesc() {
        MediaDesc md;
        bool rateSet = false;

        for(const QuickTime::Track &tk : _tracks) {
                if(tk.type() == QuickTime::Video && tk.pixelDesc().isValid()) {
                        ImageDesc idesc(tk.size(), tk.pixelDesc());
                        md.imageList().pushToBack(idesc);
                        if(!rateSet && tk.frameRate().isValid()) {
                                md.setFrameRate(tk.frameRate());
                                rateSet = true;
                        }
                } else if(tk.type() == QuickTime::Audio && tk.audioDesc().isValid()) {
                        md.audioList().pushToBack(tk.audioDesc());
                }
        }
        md.metadata() = _containerMetadata;

        if(!rateSet) {
                // Audio-only: leave the frame rate defaulted. The
                // MediaIOTask layer is expected to supply a synthetic
                // rate for audio-only tracks (Phase 5).
        }

        _mediaDesc = md;
}

PROMEKI_NAMESPACE_END
