/**
 * @file      mediaiotask_debugmedia.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <memory>
#include <promeki/namespace.h>
#include <promeki/mediaiotask.h>

PROMEKI_NAMESPACE_BEGIN

class DebugMediaFile;

/**
 * @brief MediaIO backend that captures a @ref Frame into a PMDF debug file.
 * @ingroup proav
 *
 * Thin adapter around @ref DebugMediaFile that plugs PMDF into the
 * @ref MediaIO command set.  Supports both @c MediaIO::Source (read
 * frames back) and @c MediaIO::Sink (capture to disk) — the point
 * of PMDF is lossless frame round-trip for debugging, so both
 * modes are first-class.
 *
 * Registered with the @c "pmdf" file extension so
 * @c mediaplay @c -d @c capture.pmdf routes here automatically.
 *
 * @par Config keys
 *
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::Filename | String | (required) | PMDF file path. |
 */
class MediaIOTask_DebugMedia : public MediaIOTask {
        public:
                /** @brief int64_t — frames written to (or read from) the file. */
                static inline const MediaIOStats::ID StatsFramesWritten{"FramesWritten"};
                /** @brief int64_t — frames read from the file. */
                static inline const MediaIOStats::ID StatsFramesRead{"FramesRead"};

                /** @brief Returns the format descriptor for this backend. */
                static MediaIO::FormatDesc formatDesc();

                MediaIOTask_DebugMedia();
                ~MediaIOTask_DebugMedia() override;

        private:
                Error executeCmd(MediaIOCommandOpen &cmd)  override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd)  override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandSeek &cmd)  override;
                Error executeCmd(MediaIOCommandStats &cmd) override;

                Error proposeInput(const MediaDesc &offered,
                                   MediaDesc *preferred) const override;
                Error proposeOutput(const MediaDesc &requested,
                                    MediaDesc *achievable) const override;

                std::unique_ptr<DebugMediaFile>  _file;
                String                           _filename;
                MediaIO::Mode                    _mode = MediaIO::NotOpen;
                int64_t                          _framesWritten = 0;
                int64_t                          _framesRead    = 0;
};

PROMEKI_NAMESPACE_END
