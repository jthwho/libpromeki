/**
 * @file      videodecoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Symmetric counterpart to @ref videoencoder.cpp — owns the decoder
 * registry and defines @ref VideoCodec::canDecode /
 * @ref VideoCodec::createDecoder / @ref VideoCodec::decoderSupportedOutputs /
 * @ref VideoCodec::availableDecoderBackends against it.
 */

#include <algorithm>
#include <promeki/videodecoder.h>
#include <promeki/videocodec.h>
#include <promeki/logger.h>
#include <promeki/pixelformat.h>
#include <promeki/mediaconfig.h>
#include <promeki/map.h>
#include <promeki/set.h>
#include <promeki/readwritelock.h>
#include <promeki/frame.h>
#include <promeki/ancpayload.h>
#include <promeki/ancdesc.h>

PROMEKI_NAMESPACE_BEGIN

VideoDecoder::~VideoDecoder() = default;

void VideoDecoder::configure(const MediaConfig &config) {
        if (!_stashedConfig.isValid()) {
                _stashedConfig = UniquePtr<MediaConfig>::create(config);
        } else {
                *_stashedConfig = config;
        }
        onConfigure(config);
}

void VideoDecoder::onConfigure(const MediaConfig &config) {
        (void)config;
}

const MediaConfig &VideoDecoder::config() const {
        static const MediaConfig empty;
        return _stashedConfig.isValid() ? *_stashedConfig : empty;
}

void VideoDecoder::setError(Error err, const String &msg) {
        _lastError = err;
        _lastErrorMessage = msg;
}

void VideoDecoder::clearError() {
        _lastError = Error::Ok;
        _lastErrorMessage = String();
}

// ---------------------------------------------------------------------------
// Frame-shaped helpers — shared algorithms for concrete backends.
// ---------------------------------------------------------------------------

CompressedVideoPayload::Ptr VideoDecoder::selectInputPayload(const Frame &frame, int streamIndex) {
        if (!frame.isValid()) return CompressedVideoPayload::Ptr();
        for (const VideoPayload::Ptr &vp : frame.videoPayloads()) {
                if (!vp.isValid()) continue;
                if (streamIndex >= 0 && vp->streamIndex() != streamIndex) continue;
                CompressedVideoPayload::Ptr cvp = sharedPointerCast<CompressedVideoPayload>(vp);
                if (cvp.isNull()) continue;
                return cvp;
        }
        return CompressedVideoPayload::Ptr();
}

void VideoDecoder::attachExtractedAnc(Frame &frame, AncPacket pkt, int pairedVideoStreamIndex) {
        if (!pkt.isValid()) return;
        // Walk the frame's payload list directly — frame.ancPayloads()
        // returns a transient PtrList whose entries' .modify() would
        // detach the wrong copy.  Iterating payloadList() by reference
        // gives us the stored Ptr to mutate in place.
        MediaPayload::PtrList &payloads = frame.payloadList();
        for (MediaPayload::Ptr &mp : payloads) {
                if (!mp.isValid()) continue;
                const auto *probe = mp->as<const AncPayload>();
                if (probe == nullptr) continue;
                if (probe->desc().pairedVideoStreamIndex() != pairedVideoStreamIndex) continue;
                AncPayload *target = mp.modify()->as<AncPayload>();
                if (target == nullptr) continue;
                target->addPacket(std::move(pkt));
                return;
        }
        // No matching payload yet — create one keyed to the paired stream.
        AncDesc desc;
        desc.setPairedVideoStreamIndex(pairedVideoStreamIndex);
        AncPayload::Ptr ap = AncPayload::Ptr::create(desc);
        ap.modify()->addPacket(std::move(pkt));
        frame.addPayload(ap);
}

Frame VideoDecoder::buildOutputFrame(const Frame &source, UncompressedVideoPayload::Ptr emitted) {
        Frame out;
        if (source.isValid()) {
                out.metadata() = source.metadata();
                out.setCaptureTime(source.captureTime());
                for (const AudioPayload::Ptr &ap : source.audioPayloads()) {
                        if (ap.isValid()) out.addPayload(ap);
                }
        }
        if (emitted.isValid()) out.addPayload(emitted);
        return out;
}

MediaIOAllocator::Ptr VideoDecoder::allocator() const {
        if (_allocator.isValid()) return _allocator;
        return MediaIOAllocator::defaultAllocator();
}

void VideoDecoder::setAllocator(MediaIOAllocator::Ptr a) {
        // Null clears and reverts to defaultAllocator() on the next
        // allocator() read.  Stored as-is so a backend that wants to
        // read the override directly (without the default fallback)
        // can detect "user installed nothing" by checking isValid().
        _allocator = a;
}

namespace {

        struct DecoderRegistry {
                        ReadWriteLock                                          mutex;
                        Map<VideoCodec::ID, List<VideoDecoder::BackendRecord>> entries;
        };

        DecoderRegistry &registry() {
                static DecoderRegistry inst;
                return inst;
        }

        void sortByWeight(List<VideoDecoder::BackendRecord> &list) {
                list.sortInPlace([](const VideoDecoder::BackendRecord &a, const VideoDecoder::BackendRecord &b) {
                        return a.weight > b.weight;
                });
        }

} // namespace

Error VideoDecoder::registerBackend(BackendRecord record) {
        if (!record.factory) return Error::Invalid;
        if (!record.backend.isValid()) return Error::Invalid;
        if (record.codecId == VideoCodec::Invalid) return Error::Invalid;

        auto                      &reg = registry();
        ReadWriteLock::WriteLocker lock(reg.mutex);
        auto                      &list = reg.entries[record.codecId];
        for (auto &existing : list) {
                if (existing.backend == record.backend) {
                        existing = std::move(record);
                        sortByWeight(list);
                        return Error::Ok;
                }
        }
        list.pushToBack(std::move(record));
        sortByWeight(list);
        return Error::Ok;
}

VideoCodec::BackendList VideoDecoder::availableBackends(VideoCodec::ID codecId) {
        VideoCodec::BackendList   out;
        auto                     &reg = registry();
        ReadWriteLock::ReadLocker lock(reg.mutex);
        auto                      it = reg.entries.find(codecId);
        if (it == reg.entries.end()) return out;
        for (const auto &r : it->second) out.pushToBack(r.backend);
        return out;
}

List<int> VideoDecoder::supportedOutputsFor(VideoCodec::ID codecId, VideoCodec::Backend pinned) {
        auto                     &reg = registry();
        ReadWriteLock::ReadLocker lock(reg.mutex);
        auto                      it = reg.entries.find(codecId);
        if (it == reg.entries.end()) return {};
        if (pinned.isValid()) {
                for (const auto &r : it->second) {
                        if (r.backend == pinned) return r.supportedOutputs;
                }
                return {};
        }
        Set<int> acc;
        for (const auto &r : it->second) {
                for (int pd : r.supportedOutputs) acc.insert(pd);
        }
        List<int> out;
        out.reserve(acc.size());
        for (int pd : acc) out.pushToBack(pd);
        return out;
}

Result<VideoDecoder *> VideoDecoder::create(VideoCodec::ID codecId, VideoCodec::Backend pinned,
                                            const MediaConfig *config) {
        auto               &reg = registry();
        VideoCodec::Backend chosenTag;
        Factory             factory;
        {
                ReadWriteLock::ReadLocker lock(reg.mutex);
                auto                      it = reg.entries.find(codecId);
                if (it == reg.entries.end() || it->second.isEmpty()) {
                        promekiWarn("VideoDecoder::create: no backends registered for codec id=%d", (int)codecId);
                        return makeError<VideoDecoder *>(Error::IdNotFound);
                }
                const auto &list = it->second;

                const BackendRecord *chosen = nullptr;
                if (pinned.isValid()) {
                        for (const auto &r : list) {
                                if (r.backend == pinned) {
                                        chosen = &r;
                                        break;
                                }
                        }
                        if (chosen == nullptr) {
                                promekiWarn("VideoDecoder::create: pinned backend not found for codec id=%d",
                                            (int)codecId);
                                return makeError<VideoDecoder *>(Error::IdNotFound);
                        }
                } else {
                        if (config != nullptr) {
                                String backendName = config->getAs<String>(MediaConfig::CodecBackend);
                                if (!backendName.isEmpty()) {
                                        auto bk = VideoCodec::lookupBackend(backendName);
                                        if (!error(bk).isError()) {
                                                auto backend = value(bk);
                                                for (const auto &r : list) {
                                                        if (r.backend == backend) {
                                                                chosen = &r;
                                                                break;
                                                        }
                                                }
                                                if (chosen == nullptr) {
                                                        promekiWarn("VideoDecoder::create: requested backend '%s' "
                                                                    "not found for codec id=%d",
                                                                    backendName.cstr(), (int)codecId);
                                                        return makeError<VideoDecoder *>(Error::IdNotFound);
                                                }
                                        }
                                }
                        }
                        if (chosen == nullptr) chosen = &list.front();
                }
                chosenTag = chosen->backend;
                factory = chosen->factory;
        }
        if (!factory) {
                promekiWarn("VideoDecoder::create: null factory for codec id=%d", (int)codecId);
                return makeError<VideoDecoder *>(Error::IdNotFound);
        }

        VideoDecoder *dec = factory();
        if (dec == nullptr) {
                promekiWarn("VideoDecoder::create: factory returned null for codec id=%d", (int)codecId);
                return makeError<VideoDecoder *>(Error::LibraryFailure);
        }
        dec->setCodec(VideoCodec(codecId, chosenTag));
        if (config != nullptr) dec->configure(*config);
        return makeResult(dec);
}

// ---------------------------------------------------------------------------
// VideoCodec bridge — decoder-side accessors.
// ---------------------------------------------------------------------------

bool VideoCodec::canDecode() const {
        if (!isValid()) return false;
        auto list = VideoDecoder::availableBackends(id());
        if (!_backend.isValid()) return !list.isEmpty();
        for (auto b : list)
                if (b == _backend) return true;
        return false;
}

VideoCodec::BackendList VideoCodec::availableDecoderBackends() const {
        if (!isValid()) return {};
        return VideoDecoder::availableBackends(id());
}

List<PixelFormat> VideoCodec::decoderSupportedOutputs() const {
        List<PixelFormat> out;
        if (!isValid()) return out;
        for (int rawId : VideoDecoder::supportedOutputsFor(id(), _backend)) {
                out.pushToBack(PixelFormat(static_cast<PixelFormat::ID>(rawId)));
        }
        return out;
}

Result<VideoDecoder *> VideoCodec::createDecoder(const MediaConfig *config) const {
        if (!isValid()) return makeError<VideoDecoder *>(Error::Invalid);
        return VideoDecoder::create(id(), _backend, config);
}

PROMEKI_NAMESPACE_END
