/**
 * @file      quicktime_writer.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include "quicktime_writer.h"
#include "quicktime_atom.h"

#include <promeki/file.h>
#include <promeki/h264bitstream.h>
#include <promeki/hevcbitstream.h>
#include <promeki/iodevice.h>
#include <promeki/logger.h>

#include <cstring>
#include <ctime>

PROMEKI_NAMESPACE_BEGIN

using namespace quicktime_atom;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

/** @brief Mac-epoch (1904-01-01) seconds for the current wall-clock time. */
uint64_t macEpochNow() {
        const uint64_t kEpochDelta = 2082844800ull;
        std::time_t now = std::time(nullptr);
        return static_cast<uint64_t>(now) + kEpochDelta;
}

/** @brief Returns the QuickTime FourCC to put in the @c stsd entry for @p pd. */
FourCC quickTimeCodecFourCC(const PixelDesc &pd) {
        if(pd.fourccList().isEmpty()) return FourCC('\0', '\0', '\0', '\0');
        return pd.fourccList()[0];
}

/**
 * @brief Returns the QuickTime FourCC for a PCM audio data type.
 *
 * @fixme The generic @c lpcm fallback is a silent data-format trap.
 *        QuickTime @c lpcm sample entries require a @c pcmC extension
 *        atom describing endianness + sample-is-float flags, which
 *        this writer does not currently emit. A file written with
 *        @c lpcm + missing @c pcmC is decoded by most players as
 *        big-endian signed integer of the declared bit depth — which
 *        is wrong for float or little-endian inputs. Callers that
 *        pass formats without a canonical FourCC should convert to a
 *        mapped type first (see @c pickStorageFormat() in
 *        mediaiotask_quicktime.cpp). See devplan/fixme.md entry
 *        "QuickTime: Little-Endian Float Audio Storage".
 */
FourCC pcmFourCCForDataType(AudioDesc::DataType dt) {
        switch(dt) {
                case AudioDesc::PCMI_S16LE:    return FourCC("sowt");
                case AudioDesc::PCMI_S16BE:    return FourCC("twos");
                case AudioDesc::PCMI_S24BE:    return FourCC("in24");
                case AudioDesc::PCMI_S32BE:    return FourCC("in32");
                case AudioDesc::PCMI_Float32BE: return FourCC("fl32");
                case AudioDesc::PCMI_U8:       return FourCC("raw ");
                default:
                        // FIXME: lpcm fallback needs a pcmC extension atom to
                        // carry endianness / float flag. Without it, players
                        // interpret samples as big-endian integer.
                        return FourCC("lpcm");
        }
}

/** @brief Identity 3x3 transformation matrix used in tkhd / mvhd. */
void writeIdentityMatrix(AtomWriter &w) {
        // 16.16 16.16 2.30 ... QuickTime stores 9 ints; the convention is:
        //   [ a b u ]
        //   [ c d v ]
        //   [ x y w ]
        // Identity = a=d=1.0 (16.16), w=1.0 (2.30), others=0.
        w.writeU32(0x00010000); w.writeU32(0); w.writeU32(0);
        w.writeU32(0); w.writeU32(0x00010000); w.writeU32(0);
        w.writeU32(0); w.writeU32(0); w.writeU32(0x40000000);
}

/** @brief Pack a 3-letter ISO 639-2/T language code into 16 bits. */
uint16_t packLanguage(const String &lang) {
        if(lang.size() != 3) return (('u' - 0x60) << 10) | (('n' - 0x60) << 5) | ('d' - 0x60);
        const char *c = lang.cstr();
        return static_cast<uint16_t>(((c[0] - 0x60) & 0x1f) << 10 |
                                     ((c[1] - 0x60) & 0x1f) <<  5 |
                                     ((c[2] - 0x60) & 0x1f));
}

/** @brief Greatest common divisor for the run-length packers. */
uint64_t gcd64(uint64_t a, uint64_t b) {
        while(b != 0) { uint64_t t = b; b = a % b; a = t; }
        return a > 0 ? a : 1;
}

/**
 * @brief Emits the @c stsd (sample description) box for one track.
 *
 * Shared between the classic-layout @c appendTrak path and the
 * fragmented-layout @c appendInitTrak path, which previously carried
 * byte-identical inline copies of this logic. Factoring here gives
 * codec-specific extension atoms (avcC, hvcC, esds, ...) a single
 * insertion point inside the visual / audio sample entry.
 *
 * The box layout is:
 * @code
 *   stsd (full box)
 *     entry_count = 1
 *     <Video|Audio|TimecodeTrack|"dat "> sample entry
 *       ...codec-agnostic fields...
 *       [codec-specific child boxes]   <-- Phase 3 insertion point
 * @endcode
 */
/**
 * @brief Choose the visual sample entry FourCC for a track.
 *
 * For H.264 and HEVC this is @c avc1 / @c hvc1 — the canonical form
 * recognized by essentially every player.  Those sample entries
 * require parameter sets (SPS / PPS for H.264; VPS / SPS / PPS for
 * HEVC) to live only in the @c avcC / @c hvcC configuration record,
 * which matches the way this writer emits samples: parameter-set
 * NALs are stripped from each sample during the Annex-B → AVCC
 * conversion (see @ref QuickTimeWriter::writeSample) and the
 * configuration record is extracted from the first keyframe.
 *
 * For all other codecs the first entry in the @c PixelDesc
 * FourCC list is used.  See ISO/IEC 14496-15 §5.3.3.1.1 and
 * §8.3.3.1.1.
 */
FourCC quickTimeVideoSampleEntryFourCC(const PixelDesc &pd) {
        if(pd.id() == PixelDesc::H264) return FourCC("avc1");
        if(pd.id() == PixelDesc::HEVC) return FourCC("hvc1");
        return quickTimeCodecFourCC(pd);
}

/**
 * @brief Predicate that returns @c false for H.264 parameter-set NAL
 *        units (SPS type 7, PPS type 8) so they can be stripped out
 *        when emitting @c avc1 samples.
 */
bool keepNonH264ParameterSetNal(const H264Bitstream::NalUnit &nal) {
        uint8_t t = nal.header0 & 0x1f;
        return t != 7 && t != 8;
}

/**
 * @brief Predicate that returns @c false for HEVC parameter-set NAL
 *        units (VPS 32, SPS 33, PPS 34) so they can be stripped out
 *        when emitting @c hvc1 samples.
 */
bool keepNonHevcParameterSetNal(const H264Bitstream::NalUnit &nal) {
        uint8_t t = static_cast<uint8_t>((nal.header0 >> 1) & 0x3f);
        return t < 32 || t > 34;
}

void appendStsdBox(AtomWriter &w, const QuickTimeWriterTrack &t) {
        auto stsd = w.beginFullBox(kStsd, 0, 0);
        w.writeU32(1);                    // entry_count
        if(t.type == QuickTime::Video) {
                FourCC codec = quickTimeVideoSampleEntryFourCC(t.pixelDesc);
                auto vse = w.beginBox(codec);
                w.writeU32(0); w.writeU16(0);     // reserved[6]
                w.writeU16(1);                    // data_reference_index
                w.writeU16(0);                    // version
                w.writeU16(0);                    // revision
                w.writeFourCC(FourCC("    "));    // vendor
                w.writeU32(0);                    // temporal quality
                w.writeU32(0x00000200);           // spatial quality
                w.writeU16(static_cast<uint16_t>(t.size.width()));
                w.writeU16(static_cast<uint16_t>(t.size.height()));
                w.writeFixed16_16(72.0);          // horiz res
                w.writeFixed16_16(72.0);          // vert res
                w.writeU32(0);                    // data size
                w.writeU16(1);                    // frame count
                w.writePascalString(t.pixelDesc.name(), 32);
                w.writeU16(t.pixelDesc.hasAlpha() ? 32 : 24); // depth
                w.writeS16(-1);                   // pre-defined color table id
                // Codec-specific extension boxes (e.g. avcC / hvcC).
                // Live as child boxes of the visual sample entry, so
                // they must be written before vse is closed.
                if(t.codecConfigBox && t.codecConfigBox->size() > 0 &&
                   t.codecConfigType.value() != 0) {
                        auto cfgBox = w.beginBox(t.codecConfigType);
                        w.writeBytes(t.codecConfigBox->data(),
                                     t.codecConfigBox->size());
                        w.endBox(cfgBox);
                }
                w.endBox(vse);
        } else if(t.type == QuickTime::Audio) {
                FourCC codec = pcmFourCCForDataType(t.audioDesc.dataType());
                auto ase = w.beginBox(codec);
                w.writeU32(0); w.writeU16(0);     // reserved[6]
                w.writeU16(1);                    // data_reference_index
                w.writeU16(0);                    // version (v0)
                w.writeU16(0);                    // revision
                w.writeFourCC(FourCC("    "));    // vendor
                w.writeU16(static_cast<uint16_t>(t.audioDesc.channels()));
                w.writeU16(static_cast<uint16_t>(t.audioDesc.bytesPerSample() * 8));
                w.writeU16(0);                    // pre-defined / compression ID
                w.writeU16(0);                    // packet size
                // Sample rate as 16.16 fixed in the high 16 bits + 0 fraction.
                uint32_t srFixed = static_cast<uint32_t>(t.audioDesc.sampleRate()) << 16;
                w.writeU32(srFixed);
                w.endBox(ase);
        } else if(t.type == QuickTime::TimecodeTrack) {
                auto tcse = w.beginBox(FourCC("tmcd"));
                w.writeU32(0); w.writeU16(0);     // reserved[6]
                w.writeU16(1);                    // data_reference_index
                w.writeU32(0);                    // reserved
                w.writeU32(t.tcFlags);
                w.writeU32(t.timescale);
                w.writeU32(t.frameRate.rational().denominator());
                w.writeU8(t.tcFrameCount);
                w.writeU8(0);                     // reserved
                w.endBox(tcse);
        } else {
                auto datBox = w.beginBox(FourCC("dat "));
                w.writeU32(0); w.writeU16(0);     // reserved[6]
                w.writeU16(1);                    // data_reference_index
                w.endBox(datBox);
        }
        w.endBox(stsd);
}

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

QuickTimeWriter::QuickTimeWriter() : QuickTime::Impl(QuickTime::Writer) {}

QuickTimeWriter::~QuickTimeWriter() {
        if(_isOpen) close();
        delete _file;
        _file = nullptr;
}

uint32_t QuickTimeWriter::allocateTrackId() {
        return _nextTrackId++;
}

Error QuickTimeWriter::setLayout(QuickTime::Layout layout) {
        if(_isOpen) return Error::AlreadyOpen;
        _layout = layout;
        return Error::Ok;
}

Error QuickTimeWriter::open() {
        if(_isOpen) return Error::AlreadyOpen;
        if(_filename.isEmpty() && _device == nullptr) return Error::InvalidArgument;
        if(_device != nullptr) {
                // IODevice-based writers are reserved for a later phase.
                return Error::NotSupported;
        }

        if(_file == nullptr) _file = new File(_filename);
        else                 _file->setFilename(_filename);

        Error err = _file->open(IODevice::WriteOnly,
                                File::Create | File::Truncate);
        if(err.isError()) {
                promekiWarn("QuickTime writer: open '%s': %s",
                            _filename.cstr(), err.name().cstr());
                return err;
        }

        // 1. ftyp (common to both layouts). For fragmented files the brand
        // list advertises compatibility with ISO-BMFF fragmented media.
        AtomWriter ftyp;
        auto ftypBox = ftyp.beginBox(kFtyp);
        ftyp.writeFourCC(FourCC("qt  "));   // major brand
        ftyp.writeU32(0x00000200);          // minor version
        ftyp.writeFourCC(FourCC("qt  "));   // compatible: qt
        if(_layout == QuickTime::LayoutFragmented) {
                ftyp.writeFourCC(FourCC("isom"));  // compatible: ISO-BMFF
                ftyp.writeFourCC(FourCC("iso5"));  // compatible: ISO-BMFF v5 (has fragmentation)
        }
        ftyp.endBox(ftypBox);

        const List<uint8_t> &ftypBytes = ftyp.data();
        int64_t n = _file->write(ftypBytes.data(), static_cast<int64_t>(ftypBytes.size()));
        if(n != static_cast<int64_t>(ftypBytes.size())) {
                _file->close();
                return Error::IOError;
        }

        if(_layout == QuickTime::LayoutClassic) {
                // 2. mdat box header (16-byte form: size=1 then 64-bit largesize).
                // We patch the largesize at finalize() once we know the payload length.
                _mdatHeaderOffset = _file->pos();
                AtomWriter mdat;
                mdat.writeU32(1);                         // size==1 → 64-bit largesize follows
                mdat.writeFourCC(kMdat);
                mdat.writeU64(16);                        // placeholder largesize
                const List<uint8_t> &mdatBytes = mdat.data();
                n = _file->write(mdatBytes.data(), static_cast<int64_t>(mdatBytes.size()));
                if(n != static_cast<int64_t>(mdatBytes.size())) {
                        _file->close();
                        return Error::IOError;
                }
                _mdatPayloadStart = _file->pos();
                _mdatCursor = _mdatPayloadStart;
        } else {
                // Fragmented: the init moov is written lazily on the first
                // writeSample, once tracks are known. Until then, the file
                // has only ftyp — any crash leaves a zero-sample valid file.
                _mdatHeaderOffset = 0;
                _mdatPayloadStart = _file->pos();
                _mdatCursor       = _mdatPayloadStart;
                _initMoovWritten  = false;
                _fragmentSequence = 1;
        }

        _isOpen = true;
        return Error::Ok;
}

void QuickTimeWriter::close() {
        if(_isOpen) finalize();
        if(_file != nullptr) {
                _file->close();
                delete _file;
                _file = nullptr;
        }
        _writeTracks.clear();
        _writerMetadata = Metadata();
        _isOpen = false;
        _mdatHeaderOffset = 0;
        _mdatPayloadStart = 0;
        _mdatCursor = 0;
        _nextTrackId = 1;
        _initMoovWritten = false;
        _fragmentSequence = 1;
}

// ---------------------------------------------------------------------------
// Track registration
// ---------------------------------------------------------------------------

Error QuickTimeWriter::addVideoTrack(const PixelDesc &codec, const Size2Du32 &size,
                                     const FrameRate &frameRate, uint32_t *outTrackId) {
        if(!_isOpen) return Error::NotOpen;
        if(!codec.isValid()) return Error::InvalidArgument;
        if(!frameRate.isValid()) return Error::NoFrameRate;

        QuickTimeWriterTrack t;
        t.id        = allocateTrackId();
        t.type      = QuickTime::Video;
        t.frameRate = frameRate;
        // Use the frame rate's numerator as the timescale and the denominator
        // as the per-sample duration. e.g. 24 fps → timescale 24, duration 1.
        // 23.976 (24000/1001) → timescale 24000, duration 1001.
        t.timescale = frameRate.rational().numerator();
        t.pixelDesc = codec;
        t.size      = size;
        _writeTracks.pushToBack(t);
        if(outTrackId != nullptr) *outTrackId = t.id;
        return Error::Ok;
}

Error QuickTimeWriter::addAudioTrack(const AudioDesc &desc, uint32_t *outTrackId) {
        if(!_isOpen) return Error::NotOpen;
        if(!desc.isValid()) return Error::InvalidArgument;

        QuickTimeWriterTrack t;
        t.id                = allocateTrackId();
        t.type              = QuickTime::Audio;
        t.timescale         = static_cast<uint32_t>(desc.sampleRate());
        t.audioDesc         = desc;
        t.pcmBytesPerSample = static_cast<uint32_t>(desc.bytesPerSampleStride());
        _writeTracks.pushToBack(t);
        if(outTrackId != nullptr) *outTrackId = t.id;
        return Error::Ok;
}

Error QuickTimeWriter::addTimecodeTrack(const Timecode &startTimecode,
                                        const FrameRate &frameRate, uint32_t *outTrackId) {
        if(!_isOpen) return Error::NotOpen;
        if(!startTimecode.isValid() || !frameRate.isValid()) return Error::InvalidArgument;

        QuickTimeWriterTrack t;
        t.id        = allocateTrackId();
        t.type      = QuickTime::TimecodeTrack;
        t.frameRate = frameRate;
        t.timescale = frameRate.rational().numerator();
        // Translate the SMPTE digits + mode into an absolute frame number.
        // We construct a fresh Timecode in the same mode and ask libvtc.
        Timecode tc = startTimecode;
        auto [fn, fnErr] = tc.toFrameNumber();
        if(fnErr.isError()) return fnErr;
        t.tcStartFrame = static_cast<uint32_t>(fn);
        // Drop-frame and frame-count flags from the mode.
        bool dropFrame = false;
        switch(t.timescale / 1) {
                default: break;
        }
        // Determine drop-frame status from the libvtc format directly.
        const VtcFormat *fmt = startTimecode.vtcFormat();
        if(fmt != nullptr && vtc_format_is_drop_frame(fmt)) dropFrame = true;
        t.tcFlags = dropFrame ? 0x01u : 0x00u;
        // Round nominal frame count from rate (24, 25, 30, ...).
        t.tcFrameCount = static_cast<uint8_t>((frameRate.rational().numerator() +
                                                frameRate.rational().denominator() - 1) /
                                               frameRate.rational().denominator());
        _writeTracks.pushToBack(t);
        if(outTrackId != nullptr) *outTrackId = t.id;
        return Error::Ok;
}

void QuickTimeWriter::setContainerMetadata(const Metadata &meta) {
        _writerMetadata = meta;
}

// ---------------------------------------------------------------------------
// Sample writes
// ---------------------------------------------------------------------------

Error QuickTimeWriter::writeSample(uint32_t trackId, const QuickTime::Sample &sample) {
        if(!_isOpen) return Error::NotOpen;
        if(!sample.data.isValid() || sample.data->size() == 0) return Error::InvalidArgument;

        // Find the track.
        size_t idx = SIZE_MAX;
        for(size_t i = 0; i < _writeTracks.size(); ++i) {
                if(_writeTracks[i].id == trackId) { idx = i; break; }
        }
        if(idx == SIZE_MAX) return Error::IdNotFound;
        QuickTimeWriterTrack &t = _writeTracks[idx];

        // H.264 / HEVC tracks: (1) extract the codec configuration
        // record from the first keyframe and (2) rewrite the Annex-B
        // access unit to length-prefixed (AVCC) form for storage in
        // mdat.  Both steps run before @c ensureInitMoovWritten() so
        // that the fragmented-layout init moov carries the populated
        // @c codecConfigBox in its stsd entry.
        Buffer::Ptr convertedPayload;
        const bool isH264 = (t.type == QuickTime::Video &&
                             t.pixelDesc.id() == PixelDesc::H264);
        const bool isHEVC = (t.type == QuickTime::Video &&
                             t.pixelDesc.id() == PixelDesc::HEVC);
        if(isH264 || isHEVC) {
                BufferView srcView(sample.data, 0, sample.data->size());
                if(!t.codecConfigBox && sample.keyframe) {
                        Buffer::Ptr cfgPayload;
                        FourCC      cfgType = isH264 ? FourCC("avcC") : FourCC("hvcC");
                        Error cfgErr;
                        if(isH264) {
                                AvcDecoderConfig cfg;
                                cfgErr = AvcDecoderConfig::fromAnnexB(srcView, cfg);
                                if(!cfgErr.isError()) cfgErr = cfg.serialize(cfgPayload);
                        } else {
                                HevcDecoderConfig cfg;
                                cfgErr = HevcDecoderConfig::fromAnnexB(srcView, cfg);
                                if(!cfgErr.isError()) cfgErr = cfg.serialize(cfgPayload);
                        }
                        if(cfgErr.isError()) {
                                // Without a config record the file will be
                                // unplayable — fail the write outright rather
                                // than silently emitting a broken track.
                                promekiErr("QuickTime: failed to build %s from first keyframe: %s",
                                           isH264 ? "avcC" : "hvcC", cfgErr.name().cstr());
                                return cfgErr;
                        }
                        t.codecConfigBox  = cfgPayload;
                        t.codecConfigType = cfgType;
                }
                // Parameter-set NALs (SPS / PPS for H.264; VPS / SPS /
                // PPS for HEVC) must NOT appear in the sample payload
                // for an @c avc1 / @c hvc1 sample entry — they live
                // solely in the @c avcC / @c hvcC configuration record
                // we just built above.  Filter them out as we convert
                // to AVCC.
                Error convErr = H264Bitstream::annexBToAvccFiltered(
                        srcView, 4,
                        isH264 ? keepNonH264ParameterSetNal : keepNonHevcParameterSetNal,
                        convertedPayload);
                if(convErr.isError()) {
                        promekiErr("QuickTime: Annex-B to AVCC conversion failed: %s",
                                   convErr.name().cstr());
                        return convErr;
                }
        }

        // In fragmented mode, the init moov must be written before we can
        // start producing fragments. Do it lazily the first time a sample
        // is written — now that addTrack* calls have registered all the
        // tracks we care about, and (for H.264/HEVC) the codec config
        // has been extracted from this same first sample above.
        if(_layout == QuickTime::LayoutFragmented) {
                Error mErr = ensureInitMoovWritten();
                if(mErr.isError()) return mErr;
        }

        // Determine the per-sample duration from the caller or the track's
        // frame rate. Used by both classic and fragmented paths.
        uint32_t duration = 0;
        if(sample.duration != 0) {
                duration = static_cast<uint32_t>(sample.duration);
        } else if(t.frameRate.isValid()) {
                duration = t.frameRate.rational().denominator();
        } else {
                duration = 1;
        }
        int32_t cts = static_cast<int32_t>(sample.pts - sample.dts);
        const uint8_t *bytes;
        int64_t        payloadSize;
        if(convertedPayload) {
                bytes       = static_cast<const uint8_t *>(convertedPayload->data());
                payloadSize = static_cast<int64_t>(convertedPayload->size());
        } else {
                bytes       = static_cast<const uint8_t *>(sample.data->data());
                payloadSize = static_cast<int64_t>(sample.data->size());
        }

        if(_layout == QuickTime::LayoutFragmented) {
                // Fragmented: append sample bytes to the track's
                // per-fragment payload buffer and record the per-sample
                // metadata. At flush() time each track's buffer is written
                // contiguously into the fragment's mdat, which is what
                // trun assumes (one consecutive run per trun).
                if(t.pcmBytesPerSample != 0 && t.type == QuickTime::Audio) {
                        if((payloadSize % t.pcmBytesPerSample) != 0) {
                                promekiWarn("QuickTime: audio write payload %lld not a multiple "
                                            "of bytes-per-sample %u",
                                            static_cast<long long>(payloadSize),
                                            t.pcmBytesPerSample);
                                return Error::InvalidArgument;
                        }
                }

                // Bulk-append payload bytes to the track's per-fragment
                // buffer (resize + memcpy — avoids per-byte pushToBack).
                size_t oldSize = t.fragPayload.size();
                t.fragPayload.resize(oldSize + static_cast<size_t>(payloadSize));
                std::memcpy(t.fragPayload.data() + oldSize, bytes,
                            static_cast<size_t>(payloadSize));

                if(t.fragSampleSizes.isEmpty()) {
                        t.fragBaseDts = t.fragRunningDts;
                }

                if(t.type == QuickTime::Audio) {
                        uint32_t samplesInChunk =
                                static_cast<uint32_t>(payloadSize / t.pcmBytesPerSample);
                        // Expand into per-PCM-frame samples so the trun's
                        // data_offset + sample_size run matches the
                        // canonical fMP4 PCM layout.
                        for(uint32_t i = 0; i < samplesInChunk; ++i) {
                                t.fragSampleSizes.pushToBack(t.pcmBytesPerSample);
                                t.fragSampleDurations.pushToBack(1);
                                t.fragSampleCtsOffsets.pushToBack(0);
                                t.fragSampleKeyframes.pushToBack(1);
                        }
                        t.fragRunningDts    += samplesInChunk;
                        t.totalAudioSamples += samplesInChunk;
                        t.totalDuration     += samplesInChunk;
                } else {
                        t.fragSampleSizes.pushToBack(static_cast<uint32_t>(payloadSize));
                        t.fragSampleDurations.pushToBack(duration);
                        t.fragSampleCtsOffsets.pushToBack(cts);
                        t.fragSampleKeyframes.pushToBack(sample.keyframe ? 1 : 0);
                        t.fragRunningDts += duration;
                        t.totalDuration  += duration;
                }
                return Error::Ok;
        }

        // ---- Classic layout paths ----
        // Append the payload to disk at the current cursor. Use writeBulk
        // so large sample payloads (video frames) get the DIO fast path —
        // sustained captures avoid page-cache thrash. For small samples
        // (audio chunks, tmcd samples) writeBulk falls back to normal I/O
        // automatically.
        Error err = _file->seek(_mdatCursor);
        if(err.isError()) return err;
        int64_t n = _file->writeBulk(bytes, payloadSize);
        if(n != payloadSize) return Error::IOError;
        int64_t chunkOffset = _mdatCursor;
        _mdatCursor += payloadSize;
        if(t.type == QuickTime::Audio) {
                // Audio: one writeSample call = one chunk of N PCM sample
                // frames. The canonical QuickTime PCM layout has stsz
                // sample_size = bytesPerSample (constant), stsc grouping
                // chunks with variable samples_per_chunk, and stts with
                // a single {count=total, delta=1} entry. All that is
                // emitted from the chunk-based bookkeeping below during
                // finalize().
                if(t.pcmBytesPerSample == 0 || (payloadSize % t.pcmBytesPerSample) != 0) {
                        promekiWarn("QuickTime: audio write payload %lld not a multiple of "
                                    "bytes-per-sample %u", static_cast<long long>(payloadSize),
                                    t.pcmBytesPerSample);
                        return Error::InvalidArgument;
                }
                uint32_t samplesInChunk =
                        static_cast<uint32_t>(payloadSize / t.pcmBytesPerSample);
                t.audioChunkOffsets.pushToBack(chunkOffset);
                t.audioChunkSampleCounts.pushToBack(samplesInChunk);
                t.totalAudioSamples += samplesInChunk;
                t.totalDuration     += samplesInChunk;  // mdhd timescale = sample rate
                return Error::Ok;
        }

        // Video / timecode / generic: per-sample table.
        t.offsets.pushToBack(chunkOffset);
        t.sizes.pushToBack(static_cast<uint32_t>(payloadSize));
        t.durations.pushToBack(duration);
        t.ctsOffsets.pushToBack(cts);
        t.keyframes.pushToBack(sample.keyframe ? 1 : 0);
        t.totalDuration += duration;
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Finalize
// ---------------------------------------------------------------------------

Error QuickTimeWriter::patchMdatSize() {
        // The mdat box uses the 64-bit largesize form (size==1).
        // Layout: [4B size=1][4B 'mdat'][8B largesize]
        int64_t mdatTotal = _mdatCursor - _mdatHeaderOffset;
        if(mdatTotal < 16) return Error::CorruptData;

        Error err = _file->seek(_mdatHeaderOffset + 8);
        if(err.isError()) return err;
        uint8_t buf[8];
        uint64_t v = static_cast<uint64_t>(mdatTotal);
        for(int i = 0; i < 8; ++i) {
                buf[i] = static_cast<uint8_t>((v >> ((7 - i) * 8)) & 0xff);
        }
        int64_t n = _file->write(buf, 8);
        if(n != 8) return Error::IOError;
        return Error::Ok;
}

Error QuickTimeWriter::finalize() {
        if(!_isOpen) return Error::Ok;
        if(_file == nullptr) return Error::NotOpen;

        if(_layout == QuickTime::LayoutFragmented) {
                // Flush any remaining pending samples as a final fragment.
                if(hasPendingFragmentSamples()) {
                        Error fe = flush();
                        if(fe.isError()) return fe;
                }
                // No post-moov work needed — the init moov was written at
                // the first writeSample, and every complete fragment is
                // already on disk. The file is playable at every flush
                // boundary.
                _isOpen = false;
                return Error::Ok;
        }

        // Classic layout finalize: patch the mdat header and emit moov.
        Error err = patchMdatSize();
        if(err.isError()) return err;
        err = _file->seek(_mdatCursor);
        if(err.isError()) return err;
        err = writeMoov();
        if(err.isError()) return err;

        _isOpen = false;
        return Error::Ok;
}

Error QuickTimeWriter::flush() {
        if(!_isOpen) return Error::NotOpen;
        if(_layout != QuickTime::LayoutFragmented) return Error::Ok;
        if(!hasPendingFragmentSamples()) return Error::Ok;

        // In case this is the first sample in a brand-new writer session,
        // make sure the init moov has been emitted.
        Error err = ensureInitMoovWritten();
        if(err.isError()) return err;

        err = writeFragment();
        if(err.isError()) return err;

        // Optional durable-checkpoint mode: after each fragment is
        // fully written to disk, force fdatasync so the fragment is
        // guaranteed to survive a crash. Disabled by default because
        // it stalls the writer while the kernel flushes.
        if(_flushSync && _file != nullptr) {
                Error syncErr = _file->sync(/*dataOnly=*/true);
                if(syncErr.isError()) {
                        promekiWarn("QuickTime writer: flushSync failed: %s",
                                    syncErr.name().cstr());
                        return syncErr;
                }
        }
        return Error::Ok;
}

bool QuickTimeWriter::hasPendingFragmentSamples() const {
        for(const QuickTimeWriterTrack &t : _writeTracks) {
                if(!t.fragSampleSizes.isEmpty()) return true;
        }
        return false;
}

// ---------------------------------------------------------------------------
// moov / trak builders
// ---------------------------------------------------------------------------

Error QuickTimeWriter::writeMoov() {
        AtomWriter w;

        auto moov = w.beginBox(kMoov);

        // ---- mvhd ----
        // Movie duration is the longest track duration converted to the movie timescale.
        uint32_t movieTimescale = _movieTimescale;
        uint64_t movieDuration = 0;
        for(const QuickTimeWriterTrack &t : _writeTracks) {
                if(t.timescale == 0) continue;
                uint64_t durMovie = (t.totalDuration * static_cast<uint64_t>(movieTimescale)) /
                                    static_cast<uint64_t>(t.timescale);
                if(durMovie > movieDuration) movieDuration = durMovie;
        }

        uint64_t now = macEpochNow();
        auto mvhd = w.beginFullBox(kMvhd, 0, 0);
        w.writeU32(static_cast<uint32_t>(now));   // creation_time
        w.writeU32(static_cast<uint32_t>(now));   // modification_time
        w.writeU32(movieTimescale);
        w.writeU32(static_cast<uint32_t>(movieDuration));
        w.writeFixed16_16(1.0);                    // rate = 1.0
        w.writeFixed8_8(1.0);                      // volume = 1.0
        w.writeU16(0);                             // reserved
        w.writeU32(0); w.writeU32(0);              // reserved[2]
        writeIdentityMatrix(w);
        w.writeU32(0); w.writeU32(0); w.writeU32(0); w.writeU32(0); w.writeU32(0); w.writeU32(0); // pre_defined[6]
        w.writeU32(_nextTrackId);                  // next_track_id
        w.endBox(mvhd);

        // ---- trak per track ----
        for(const QuickTimeWriterTrack &t : _writeTracks) {
                appendTrak(w, t, movieTimescale);
        }

        // ---- udta (optional) ----
        // Container-level text metadata is emitted as classic
        // ©-prefixed atoms inside a udta box.  appendUdta() is a
        // no-op when no recognized fields are present.
        appendUdta(w);

        w.endBox(moov);

        const List<uint8_t> &bytes = w.data();
        int64_t n = _file->write(bytes.data(), static_cast<int64_t>(bytes.size()));
        if(n != static_cast<int64_t>(bytes.size())) return Error::IOError;
        return Error::Ok;
}

// XML-escapes @p in for embedding as element content in an XMP packet.
// Only the minimum set required by XML text content: &, <, >.
static String xmlEscape(const String &in) {
        String out;
        const char *s = in.cstr();
        size_t n = in.size();
        for(size_t i = 0; i < n; ++i) {
                char c = s[i];
                switch(c) {
                        case '&': out += "&amp;";  break;
                        case '<': out += "&lt;";   break;
                        case '>': out += "&gt;";   break;
                        default:  out += c;        break;
                }
        }
        return out;
}

// Builds a minimal XMP packet describing the supplied bext: fields.
// Returns an empty String if no bext fields are present, signalling
// that no XMP_ box should be emitted.  The packet uses the standard
// Adobe BWF bext namespace (http://ns.adobe.com/bwf/bext/1.0/) so
// third-party tools (Adobe Bridge, MXF readers) can parse it.
static String buildBextXmpPacket(const Metadata &meta) {
        // Collect the fields we care about.  OriginationDateTime is
        // stored in libpromeki as a single ISO-8601 "YYYY-MM-DDTHH:MM:SS"
        // string, but the bext XMP schema splits it into separate
        // originationDate and originationTime elements — split here
        // on the 'T' separator if present.
        String originator;
        String originatorRef;
        String originationDate;
        String originationTime;
        String umid;
        bool any = false;

        if(meta.contains(Metadata::Originator)) {
                originator = meta.get(Metadata::Originator).get<String>();
                if(!originator.isEmpty()) any = true;
        }
        if(meta.contains(Metadata::OriginatorReference)) {
                originatorRef = meta.get(Metadata::OriginatorReference).get<String>();
                if(!originatorRef.isEmpty()) any = true;
        }
        if(meta.contains(Metadata::OriginationDateTime)) {
                String dt = meta.get(Metadata::OriginationDateTime).get<String>();
                size_t tpos = dt.find('T');
                if(tpos != String::npos) {
                        originationDate = dt.mid(0, tpos);
                        originationTime = dt.mid(tpos + 1);
                } else {
                        // Fallback: treat the whole thing as a date.
                        originationDate = dt;
                }
                if(!originationDate.isEmpty() || !originationTime.isEmpty()) any = true;
        }
        if(meta.contains(Metadata::UMID)) {
                umid = meta.get(Metadata::UMID).get<String>();
                if(!umid.isEmpty()) any = true;
        }

        if(!any) return String();

        // Standard XMP header — the magic GUID is part of the Adobe
        // XMP spec for locating the packet inside a host file.
        String out;
        out += "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n";
        out += "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"libpromeki\">\n";
        out += " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n";
        out += "  <rdf:Description rdf:about=\"\"\n";
        out += "   xmlns:bext=\"http://ns.adobe.com/bwf/bext/1.0/\">\n";
        if(!originator.isEmpty()) {
                out += "   <bext:originator>";
                out += xmlEscape(originator);
                out += "</bext:originator>\n";
        }
        if(!originatorRef.isEmpty()) {
                out += "   <bext:originatorReference>";
                out += xmlEscape(originatorRef);
                out += "</bext:originatorReference>\n";
        }
        if(!originationDate.isEmpty()) {
                out += "   <bext:originationDate>";
                out += xmlEscape(originationDate);
                out += "</bext:originationDate>\n";
        }
        if(!originationTime.isEmpty()) {
                out += "   <bext:originationTime>";
                out += xmlEscape(originationTime);
                out += "</bext:originationTime>\n";
        }
        if(!umid.isEmpty()) {
                out += "   <bext:umid>";
                out += xmlEscape(umid);
                out += "</bext:umid>\n";
        }
        out += "  </rdf:Description>\n";
        out += " </rdf:RDF>\n";
        out += "</x:xmpmeta>\n";
        out += "<?xpacket end=\"w\"?>";
        return out;
}

void QuickTimeWriter::appendUdta(AtomWriter &w) {
        // Maps a Metadata::ID to a classic ©-prefixed four-byte atom
        // type.  The first byte of each atom type is 0xA9 ('©');
        // `c1`/`c2`/`c3` supply the three ASCII bytes that follow.
        struct AtomEntry { char c1; char c2; char c3; String text; };
        List<AtomEntry> entries;

        auto addIfPresent = [&](Metadata::ID id, char c1, char c2, char c3) {
                if(!_writerMetadata.contains(id)) return;
                String text = _writerMetadata.get(id).get<String>();
                if(text.isEmpty()) return;
                entries.pushToBack(AtomEntry{c1, c2, c3, std::move(text)});
        };

        // Standard iTunes / QuickTime ©-atoms.  Software is mapped to
        // ©too (encoder tool) to match the reader's parsing at
        // quicktime_reader.cpp:parseUdta.
        addIfPresent(Metadata::Title,       'n', 'a', 'm');
        addIfPresent(Metadata::Comment,     'c', 'm', 't');
        addIfPresent(Metadata::Date,        'd', 'a', 'y');
        addIfPresent(Metadata::Artist,      'A', 'R', 'T');
        addIfPresent(Metadata::Copyright,   'c', 'p', 'y');
        addIfPresent(Metadata::Software,    't', 'o', 'o');
        addIfPresent(Metadata::Album,       'a', 'l', 'b');
        addIfPresent(Metadata::Genre,       'g', 'e', 'n');
        addIfPresent(Metadata::Description, 'd', 'e', 's');

        // BWF-ish libpromeki metadata (Originator / OriginatorReference
        // / OriginationDateTime / UMID) is emitted as an XMP packet
        // using the Adobe bext namespace.  XMP is the standard way to
        // carry these fields inside a QuickTime container and is
        // preserved by common media tools.
        String xmpPacket = buildBextXmpPacket(_writerMetadata);

        if(entries.isEmpty() && xmpPacket.isEmpty()) return;

        auto udta = w.beginBox(kUdta);
        for(size_t i = 0; i < entries.size(); ++i) {
                const AtomEntry &e = entries[i];
                FourCC atomType(static_cast<char>(0xA9), e.c1, e.c2, e.c3);
                auto atom = w.beginBox(atomType);
                size_t len = e.text.size();
                if(len > 0xFFFF) len = 0xFFFF;  // Pascal-style 16-bit length cap.
                w.writeU16(static_cast<uint16_t>(len));
                w.writeU16(0);   // language code — 0 = unspecified.
                if(len > 0) w.writeBytes(e.text.cstr(), len);
                w.endBox(atom);
        }
        if(!xmpPacket.isEmpty()) {
                // XMP_ box: raw XMP packet bytes, no length prefix or
                // language code.  Four-byte type is 'X','M','P','_'.
                auto xmp = w.beginBox(FourCC('X', 'M', 'P', '_'));
                w.writeBytes(xmpPacket.cstr(), xmpPacket.size());
                w.endBox(xmp);
        }
        w.endBox(udta);
}

void QuickTimeWriter::appendTrak(AtomWriter &w, const QuickTimeWriterTrack &t,
                                 uint32_t movieTimescale) {
        auto trak = w.beginBox(kTrak);

        // ---- tkhd ----
        // Flags: track_enabled = 0x000001, track_in_movie = 0x000002, track_in_preview = 0x000004
        const uint32_t tkhdFlags = 0x000007;
        uint64_t now = macEpochNow();
        uint64_t durMovie = (t.timescale > 0)
                ? (t.totalDuration * static_cast<uint64_t>(movieTimescale)) /
                  static_cast<uint64_t>(t.timescale)
                : 0;

        auto tkhd = w.beginFullBox(kTkhd, 0, tkhdFlags);
        w.writeU32(static_cast<uint32_t>(now));     // creation
        w.writeU32(static_cast<uint32_t>(now));     // modification
        w.writeU32(t.id);                           // track_id
        w.writeU32(0);                              // reserved
        w.writeU32(static_cast<uint32_t>(durMovie));// duration (movie timescale)
        w.writeU32(0); w.writeU32(0);               // reserved[2]
        w.writeU16(0);                              // layer
        w.writeU16(0);                              // alternate_group
        // Volume: 1.0 for audio, 0 for video/timecode.
        w.writeFixed8_8(t.type == QuickTime::Audio ? 1.0 : 0.0);
        w.writeU16(0);                              // reserved
        writeIdentityMatrix(w);
        if(t.type == QuickTime::Video) {
                w.writeFixed16_16(static_cast<double>(t.size.width()));
                w.writeFixed16_16(static_cast<double>(t.size.height()));
        } else {
                w.writeFixed16_16(0.0);
                w.writeFixed16_16(0.0);
        }
        w.endBox(tkhd);

        // ---- mdia ----
        auto mdia = w.beginBox(kMdia);

        // mdhd
        auto mdhd = w.beginFullBox(kMdhd, 0, 0);
        w.writeU32(static_cast<uint32_t>(now));     // creation
        w.writeU32(static_cast<uint32_t>(now));     // modification
        w.writeU32(t.timescale);
        w.writeU32(static_cast<uint32_t>(t.totalDuration));
        w.writeU16(packLanguage(String("und")));
        w.writeU16(0);                              // pre_defined
        w.endBox(mdhd);

        // hdlr
        auto hdlr = w.beginFullBox(kHdlr, 0, 0);
        w.writeU32(0);                              // pre_defined
        switch(t.type) {
                case QuickTime::Video:         w.writeFourCC(kHdlrVide); break;
                case QuickTime::Audio:         w.writeFourCC(kHdlrSoun); break;
                case QuickTime::TimecodeTrack: w.writeFourCC(kHdlrTmcd); break;
                default:                       w.writeFourCC(FourCC("data")); break;
        }
        w.writeU32(0); w.writeU32(0); w.writeU32(0); // reserved[3]
        // null-terminated handler name
        const char *hname =
                t.type == QuickTime::Video         ? "VideoHandler" :
                t.type == QuickTime::Audio         ? "SoundHandler" :
                t.type == QuickTime::TimecodeTrack ? "TimecodeHandler" :
                                                     "DataHandler";
        w.writeBytes(hname, std::strlen(hname) + 1);
        w.endBox(hdlr);

        // minf
        auto minf = w.beginBox(kMinf);

        // vmhd / smhd / gmhd
        if(t.type == QuickTime::Video) {
                auto vmhd = w.beginFullBox(FourCC("vmhd"), 0, 0x000001);
                w.writeU16(0);            // graphics_mode (copy)
                w.writeU16(0); w.writeU16(0); w.writeU16(0); // opcolor[3]
                w.endBox(vmhd);
        } else if(t.type == QuickTime::Audio) {
                auto smhd = w.beginFullBox(FourCC("smhd"), 0, 0);
                w.writeU16(0);            // balance (8.8 fixed)
                w.writeU16(0);            // reserved
                w.endBox(smhd);
        } else {
                // gmhd: a generic media header for timecode / data tracks.
                auto gmhd = w.beginBox(FourCC("gmhd"));
                auto gmin = w.beginFullBox(FourCC("gmin"), 0, 0);
                w.writeU16(0x0040);       // graphics_mode = 64 (copy)
                w.writeU16(0x8000); w.writeU16(0x8000); w.writeU16(0x8000); // opcolor
                w.writeU16(0);            // balance
                w.writeU16(0);            // reserved
                w.endBox(gmin);
                if(t.type == QuickTime::TimecodeTrack) {
                        // tmcd container with a 'tcmi' (text/info) atom.
                        auto tmcd = w.beginBox(FourCC("tmcd"));
                        auto tcmi = w.beginFullBox(FourCC("tcmi"), 0, 0);
                        w.writeU16(0);     // text_font
                        w.writeU16(0);     // text_face
                        w.writeU16(0x0c);  // text_size = 12
                        w.writeU16(0);     // text_color reserved
                        w.writeU16(0xffff); w.writeU16(0xffff); w.writeU16(0xffff); // text_color
                        w.writeU16(0); w.writeU16(0); w.writeU16(0);                 // background
                        w.writeU8(0);     // font_name length
                        w.endBox(tcmi);
                        w.endBox(tmcd);
                }
                w.endBox(gmhd);
        }

        // dinf / dref (self-reference)
        auto dinf = w.beginBox(FourCC("dinf"));
        auto dref = w.beginFullBox(FourCC("dref"), 0, 0);
        w.writeU32(1);                    // entry_count
        auto urlBox = w.beginFullBox(FourCC("url "), 0, 0x000001);
        w.endBox(urlBox);
        w.endBox(dref);
        w.endBox(dinf);

        // stbl
        auto stbl = w.beginBox(kStbl);

        appendStsdBox(w, t);

        if(t.type == QuickTime::Audio) {
                // ---- Audio: canonical PCM stbl layout ----
                // stsz: constant sample_size = bytesPerSample, sample_count = total PCM frames
                // stsc: run-length collapse of (chunk_index, samples_per_chunk)
                // stco/co64: per-chunk file offsets
                // stts: one entry {count=totalSamples, delta=1} (delta in audio timescale = sample rate)

                // stts
                auto stts = w.beginFullBox(kStts, 0, 0);
                if(t.totalAudioSamples > 0) {
                        w.writeU32(1);
                        w.writeU32(static_cast<uint32_t>(t.totalAudioSamples));
                        w.writeU32(1);  // delta
                } else {
                        w.writeU32(0);
                }
                w.endBox(stts);

                // stsc
                auto stsc = w.beginFullBox(kStsc, 0, 0);
                size_t stscCountOffset = w.pos();
                w.writeU32(0);
                uint32_t stscEntries = 0;
                uint32_t prevSpc = 0;
                for(size_t i = 0; i < t.audioChunkSampleCounts.size(); ++i) {
                        uint32_t spc = t.audioChunkSampleCounts[i];
                        if(i == 0 || spc != prevSpc) {
                                w.writeU32(static_cast<uint32_t>(i + 1));  // first_chunk (1-based)
                                w.writeU32(spc);                           // samples_per_chunk
                                w.writeU32(1);                             // sample_description_index
                                stscEntries++;
                                prevSpc = spc;
                        }
                }
                w.patchU32(stscCountOffset, stscEntries);
                w.endBox(stsc);

                // stsz — constant sample size
                auto stsz = w.beginFullBox(kStsz, 0, 0);
                w.writeU32(t.pcmBytesPerSample);                    // sample_size
                w.writeU32(static_cast<uint32_t>(t.totalAudioSamples)); // sample_count
                w.endBox(stsz);

                // stco / co64 from chunk offsets
                bool need64 = false;
                for(int64_t off : t.audioChunkOffsets) {
                        if(off > static_cast<int64_t>(UINT32_MAX)) { need64 = true; break; }
                }
                if(need64) {
                        auto co64 = w.beginFullBox(kCo64, 0, 0);
                        w.writeU32(static_cast<uint32_t>(t.audioChunkOffsets.size()));
                        for(int64_t off : t.audioChunkOffsets) w.writeU64(static_cast<uint64_t>(off));
                        w.endBox(co64);
                } else {
                        auto stco = w.beginFullBox(kStco, 0, 0);
                        w.writeU32(static_cast<uint32_t>(t.audioChunkOffsets.size()));
                        for(int64_t off : t.audioChunkOffsets) w.writeU32(static_cast<uint32_t>(off));
                        w.endBox(stco);
                }

                // Audio samples are all "sync" — no stss emitted (convention).
        } else {
                // ---- Video / timecode / generic: per-sample arrays ----

                // stts (run-length time-to-sample)
                auto stts = w.beginFullBox(kStts, 0, 0);
                size_t sttsCountOffset = w.pos();
                w.writeU32(0);                    // entry_count placeholder
                uint32_t sttsEntryCount = 0;
                if(!t.durations.isEmpty()) {
                        uint32_t curDelta = t.durations[0];
                        uint32_t curCount = 1;
                        for(size_t i = 1; i < t.durations.size(); ++i) {
                                if(t.durations[i] == curDelta) {
                                        curCount++;
                                } else {
                                        w.writeU32(curCount);
                                        w.writeU32(curDelta);
                                        sttsEntryCount++;
                                        curDelta = t.durations[i];
                                        curCount = 1;
                                }
                        }
                        w.writeU32(curCount);
                        w.writeU32(curDelta);
                        sttsEntryCount++;
                }
                w.patchU32(sttsCountOffset, sttsEntryCount);
                w.endBox(stts);

                // ctts — only emit if any non-zero entries
                bool hasCtts = false;
                for(int32_t v : t.ctsOffsets) { if(v != 0) { hasCtts = true; break; } }
                if(hasCtts) {
                        auto ctts = w.beginFullBox(kCtts, 1, 0); // version 1 (signed offsets)
                        size_t cttsCountOffset = w.pos();
                        w.writeU32(0);
                        uint32_t cttsEntries = 0;
                        if(!t.ctsOffsets.isEmpty()) {
                                int32_t curOff = t.ctsOffsets[0];
                                uint32_t curCount = 1;
                                for(size_t i = 1; i < t.ctsOffsets.size(); ++i) {
                                        if(t.ctsOffsets[i] == curOff) curCount++;
                                        else {
                                                w.writeU32(curCount);
                                                w.writeS32(curOff);
                                                cttsEntries++;
                                                curOff = t.ctsOffsets[i];
                                                curCount = 1;
                                        }
                                }
                                w.writeU32(curCount);
                                w.writeS32(curOff);
                                cttsEntries++;
                        }
                        w.patchU32(cttsCountOffset, cttsEntries);
                        w.endBox(ctts);
                }

                // stsc — one chunk per sample, single entry.
                auto stsc = w.beginFullBox(kStsc, 0, 0);
                w.writeU32(1);                    // entry_count
                w.writeU32(1); w.writeU32(1); w.writeU32(1);
                w.endBox(stsc);

                // stsz — variable sizes
                auto stsz = w.beginFullBox(kStsz, 0, 0);
                w.writeU32(0);                    // sample_size = 0 → per-sample table follows
                w.writeU32(static_cast<uint32_t>(t.sizes.size()));
                for(uint32_t s : t.sizes) w.writeU32(s);
                w.endBox(stsz);

                // stco / co64 — pick based on whether any offset exceeds 32 bits
                bool need64 = false;
                for(int64_t off : t.offsets) {
                        if(off > static_cast<int64_t>(UINT32_MAX)) { need64 = true; break; }
                }
                if(need64) {
                        auto co64 = w.beginFullBox(kCo64, 0, 0);
                        w.writeU32(static_cast<uint32_t>(t.offsets.size()));
                        for(int64_t off : t.offsets) w.writeU64(static_cast<uint64_t>(off));
                        w.endBox(co64);
                } else {
                        auto stco = w.beginFullBox(kStco, 0, 0);
                        w.writeU32(static_cast<uint32_t>(t.offsets.size()));
                        for(int64_t off : t.offsets) w.writeU32(static_cast<uint32_t>(off));
                        w.endBox(stco);
                }

                // stss — only emit if not all samples are sync
                bool allSync = true;
                for(uint8_t k : t.keyframes) { if(!k) { allSync = false; break; } }
                if(!allSync) {
                        auto stss = w.beginFullBox(kStss, 0, 0);
                        size_t stssCountOffset = w.pos();
                        w.writeU32(0);
                        uint32_t stssEntries = 0;
                        for(size_t i = 0; i < t.keyframes.size(); ++i) {
                                if(t.keyframes[i]) {
                                        w.writeU32(static_cast<uint32_t>(i + 1));
                                        stssEntries++;
                                }
                        }
                        w.patchU32(stssCountOffset, stssEntries);
                        w.endBox(stss);
                }
        }

        w.endBox(stbl);
        w.endBox(minf);
        w.endBox(mdia);
        w.endBox(trak);
        (void)gcd64;
}

// ---------------------------------------------------------------------------
// Fragmented writer: init moov + per-fragment moof/mdat
// ---------------------------------------------------------------------------

Error QuickTimeWriter::ensureInitMoovWritten() {
        if(_initMoovWritten) return Error::Ok;
        if(_writeTracks.isEmpty()) return Error::InvalidArgument;
        Error err = writeInitMoov();
        if(err.isError()) return err;
        _initMoovWritten = true;
        // After the init moov, the file position is where the next fragment
        // will begin. Reset the cursor accordingly.
        _mdatCursor = _file->pos();
        return Error::Ok;
}

Error QuickTimeWriter::writeInitMoov() {
        AtomWriter w;

        auto moov = w.beginBox(kMoov);

        // mvhd — duration is 0 for an init segment (no samples yet). The
        // mvex.mehd carries a duration hint if we want one; we don't bother.
        uint32_t movieTimescale = _movieTimescale;
        uint64_t now = macEpochNow();
        auto mvhd = w.beginFullBox(kMvhd, 0, 0);
        w.writeU32(static_cast<uint32_t>(now));   // creation_time
        w.writeU32(static_cast<uint32_t>(now));   // modification_time
        w.writeU32(movieTimescale);
        w.writeU32(0);                             // duration (0 for init)
        w.writeFixed16_16(1.0);                    // rate
        w.writeFixed8_8(1.0);                      // volume
        w.writeU16(0);                             // reserved
        w.writeU32(0); w.writeU32(0);              // reserved[2]
        writeIdentityMatrix(w);
        w.writeU32(0); w.writeU32(0); w.writeU32(0); w.writeU32(0); w.writeU32(0); w.writeU32(0); // pre_defined[6]
        w.writeU32(_nextTrackId);                  // next_track_id
        w.endBox(mvhd);

        // Per-track trak with empty sample tables.
        for(const QuickTimeWriterTrack &t : _writeTracks) {
                appendInitTrak(w, t, movieTimescale);
        }

        // mvex: required for fragmented files. Contains one trex per track
        // giving default sample values that trun can override.
        appendMvex(w);

        // udta: container-level text metadata.  Must come after mvex
        // inside moov in the init segment so the moov layout stays
        // conventional.  appendUdta() is a no-op when no recognized
        // fields are present.
        appendUdta(w);

        w.endBox(moov);

        const List<uint8_t> &bytes = w.data();
        int64_t n = _file->write(bytes.data(), static_cast<int64_t>(bytes.size()));
        if(n != static_cast<int64_t>(bytes.size())) return Error::IOError;
        return Error::Ok;
}

void QuickTimeWriter::appendInitTrak(AtomWriter &w, const QuickTimeWriterTrack &t,
                                     uint32_t movieTimescale) {
        // This is nearly identical to appendTrak() but with empty sample
        // tables (all zero entry counts). Factoring out the common body
        // would require significant restructuring of appendTrak; for now
        // we duplicate the essential structure and keep the paths simple.
        auto trak = w.beginBox(kTrak);

        const uint32_t tkhdFlags = 0x000007;
        uint64_t now = macEpochNow();

        auto tkhd = w.beginFullBox(kTkhd, 0, tkhdFlags);
        w.writeU32(static_cast<uint32_t>(now));
        w.writeU32(static_cast<uint32_t>(now));
        w.writeU32(t.id);
        w.writeU32(0);
        w.writeU32(0);                              // duration 0 for init
        w.writeU32(0); w.writeU32(0);               // reserved[2]
        w.writeU16(0);                              // layer
        w.writeU16(0);                              // alternate_group
        w.writeFixed8_8(t.type == QuickTime::Audio ? 1.0 : 0.0);
        w.writeU16(0);                              // reserved
        writeIdentityMatrix(w);
        if(t.type == QuickTime::Video) {
                w.writeFixed16_16(static_cast<double>(t.size.width()));
                w.writeFixed16_16(static_cast<double>(t.size.height()));
        } else {
                w.writeFixed16_16(0.0);
                w.writeFixed16_16(0.0);
        }
        w.endBox(tkhd);

        // mdia / mdhd / hdlr / minf
        auto mdia = w.beginBox(kMdia);

        auto mdhd = w.beginFullBox(kMdhd, 0, 0);
        w.writeU32(static_cast<uint32_t>(now));
        w.writeU32(static_cast<uint32_t>(now));
        w.writeU32(t.timescale);
        w.writeU32(0);                              // duration 0 for init
        w.writeU16(packLanguage(String("und")));
        w.writeU16(0);                              // pre_defined
        w.endBox(mdhd);

        auto hdlr = w.beginFullBox(kHdlr, 0, 0);
        w.writeU32(0);                              // pre_defined
        switch(t.type) {
                case QuickTime::Video:         w.writeFourCC(kHdlrVide); break;
                case QuickTime::Audio:         w.writeFourCC(kHdlrSoun); break;
                case QuickTime::TimecodeTrack: w.writeFourCC(kHdlrTmcd); break;
                default:                       w.writeFourCC(FourCC("data")); break;
        }
        w.writeU32(0); w.writeU32(0); w.writeU32(0);
        const char *hname =
                t.type == QuickTime::Video         ? "VideoHandler" :
                t.type == QuickTime::Audio         ? "SoundHandler" :
                t.type == QuickTime::TimecodeTrack ? "TimecodeHandler" :
                                                     "DataHandler";
        w.writeBytes(hname, std::strlen(hname) + 1);
        w.endBox(hdlr);

        auto minf = w.beginBox(kMinf);

        // vmhd/smhd/gmhd — same as in classic appendTrak
        if(t.type == QuickTime::Video) {
                auto vmhd = w.beginFullBox(FourCC("vmhd"), 0, 0x000001);
                w.writeU16(0);
                w.writeU16(0); w.writeU16(0); w.writeU16(0);
                w.endBox(vmhd);
        } else if(t.type == QuickTime::Audio) {
                auto smhd = w.beginFullBox(FourCC("smhd"), 0, 0);
                w.writeU16(0);
                w.writeU16(0);
                w.endBox(smhd);
        } else {
                auto gmhd = w.beginBox(FourCC("gmhd"));
                auto gmin = w.beginFullBox(FourCC("gmin"), 0, 0);
                w.writeU16(0x0040);
                w.writeU16(0x8000); w.writeU16(0x8000); w.writeU16(0x8000);
                w.writeU16(0);
                w.writeU16(0);
                w.endBox(gmin);
                w.endBox(gmhd);
        }

        // dinf
        auto dinf = w.beginBox(FourCC("dinf"));
        auto dref = w.beginFullBox(FourCC("dref"), 0, 0);
        w.writeU32(1);
        auto urlBox = w.beginFullBox(FourCC("url "), 0, 0x000001);
        w.endBox(urlBox);
        w.endBox(dref);
        w.endBox(dinf);

        // stbl — sample description with real codec info, empty sample tables
        auto stbl = w.beginBox(kStbl);

        appendStsdBox(w, t);

        // Empty sample tables — all zero entry counts. Required by spec to
        // be present even for fragmented init segments.
        { auto b = w.beginFullBox(kStts, 0, 0); w.writeU32(0); w.endBox(b); }
        { auto b = w.beginFullBox(kStsc, 0, 0); w.writeU32(0); w.endBox(b); }
        { auto b = w.beginFullBox(kStsz, 0, 0); w.writeU32(0); w.writeU32(0); w.endBox(b); }
        { auto b = w.beginFullBox(kStco, 0, 0); w.writeU32(0); w.endBox(b); }

        w.endBox(stbl);
        w.endBox(minf);
        w.endBox(mdia);
        w.endBox(trak);
}

void QuickTimeWriter::appendMvex(AtomWriter &w) {
        auto mvex = w.beginBox(kMvex);
        for(const QuickTimeWriterTrack &t : _writeTracks) {
                // trex: default sample values for fragments. We set
                // defaults for the "common case" and let trun override
                // on a per-sample basis when needed.
                auto trex = w.beginFullBox(kTrex, 0, 0);
                w.writeU32(t.id);                                  // track_ID
                w.writeU32(1);                                     // default_sample_description_index
                // default_sample_duration: for video use frame rate, for audio use 1, else 0.
                uint32_t defDur = 0;
                if(t.type == QuickTime::Video && t.frameRate.isValid()) {
                        defDur = t.frameRate.rational().denominator();
                } else if(t.type == QuickTime::Audio) {
                        defDur = 1;
                }
                w.writeU32(defDur);
                // default_sample_size: for audio PCM use bytes-per-sample,
                // for variable-size video use 0 (trun overrides).
                uint32_t defSize = (t.type == QuickTime::Audio) ? t.pcmBytesPerSample : 0;
                w.writeU32(defSize);
                // default_sample_flags: 0 (sync sample). Each trun can
                // override the first_sample_flags or per-sample_flags.
                w.writeU32(0);
                w.endBox(trex);
        }
        w.endBox(mvex);
}

Error QuickTimeWriter::writeFragment() {
        // Compute each track's offset within the concatenated fragment
        // mdat: track 0's samples at offset 0, track 1's samples after
        // track 0's bytes, etc. Tracks with no pending samples are skipped.
        //
        // Layout inside the fragment mdat:
        //   [ track A payload ][ track B payload ][ ... ]
        // Within each track's section, samples are adjacent so trun's
        // data_offset + consecutive size advancement works correctly.

        struct FragTrack {
                size_t   trackIndex;
                uint64_t payloadOffset;  // offset within mdat payload
        };
        List<FragTrack> fragTracks;
        uint64_t totalPayload = 0;
        for(size_t ti = 0; ti < _writeTracks.size(); ++ti) {
                QuickTimeWriterTrack &t = _writeTracks[ti];
                if(t.fragSampleSizes.isEmpty()) continue;
                FragTrack ft;
                ft.trackIndex    = ti;
                ft.payloadOffset = totalPayload;
                fragTracks.pushToBack(ft);
                totalPayload += t.fragPayload.size();
        }

        // Build the moof box in memory so we know its size before writing.
        AtomWriter moofWriter;
        auto moofBox = moofWriter.beginBox(kMoof);

        // mfhd: the fragment's sequence number.  Per ISO/IEC 14496-12
        // §8.8.5 the sequence number of the first fragment shall be 1
        // and subsequent fragments shall increment by 1.  We start at
        // 1 in the constructor and ++ after each successful writeFragment().
        //
        // Note on VLC's "Fragment sequence discontinuity detected 1 != 0"
        // info message: VLC's mp4 demuxer (modules/demux/mp4/mp4.c, the
        // FragGetMoofSequenceNumber path) initializes its internal
        // `i_lastseqnumber` sentinel to UINT32_MAX and checks each
        // fragment with
        //     b_discontinuity = (i_sequence_number != i_lastseqnumber + 1);
        // On the first fragment UINT32_MAX + 1 wraps to 0, so VLC expects
        // sequence 0 and prints `1 != 0` at msg_Info severity.  This is
        // a VLC-side off-by-one — every spec-compliant fragmented MP4
        // (ffmpeg, GPAC, Bento4) trips the same message on its first
        // fragment.  Playback is unaffected (the "discontinuity" flag
        // just triggers a benign time rebase at t=0).  We stay
        // spec-compliant and do NOT start from 0 to paper over it.
        // Verified against VLC master mp4.c:5655-5660 on 2026-04-08.
        auto mfhd = moofWriter.beginFullBox(kMfhd, 0, 0);
        moofWriter.writeU32(_fragmentSequence);
        moofWriter.endBox(mfhd);

        struct TrunPatch {
                size_t   dataOffsetPos;  // byte offset within moofWriter where the trun data_offset lives
                uint64_t mdatOffset;     // offset within the fragment mdat payload for this track
        };
        List<TrunPatch> patches;

        for(const FragTrack &ft : fragTracks) {
                QuickTimeWriterTrack &t = _writeTracks[ft.trackIndex];

                // Detect whether all samples share the same duration /
                // size / flags — common for PCM audio (all PCM frames are
                // identical) and also possible for constant-duration
                // uncompressed video. When true, we compress the trun by
                // setting tfhd overrides and stripping per-sample fields
                // from trun. For a 30-frame fragment of 48 kHz stereo s16
                // audio this saves ~720 KB of trun metadata.
                uint32_t sampleCount = static_cast<uint32_t>(t.fragSampleSizes.size());
                bool uniformDuration = true;
                bool uniformSize     = true;
                bool uniformFlags    = true;
                for(size_t i = 1; i < sampleCount; ++i) {
                        if(t.fragSampleDurations[i] != t.fragSampleDurations[0]) uniformDuration = false;
                        if(t.fragSampleSizes[i]     != t.fragSampleSizes[0])     uniformSize     = false;
                        if(t.fragSampleKeyframes[i] != t.fragSampleKeyframes[0]) uniformFlags    = false;
                }
                bool hasCtts = false;
                for(int32_t v : t.fragSampleCtsOffsets) { if(v != 0) { hasCtts = true; break; } }
                uint32_t uniformSampleFlags = 0;
                if(!t.fragSampleKeyframes.isEmpty() && !t.fragSampleKeyframes[0]) {
                        uniformSampleFlags |= (1u << 16);
                }

                auto traf = moofWriter.beginBox(kTraf);

                // tfhd: default_base_is_moof (0x020000) plus whichever
                // default overrides we're using to compress the trun.
                //   0x000008 default_sample_duration_present
                //   0x000010 default_sample_size_present
                //   0x000020 default_sample_flags_present
                uint32_t tfhdFlags = 0x020000;
                if(uniformDuration) tfhdFlags |= 0x000008;
                if(uniformSize)     tfhdFlags |= 0x000010;
                if(uniformFlags)    tfhdFlags |= 0x000020;

                auto tfhd = moofWriter.beginFullBox(kTfhd, 0, tfhdFlags);
                moofWriter.writeU32(t.id);
                if(uniformDuration) moofWriter.writeU32(t.fragSampleDurations[0]);
                if(uniformSize)     moofWriter.writeU32(t.fragSampleSizes[0]);
                if(uniformFlags)    moofWriter.writeU32(uniformSampleFlags);
                moofWriter.endBox(tfhd);

                // tfdt: base_media_decode_time (v1, 64-bit)
                auto tfdt = moofWriter.beginFullBox(kTfdt, 1, 0);
                moofWriter.writeU64(t.fragBaseDts);
                moofWriter.endBox(tfdt);

                // trun: data_offset is always present. Per-sample fields
                // are only emitted for dimensions that aren't uniform.
                uint32_t trunFlags = 0x000001;  // data_offset_present
                if(!uniformDuration) trunFlags |= 0x000100;  // sample_duration_present
                if(!uniformSize)     trunFlags |= 0x000200;  // sample_size_present
                if(!uniformFlags)    trunFlags |= 0x000400;  // sample_flags_present
                if(hasCtts)          trunFlags |= 0x000800;  // sample_composition_time_offsets_present

                auto trun = moofWriter.beginFullBox(kTrun, hasCtts ? 1 : 0, trunFlags);
                moofWriter.writeU32(sampleCount);
                size_t dataOffsetPos = moofWriter.pos();
                moofWriter.writeS32(0);  // patched below

                for(size_t i = 0; i < sampleCount; ++i) {
                        if(!uniformDuration) moofWriter.writeU32(t.fragSampleDurations[i]);
                        if(!uniformSize)     moofWriter.writeU32(t.fragSampleSizes[i]);
                        if(!uniformFlags) {
                                uint32_t sflags = 0;
                                if(!t.fragSampleKeyframes[i]) sflags |= (1u << 16);
                                moofWriter.writeU32(sflags);
                        }
                        if(hasCtts) {
                                moofWriter.writeS32(t.fragSampleCtsOffsets[i]);
                        }
                }
                moofWriter.endBox(trun);

                TrunPatch patch;
                patch.dataOffsetPos = dataOffsetPos;
                patch.mdatOffset    = ft.payloadOffset;
                patches.pushToBack(patch);

                moofWriter.endBox(traf);
        }

        moofWriter.endBox(moofBox);

        // Patch trun.data_offset values now that we know the moof size.
        size_t        moofSize       = moofWriter.pos();
        const int64_t mdatHeaderSize = 8;  // 32-bit size form for per-fragment mdat
        for(const TrunPatch &p : patches) {
                int64_t dataOffset = static_cast<int64_t>(moofSize) + mdatHeaderSize +
                                     static_cast<int64_t>(p.mdatOffset);
                if(dataOffset < INT32_MIN || dataOffset > INT32_MAX) {
                        promekiWarn("QuickTime: fragment data_offset %lld exceeds int32",
                                    static_cast<long long>(dataOffset));
                        return Error::OutOfRange;
                }
                uint32_t v = static_cast<uint32_t>(static_cast<int32_t>(dataOffset));
                moofWriter.patchU32(p.dataOffsetPos, v);
        }

        // Write moof + mdat box header as a single writev. Both are
        // small (KB scale) and back-to-back; combining them saves one
        // syscall per fragment without giving up anything else.
        // The per-track payloads that follow are large and still go
        // through writeBulk so they can hit the DIO fast path.
        const List<uint8_t> &moofBytes = moofWriter.data();
        int64_t mdatTotalSize = static_cast<int64_t>(totalPayload) + mdatHeaderSize;
        if(mdatTotalSize > UINT32_MAX) {
                promekiWarn("QuickTime: fragment mdat size %lld exceeds 32-bit",
                            static_cast<long long>(mdatTotalSize));
                return Error::OutOfRange;
        }
        AtomWriter mdatHdr;
        mdatHdr.writeU32(static_cast<uint32_t>(mdatTotalSize));
        mdatHdr.writeFourCC(kMdat);
        const List<uint8_t> &hdrBytes = mdatHdr.data();

        Error err = _file->seek(_mdatCursor);
        if(err.isError()) return err;

        File::IOVec iov[2];
        iov[0].data = moofBytes.data();
        iov[0].size = moofBytes.size();
        iov[1].data = hdrBytes.data();
        iov[1].size = hdrBytes.size();
        int64_t headerTotal = static_cast<int64_t>(moofBytes.size() + hdrBytes.size());
        int64_t n = _file->writev(iov, 2);
        if(n != headerTotal) return Error::IOError;
        _mdatCursor += headerTotal;

        // Write each track's payload contiguously, in the order used above.
        // Use writeBulk so large payloads (video) get DIO. The fragment
        // payload buffer is backed by List<uint8_t> (std::vector), whose
        // data pointer has no alignment guarantee — writeBulk will detect
        // that and fall back to normal I/O for non-aligned sources. When
        // the allocator happens to hand us an aligned chunk (common for
        // sizes > one page), DIO kicks in.
        for(const FragTrack &ft : fragTracks) {
                QuickTimeWriterTrack &t = _writeTracks[ft.trackIndex];
                int64_t tpSize = static_cast<int64_t>(t.fragPayload.size());
                if(tpSize == 0) continue;
                n = _file->writeBulk(t.fragPayload.data(), tpSize);
                if(n != tpSize) return Error::IOError;
                _mdatCursor += tpSize;
        }

        // Reset per-fragment state on every track (whether or not it had samples).
        for(QuickTimeWriterTrack &t : _writeTracks) {
                t.fragSampleSizes.clear();
                t.fragSampleDurations.clear();
                t.fragSampleCtsOffsets.clear();
                t.fragSampleKeyframes.clear();
                t.fragPayload.clear();
                t.fragBaseDts = t.fragRunningDts;
        }
        _fragmentSequence++;

        return Error::Ok;
}

PROMEKI_NAMESPACE_END
