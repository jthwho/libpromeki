/**
 * @file      srtgroup.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_SRT
#include <promeki/iodevice.h>
#include <promeki/error.h>
#include <promeki/socketaddress.h>
#include <promeki/string.h>
#include <promeki/list.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Caller-side SRT bonded-socket group (multi-path redundancy).
 * @ingroup network
 *
 * An SrtGroup ties several outbound SRT paths together so the
 * application sees one logical reliable stream even when the
 * underlying network is multipath.  libsrt brokers the membership
 * internally — every @ref read returns one copy of each delivered
 * message regardless of which path carried it, and every
 * @ref write fans out (broadcast) or steers (backup) according to
 * @ref Type.
 *
 * @par Topology in scope
 *
 * Caller side only.  Listener-side bonding (where a single listener
 * accepts connections from multiple paths and groups them
 * automatically) is not exposed in this phase.  Each member of the
 * group is one outbound (source-addr, peer-addr) pair, listed at
 * @ref connect time.
 *
 * @par Type semantics
 *
 *  - @ref Broadcast: every member carries every datagram.  The
 *    receiver picks the first copy that arrives in time and drops
 *    duplicates.  Doubles wire bandwidth but tolerates outright
 *    loss on any one path.
 *  - @ref Backup: only the highest-weight path that is up carries
 *    traffic; libsrt fails over to the next path on link loss.
 *    Cheaper on bandwidth, slower to recover (one-RTT switchover).
 *
 * @par Encryption / latency / streamid
 *
 * Group-level options set via this class apply uniformly to every
 * member at @ref connect time, the same way a non-bonded
 * @ref SrtSocket inherits them through @ref SrtServer.  Per-member
 * weighting / different streamids per path are not exposed in this
 * phase — they belong in a multi-tenant deployment helper, not
 * here.
 *
 * @par Thread safety
 *
 * Inherits @ref IODevice — thread-affine.  Different SrtGroup
 * instances are independent and may be used concurrently from
 * different threads.
 *
 * @par Example
 * @code
 * SrtGroup grp(SrtGroup::Broadcast);
 * grp.setLatency(120);
 * grp.open(IODevice::ReadWrite);
 * SrtGroup::MemberList paths;
 * paths.pushToBack({SocketAddress(), SocketAddress(Ipv4Address(10,0,0,1), 4200)});
 * paths.pushToBack({SocketAddress(), SocketAddress(Ipv4Address(10,1,0,1), 4200)});
 * grp.connect(paths);
 * grp.write(payload, payloadSize);
 * @endcode
 */
class SrtGroup : public IODevice {
                PROMEKI_OBJECT(SrtGroup, IODevice)
        public:
                /** @brief Unique-ownership pointer to a SrtGroup. */
                using UPtr = UniquePtr<SrtGroup>;

                /** @brief Sentinel for an uninitialized group SRTSOCKET. */
                static constexpr int InvalidHandle = -1;

                /** @brief Group bonding mode. */
                enum Type {
                        Broadcast, ///< All members carry every datagram (redundancy).
                        Backup     ///< One active member; fail over on link loss.
                };

                /** @brief One member endpoint, listed at @ref connect time. */
                struct Member {
                                /** @brief Local source address (optional — empty = let kernel pick). */
                                SocketAddress sourceAddress;
                                /** @brief Remote peer address (required). */
                                SocketAddress peerAddress;
                };

                /** @brief List of members for @ref connect. */
                using MemberList = ::promeki::List<Member>;

                /** @brief Per-member status, returned by @ref memberStatus. */
                struct MemberStatus {
                                /** @brief Member SRTSOCKET handle. */
                                int           handle = InvalidHandle;
                                /** @brief Resolved peer address. */
                                SocketAddress peerAddress;
                                /** @brief Connection state (Connected / Broken / etc.). */
                                int           state = 0;
                                /** @brief Membership status (Pending / Idle / Running / Broken). */
                                int           membership = 0;
                                /** @brief Backup-mode weight; 0 in Broadcast mode. */
                                int           weight = 0;
                };

                /** @brief List of per-member status entries. */
                using MemberStatusList = ::promeki::List<MemberStatus>;

                /**
                 * @brief Constructs a fresh, unopened group.
                 * @param type    Bonding mode.
                 * @param parent  Optional parent ObjectBase.
                 */
                explicit SrtGroup(Type type = Broadcast, ObjectBase *parent = nullptr);

                /**
                 * @brief Adopts an existing group SRTSOCKET (listener-side mirror).
                 *
                 * libsrt's group-mirror feature auto-creates a server
                 * group when a bonded caller connects in.  Listener
                 * code that wants to read on the group level
                 * (instead of polling individual member sockets via
                 * @ref SrtSocket) wraps the group handle in an
                 * adopted @ref SrtGroup using this ctor.  Discover
                 * the handle via @ref SrtSocket::groupHandle on the
                 * first accepted member; subsequent members of the
                 * same group share the same id.
                 *
                 * @par Lifetime
                 * The adopted group owns the SRTSOCKET — destruction
                 * closes the group, which tears down every member.
                 * Make sure all related @ref SrtSocket instances
                 * have been closed (or simply discarded) before the
                 * adopted SrtGroup goes away.
                 *
                 * @param adoptHandle Group @c SRTSOCKET to adopt.
                 * @param type        The group's bonding mode (must
                 *                    match the actual libsrt group;
                 *                    @ref Broadcast is the default
                 *                    libsrt picks when the caller
                 *                    creates a Broadcast group).
                 * @param parent      Optional parent ObjectBase.
                 */
                SrtGroup(int adoptHandle, Type type, ObjectBase *parent = nullptr);

                /** @brief Destructor.  Closes the group if open. */
                ~SrtGroup() override;

                /** @brief Returns the configured bonding type. */
                Type type() const { return _type; }

                /**
                 * @brief Creates the group SRTSOCKET via @c srt_create_group.
                 *
                 * Pre-connect options buffered on the SrtGroup are
                 * applied as part of @ref connect, not here.
                 *
                 * @param mode The open mode (typically @c ReadWrite).
                 * @return @ref Error::Ok or @ref Error::LibraryFailure.
                 */
                Error open(OpenMode mode) override;

                /** @brief Closes the group and tears down every member. */
                Error close() override;

                /** @brief Returns true while the group is open. */
                bool isOpen() const override;

                /**
                 * @brief Connects all members in @p members in parallel.
                 *
                 * libsrt fires each member's handshake on its own
                 * worker; the call returns once *all* members have
                 * either finished handshaking or failed.  Subsequent
                 * @ref read / @ref write transparently span every
                 * connected member.
                 *
                 * @param members Member list (1..N entries).
                 * @return @ref Error::Ok if at least one member came
                 *         up, an error otherwise.
                 */
                Error connect(const MemberList &members);

                /** @copydoc SrtSocket::read */
                int64_t read(void *data, int64_t maxSize) override;

                /** @copydoc SrtSocket::write */
                int64_t write(const void *data, int64_t maxSize) override;

                /** @brief Returns true — SRT is sequential. */
                bool isSequential() const override { return true; }

                /** @brief Returns the group SRTSOCKET handle. */
                int handle() const { return _sock; }

                /** @brief Returns the human-readable text of the last SRT error. */
                String lastSrtError() const { return _lastError; }

                /**
                 * @brief Returns per-member status snapshots.
                 *
                 * Wraps @c srt_group_data.  Each entry's @c state /
                 * @c membership values match libsrt's @c SRT_SOCKSTATUS
                 * and @c SRT_MEMBERSTATUS enums.
                 */
                MemberStatusList memberStatus() const;

                // ---- Pre-connect group options (applied at @ref connect) ----

                /** @copydoc SrtSocket::setLatency */
                Error setLatency(int ms);

                /** @copydoc SrtSocket::setPassphrase */
                Error setPassphrase(const String &passphrase);

                /** @copydoc SrtSocket::setEncryptionKeyLength */
                Error setEncryptionKeyLength(int bytes);

                /** @copydoc SrtSocket::setStreamId */
                Error setStreamId(const String &id);

                /** @copydoc SrtSocket::setPayloadSize */
                Error setPayloadSize(int bytes);

                /** @copydoc SrtSocket::setMaxBandwidth */
                Error setMaxBandwidth(int64_t bytesPerSec);

                /** @copydoc SrtSocket::setReceiveTimeout */
                Error setReceiveTimeout(int timeoutMs);

                /** @copydoc SrtSocket::setSendTimeout */
                Error setSendTimeout(int timeoutMs);

                /** @copydoc SrtSocket::setConnectTimeout */
                Error setConnectTimeout(int timeoutMs);

        protected:
                /**
                 * @brief Captures the current libsrt error string.
                 */
                void captureLastError();

                /**
                 * @brief Plays cached pre-connect options onto the group socket.
                 *
                 * Called from @ref connect (the group socket exists
                 * after @ref open but options must still be set
                 * before connect for libsrt to honour them on the
                 * member handshakes).
                 */
                Error applyPreConnectOptions();

        private:
                int     _sock = InvalidHandle;
                Type    _type = Broadcast;
                String  _passphrase;
                String  _streamId;
                String  _lastError;
                int     _latencyMs = 120;
                int     _payloadSize = 0;
                int     _pbKeyLen = 0;
                int     _connectTimeoutMs = 0;
                int     _recvTimeoutMs = 0;
                int     _sendTimeoutMs = 0;
                int64_t _maxBw = 0;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_SRT
