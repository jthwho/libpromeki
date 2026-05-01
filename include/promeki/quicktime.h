/**
 * @file      quicktime.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/result.h>
#include <promeki/list.h>
#include <promeki/buffer.h>
#include <promeki/fourcc.h>
#include <promeki/framerate.h>
#include <promeki/size2d.h>
#include <promeki/pixelformat.h>
#include <promeki/audiodesc.h>
#include <promeki/mediadesc.h>
#include <promeki/metadata.h>
#include <promeki/timecode.h>
#include <promeki/iodevice.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief QuickTime / ISO-BMFF container reader and writer.
 * @ingroup proav
 *
 * Provides a format-level interface for QuickTime (.mov), MP4, and
 * ISO-BMFF (.m4v / .m4a) container files. This class parses and writes
 * the container structure — atoms, sample tables, track descriptors,
 * timecode tracks, and metadata — but does not encode or decode any
 * sample payloads. Compressed sample bytes flow through verbatim in
 * Sample::data buffers, and downstream consumers (or
 * QuickTimeMediaIO, which wraps compressed samples as
 * @ref CompressedVideoPayload) are responsible for running any codec.
 *
 * Tracks are exposed as a plain list; each Track carries its codec
 * (as a PixelFormat for video, an AudioDesc for audio), frame rate,
 * sample count, language, and per-track metadata. Container-level
 * metadata (from @c udta) is available via @c containerMetadata().
 *
 * @par Reader example
 * @code
 * QuickTime qt = QuickTime::createReader("/path/to/clip.mov");
 * if(qt.open() == Error::Ok) {
 *     for(const QuickTime::Track &tk : qt.tracks()) {
 *         if(tk.type() == QuickTime::Video) {
 *             // tk.pixelFormat() identifies the codec (H264, ProRes, ...)
 *         }
 *     }
 * }
 * @endcode
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used concurrently;
 * concurrent access to a single instance must be externally synchronized.
 */
class QuickTime {
        public:
                /** @brief Shared pointer type for QuickTime. */
                using Ptr = SharedPtr<QuickTime>;

                /** @brief Operation mode for a QuickTime instance. */
                enum Operation {
                        InvalidOperation = 0, ///< No valid operation.
                        Reader,               ///< Read an existing container.
                        Writer                ///< Write a new container.
                };

                /** @brief Kind of track carried in a container.
                 *
                 * @note @c TimecodeTrack is named with the @c Track suffix to
                 *       avoid shadowing the @c promeki::Timecode class
                 *       inside the @c QuickTime scope. */
                enum TrackType {
                        InvalidTrack = 0,
                        Video,
                        Audio,
                        TimecodeTrack,
                        Subtitle,
                        Data
                };

                /** @brief On-disk layout choice for writers. */
                enum Layout {
                        /** @brief Classic mdat-then-moov. The complete sample
                         *         tables are written in a single @c moov at
                         *         @c finalize() time. Most space-efficient,
                         *         but the file is unplayable until finalize
                         *         completes, and a crash mid-write leaves
                         *         a broken file. */
                        LayoutClassic = 0,
                        /** @brief Fragmented: ftyp + init-moov + per-fragment
                         *         moof/mdat pairs. Each fragment is
                         *         self-describing and independently playable,
                         *         so a crash mid-write loses at most one
                         *         fragment. Fragments also give player
                         *         demuxers natural packet-buffer reset
                         *         points. */
                        LayoutFragmented = 1
                };

                /**
                 * @brief Descriptor for a single media track in the container.
                 *
                 * Tracks are populated by the engine during open() (for
                 * readers) or addTrack()/writeSample() (for writers).
                 * Callers observe track properties through the const
                 * accessors; the non-const setters exist for the engine's
                 * own use and are not part of the normal consumer API.
                 */
                class Track {
                        public:
                                /** @brief Constructs an invalid (default) track. */
                                Track() = default;

                                /** @brief Returns true if this track has a valid type. */
                                bool isValid() const { return _type != InvalidTrack; }

                                /** @brief Returns the track kind. */
                                TrackType type() const { return _type; }
                                /** @brief Returns the QuickTime track ID. */
                                uint32_t id() const { return _id; }
                                /** @brief Returns the media timescale (ticks per second). */
                                uint32_t timescale() const { return _timescale; }
                                /** @brief Returns the track duration in media-timescale ticks. */
                                uint64_t duration() const { return _duration; }
                                /** @brief Returns the track frame rate (video / timecode). */
                                const FrameRate &frameRate() const { return _frameRate; }
                                /** @brief Returns the number of samples in the track. */
                                uint64_t sampleCount() const { return _sampleCount; }
                                /** @brief Returns the ISO 639-2 language tag (e.g. "eng"). */
                                const String &language() const { return _language; }
                                /** @brief Returns the human-readable track name (if any). */
                                const String &name() const { return _name; }
                                /** @brief Returns the video pixel description (codec). */
                                const PixelFormat &pixelFormat() const { return _pixelFormat; }
                                /** @brief Returns the video dimensions in pixels. */
                                const Size2Du32 &size() const { return _size; }
                                /** @brief Returns the audio description (channel layout, rate). */
                                const AudioDesc &audioDesc() const { return _audioDesc; }
                                /** @brief Returns the per-track metadata. */
                                const Metadata &metadata() const { return _metadata; }
                                /** @brief Returns the elst start offset (in this track's timescale). */
                                int64_t editStartOffset() const { return _editStartOffset; }
                                /**
                                 * @brief Returns the codec-specific
                                 *        configuration record payload
                                 *        (e.g. the bytes inside the
                                 *        @c avcC / @c hvcC box), or
                                 *        an empty Buffer::Ptr if the
                                 *        track has no such record.
                                 */
                                const Buffer::Ptr &codecConfig() const { return _codecConfig; }
                                /**
                                 * @brief Returns the FourCC type of
                                 *        the codec configuration
                                 *        record (@c avcC, @c hvcC,
                                 *        etc.) or zero when absent.
                                 */
                                FourCC codecConfigType() const { return _codecConfigType; }

                                /** @brief Sets the track kind. */
                                void setType(TrackType t) { _type = t; }
                                /** @brief Sets the track ID. */
                                void setId(uint32_t id) { _id = id; }
                                /** @brief Sets the media timescale. */
                                void setTimescale(uint32_t ts) { _timescale = ts; }
                                /** @brief Sets the track duration in timescale ticks. */
                                void setDuration(uint64_t d) { _duration = d; }
                                /** @brief Sets the frame rate. */
                                void setFrameRate(const FrameRate &r) { _frameRate = r; }
                                /** @brief Sets the sample count. */
                                void setSampleCount(uint64_t n) { _sampleCount = n; }
                                /** @brief Sets the ISO 639-2 language tag. */
                                void setLanguage(const String &s) { _language = s; }
                                /** @brief Sets the human-readable track name. */
                                void setName(const String &s) { _name = s; }
                                /** @brief Sets the video pixel description (codec). */
                                void setPixelFormat(const PixelFormat &pd) { _pixelFormat = pd; }
                                /** @brief Sets the video dimensions. */
                                void setSize(const Size2Du32 &s) { _size = s; }
                                /** @brief Sets the audio description. */
                                void setAudioDesc(const AudioDesc &ad) { _audioDesc = ad; }
                                /** @brief Returns a mutable reference to the metadata. */
                                Metadata &metadata() { return _metadata; }
                                /** @brief Sets the elst start offset (in this track's timescale). */
                                void setEditStartOffset(int64_t v) { _editStartOffset = v; }
                                /** @brief Sets the codec configuration record payload. */
                                void setCodecConfig(const Buffer::Ptr &b) { _codecConfig = b; }
                                /** @brief Sets the FourCC type of the codec configuration record. */
                                void setCodecConfigType(FourCC t) { _codecConfigType = t; }

                        private:
                                TrackType   _type = InvalidTrack;
                                uint32_t    _id = 0;
                                uint32_t    _timescale = 0;
                                uint64_t    _duration = 0;
                                FrameRate   _frameRate;
                                uint64_t    _sampleCount = 0;
                                String      _language;
                                String      _name;
                                PixelFormat _pixelFormat;
                                Size2Du32   _size;
                                AudioDesc   _audioDesc;
                                Metadata    _metadata;
                                int64_t     _editStartOffset = 0;
                                Buffer::Ptr _codecConfig;
                                FourCC      _codecConfigType{'\0', '\0', '\0', '\0'};
                };

                /** @brief Plain-value list of tracks. */
                using TrackList = List<Track>;

                /**
                 * @brief One encoded sample extracted from (or destined for) a track.
                 *
                 * @c data carries the raw encoded payload bytes — for compressed
                 * codecs this is the bitstream chunk verbatim, for uncompressed
                 * formats it is the pixel/sample bytes. The buffer is allocated
                 * by the engine when read; on writes the caller supplies it.
                 */
                struct Sample {
                                uint32_t    trackId = 0;      ///< QuickTime track ID.
                                uint64_t    index = 0;        ///< 0-based sample index within the track.
                                int64_t     dts = 0;          ///< Decode timestamp in track timescale.
                                int64_t     pts = 0;          ///< Presentation timestamp (dts + ctts offset).
                                uint64_t    duration = 0;     ///< Sample duration in track timescale.
                                bool        keyframe = false; ///< True if this is a sync (key) sample.
                                Buffer::Ptr data;             ///< Raw encoded payload bytes.
                };

                /**
                 * @brief Abstract backend for a QuickTime instance.
                 *
                 * Readers and writers derive from this base. The base
                 * provides default "not implemented" behavior for every
                 * virtual so that a partially-implemented backend still
                 * compiles and returns meaningful errors.
                 */
                class Impl {
                                PROMEKI_SHARED(Impl)
                        public:
                                /** @brief Constructs an Impl for the given operation. */
                                explicit Impl(Operation op) : _operation(op) {}

                                /** @brief Virtual destructor. Releases the owned device if any. */
                                virtual ~Impl();

                                /** @brief Returns the operation this backend performs. */
                                Operation operation() const { return _operation; }

                                /** @brief Returns the filename associated with the backend. */
                                const String &filename() const { return _filename; }

                                /** @brief Sets the filename. Not valid once open(). */
                                void setFilename(const String &fn) { _filename = fn; }

                                /** @brief Returns the IODevice (if set), or nullptr. */
                                IODevice *device() const { return _device; }

                                /**
                                 * @brief Sets the IODevice. The caller retains ownership unless
                                 *        @c takeOwnership is true.
                                 */
                                void setDevice(IODevice *dev, bool takeOwnership = false) {
                                        _device = dev;
                                        _ownsDevice = takeOwnership;
                                }

                                /** @brief Opens the container. Default: returns NotImplemented. */
                                virtual Error open();

                                /** @brief Closes the container. Default: no-op. */
                                virtual void close();

                                /** @brief Returns true if the container is currently open. */
                                virtual bool isOpen() const;

                                /**
                                 * @brief Reads the @p sampleIndex'th sample from track
                                 *        @p trackIndex into @p out.
                                 *
                                 * @param trackIndex 0-based position in @c tracks().
                                 * @param sampleIndex 0-based sample within the track.
                                 * @param out Populated on success with the sample
                                 *            metadata and an allocated payload buffer.
                                 * @return Error::Ok on success.
                                 */
                                virtual Error readSample(size_t trackIndex, uint64_t sampleIndex, Sample &out);

                                /**
                                 * @brief Reads @p count consecutive samples starting
                                 *        at @p startSampleIndex as a single
                                 *        concatenated payload.
                                 *
                                 * Useful for pulling a block of PCM audio in one go
                                 * without N individual @c readSample calls. The
                                 * returned @c Sample's metadata reflects the first
                                 * sample in the range, and @c data contains the
                                 * concatenated bytes of all @p count samples.
                                 *
                                 * The default implementation calls @c readSample in
                                 * a loop; backends can override for efficiency.
                                 *
                                 * @return Error::Ok on success.
                                 */
                                virtual Error readSampleRange(size_t trackIndex, uint64_t startSampleIndex,
                                                              uint64_t count, Sample &out);

                                /**
                                 * @brief Adds a video track to a writer.
                                 *
                                 * @param codec      Pixel description identifying the codec.
                                 * @param size       Pixel dimensions of the encoded video.
                                 * @param frameRate  Track frame rate.
                                 * @param outTrackId If non-null, receives the assigned track ID.
                                 * @return Error::Ok on success.
                                 */
                                virtual Error addVideoTrack(const PixelFormat &codec, const Size2Du32 &size,
                                                            const FrameRate &frameRate, uint32_t *outTrackId);

                                /**
                                 * @brief Adds a PCM audio track to a writer.
                                 *
                                 * @param desc       Audio description (format, channels, rate).
                                 * @param outTrackId If non-null, receives the assigned track ID.
                                 * @return Error::Ok on success.
                                 */
                                virtual Error addAudioTrack(const AudioDesc &desc, uint32_t *outTrackId);

                                /**
                                 * @brief Adds a single-sample @c tmcd timecode track to a writer.
                                 *
                                 * @param startTimecode The starting timecode (its mode determines
                                 *                      the timescale and drop-frame flag).
                                 * @param frameRate     Track frame rate (matches the video track).
                                 * @param outTrackId    If non-null, receives the assigned track ID.
                                 * @return Error::Ok on success.
                                 */
                                virtual Error addTimecodeTrack(const Timecode  &startTimecode,
                                                               const FrameRate &frameRate, uint32_t *outTrackId);

                                /**
                                 * @brief Appends an encoded sample to a writer track.
                                 *
                                 * The sample's @c data buffer is written to the @c mdat
                                 * payload at the current write cursor and a sample-table
                                 * entry is recorded for finalize().
                                 *
                                 * @param trackId Track ID returned by an earlier @c addXxxTrack.
                                 * @param sample  The sample to append. @c data must be non-null;
                                 *                @c duration is in the track's media timescale,
                                 *                and @c keyframe is honored for sync-sample lists.
                                 * @return Error::Ok on success.
                                 */
                                virtual Error writeSample(uint32_t trackId, const Sample &sample);

                                /** @brief Sets the container-level metadata (udta). */
                                virtual void setContainerMetadata(const Metadata &meta);

                                /** @brief Returns the on-disk layout used by this writer. */
                                Layout layout() const { return _layout; }

                                /**
                                 * @brief Sets the on-disk layout. Only valid
                                 *        before @c open(); returns
                                 *        @c AlreadyOpen otherwise.
                                 */
                                virtual Error setLayout(Layout layout);

                                /** @brief Returns true if the writer syncs to stable storage on each flush. */
                                bool flushSync() const { return _flushSync; }

                                /**
                                 * @brief Enables or disables forcing an @c fdatasync
                                 *        after each successful @c flush().
                                 *
                                 * When enabled, each fragment boundary becomes a
                                 * durable checkpoint: the data is guaranteed to
                                 * have reached stable storage before @c flush()
                                 * returns. Useful for crash-critical capture
                                 * workflows where losing the last fragment is
                                 * unacceptable. Expensive — adds latency at every
                                 * fragment boundary.
                                 *
                                 * Default: false.
                                 */
                                void setFlushSync(bool enable) { _flushSync = enable; }

                                /**
                                 * @brief Flushes the current fragment to disk.
                                 *
                                 * For @c LayoutFragmented writers, emits a
                                 * @c moof + @c mdat pair containing all
                                 * samples written since the last flush and
                                 * resets the per-fragment state.
                                 *
                                 * For @c LayoutClassic writers, this is a
                                 * no-op (the classic layout has no
                                 * notion of flushable sub-units).
                                 *
                                 * @return Error::Ok on success.
                                 */
                                virtual Error flush();

                                /**
                                 * @brief Finalizes a writer: builds @c moov, writes it after
                                 *        @c mdat, patches the @c mdat size, and closes the
                                 *        underlying file.
                                 */
                                virtual Error finalize();

                                /** @brief Returns the list of tracks discovered by open(). */
                                const TrackList &tracks() const { return _tracks; }

                                /** @brief Returns the aggregated MediaDesc derived from the tracks. */
                                const MediaDesc &mediaDesc() const { return _mediaDesc; }

                                /** @brief Returns container-level metadata (from udta, etc). */
                                const Metadata &containerMetadata() const { return _containerMetadata; }

                                /**
                                 * @brief Returns the anchor timecode resolved from the
                                 *        container's @c tmcd track, or an invalid
                                 *        Timecode if no timecode track is present.
                                 *
                                 * Per-frame timecode is computed by advancing this
                                 * anchor by the requested frame index — only the
                                 * starting timecode (and the @c tmcd flags such as
                                 * drop-frame) is parsed during open().
                                 */
                                const Timecode &startTimecode() const { return _startTimecode; }

                        protected:
                                Operation _operation;
                                Layout    _layout = LayoutClassic;
                                bool      _flushSync = false;
                                String    _filename;
                                IODevice *_device = nullptr;
                                bool      _ownsDevice = false;
                                TrackList _tracks;
                                MediaDesc _mediaDesc;
                                Metadata  _containerMetadata;
                                Timecode  _startTimecode;
                };

                /** @brief Creates a QuickTime reader for the given filename. */
                static QuickTime createReader(const String &filename);

                /** @brief Creates a QuickTime writer for the given filename. */
                static QuickTime createWriter(const String &filename);

                /**
                 * @brief Creates a QuickTime instance that operates on an IODevice.
                 *
                 * The device must be seekable. The caller retains ownership.
                 * @note IODevice-based operation is reserved for future use;
                 *       current backends only implement file-based I/O.
                 */
                static Result<QuickTime> createForOperation(Operation op, IODevice *device);

                /** @brief Default constructor. Creates an invalid QuickTime. */
                QuickTime() : d(SharedPtr<Impl>::create(InvalidOperation)) {}

                /** @brief Constructs a QuickTime from an Impl pointer, taking ownership. */
                explicit QuickTime(Impl *impl) : d(SharedPtr<Impl>::takeOwnership(impl)) {}

                /** @brief Returns true if this QuickTime has a valid operation. */
                bool isValid() const { return d->operation() != InvalidOperation; }

                /** @brief Returns the operation this instance performs. */
                Operation operation() const { return d->operation(); }

                /** @brief Returns the filename associated with this instance. */
                const String &filename() const { return d->filename(); }

                /** @brief Sets the filename. Only valid before open(). */
                void setFilename(const String &fn) { d.modify()->setFilename(fn); }

                /** @brief Returns the associated IODevice, or nullptr. */
                IODevice *device() const { return d->device(); }

                /** @brief Opens the container. */
                Error open() { return d.modify()->open(); }

                /** @brief Closes the container. */
                void close() { d.modify()->close(); }

                /** @brief Returns true if the container is open. */
                bool isOpen() const { return d->isOpen(); }

                /** @brief Reads a single sample from the given track index. */
                Error readSample(size_t trackIndex, uint64_t sampleIndex, Sample &out) {
                        return d.modify()->readSample(trackIndex, sampleIndex, out);
                }

                /** @brief Reads @p count consecutive samples starting at
                 *         @p startSampleIndex as a single concatenated payload. */
                Error readSampleRange(size_t trackIndex, uint64_t startSampleIndex, uint64_t count, Sample &out) {
                        return d.modify()->readSampleRange(trackIndex, startSampleIndex, count, out);
                }

                /** @brief Adds a video track to the writer. */
                Error addVideoTrack(const PixelFormat &codec, const Size2Du32 &size, const FrameRate &frameRate,
                                    uint32_t *outTrackId = nullptr) {
                        return d.modify()->addVideoTrack(codec, size, frameRate, outTrackId);
                }

                /** @brief Adds a PCM audio track to the writer. */
                Error addAudioTrack(const AudioDesc &desc, uint32_t *outTrackId = nullptr) {
                        return d.modify()->addAudioTrack(desc, outTrackId);
                }

                /** @brief Adds a timecode track to the writer. */
                Error addTimecodeTrack(const Timecode &startTimecode, const FrameRate &frameRate,
                                       uint32_t *outTrackId = nullptr) {
                        return d.modify()->addTimecodeTrack(startTimecode, frameRate, outTrackId);
                }

                /** @brief Appends a sample to the writer. */
                Error writeSample(uint32_t trackId, const Sample &sample) {
                        return d.modify()->writeSample(trackId, sample);
                }

                /** @brief Sets container-level metadata for the writer. */
                void setContainerMetadata(const Metadata &meta) { d.modify()->setContainerMetadata(meta); }

                /** @brief Returns the writer's on-disk layout. */
                Layout layout() const { return d->layout(); }

                /** @brief Sets the on-disk layout (before open()). */
                Error setLayout(Layout layout) { return d.modify()->setLayout(layout); }

                /** @brief Returns whether flushSync is enabled on the writer. */
                bool flushSync() const { return d->flushSync(); }

                /** @brief Enables/disables fdatasync-on-flush for crash-critical writers. */
                void setFlushSync(bool enable) { d.modify()->setFlushSync(enable); }

                /** @brief Flushes the current fragment (fragmented writers only). */
                Error flush() { return d.modify()->flush(); }

                /** @brief Finalizes the writer (builds moov and closes the file). */
                Error finalize() { return d.modify()->finalize(); }

                /** @brief Returns the list of tracks. */
                const TrackList &tracks() const { return d->tracks(); }

                /** @brief Returns the aggregated MediaDesc. */
                const MediaDesc &mediaDesc() const { return d->mediaDesc(); }

                /** @brief Returns container-level metadata. */
                const Metadata &containerMetadata() const { return d->containerMetadata(); }

                /** @brief Returns the anchor timecode (or invalid). */
                const Timecode &startTimecode() const { return d->startTimecode(); }

        private:
                SharedPtr<Impl> d;
};

PROMEKI_NAMESPACE_END
