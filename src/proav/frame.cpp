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

namespace {

// Parse "<prefix>[N].<inner>".  On success, fills index/inner and
// returns true.  Inner may itself contain dots (e.g. nested subscripts
// in the future) — we deliberately only split on the first '.' after
// the closing ']'.
bool parseSubscript(const String &key, const char *prefix,
                    size_t &index, String &inner) {
        size_t prefixLen = std::strlen(prefix);
        if(key.byteCount() < prefixLen + 4) return false; // [N].x minimum
        const char *p = key.cstr();
        if(std::strncmp(p, prefix, prefixLen) != 0) return false;
        if(p[prefixLen] != '[') return false;
        size_t i = prefixLen + 1;
        const size_t total = key.byteCount();
        const size_t numStart = i;
        while(i < total && p[i] >= '0' && p[i] <= '9') ++i;
        if(i == numStart) return false;
        if(i >= total || p[i] != ']') return false;
        if(i + 1 >= total || p[i + 1] != '.') return false;
        char *endp = nullptr;
        index = static_cast<size_t>(std::strtoull(p + numStart, &endp, 10));
        inner = String(p + i + 2, total - (i + 2));
        return true;
}

} // namespace

std::optional<String> Frame::resolveTemplateKey(const String &key, const String &spec) const {
        if(!key.isEmpty() && key.cstr()[0] == '@') {
                return resolvePseudoKey(key, spec);
        }
        size_t idx = 0;
        String inner;
        if(parseSubscript(key, "Image", idx, inner)) {
                if(idx >= _imageList.size()) return std::nullopt;
                const Image::Ptr &img = _imageList[idx];
                if(!img) return std::nullopt;
                return img->resolveTemplateKey(inner, spec);
        }
        if(parseSubscript(key, "Audio", idx, inner)) {
                if(idx >= _audioList.size()) return std::nullopt;
                const Audio::Ptr &aud = _audioList[idx];
                if(!aud) return std::nullopt;
                return aud->resolveTemplateKey(inner, spec);
        }
        Metadata::ID id = Metadata::ID::find(key);
        if(id.isValid() && _metadata.contains(id)) {
                return _metadata.get(id).format(spec);
        }
        return std::nullopt;
}

std::optional<String> Frame::resolvePseudoKey(const String &key, const String &spec) const {
        Variant v;
        if(key == String("@ImageCount"))         v = static_cast<uint64_t>(_imageList.size());
        else if(key == String("@AudioCount"))    v = static_cast<uint64_t>(_audioList.size());
        else if(key == String("@HasBenchmark"))  v = _benchmark.isValid();
        else if(key == String("@VideoFormat")) {
                v = videoFormat(0);
        }
        else if(key.byteCount() > std::strlen("@VideoFormat[")
                && std::strncmp(key.cstr(), "@VideoFormat[",
                                std::strlen("@VideoFormat[")) == 0
                && key.cstr()[key.byteCount() - 1] == ']') {
                // @VideoFormat[N] — parse the index, fail to nullopt
                // if it's malformed or out-of-range so the caller's
                // resolver still gets a chance.
                const size_t pfx = std::strlen("@VideoFormat[");
                const size_t numLen = key.byteCount() - pfx - 1;
                String numStr(key.cstr() + pfx, numLen);
                char *endp = nullptr;
                size_t idx = static_cast<size_t>(
                        std::strtoull(numStr.cstr(), &endp, 10));
                if(endp == nullptr || *endp != '\0' || numLen == 0) {
                        return std::nullopt;
                }
                if(idx >= _imageList.size()) return std::nullopt;
                v = videoFormat(idx);
        }
        else return std::nullopt;
        return v.format(spec);
}

PROMEKI_NAMESPACE_END


