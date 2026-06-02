/**
 * @file      v4l2m2mcodec.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_V4L2
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/result.h>
#include <promeki/list.h>
#include <promeki/buffer.h>
#include <promeki/size2d.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Low-level driver for a V4L2 stateful memory-to-memory codec.
 * @ingroup proav
 *
 * Wraps the kernel's "Memory-to-Memory Stateful Video Encoder/Decoder
 * Interface" — the two-queue model shared by the Xilinx VCU
 * (@c allegro / @c al5d), the Raspberry Pi (@c bcm2835-codec), and the
 * kernel's own @c vicodec test driver.  Both the multiplanar API
 * (@c V4L2_CAP_VIDEO_M2M_MPLANE — VCU / Pi) and the single-planar API
 * (@c V4L2_CAP_VIDEO_M2M — vicodec and simpler codecs) are supported;
 * the layout is detected at @ref open.  A codec node exposes two
 * independent queues:
 *
 *   - @b OUTPUT (@c V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) — buffers the
 *     caller fills and submits @em into the codec.  For an encoder this
 *     carries raw frames; for a decoder, coded bitstream.
 *   - @b CAPTURE (@c V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) — buffers the
 *     codec fills and the caller drains.  For an encoder this carries
 *     the coded bitstream; for a decoder, raw frames.
 *
 * This class owns the file descriptor, the MMAP buffer pools on both
 * queues, format negotiation, control programming, and the QBUF/DQBUF
 * bookkeeping.  It is codec-agnostic: callers pick the OUTPUT/CAPTURE
 * FourCCs and program codec controls via @ref setControl.  The
 * higher-level @ref V4l2VideoEncoder layers H.264 / HEVC policy
 * (pixel-format selection, @ref MediaConfig → control mapping, payload
 * assembly) on top.
 *
 * Only @c V4L2_MEMORY_MMAP is implemented for now; raw bytes are copied
 * in on @ref submitOutput and out on @ref dequeueCapture.  A future
 * pass adds @c V4L2_MEMORY_DMABUF import/export for zero-copy on the
 * VCU (see the dma-buf buffer backend).
 *
 * @par Thread Safety
 * Not thread-safe.  One session is driven from a single thread (or
 * with external serialisation).
 */
class V4l2M2mCodec {
        public:
                /** @brief Distinguishes an encoder node from a decoder node. */
                enum class Role {
                        Encoder, ///< OUTPUT = raw, CAPTURE = coded; drains via @c VIDIOC_ENCODER_CMD.
                        Decoder, ///< OUTPUT = coded, CAPTURE = raw; drains via @c VIDIOC_DECODER_CMD.
                };

                /** @brief Buffer memory model for a queue. */
                enum class Memory {
                        Mmap,   ///< Driver-allocated, mmap'd; bytes copied in/out (@c V4L2_MEMORY_MMAP).
                        Dmabuf, ///< Caller supplies an imported dma-buf fd per buffer (@c V4L2_MEMORY_DMABUF) — zero-copy.
                };

                /** @brief Parameters for @ref open. */
                struct OpenParams {
                                Role role = Role::Encoder; ///< Encoder or decoder semantics.
                                /**
                                 * @brief Explicit device node (e.g. @c "/dev/video11").
                                 *
                                 * Empty means "auto-probe": @ref open scans
                                 * @c /dev/video* via @ref findDevice for an M2M
                                 * node whose OUTPUT queue accepts
                                 * @ref outputFourcc and CAPTURE queue accepts
                                 * @ref captureFourcc.
                                 */
                                String    devPath;
                                uint32_t  outputFourcc = 0;  ///< V4L2 FourCC for the OUTPUT queue.
                                uint32_t  captureFourcc = 0; ///< V4L2 FourCC for the CAPTURE queue.
                                Size2Du32 size;              ///< Coded picture dimensions.
                                uint32_t  outputBufferCount = 6;  ///< OUTPUT pool depth.
                                uint32_t  captureBufferCount = 6; ///< CAPTURE pool depth.
                                /**
                                 * @brief OUTPUT queue memory model.
                                 *
                                 * @ref Memory::Dmabuf makes the OUTPUT queue
                                 * import a caller-supplied dma-buf fd per frame
                                 * (zero-copy) — use @ref queueOutputDmabuf
                                 * instead of @ref acquireOutput / @ref submitOutput.
                                 */
                                Memory    outputMemory = Memory::Mmap;
                                Memory    captureMemory = Memory::Mmap; ///< CAPTURE queue memory model.
                                /**
                                 * @brief Size hint for the coded queue's plane.
                                 *
                                 * Applied as @c sizeimage on whichever queue
                                 * carries the bitstream (CAPTURE for an encoder,
                                 * OUTPUT for a decoder).  Zero lets the engine
                                 * pick a generous default; the driver may clamp.
                                 */
                                uint32_t codedBufferSize = 0;

                                // Colorimetry applied to the raw queue's format
                                // (the encoder's OUTPUT) so the codec writes a
                                // matching VUI into the bitstream.  Zero =
                                // DEFAULT (leave to the driver).  See
                                // @ref v4l2ColorimetryFromH273.
                                uint32_t colorspace = 0;   ///< @c V4L2_COLORSPACE_*.
                                uint32_t ycbcrEnc = 0;     ///< @c V4L2_YCBCR_ENC_*.
                                uint32_t xferFunc = 0;     ///< @c V4L2_XFER_FUNC_*.
                                uint32_t quantization = 0; ///< @c V4L2_QUANTIZATION_*.
                };

                /** @brief A writable OUTPUT plane handed back by @ref acquireOutput. */
                struct OutPlane {
                                uint8_t *data = nullptr;   ///< MMAP'd plane base; write raw bytes here.
                                size_t   capacity = 0;     ///< Plane length in bytes (the QBUF ceiling).
                                size_t   stride = 0;       ///< Negotiated line stride (@c bytesperline).
                };

                /** @brief A readable CAPTURE plane handed back by @ref dequeueRawFrame. */
                struct CapturePlane {
                                const uint8_t *data = nullptr; ///< MMAP'd plane base; read decoded bytes here.
                                size_t         size = 0;       ///< Filled byte count for this plane.
                                size_t         stride = 0;     ///< Negotiated line stride (@c bytesperline).
                };

                V4l2M2mCodec();
                ~V4l2M2mCodec();
                V4l2M2mCodec(const V4l2M2mCodec &) = delete;
                V4l2M2mCodec &operator=(const V4l2M2mCodec &) = delete;

                /**
                 * @brief Scans @c /dev/video* for a matching mem2mem codec node.
                 *
                 * Returns the path of the first node that reports an M2M
                 * (multiplanar) capability and whose @p role-appropriate
                 * queues enumerate both @p outputFourcc and @p captureFourcc.
                 * Returns an empty String when none is found.
                 */
                static String findDevice(Role role, uint32_t outputFourcc, uint32_t captureFourcc);

                /** @brief Opens and configures both queues, allocates MMAP pools. */
                Error open(const OpenParams &params);

                /** @brief Streams off, unmaps, and closes the device. */
                void close();

                /** @brief True between a successful @ref open and @ref close. */
                bool isOpen() const { return _fd >= 0; }

                /** @brief Raw file descriptor (for @c poll); -1 when closed. */
                int fd() const { return _fd; }

                /** @brief The opened device node path. */
                const String &devPath() const { return _devPath; }

                /** @brief The kernel driver name from @c VIDIOC_QUERYCAP (e.g. @c "vicodec"). */
                const String &driverName() const { return _driver; }

                /** @brief Raw geometry the driver accepted on the raw-frame queue. */
                uint32_t negotiatedWidth() const { return _width; }
                uint32_t negotiatedHeight() const { return _height; } ///< @copydoc negotiatedWidth

                /** @brief Number of memory-planes on the raw-frame (OUTPUT) queue. */
                uint32_t outputPlaneCount() const { return _outPlaneCount; }

                /** @brief Negotiated @c bytesperline of OUTPUT memory-plane @p plane. */
                uint32_t outputStride(uint32_t plane) const;

                /** @brief Colorimetry the driver accepted on the raw queue (post-@ref open). */
                uint32_t negotiatedColorspace() const { return _colorspace; }
                uint32_t negotiatedYcbcrEnc() const { return _ycbcrEnc; }       ///< @copydoc negotiatedColorspace
                uint32_t negotiatedXferFunc() const { return _xferFunc; }       ///< @copydoc negotiatedColorspace
                uint32_t negotiatedQuantization() const { return _quantization; } ///< @copydoc negotiatedColorspace

                /**
                 * @brief Programs one integer codec control (@c VIDIOC_S_EXT_CTRLS).
                 *
                 * @param id       A @c V4L2_CID_MPEG_VIDEO_* control id.
                 * @param value    The integer value.
                 * @param optional When true (the default) a driver that
                 *                 rejects the control (@c EINVAL — not all
                 *                 SoCs implement every knob) is logged at
                 *                 debug level and @c Error::Ok is returned, so
                 *                 best-effort control programming does not
                 *                 abort session setup.  When false the
                 *                 rejection is surfaced as @c Error::DeviceError.
                 */
                Error setControl(uint32_t id, int32_t value, bool optional = true);

                /**
                 * @brief Programs one compound (pointer) codec control.
                 *
                 * For controls whose payload is a struct rather than an
                 * integer — e.g. the HDR10 mastering-display / CLL controls.
                 * Sets @c v4l2_ext_control::ptr + @c size.
                 *
                 * @param id       A compound @c V4L2_CID_* control id.
                 * @param payload  Pointer to the control's struct.
                 * @param size     @c sizeof the struct.
                 * @param optional When true (default) a driver that rejects the
                 *                 control is logged at debug level and
                 *                 @c Error::Ok is returned (best-effort).
                 */
                Error setControlCompound(uint32_t id, void *payload, uint32_t size, bool optional = true);

                /**
                 * @brief @c VIDIOC_STREAMON on both queues (encoder path).
                 *
                 * Allocates both MMAP pools and streams both queues on at
                 * once.  Valid only when both formats are known up front —
                 * i.e. an encoder, where the raw OUTPUT geometry is supplied
                 * to @ref open.  A stateful decoder cannot use this (its
                 * CAPTURE geometry is unknown until the first
                 * @c SOURCE_CHANGE event); it uses @ref startOutput +
                 * @ref setupCapture instead.
                 */
                Error start();

                /** @brief @c VIDIOC_STREAMOFF on both queues. */
                Error stop();

                // ---- Staged decoder init (Role::Decoder) ----
                //
                // A stateful decoder is brought up in two phases: feed coded
                // data on the OUTPUT queue, wait for the driver to parse the
                // stream geometry (a SOURCE_CHANGE event), then configure and
                // stream the CAPTURE (raw) queue.

                /**
                 * @brief Allocates + streams the OUTPUT (coded) queue only.
                 *
                 * The decoder feeds coded access units via
                 * @ref acquireOutput / @ref submitOutput after this; the
                 * CAPTURE queue stays down until @ref setupCapture.
                 */
                Error startOutput();

                /**
                 * @brief Dequeues one pending driver event (non-blocking).
                 *
                 * @param[out] sourceChange True on a resolution
                 *                           @c V4L2_EVENT_SOURCE_CHANGE — the
                 *                           cue to call @ref setupCapture.
                 * @param[out] eos          True on @c V4L2_EVENT_EOS.
                 * @return @c Error::Ok when an event was dequeued,
                 *         @c Error::NotReady when the event queue is empty.
                 */
                Error dequeueEvent(bool &sourceChange, bool &eos);

                /**
                 * @brief Configures + streams the CAPTURE (raw) queue after a
                 *        source change.
                 *
                 * Reads the driver's negotiated raw format via @c G_FMT
                 * (optionally pinning the raw FourCC requested in
                 * @ref OpenParams::captureFourcc), allocates the CAPTURE pool,
                 * queues every buffer, and streams the queue on.  After this
                 * the capture geometry accessors are valid and
                 * @ref dequeueRawFrame yields decoded frames.
                 */
                Error setupCapture();

                /** @brief True once @ref setupCapture has configured the CAPTURE queue. */
                bool captureConfigured() const { return _captureConfigured; }

                /** @brief Negotiated raw CAPTURE geometry (valid after @ref setupCapture). */
                uint32_t captureWidth() const { return _capWidth; }
                uint32_t captureHeight() const { return _capHeight; }     ///< @copydoc captureWidth
                uint32_t captureFourcc() const { return _capFourcc; }     ///< @copydoc captureWidth
                uint32_t capturePlaneCount() const { return _capPlaneCount; } ///< @copydoc captureWidth
                /** @brief Negotiated @c bytesperline of CAPTURE plane @p plane. */
                uint32_t captureStride(uint32_t plane) const;

                /**
                 * @brief Dequeues one decoded CAPTURE frame (non-blocking).
                 *
                 * Exposes the MMAP'd planes for the caller to copy out; the
                 * buffer is @em not requeued until @ref requeueRawFrame is
                 * called with the returned @p index.
                 *
                 * @param[out] planes      One entry per CAPTURE memory-plane.
                 * @param[out] index       Buffer index to pass to @ref requeueRawFrame.
                 * @param[out] ptsUsec     Timestamp threaded from the source buffer.
                 * @param[out] endOfStream True on the post-STOP @c LAST sentinel.
                 * @return @c Error::Ok on a decoded frame, @c Error::NotReady
                 *         when none is ready.
                 */
                Error dequeueRawFrame(List<CapturePlane> &planes, int &index, int64_t &ptsUsec,
                                      bool &endOfStream);

                /** @brief Requeues a CAPTURE buffer after the caller has consumed it. */
                Error requeueRawFrame(int index);

                /**
                 * @brief Reclaims consumed OUTPUT buffers and hands back a free one.
                 *
                 * Non-blocking.  Dequeues any OUTPUT buffers the codec has
                 * finished with, then returns the writable planes of the
                 * first free buffer.  Returns @c Error::NotReady when every
                 * OUTPUT buffer is still in flight — the caller should drain
                 * the CAPTURE queue (which frees OUTPUT buffers) and retry.
                 *
                 * @param[out] index  Opaque buffer index to pass to @ref submitOutput.
                 * @param[out] planes One entry per OUTPUT memory-plane.
                 */
                Error acquireOutput(int &index, List<OutPlane> &planes);

                /**
                 * @brief Queues a filled OUTPUT buffer into the codec.
                 *
                 * @param index     Index from the matching @ref acquireOutput.
                 * @param bytesused One byte count per memory-plane.
                 * @param ptsUsec   Presentation timestamp (microseconds); the
                 *                  codec copies it onto the matching CAPTURE
                 *                  buffer so it threads through @ref dequeueCapture.
                 */
                Error submitOutput(int index, const List<size_t> &bytesused, int64_t ptsUsec);

                /**
                 * @brief Queues an imported dma-buf into the OUTPUT queue (zero-copy).
                 *
                 * For an OUTPUT queue opened with @ref Memory::Dmabuf: reclaims
                 * consumed buffers, then queues @p fd into a free slot.  No
                 * copy — the codec reads the caller's dma-buf directly.  The
                 * caller must keep @p fd (and its backing memory) alive until
                 * the buffer is dequeued (a later @ref queueOutputDmabuf /
                 * @ref acquireOutput reclaims it).
                 *
                 * @param      fd        The dma-buf file descriptor for the frame.
                 * @param      bytesused Filled byte count (whole frame).
                 * @param      ptsUsec   Presentation timestamp; threaded to the
                 *                        matching CAPTURE buffer.
                 * @param[out]  queued    True when a free slot accepted the fd;
                 *                         false when every OUTPUT buffer is in
                 *                         flight (drain CAPTURE and retry).
                 */
                Error queueOutputDmabuf(int fd, size_t bytesused, int64_t ptsUsec, bool &queued);

                /**
                 * @brief Exports a buffer as a dma-buf fd (@c VIDIOC_EXPBUF).
                 *
                 * Lets a downstream consumer (display, GPU, another codec)
                 * import the buffer zero-copy.  Valid for @ref Memory::Mmap
                 * queues.  The caller owns the returned fd and must @c close it.
                 *
                 * @param capture True for the CAPTURE queue, false for OUTPUT.
                 * @param index   Buffer index in the pool.
                 * @return The dma-buf fd, or an error.
                 */
                Result<int> exportBuffer(bool capture, int index);

                /**
                 * @brief Drains one finished CAPTURE buffer (non-blocking).
                 *
                 * On success copies the produced bytes into @p out (owned),
                 * requeues the kernel buffer (unless it was the post-STOP
                 * @c LAST buffer), and reports timestamp / flags.
                 *
                 * @param[out] out         Receives the produced bytes.
                 * @param[out] ptsUsec     Timestamp copied from the source buffer.
                 * @param[out] keyframe    True when @c V4L2_BUF_FLAG_KEYFRAME is set.
                 * @param[out] endOfStream True when the @c V4L2_BUF_FLAG_LAST
                 *                          drain sentinel is seen.
                 * @return @c Error::Ok when a buffer was produced,
                 *         @c Error::NotReady when none is ready, or a device
                 *         error.
                 */
                Error dequeueCapture(Buffer &out, int64_t &ptsUsec, bool &keyframe, bool &endOfStream);

                /**
                 * @brief Waits for queue activity.
                 *
                 * @param[out] outputWritable True when an OUTPUT buffer can be queued.
                 * @param[out] captureReadable True when a CAPTURE buffer is ready.
                 * @param      timeoutMs       Milliseconds to wait; negative blocks.
                 */
                Error poll(bool &outputWritable, bool &captureReadable, int timeoutMs);

                /**
                 * @brief Waits for queue activity or a driver event.
                 *
                 * Like @ref poll but also reports @c POLLPRI (the
                 * @c SOURCE_CHANGE / @c EOS event channel) used by the
                 * staged decoder path.
                 */
                Error pollEvents(bool &outputWritable, bool &captureReadable, bool &eventPending, int timeoutMs);

                /**
                 * @brief Begins the drain sequence (@c VIDIOC_ENCODER_CMD /
                 *        @c VIDIOC_DECODER_CMD with the @c STOP command).
                 *
                 * After this the codec flushes buffered frames and marks the
                 * final CAPTURE buffer with @c V4L2_BUF_FLAG_LAST.
                 */
                Error sendStop();

        private:
                struct MappedPlane {
                                void  *start = nullptr;
                                size_t length = 0;
                };
                struct MappedBuffer {
                                List<MappedPlane> planes;
                                bool              queued = false;
                };

                Error negotiateFormats(const OpenParams &p);
                Error allocQueue(uint32_t type, uint32_t memory, uint32_t count, List<MappedBuffer> &pool,
                                 uint32_t &planeCount);
                void  unmapQueue(List<MappedBuffer> &pool);
                Error queueAllCapture();
                void  reclaimOutput();
                Error subscribeEvents();

                int    _fd = -1;
                String _devPath;
                String _driver;
                Role   _role = Role::Encoder;

                // True for the multiplanar API (V4L2_CAP_VIDEO_M2M_MPLANE — the
                // Xilinx VCU and Raspberry Pi); false for single-planar
                // (V4L2_CAP_VIDEO_M2M — the kernel vicodec test driver and many
                // simpler codecs).  Selects the buffer types and the
                // v4l2_format / v4l2_buffer layout used throughout.
                bool _mplane = true;

                uint32_t _outputType = 0;  // V4L2_BUF_TYPE_VIDEO_OUTPUT[_MPLANE]
                uint32_t _captureType = 0; // V4L2_BUF_TYPE_VIDEO_CAPTURE[_MPLANE]

                uint32_t _outputMemory = 0;  // V4L2_MEMORY_MMAP | V4L2_MEMORY_DMABUF
                uint32_t _captureMemory = 0; // V4L2_MEMORY_MMAP | V4L2_MEMORY_DMABUF

                // Requested pool depths (OpenParams::outputBufferCount /
                // captureBufferCount); the driver may round these up.
                uint32_t _outputBufferCount = 6;
                uint32_t _captureBufferCount = 6;

                uint32_t _width = 0;
                uint32_t _height = 0;
                uint32_t _outPlaneCount = 1;
                uint32_t _capPlaneCount = 1;
                uint32_t _outStride[4] = {0, 0, 0, 0};

                // Negotiated CAPTURE (raw) geometry — filled by setupCapture on
                // the decoder path.
                uint32_t _capWidth = 0;
                uint32_t _capHeight = 0;
                uint32_t _capFourcc = 0;
                uint32_t _capStride[4] = {0, 0, 0, 0};
                uint32_t _rawFourccWanted = 0; // Desired CAPTURE raw FourCC (OpenParams::captureFourcc).

                // Colorimetry negotiated on the raw queue (encoder OUTPUT).
                uint32_t _colorspace = 0;
                uint32_t _ycbcrEnc = 0;
                uint32_t _xferFunc = 0;
                uint32_t _quantization = 0;

                List<MappedBuffer> _outputPool;
                List<MappedBuffer> _capturePool;

                bool _streaming = false;          // OUTPUT queue streaming.
                bool _captureConfigured = false;  // CAPTURE queue set up + streaming (decoder).
                bool _captureDrained = false;     // LAST buffer seen.
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_V4L2
