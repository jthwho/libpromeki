/**
 * @file      proav/audiofile.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/sharedptr.h>
#include <promeki/core/string.h>
#include <promeki/core/error.h>
#include <promeki/proav/audio.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Audio file reader and writer.
 *
 * Provides a high-level interface for reading and writing audio files.
 * The actual file format handling is delegated to an Impl subclass,
 * which is selected automatically via AudioFileFactory based on the
 * filename extension and requested operation.
 */
class AudioFile {
        public:
                /** @brief Shared pointer type for AudioFile. */
                using Ptr = SharedPtr<AudioFile>;

                /** @brief The type of operation to perform on an audio file. */
                enum Operation {
                        InvalidOperation = 0, ///< @brief No valid operation.
                        Reader,               ///< @brief Open the file for reading.
                        Writer                ///< @brief Open the file for writing.
                };

                /**
                 * @brief Abstract implementation backend for AudioFile.
                 *
                 * Subclasses provide format-specific logic for opening, reading,
                 * writing, seeking, and closing audio files.
                 */
                class Impl {
                        PROMEKI_SHARED(Impl)
                        public:
                                /**
                                 * @brief Constructs an Impl for the given operation.
                                 * @param op The operation this implementation will perform.
                                 */
                                Impl(Operation op) : _operation(op) {}

                                /** @brief Virtual destructor. */
                                virtual ~Impl();

                                /**
                                 * @brief Returns true if this implementation has a valid operation.
                                 * @return true if the operation is not InvalidOperation.
                                 */
                                bool isValid() const {
                                        return _operation != InvalidOperation;
                                }

                                /**
                                 * @brief Returns the operation type for this implementation.
                                 * @return The Operation value (Reader or Writer).
                                 */
                                Operation operation() const {
                                        return _operation;
                                }

                                /**
                                 * @brief Returns the filename associated with this audio file.
                                 * @return A const reference to the filename string.
                                 */
                                const String &filename() const {
                                        return _filename;
                                }

                                /**
                                 * @brief Sets the filename for this audio file.
                                 * @param val The filename to set.
                                 */
                                void setFilename(const String &val) {
                                        _filename = val;
                                        return;
                                }

                                /**
                                 * @brief Returns the audio description (format, channels, sample rate, etc.).
                                 * @return The AudioDesc for this file.
                                 */
                                AudioDesc desc() const {
                                        return _desc;
                                }

                                /**
                                 * @brief Sets the audio description for writing.
                                 * @param val The AudioDesc to set.
                                 */
                                void setDesc(const AudioDesc &val) {
                                        _desc = val;
                                        return;
                                }

                                /**
                                 * @brief Opens the audio file.
                                 * @return Error::Ok on success, or an error on failure.
                                 */
                                virtual Error open();

                                /** @brief Closes the audio file. */
                                virtual void close();

                                /**
                                 * @brief Reads audio samples from the file.
                                 * @param audio The Audio object to read into.
                                 * @param maxSamples Maximum number of samples to read.
                                 * @return Error::Ok on success, or an error on failure.
                                 */
                                virtual Error read(Audio &audio, size_t maxSamples);

                                /**
                                 * @brief Writes audio samples to the file.
                                 * @param audio The Audio object containing samples to write.
                                 * @return Error::Ok on success, or an error on failure.
                                 */
                                virtual Error write(const Audio &audio);

                                /**
                                 * @brief Seeks to a specific sample position in the file.
                                 * @param sample The sample index to seek to.
                                 * @return Error::Ok on success, or an error on failure.
                                 */
                                virtual Error seekToSample(size_t sample);

                                /**
                                 * @brief Returns the total number of samples in the file.
                                 * @return The sample count.
                                 */
                                virtual size_t sampleCount() const;

                        protected:
                                Operation       _operation;
                                String          _filename;
                                AudioDesc       _desc;
                };

                /**
                 * @brief Creates an AudioFile for the given operation and filename.
                 *
                 * Uses AudioFileFactory to find an appropriate backend implementation.
                 * @param op The operation (Reader or Writer).
                 * @param filename The path to the audio file.
                 * @return An AudioFile configured for the requested operation.
                 */
                static AudioFile createForOperation(Operation op, const String &filename);

                /**
                 * @brief Creates an AudioFile reader for the given filename.
                 * @param filename The path to the audio file to read.
                 * @return An AudioFile configured for reading.
                 */
                static AudioFile createReader(const String &filename) { return createForOperation(Reader, filename); }

                /**
                 * @brief Creates an AudioFile writer for the given filename.
                 * @param filename The path to the audio file to write.
                 * @return An AudioFile configured for writing.
                 */
                static AudioFile createWriter(const String &filename) { return createForOperation(Writer, filename); }

                /** @brief Default constructor. Creates an invalid AudioFile. */
                AudioFile() : d(SharedPtr<Impl>::create(InvalidOperation)) {};

                /**
                 * @brief Constructs an AudioFile from an Impl pointer, taking ownership.
                 * @param impl Pointer to the implementation object.
                 */
                AudioFile(Impl *impl) : d(SharedPtr<Impl>::takeOwnership(impl)) {};

                /**
                 * @brief Returns true if this AudioFile has a valid operation.
                 * @return true if the underlying implementation is valid.
                 */
                bool isValid() const { return d->isValid(); }

                /**
                 * @brief Returns the operation type (Reader or Writer).
                 * @return The Operation value.
                 */
                Operation operation() const { return d->operation(); }

                /**
                 * @brief Returns the filename associated with this audio file.
                 * @return A const reference to the filename string.
                 */
                const String &filename() const { return d->filename(); }

                /**
                 * @brief Sets the filename for this audio file.
                 * @param val The filename to set.
                 */
                void setFilename(const String &val) { d.modify()->setFilename(val); return; }

                /**
                 * @brief Returns the audio description.
                 * @return The AudioDesc for this file.
                 */
                AudioDesc desc() const { return d->desc(); }

                /**
                 * @brief Sets the audio description for writing.
                 * @param val The AudioDesc to set.
                 */
                void setDesc(const AudioDesc &val) { d.modify()->setDesc(val); return; }

                /**
                 * @brief Opens the audio file.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error open() { return d.modify()->open(); }

                /** @brief Closes the audio file. */
                void close() { d.modify()->close(); return; }

                /**
                 * @brief Reads audio samples from the file.
                 * @param audio The Audio object to read into.
                 * @param maxSamples Maximum number of samples to read.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error read(Audio &audio, size_t maxSamples) { return d.modify()->read(audio, maxSamples); }

                /**
                 * @brief Writes audio samples to the file.
                 * @param audio The Audio object containing samples to write.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error write(const Audio &audio) { return d.modify()->write(audio); }

                /**
                 * @brief Seeks to a specific sample position in the file.
                 * @param sample The sample index to seek to.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error seekToSample(size_t sample) { return d.modify()->seekToSample(sample); }

                /**
                 * @brief Returns the total number of samples in the file.
                 * @return The sample count.
                 */
                size_t sampleCount() const { return d->sampleCount(); }

        private:
                SharedPtr<Impl> d;
};

PROMEKI_NAMESPACE_END

