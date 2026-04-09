/**
 * @file      quicktime_atom.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Internal header for the QuickTime / ISO-BMFF atom parser. Not part
 * of the public API — included only by the QuickTime backend sources
 * under src/proav/.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <promeki/namespace.h>
#include <promeki/fourcc.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class IODevice;

namespace quicktime_atom {

// ---------------------------------------------------------------------------
// Well-known atom (box) types.
//
// Only the handful actually needed by the reader skeleton is defined
// here; additional constants are added as later phases need them.
// ---------------------------------------------------------------------------

constexpr FourCC kFtyp{"ftyp"};  ///< File type box (ISO-BMFF) / "ftyp" atom (QT).
constexpr FourCC kMoov{"moov"};  ///< Movie box.
constexpr FourCC kMvhd{"mvhd"};  ///< Movie header.
constexpr FourCC kTrak{"trak"};  ///< Track box.
constexpr FourCC kTkhd{"tkhd"};  ///< Track header.
constexpr FourCC kEdts{"edts"};  ///< Edit list container.
constexpr FourCC kElst{"elst"};  ///< Edit list.
constexpr FourCC kMdia{"mdia"};  ///< Media box.
constexpr FourCC kMdhd{"mdhd"};  ///< Media header.
constexpr FourCC kHdlr{"hdlr"};  ///< Handler reference.
constexpr FourCC kMinf{"minf"};  ///< Media information container.
constexpr FourCC kStbl{"stbl"};  ///< Sample table.
constexpr FourCC kStsd{"stsd"};  ///< Sample description.
constexpr FourCC kStts{"stts"};  ///< Time-to-sample.
constexpr FourCC kCtts{"ctts"};  ///< Composition time offsets.
constexpr FourCC kStsz{"stsz"};  ///< Sample sizes.
constexpr FourCC kStsc{"stsc"};  ///< Sample-to-chunk.
constexpr FourCC kStco{"stco"};  ///< 32-bit chunk offsets.
constexpr FourCC kCo64{"co64"};  ///< 64-bit chunk offsets.
constexpr FourCC kStss{"stss"};  ///< Sync samples (keyframes).
constexpr FourCC kUdta{"udta"};  ///< User data container.
constexpr FourCC kMdat{"mdat"};  ///< Media data.
constexpr FourCC kFree{"free"};  ///< Free space (skippable).
constexpr FourCC kSkip{"skip"};  ///< Skippable free space.
constexpr FourCC kWide{"wide"};  ///< Reserved padding before mdat (QT).

// Fragmented MP4 / movie fragments
constexpr FourCC kMvex{"mvex"};  ///< Movie extends (required for fragmented files).
constexpr FourCC kMehd{"mehd"};  ///< Movie extends header (optional total duration).
constexpr FourCC kTrex{"trex"};  ///< Track extends (per-track fragment defaults).
constexpr FourCC kMoof{"moof"};  ///< Movie fragment box.
constexpr FourCC kMfhd{"mfhd"};  ///< Movie fragment header.
constexpr FourCC kTraf{"traf"};  ///< Track fragment box.
constexpr FourCC kTfhd{"tfhd"};  ///< Track fragment header.
constexpr FourCC kTfdt{"tfdt"};  ///< Track fragment decode time.
constexpr FourCC kTrun{"trun"};  ///< Track fragment run.
constexpr FourCC kSidx{"sidx"};  ///< Segment index.
constexpr FourCC kMfra{"mfra"};  ///< Movie fragment random access.

// Handler types that appear in the hdlr atom
constexpr FourCC kHdlrVide{"vide"};  ///< Video handler.
constexpr FourCC kHdlrSoun{"soun"};  ///< Sound handler.
constexpr FourCC kHdlrTmcd{"tmcd"};  ///< Timecode handler.
constexpr FourCC kHdlrSbtl{"sbtl"};  ///< Subtitle handler (MP4).
constexpr FourCC kHdlrText{"text"};  ///< Text/subtitle handler (QT).
constexpr FourCC kHdlrMeta{"meta"};  ///< Metadata handler.

/**
 * @brief Box header parsed from the wire.
 *
 * Box layout on disk:
 *   [4B size][4B type]         — 32-bit form
 *   [4B size=1][4B type][8B largeSize] — 64-bit form
 *   [4B size=0] ...            — box extends to EOF
 *
 * @c headerOffset is the absolute offset of the first size byte;
 * @c payloadOffset points to the first byte after the header;
 * @c payloadSize is the number of data bytes within the box.
 * @c endOffset is payloadOffset + payloadSize (absolute file offset).
 */
struct Box {
        FourCC  type{'\0','\0','\0','\0'};
        int64_t headerOffset = 0;   ///< Absolute offset of the size field.
        int64_t payloadOffset = 0;  ///< First byte after the header.
        int64_t payloadSize = 0;    ///< Number of payload bytes.
        int64_t endOffset = 0;      ///< payloadOffset + payloadSize.

        bool isValid() const { return type.value() != 0 && payloadSize >= 0; }
};

// ---------------------------------------------------------------------------
// Big-endian reader over an IODevice.
//
// QuickTime and MP4 atoms store all multi-byte integers big-endian.
// ReadStream owns no state beyond the device pointer and a tiny error
// flag, so callers can create it on the stack.
// ---------------------------------------------------------------------------

/**
 * @brief Streaming big-endian reader over an IODevice.
 *
 * Every read advances the device. If any read fails or hits EOF before
 * satisfying the request, the stream enters a sticky error state
 * (isError() returns true) and subsequent reads return zero.
 */
class ReadStream {
        public:
                explicit ReadStream(IODevice *dev) : _dev(dev) {}

                /** @brief Returns true if any prior read failed. */
                bool     isError() const { return _error; }

                /** @brief Sets the error flag. Used for internal error propagation. */
                void     setError() { _error = true; }

                /** @brief Returns the underlying device. */
                IODevice *device() const { return _dev; }

                /** @brief Returns the current device position. */
                int64_t  pos() const;

                /** @brief Seeks to an absolute device offset. */
                Error    seek(int64_t offset);

                /** @brief Reads @p n raw bytes into @p out; returns Error::Ok on success. */
                Error    readBytes(void *out, int64_t n);

                /** @brief Skips @p n bytes. */
                Error    skip(int64_t n);

                uint8_t  readU8();
                uint16_t readU16();
                uint32_t readU32();
                uint64_t readU64();
                int16_t  readS16() { return static_cast<int16_t>(readU16()); }
                int32_t  readS32() { return static_cast<int32_t>(readU32()); }
                int64_t  readS64() { return static_cast<int64_t>(readU64()); }

                /** @brief Reads a four-character code as a FourCC. */
                FourCC   readFourCC();

                /** @brief Reads 16.16 fixed-point, returned as double. */
                double   readFixed16_16();

                /** @brief Reads 8.8 fixed-point, returned as double. */
                double   readFixed8_8();

        private:
                IODevice *_dev = nullptr;
                bool      _error = false;
};

/**
 * @brief Reads the next box header starting at the stream's current position.
 *
 * On success, @p box is populated and the stream is positioned at
 * @c box.payloadOffset (ready to read the box payload). If @p enforceEnd
 * is non-zero the function will refuse to report a box whose end would
 * exceed that limit (used when walking inside a parent box).
 *
 * @return Error::Ok on success; Error::EndOfFile if there are no more
 *         bytes to read; Error::Malformed on an invalid box header.
 */
Error readBoxHeader(ReadStream &stream, Box &box, int64_t enforceEnd = 0);

/**
 * @brief Helper: seek the stream to the start of the next sibling box.
 *
 * After a caller finishes processing a box's payload (possibly
 * partially), this positions the stream at @c box.endOffset so the next
 * @c readBoxHeader yields the sibling.
 */
Error advanceToSibling(ReadStream &stream, const Box &box);

/**
 * @brief Scans the top-level box list looking for the first box with
 *        the requested type.
 *
 * Starts at @p startOffset and walks forward to @p endOffset, reading
 * box headers and skipping non-matching boxes. On success, @p out is
 * populated and the stream is left positioned at @c out.payloadOffset.
 *
 * @return Error::Ok on match; Error::NotFound if the box does not exist;
 *         other error on I/O failure.
 */
Error findTopLevelBox(ReadStream &stream, FourCC type,
                      int64_t startOffset, int64_t endOffset, Box &out);

// ---------------------------------------------------------------------------
// AtomWriter — in-memory big-endian byte buffer with nested-box helpers.
//
// Used by the QuickTime writer to compose moov / trak / stbl atoms.
// Boxes are emitted via beginBox() / endBox() pairs which patch the
// 32-bit size field at the box's start once the contents are known.
// ---------------------------------------------------------------------------

class AtomWriter {
        public:
                AtomWriter() = default;

                /** @brief Returns the current write position. */
                size_t pos() const { return _data.size(); }

                /** @brief Returns const access to the accumulated bytes. */
                const List<uint8_t> &data() const { return _data; }

                /** @brief Releases the accumulated bytes by move. */
                List<uint8_t> takeData() { List<uint8_t> out = std::move(_data); _data.clear(); return out; }

                void writeU8(uint8_t v);
                void writeU16(uint16_t v);
                void writeU24(uint32_t v);   ///< low 24 bits, big-endian
                void writeU32(uint32_t v);
                void writeU64(uint64_t v);
                void writeS16(int16_t v)  { writeU16(static_cast<uint16_t>(v)); }
                void writeS32(int32_t v)  { writeU32(static_cast<uint32_t>(v)); }
                void writeS64(int64_t v)  { writeU64(static_cast<uint64_t>(v)); }
                void writeFourCC(FourCC fc);
                void writeFixed16_16(double v);
                void writeFixed8_8(double v);
                void writeBytes(const void *p, size_t n);
                void writeZeros(size_t n);

                /** @brief Writes a Pascal-style fixed-width string: 1-byte length
                 *         followed by content padded to @p totalBytes - 1 bytes. */
                void writePascalString(const String &s, size_t totalBytes);

                /** @brief Patches a 32-bit big-endian value at the given offset. */
                void patchU32(size_t offset, uint32_t v);

                /** @brief Marker returned by beginBox() and consumed by endBox(). */
                struct Marker {
                        size_t sizeOffset = 0;
                };

                /**
                 * @brief Starts a new box: writes a placeholder 32-bit size and
                 *        the 4-byte type, returns a marker for endBox().
                 */
                Marker beginBox(FourCC type);

                /** @brief Closes a box opened by beginBox(), patching its size. */
                void endBox(Marker m);

                /**
                 * @brief Writes a fixed-form box header for "full" boxes that
                 *        include version+flags. Helper used by per-atom builders.
                 */
                Marker beginFullBox(FourCC type, uint8_t version, uint32_t flags) {
                        Marker m = beginBox(type);
                        writeU8(version);
                        writeU24(flags);
                        return m;
                }

        private:
                List<uint8_t> _data;
};

/**
 * @brief Decodes a 16-bit packed ISO 639-2/T language code.
 *
 * The ISO-BMFF @c mdhd atom stores languages as three 5-bit values
 * packed into a 16-bit big-endian word, with each value equal to the
 * lowercase ASCII letter minus 0x60. Returns a 3-character ASCII
 * string like "eng" or the empty string if the code is unset / invalid.
 */
String decodeLanguage(uint16_t packed);

/**
 * @brief Converts a QuickTime Mac epoch time (seconds since 1904-01-01)
 *        into a Unix epoch (seconds since 1970-01-01), saturating at
 *        zero if the input predates the Unix epoch.
 */
int64_t macEpochToUnix(uint64_t macSeconds);

} // namespace quicktime_atom

PROMEKI_NAMESPACE_END
