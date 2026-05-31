/**
 * @file      mpegtsdemuxer.h
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
#include <promeki/bufferview.h>
#include <promeki/error.h>
#include <promeki/function.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/mpegts.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Single-program MPEG-2 Transport Stream demuxer.
 * @ingroup proav
 *
 * Counterpart to @ref MpegTsMuxer.  Accepts arbitrary byte chunks via
 * @ref push, aligns to the @c 0x47 sync byte boundary, parses PAT and
 * PMT sections, reassembles per-PID PES packets, and emits one
 * @ref AccessUnit per complete PES via the @ref StreamCallback.
 *
 * The demuxer is stream-oriented — callers can feed bytes in any
 * granularity (single packet, large chunks read from a file, partial
 * fragments from a network read).  Internal state tracks:
 *
 *  - The 0x47 alignment search (lost on torn input).
 *  - PSI table reassembly (PAT then PMT — single-program only).
 *  - Per-PID PES reassembly using @c payload_unit_start_indicator
 *    and PES @c packet_length.
 *  - Per-PID @c continuity_counter discontinuities.
 *
 * @par Single-program assumption
 *
 * Only the first program in the PAT is followed.  Multi-program
 * streams have additional PIDs that are simply ignored.  A future
 * extension can expose program selection via a configurable program
 * number.
 *
 * @par PES @c packet_length handling
 *
 * Video PES packets typically use @c packet_length = 0 (unbounded)
 * because access units can exceed the 64 KB limit of the
 * @c packet_length field.  This demuxer detects unbounded video PES
 * via the @c stream_id (0xE0..0xEF) and finalises the access unit on
 * the next @c PUSI=1 packet for that PID.  Audio PES always set the
 * literal length and are finalised at that byte count.
 *
 * @par Thread Safety
 * Single-threaded — owned by one consumer at a time.
 *
 * @par Example
 * @code
 * MpegTsDemuxer demux;
 * demux.setStreamCallback([&](const MpegTsDemuxer::AccessUnit &au) {
 *         printf("PID=0x%04x type=0x%02x PTS=%llu size=%zu\n",
 *                au.pid, au.streamType, (unsigned long long)au.pts90k,
 *                au.payload.size());
 *         return Error::Ok;
 * });
 *
 * uint8_t buf[64 * 1024];
 * while (size_t n = readFromFile(buf, sizeof buf)) {
 *         demux.push(BufferView::wrap(buf, n));
 * }
 * demux.flush();
 * @endcode
 */
class MpegTsDemuxer {
        public:
                /**
                 * @brief One complete access unit, ready for downstream
                 *        decode.
                 *
                 * Lives only for the duration of the @ref StreamCallback
                 * invocation — the @c payload @ref Buffer is owned by
                 * the demuxer's internal reassembly state and may be
                 * recycled on return.  Callers that need to keep the
                 * bytes past the callback must copy them.
                 */
                struct AccessUnit {
                                uint16_t pid = 0;                                          ///< PID this AU came from.
                                MpegTs::StreamType streamType = MpegTs::StreamTypeReserved; ///< From PMT.
                                /**
                                 * @brief PMT @c registration_descriptor's
                                 *        @c format_identifier when the stream's
                                 *        PMT entry carried one; @c 0 otherwise.
                                 *
                                 * Disambiguates @ref MpegTs::StreamTypePrivatePes
                                 * (Opus / AV1 / SMPTE 302M all share
                                 * @c stream_type @c 0x06).  Set once at PMT
                                 * parse time and copied verbatim onto every
                                 * access unit from that PID.
                                 */
                                uint32_t           registrationFormat = 0;
                                BufferView         payload;                                 ///< Access-unit bytes.
                                bool               hasPts = false;
                                bool               hasDts = false;
                                uint64_t           pts90k = 0;
                                uint64_t           dts90k = 0;
                                bool               randomAccess = false; ///< @c random_access_indicator was set.
                                bool               dataAlignment = false; ///< PES @c data_alignment_indicator was set.
                                bool               discontinuity = false; ///< @c discontinuity_indicator was set.
                };

                /**
                 * @brief Callback invoked once per reassembled access unit.
                 *
                 * Returning a non-Ok @ref Error aborts the current
                 * @ref push invocation; the error propagates back to
                 * the caller.
                 */
                using StreamCallback = Function<Error(const AccessUnit &)>;

                /**
                 * @brief Callback invoked when a PAT or PMT update
                 *        changes the program structure.
                 *
                 * Useful for callers that want to discover stream
                 * shape (PIDs, codec types) before consuming any
                 * access units.  Optional — the demuxer works fine
                 * without one.
                 */
                using ProgramCallback = Function<void()>;

                /**
                 * @brief Callback invoked each time a Program Clock
                 *        Reference value is observed in an AF.
                 *
                 * @c pid is the PID carrying the PCR (typically the
                 * PCR PID announced in the PMT).  @c pcr27mhz is the
                 * decoded value in 27 MHz ticks.  Useful for stream
                 * clock recovery, drift measurement against a local
                 * reference, and PCR-jitter analysis under lossy
                 * transports.
                 */
                using PcrCallback = Function<void(uint16_t pid, uint64_t pcr27mhz)>;

                MpegTsDemuxer();
                ~MpegTsDemuxer();

                /** @brief Replaces the per-AU callback. */
                void setStreamCallback(StreamCallback cb) { _streamCallback = std::move(cb); }

                /** @brief Replaces the program-change callback. */
                void setProgramCallback(ProgramCallback cb) { _programCallback = std::move(cb); }

                /**
                 * @brief Replaces the PCR-observation callback.
                 *
                 * The callback is invoked synchronously from
                 * @ref push for every TS packet whose adaptation
                 * field carries a PCR.  Pass a null callback to
                 * disable.
                 */
                void setPcrCallback(PcrCallback cb) { _pcrCallback = std::move(cb); }

                /**
                 * @brief Feeds @p data into the demuxer.
                 *
                 * The bytes are scanned for sync alignment, packets
                 * are dispatched, and any complete access units that
                 * fall out of reassembly are passed to the stream
                 * callback synchronously before this call returns.
                 *
                 * @param data Bytes to feed.  Any size is accepted.
                 * @return @c Error::Ok on success, or the first non-Ok
                 *         error returned by the stream callback.
                 */
                Error push(const BufferView &data);

                /**
                 * @brief Drains any in-flight PES whose end-of-frame
                 *        marker was the next-PUSI (i.e. unbounded
                 *        video PES whose successor never arrived).
                 *
                 * Call once after the producer stops feeding bytes
                 * (end-of-file, connection closed).  Any final
                 * pending video AU is flushed to the stream callback.
                 */
                Error flush();

                /**
                 * @brief Returns the current program's PMT PID, or
                 *        @c MpegTs::PidNull when no PAT has been
                 *        observed yet.
                 */
                uint16_t pmtPid() const { return _pmtPid; }

                /**
                 * @brief Returns the current program number, or 0 when
                 *        no PAT has been observed yet.
                 */
                uint16_t programNumber() const { return _programNumber; }

                /**
                 * @brief Returns the current PCR PID, or
                 *        @c MpegTs::PidNull when no PMT has been
                 *        observed yet.
                 */
                uint16_t pcrPid() const { return _pcrPid; }

                /**
                 * @brief Read-only descriptor for one elementary stream
                 *        the demuxer has discovered from the PMT.
                 */
                struct StreamInfo {
                                uint16_t           pid = 0;
                                MpegTs::StreamType streamType = MpegTs::StreamTypeReserved;
                                /**
                                 * @brief @c format_identifier from the
                                 *        stream's @c registration_descriptor,
                                 *        or @c 0 when absent.  See
                                 *        @ref AccessUnit::registrationFormat.
                                 */
                                uint32_t           registrationFormat = 0;
                };

                /**
                 * @brief Returns the list of streams currently
                 *        published by the PMT.
                 *
                 * Empty until the first PMT is parsed.
                 */
                List<StreamInfo> streams() const;

                /**
                 * @brief Returns the count of TS packets observed so
                 *        far whose @c continuity_counter advanced by
                 *        an unexpected amount.
                 */
                uint64_t continuityErrors() const { return _continuityErrors; }

                /**
                 * @brief Returns the count of bytes the demuxer has
                 *        discarded while searching for a sync byte
                 *        after losing alignment.
                 */
                uint64_t bytesDiscarded() const { return _bytesDiscarded; }

        private:
                /**
                 * @brief Per-PID PSI reassembly state.
                 *
                 * Buffers section bytes across as many TS packets as a
                 * single section spans.  ISO/IEC 13818-1 §2.4.4.1
                 * allows a single PSI section to straddle multiple
                 * 188-byte TS packets when the section exceeds
                 * 183 bytes (the first packet's payload room after
                 * the pointer_field).
                 */
                struct PsiReasm {
                                Buffer  buffer;
                                size_t  writePos = 0;
                                size_t  expectedTotal = 0; // 0 = not yet known.
                };

                struct PesReasm {
                                MpegTs::StreamType streamType = MpegTs::StreamTypeReserved;
                                Buffer             buffer;              ///< Growing reassembly buffer.
                                size_t             writePos = 0;
                                size_t             expectedTotal = 0;   ///< From PES packet_length; 0 = unbounded.
                                bool               inProgress = false;
                                bool               unbounded = false;
                                bool               hasPts = false;
                                bool               hasDts = false;
                                uint64_t           pts90k = 0;
                                uint64_t           dts90k = 0;
                                bool               randomAccess = false;
                                bool               dataAlignment = false;
                                bool               discontinuity = false;
                                uint8_t            continuityCounter = 0;
                                bool               haveCc = false;
                };

                Error processPacket(const uint8_t *p);
                Error processPsiPacket(const uint8_t *payload, size_t payloadLen, uint16_t pid, bool pusi);
                Error dispatchPsiSection(uint16_t pid, const uint8_t *section, size_t len);
                Error parsePat(const uint8_t *section, size_t len);
                Error parsePmt(const uint8_t *section, size_t len);
                Error finalizePes(uint16_t pid, PesReasm &pr);
                Error startNewPes(uint16_t pid, PesReasm &pr, const uint8_t *pesStart, size_t pesAvail,
                                  bool randomAccess, bool discontinuity);

                StreamCallback  _streamCallback;
                ProgramCallback _programCallback;
                PcrCallback     _pcrCallback;

                // Sync-alignment ring buffer.  Holds up to (188 - 1)
                // bytes carried over from one push() to the next.
                uint8_t _carry[MpegTs::PacketSize];
                size_t  _carrySize = 0;

                uint16_t _programNumber = 0;
                uint16_t _pmtPid = MpegTs::PidNull;
                uint16_t _pcrPid = MpegTs::PidNull;
                bool     _havePat = false;
                bool     _havePmt = false;

                Map<uint16_t, MpegTs::StreamType> _streamTypeByPid;
                Map<uint16_t, uint32_t>           _registrationByPid;
                Map<uint16_t, PesReasm>           _pes;
                Map<uint16_t, PsiReasm>           _psi;

                uint64_t _continuityErrors = 0;
                uint64_t _bytesDiscarded = 0;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
