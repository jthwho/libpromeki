/**
 * @file      pcapreader.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/datetime.h>
#include <promeki/enums_pcap.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class IODevice;

/**
 * @brief One captured frame yielded by @ref PcapReader::next.
 * @ingroup network
 *
 * A record is the container's view of a single captured frame: the
 * arrival timestamp, the link-layer type needed to demux it, and a
 * zero-copy @ref BufferView of the captured bytes.  The view aliases
 * the reader's single backing @ref Buffer — it stays valid as long as
 * the reader (or any copy of the view) is alive; no per-record
 * allocation occurs.
 *
 * @par Two notions of "truncated"
 * @ref snapTruncated marks a frame whose capture was limited by the
 * capture tool's snap length (@ref capturedLength < @ref
 * originalLength) — this is normal and the record is fully valid.
 * It is unrelated to @c Error::TruncatedData, which @ref
 * PcapReader::next returns when the *file itself* ends mid-record.
 */
struct PcapRecord {
                /// @brief Wall-clock arrival time (system-clock / Unix
                ///        epoch).  Invalid (@ref DateTime::isValid is
                ///        @c false) for records that carry no timestamp,
                ///        e.g. a pcapng Simple Packet Block.
                DateTime captureTime;

                /// @brief Link-layer type of @ref frame.  For pcapng this
                ///        is the type of the interface the packet was
                ///        captured on, so a file mixing link types per
                ///        interface still decodes correctly.
                PcapLinkType linkType;

                /// @brief Zero-copy view of the captured bytes (length ==
                ///        @ref capturedLength).  Aliases the reader's
                ///        backing buffer.
                BufferView frame;

                /// @brief Full on-the-wire length of the frame before any
                ///        snap-length truncation.
                uint32_t originalLength = 0;

                /// @brief @c true when the captured bytes are fewer than
                ///        @ref originalLength because the capture tool's
                ///        snap length cut the frame short.
                bool snapTruncated = false;

                /// @brief Number of bytes actually captured (== @c frame.size()).
                size_t capturedLength() const { return frame.size(); }
};

/**
 * @brief Streaming reader for classic pcap and pcapng capture files.
 * @ingroup network
 *
 * @c PcapReader is the container front-end of the offline ingest path:
 * it parses the file framing and hands up one @ref PcapRecord at a
 * time, leaving link-layer / IP / UDP demux to a later stage.  It
 * understands:
 *
 *  - **Classic pcap** — both byte orders, and both microsecond
 *    (@c 0xa1b2c3d4) and nanosecond (@c 0xa1b23c4d) timestamp magics.
 *  - **pcapng** — Section Header, Interface Description, Enhanced
 *    Packet, and Simple Packet blocks; per-interface link type and
 *    timestamp resolution (@c if_tsresol); multiple sections; and
 *    forward-compatible skipping of unrecognised block types.
 *
 * @par Zero-copy
 * The whole capture is read once into a single backing @ref Buffer and
 * every @ref PcapRecord::frame is a @ref BufferView into it.  No
 * allocation happens per record, which matters because ST 2110
 * captures grow large quickly.
 *
 * @par Source requirements
 * The input must be a sized, seekable device (a @ref File or an
 * in-memory buffer).  Non-seekable / streaming sources and compressed
 * containers are out of scope for this revision.
 *
 * @par Error model
 * @ref next returns @c Error::EndOfFile at a clean end of capture, and
 * @c Error::TruncatedData when the final record or block is cut short
 * by the end of the file (a capture stopped mid-write).  A
 * structurally invalid but fully-present record yields
 * @c Error::CorruptData.
 *
 * @par Example
 * @code
 * PcapReader reader;
 * if(reader.openFile("capture.pcapng").isError()) return;
 * for(;;) {
 *         auto [rec, err] = reader.next();
 *         if(err == Error::EndOfFile) break;
 *         if(err.isError()) break; // truncated or corrupt
 *         // rec.linkType / rec.frame ready to hand to the demux
 * }
 * @endcode
 */
class PcapReader {
        public:
                /// @brief Hard upper bound on a single captured record's
                ///        length, guarding against a corrupt length field
                ///        sending the parser off the rails.
                static constexpr size_t MaxRecordLength = 256u * 1024u * 1024u;

                /// @brief Classic pcap global-header magic (microsecond
                ///        timestamps, file's native byte order).
                static constexpr uint32_t MagicMicros = 0xa1b2c3d4u;

                /// @brief Classic pcap global-header magic (nanosecond
                ///        timestamps, file's native byte order).
                static constexpr uint32_t MagicNanos = 0xa1b23c4du;

                /// @brief pcapng Section Header Block type / byte-order
                ///        magic (the SHB body magic that fixes endianness).
                static constexpr uint32_t PngBlockShb = 0x0a0d0d0au;
                static constexpr uint32_t PngByteOrderMagic = 0x1a2b3c4du;
                static constexpr uint32_t PngBlockIdb = 0x00000001u; ///< Interface Description Block.
                static constexpr uint32_t PngBlockSpb = 0x00000003u; ///< Simple Packet Block.
                static constexpr uint32_t PngBlockEpb = 0x00000006u; ///< Enhanced Packet Block.

                /** @brief Constructs an unopened reader. */
                PcapReader() = default;

                /**
                 * @brief Reads a capture from a sized, seekable device and
                 *        parses its container header.
                 *
                 * The device's full contents are copied once into the
                 * reader's backing buffer; the device is not retained and
                 * may be closed by the caller afterwards.
                 *
                 * @param device An open, readable, seekable device.
                 * @return @c Error::Ok, or @c Error::NotSupported for a
                 *         non-seekable source, @c Error::CorruptData /
                 *         @c Error::TruncatedData for a bad header, or an
                 *         I/O error.
                 */
                Error open(IODevice &device);

                /**
                 * @brief Convenience: open a capture file by path.
                 * @param path Filesystem (or resource) path to a
                 *             @c .pcap / @c .pcapng file.
                 * @return As @ref open, plus @c Error::OpenFailed if the
                 *         file cannot be opened.
                 */
                Error openFile(const String &path);

                /**
                 * @brief Parse a capture already resident in memory.
                 *
                 * Shares (does not copy) @p buf as the backing buffer, so
                 * every yielded @ref PcapRecord::frame aliases it.
                 *
                 * @param buf A buffer holding a complete capture image.
                 * @return As @ref open.
                 */
                Error openBuffer(const Buffer &buf);

                /** @brief True once a container header has been parsed. */
                bool isOpen() const { return _format != PcapFileFormat::Unknown; }

                /** @brief The detected container format. */
                PcapFileFormat format() const { return _format; }

                /** @brief The detected byte order (per first section for pcapng). */
                PcapByteOrder byteOrder() const { return _byteOrder; }

                /**
                 * @brief The capture's link-layer type.
                 *
                 * For classic pcap this is the single global link type.
                 * For pcapng it is the first interface's link type
                 * (@c PcapLinkType::Null if no interface has been seen
                 * yet); per-record link type is always authoritative via
                 * @ref PcapRecord::linkType.
                 */
                PcapLinkType linkType() const;

                /**
                 * @brief The capture's snap length in bytes.
                 *
                 * Classic pcap: the global snaplen.  pcapng: the first
                 * interface's snaplen (0 if none seen yet).
                 */
                uint32_t snapLength() const;

                /** @brief Number of interfaces seen so far (always 1 for classic pcap). */
                size_t interfaceCount() const { return _interfaces.size(); }

                /**
                 * @brief Pull the next captured frame.
                 * @return A @ref PcapRecord with @c Error::Ok; or
                 *         @c Error::EndOfFile at a clean end;
                 *         @c Error::TruncatedData on a cut-off final
                 *         record/block; @c Error::CorruptData on a
                 *         structurally invalid record; @c Error::NotOpen
                 *         if no capture has been opened.
                 */
                Result<PcapRecord> next();

                /** @brief Resets the reader to the unopened state. */
                void close();

        private:
                /// @brief Per-interface state extracted from a pcapng
                ///        Interface Description Block.
                struct Interface {
                                PcapLinkType linkType = PcapLinkType::Ethernet;
                                uint32_t snapLength = 0;
                                /// @brief @c if_tsresol byte: high bit set =>
                                ///        2^-n s/tick, else 10^-n s/tick.
                                ///        Default 6 (microseconds).
                                uint8_t tsResolCode = 6;
                };

                Error parseHeader();
                Error parseClassicHeader();
                Error parsePcapngFirstSection();
                Result<PcapRecord> nextClassic();
                Result<PcapRecord> nextPcapng();
                Error consumePcapngIdb(size_t bodyOff, size_t bodyLen);

                Buffer _backing;                  ///< Whole-file image; frames view into this.
                size_t _pos = 0;                  ///< Parse cursor into @ref _backing.
                size_t _size = 0;                 ///< Logical size of @ref _backing.
                PcapFileFormat _format = PcapFileFormat::Unknown;
                PcapByteOrder _byteOrder = PcapByteOrder::Unknown;
                bool _be = false;                 ///< Cached: byte order is big-endian.
                bool _nanoTs = false;             ///< Classic: timestamps are nanosecond (else microsecond).
                uint32_t _snaplen = 0;            ///< Classic global snaplen.
                PcapLinkType _classicLink = PcapLinkType::Ethernet;
                List<Interface> _interfaces;      ///< pcapng interface table for the current section.
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
