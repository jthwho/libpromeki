/**
 * @file      imagefilemediaio.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/audiodesc.h>
#include <promeki/audiofile.h>
#include <promeki/dedicatedthreadmediaio.h>
#include <promeki/enum.h>
#include <promeki/filepath.h>
#include <promeki/framenumber.h>
#include <promeki/framerate.h>
#include <promeki/imagefile.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaiofactory.h>
#include <promeki/metadata.h>
#include <promeki/namespace.h>
#include <promeki/numname.h>
#include <promeki/pixelformat.h>
#include <promeki/size2d.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

class ImageFileIO;

/**
 * @brief MediaIO backend for image files and image sequences.
 * @ingroup proav
 *
 * Wraps the @ref ImageFile / @ref ImageFileIO subsystem to provide read
 * and write access to image files through the MediaIO interface.
 * Supported single-image formats include DPX, Cineon, TGA, SGI, PNM,
 * PNG, JPEG, JPEG XS, and headerless raw YUV variants.
 *
 * Each per-format @ref ImageFileIO backend (registered via
 * @ref ImageFileIO::registerImageFileIO) gets a paired
 * @ref ImageFileFactory entry — they all map back to a single
 * @c ImageFileMediaIO implementation that resolves the concrete
 * @ref ImageFile::ID from the caller-supplied filename at open time.
 *
 * @par Single-image mode
 *
 * When the filename resolves to a plain file (no mask placeholder, no
 * @c .imgseq extension), the backend treats it as a single-frame source
 * or sink:
 *
 * - Readers load the file on @c open() and deliver the same cached
 *   frame on every subsequent read.  With the backend's default step
 *   of 0, reads continue indefinitely; with step != 0, a second read
 *   returns @ref Error::EndOfFile.
 * - Writers save exactly one file per @c writeFrame() call; each call
 *   overwrites the target file.
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
 * - Readers scan the containing directory once on @c open() to detect
 *   the head and tail frame numbers (unless the sidecar supplies
 *   them), then lazily load each requested frame on read.  @c canSeek
 *   is @c true and @c frameCount is the length of the detected range.
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
 * An @c .imgseq file is a small JSON document describing one sequence
 * in its containing directory.  See the @c ImgSeq class for the full
 * schema.
 *
 * @par Frame rate
 *
 * Still images have no intrinsic temporal rate, so the backend reports
 * a frame rate chosen by a priority chain:
 *
 *   1. Writer: @c cmd.pendingMediaDesc.frameRate() if valid.
 *   2. @c ImgSeq sidecar @c frameRate (reader only).
 *   3. @ref MediaConfig::FrameRate (always populated — the backend's
 *      default config pre-seeds it with @c DefaultFrameRate).
 *
 * The resolved source is recorded in the returned metadata under
 * @ref Metadata::FrameRateSource as one of @c "file" or @c "config".
 *
 * @par Threading
 * Runs on a per-instance dedicated worker thread inherited from
 * @ref DedicatedThreadMediaIO so blocking image-file load/save
 * syscalls cannot starve the shared pool.
 */
class ImageFileMediaIO : public DedicatedThreadMediaIO {
                PROMEKI_OBJECT(ImageFileMediaIO, DedicatedThreadMediaIO)
        public:
                /** @brief Default frame rate when no config or caller override is supplied. */
                static inline const FrameRate DefaultFrameRate{FrameRate::FPS_30};

                /** @brief Default head index for sequence writers. */
                static inline constexpr int DefaultSequenceHead = 1;

                /** @brief Constructs an ImageFileMediaIO. */
                ImageFileMediaIO(ObjectBase *parent = nullptr);

                /** @brief Destructor. */
                ~ImageFileMediaIO() override;

                Error proposeInput(const MediaDesc &offered, MediaDesc *preferred) const override;

        protected:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandSeek &cmd) override;

        private:
                // Maps the extension in @p filename to the preferred writer
                // PixelFormat for that format, taking the source's bit depth
                // into account so we don't drop precision unnecessarily
                // (e.g. 10-bit source -> 10-bit DPX, not 8-bit).
                PixelFormat preferredWriterPixelFormat(const String &filename, const PixelFormat &source) const;

                Error openSingle(MediaIOCommandOpen &cmd, MediaDesc &mediaDesc, const String &frSource, bool isWrite,
                                 bool &canSeek, FrameCount &frameCount);
                Error openSequence(MediaIOCommandOpen &cmd, MediaDesc &mediaDesc, const String &frSource, bool isWrite,
                                   bool &canSeek, FrameCount &frameCount);
                Error readSingle(MediaIOCommandRead &cmd);
                Error readSequence(MediaIOCommandRead &cmd);
                Error writeSingle(MediaIOCommandWrite &cmd);
                Error writeSequence(MediaIOCommandWrite &cmd);

                Error writeImgSeqSidecar();

                // Common state
                String      _filename;
                int         _imageFileID = ImageFile::Invalid;
                bool        _isOpen = false;
                bool        _isWrite = false;
                bool        _sequenceMode = false;
                Metadata    _writeContainerMetadata;
                MediaConfig _ioConfig;
                bool        _saveImgSeqEnabled = true;
                String      _saveImgSeqPath;
                Enum        _saveImgSeqPathMode;
                FrameRate   _writerFrameRate;

                // Single-file state
                Frame::Ptr _frame;
                FrameCount _readCount{0};
                FrameCount _writeCount{0};
                bool       _loaded = false;

                // Sequence state
                NumName     _seqName;
                FilePath    _seqDir;
                FrameNumber _seqHead{0};
                FrameNumber _seqTail{0};
                FrameNumber _seqIndex{0};
                bool        _seqAtEnd = false;
                Metadata    _seqMetadata;
                Size2Du32   _seqSize;
                PixelFormat _seqPixelFormat;

                // Sidecar audio state (sequence mode only)
                AudioFile _sidecarAudio;
                AudioDesc _sidecarAudioDesc;
                String    _sidecarAudioPath;
                FrameRate _sidecarFrameRate;
                int64_t   _sidecarSampleRate = 0;
                bool      _sidecarAudioOpen = false;
                bool      _sidecarAudioEnabled = true;
                String    _sidecarAudioName;
};

/**
 * @brief Factory entry for the @ref ImageFileMediaIO backend.
 * @ingroup proav
 *
 * Each registered @ref ImageFileIO backend registers one
 * @c ImageFileFactory instance carrying its identity, extensions, and
 * load/save role flags; an additional @c "ImageFile" umbrella entry is
 * registered at static-init time so callers that refer to the backend
 * by that legacy name still resolve.  The umbrella factory accepts
 * every extension any registered backend claims and supports both
 * loading and saving.
 *
 * The @c create method always returns a fresh @ref ImageFileMediaIO —
 * the per-format identity flows through the @ref MediaConfig the
 * factory passes to the new instance.
 */
class ImageFileFactory : public MediaIOFactory {
        public:
                /**
                 * @brief Constructs a factory entry with the supplied identity.
                 *
                 * @param name        Registry name (e.g. @c "ImgSeqDPX",
                 *                    @c "ImageFile" for the umbrella).
                 * @param displayName Human-readable label.
                 * @param description One-line description.
                 * @param extensions  File extensions claimed by this entry.
                 * @param canBeSource @c true if the backend can read.
                 * @param canBeSink   @c true if the backend can write.
                 */
                ImageFileFactory(String name, String displayName, String description, StringList extensions,
                                 bool canBeSource, bool canBeSink);

                /**
                 * @brief Builds the factory entry for a specific @ref ImageFileIO backend.
                 *
                 * Driven by @p io 's own @c extensions(), @c canLoad(),
                 * @c canSave(), @c mediaIoName(), and @c description() —
                 * no hard-coded per-format table.  Called by
                 * @ref ImageFileIO::registerImageFileIO so per-format
                 * @c ImgSeqXxx entries appear in the factory registry
                 * the moment a new backend's constructor runs.
                 *
                 * @param io The backend to describe.  Must be non-null.
                 * @return A heap-allocated factory the registry takes ownership of.
                 */
                static ImageFileFactory *buildFactoryFor(const ImageFileIO *io);

                String     name() const override { return _name; }
                String     displayName() const override { return _displayName; }
                String     description() const override { return _description; }
                StringList extensions() const override { return _extensions; }

                bool canBeSource() const override { return _canBeSource; }
                bool canBeSink() const override { return _canBeSink; }

                bool            canHandleDevice(IODevice *device) const override;
                Config::SpecMap configSpecs() const override;
                Metadata        defaultMetadata() const override;

                MediaIO *create(const Config &config, ObjectBase *parent = nullptr) const override;

        private:
                String     _name;
                String     _displayName;
                String     _description;
                StringList _extensions;
                bool       _canBeSource;
                bool       _canBeSink;
};

PROMEKI_NAMESPACE_END
