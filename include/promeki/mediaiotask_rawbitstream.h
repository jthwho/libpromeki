/**
 * @file      mediaiotask_rawbitstream.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/mediaiotask.h>
#include <promeki/file.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief MediaIO sink that writes encoded packet payloads verbatim to a file.
 * @ingroup proav
 *
 * @c MediaIOTask_RawBitstream is the minimum possible backend for
 * capturing the output of a @ref MediaIOTask_VideoEncoder — every
 * @ref VideoPacket on an incoming Frame is appended to the target
 * file exactly as it arrived, with no container framing, no
 * timestamps, no muxing, and no modification of the payload.  The
 * resulting file is a raw elementary stream — for H.264 / HEVC
 * output from NVENC, that's Annex-B byte-stream form, which is
 * exactly what @c ffplay / @c ffprobe / @c mpv will accept.
 *
 * Registered with the extension list @c "h264" / @c "h265" /
 * @c "hevc" so @c mediaplay's file-path auto-detection routes
 * @c -d foo.h264 through this backend without requiring the caller
 * to say @c RawBitstream explicitly.
 *
 * Only sink mode (@c MediaIO::Sink) is supported in this first
 * iteration — reading an elementary-stream file back into
 * @ref VideoPacket "VideoPackets" requires a codec-specific NAL-unit
 * parser that belongs with the decoder work.
 *
 * @par Config keys
 *
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::Filename | String | (required) | Output file path. |
 *
 * @par Example
 * @code
 * // Capture NVENC output as a raw H.264 elementary stream:
 * MediaIO::Config cfg;
 * cfg.set(MediaConfig::Type,     "RawBitstream");
 * cfg.set(MediaConfig::Filename, "/tmp/out.h264");
 * MediaIO *sink = MediaIO::create(cfg);
 * sink->open(MediaIO::Sink);
 * @endcode
 */
class MediaIOTask_RawBitstream : public MediaIOTask {
        public:
                /** @brief int64_t — total packets successfully written. */
                static inline const MediaIOStats::ID StatsPacketsWritten{"PacketsWritten"};

                /** @brief int64_t — total bytes appended to the file. */
                static inline const MediaIOStats::ID StatsBytesWritten{"BytesWritten"};

                /** @brief Returns the format descriptor for this backend. */
                static MediaIO::FormatDesc formatDesc();

                MediaIOTask_RawBitstream() = default;
                ~MediaIOTask_RawBitstream() override;

        private:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandStats &cmd) override;

                Error describe(MediaIODescription *out) const override;
                Error proposeInput(const MediaDesc &offered,
                                   MediaDesc *preferred) const override;

                File      _file;
                String    _filename;
                int64_t   _packetsWritten = 0;
                int64_t   _bytesWritten = 0;
                bool      _warnedNoPackets = false;
};

PROMEKI_NAMESPACE_END
