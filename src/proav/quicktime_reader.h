/**
 * @file      quicktime_reader.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Internal header for the QuickTime reader backend. Not part of the
 * public API — included only by quicktime.cpp (for the factory) and
 * quicktime_reader.cpp (for the implementation).
 */

#pragma once

#include <promeki/quicktime.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

class File;

/**
 * @brief Per-track in-memory sample index built during open().
 *
 * Video / timecode / generic tracks store fully-expanded per-sample
 * arrays — straightforward O(1) lookups. Audio tracks with a canonical
 * PCM layout (constant stsz sample_size and constant stts sample
 * delta) use a compact chunk-level representation instead, to keep
 * memory bounded for long captures: one hour of 48 kHz stereo would
 * otherwise take ~7 GB of per-sample metadata.
 *
 * When @c audioCompact is true, the per-sample @c offset / @c size /
 * @c dts / @c pts / @c duration / @c keyframe arrays are empty and
 * per-sample values are computed on demand from the chunk arrays in
 * @c audioChunkOffsets + @c audioChunkSamplesPerChunk +
 * @c audioChunkFirstSample, plus the constants @c audioSampleSize and
 * @c audioSampleDelta. See @c sampleOffset() / @c sampleSize() /
 * @c sampleDts() in QuickTimeReader for the lookup.
 */
struct QuickTimeSampleIndex {
        // ---- Per-sample path (video, timecode, generic, non-compact audio) ----
        List<int64_t>  offset;     ///< Absolute file offset of each sample.
        List<uint32_t> size;       ///< Size in bytes of each sample.
        List<int64_t>  dts;        ///< Decode timestamp in track timescale.
        List<int64_t>  pts;        ///< Presentation timestamp (dts + ctts offset).
        List<uint32_t> duration;   ///< Sample duration in track timescale.
        List<uint8_t>  keyframe;   ///< 1 if sync sample (or 1 for all if no stss).

        // ---- Compact audio path ----
        bool           audioCompact = false;
        List<int64_t>  audioChunkOffsets;      ///< File offset of each chunk (from stco/co64).
        List<uint32_t> audioChunkSamplesPerChunk; ///< Samples in each chunk (from stsc expansion).
        List<uint64_t> audioChunkFirstSample;  ///< Cumulative first-sample index per chunk.
        uint32_t       audioSampleSize = 0;    ///< Constant bytes per PCM frame.
        uint32_t       audioSampleDelta = 0;   ///< Constant stts delta per sample.
        uint64_t       audioTotalSamples = 0;  ///< Total samples across all chunks.
};

/**
 * @brief Reader backend for the QuickTime / ISO-BMFF container.
 *
 * Holds (in later phases) two @c File handles on the same path:
 * @c _metaFile for buffered reads of atoms, sample tables, and audio
 * samples, and @c _imageFile (direct-I/O enabled) for bulk video
 * sample reads using @c File::readBulk(). Phase 1 only exercises the
 * metadata handle during open(); the DIO handle is prepared lazily in
 * Phase 2 when sample reads land.
 *
 * The file handle is held via raw pointer so the containing class
 * stays (shallow-)copyable for the PROMEKI_SHARED clone machinery,
 * matching the established AudioFile::Impl pattern. Ownership is
 * managed manually in open()/close()/~QuickTimeReader().
 */
class QuickTimeReader : public QuickTime::Impl {
        PROMEKI_SHARED_DERIVED(QuickTime::Impl, QuickTimeReader)
        public:
                QuickTimeReader();
                ~QuickTimeReader() override;

                Error open() override;
                void  close() override;
                bool  isOpen() const override { return _isOpen; }

                Error readSample(size_t trackIndex, uint64_t sampleIndex,
                                 QuickTime::Sample &out) override;

                Error readSampleRange(size_t trackIndex, uint64_t startSampleIndex,
                                      uint64_t count, QuickTime::Sample &out) override;

        private:
                /** @brief Holds per-track timecode entry parameters until the
                 *         single tmcd sample is read and turned into a Timecode. */
                struct TimecodeTrackInfo {
                        bool      present = false;
                        size_t    trackIndex = 0;
                        uint32_t  flags = 0;        ///< tmcd entry flags (bit0 = drop frame).
                        uint32_t  timescale = 0;
                        uint32_t  frameDuration = 0;
                        uint8_t   numberOfFrames = 0;
                };

                /** @brief Parses the top-level atom list after @c ftyp. */
                Error parseTopLevel(int64_t fileSize);

                /** @brief Parses a @c moov box at the given payload range. */
                Error parseMoov(int64_t payloadOffset, int64_t payloadEnd);

                /** @brief Parses a single @c trak box and appends to _tracks. */
                Error parseTrak(int64_t payloadOffset, int64_t payloadEnd);

                /** @brief Parses the sample table atoms inside @c stbl. */
                Error parseSampleTable(int64_t stblPayloadOffset, int64_t stblPayloadEnd,
                                       QuickTimeSampleIndex &out, bool isAudio);

                /** @brief Given a sample index, returns its absolute file offset. */
                int64_t  sampleOffset(const QuickTimeSampleIndex &idx, uint64_t sampleIndex) const;
                /** @brief Given a sample index, returns its size in bytes. */
                uint32_t sampleSize(const QuickTimeSampleIndex &idx, uint64_t sampleIndex) const;
                /** @brief Given a sample index, returns its dts in the track's timescale. */
                int64_t  sampleDts(const QuickTimeSampleIndex &idx, uint64_t sampleIndex) const;
                /** @brief Returns the number of samples covered by @p idx. */
                uint64_t sampleCount(const QuickTimeSampleIndex &idx) const;

                /** @brief Parses @c edts/elst into a track edit start offset. */
                Error parseEditList(int64_t edtsPayloadOffset, int64_t edtsPayloadEnd,
                                    int64_t &outStartOffset);

                /** @brief Parses a @c tmcd sample-entry, capturing flags/scale. */
                Error parseTimecodeSampleEntry(int64_t entryPayloadOffset, int64_t entryPayloadEnd,
                                               TimecodeTrackInfo &info);

                /** @brief Parses @c udta at the given range into _containerMetadata. */
                Error parseUdta(int64_t payloadOffset, int64_t payloadEnd);

                /** @brief Walks the file from @p startOffset for @c moof boxes
                 *         and appends each fragment's samples to the matching
                 *         track's sample index. */
                Error parseFragments(int64_t startOffset, int64_t fileSize);

                /** @brief Parses one @c moof box. */
                Error parseMoof(int64_t moofPayloadOffset, int64_t moofPayloadEnd, int64_t moofStart);

                /** @brief Parses one @c traf box inside a @c moof. */
                Error parseTraf(int64_t trafPayloadOffset, int64_t trafPayloadEnd, int64_t moofStart);

                /** @brief Parses one @c trun and appends samples to @p idx. */
                Error parseTrun(int64_t trunPayloadOffset, int64_t trunPayloadEnd,
                                size_t trackIdx, int64_t moofStart,
                                bool baseDataOffsetPresent, uint64_t baseDataOffset,
                                bool defaultBaseIsMoof,
                                uint32_t defSampleDuration, uint32_t defSampleSize,
                                uint32_t defSampleFlags,
                                int64_t &cursorDts, int64_t &prevDataEnd);

                /** @brief Looks up a track index by its QuickTime track ID. */
                size_t findTrackIndexById(uint32_t trackId) const;

                /** @brief After tracks are built, populates _mediaDesc. */
                void  buildMediaDesc();

                /** @brief Reads the single sample of the tmcd track and builds
                 *         a Timecode in @c _startTimecode. */
                Error resolveStartTimecode();

                /** @brief Returns the active device (owned File or user-provided). */
                IODevice *activeDevice() const;

                /** @brief Confirms the file handle suitable for video sample
                 *         reads is available. With the single-handle design
                 *         this just verifies _metaFile is non-null. */
                Error ensureImageFile();

                File                          *_metaFile = nullptr;
                bool                           _isOpen = false;
                uint32_t                       _movieTimescale = 0;
                uint64_t                       _movieDuration  = 0;
                List<QuickTimeSampleIndex>     _sampleIndices;  ///< Parallel to _tracks.
                TimecodeTrackInfo              _tmcdInfo;
};

PROMEKI_NAMESPACE_END
