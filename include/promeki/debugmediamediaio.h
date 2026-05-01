/**
 * @file      debugmediamediaio.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/debugmediafile.h>
#include <promeki/mediaiofactory.h>
#include <promeki/namespace.h>
#include <promeki/sharedthreadmediaio.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief MediaIO backend that captures a @ref Frame into a PMDF debug file.
 * @ingroup proav
 *
 * Thin adapter around @ref DebugMediaFile that plugs PMDF into the
 * @ref MediaIO command set.  Supports both source (read frames back)
 * and sink (capture to disk) — the point of PMDF is lossless frame
 * round-trip for debugging, so both modes are first-class.
 *
 * Registered with the @c "pmdf" file extension so
 * @c mediaplay @c -d @c capture.pmdf routes here automatically.
 *
 * @par Config keys
 *
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::Filename | String | (required) | PMDF file path. |
 *
 * @par Thread Safety
 * Strand-affine — see @ref SharedThreadMediaIO.
 */
class DebugMediaMediaIO : public SharedThreadMediaIO {
                PROMEKI_OBJECT(DebugMediaMediaIO, SharedThreadMediaIO)
        public:
                /** @brief int64_t — frames written to (or read from) the file. */
                static inline const MediaIOStats::ID StatsFramesWritten{"FramesWritten"};
                /** @brief int64_t — frames read from the file. */
                static inline const MediaIOStats::ID StatsFramesRead{"FramesRead"};

                /** @brief Constructs a closed, unconfigured PMDF MediaIO. */
                DebugMediaMediaIO(ObjectBase *parent = nullptr);

                /** @brief Destructor.  Closes the underlying file if still open. */
                ~DebugMediaMediaIO() override;

                Error proposeInput(const MediaDesc &offered, MediaDesc *preferred) const override;
                Error proposeOutput(const MediaDesc &requested, MediaDesc *achievable) const override;

        protected:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandSeek &cmd) override;
                Error executeCmd(MediaIOCommandStats &cmd) override;

        private:
                DebugMediaFile::UPtr _file;
                String               _filename;
                bool                 _isOpen = false;
                bool                 _isWrite = false;
                FrameCount           _framesWritten{0};
                FrameCount           _framesRead{0};
};

/**
 * @brief @ref MediaIOFactory for the PMDF debug-media backend.
 * @ingroup proav
 */
class DebugMediaFactory : public MediaIOFactory {
        public:
                DebugMediaFactory() = default;

                String name() const override { return String("PMDF"); }
                String displayName() const override { return String("Debug Media (.pmdf)"); }
                String description() const override {
                        return String("ProMEKI Debug Frame (.pmdf) — lossless Frame capture for debugging");
                }

                StringList extensions() const override { return {String("pmdf")}; }
                StringList schemes() const override { return {String("pmdf")}; }

                bool canBeSource() const override { return true; }
                bool canBeSink() const override { return true; }
                bool canHandlePath(const String &path) const override { return path.toLower().endsWith(".pmdf"); }

                Config::SpecMap configSpecs() const override;
                Error           urlToConfig(const Url &url, Config *outConfig) const override;

                MediaIO *create(const Config &config, ObjectBase *parent = nullptr) const override;
};

PROMEKI_NAMESPACE_END
