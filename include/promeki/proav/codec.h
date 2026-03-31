/**
 * @file      proav/codec.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <promeki/core/namespace.h>
#include <promeki/core/string.h>
#include <promeki/core/error.h>
#include <promeki/core/list.h>
#include <promeki/core/map.h>

PROMEKI_NAMESPACE_BEGIN

class Image;

/**
 * @brief Abstract base class for image codecs.
 * @ingroup proav_media
 *
 * ImageCodec provides an interface for encoding and decoding images.
 * Concrete subclasses implement specific algorithms (JPEG, PNG, etc.).
 *
 * Codec instances are configured once (quality, subsampling, etc.)
 * then encode or decode multiple frames without reconfiguration.
 *
 * Subclasses are registered via PROMEKI_REGISTER_IMAGE_CODEC and
 * looked up by name at runtime.
 *
 * This class is not thread-safe. Each thread should use its own
 * codec instance.
 *
 * @par Example
 * @code
 * ImageCodec *codec = ImageCodec::createCodec("jpeg");
 * if(codec) {
 *         Image compressed = codec->encode(sourceImage);
 *         delete codec;
 * }
 * @endcode
 */
class ImageCodec {
        public:
                /** @brief Virtual destructor. */
                virtual ~ImageCodec();

                /** @brief Returns the codec name (e.g. "jpeg", "png"). */
                virtual String name() const = 0;

                /** @brief Returns a human-readable description. */
                virtual String description() const = 0;

                /**
                 * @brief Returns whether this codec supports encoding.
                 * @return true if encode() is implemented.
                 */
                virtual bool canEncode() const = 0;

                /**
                 * @brief Returns whether this codec supports decoding.
                 * @return true if decode() is implemented.
                 */
                virtual bool canDecode() const = 0;

                /**
                 * @brief Encodes an uncompressed image to compressed form.
                 * @param input The source image (uncompressed pixel format).
                 * @return The compressed image, or an invalid Image on failure.
                 */
                virtual Image encode(const Image &input) = 0;

                /**
                 * @brief Decodes a compressed image to uncompressed form.
                 * @param input The compressed image.
                 * @param outputFormat Desired output pixel format ID. Pass 0 for codec default.
                 * @return The decoded image, or an invalid Image on failure.
                 */
                virtual Image decode(const Image &input, int outputFormat = 0) = 0;

                /**
                 * @brief Returns the last error that occurred.
                 * @return The last error.
                 */
                Error lastError() const { return _lastError; }

                /**
                 * @brief Returns a human-readable string for the last error.
                 * @return The last error message.
                 */
                const String &lastErrorMessage() const { return _lastErrorMessage; }

                // ---- Registry ----

                /**
                 * @brief Registers an image codec factory.
                 * @param name Unique codec name.
                 * @param factory Factory function that creates a new instance.
                 */
                static void registerCodec(const String &name,
                                          std::function<ImageCodec *()> factory);

                /**
                 * @brief Creates a codec instance by name.
                 * @param name The codec name.
                 * @return A new codec instance, or nullptr if not registered.
                 *
                 * @par Example
                 * @code
                 * ImageCodec *codec = ImageCodec::createCodec("jpeg");
                 * if(codec) {
                 *         Image out = codec->encode(src);
                 *         delete codec;
                 * }
                 * @endcode
                 */
                static ImageCodec *createCodec(const String &name);

                /**
                 * @brief Returns the list of all registered codec names.
                 * @return A list of codec name strings.
                 *
                 * @par Example
                 * @code
                 * for(const auto &name : ImageCodec::registeredCodecs())
                 *         qDebug() << name;
                 * @endcode
                 */
                static List<String> registeredCodecs();

        protected:
                Error           _lastError;
                String          _lastErrorMessage;

                /**
                 * @brief Sets the last error state.
                 * @param err The error code.
                 * @param msg Human-readable message.
                 */
                void setError(Error err, const String &msg = String());

                /** @brief Clears the error state. */
                void clearError();

        private:
                static Map<String, std::function<ImageCodec *()>> &codecRegistry();
};

/**
 * @brief Stub base class for audio codecs (future expansion).
 * @ingroup proav_media
 */
class AudioCodec {
        public:
                /** @brief Virtual destructor. */
                virtual ~AudioCodec();

                /** @brief Returns the codec name. */
                virtual String name() const = 0;

                /** @brief Returns a human-readable description. */
                virtual String description() const = 0;
};

/**
 * @brief Macro to register an ImageCodec subclass for runtime creation.
 *
 * Place this in the .cpp file of each concrete ImageCodec subclass.
 *
 * @param ClassName The concrete ImageCodec subclass name.
 * @param CodecName A string literal for the codec name (e.g. "jpeg").
 */
#define PROMEKI_REGISTER_IMAGE_CODEC(ClassName, CodecName) \
        static struct ClassName##CodecRegistrar { \
                ClassName##CodecRegistrar() { \
                        ImageCodec::registerCodec(CodecName, \
                                []() -> ImageCodec * { return new ClassName(); }); \
                } \
        } __##ClassName##CodecRegistrar;

PROMEKI_NAMESPACE_END
