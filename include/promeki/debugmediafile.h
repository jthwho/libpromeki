/**
 * @file      debugmediafile.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/objectbase.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/frame.h>
#include <promeki/framenumber.h>
#include <promeki/framecount.h>
#include <promeki/metadata.h>
#include <promeki/list.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

class File;

/**
 * @brief Debug-grade media container that captures a @ref Frame
 *        byte-for-byte.
 * @ingroup proav
 *
 * PMDF (ProMEKI Debug Frame) is a bespoke container built for
 * debugging: the intent is to capture every bit of information a
 * @ref Frame carries — every image plane, every audio sample,
 * every metadata key, every attached packet — and then
 * read it back producing a structurally identical @ref Frame.
 * It is @em not an interoperable format; even between promeki
 * versions the reader enforces the major-version field on the
 * file signature.
 *
 * The class is usable standalone — open a file for
 * @ref Mode::Write, push frames through @ref writeFrame, and call
 * @ref close on shutdown.  The @ref DebugMediaMediaIO
 * adapter wraps it for the @ref MediaIO framework.
 *
 * @par File layout (current version = 1)
 *
 * ```
 *   [32-byte file signature]
 *   [SESN chunk — session-level metadata, written once]
 *   [FRAM chunk — one per frame, nested IMAG / AUDO sub-chunks]
 *   ... more FRAM chunks ...
 *   [TOC  chunk — optional, written on clean close]
 *   [ENDF chunk — optional, written on clean close]
 * ```
 *
 * Every chunk uses a 16-byte header (@c fourCC + @c flags +
 * @c uint64 @c payloadSize) so an unknown @c fourCC can be
 * skipped via @c seek(pos + 16 + payloadSize).  Structured
 * payloads flow through @ref DataStream (which gives us
 * length-prefixed strings, per-item tags, and native
 * @ref VariantDatabase binary encoding for keyed metadata);
 * raw image / audio buffers are written verbatim so reads are
 * zero-copy.
 *
 * @par Crash resistance
 *
 * FRAM chunks are self-contained.  A writer crash in the middle
 * of a FRAM leaves that chunk truncated, but every earlier frame
 * is fully readable.  The reader, on finding a truncated trailing
 * chunk, reports EOF at the last good frame.  @c TOC and @c ENDF
 * are only written on a graceful @ref close; their absence
 * triggers a linear scan to rebuild the index.
 *
 * @par Extensibility
 *
 * New metadata keys flow through automatically (the wire format
 * carries string-named keys).  New chunk types are skipped by
 * old readers via the 16-byte-header size field.  Per-buffer
 * @c flags words reserve space for future optional compression
 * (zstd, lz4, ...).
 *
 * @par Thread Safety
 * Thread-affine via @ref ObjectBase.  An instance must be used on the
 * thread that created it; cross-thread interaction goes through
 * @ref ObjectBase signal/slot dispatch.
 */
class DebugMediaFile : public ObjectBase {
                PROMEKI_OBJECT(DebugMediaFile, ObjectBase)
        public:
                /** @brief Unique-ownership pointer to a DebugMediaFile. */
                using UPtr = UniquePtr<DebugMediaFile>;

                /** @brief Open mode. */
                enum Mode {
                        NotOpen = 0,
                        Read,
                        Write
                };

                /**
                 * @brief Optional open-time tunables.
                 *
                 * Defaults are safe for general use; callers only set
                 * fields they care about.
                 */
                struct OpenOptions {
                                /// @brief Metadata stamped into the session
                                ///        chunk on @c Write.  Ignored on @c Read.
                                Metadata sessionInfo;
                };

                /** @brief One entry in the frame index. */
                struct FrameIndexEntry {
                                int64_t     fileOffset = 0; ///< Byte offset of the FRAM chunk header.
                                FrameNumber frameNumber;    ///< Frame index recorded in the chunk.
                                int64_t     presentationUs =
                                        0; ///< Presentation time in microseconds (reserved for future use, currently always 0).
                };

                /** @brief The file-signature magic (8 bytes). */
                static constexpr char kMagic[8] = {'P', 'M', 'D', 'F', '\x1A', '\x0A', '\x00', '\x00'};

                /** @brief Current format version stamped on write. */
                static constexpr uint32_t kFormatVersion = 1;

                /** @brief File-flags bit: TOC/ENDF were written on clean close. */
                static constexpr uint32_t kFileFlagHasFooter = 0x00000001;

                /** @brief Constructs an unopened DebugMediaFile. */
                explicit DebugMediaFile(ObjectBase *parent = nullptr);

                /** @brief Closes the file and releases resources. */
                ~DebugMediaFile() override;

                DebugMediaFile(const DebugMediaFile &) = delete;
                DebugMediaFile &operator=(const DebugMediaFile &) = delete;

                /**
                 * @brief Opens @p filename in the given mode.
                 *
                 * On @c Write, writes the file signature and the
                 * @c SESN chunk using
                 * @ref OpenOptions::sessionInfo immediately.  On
                 * @c Read, validates the signature and caches the
                 * session info from the @c SESN chunk.
                 *
                 * @param filename Target file path.
                 * @param mode     @c Read or @c Write.
                 * @param opts     Open-time tunables.
                 * @return @c Error::Ok on success.
                 */
                Error open(const String &filename, Mode mode, const OpenOptions &opts = {});

                /**
                 * @brief Closes the file.
                 *
                 * On @c Write, appends a @c TOC chunk (if any frames
                 * were written) and an @c ENDF chunk, and stamps the
                 * @ref kFileFlagHasFooter bit in the file-signature
                 * flags field.  Safe to call multiple times; calling
                 * on an unopened instance is a no-op.
                 *
                 * @return @c Error::Ok on success.
                 */
                Error close();

                /** @brief Returns true if the file is open. */
                bool isOpen() const { return _mode != NotOpen; }

                /** @brief Returns the open mode. */
                Mode mode() const { return _mode; }

                /** @brief Returns the filename this instance was opened against. */
                const String &filename() const { return _filename; }

                // ---- Write API ----

                /**
                 * @brief Appends @p frame to the file.
                 *
                 * Serialises the frame's metadata, config update,
                 * every video payload (ImageDesc plus the uncompressed
                 * plane bytes or the CompressedVideoPayload blob) and
                 * every audio payload (AudioDesc plus the uncompressed
                 * sample bytes or the CompressedAudioPayload blob) into
                 * a single FRAM chunk.  Valid only in @c Mode::Write.
                 *
                 * @param frame The frame to write.  Must be valid.
                 * @return @c Error::Ok on success, or an I/O error.
                 */
                Error writeFrame(const Frame::Ptr &frame);

                /** @brief Number of frames written since open, or the last read frame index + 1 on read. */
                FrameCount framesWritten() const { return _framesWritten; }

                // ---- Read API ----

                /**
                 * @brief Reads the next frame at the current position.
                 *
                 * Valid only in @c Mode::Read.  Returns
                 * @c Error::EndOfFile when the file is exhausted.
                 *
                 * @param out Receives the deserialised frame.
                 * @return @c Error::Ok, @c Error::EndOfFile, or an
                 *         I/O / corruption error.
                 */
                Error readFrame(Frame::Ptr &out);

                /**
                 * @brief Reads the frame with the given zero-based index.
                 *
                 * Builds the index lazily on first call (via TOC if
                 * present, else linear scan) and uses it to seek to
                 * the requested frame.
                 *
                 * @param frameNumber The zero-based frame number.
                 * @param out         Receives the frame on success.
                 * @return @c Error::Ok on success, @c Error::EndOfFile
                 *         when @p frameNumber is past the end, or an
                 *         I/O / corruption error.
                 */
                Error readFrameAt(const FrameNumber &frameNumber, Frame::Ptr &out);

                /**
                 * @brief Positions the next @ref readFrame at
                 *        @p frameNumber.
                 *
                 * @param frameNumber The zero-based frame number.
                 * @return @c Error::Ok on success or
                 *         @c Error::IllegalSeek when the target is
                 *         past the last indexed frame.
                 */
                Error seek(const FrameNumber &frameNumber);

                /** @brief Session info from the @c SESN chunk (read mode). */
                const Metadata &sessionInfo() const { return _sessionInfo; }

                /** @brief Total frame count (uses TOC if present, else linear scan). */
                FrameCount frameCount() const;

                /** @brief True when the file signature indicates a TOC was written on close. */
                bool hasFooter() const { return (_fileFlags & kFileFlagHasFooter) != 0; }

                /**
                 * @brief Returns the lazy frame-offset index, building
                 *        it on first access.
                 */
                const List<FrameIndexEntry> &index() const;

        private:
                String                        _filename;
                Mode                          _mode = NotOpen;
                File                         *_file = nullptr; ///< Owned via ObjectBase parent-child tree.
                uint32_t                      _fileFlags = 0;
                uint32_t                      _fileVersion = 0;
                int64_t                       _firstFramePos = 0; ///< File position right after SESN chunk.
                FrameCount                    _framesWritten{0};
                mutable FrameNumber           _readCursor{0}; ///< Current frame index when reading.
                mutable bool                  _indexBuilt = false;
                mutable List<FrameIndexEntry> _index;
                Metadata                      _sessionInfo;

                Error writeSignature(uint32_t flags);
                Error writeSessionChunk(const Metadata &sessionInfo);
                Error readSignature();
                Error readSessionChunk();
                Error appendFooter();
                Error buildIndex() const;
};

PROMEKI_NAMESPACE_END
