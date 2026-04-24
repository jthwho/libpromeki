/**
 * @file      frame.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <cstdlib>
#include <cstring>
#include <promeki/frame.h>
#include <promeki/mediadesc.h>
#include <promeki/videopayload.h>
#include <promeki/audiopayload.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/uncompressedaudiopayload.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/compressedaudiopayload.h>
#include <promeki/variantlookup.h>

PROMEKI_NAMESPACE_BEGIN

VideoPayload::PtrList Frame::videoPayloads() const {
        VideoPayload::PtrList out;
        for(const MediaPayload::Ptr &p : _payloads) {
                if(!p.isValid()) continue;
                if(p->kind() != MediaPayloadKind::Video) continue;
                VideoPayload::Ptr vp = sharedPointerCast<VideoPayload>(p);
                if(vp.isValid()) out.pushToBack(std::move(vp));
        }
        return out;
}

AudioPayload::PtrList Frame::audioPayloads() const {
        AudioPayload::PtrList out;
        for(const MediaPayload::Ptr &p : _payloads) {
                if(!p.isValid()) continue;
                if(p->kind() != MediaPayloadKind::Audio) continue;
                AudioPayload::Ptr ap = sharedPointerCast<AudioPayload>(p);
                if(ap.isValid()) out.pushToBack(std::move(ap));
        }
        return out;
}

bool Frame::isSafeCutPoint(CutPointScope scope) const {
        const bool wantVideo = (scope == CutPointVideoOnly || scope == CutPointAudioVideo);
        const bool wantAudio = (scope == CutPointAudioOnly || scope == CutPointAudioVideo);
        for(size_t i = 0; i < _payloads.size(); ++i) {
                const MediaPayload::Ptr &p = _payloads[i];
                if(!p.isValid()) continue;
                const MediaPayloadKind &k = p->kind();
                if(k == MediaPayloadKind::Video && wantVideo) {
                        if(!p->isSafeCutPoint()) return false;
                } else if(k == MediaPayloadKind::Audio && wantAudio) {
                        if(!p->isSafeCutPoint()) return false;
                }
        }
        return true;
}

VideoFormat Frame::videoFormat(size_t index) const {
        size_t videoIdx = 0;
        for(const MediaPayload::Ptr &p : _payloads) {
                if(!p.isValid()) continue;
                if(p->kind() != MediaPayloadKind::Video) continue;
                if(videoIdx != index) { ++videoIdx; continue; }
                const auto *vp = p->as<VideoPayload>();
                if(vp == nullptr) return VideoFormat();
                const ImageDesc &d = vp->desc();
                return VideoFormat(d.size(),
                                   _metadata.getAs<FrameRate>(Metadata::FrameRate),
                                   d.videoScanMode());
        }
        return VideoFormat();
}

MediaDesc Frame::mediaDesc() const {
        MediaDesc md;
        md.setFrameRate(_metadata.getAs<FrameRate>(Metadata::FrameRate));
        for(const MediaPayload::Ptr &p : _payloads) {
                if(!p.isValid()) continue;
                if(const auto *vp = p->as<VideoPayload>()) {
                        md.imageList().pushToBack(vp->desc());
                } else if(const auto *ap = p->as<AudioPayload>()) {
                        md.audioList().pushToBack(ap->desc());
                }
        }
        md.metadata() = _metadata;
        return md;
}

StringList Frame::dump(const String &indent) const {
        StringList out;
        VariantLookup<Frame>::forEachScalar([this, &out, &indent](const String &name) {
                auto v = VariantLookup<Frame>::resolve(*this, name);
                if(v.has_value()) {
                        out += indent + name + ": " + v->format(String());
                }
        });

        StringList mdLines = _metadata.dump();
        if(!mdLines.isEmpty()) {
                out += indent + "Meta:";
                String sub = indent + "  ";
                for(const String &ln : mdLines) out += sub + ln;
        }

        if(!_configUpdate.isEmpty()) {
                out += indent + "ConfigUpdate:";
                String sub = indent + "  ";
                _configUpdate.forEach([&out, &sub](MediaConfig::ID id, const Variant &value) {
                        String s = id.name();
                        s += " [";
                        s += value.typeName();
                        s += "]: ";
                        s += value.format(String());
                        out += sub + s;
                });
        }

        size_t videoIdx = 0;
        size_t audioIdx = 0;
        const String sub = indent + "  ";
        for(const MediaPayload::Ptr &p : _payloads) {
                if(!p.isValid()) {
                        out += indent + "<null payload>";
                        continue;
                }
                if(p->kind() == MediaPayloadKind::Video) {
                        out += indent + String::sprintf("VideoPayload[%zu]:", videoIdx++);
                        if(const auto *vp = p->as<VideoPayload>()) {
                                VariantLookup<VideoPayload>::forEachScalar(
                                        [vp, &out, &sub](const String &name) {
                                                auto v = VariantLookup<VideoPayload>::resolve(*vp, name);
                                                if(v.has_value()) {
                                                        out += sub + name + ": " + v->format(String());
                                                }
                                        });
                        }
                } else if(p->kind() == MediaPayloadKind::Audio) {
                        out += indent + String::sprintf("AudioPayload[%zu]:", audioIdx++);
                        if(const auto *ap = p->as<AudioPayload>()) {
                                VariantLookup<AudioPayload>::forEachScalar(
                                        [ap, &out, &sub](const String &name) {
                                                auto v = VariantLookup<AudioPayload>::resolve(*ap, name);
                                                if(v.has_value()) {
                                                        out += sub + name + ": " + v->format(String());
                                                }
                                        });
                        }
                } else {
                        out += indent + String::sprintf("Payload[%s]:",
                                                       String(p->kind().valueName()).cstr());
                }
        }
        return out;
}

PROMEKI_LOOKUP_REGISTER(Frame)
        .scalar("PayloadCount",
                [](const Frame &f) -> std::optional<Variant> {
                        return Variant(static_cast<uint64_t>(f.payloadList().size()));
                })
        .scalar("ImageCount",
                [](const Frame &f) -> std::optional<Variant> {
                        return Variant(static_cast<uint64_t>(f.videoPayloads().size()));
                })
        .scalar("AudioCount",
                [](const Frame &f) -> std::optional<Variant> {
                        return Variant(static_cast<uint64_t>(f.audioPayloads().size()));
                })
        .scalar("HasBenchmark",
                [](const Frame &f) -> std::optional<Variant> {
                        return Variant(f.benchmark().isValid());
                })
        .scalar("VideoFormat",
                [](const Frame &f) -> std::optional<Variant> {
                        return Variant(f.videoFormat(0));
                })
        .indexedScalar("VideoFormat",
                [](const Frame &f, size_t i) -> std::optional<Variant> {
                        auto vf = f.videoFormat(i);
                        if(!vf.isValid()) return std::nullopt;
                        return Variant(vf);
                })
        // indexedChild lambdas return @c VideoPayload* /
        // @c AudioPayload* that point directly into @c f.payloadList.
        // We cannot go through @ref Frame::videoPayloads — it returns
        // a fresh @c PtrList of shared pointers on every call; any
        // pointer derived from its entries dangles the moment the
        // list goes out of scope.  Walking @c payloadList in place
        // yields a stable raw pointer for the duration of the
        // enclosing Frame reference.
        .indexedChild<VideoPayload>("VideoPayload",
                [](const Frame &f, size_t i) -> const VideoPayload * {
                        size_t idx = 0;
                        for(const MediaPayload::Ptr &p : f.payloadList()) {
                                if(!p.isValid() ||
                                   p->kind() != MediaPayloadKind::Video) continue;
                                if(idx++ != i) continue;
                                return p->as<VideoPayload>();
                        }
                        return nullptr;
                },
                [](Frame &f, size_t i) -> VideoPayload * {
                        size_t idx = 0;
                        for(MediaPayload::Ptr &p : f.payloadList()) {
                                if(!p.isValid() ||
                                   p->kind() != MediaPayloadKind::Video) continue;
                                if(idx++ != i) continue;
                                return p.modify()->as<VideoPayload>();
                        }
                        return nullptr;
                })
        // Legacy @c Image[N] alias: template strings authored before
        // the payload migration use @c {Image[0].*} / @c {Audio[0].*}.
        // The alias resolves to the same @ref VideoPayload /
        // @ref AudioPayload scalar bindings.
        .indexedChild<VideoPayload>("Image",
                [](const Frame &f, size_t i) -> const VideoPayload * {
                        size_t idx = 0;
                        for(const MediaPayload::Ptr &p : f.payloadList()) {
                                if(!p.isValid() ||
                                   p->kind() != MediaPayloadKind::Video) continue;
                                if(idx++ != i) continue;
                                return p->as<VideoPayload>();
                        }
                        return nullptr;
                },
                [](Frame &f, size_t i) -> VideoPayload * {
                        size_t idx = 0;
                        for(MediaPayload::Ptr &p : f.payloadList()) {
                                if(!p.isValid() ||
                                   p->kind() != MediaPayloadKind::Video) continue;
                                if(idx++ != i) continue;
                                return p.modify()->as<VideoPayload>();
                        }
                        return nullptr;
                })
        .indexedChild<AudioPayload>("AudioPayload",
                [](const Frame &f, size_t i) -> const AudioPayload * {
                        size_t idx = 0;
                        for(const MediaPayload::Ptr &p : f.payloadList()) {
                                if(!p.isValid() ||
                                   p->kind() != MediaPayloadKind::Audio) continue;
                                if(idx++ != i) continue;
                                return p->as<AudioPayload>();
                        }
                        return nullptr;
                },
                [](Frame &f, size_t i) -> AudioPayload * {
                        size_t idx = 0;
                        for(MediaPayload::Ptr &p : f.payloadList()) {
                                if(!p.isValid() ||
                                   p->kind() != MediaPayloadKind::Audio) continue;
                                if(idx++ != i) continue;
                                return p.modify()->as<AudioPayload>();
                        }
                        return nullptr;
                })
        .indexedChild<AudioPayload>("Audio",
                [](const Frame &f, size_t i) -> const AudioPayload * {
                        size_t idx = 0;
                        for(const MediaPayload::Ptr &p : f.payloadList()) {
                                if(!p.isValid() ||
                                   p->kind() != MediaPayloadKind::Audio) continue;
                                if(idx++ != i) continue;
                                return p->as<AudioPayload>();
                        }
                        return nullptr;
                },
                [](Frame &f, size_t i) -> AudioPayload * {
                        size_t idx = 0;
                        for(MediaPayload::Ptr &p : f.payloadList()) {
                                if(!p.isValid() ||
                                   p->kind() != MediaPayloadKind::Audio) continue;
                                if(idx++ != i) continue;
                                return p.modify()->as<AudioPayload>();
                        }
                        return nullptr;
                })
        .database<"Metadata">("Meta",
                [](const Frame &f) -> const VariantDatabase<"Metadata"> * {
                        return &f.metadata();
                },
                [](Frame &f) -> VariantDatabase<"Metadata"> * {
                        return &f.metadata();
                });

PROMEKI_NAMESPACE_END
