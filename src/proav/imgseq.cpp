/**
 * @file      imgseq.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/imgseq.h>
#include <promeki/dir.h>
#include <promeki/file.h>
#include <promeki/buffer.h>
#include <promeki/bufferediodevice.h>
#include <promeki/iodevice.h>
#include <promeki/json.h>
#include <promeki/logger.h>
#include <promeki/result.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

// JSON field names used for the sidecar format.  Keeping them in one
// place makes the schema easy to audit and reuse.
constexpr const char *kFieldType      = "type";
constexpr const char *kFieldName      = "name";
constexpr const char *kFieldDir       = "dir";
constexpr const char *kFieldHead      = "head";
constexpr const char *kFieldTail      = "tail";
constexpr const char *kFieldFrameRate = "frameRate";
constexpr const char *kFieldVideoSize = "videoSize";
constexpr const char *kFieldPixelFormat = "pixelFormat";
constexpr const char *kFieldMetadata  = "metadata";
constexpr const char *kFieldAudioFile = "audioFile";

} // namespace

ImgSeq ImgSeq::load(const FilePath &path, Error *err) {
        ImgSeq ret;

        File f(path.toString());
        Error openErr = f.open(IODevice::ReadOnly);
        if(openErr.isError()) {
                promekiErr("ImgSeq::load: failed to open '%s': %s",
                        path.toString().cstr(), openErr.name().cstr());
                if(err) *err = openErr;
                return ret;
        }

        Buffer data = f.readAll();
        f.close();

        String text(static_cast<const char *>(data.data()), data.size());
        Error parseErr;
        JsonObject root = JsonObject::parse(text, &parseErr);
        if(parseErr.isError()) {
                promekiErr("ImgSeq::load: '%s' is not a valid JSON object",
                        path.toString().cstr());
                if(err) *err = Error::Invalid;
                return ret;
        }

        ret = fromJson(root, err);
        if(ret.isValid()) {
                ret.setSidecarPath(path);
        }
        return ret;
}

ImgSeq ImgSeq::fromJson(const JsonObject &json, Error *err) {
        ImgSeq ret;

        // Must have the right type tag to count as an ImgSeq sidecar.
        if(!json.contains(kFieldType)) {
                promekiErr("ImgSeq::fromJson: missing '%s' field", kFieldType);
                if(err) *err = Error::Invalid;
                return ret;
        }
        if(json.getString(kFieldType) != String(TypeTag)) {
                promekiErr("ImgSeq::fromJson: '%s' field is not '%s'",
                        kFieldType, TypeTag);
                if(err) *err = Error::Invalid;
                return ret;
        }

        // The pattern name is required — everything else is optional.
        if(!json.contains(kFieldName)) {
                promekiErr("ImgSeq::fromJson: missing '%s' field", kFieldName);
                if(err) *err = Error::Invalid;
                return ret;
        }
        String nameStr = json.getString(kFieldName);
        NumName nn = NumName::fromMask(nameStr);
        if(!nn.isValid()) {
                // Fall back to parse() in case the user supplied a
                // concrete filename like "shot_0001.dpx" instead of a
                // mask — in that case we still get a usable NumName.
                nn = NumName::parse(nameStr);
        }
        if(!nn.isValid()) {
                promekiErr("ImgSeq::fromJson: '%s' is not a valid pattern",
                        nameStr.cstr());
                if(err) *err = Error::Invalid;
                return ret;
        }
        ret.setName(nn);

        if(json.contains(kFieldDir)) {
                ret.setDir(FilePath(json.getString(kFieldDir)));
        }

        if(json.contains(kFieldHead)) {
                int64_t h = json.getInt(kFieldHead);
                if(h < 0) h = 0;
                ret.setHead(static_cast<size_t>(h));
        }
        if(json.contains(kFieldTail)) {
                int64_t t = json.getInt(kFieldTail);
                if(t < 0) t = 0;
                ret.setTail(static_cast<size_t>(t));
        }

        if(json.contains(kFieldFrameRate)) {
                String frStr = json.getString(kFieldFrameRate);
                Result<FrameRate> r = FrameRate::fromString(frStr);
                if(r.second().isOk()) {
                        ret.setFrameRate(r.first());
                } else {
                        promekiWarn("ImgSeq::fromJson: invalid '%s' value '%s'",
                                kFieldFrameRate, frStr.cstr());
                }
        }

        if(json.contains(kFieldVideoSize)) {
                String szStr = json.getString(kFieldVideoSize);
                Result<Size2Du32> r = Size2Du32::fromString(szStr);
                if(r.second().isOk()) {
                        ret.setVideoSize(r.first());
                } else {
                        promekiWarn("ImgSeq::fromJson: invalid '%s' value '%s'",
                                kFieldVideoSize, szStr.cstr());
                }
        }

        if(json.contains(kFieldPixelFormat)) {
                String pdStr = json.getString(kFieldPixelFormat);
                PixelFormat pd = PixelFormat::lookup(pdStr);
                if(pd.isValid()) {
                        ret.setPixelFormat(pd);
                } else {
                        promekiWarn("ImgSeq::fromJson: invalid '%s' value '%s'",
                                kFieldPixelFormat, pdStr.cstr());
                }
        }

        if(json.contains(kFieldAudioFile)) {
                ret.setAudioFile(json.getString(kFieldAudioFile));
        }

        if(json.valueIsObject(kFieldMetadata)) {
                JsonObject metaObj = json.getObject(kFieldMetadata);
                ret.setMetadata(Metadata::fromJson(metaObj));
        }

        if(err) *err = Error::Ok;
        return ret;
}

bool ImgSeq::isImgSeqJson(const String &jsonText) {
        Error parseErr;
        JsonObject root = JsonObject::parse(jsonText, &parseErr);
        if(parseErr.isError()) return false;
        if(!root.contains(kFieldType)) return false;
        return root.getString(kFieldType) == String(TypeTag);
}

JsonObject ImgSeq::toJson() const {
        JsonObject root;
        root.set(kFieldType, String(TypeTag));
        root.set(kFieldName, _name.hashmask());
        if(!_dir.isEmpty()) {
                root.set(kFieldDir, _dir.toString());
        }
        root.set(kFieldHead, static_cast<int64_t>(_head));
        root.set(kFieldTail, static_cast<int64_t>(_tail));
        if(_frameRate.isValid()) {
                root.set(kFieldFrameRate, _frameRate.toString());
        }
        if(_videoSize.width() > 0 && _videoSize.height() > 0) {
                root.set(kFieldVideoSize, _videoSize.toString());
        }
        if(_pixelFormat.isValid()) {
                root.set(kFieldPixelFormat, _pixelFormat.name());
        }
        if(!_audioFile.isEmpty()) {
                root.set(kFieldAudioFile, _audioFile);
        }
        if(!_metadata.isEmpty()) {
                root.set(kFieldMetadata, _metadata.toJson());
        }
        return root;
}

Error ImgSeq::save(const FilePath &path) const {
        if(!isValid()) {
                promekiErr("ImgSeq::save: pattern is not set");
                return Error::Invalid;
        }

        JsonObject root = toJson();
        String text = root.toString(4);  // human-readable indentation

        File f(path.toString());
        Error err = f.open(IODevice::WriteOnly, File::Create | File::Truncate);
        if(err.isError()) {
                promekiErr("ImgSeq::save: failed to open '%s': %s",
                        path.toString().cstr(), err.name().cstr());
                return err;
        }
        int64_t n = f.write(text.cstr(), static_cast<int64_t>(text.size()));
        f.close();
        if(n != static_cast<int64_t>(text.size())) {
                promekiErr("ImgSeq::save: short write to '%s'",
                        path.toString().cstr());
                return Error::IOError;
        }
        return Error::Ok;
}

Error ImgSeq::detectRange(const FilePath &dir) {
        if(!_name.isValid()) {
                promekiErr("ImgSeq::detectRange: pattern not set");
                return Error::Invalid;
        }

        Dir d(dir);
        bool haveAny = false;
        size_t head = 0;
        size_t tail = 0;

        for(const FilePath &entry : d.entryList()) {
                int val = -1;
                NumName parsed = NumName::parse(entry.fileName(), &val);
                if(!parsed.isValid()) continue;
                // Only keep files that match our template (prefix, suffix,
                // and padding compatibility).  isInSequence enforces that.
                if(!_name.isInSequence(parsed)) continue;
                if(val < 0) continue;
                size_t v = static_cast<size_t>(val);
                if(!haveAny) {
                        head = v;
                        tail = v;
                        haveAny = true;
                } else {
                        if(v < head) head = v;
                        if(v > tail) tail = v;
                }
        }

        if(haveAny) {
                _head = head;
                _tail = tail;
        }
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
