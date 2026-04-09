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
#include <promeki/pixeldesc.h>
#include <promeki/mediadesc.h>
#include <promeki/metadata.h>
#include <promeki/framerate.h>
#include <promeki/size2d.h>
#include <promeki/filepath.h>
#include <promeki/numname.h>

PROMEKI_NAMESPACE_BEGIN

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
 */
class MediaIOTask_ImageFile : public MediaIOTask {
        public:
                /** @brief Default frame rate when no config or caller override is supplied. */
                static inline const FrameRate DefaultFrameRate{FrameRate::FPS_30};

                /** @brief Default head index for sequence writers. */
                static inline constexpr int DefaultSequenceHead = 1;

                /**
                 * @brief Returns the format descriptor for this backend.
                 * @return A FormatDesc covering all supported image file extensions.
                 */
                static MediaIO::FormatDesc formatDesc();

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

                Error openSingle(MediaIOCommandOpen &cmd, MediaDesc &mediaDesc,
                                 const String &frSource);
                Error openSequence(MediaIOCommandOpen &cmd, MediaDesc &mediaDesc,
                                   const String &frSource);
                Error readSingle(MediaIOCommandRead &cmd);
                Error readSequence(MediaIOCommandRead &cmd);
                Error writeSingle(MediaIOCommandWrite &cmd);
                Error writeSequence(MediaIOCommandWrite &cmd);

                // Common state
                String          _filename;          ///< @brief Raw config filename (single file, mask, or .imgseq).
                int             _imageFileID = ImageFile::Invalid;
                MediaIOMode     _mode = MediaIO_NotOpen;
                bool            _sequenceMode = false;
                Metadata        _writeContainerMetadata; ///< @brief Container metadata merged into each written frame (writer only).
                MediaConfig     _ioConfig;          ///< @brief Open-time MediaConfig forwarded to ImageFileIO load/save calls.

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
};

PROMEKI_NAMESPACE_END
