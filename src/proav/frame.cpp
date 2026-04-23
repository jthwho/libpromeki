/**
 * @file      frame.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdlib>
#include <cstring>
#include <promeki/frame.h>
#include <promeki/mediadesc.h>

PROMEKI_NAMESPACE_BEGIN

bool Frame::isSafeCutPoint(CutPointScope scope) const {
        if(scope == CutPointVideoOnly || scope == CutPointAudioVideo) {
                for(size_t i = 0; i < _imageList.size(); ++i) {
                        const Image::Ptr &img = _imageList[i];
                        if(!img.isValid()) continue;
                        if(!img->isSafeCutPoint()) return false;
                }
        }
        if(scope == CutPointAudioOnly || scope == CutPointAudioVideo) {
                for(size_t i = 0; i < _audioList.size(); ++i) {
                        const Audio::Ptr &aud = _audioList[i];
                        if(!aud.isValid()) continue;
                        if(!aud->isSafeCutPoint()) return false;
                }
        }
        return true;
}

MediaDesc Frame::mediaDesc() const {
        MediaDesc md;
        md.setFrameRate(_metadata.getAs<FrameRate>(Metadata::FrameRate));
        for(const Image::Ptr &img : _imageList) {
                if(img) md.imageList().pushToBack(img->desc());
        }
        for(const Audio::Ptr &aud : _audioList) {
                if(aud) md.audioList().pushToBack(aud->desc());
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
                        // format() renders via the Variant's own formatter
                        // and handles types (PixelFormat, FrameRate, etc.)
                        // whose get<String> would silently produce an
                        // empty string.
                        s += value.format(String());
                        out += sub + s;
                });
        }

        for(size_t i = 0; i < _imageList.size(); ++i) {
                out += indent + String::sprintf("Image[%zu]:", i);
                const Image::Ptr &img = _imageList[i];
                if(img.isValid()) {
                        StringList lines = img->dump(indent + "  ");
                        for(const String &ln : lines) out += ln;
                } else {
                        out += indent + "  <null>";
                }
        }
        for(size_t i = 0; i < _audioList.size(); ++i) {
                out += indent + String::sprintf("Audio[%zu]:", i);
                const Audio::Ptr &aud = _audioList[i];
                if(aud.isValid()) {
                        StringList lines = aud->dump(indent + "  ");
                        for(const String &ln : lines) out += ln;
                } else {
                        out += indent + "  <null>";
                }
        }
        return out;
}

PROMEKI_LOOKUP_REGISTER(Frame)
        .scalar("ImageCount",
                [](const Frame &f) -> std::optional<Variant> {
                        return Variant(static_cast<uint64_t>(f.imageList().size()));
                })
        .scalar("AudioCount",
                [](const Frame &f) -> std::optional<Variant> {
                        return Variant(static_cast<uint64_t>(f.audioList().size()));
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
                        if(i >= f.imageList().size()) return std::nullopt;
                        return Variant(f.videoFormat(i));
                })
        .indexedChild<Image>("Image",
                [](const Frame &f, size_t i) -> const Image * {
                        if(i >= f.imageList().size()) return nullptr;
                        const Image::Ptr &img = f.imageList()[i];
                        if(!img.isValid()) return nullptr;
                        return img.ptr();
                },
                [](Frame &f, size_t i) -> Image * {
                        if(i >= f.imageList().size()) return nullptr;
                        Image::Ptr &img = f.imageList()[i];
                        if(!img.isValid()) return nullptr;
                        return img.modify();
                })
        .indexedChild<Audio>("Audio",
                [](const Frame &f, size_t i) -> const Audio * {
                        if(i >= f.audioList().size()) return nullptr;
                        const Audio::Ptr &aud = f.audioList()[i];
                        if(!aud.isValid()) return nullptr;
                        return aud.ptr();
                },
                [](Frame &f, size_t i) -> Audio * {
                        if(i >= f.audioList().size()) return nullptr;
                        Audio::Ptr &aud = f.audioList()[i];
                        if(!aud.isValid()) return nullptr;
                        return aud.modify();
                })
        .database<"Metadata">("Meta",
                [](const Frame &f) -> const VariantDatabase<"Metadata"> * {
                        return &f.metadata();
                },
                [](Frame &f) -> VariantDatabase<"Metadata"> * {
                        return &f.metadata();
                });

PROMEKI_NAMESPACE_END


