/**
 * @file      mediaiotask_imagefile.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/mediaiotask.h>
#include <promeki/imagefile.h>
#include <promeki/audiofile.h>
#include <promeki/audiodesc.h>
#include <promeki/pixeldesc.h>
#include <promeki/mediadesc.h>
#include <promeki/metadata.h>
#include <promeki/framerate.h>
#include <promeki/size2d.h>
#include <promeki/filepath.h>
#include <promeki/enum.h>
#include <promeki/numname.h>

PROMEKI_NAMESPACE_BEGIN

class ImageFileIO;

/**
 * @brief MediaIOTask backend for image files and image sequences.
 * @ingroup proav
 *
 * Wraps the ImageFile / ImageFileIO subsystem to provide read and
 * write access to image files through the MediaIO interface.
 * Supported single-image formats include DPX, Cineon, TGA, SGI,
 * PNM, PNG, and raw YUV variants.
 *
 * @par Single-image mode
 *
 * When the filename resolves to a plain file (no mask placeholder,
 * no @c .imgseq extension), the backend treats it as a single-frame
 * source or sink:
 *
 * - Readers load the file on @c open() and deliver the same cached
 *   frame on every subsequent read.  With the backend's default step
 *   of 0, reads continue indefinitely; with step != 0, a second read
 *   returns EndOfFile.
 * - Writers save exactly one file per @c writeFrame() call; each
 *   call overwrites the target file.
 * - @c canSeek is @c false, @c frameCount is 1 (reader) or 0 (writer).
 *
 * @par Sequence mode
 *
 * When the filename contains a mask placeholder — either hash-style
 * (@c "shot_####.dpx", @c "shot_#.dpx") or printf-style
 * (@c "shot_%04d.dpx", @c "shot_%d.dpx") — or refers to an
 * @c .imgseq JSON sidecar file, the backend treats it as an image
 * sequence:
 *
 * - Readers scan the containing directory once on @c open() to
 *   detect the head and tail frame numbers (unless the sidecar
 *   supplies them), then lazily load each requested frame on read.
 *   @c canSeek is @c true and @c frameCount is the length of the
 *   detected range.
 * - Writers synthesize the output filename for each @c writeFrame()
 *   call from the mask, starting at the configured head (default 1).
 * - Step, reverse playback and @c seekToFrame() all work as expected.
 *
 * The format of each individual image in the sequence is taken from
 * the mask's suffix extension (e.g. @c ".dpx" maps to
 * @c ImageFile::DPX).
 *
 * @par The .imgseq sidecar
 *
 * An @c .imgseq file is a small JSON document describing one
 * sequence in its containing directory.  See the @c ImgSeq class for
 * the full schema.  The pattern inside the sidecar can use either
 * mask syntax and is resolved relative to the sidecar's directory.
 * The sidecar can also supply an optional frame rate, video size,
 * pixel descriptor, and sequence-level metadata.  Multiple sidecars
 * may coexist in a single directory, each describing a different
 * sequence.
 *
 * @par Frame rate
 *
 * Still images have no intrinsic temporal rate, so the backend
 * reports a frame rate chosen by a priority chain:
 *
 *   1. Writer: @c cmd.pendingMediaDesc.frameRate() if valid.
 *   2. @c ImgSeq sidecar @c frameRate (reader only).
 *   3. @ref MediaConfig::FrameRate (always populated — the backend's
 *      default config pre-seeds it with @c DefaultFrameRate).
 *
 * The resolved source is recorded in the returned metadata under
 * @c Metadata::FrameRateSource as one of @c "file" (from a sidecar
 * or writer-supplied @c MediaDesc) or @c "config" (from the
 * @ref MediaConfig::FrameRate entry, whether it came from the backend
 * default or a caller override).  Callers that care about whether a
 * real frame rate was available can inspect that metadata entry.
 *
 * @par Config keys
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::Filename         | String    | — | File path, mask, or @c .imgseq sidecar. |
 * | @ref MediaConfig::ImageFileID      | int       | Invalid | Explicit ImageFile::ID override. |
 * | @ref MediaConfig::VideoSize        | Size2Du32 | 0x0 | Image size hint for headerless formats. |
 * | @ref MediaConfig::VideoPixelFormat | PixelDesc | — | Pixel description for headerless formats. |
 * | @ref MediaConfig::FrameRate        | FrameRate | 30/1 | Reported frame rate for the still image or sequence. |
 * | @ref MediaConfig::SequenceHead     | int       | 1 | First frame number for a sequence writer. |
 * | @ref MediaConfig::SaveImgSeqEnabled | bool      | true | Enable automatic @c .imgseq sidecar (read + write). |
 * | @ref MediaConfig::SaveImgSeqPath   | String    | — | Override path for @c .imgseq sidecar (empty = auto-derive from pattern). |
 * | @ref MediaConfig::SaveImgSeqPathMode | Enum @ref ImgSeqPathMode | Relative | Whether sidecar dir reference is relative or absolute. |
 * | @ref MediaConfig::SidecarAudioEnabled | bool   | true | Enable automatic sidecar Broadcast WAV (read + write). |
 * | @ref MediaConfig::SidecarAudioPath  | String   | — | Override path for the sidecar audio file (empty = auto-derive from pattern). |
 * | @ref MediaConfig::AudioSource       | Enum @ref AudioSourceHint | Sidecar | Preferred audio source when reading (sidecar vs. embedded). |
 */
class MediaIOTask_ImageFile : public MediaIOTask {
        public:
                /** @brief Default frame rate when no config or caller override is supplied. */
                static inline const FrameRate DefaultFrameRate{FrameRate::FPS_30};

                /** @brief Default head index for sequence writers. */
                static inline constexpr int DefaultSequenceHead = 1;

                /**
                 * @brief Returns the format descriptor for this backend.
                 *
                 * Legacy accessor that returns the @c "ImageFile"
                 * umbrella entry — it covers every extension any
                 * @ref ImageFileIO advertises.  New code should walk
                 * @ref MediaIO::registeredFormats and pick up the
                 * per-backend @c ImgSeqXxx entries, which carry
                 * accurate per-format capability flags (e.g. Cineon's
                 * @c canBeSink=false).
                 *
                 * @return A FormatDesc covering all supported image file extensions.
                 */
                static MediaIO::FormatDesc formatDesc();

                /**
                 * @brief Builds a @ref MediaIO::FormatDesc for a single @ref ImageFileIO backend.
                 *
                 * Driven by @p io 's own @c extensions(), @c canLoad(),
                 * @c canSave(), and @c mediaIoName() accessors — no
                 * hard-coded per-format table.  Called by
                 * @ref ImageFileIO::registerImageFileIO to piggy-back
                 * a MediaIO registration onto every ImageFileIO
                 * backend registration, which keeps the two registries
                 * in sync without suffering from static-init ordering
                 * across translation units.
                 *
                 * @param io The backend to describe.  Must be non-null.
                 * @return A populated @ref MediaIO::FormatDesc.
                 */
                static MediaIO::FormatDesc buildFormatDescFor(const ImageFileIO *io);

                /** @brief Constructs a MediaIOTask_ImageFile. */
                MediaIOTask_ImageFile() = default;

                /** @brief Destructor. */
                ~MediaIOTask_ImageFile() override;

        private:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandSeek &cmd) override;

                Error proposeInput(const MediaDesc &offered,
                                   MediaDesc *preferred) const override;

                // Maps the extension in @p filename to the preferred
                // writer PixelDesc for that format, taking the
                // source's bit depth into account so we don't drop
                // precision unnecessarily (e.g. 10-bit source ->
                // 10-bit DPX, not 8-bit).  Returns an invalid
                // PixelDesc when no extension-specific preference
                // applies; the proposeInput override then accepts
                // whatever was offered.
                PixelDesc preferredWriterPixelDesc(const String &filename,
                                                   const PixelDesc &source) const;

                Error openSingle(MediaIOCommandOpen &cmd, MediaDesc &mediaDesc,
                                 const String &frSource);
                Error openSequence(MediaIOCommandOpen &cmd, MediaDesc &mediaDesc,
                                   const String &frSource);
                Error readSingle(MediaIOCommandRead &cmd);
                Error readSequence(MediaIOCommandRead &cmd);
                Error writeSingle(MediaIOCommandWrite &cmd);
                Error writeSequence(MediaIOCommandWrite &cmd);

                Error writeImgSeqSidecar();

                // Common state
                String          _filename;          ///< @brief Raw config filename (single file, mask, or .imgseq).
                int             _imageFileID = ImageFile::Invalid;
                MediaIOMode     _mode = MediaIO_NotOpen;
                bool            _sequenceMode = false;
                Metadata        _writeContainerMetadata; ///< @brief Container metadata merged into each written frame (writer only).
                MediaConfig     _ioConfig;          ///< @brief Open-time MediaConfig forwarded to ImageFileIO load/save calls.
                bool            _saveImgSeqEnabled = true; ///< @brief Config-level switch for .imgseq sidecar.
                String          _saveImgSeqPath;    ///< @brief Path for .imgseq sidecar written on close (writer only).
                Enum            _saveImgSeqPathMode; ///< @brief Relative or Absolute path mode for the sidecar.
                FrameRate       _writerFrameRate;   ///< @brief Resolved frame rate stashed for the sidecar.

                // Single-file state
                Frame::Ptr      _frame;
                int64_t         _readCount = 0;
                int64_t         _writeCount = 0;
                bool            _loaded = false;

                // Sequence state
                NumName         _seqName;            ///< @brief Filename pattern for the sequence.
                FilePath        _seqDir;             ///< @brief Directory containing the sequence files.
                int64_t         _seqHead = 0;        ///< @brief First frame number (inclusive).
                int64_t         _seqTail = 0;        ///< @brief Last frame number (inclusive).
                int64_t         _seqIndex = 0;       ///< @brief Current zero-based offset from head.
                bool            _seqAtEnd = false;   ///< @brief True after the step-direction end was reached.
                Metadata        _seqMetadata;        ///< @brief Sidecar-supplied metadata merged onto frames.
                Size2Du32       _seqSize;            ///< @brief Size hint for headerless formats.
                PixelDesc       _seqPixelDesc;       ///< @brief Pixel desc hint for headerless formats.

                // Sidecar audio state (sequence mode only)
                AudioFile       _sidecarAudio;       ///< @brief Reader/writer handle for the sidecar audio file.
                AudioDesc       _sidecarAudioDesc;   ///< @brief Audio format descriptor for the sidecar.
                String          _sidecarAudioPath;   ///< @brief Resolved path to the sidecar audio file.
                FrameRate       _sidecarFrameRate;   ///< @brief Frame rate for per-frame sample accounting.
                int64_t         _sidecarSampleRate = 0; ///< @brief Audio sample rate (int64_t for FrameRate API).
                bool            _sidecarAudioOpen = false;   ///< @brief True when the sidecar file is open.
                bool            _sidecarAudioEnabled = true; ///< @brief Config-level master switch.
                String          _sidecarAudioName;   ///< @brief Bare filename of the sidecar (for .imgseq output).
};

PROMEKI_NAMESPACE_END
