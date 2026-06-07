/**
 * @file      ffmpegmediaio.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV && PROMEKI_ENABLE_FFMPEG
#include <promeki/audiodesc.h>
#include <promeki/dedicatedthreadmediaio.h>
#include <promeki/deque.h>
#include <promeki/framecount.h>
#include <promeki/framenumber.h>
#include <promeki/framerate.h>
#include <promeki/mediaiofactory.h>
#include <promeki/namespace.h>
#include <promeki/string.h>

// Forward-declare the libav* types we hold by pointer so this promeki
// header never pulls in the libavformat / libavcodec headers — they live
// only in the .cpp.
struct AVFormatContext;
struct AVBSFContext;
struct AVStream;
struct AVPacket;

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Generic FFmpeg (libavformat) container MediaIO backend.
 * @ingroup proav
 *
 * Reads and writes any container format the vendored libavformat
 * provides a (de)muxer for — Matroska / WebM, AVI, FLV, MPEG-PS,
 * Ogg, and many more.  It is the library's @em fallback container
 * backend: the native backends (QuickTime, WAV/AIFF, image files,
 * MPEG-TS) always win for the formats they own, and FFmpeg is only
 * auto-selected for formats no native backend handles.  It is always
 * reachable explicitly by name — set @c MediaConfig::Type to
 * @c "FFmpeg" — so a caller can force FFmpeg even for a @c .mov.
 *
 * @par Codec strategy — compressed passthrough
 * Like @ref QuickTimeMediaIO, this backend operates at the container
 * level only.  On read each compressed video / audio stream is
 * surfaced as a @ref CompressedVideoPayload / @ref CompressedAudioPayload
 * (PCM audio as a @ref PcmAudioPayload); the planner splices the
 * appropriate decoder downstream.  On write, an offered uncompressed
 * stream is rewritten to the codec named by @ref MediaConfig::FfmpegVideoCodec /
 * @ref MediaConfig::FfmpegAudioCodec so the planner splices an encoder
 * ahead of the muxer; an already-compressed stream is muxed as-is.
 *
 * @par H.264 / HEVC bitstream form
 * On read the @c h264_mp4toannexb / @c hevc_mp4toannexb bitstream
 * filter is applied for length-prefixed (avcC/hvcC) sources so the
 * emitted payloads carry Annex-B with in-band parameter sets — the
 * form the promeki decoders expect.  On write the inverse is done: the
 * encoder's Annex-B access units are converted to length-prefixed
 * (AVCC) samples and an @c avcC / @c hvcC configuration record is built
 * from the parameter sets and stored as the stream @c extradata, which
 * is what mp4 / Matroska require.
 *
 * @par Known limitation — compressed audio needing codec extradata
 * The writer builds each audio stream from the payload's @ref AudioDesc
 * alone and sets no codec @c extradata.  Self-describing audio muxes
 * fine — PCM (the default @ref MediaConfig::FfmpegAudioCodec), AC-3 and
 * MP3 carry everything in-band — but codecs whose container mapping
 * requires an out-of-band configuration record (AAC AudioSpecificConfig,
 * FLAC @c STREAMINFO, Opus @c OpusHead) would produce an undecodable
 * track.  Those formats are rejected at write-setup with a clear
 * @ref Error::NotSupported rather than silently writing a broken file;
 * route them through a native backend (QuickTime for mp4/mov) instead.
 *
 * @par Known limitation — ProRes in Matroska
 * Writing ProRes into a Matroska container is not yet correct: FFmpeg's
 * Matroska ProRes path uses a frame @c icpf-atom wrapping convention
 * this backend does not reproduce, so a @c .mkv it writes is not
 * decodable.  ProRes round-trips natively through @ref QuickTimeMediaIO
 * (@c .mov).  Demuxing ProRes from a container produced by other tools
 * (where the variant is recovered from the stream's profile FourCC) is
 * unaffected.
 *
 * @par Config keys
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::Filename        | String | — | Path to the container file. |
 * | @ref MediaConfig::VideoTrack      | int | -1 (auto) | Video stream index to read. |
 * | @ref MediaConfig::AudioTrack      | int | -1 (auto) | Audio stream index to read. |
 * | @ref MediaConfig::FfmpegFormat    | String | "" | Writer muxer name (empty = derive from filename). |
 * | @ref MediaConfig::FfmpegVideoCodec| VideoCodec | Invalid | Writer video codec (Invalid = passthrough). |
 * | @ref MediaConfig::FfmpegAudioCodec| AudioCodec | PCM | Writer audio codec. |
 *
 * @par Threading
 * Runs on a per-instance dedicated worker thread (inherited from
 * @ref DedicatedThreadMediaIO) so blocking container syscalls do not
 * starve the shared pool.
 */
class FfmpegMediaIO : public DedicatedThreadMediaIO {
                PROMEKI_OBJECT(FfmpegMediaIO, DedicatedThreadMediaIO)
        public:
                /** @brief Constructs an FfmpegMediaIO. */
                FfmpegMediaIO(ObjectBase *parent = nullptr);

                /** @brief Destructor.  Closes any open context. */
                ~FfmpegMediaIO() override;

                Error proposeInput(const MediaDesc &offered, MediaDesc *preferred) const override;

        protected:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandSeek &cmd) override;

        private:
                Error openReader(const MediaIOCommandOpen &cmd, MediaIOCommandOpen &out);
                Error openWriter(const MediaIOCommandOpen &cmd, MediaIOCommandOpen &out);
                Error setupWriterFromFrame(const Frame &frame);
                void  closeContexts();

                // Rewrites @p desc's image / audio descriptors to the compressed
                // formats named by MediaConfig::FfmpegVideoCodec /
                // FfmpegAudioCodec, so the planner splices encoders.  Invalid /
                // already-compressed streams are left untouched.
                void rewriteVideoForCodec(MediaDesc &desc) const;
                void rewriteAudioForCodec(MediaDesc &desc) const;

                // ---- Shared state ----
                bool   _isOpen = false;
                bool   _isWrite = false;
                String _filename;

                // ---- Reader state ----
                AVFormatContext *_inFmt = nullptr;
                AVBSFContext    *_bsf = nullptr; // h264/hevc mp4toannexb, or null
                int              _videoStream = -1;
                int              _audioStream = -1;
                FrameRate        _frameRate;
                FrameCount       _frameCount{FrameCount::unknown()};
                FrameNumber      _currentFrame{0};
                AudioDesc        _audioDesc;
                bool             _eof = false;

                // ---- Writer state ----
                AVFormatContext *_outFmt = nullptr;
                AVStream        *_outVideo = nullptr;
                AVStream        *_outAudio = nullptr;
                bool             _headerWritten = false;
                bool             _writerTracksRegistered = false;
                int64_t          _writerVideoPts = 0;
                int64_t          _writerAudioPts = 0;
};

/**
 * @brief @ref MediaIOFactory for the FFmpeg container backend.
 * @ingroup proav
 *
 * Declared as a @ref isFallback "fallback" factory so it never
 * out-races a native backend for a format the native backend owns.
 */
class FfmpegFactory : public MediaIOFactory {
        public:
                FfmpegFactory() = default;

                String name() const override { return String("FFmpeg"); }
                String displayName() const override { return String("FFmpeg (libavformat)"); }
                String description() const override {
                        return String("Generic FFmpeg container backend (Matroska/WebM, AVI, FLV, …)");
                }

                // The fallback extension list deliberately covers only
                // containers no native backend claims; .mov / .mp4 / .wav
                // etc. are intentionally absent so the native backends win
                // the auto-dispatch.  (isFallback() makes the order moot,
                // but keeping the list narrow keeps probe behaviour clear.)
                StringList extensions() const override {
                        return {String("mkv"),  String("webm"), String("avi"), String("flv"),
                                String("wmv"),  String("asf"),  String("ogv"), String("ogg"),
                                String("mpg"),  String("mpeg"), String("vob"), String("3gp"),
                                String("rm"),   String("rmvb"), String("nut"), String("mxf")};
                }

                bool canBeSource() const override { return true; }
                bool canBeSink() const override { return true; }
                bool isFallback() const override { return true; }

                bool            canHandleDevice(IODevice *device) const override;
                Config::SpecMap configSpecs() const override;

                MediaIO *create(const Config &config, ObjectBase *parent = nullptr) const override;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV && PROMEKI_ENABLE_FFMPEG
