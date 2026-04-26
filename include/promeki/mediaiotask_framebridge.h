/**
 * @file      mediaiotask_framebridge.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/mediaiotask.h>
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
 * Publishes the output-side UUID as the @c SourceUUID string key on
 * every frame metadata passed to downstream consumers, so logs and
 * schedulers can correlate frames back to the producing bridge
 * instance.  Additionally sets @ref Metadata::FrameBridgeTimeStamp
 * to the publisher's queue timestamp so downstream stages can
 * measure cross-process transport latency.
 *
 * @par Thread Safety
 * Strand-affine — see @ref MediaIOTask.
 */
class MediaIOTask_FrameBridge : public MediaIOTask {
        public:
                /** @brief Format descriptor for registration. */
                static MediaIO::FormatDesc formatDesc();

                /** @brief Default constructor. */
                MediaIOTask_FrameBridge();

                /** @brief Destructor — closes the bridge if open. */
                ~MediaIOTask_FrameBridge() override;

        private:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                void  cancelBlockingWork() override;

                FrameBridge::UPtr _bridge;
                bool              _isOutput = false;
};

PROMEKI_NAMESPACE_END
