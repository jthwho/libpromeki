/**
 * @file      framebridgemediaio.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/sharedthreadmediaio.h>
#include <promeki/mediaiofactory.h>
#include <promeki/framebridge.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief MediaIO backend that publishes/consumes frames via @ref FrameBridge.
 * @ingroup proav
 *
 * A thin adapter that exposes @ref FrameBridge through the standard
 * @ref MediaIO command interface.  Both @c Input and @c Output modes
 * are supported — @c Output constructs a bridge with the ring shape
 * derived from config + @c pendingMediaDesc + @c pendingAudioDesc;
 * @c Input connects by name and learns the shape from the handshake.
 *
 * @par Config keys
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::FrameBridgeName                    | String  | —      | Logical bridge name (required). |
 * | @ref MediaConfig::FrameBridgeRingDepth               | int32   | 2      | Ring-buffer depth. |
 * | @ref MediaConfig::FrameBridgeMetadataReserveBytes    | int32   | 65536  | Reserved metadata bytes per slot. |
 * | @ref MediaConfig::FrameBridgeAudioHeadroomFraction   | double  | 0.20   | Audio capacity headroom. |
 * | @ref MediaConfig::FrameBridgeAccessMode              | int32   | 0600   | POSIX file mode for shm + socket. |
 * | @ref MediaConfig::FrameBridgeGroupName               | String  | ""     | Group for cross-user access. |
 * | @ref MediaConfig::FrameBridgeSyncMode                | bool    | true   | Input-side sync (ACK every TICK). |
 * | @ref MediaConfig::FrameBridgeWaitForConsumer         | bool    | true   | Output blocks writeFrame until a consumer attaches. |
 *
 * @par Stats
 * Publishes the output-side instance name as the @c SourceName string
 * key on every frame metadata passed to downstream consumers, so logs
 * and schedulers can correlate frames back to the producing bridge
 * instance.  Additionally sets @ref Metadata::FrameBridgeTimeStamp
 * to the publisher's queue timestamp so downstream stages can
 * measure cross-process transport latency.
 *
 * @par Thread Safety
 * Strand-affine — see @ref CommandMediaIO.
 */
class FrameBridgeMediaIO : public SharedThreadMediaIO {
                PROMEKI_OBJECT(FrameBridgeMediaIO, SharedThreadMediaIO)
        public:
                FrameBridgeMediaIO(ObjectBase *parent = nullptr);

                /** @brief Destructor — closes the bridge if open. */
                ~FrameBridgeMediaIO() override;

                void cancelBlockingWork() override;

        protected:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;

        private:
                FrameBridge::UPtr _bridge;
                bool              _isOutput = false;
};

/**
 * @brief @ref MediaIOFactory for the FrameBridge IPC backend.
 * @ingroup proav
 */
class FrameBridgeFactory : public MediaIOFactory {
        public:
                FrameBridgeFactory() = default;

                String name() const override { return String("FrameBridge"); }
                String displayName() const override { return String("Frame Bridge (IPC)"); }
                String description() const override {
                        return String("Cross-process frame transport via shared memory + UNIX-domain handshake");
                }
                StringList schemes() const override { return {String("pmfb")}; }
                bool       canBeSource() const override { return true; }
                bool       canBeSink() const override { return true; }

                Config::SpecMap configSpecs() const override;
                Error           urlToConfig(const Url &url, Config *outConfig) const override;
                MediaIO        *create(const Config &config, ObjectBase *parent = nullptr) const override;
};

PROMEKI_NAMESPACE_END
