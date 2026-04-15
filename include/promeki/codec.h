/**
 * @file      codec.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/mediapacket.h>

PROMEKI_NAMESPACE_BEGIN

class Image;
class MediaConfig;

/**
 * @brief Abstract base class for image codecs.
 * @ingroup proav
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
 * ImageCodec *codec = ImageCodec::createCodec("JPEG");
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

                /** @brief Returns the codec name (e.g. "JPEG", "png"). */
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
                 * @brief Applies caller-supplied options to this codec instance.
                 *
                 * Lets a caller (typically @ref Image::convert) hand a
                 * @ref MediaConfig to a freshly-constructed codec without
                 * having to know which keys the concrete subclass cares
                 * about.  Each subclass overrides this to read its own
                 * well-known @ref MediaConfig keys (e.g. @c JpegQuality,
                 * @c JpegSubsampling) and updates its internal state.
                 * Keys the codec doesn't recognize are ignored, so the
                 * same @ref MediaConfig can be reused across pipeline
                 * stages without filtering.
                 *
                 * The default implementation is a no-op so codecs that
                 * have no configurable knobs don't need to override.
                 *
                 * Calling @c configure() between encode/decode operations
                 * is allowed; the codec is expected to honor the new
                 * settings on subsequent calls.
                 *
                 * @param config Caller-supplied configuration database.
                 *               May be empty.
                 *
                 * @par Example
                 * @code
                 * ImageCodec *codec = ImageCodec::createCodec("JPEG");
                 * MediaConfig cfg;
                 * cfg.set(MediaConfig::JpegQuality, 95);
                 * codec->configure(cfg);
                 * Image jpeg = codec->encode(rgb);
                 * delete codec;
                 * @endcode
                 */
                virtual void configure(const MediaConfig &config);

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
};

/**
 * @brief Abstract base class for stateful video encoders.
 * @ingroup proav
 *
 * VideoEncoder is the push-frame / pull-packet counterpart to
 * @ref ImageCodec for codecs that are fundamentally temporal — H.264,
 * HEVC, AV1, VP9, ProRes, and so on.  Unlike ImageCodec, which treats
 * every image as an independent round-trip, VideoEncoder holds a
 * long-lived session: frames submitted via @ref submitFrame feed an
 * internal pipeline, and encoded output comes back out of
 * @ref receivePacket zero, one, or many frames later depending on the
 * codec's coding structure and configured look-ahead.
 *
 * @par Session lifecycle
 *
 *   1. Create an instance via @ref createEncoder.
 *   2. Call @ref configure with a @ref MediaConfig holding bitrate,
 *      GOP length, preset, and any other well-known knobs.
 *   3. For each source frame, call @ref submitFrame.
 *   4. After each submit (and any time after), drain with
 *      @ref receivePacket until it returns a null Ptr.  Packets may
 *      arrive with PTS out of order (B-frames); the DTS on each
 *      packet reflects decode order.
 *   5. When the input stream is exhausted, call @ref flush and keep
 *      draining until @ref receivePacket returns a packet with the
 *      @ref MediaPacket::EndOfStream flag set.
 *   6. Destroy the encoder.
 *
 * Implementations are not required to be thread-safe.  Each pipeline
 * thread should own its own encoder instance; typical use is one
 * encoder per stream.
 *
 * @par Example
 * @code
 * VideoEncoder *enc = VideoEncoder::createEncoder("H264");
 * MediaConfig cfg;
 * cfg.set(MediaConfig::BitrateKbps,   8000);
 * cfg.set(MediaConfig::VideoRcMode,   VideoRateControl::CBR);
 * cfg.set(MediaConfig::GopLength,     60);
 * cfg.set(MediaConfig::VideoPreset,   VideoEncoderPreset::LowLatency);
 * enc->configure(cfg);
 *
 * for(const Image &frame : source) {
 *         enc->submitFrame(frame);
 *         while(auto pkt = enc->receivePacket()) {
 *                 sink.write(pkt);
 *         }
 * }
 * enc->flush();
 * while(auto pkt = enc->receivePacket()) {
 *         sink.write(pkt);
 *         if(pkt->isEndOfStream()) break;
 * }
 * delete enc;
 * @endcode
 */
class VideoEncoder {
        public:
                /** @brief Virtual destructor. */
                virtual ~VideoEncoder();

                /** @brief Returns the codec name (e.g. @c "H264", @c "HEVC"). */
                virtual String name() const = 0;

                /** @brief Returns a human-readable description. */
                virtual String description() const = 0;

                /**
                 * @brief Returns the compressed @ref PixelDesc this encoder produces.
                 *
                 * For H.264 this is @c PixelDesc::H264; for HEVC it is
                 * @c PixelDesc::HEVC; etc.  The returned value is copied
                 * into each outgoing @ref MediaPacket.
                 */
                virtual PixelDesc outputPixelDesc() const = 0;

                /**
                 * @brief Returns the uncompressed @ref PixelDesc IDs this encoder accepts.
                 *
                 * Callers can check whether their source pixel format is
                 * supported before submitting any frames.  Backends that
                 * can ingest any format they can convert internally may
                 * return an empty list, indicating "any input accepted".
                 */
                virtual List<int> supportedInputs() const = 0;

                /**
                 * @brief Applies encoder parameters from a @ref MediaConfig.
                 *
                 * Each backend reads the well-known keys it understands
                 * (e.g. @c BitrateKbps, @c VideoRcMode, @c GopLength,
                 * @c VideoPreset) and silently ignores the rest, so the
                 * same @ref MediaConfig can be reused across pipeline
                 * stages without filtering.  Calling @c configure() while
                 * an encoder session is active is allowed; the backend
                 * applies what it can at runtime and may defer the rest
                 * to the next IDR.  The default implementation is a
                 * no-op.
                 *
                 * @param config Caller-supplied configuration database.
                 */
                virtual void configure(const MediaConfig &config);

                /**
                 * @brief Submits one uncompressed frame for encoding.
                 *
                 * The encoder takes a logical copy of what it needs from
                 * @p frame before returning; the caller may reuse or
                 * release the frame afterward.  The @p pts is recorded
                 * in the encoder's internal queue so that the matching
                 * output packet can carry it back out.  Submitting does
                 * not guarantee a packet will be available immediately —
                 * B-frames and look-ahead both introduce output delay.
                 *
                 * @param frame The source image in one of the supported
                 *              uncompressed pixel formats.
                 * @param pts   Presentation timestamp for this frame.
                 *              May be invalid if the pipeline does not
                 *              care about presentation ordering.
                 * @return @c Error::Ok on success, or an error code
                 *         describing why the frame was rejected.
                 */
                virtual Error submitFrame(const Image &frame,
                                          const MediaTimeStamp &pts = MediaTimeStamp()) = 0;

                /**
                 * @brief Dequeues one encoded packet from the encoder's output queue.
                 *
                 * Returns a null @ref MediaPacket::Ptr when no packet is
                 * ready yet (the encoder is pipelined and has not emitted
                 * output corresponding to any submitted frame).  Callers
                 * typically drain in a loop after every @ref submitFrame
                 * and again after @ref flush.
                 *
                 * @return The next encoded packet, or a null Ptr if none
                 *         is available.
                 */
                virtual MediaPacket::Ptr receivePacket() = 0;

                /**
                 * @brief Signals end-of-stream to the encoder and asks it to emit remaining packets.
                 *
                 * After @c flush(), the caller drains with
                 * @ref receivePacket until it sees a packet with the
                 * @ref MediaPacket::EndOfStream flag or a null Ptr.  After
                 * the flush completes, the encoder may be reused for a
                 * fresh stream by calling @ref reset followed by
                 * @ref configure; or it may be destroyed.
                 *
                 * @return @c Error::Ok on success.
                 */
                virtual Error flush() = 0;

                /**
                 * @brief Discards any pending frames / packets and returns the encoder to a fresh state.
                 *
                 * Unlike @ref flush, @c reset does not emit the pending
                 * packets — the encoder simply drops them.  Configuration
                 * set by @ref configure is preserved; call @c configure
                 * again only if knobs need to change.
                 *
                 * @return @c Error::Ok on success.
                 */
                virtual Error reset() = 0;

                /**
                 * @brief Forces the next submitted frame to be coded as a keyframe.
                 *
                 * The request persists until the next frame is coded as
                 * an IDR / keyframe; further calls before that frame is
                 * consumed are idempotent.
                 */
                virtual void requestKeyframe() = 0;

                /** @brief Returns the last error produced by the encoder. */
                Error lastError() const { return _lastError; }

                /** @brief Returns a human-readable message for the last error. */
                const String &lastErrorMessage() const { return _lastErrorMessage; }

                // ---- Registry ----

                /**
                 * @brief Registers a video encoder factory.
                 * @param name    Unique codec name (matches @ref PixelDesc::Data::codecName).
                 * @param factory Factory function that creates a new instance.
                 */
                static void registerEncoder(const String &name,
                                            std::function<VideoEncoder *()> factory);

                /**
                 * @brief Creates a video encoder instance by name.
                 * @param name The codec name (e.g. @c "H264", @c "HEVC").
                 * @return A new encoder instance, or @c nullptr if not registered.
                 */
                static VideoEncoder *createEncoder(const String &name);

                /** @brief Returns the list of all registered encoder codec names. */
                static List<String> registeredEncoders();

        protected:
                Error   _lastError;
                String  _lastErrorMessage;

                /** @brief Records a new error state.
                 *  @param err The error code.
                 *  @param msg Human-readable message. */
                void setError(Error err, const String &msg = String());

                /** @brief Clears the error state. */
                void clearError();

        private:
                static Map<String, std::function<VideoEncoder *()>> &encoderRegistry();
};

/**
 * @brief Abstract base class for stateful video decoders.
 * @ingroup proav
 *
 * VideoDecoder is the inverse of @ref VideoEncoder: packets submitted
 * via @ref submitPacket feed an internal decode pipeline and
 * uncompressed frames come back out of @ref receiveFrame.  The
 * decoder may buffer several packets before producing its first
 * frame (B-frame reordering, reference-frame dependencies).
 *
 * @par Session lifecycle
 *
 *   1. Create an instance via @ref createDecoder.
 *   2. Optionally call @ref configure (most decoders infer parameters
 *      from the bitstream and need no explicit configuration).
 *   3. For each encoded packet, call @ref submitPacket.
 *   4. After each submit, drain with @ref receiveFrame until it
 *      returns an invalid @ref Image.
 *   5. Call @ref flush when the input stream ends, then drain again.
 *   6. Destroy the decoder.
 *
 * Implementations are not required to be thread-safe.
 */
class VideoDecoder {
        public:
                /** @brief Virtual destructor. */
                virtual ~VideoDecoder();

                /** @brief Returns the codec name (e.g. @c "H264", @c "HEVC"). */
                virtual String name() const = 0;

                /** @brief Returns a human-readable description. */
                virtual String description() const = 0;

                /**
                 * @brief Returns the compressed @ref PixelDesc this decoder accepts.
                 */
                virtual PixelDesc inputPixelDesc() const = 0;

                /**
                 * @brief Returns the uncompressed @ref PixelDesc IDs this decoder can emit.
                 *
                 * Callers can use @ref configure with
                 * @c MediaConfig::OutputPixelDesc to request a specific
                 * output; decoders that only support a single native
                 * output may ignore the request.
                 */
                virtual List<int> supportedOutputs() const = 0;

                /**
                 * @brief Applies decoder parameters from a @ref MediaConfig.
                 *
                 * Same semantics as @ref VideoEncoder::configure: reads
                 * known keys, ignores the rest.
                 *
                 * @param config Caller-supplied configuration database.
                 */
                virtual void configure(const MediaConfig &config);

                /**
                 * @brief Submits one encoded packet for decoding.
                 * @param packet The packet to decode.  Its
                 *               @ref MediaPacket::pixelDesc must match
                 *               @ref inputPixelDesc.
                 * @return @c Error::Ok on success.
                 */
                virtual Error submitPacket(const MediaPacket &packet) = 0;

                /**
                 * @brief Dequeues one decoded frame.
                 * @return A valid @ref Image on success, or an invalid
                 *         Image if no frame is ready yet.
                 */
                virtual Image receiveFrame() = 0;

                /** @brief Signals end-of-stream; remaining frames can be drained with @ref receiveFrame. */
                virtual Error flush() = 0;

                /** @brief Discards any pending packets / frames. */
                virtual Error reset() = 0;

                /** @brief Returns the last error produced by the decoder. */
                Error lastError() const { return _lastError; }

                /** @brief Returns a human-readable message for the last error. */
                const String &lastErrorMessage() const { return _lastErrorMessage; }

                // ---- Registry ----

                /**
                 * @brief Registers a video decoder factory.
                 * @param name    Unique codec name (matches @ref PixelDesc::Data::codecName).
                 * @param factory Factory function that creates a new instance.
                 */
                static void registerDecoder(const String &name,
                                            std::function<VideoDecoder *()> factory);

                /**
                 * @brief Creates a video decoder instance by name.
                 * @param name The codec name.
                 * @return A new decoder instance, or @c nullptr if not registered.
                 */
                static VideoDecoder *createDecoder(const String &name);

                /** @brief Returns the list of all registered decoder codec names. */
                static List<String> registeredDecoders();

        protected:
                Error   _lastError;
                String  _lastErrorMessage;

                /** @brief Records a new error state.
                 *  @param err The error code.
                 *  @param msg Human-readable message. */
                void setError(Error err, const String &msg = String());

                /** @brief Clears the error state. */
                void clearError();

        private:
                static Map<String, std::function<VideoDecoder *()>> &decoderRegistry();
};

// Note: the legacy `class AudioCodec` stub that used to live here was
// retired in task 37 — its name is now claimed by the typed
// @ref AudioCodec TypeRegistry in audiocodec.h.  The stateful audio
// session base classes (AudioEncoder / AudioDecoder, symmetric to
// VideoEncoder / VideoDecoder) will arrive when the first audio codec
// backend lands.

// Note: the legacy PROMEKI_REGISTER_IMAGE_CODEC macro that used to
// live here was retired in task 37 alongside the
// ImageCodec::registerCodec / createCodec / registeredCodecs registry.
// Concrete codec classes now register through
// PROMEKI_REGISTER_VIDEO_ENCODER / PROMEKI_REGISTER_VIDEO_DECODER (or
// directly via VideoCodec::registerData with a populated factory hook).

/**
 * @brief Macro to register a VideoEncoder subclass for runtime creation.
 *
 * Place this in the .cpp file of each concrete VideoEncoder subclass.
 *
 * @param ClassName The concrete VideoEncoder subclass name.
 * @param CodecName A string literal for the codec name (e.g. @c "H264").
 */
#define PROMEKI_REGISTER_VIDEO_ENCODER(ClassName, CodecName) \
        static struct ClassName##EncoderRegistrar { \
                ClassName##EncoderRegistrar() { \
                        VideoEncoder::registerEncoder(CodecName, \
                                []() -> VideoEncoder * { return new ClassName(); }); \
                } \
        } __##ClassName##EncoderRegistrar;

/**
 * @brief Macro to register a VideoDecoder subclass for runtime creation.
 *
 * Place this in the .cpp file of each concrete VideoDecoder subclass.
 *
 * @param ClassName The concrete VideoDecoder subclass name.
 * @param CodecName A string literal for the codec name (e.g. @c "H264").
 */
#define PROMEKI_REGISTER_VIDEO_DECODER(ClassName, CodecName) \
        static struct ClassName##DecoderRegistrar { \
                ClassName##DecoderRegistrar() { \
                        VideoDecoder::registerDecoder(CodecName, \
                                []() -> VideoDecoder * { return new ClassName(); }); \
                } \
        } __##ClassName##DecoderRegistrar;

PROMEKI_NAMESPACE_END
