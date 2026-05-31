/**
 * @file      mpegtsfilemediaio.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/buffer.h>
#include <promeki/file.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/mediaiofactory.h>
#include <promeki/sharedthreadmediaio.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

class MpegTsFramer;

/**
 * @brief Bidirectional MediaIO backend that reads and writes single-
 *        program MPEG-2 Transport Stream files.
 * @ingroup proav
 *
 * Provides the round-trip storage path needed to exercise
 * @ref MpegTsMuxer and @ref MpegTsDemuxer end-to-end without the
 * complexity of a network transport.  The same file written by this
 * MediaIO can be played back by any standard tool (@c ffplay,
 * @c mediainfo, @c tsduck) — useful for visual confirmation during
 * codec or pipeline bring-up.
 *
 * Internally composes a @ref File with a @ref MpegTsFramer that owns
 * the muxer / demuxer state and the Frame-level glue.  The same
 * framer is shared with the SRT MediaIO so configuration knobs and
 * behaviour stay consistent across transports.
 *
 * @par Mode support
 *
 * - @c Sink — accepts @ref Frame objects carrying one
 *   @ref CompressedVideoPayload (H.264 or HEVC) and any number of
 *   @ref CompressedAudioPayload entries (AAC).  Like
 *   @ref RawBitstreamMediaIO this is a "dumb sink": uncompressed
 *   inputs are rejected so the pipeline planner inserts a
 *   @ref VideoEncoderMediaIO / @ref AudioEncoderMediaIO upstream.
 *
 * - @c Source — reads the file in 64 KB chunks, feeds the bytes to
 *   @ref MpegTsFramer, and emits each reassembled access unit as a
 *   one-payload @c Frame.
 *
 * @par Config keys
 *
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::Filename | String | (required) | Path to read / write. |
 * | @ref MediaConfig::OpenMode | Enum   | @c Read    | @c Read = source, @c Write = sink. |
 * | @ref MediaConfig::FrameRate | FrameRate | 30/1 | Used to synthesise PTS when payloads don't carry one. |
 * | @ref MediaConfig::MpegTsVideoPid | int | 0x100 | Video PID. |
 * | @ref MediaConfig::MpegTsAudioPid | int | 0x101 | Audio PID. |
 * | @ref MediaConfig::MpegTsPmtPid   | int | 0x1000 | PMT PID. |
 * | @ref MediaConfig::MpegTsProgramNumber | int | 1 | @c program_number for PAT / PMT. |
 * | @ref MediaConfig::MpegTsPatPmtIntervalMs | int | 100 | PAT/PMT emit cadence. |
 * | @ref MediaConfig::MpegTsPcrIntervalMs | int | 20 | PCR insertion cadence. |
 * | @ref MediaConfig::MpegTsMuxRateBps | int64 | 0 | CBR target (0 = disabled). |
 * | @ref MediaConfig::MpegTsAacFraming | Enum | Adts | ADTS or LATM. |
 *
 * @par Thread Safety
 * Strand-affine — see @ref CommandMediaIO.
 *
 * @par Example
 * @code
 * MediaIO::Config cfg;
 * cfg.set(MediaConfig::Type,     "MpegTsFile");
 * cfg.set(MediaConfig::Filename, "/tmp/out.ts");
 * cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
 * cfg.set(MediaConfig::MpegTsMuxRateBps, int64_t(6'000'000));
 * MediaIO *sink = MediaIO::create(cfg);
 * sink->open(MediaIO::Sink);
 * @endcode
 */
class MpegTsFileMediaIO : public SharedThreadMediaIO {
                PROMEKI_OBJECT(MpegTsFileMediaIO, SharedThreadMediaIO)
        public:
                /** @brief int64_t — TS packets written across all PIDs. */
                static inline const MediaIOStats::ID StatsPacketsWritten{"PacketsWritten"};

                /** @brief int64_t — bytes written to the file (PSI + PES + NULL padding). */
                static inline const MediaIOStats::ID StatsBytesWritten{"BytesWritten"};

                /** @brief int64_t — Frames (access units) handed to the framer. */
                static inline const MediaIOStats::ID StatsFramesWritten{"FramesWritten"};

                /** @brief int64_t — TS packets read from the file. */
                static inline const MediaIOStats::ID StatsPacketsRead{"PacketsRead"};

                /** @brief int64_t — bytes read from the file. */
                static inline const MediaIOStats::ID StatsBytesRead{"BytesRead"};

                /** @brief int64_t — Frames emitted from the framer. */
                static inline const MediaIOStats::ID StatsFramesRead{"FramesRead"};

                /** @brief int64_t — continuity-counter discontinuities seen by the demuxer. */
                static inline const MediaIOStats::ID StatsContinuityErrors{"ContinuityErrors"};

                /** @brief int64_t — bytes the demuxer dropped while searching for a sync byte. */
                static inline const MediaIOStats::ID StatsBytesDiscarded{"BytesDiscarded"};

                MpegTsFileMediaIO(ObjectBase *parent = nullptr);
                ~MpegTsFileMediaIO() override;

                Error describe(MediaIODescription *out) const override;
                Error proposeInput(const MediaDesc &offered, MediaDesc *preferred) const override;

        protected:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandStats &cmd) override;

        private:
                Error openSink(const MediaIOCommandOpen &cmd);
                Error openSource(const MediaIOCommandOpen &cmd);
                Error pumpReader();
                void  applyFramerConfig(const MediaIO::Config &cfg);

                File   _file;
                String _filename;
                bool   _isWrite = false;
                bool   _isOpen = false;
                bool   _eof = false;

                UniquePtr<MpegTsFramer> _framer;
                Frame::List             _readQueue;

                int64_t _packetsWritten = 0;
                int64_t _bytesWritten = 0;
                int64_t _framesWritten = 0;
                int64_t _packetsRead = 0;
                int64_t _bytesRead = 0;
                int64_t _framesRead = 0;
};

/**
 * @brief @ref MediaIOFactory for the MPEG-TS file backend.
 * @ingroup proav
 */
class MpegTsFileFactory : public MediaIOFactory {
        public:
                MpegTsFileFactory() = default;

                String name() const override { return String("MpegTsFile"); }
                String displayName() const override { return String("MPEG-2 TS file"); }
                String description() const override {
                        return String("MPEG-2 Transport Stream file read / write "
                                      "backend (H.264 / HEVC + AAC).");
                }
                StringList extensions() const override {
                        return {String("ts"), String("m2ts"), String("mts")};
                }
                bool canBeSink() const override { return true; }
                bool canBeSource() const override { return true; }

                Config::SpecMap configSpecs() const override;
                MediaIO        *create(const Config &config, ObjectBase *parent = nullptr) const override;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
