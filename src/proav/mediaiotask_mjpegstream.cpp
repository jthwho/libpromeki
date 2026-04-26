/**
 * @file      mediaiotask_mjpegstream.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <chrono>
#include <cstring>

#include <promeki/mediaiotask_mjpegstream.h>

#include <promeki/asyncbufferqueue.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/frame.h>
#include <promeki/httprequest.h>
#include <promeki/httpresponse.h>
#include <promeki/httpserver.h>
#include <promeki/httpstatus.h>
#include <promeki/imagedesc.h>
#include <promeki/jpegvideocodec.h>
#include <promeki/logger.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/pixelformat.h>
#include <promeki/rational.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/variantspec.h>
#include <promeki/videoencoder.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(MediaIOTask_MjpegStream)

PROMEKI_REGISTER_MEDIAIO(MediaIOTask_MjpegStream)

const String MediaIOTask_MjpegStream::HttpBoundary("promeki-mjpeg");
const String MediaIOTask_MjpegStream::LatestJpegContentType("image/jpeg");

namespace {

        bool isJpegEncoderInputFormat(const PixelFormat &pd) {
                if (!pd.isValid() || pd.isCompressed()) return false;
                const List<PixelFormat> supported = VideoCodec(VideoCodec::JPEG).encoderSupportedInputs();
                for (const PixelFormat &cand : supported) {
                        if (cand == pd) return true;
                }
                return false;
        }

        PixelFormat jpegFallbackFormat(const PixelFormat &offered) {
                // Prefer a chroma-related fallback when the source already
                // implies a YCbCr pipeline; otherwise fall back to RGBA8 so
                // the planner inserts a simple CSC bridge.
                if (offered.isValid() && !offered.isCompressed()) {
                        const auto &ml = offered.memLayout();
                        if (ml.sampling() == PixelMemLayout::Sampling420) {
                                return PixelFormat(PixelFormat::YUV8_420_Planar_Rec709);
                        }
                        if (ml.sampling() == PixelMemLayout::Sampling422) {
                                return PixelFormat(PixelFormat::YUV8_422_Rec709);
                        }
                        if (ml.sampling() == PixelMemLayout::Sampling444) {
                                return PixelFormat(PixelFormat::YUV8_422_Rec709);
                        }
                }
                return PixelFormat(PixelFormat::RGBA8_sRGB);
        }

        Buffer::Ptr copyPayloadToBuffer(const CompressedVideoPayload &cvp) {
                const size_t total = cvp.size();
                Buffer::Ptr  out = Buffer::Ptr::create(total);
                if (!out.isValid() || total == 0) {
                        if (out.isValid()) out.modify()->setSize(0);
                        return out;
                }
                Buffer     *raw = out.modify();
                size_t      cursor = 0;
                const auto &view = cvp.data();
                for (size_t i = 0; i < view.count(); ++i) {
                        const auto entry = view[i];
                        if (entry.size() == 0 || entry.data() == nullptr) continue;
                        std::memcpy(static_cast<uint8_t *>(raw->data()) + cursor, entry.data(), entry.size());
                        cursor += entry.size();
                }
                raw->setSize(cursor);
                return out;
        }

} // namespace

// ---------------------------------------------------------------------------
// MediaIO factory descriptor
// ---------------------------------------------------------------------------

MediaIO::FormatDesc MediaIOTask_MjpegStream::formatDesc() {
        return {"MjpegStream",
                "MJPEG Preview Stream",
                "Frame-rate-limited motion-JPEG preview sink "
                "(HTTP multipart-friendly).",
                {},    // No file extensions — pure sink.
                false, // canBeSource
                true,  // canBeSink
                false, // canBeTransform
                []() -> MediaIOTask * { return new MediaIOTask_MjpegStream(); },
                []() -> MediaIO::Config::SpecMap {
                        MediaIO::Config::SpecMap specs;
                        auto                     s = [&specs](MediaConfig::ID id, const Variant &def) {
                                const VariantSpec *gs = MediaConfig::spec(id);
                                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
                        };
                        s(MediaConfig::MjpegMaxFps, Rational<int>(15, 1));
                        s(MediaConfig::MjpegQuality, int32_t(80));
                        s(MediaConfig::MjpegMaxQueueFrames, int32_t(1));
                        return specs;
                },
                []() -> Metadata {
                        return Metadata();
                }};
}

// ---------------------------------------------------------------------------
// Construction / lifecycle
// ---------------------------------------------------------------------------

MediaIOTask_MjpegStream::MediaIOTask_MjpegStream() = default;

MediaIOTask_MjpegStream::~MediaIOTask_MjpegStream() {
        // The owning MediaIO has already routed Close through the
        // strand by the time we get here, so the subscriber list and
        // ring are empty under normal teardown.  Belt-and-braces:
        // notify any stragglers and clear, in case a test or a
        // direct-task user destructs without a prior close().
        List<MjpegStreamSubscriber *> stragglers;
        {
                Mutex::Locker lk(_stateMutex);
                for (auto it = _subscribers.cbegin(); it != _subscribers.cend(); ++it) {
                        stragglers.pushToBack(it->second);
                }
                _subscribers.clear();
                _ring.clear();
        }
        for (MjpegStreamSubscriber *s : stragglers) {
                if (s != nullptr) s->onClosed();
        }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

int MediaIOTask_MjpegStream::attachSubscriber(MjpegStreamSubscriber *s) {
        if (s == nullptr) return -1;
        Buffer::Ptr primer;
        TimeStamp   primerTs;
        bool        hasPrimer = false;
        int         id = -1;
        {
                Mutex::Locker lk(_stateMutex);
                id = _nextSubscriberId++;
                _subscribers.insert(id, s);
                if (!_ring.isEmpty()) {
                        const RingEntry &back = _ring[_ring.size() - 1];
                        if (back.jpeg.isValid() && back.jpeg->size() > 0) {
                                primer = back.jpeg;
                                primerTs = back.timestamp;
                                hasPrimer = true;
                        }
                }
        }
        // Prime outside the lock — the subscriber is allowed to do
        // its own (cheap) work in onFrame and we don't want it
        // contending with the encoder strand.
        if (hasPrimer) s->onFrame(primer, primerTs);
        return id;
}

void MediaIOTask_MjpegStream::detachSubscriber(int id) {
        MjpegStreamSubscriber *removed = nullptr;
        {
                Mutex::Locker lk(_stateMutex);
                auto          it = _subscribers.find(id);
                if (it != _subscribers.end()) {
                        removed = it->second;
                        _subscribers.remove(id);
                }
        }
        if (removed != nullptr) removed->onClosed();
}

Buffer::Ptr MediaIOTask_MjpegStream::latestJpeg() const {
        Buffer::Ptr jpeg;
        TimeStamp   ts;
        latestRingEntry(&jpeg, &ts);
        return jpeg;
}

TimeStamp MediaIOTask_MjpegStream::latestJpegTimestamp() const {
        Buffer::Ptr jpeg;
        TimeStamp   ts;
        latestRingEntry(&jpeg, &ts);
        return ts;
}

MjpegStreamSnapshot MediaIOTask_MjpegStream::snapshot() const {
        Mutex::Locker lk(_stateMutex);
        return _stats;
}

bool MediaIOTask_MjpegStream::isStreaming() const {
        Mutex::Locker lk(_stateMutex);
        return _isStreaming;
}

bool MediaIOTask_MjpegStream::latestRingEntry(Buffer::Ptr *out, TimeStamp *outTs) const {
        Mutex::Locker lk(_stateMutex);
        if (_ring.isEmpty()) return false;
        const RingEntry &back = _ring[_ring.size() - 1];
        if (out != nullptr) *out = back.jpeg;
        if (outTs != nullptr) *outTs = back.timestamp;
        return true;
}

// ---------------------------------------------------------------------------
// Strand / executeCmd plumbing
// ---------------------------------------------------------------------------

Error MediaIOTask_MjpegStream::executeCmd(MediaIOCommandOpen &cmd) {
        if (cmd.mode != MediaIO::Sink) return Error::NotSupported;

        const MediaIO::Config &cfg = cmd.config;

        Rational<int> configuredFps = cfg.getAs<Rational<int>>(MediaConfig::MjpegMaxFps, Rational<int>(15, 1));
        if (configuredFps.numerator() <= 0 || configuredFps.denominator() <= 0) {
                _maxRate = FrameRate();
                _period = Duration();
        } else {
                FrameRate::RationalType r(static_cast<unsigned int>(configuredFps.numerator()),
                                          static_cast<unsigned int>(configuredFps.denominator()));
                _maxRate = FrameRate(r);
                _period = _maxRate.frameDuration();
        }

        _quality = cfg.getAs<int>(MediaConfig::MjpegQuality, 80);
        if (_quality < 1) _quality = 1;
        if (_quality > 100) _quality = 100;

        _ringDepth = cfg.getAs<int>(MediaConfig::MjpegMaxQueueFrames, 1);
        if (_ringDepth < 1) _ringDepth = 1;
        if (_ringDepth > 16) _ringDepth = 16;

        // Build the JPEG encoder eagerly so the first incoming frame
        // doesn't pay the libjpeg-turbo allocation cost.  The encoder
        // is configured with the latched MjpegQuality forwarded as
        // MediaConfig::JpegQuality.
        MediaConfig encCfg;
        encCfg.set(MediaConfig::JpegQuality, _quality);
        encCfg.set(MediaConfig::OutputPixelFormat, PixelFormat(PixelFormat::JPEG_RGB8_sRGB));
        auto encResult = VideoCodec(VideoCodec::JPEG).createEncoder(&encCfg);
        if (error(encResult).isError()) {
                promekiErr("MediaIOTask_MjpegStream: createEncoder failed: %s", error(encResult).name().cstr());
                return Error::NotSupported;
        }
        _encoder = VideoEncoder::UPtr::takeOwnership(value(encResult));

        _hasLastEncoded = false;
        _lastEncoded = TimeStamp();
        _frameSerial = 0;
        {
                Mutex::Locker lk(_stateMutex);
                _ring.clear();
                _stats = MjpegStreamSnapshot{};
                _isStreaming = true;
        }

        promekiInfo("MediaIOTask_MjpegStream: opened maxFps=%.4f quality=%d "
                    "ringDepth=%d",
                    _maxRate.isValid() ? _maxRate.toDouble() : 0.0, _quality, _ringDepth);

        _isOpen = true;
        cmd.canSeek = false;
        cmd.frameCount = MediaIO::FrameCountInfinite;
        return Error::Ok;
}

Error MediaIOTask_MjpegStream::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        if (!_isOpen) return Error::Ok;

        // Hand a final onClosed() to every still-attached subscriber
        // and clear the ring.  Snapshot the subscriber list under the
        // lock then dispatch outside it so subscribers can re-enter
        // attach/detach if they want to (they shouldn't, but it's the
        // cleaner contract).
        List<MjpegStreamSubscriber *> finalDispatch;
        {
                Mutex::Locker lk(_stateMutex);
                for (auto it = _subscribers.cbegin(); it != _subscribers.cend(); ++it) {
                        finalDispatch.pushToBack(it->second);
                }
                _subscribers.clear();
                _ring.clear();
                _isStreaming = false;
        }
        for (MjpegStreamSubscriber *s : finalDispatch) {
                if (s != nullptr) s->onClosed();
        }

        if (_encoder.isValid()) {
                _encoder->flush();
                _encoder.clear();
        }

        MjpegStreamSnapshot snap;
        {
                Mutex::Locker lk(_stateMutex);
                snap = _stats;
        }
        const double avgUs = snap.encodeSamples > 0
                                     ? static_cast<double>(snap.totalEncodeUs) / static_cast<double>(snap.encodeSamples)
                                     : 0.0;
        promekiInfo("MediaIOTask_MjpegStream: closed — encoded %lld, "
                    "rateLimited %lld, encodeError %lld, "
                    "avgEncodeUs %.1f, peakEncodeUs %lld, bytes %lld",
                    static_cast<long long>(snap.framesEncoded), static_cast<long long>(snap.framesRateLimited),
                    static_cast<long long>(snap.framesEncodeError), avgUs, static_cast<long long>(snap.peakEncodeUs),
                    static_cast<long long>(snap.totalEncodedBytes));

        _isOpen = false;
        _hasLastEncoded = false;
        return Error::Ok;
}

Error MediaIOTask_MjpegStream::executeCmd(MediaIOCommandWrite &cmd) {
        if (!_isOpen) return Error::NotOpen;
        if (!cmd.frame.isValid()) return Error::InvalidArgument;
        stampWorkBegin();

        const TimeStamp arrival = TimeStamp::now();

        // Rate gate: drop everything inside the configured period.
        if (_period.nanoseconds() > 0 && _hasLastEncoded) {
                const TimeStamp gateBoundary = _lastEncoded + _period;
                if (arrival.value() < gateBoundary.value()) {
                        noteFrameDropped();
                        {
                                Mutex::Locker lk(_stateMutex);
                                _stats.framesRateLimited++;
                        }
                        cmd.currentFrame = FrameNumber(_frameSerial);
                        cmd.frameCount = MediaIO::FrameCountInfinite;
                        stampWorkEnd();
                        return Error::Ok;
                }
        }

        Buffer::Ptr jpeg;
        TimeStamp   encodedAt;
        Error       err = encodeFrame(*cmd.frame, &jpeg, &encodedAt);
        if (err.isError()) {
                Mutex::Locker lk(_stateMutex);
                _stats.framesEncodeError++;
                cmd.currentFrame = FrameNumber(_frameSerial);
                cmd.frameCount = MediaIO::FrameCountInfinite;
                stampWorkEnd();
                return err;
        }

        const TimeStamp drain = TimeStamp::now();
        const Duration  encDur = Duration::fromNanoseconds(
                std::chrono::duration_cast<std::chrono::nanoseconds>(drain.value() - arrival.value()).count());
        const int64_t encUs = encDur.microseconds();
        const int64_t bytes = jpeg.isValid() ? static_cast<int64_t>(jpeg->size()) : 0;

        publishEncoded(jpeg, encodedAt);

        _lastEncoded = arrival;
        _hasLastEncoded = true;
        _frameSerial++;

        {
                Mutex::Locker lk(_stateMutex);
                _stats.framesEncoded++;
                _stats.totalEncodeUs += encUs;
                _stats.encodeSamples++;
                if (encUs > _stats.peakEncodeUs) _stats.peakEncodeUs = encUs;
                _stats.totalEncodedBytes += bytes;
        }

        cmd.currentFrame = FrameNumber(_frameSerial);
        cmd.frameCount = MediaIO::FrameCountInfinite;
        stampWorkEnd();
        return Error::Ok;
}

Error MediaIOTask_MjpegStream::executeCmd(MediaIOCommandStats &cmd) {
        MjpegStreamSnapshot snap;
        {
                Mutex::Locker lk(_stateMutex);
                snap = _stats;
        }
        // Encode time is naturally measured in microseconds; the
        // MediaIOStats schema reports latencies in milliseconds, so
        // we only convert at publish time.
        const double avgUs = snap.encodeSamples > 0
                                     ? static_cast<double>(snap.totalEncodeUs) / static_cast<double>(snap.encodeSamples)
                                     : 0.0;
        cmd.stats.set(MediaIOStats::AverageLatencyMs, avgUs / 1000.0);
        cmd.stats.set(MediaIOStats::PeakLatencyMs, static_cast<double>(snap.peakEncodeUs) / 1000.0);
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// proposeInput: declare the encoder's accepted descriptor so the
// planner inserts a CSC upstream when needed.
// ---------------------------------------------------------------------------

Error MediaIOTask_MjpegStream::proposeInput(const MediaDesc &offered, MediaDesc *preferred) const {
        if (preferred == nullptr) return Error::Invalid;
        if (offered.imageList().isEmpty()) return Error::NotSupported;

        const PixelFormat &pd = offered.imageList()[0].pixelFormat();
        if (!pd.isValid()) return Error::NotSupported;

        if (isJpegEncoderInputFormat(pd)) {
                *preferred = offered;
                return Error::Ok;
        }

        // Ask the planner to land on a JPEG-friendly format.
        const PixelFormat fallback = jpegFallbackFormat(pd);
        MediaDesc         out = offered;
        const auto       &offeredImages = offered.imageList();
        out.imageList().clear();
        for (const auto &img : offeredImages) {
                out.imageList().pushToBack(ImageDesc(img.size(), fallback));
        }
        *preferred = out;
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Encode + publish helpers
// ---------------------------------------------------------------------------

Error MediaIOTask_MjpegStream::encodeFrame(const Frame &frame, Buffer::Ptr *out, TimeStamp *ts) {
        if (!_encoder.isValid()) return Error::NotOpen;
        const auto videos = frame.videoPayloads();
        if (videos.isEmpty()) return Error::NotSupported;

        UncompressedVideoPayload::Ptr uvp;
        for (const auto &vp : videos) {
                if (!vp.isValid()) continue;
                UncompressedVideoPayload::Ptr cand = sharedPointerCast<UncompressedVideoPayload>(vp);
                if (cand.isValid()) {
                        uvp = cand;
                        break;
                }
        }
        if (!uvp.isValid()) return Error::NotSupported;

        Error err = _encoder->submitPayload(uvp);
        if (err.isError()) return err;

        CompressedVideoPayload::Ptr cvp = _encoder->receiveCompressedPayload();
        if (!cvp.isValid()) return Error::TryAgain;

        Buffer::Ptr flat = copyPayloadToBuffer(*cvp);
        if (!flat.isValid() || flat->size() < 4) return Error::ConversionFailed;

        // libjpeg-turbo emits a complete JPEG bitstream; verify SOI/EOI
        // markers so a malformed encode surfaces here rather than at
        // the subscriber.
        const uint8_t *bytes = static_cast<const uint8_t *>(flat->data());
        if (bytes[0] != 0xFF || bytes[1] != 0xD8) return Error::ConversionFailed;
        const size_t end = flat->size();
        if (bytes[end - 2] != 0xFF || bytes[end - 1] != 0xD9) {
                return Error::ConversionFailed;
        }

        if (out != nullptr) *out = flat;
        if (ts != nullptr) *ts = TimeStamp::now();
        return Error::Ok;
}

void MediaIOTask_MjpegStream::publishEncoded(const Buffer::Ptr &jpeg, const TimeStamp &ts) {
        // Snapshot the subscriber list under the lock then dispatch
        // outside it so a slow subscriber can't block another
        // subscription's attach/detach.  The Buffer::Ptr is the same
        // pointer pushed into the ring and dispatched to every
        // subscriber — encode-once, share-by-pointer, no copies.
        List<MjpegStreamSubscriber *> recipients;
        {
                Mutex::Locker lk(_stateMutex);
                RingEntry     e;
                e.jpeg = jpeg;
                e.timestamp = ts;
                _ring.pushToBack(e);
                while (static_cast<int>(_ring.size()) > _ringDepth) {
                        _ring.remove(static_cast<size_t>(0));
                }
                for (auto it = _subscribers.cbegin(); it != _subscribers.cend(); ++it) {
                        recipients.pushToBack(it->second);
                }
        }
        for (MjpegStreamSubscriber *s : recipients) {
                if (s != nullptr) s->onFrame(jpeg, ts);
        }
}

// ---------------------------------------------------------------------------
// HTTP helper.  See registerHttpRoute doxygen for the streaming shape.
// ---------------------------------------------------------------------------

namespace {

        // Per-connection adapter wiring an MjpegStreamSubscriber to an
        // AsyncBufferQueue body.  The adapter is parented to the queue so
        // the queue's destruction (when the connection drops the last
        // IODevice::Shared ref) cascades into the adapter dtor, which
        // detaches from the sink.
        class MjpegMultipartAdapter : public ObjectBase, public MjpegStreamSubscriber {
                public:
                        MjpegMultipartAdapter(MediaIOTask_MjpegStream *sink, AsyncBufferQueue *queue)
                            : ObjectBase(queue), _sink(sink), _queue(queue) {
                                // Parented to queue: when the queue dies (its
                                // last IODevice::Shared ref drops), this
                                // adapter's destructor runs and detaches the
                                // subscriber from the sink.
                        }

                        ~MjpegMultipartAdapter() override {
                                // Clear the queue ref *first*: detachSubscriber
                                // re-enters our onClosed via the sink's normal
                                // dispatch path, which would otherwise touch
                                // the queue we are mid-destroying (the queue
                                // is our parent and is destroying us).
                                _queue = nullptr;
                                if (_subId >= 0 && _sink != nullptr) {
                                        _sink->detachSubscriber(_subId);
                                        _subId = -1;
                                }
                        }

                        void setSubId(int id) { _subId = id; }

                        void onFrame(const Buffer::Ptr &jpeg, const TimeStamp &) override {
                                if (_queue == nullptr || !jpeg.isValid()) return;
                                const size_t n = jpeg->size();
                                if (n == 0) return;

                                // Frame the JPEG in a single multipart part:
                                //   --<boundary>\r\n
                                //   Content-Type: image/jpeg\r\n
                                //   Content-Length: <n>\r\n
                                //   \r\n
                                //   <jpeg bytes>
                                //   \r\n
                                // Header is its own Buffer::Ptr so the JPEG
                                // payload itself is shared by pointer (not
                                // copied) into the queue.
                                const String header =
                                        String("--") + MediaIOTask_MjpegStream::HttpBoundary +
                                        "\r\nContent-Type: " + MediaIOTask_MjpegStream::LatestJpegContentType +
                                        "\r\nContent-Length: " + String::number(static_cast<int64_t>(n)) + "\r\n\r\n";
                                const String trailer("\r\n");

                                Buffer::Ptr hdr = Buffer::Ptr::create(header.byteCount());
                                std::memcpy(hdr.modify()->data(), header.cstr(), header.byteCount());
                                hdr.modify()->setSize(header.byteCount());

                                Buffer::Ptr tl = Buffer::Ptr::create(trailer.byteCount());
                                std::memcpy(tl.modify()->data(), trailer.cstr(), trailer.byteCount());
                                tl.modify()->setSize(trailer.byteCount());

                                _queue->enqueue(hdr);
                                _queue->enqueue(jpeg);
                                _queue->enqueue(tl);
                        }

                        void onClosed() override {
                                // Sink-driven termination: the sink already
                                // removed us from its subscriber map, so
                                // there is no detach call to make — record
                                // that and let the connection drain.
                                _subId = -1;
                                if (_queue != nullptr) _queue->closeWriting();
                        }

                private:
                        MediaIOTask_MjpegStream *_sink = nullptr;
                        AsyncBufferQueue        *_queue = nullptr;
                        int                      _subId = -1;
        };

} // namespace

HttpHandlerFunc MediaIOTask_MjpegStream::buildMultipartHandler(MediaIOTask_MjpegStream *sink) {
        // Capture the sink pointer by value.  Lifetime: the caller is
        // responsible for keeping the sink alive for as long as any
        // route holding the returned handler can fire — same contract
        // HttpFileHandler etc. observe today.  A null sink yields the
        // same 503 path as a closed sink so dynamic dispatch can use
        // the handler unconditionally.
        return [sink](const HttpRequest &, HttpResponse &res) {
                if (sink == nullptr || !sink->isStreaming()) {
                        res.setStatus(HttpStatus::ServiceUnavailable);
                        res.setText("MjpegStream: sink is not open");
                        res.setHeader("Cache-Control", "no-store");
                        return;
                }

                // Per-connection async body queue.  The connection
                // holds the only IODevice::Shared ref outside the
                // adapter; its drop fires the queue dtor → destroys
                // the adapter (parented to the queue) → detaches from
                // the sink.
                AsyncBufferQueue *rawQueue = new AsyncBufferQueue();
                Error             openErr = rawQueue->open(IODevice::ReadOnly);
                if (openErr.isError()) {
                        delete rawQueue;
                        res.setStatus(HttpStatus::InternalServerError);
                        res.setText("MjpegStream: queue open failed");
                        return;
                }
                IODevice::Shared queueShared = IODevice::Shared::takeOwnership(rawQueue);

                MjpegMultipartAdapter *adapter = new MjpegMultipartAdapter(sink, rawQueue);
                const int              id = sink->attachSubscriber(adapter);
                if (id < 0) {
                        // attachSubscriber only fails on null; we
                        // never pass null.
                        delete adapter;
                        res.setStatus(HttpStatus::InternalServerError);
                        res.setText("MjpegStream: subscribe failed");
                        return;
                }
                adapter->setSubId(id);

                const String contentType =
                        String("multipart/x-mixed-replace; boundary=") + MediaIOTask_MjpegStream::HttpBoundary;
                res.setStatus(HttpStatus::Ok);
                res.setHeader("Cache-Control", "no-store");
                res.setHeader("Pragma", "no-cache");
                res.setHeader("Connection", "close");
                res.setBodyStream(queueShared, /*length=*/-1, contentType);
        };
}

void MediaIOTask_MjpegStream::registerHttpRoute(HttpServer &server, const String &path) {
        server.route(path, HttpMethod::Get, buildMultipartHandler(this));
}

PROMEKI_NAMESPACE_END
