/**
 * @file      mediaiotask_quicktime.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/mediaiotask.h>
#include <promeki/quicktime.h>
#include <promeki/audiobuffer.h>
#include <promeki/mediapayload.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief MediaIOTask backend for QuickTime / ISO-BMFF container files.
 * @ingroup proav
 *
 * Wraps the @c QuickTime engine to provide read and write access to
 * @c .mov / @c .mp4 / @c .m4v container files through the MediaIO
 * interface. Compressed sample bytes flow through the @c Frame as
 * @ref CompressedVideoPayload objects, with downstream consumers
 * responsible for decoding via whichever codec implementation they
 * prefer. Uncompressed video tracks (@c 2vuy / @c v210 / etc.) are
 * wrapped as @ref UncompressedVideoPayload objects.
 *
 * @par Audio
 *
 * PCM audio tracks are read and written. On read, each @c Frame carries
 * an @ref UncompressedAudioPayload containing the PCM samples that
 * correspond to the video frame duration; compressed (e.g. AAC) audio
 * tracks are surfaced as @ref CompressedAudioPayload objects.
 * On write, @c Frame audio is accumulated in an @c AudioBuffer FIFO and
 * flushed to the engine writer in per-video-frame-aligned chunks. Source
 * audio that differs from the on-disk storage format (e.g. float32 input
 * → s16 little-endian on disk) is converted by the FIFO on push. The
 * compressed audio write path is not yet implemented.
 *
 * @par Probe
 *
 * The format probe checks for an @c ftyp box in the first 16 bytes of
 * the device and verifies the major brand against a small set of
 * recognized values (@c qt, @c isom, @c mp41, @c mp42, @c iso2 — @c iso8,
 * @c f4v).
 *
 * @par Config keys
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::Filename                | String                 | — | Path to the .mov / .mp4 file. |
 * | @ref MediaConfig::VideoTrack              | int                    | -1 (auto) | 0-based index of the video track to read. |
 * | @ref MediaConfig::AudioTrack              | int                    | -1 (auto) | 0-based index of the audio track to read. |
 * | @ref MediaConfig::QuickTimeLayout         | Enum (QuickTimeLayout) | Fragmented | Writer on-disk layout. |
 * | @ref MediaConfig::QuickTimeFragmentFrames | int                    | 30 | Video frames per fragment (Fragmented layout only). |
 * | @ref MediaConfig::QuickTimeFlushSync      | bool                   | false | fdatasync after every flush. |
 */
class MediaIOTask_QuickTime : public MediaIOTask {
        public:
                /** @brief Default number of video frames per fragment. */
                static inline constexpr int DefaultFragmentFrames = 30;

                /** @brief Returns the format descriptor for this backend. */
                static MediaIO::FormatDesc formatDesc();

                /** @brief Constructs a MediaIOTask_QuickTime. */
                MediaIOTask_QuickTime() = default;

                /** @brief Destructor. */
                ~MediaIOTask_QuickTime() override;

        private:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandSeek &cmd) override;

                Error proposeInput(const MediaDesc &offered,
                                   MediaDesc *preferred) const override;

                // True when @p pd is a PixelFormat this writer knows
                // how to serialise into a valid QuickTime sample
                // entry plus mdat payload.  See proposeInput() for
                // the supported set and the rationale for each
                // omission.
                static bool isSupportedPixelFormat(const PixelFormat &pd);

                // Picks the closest writer-supported PixelFormat for
                // an offered shape, preserving bit depth, chroma
                // subsampling, and YUV / RGB family where possible
                // so the planner-inserted CSC stays cheap.
                static PixelFormat pickSupportedPixelFormat(const PixelFormat &offered);

                /** @brief Build a Frame for the requested video sample. */
                Error readVideoFrame(const FrameNumber &frameIndex, Frame::Ptr &outFrame);

                /** @brief Pulls @p samples samples for the current audio track into @p out. */
                Error readAudioSlice(uint64_t startSample, size_t samples,
                                     MediaPayload::Ptr &out);

                /** @brief Lazily registers writer tracks from the supplied frame. */
                Error setupWriterFromFrame(const Frame &frame);

                /** @brief Drains pending audio from the FIFO into the engine writer. */
                Error drainWriterAudio(bool flush);

                QuickTime    _qt;
                MediaIOMode  _mode = MediaIO_NotOpen;
                String       _filename;
                int          _videoTrackIndex = -1;   ///< Selected video track in the engine.
                int          _audioTrackIndex = -1;   ///< Selected audio track in the engine.
                FrameNumber  _currentFrame{0};
                FrameCount   _frameCount{0};
                FrameRate    _frameRate;
                Timecode     _anchorTimecode;
                uint64_t     _audioSampleCursor = 0;  ///< Next audio sample index to read.
                AudioDesc    _audioDesc;              ///< Selected audio track descriptor.

                bool         _writerTracksRegistered = false;
                uint32_t     _writerVideoTrackId = 0;
                uint32_t     _writerAudioTrackId = 0;
                uint32_t     _writerTimecodeTrackId = 0;
                FrameCount   _writerFrameCount{0};
                int          _writerFragmentFrames = DefaultFragmentFrames;
                FrameCount   _writerFramesSinceFlush{0};
                AudioBuffer  _writerAudioFifo;        ///< Bridges Frame audio to per-video-frame audio chunks.
                AudioDesc    _writerAudioStorage;     ///< Storage format for the audio track.
};

PROMEKI_NAMESPACE_END
