/**
 * @file      imgseq.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/error.h>
#include <promeki/string.h>
#include <promeki/filepath.h>
#include <promeki/numname.h>
#include <promeki/framerate.h>
#include <promeki/size2d.h>
#include <promeki/pixelformat.h>
#include <promeki/metadata.h>

PROMEKI_NAMESPACE_BEGIN

class JsonObject;

/**
 * @brief Describes a numbered image sequence on disk.
 * @ingroup proav
 *
 * @c ImgSeq captures everything an image-sequence reader needs to know
 * about a sequence:
 *
 * - The filename pattern (a @c NumName — e.g. @c "shot_####.dpx")
 * - The head and tail frame numbers (inclusive range)
 * - Optional @c FrameRate, @c Size2Du32 "videoSize" hint, and
 *   @c PixelFormat for headerless formats
 * - Optional @c Metadata passthrough carried alongside each frame
 *
 * The class is also the in-memory representation of the @c .imgseq
 * JSON sidecar format.  An @c .imgseq file is a small JSON document
 * sitting next to the actual image files in the same directory, and
 * is used to describe one specific sequence's intended configuration
 * (including things the image files themselves cannot carry, such as
 * a frame rate).  Multiple @c .imgseq files may sit in the same
 * directory, each describing a different sequence.
 *
 * @par JSON format
 *
 * @code
 * {
 *     "type":      "imgseq",
 *     "name":      "shot_####.dpx",
 *     "head":      1,
 *     "tail":      100,
 *     "frameRate": "24/1",
 *     "videoSize": "1920x1080",
 *     "pixelFormat": "YUV_10_422_DPX",
 *     "metadata":  { "Title": "Shot 01" }
 * }
 * @endcode
 *
 * All fields except @c type are optional: the bare minimum for a
 * valid sidecar is a @c name field, everything else is filled in
 * from the directory scan or defaulted.
 *
 * The @c "type" field must be present and equal to @c "imgseq" —
 * this tag identifies the file as an @c ImgSeq sidecar regardless of
 * its extension.  A directory may contain multiple @c .imgseq files
 * for different sequences.
 *
 * @par Separation from MediaIOTask_ImageFile
 *
 * @c ImgSeq is a standalone data class with no dependency on the
 * MediaIO framework.  Code that wants to describe or persist a
 * sequence can use it directly; the image-file MediaIO task uses it
 * as a convenient container when reading @c .imgseq files and when a
 * user chooses to write one out after producing a sequence.
 */
class ImgSeq {
        public:
                /**
                 * @brief The required JSON @c type field value for an @c .imgseq file.
                 *
                 * A JSON document must have a top-level @c "type" field
                 * equal to this string to be recognized as an @c ImgSeq
                 * sidecar.
                 */
                static inline const char *TypeTag = "imgseq";

                /** @brief Default constructor. Creates an invalid sequence. */
                ImgSeq() = default;

                /**
                 * @brief Loads an @c ImgSeq from a sidecar JSON file.
                 *
                 * Opens the file at @p path, parses the JSON, verifies the
                 * @c "type" field, and populates the returned @c ImgSeq.
                 * On success the @c sidecarPath() of the returned object
                 * is set to @p path so the pattern and subsequent
                 * resolution can be relative to the sidecar's directory.
                 *
                 * @param path Path to an @c .imgseq file.
                 * @param err  Optional error output.
                 * @return A valid @c ImgSeq on success, an invalid one on failure.
                 */
                static ImgSeq load(const FilePath &path, Error *err = nullptr);

                /**
                 * @brief Parses an @c ImgSeq from a JSON object.
                 * @param json The root JSON object.
                 * @param err  Optional error output.
                 * @return A valid @c ImgSeq on success, an invalid one on failure.
                 */
                static ImgSeq fromJson(const JsonObject &json, Error *err = nullptr);

                /**
                 * @brief Probes a JSON document string for the @c ImgSeq tag.
                 *
                 * Performs a lightweight parse and checks whether the root
                 * is an object with @c "type" equal to @c "imgseq".  Useful
                 * for format detection when the sidecar extension is
                 * unknown or ambiguous.
                 *
                 * @param jsonText The JSON document as text.
                 * @return @c true if the document is a valid @c ImgSeq sidecar.
                 */
                static bool isImgSeqJson(const String &jsonText);

                /**
                 * @brief Saves this sequence to a sidecar JSON file.
                 *
                 * Writes a human-readable JSON document describing this
                 * sequence to @p path.  The @c type field is set to
                 * @c "imgseq".  The @c name is emitted as the hashmask
                 * form of the pattern (e.g. @c "shot_####.dpx").
                 *
                 * @param path Destination @c .imgseq path.
                 * @return @c Error::Ok on success, or an I/O error.
                 */
                Error save(const FilePath &path) const;

                /**
                 * @brief Serializes this sequence to a JSON object.
                 * @return The populated JSON document.
                 */
                JsonObject toJson() const;

                /**
                 * @brief Detects the head and tail from the containing directory.
                 *
                 * Scans @p dir for files matching the current pattern
                 * and updates head/tail to the minimum/maximum frame
                 * numbers observed.  Files whose @c NumName is not in
                 * the same sequence as this pattern (different prefix,
                 * suffix, or padding) are ignored.  The pattern must
                 * already be set — call @c setName() before calling this.
                 *
                 * @param dir The directory to scan.
                 * @return @c Error::Ok on success (including zero matches),
                 *         or @c Error::Invalid if the pattern is invalid.
                 */
                Error detectRange(const FilePath &dir);

                /** @brief Returns @c true if the pattern is valid. */
                bool isValid() const { return _name.isValid(); }

                /**
                 * @brief Returns the directory where the image files live.
                 *
                 * When loaded from a sidecar, this is the value of the
                 * @c "dir" JSON field (empty means same directory as the
                 * sidecar).  Relative paths are resolved against the
                 * sidecar's containing directory; absolute paths are used
                 * as-is.
                 */
                const FilePath &dir() const { return _dir; }

                /** @brief Sets the image directory. */
                void setDir(const FilePath &val) { _dir = val; }

                /** @brief Returns the file pattern. */
                const NumName &name() const { return _name; }

                /** @brief Sets the file pattern. */
                void setName(const NumName &val) { _name = val; }

                /** @brief Returns the head (first) frame number. */
                size_t head() const { return _head; }

                /** @brief Sets the head (first) frame number. */
                void setHead(size_t val) { _head = val; }

                /** @brief Returns the tail (last, inclusive) frame number. */
                size_t tail() const { return _tail; }

                /** @brief Sets the tail (last, inclusive) frame number. */
                void setTail(size_t val) { _tail = val; }

                /**
                 * @brief Returns the number of frames in the sequence.
                 *
                 * Computed as @c tail - @c head + 1.  Returns 0 for an
                 * invalid sequence (no pattern set) or when
                 * @c head > @c tail.
                 *
                 * @return The number of frames, or 0 if empty/invalid.
                 */
                size_t length() const {
                        if(!isValid()) return 0;
                        return (_tail >= _head) ? (_tail - _head + 1) : 0;
                }

                /** @brief Returns the optional frame rate. */
                const FrameRate &frameRate() const { return _frameRate; }

                /** @brief Sets the optional frame rate. */
                void setFrameRate(const FrameRate &val) { _frameRate = val; }

                /** @brief Returns the optional size hint (for headerless formats). */
                const Size2Du32 &videoSize() const { return _videoSize; }

                /** @brief Sets the optional size hint. */
                void setVideoSize(const Size2Du32 &val) { _videoSize = val; }

                /** @brief Returns the optional pixel descriptor (for headerless formats). */
                const PixelFormat &pixelFormat() const { return _pixelFormat; }

                /** @brief Sets the optional pixel descriptor. */
                void setPixelFormat(const PixelFormat &val) { _pixelFormat = val; }

                /**
                 * @brief Returns the sidecar audio file path.
                 *
                 * When loaded from an @c .imgseq sidecar, this is the value
                 * of the @c "audioFile" JSON field.  Relative paths are
                 * resolved against the sidecar's (or sequence's) directory;
                 * absolute paths are used as-is.  Empty means no sidecar
                 * audio was specified in the sidecar file.
                 */
                const String &audioFile() const { return _audioFile; }

                /** @brief Sets the sidecar audio file path. */
                void setAudioFile(const String &val) { _audioFile = val; }

                /** @brief Returns the sequence-level metadata. */
                const Metadata &metadata() const { return _metadata; }

                /** @brief Returns a mutable reference to the sequence-level metadata. */
                Metadata &metadata() { return _metadata; }

                /** @brief Sets the sequence-level metadata. */
                void setMetadata(const Metadata &val) { _metadata = val; }

                /**
                 * @brief Returns the sidecar @c .imgseq path, if loaded from one.
                 *
                 * Populated by @c load(); empty when the object was
                 * constructed programmatically.  Used by callers who
                 * need to resolve the sequence files relative to the
                 * sidecar's directory.
                 *
                 * @return The sidecar file path, or an empty @c FilePath.
                 */
                const FilePath &sidecarPath() const { return _sidecarPath; }

                /** @brief Sets the sidecar @c .imgseq path. */
                void setSidecarPath(const FilePath &val) { _sidecarPath = val; }

                /**
                 * @brief Returns the filename for the given zero-based index
                 *        within the sequence.
                 *
                 * Computed as @c name().name(head() + idx).  Does not
                 * bounds-check @p idx.
                 *
                 * @param idx Zero-based offset from @c head().
                 * @return The computed filename (not including any directory).
                 */
                String frameFileName(size_t idx) const {
                        return _name.name(static_cast<int>(_head + idx));
                }

        private:
                FilePath        _dir;
                NumName         _name;
                size_t          _head = 0;
                size_t          _tail = 0;
                FrameRate       _frameRate;
                Size2Du32       _videoSize;
                PixelFormat       _pixelFormat;
                String          _audioFile;
                Metadata        _metadata;
                FilePath        _sidecarPath;
};

PROMEKI_NAMESPACE_END
