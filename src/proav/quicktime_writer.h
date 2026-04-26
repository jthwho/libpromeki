/**
 * @file      quicktime_writer.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Internal header for the QuickTime writer backend. Not part of the
 * public API — included only by quicktime.cpp (for the factory) and
 * quicktime_writer.cpp (for the implementation).
 */

#pragma once

#include <promeki/quicktime.h>
#include <promeki/list.h>
#include <promeki/file.h>

#include "quicktime_atom.h"

PROMEKI_NAMESPACE_BEGIN

class File;

/**
 * @brief Per-track state accumulated by the writer until @c finalize().
 *
 * Sample-table data is stored fully expanded (one entry per sample) for
 * the same simplicity reasons as the reader. The arrays are walked
 * during @c finalize() to emit the @c stbl child atoms in their
 * run-length-encoded forms.
 */
struct QuickTimeWriterTrack {
                uint32_t             id = 0;
                QuickTime::TrackType type = QuickTime::InvalidTrack;
                uint32_t             timescale = 0; ///< mdia.mdhd timescale.
                FrameRate            frameRate;
                // Video specifics:
                PixelFormat pixelFormat;
                Size2Du32   size;
                /**
         * @brief Codec-specific sample-description extension payload
         *        (e.g. serialized @c avcC or @c hvcC record).
         *
         * Populated lazily from the first keyframe sample for H.264
         * and HEVC tracks.  Written inside the visual sample entry as
         * a child box of type @c codecConfigType when the @c stsd is
         * emitted (classic layout: at @c finalize(); fragmented
         * layout: at @c ensureInitMoovWritten()).  Empty for codecs
         * that do not need an out-of-band configuration record.
         */
                Buffer::Ptr codecConfigBox;
                FourCC      codecConfigType{'\0', '\0', '\0',
                                       '\0'}; ///< @c avcC, @c hvcC, ... (zero FourCC when @c codecConfigBox is empty).
                // Audio specifics:
                AudioDesc audioDesc;
                uint32_t  pcmBytesPerSample = 0; ///< channels × bytesPerSample, for audio tracks.
                // Timecode specifics:
                uint32_t tcStartFrame = 0;
                uint32_t tcFlags = 0;
                uint8_t  tcFrameCount = 0;

                // ---- Video / timecode / generic: per-sample arrays ----
                // Each writeSample call records one entry per array.
                List<int64_t>  offsets;
                List<uint32_t> sizes;
                List<uint32_t> durations;  ///< In track timescale.
                List<int32_t>  ctsOffsets; ///< pts - dts; all zero in MVP.
                List<uint8_t>  keyframes;  ///< 1 if sync sample.
                uint64_t       totalDuration = 0;

                // ---- Audio: chunk-based arrays ----
                // QuickTime PCM audio stores N "samples" (one per PCM frame) grouped
                // into chunks. Each writeSample call appends one chunk and records
                // how many PCM frames it contains.
                List<int64_t>  audioChunkOffsets;
                List<uint32_t> audioChunkSampleCounts;
                uint64_t       totalAudioSamples = 0;

                // ---- Fragmented: per-fragment sample state ----
                // Populated for samples written since the last flush(). Each track
                // has its own fragPayload so that at flush time all of the track's
                // samples can be written contiguously in the fragment's mdat —
                // which trun requires, since it describes samples as a consecutive
                // run starting from a single data_offset.
                List<uint32_t> fragSampleSizes;
                List<uint32_t> fragSampleDurations;
                List<int32_t>  fragSampleCtsOffsets;
                List<uint8_t>  fragSampleKeyframes;
                List<uint8_t>  fragPayload;        ///< Sample bytes for this track in the current fragment.
                uint64_t       fragBaseDts = 0;    ///< dts of the first sample in the current fragment.
                uint64_t       fragRunningDts = 0; ///< Next dts to assign (advances across fragments).
};

/**
 * @brief Writer backend for the QuickTime / ISO-BMFF container.
 *
 * Builds a classic mov file with @c mdat first and @c moov at the end.
 * Sample payloads are appended to the file as @c writeSample() is
 * called; per-sample tables are accumulated in memory and serialized
 * during @c finalize() into the @c moov atom following the @c mdat.
 *
 * The writer uses a single @c File handle for both atom writes and
 * payload writes. Phase 4 keeps direct I/O out of the picture for
 * simplicity — Phase 6 (the DIO audit) will revisit and add DIO to
 * the bulk @c mdat write path.
 */
class QuickTimeWriter : public QuickTime::Impl {
                PROMEKI_SHARED_DERIVED(QuickTime::Impl, QuickTimeWriter)
        public:
                QuickTimeWriter();
                ~QuickTimeWriter() override;

                Error open() override;
                void  close() override;
                bool  isOpen() const override { return _isOpen; }

                Error setLayout(QuickTime::Layout layout) override;

                Error addVideoTrack(const PixelFormat &codec, const Size2Du32 &size, const FrameRate &frameRate,
                                    uint32_t *outTrackId) override;
                Error addAudioTrack(const AudioDesc &desc, uint32_t *outTrackId) override;
                Error addTimecodeTrack(const Timecode &startTimecode, const FrameRate &frameRate,
                                       uint32_t *outTrackId) override;
                Error writeSample(uint32_t trackId, const QuickTime::Sample &sample) override;
                void  setContainerMetadata(const Metadata &meta) override;
                Error flush() override;
                Error finalize() override;

        private:
                /** @brief Returns a freshly assigned track ID (1-based). */
                uint32_t allocateTrackId();

                // ---- Classic layout helpers ----
                Error writeMoov();
                void appendTrak(quicktime_atom::AtomWriter &mw, const QuickTimeWriterTrack &t, uint32_t movieTimescale);
                Error patchMdatSize();

                /**
                 * @brief Emits a @c udta box containing classic
                 *        ©-prefixed text atoms for the container
                 *        metadata set via @ref setContainerMetadata.
                 *
                 * The box is appended to @p mw at the current write
                 * position; it is intended to be called from inside an
                 * already-open @c moov box.  No @c udta box is written
                 * when the container metadata has no recognized text
                 * fields.
                 */
                void appendUdta(quicktime_atom::AtomWriter &mw);

                // ---- Fragmented layout helpers ----

                /** @brief Ensures the init moov has been written.
                 *         Idempotent; call from writeSample once tracks are known. */
                Error ensureInitMoovWritten();

                /** @brief Writes the movie header (init moov) for a fragmented file. */
                Error writeInitMoov();

                /** @brief Appends a trak to the init moov with empty sample tables. */
                void appendInitTrak(quicktime_atom::AtomWriter &mw, const QuickTimeWriterTrack &t,
                                    uint32_t movieTimescale);

                /** @brief Appends the mvex box (movie extends) with trex per track. */
                void appendMvex(quicktime_atom::AtomWriter &mw);

                /** @brief Emits the current pending fragment as a moof+mdat pair. */
                Error writeFragment();

                /** @brief Returns true if any track has pending fragment samples. */
                bool hasPendingFragmentSamples() const;

                // ---- Common ----

                File                      *_file = nullptr;
                bool                       _isOpen = false;
                int64_t                    _mdatHeaderOffset = 0;
                int64_t                    _mdatPayloadStart = 0;
                int64_t                    _mdatCursor = 0;
                List<QuickTimeWriterTrack> _writeTracks;
                Metadata                   _writerMetadata;
                uint32_t                   _nextTrackId = 1;

                // Fragmented-mode state. Sample bytes are accumulated in
                // per-track @c QuickTimeWriterTrack::fragPayload buffers
                // until flush() writes the whole fragment (moof + mdat +
                // per-track payloads concatenated) atomically. This gives
                // us the standard moof-before-mdat layout for maximum
                // player compatibility at the cost of buffering one
                // fragment's worth of sample data in memory.
                bool     _initMoovWritten = false;
                uint32_t _fragmentSequence = 1;
};

PROMEKI_NAMESPACE_END
