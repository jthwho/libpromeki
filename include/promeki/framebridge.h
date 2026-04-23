/**
 * @file      framebridge.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/objectbase.h>
#include <promeki/error.h>
#include <promeki/string.h>
#include <promeki/uniqueptr.h>
#include <promeki/uuid.h>
#include <promeki/mediadesc.h>
#include <promeki/audiodesc.h>
#include <promeki/frame.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Cross-process frame transport over shared memory + local sockets.
 * @ingroup proav
 *
 * A FrameBridge publishes or consumes @ref Frame objects across a
 * process boundary on the same host.  The @em output side creates a
 * shared memory ring of frame slots and a control socket; @em input
 * sides connect to the control socket by name, map the shm read-only,
 * and read slots as new frames are announced.
 *
 * Each input opts into one of two pacing modes at @ref openInput
 * time:
 *
 * - @b Synchronous (default): the input must acknowledge every
 *   TICK before the output's @ref writeFrame returns.  The output's
 *   cadence is bounded by the slowest sync input, which makes
 *   back-pressured pipelines trivial.  Sync ACKs are emitted from
 *   inside @ref readFrame after a fresh slot has been consumed.
 *
 * - @b Asynchronous (no-sync): the input never acknowledges and
 *   the output never waits for it.  Readers that fall more than
 *   @ref Config::ringDepth frames behind miss frames — signalled
 *   via @ref framesMissed — and resynchronize on the next TICK.
 *
 * Mixing modes is supported: an output may have both sync and
 * no-sync inputs attached, and only the sync inputs gate the
 * writer.
 *
 * @par Waiting for consumers
 * With @ref Config::waitForConsumer set, the output-side
 * @ref writeFrame blocks until at least one consumer is
 * connected instead of silently discarding frames.  This
 * matches the common "producer stalls until downstream is
 * ready" pattern without requiring callers to poll
 * @ref connectionCount themselves.
 *
 * @par Wire protocol
 * The control channel uses @ref KlvFrame framing.  A @c HELO / @c ACPT
 * handshake establishes wire-version compatibility and carries a
 * @c config_hash that rejects readers whose expected shape does not
 * match the publisher.  Per-frame @c TICK messages announce new slot
 * publications.  See the class-level Doxygen on @ref KlvFrame for
 * details of the framing; the protocol identifiers are documented in
 * the implementation.
 *
 * @par Versioning
 * Each instance has a fresh per-create @ref UUID, used for logs and
 * cross-host identification.  The shm header carries a wire-format
 * version; major mismatches are rejected at handshake time, minor
 * mismatches are accepted with a log entry.
 *
 * @par MVP scope
 * The MVP supports one image plane group (one @c ImageDesc in the
 * @c MediaDesc) and one audio track.  Multi-image and multi-audio
 * frames are planned work.  Writes are always-copy — a future
 * extension will add zero-copy via a dedicated @c MemSpace.
 *
 * @par Cross-user access
 * Pass a non-default @c Config::accessMode (for example @c 0660)
 * and a @c Config::groupName that both ends share.  The underlying
 * shm object and socket file are @c chmod'd and @c chown'd at
 * @ref openOutput time.
 */
class FrameBridge : public ObjectBase {
        PROMEKI_OBJECT(FrameBridge, ObjectBase)
        public:
                /** @brief Unique-ownership pointer to a FrameBridge. */
                using UPtr = UniquePtr<FrameBridge>;

                /** @brief Wire-protocol major version. Mismatches are rejected. */
                static constexpr uint32_t WireMajor = 1;

                /** @brief Wire-protocol minor version. Mismatches are accepted with a log. */
                static constexpr uint32_t WireMinor = 0;

                /** @brief Default metadata reserve per slot (64 KiB). */
                static constexpr size_t DefaultMetadataReserveBytes = 64u * 1024u;

                /** @brief Default extra audio capacity as a fraction of nominal. */
                static constexpr double DefaultAudioHeadroomFraction = 0.20;

                /** @brief Default ring depth (ping/pong). */
                static constexpr int DefaultRingDepth = 2;

                /** @brief Default shared memory / socket file mode (owner-only). */
                static constexpr uint32_t DefaultAccessMode = 0600;

                /**
                 * @brief Output-side configuration.
                 *
                 * Only @ref openOutput consumes this struct.  Input-side
                 * openers learn the effective config from the shm header
                 * and the @c ACPT handshake.
                 */
                struct Config {
                        /** @brief Video description: frame rate, images, metadata. */
                        MediaDesc mediaDesc;

                        /** @brief Audio track description. */
                        AudioDesc audioDesc;

                        /** @brief Ring depth — number of slots in the shm buffer. */
                        int ringDepth = DefaultRingDepth;

                        /** @brief Reserved metadata bytes per slot. */
                        size_t metadataReserveBytes = DefaultMetadataReserveBytes;

                        /** @brief Extra audio capacity fraction above worst-case per-frame. */
                        double audioHeadroomFraction = DefaultAudioHeadroomFraction;

                        /** @brief POSIX file mode for the shm and socket objects. */
                        uint32_t accessMode = DefaultAccessMode;

                        /** @brief Optional group to @c chown both objects to (empty = skip). */
                        String groupName;

                        /**
                         * @brief Block @ref writeFrame until a consumer is connected.
                         *
                         * When @c true (the default) the first
                         * @ref writeFrame call after opening (and any
                         * later call made while no consumers are
                         * attached) blocks, servicing accepts and
                         * pruning dead peers, until at least one
                         * consumer is connected.  This matches the
                         * common "producer stalls until downstream is
                         * ready" pattern and prevents silently dropping
                         * frames before any consumer connects.
                         * @ref close aborts the wait and causes
                         * @ref writeFrame to return @c Error::NotOpen.
                         *
                         * Set to @c false when the publisher is the
                         * source of truth for pacing (for example a
                         * live capture that must run whether anyone
                         * is listening or not); @ref writeFrame then
                         * returns immediately and becomes a silent
                         * no-op when no consumers are attached.
                         */
                        bool waitForConsumer = true;
                };

                /**
                 * @brief Constructs a FrameBridge.
                 * @param parent Optional parent @ref ObjectBase.
                 */
                FrameBridge(ObjectBase *parent = nullptr);

                /** @brief Destructor — closes if open. */
                ~FrameBridge() override;

                /**
                 * @brief Opens this instance as the @em output for @p name.
                 *
                 * Creates the shm ring, listens on the control socket,
                 * and generates a fresh per-instance @ref UUID.  Fails
                 * if the name is already in use.
                 *
                 * @param name   Logical output name (shared with inputs).
                 * @param config Ring shape, audio headroom, permissions, etc.
                 * @return @c Error::Ok on success, or an error.
                 */
                Error openOutput(const String &name, const Config &config);

                /**
                 * @brief Opens this instance as an @em input for @p name.
                 *
                 * Connects to the output's control socket, performs the
                 * handshake, and maps the shm read-only.  Learns
                 * @ref mediaDesc, @ref audioDesc, @ref uuid, and
                 * @ref ringDepth from the handshake.
                 *
                 * @param name Logical output name to attach to.
                 * @param sync When @c true (the default) the output waits
                 *             for this input to acknowledge each published
                 *             frame before returning from @ref writeFrame.
                 *             When @c false the output never waits — the
                 *             input must keep up on its own or lose frames.
                 * @return @c Error::Ok on success, or an error.
                 */
                Error openInput(const String &name, bool sync = true);

                /**
                 * @brief Returns the input-side sync mode selected at @ref openInput.
                 *
                 * Meaningful only on the input side; returns @c true for
                 * outputs (which do not themselves have a sync mode —
                 * sync is a per-input choice).
                 */
                bool isSyncInput() const;

                /** @brief Closes the bridge. */
                void close();

                /**
                 * @brief Interrupts any in-flight blocking wait.
                 *
                 * Thread-safe: may be called from any thread at any
                 * time.  Sets an atomic flag that causes a
                 * @ref writeFrame currently blocked inside a
                 * @ref Config::waitForConsumer wait (or waiting on
                 * sync-input acknowledgements) to return
                 * @c Error::Cancelled promptly.  Later @ref writeFrame
                 * calls also return @c Error::Cancelled until the
                 * bridge is closed and reopened, so the caller sees a
                 * clean stop rather than silently resuming.
                 *
                 * The typical use is a signal / quit handler that
                 * needs to break the producer out of its wait so the
                 * owning pipeline can close cleanly.  See the
                 * @ref MediaIOTask "MediaIO wrapper"'s
                 * @c cancelBlockingWork hook for the framework-level
                 * integration.
                 */
                void abort();

                /** @brief Returns true if the bridge is open (either side). */
                bool isOpen() const;

                /** @brief Returns true if this instance is the output (master). */
                bool isOutput() const;

                /** @brief Returns the per-instance UUID. */
                const UUID &uuid() const;

                /** @brief Returns the logical name used at @c openOutput / @c openInput. */
                const String &name() const;

                /** @brief Returns the effective video description. */
                const MediaDesc &mediaDesc() const;

                /** @brief Returns the effective audio description. */
                const AudioDesc &audioDesc() const;

                /** @brief Returns the number of ring slots. */
                int ringDepth() const;

                /**
                 * @brief Returns the timestamp of the most recent frame.
                 *
                 * On the @em output side this is the steady-clock time
                 * captured just before the frame was published to the
                 * ring (i.e. placed in the output queue).  On the
                 * @em input side this is the publisher's timestamp
                 * carried by the most recently drained TICK.  Returns
                 * a default-constructed (epoch) @ref TimeStamp when no
                 * frame has been published/received yet.
                 *
                 * @return The last frame's publish timestamp.
                 */
                TimeStamp lastFrameTimeStamp() const;

                // ========== Output-side ==========

                /**
                 * @brief Publishes a frame to all connected inputs.
                 *
                 * Serializes the frame's images, audio, and metadata
                 * into the next ring slot, increments the slot sequence
                 * counter twice (seqlock), and sends a @c TICK to every
                 * connected input.  A no-op (other than processing
                 * accepts and drops) when zero inputs are connected.
                 *
                 * @param frame The frame to publish (must be compatible
                 *              with the configured @ref mediaDesc and
                 *              @ref audioDesc).
                 * @return @c Error::Ok on success, @c Error::OutOfRange
                 *         when audio or metadata exceeds slot capacity,
                 *         or another error.
                 */
                Error writeFrame(const Frame::Ptr &frame);

                /** @brief Returns the number of currently-connected inputs. */
                size_t connectionCount() const;

                /**
                 * @brief Accepts pending inputs and prunes disconnected ones.
                 *
                 * @ref writeFrame already services accepts implicitly, so
                 * typical usage does not need this.  Call it when a publisher
                 * needs to accept new inputs while not actively writing
                 * frames — for example while waiting for the first frame
                 * to arrive from upstream, or in tests.
                 *
                 * No-op when not open as an output.
                 */
                void service();

                // ========== Input-side ==========

                /**
                 * @brief Reads the most recent frame available.
                 *
                 * Uses the last received TICK to locate the slot, then
                 * seqlock-validates the copy.  Returns a null Ptr with
                 * @c Error::Ok when no TICK has been seen yet.
                 *
                 * @param err Optional error output.
                 * @return The frame, or a null Ptr.
                 */
                Frame::Ptr readFrame(Error *err = nullptr);

                /**
                 * @brief Signal emitted each time a TICK is received.
                 * @signal
                 */
                PROMEKI_SIGNAL(frameAvailable);

                /**
                 * @brief Signal emitted when the input detects the ring
                 *        lapped us.
                 *
                 * Argument: the number of frames skipped.  After this
                 * fires the input resynchronizes to the newest slot.
                 *
                 * @signal
                 */
                PROMEKI_SIGNAL(framesMissed, uint64_t);

                /**
                 * @brief Signal emitted on either side when the peer
                 *        disappears (EOF on the control socket).
                 * @signal
                 */
                PROMEKI_SIGNAL(peerDisconnected);

        private:
                class Impl;
                using ImplPtr = UniquePtr<Impl>;
                ImplPtr _d;
};

PROMEKI_NAMESPACE_END
