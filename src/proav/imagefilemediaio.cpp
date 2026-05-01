/**
 * @file      imagefilemediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <cstring>

#include <promeki/buffer.h>
#include <promeki/colormodel.h>
#include <promeki/dir.h>
#include <promeki/enums.h>
#include <promeki/filepath.h>
#include <promeki/imagedesc.h>
#include <promeki/imagefile.h>
#include <promeki/imagefileio.h>
#include <promeki/imagefilemediaio.h>
#include <promeki/imgseq.h>
#include <promeki/iodevice.h>
#include <promeki/logger.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiorequest.h>
#include <promeki/metadata.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/stringlist.h>
#include <promeki/timecode.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/videopayload.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Frame-rate source tags (values for Metadata::FrameRateSource)
// ============================================================================
static const char *const kFrameRateSourceFile = "file";
static const char *const kFrameRateSourceConfig = "config";

// ============================================================================
// Extension helpers
// ============================================================================
//
// The .imgseq sidecar is recognized as its own extension but does not
// map to any ImageFile::ID — the backend looks inside the sidecar to
// find the referenced image format.
static const char *const kImgSeqExtension = "imgseq";

static String extensionOf(const String &filename) {
        size_t dot = filename.rfind('.');
        if (dot == String::npos || dot + 1 >= filename.size()) return String();
        return filename.mid(dot + 1).toLower();
}

static int imageFileIDFromExtension(const String &filename) {
        String ext = extensionOf(filename);
        if (ext.isEmpty()) return ImageFile::Invalid;
        for (int id : ImageFileIO::registeredIDs()) {
                const ImageFileIO *io = ImageFileIO::lookup(id);
                if (io == nullptr || !io->isValid()) continue;
                for (const auto &e : io->extensions()) {
                        if (e == ext) return id;
                }
        }
        return ImageFile::Invalid;
}

static bool filenameHasMask(const String &name) {
        const int len = static_cast<int>(name.size());
        for (int i = 0; i < len; i++) {
                if (name[i] == '#') return true;
        }
        for (int i = 0; i < len; i++) {
                if (name[i] != '%') continue;
                int j = i + 1;
                if (j < len && name[j] == '0') j++;
                while (j < len && name[j] >= '0' && name[j] <= '9') j++;
                if (j < len && name[j] == 'd') return true;
        }
        return false;
}

static bool filenameIsImgSeqSidecar(const String &filename) {
        return extensionOf(filename) == kImgSeqExtension;
}

// Strip trailing non-alphanumeric characters from a NumName prefix.
static String sidecarPrefix(const NumName &nn) {
        String px = nn.prefix();
        while (!px.isEmpty()) {
                char c = px[px.size() - 1];
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
                        break;
                }
                px = px.left(px.size() - 1);
        }
        return px;
}

static String sidecarAudioName(const NumName &nn) {
        String px = sidecarPrefix(nn);
        if (px.isEmpty()) return String("audio.wav");
        return px + ".wav";
}

static String sidecarImgSeqName(const NumName &nn) {
        String px = sidecarPrefix(nn);
        if (px.isEmpty()) return String("sequence.imgseq");
        return px + ".imgseq";
}

// ============================================================================
// Magic number probing — used by ImageFileFactory::canHandleDevice.
// ============================================================================

static bool probeImageDevice(IODevice *device) {
        uint8_t buf[8] = {};
        int64_t n = device->read(buf, 8);
        if (n < 2) return false;

        uint32_t magic32 =
                (uint32_t(buf[0]) << 24) | (uint32_t(buf[1]) << 16) | (uint32_t(buf[2]) << 8) | uint32_t(buf[3]);

        // DPX: "SDPX" in either endian
        if (magic32 == 0x53445058 || magic32 == 0x58504453) return true;
        // Cineon: either endian
        if (magic32 == 0x802A5FD7 || magic32 == 0xD75F2A80) return true;
        // PNG
        if (magic32 == 0x89504E47) return true;
        // JPEG: SOI + marker
        if (buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF) return true;
        // JPEG XS: SOC
        if (buf[0] == 0xFF && buf[1] == 0x10) return true;
        // SGI
        uint16_t magic16 = (uint16_t(buf[0]) << 8) | uint16_t(buf[1]);
        if (magic16 == 0x01DA) return true;
        // PNM: 'P1'..'P6'
        if (buf[0] == 'P' && buf[1] >= '1' && buf[1] <= '6') return true;

        // .imgseq sidecar: JSON object starting with '{', possibly after
        // leading whitespace.  Slurp the rest of the file (capped) and
        // probe so the device-based path matches extension-based detection.
        int leading = 0;
        while (leading < n &&
               (buf[leading] == ' ' || buf[leading] == '\t' || buf[leading] == '\r' || buf[leading] == '\n')) {
                leading++;
        }
        if (leading < n && buf[leading] == '{') {
                device->seek(0);
                const int64_t cap = 64 * 1024;
                Buffer        body(cap);
                int64_t       total = device->read(body.data(), cap);
                if (total > 0) {
                        String text(static_cast<const char *>(body.data()), static_cast<size_t>(total));
                        if (ImgSeq::isImgSeqJson(text)) return true;
                }
        }
        return false;
}

// ============================================================================
// Shared config / metadata schemas
// ============================================================================

static MediaIOFactory::Config::SpecMap imageFileConfigSpecs() {
        MediaIOFactory::Config::SpecMap specs;
        auto                            s = [&specs](MediaConfig::ID id, const Variant &def) {
                const VariantSpec *gs = MediaConfig::spec(id);
                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
        };
        // 0 == ImageFile::Invalid — the Open handler treats this as "infer
        // the backend from the filename extension or the content probe".
        s(MediaConfig::ImageFileID, int32_t(0));
        // Empty size hint: only used by headerless formats (RawYUV) that
        // can't derive the geometry from the file itself.
        s(MediaConfig::VideoSize, Size2Du32());
        s(MediaConfig::VideoPixelFormat, PixelFormat());
        s(MediaConfig::FrameRate, ImageFileMediaIO::DefaultFrameRate);
        s(MediaConfig::SequenceHead, int32_t(ImageFileMediaIO::DefaultSequenceHead));
        s(MediaConfig::SaveImgSeqEnabled, true);
        s(MediaConfig::SaveImgSeqPath, String());
        s(MediaConfig::SaveImgSeqPathMode, ImgSeqPathMode::Relative);
        s(MediaConfig::SidecarAudioEnabled, true);
        s(MediaConfig::SidecarAudioPath, String());
        s(MediaConfig::AudioSource, AudioSourceHint::Sidecar);
        return specs;
}

static Metadata imageFileDefaultMetadata() {
        Metadata m;
        // Common (write-to-DPX-header) fields.
        m.set(Metadata::FileOrigName, String());
        m.set(Metadata::Date, String());
        m.set(Metadata::Software, String());
        m.set(Metadata::Project, String());
        m.set(Metadata::Copyright, String());
        m.set(Metadata::Reel, String());
        m.set(Metadata::Timecode, Timecode());
        m.set(Metadata::Gamma, double(0.0));
        m.set(Metadata::FrameRate, double(0.0));
        // Film information block.
        m.set(Metadata::FilmMfgID, String());
        m.set(Metadata::FilmType, String());
        m.set(Metadata::FilmOffset, String());
        m.set(Metadata::FilmPrefix, String());
        m.set(Metadata::FilmCount, String());
        m.set(Metadata::FilmFormat, String());
        m.set(Metadata::FilmSeqPos, int(0));
        m.set(Metadata::FilmSeqLen, int(0));
        m.set(Metadata::FilmHoldCount, int(1));
        m.set(Metadata::FilmShutter, double(0.0));
        m.set(Metadata::FilmFrameID, String());
        m.set(Metadata::FilmSlate, String());
        // TV / image element block.
        m.set(Metadata::FieldID, int(0));
        m.set(Metadata::TransferCharacteristic, int(0));
        m.set(Metadata::Colorimetric, int(0));
        m.set(Metadata::Orientation, int(0));
        // Sidecar audio (BWF) metadata forwarded to the sidecar audio
        // file when present.
        m.set(Metadata::EnableBWF, false);
        m.set(Metadata::Description, String());
        m.set(Metadata::Originator, String());
        m.set(Metadata::OriginatorReference, String());
        m.set(Metadata::OriginationDateTime, String());
        m.set(Metadata::CodingHistory, String());
        m.set(Metadata::UMID, String());
        return m;
}

// ============================================================================
// ImageFileFactory — parameterized so per-format ImgSeqXxx entries and
// the legacy "ImageFile" umbrella + .imgseq sidecar entries all share a
// single class.  Identity flows through the constructor; the create path
// is identical because all variants resolve the concrete ImageFile::ID
// from the caller-supplied filename at open time.
// ============================================================================

ImageFileFactory::ImageFileFactory(String name, String displayName, String description, StringList extensions,
                                   bool canBeSource, bool canBeSink)
        : _name(std::move(name)),
          _displayName(std::move(displayName)),
          _description(std::move(description)),
          _extensions(std::move(extensions)),
          _canBeSource(canBeSource),
          _canBeSink(canBeSink) {}

ImageFileFactory *ImageFileFactory::buildFactoryFor(const ImageFileIO *io) {
        if (io == nullptr || !io->isValid()) return nullptr;
        StringList exts = io->extensions();
        // The .imgseq sidecar gets its own factory entry below — keep it
        // out of per-backend extension lists so the per-format factories
        // stay narrowly scoped.
        return new ImageFileFactory(io->mediaIoName(), io->description(), io->description(), std::move(exts),
                                    /*canBeSource*/ io->canLoad(),
                                    /*canBeSink*/ io->canSave());
}

bool ImageFileFactory::canHandleDevice(IODevice *device) const {
        return probeImageDevice(device);
}

ImageFileFactory::Config::SpecMap ImageFileFactory::configSpecs() const {
        return imageFileConfigSpecs();
}

Metadata ImageFileFactory::defaultMetadata() const {
        return imageFileDefaultMetadata();
}

MediaIO *ImageFileFactory::create(const Config &config, ObjectBase *parent) const {
        auto *io = new ImageFileMediaIO(parent);
        io->setConfig(config);
        return io;
}

// ============================================================================
// ImageFileMediaIO lifecycle
// ============================================================================

ImageFileMediaIO::ImageFileMediaIO(ObjectBase *parent) : DedicatedThreadMediaIO(parent) {}

ImageFileMediaIO::~ImageFileMediaIO() {
        if (isOpen()) (void)close().wait();
}

Error ImageFileMediaIO::executeCmd(MediaIOCommandOpen &cmd) {
        const MediaIO::Config &cfg = cmd.config;

        // Stash a copy of the open-time config so every read / write
        // command forwards the same hints to the resolved
        // @ref ImageFileIO backend.  Codec-specific knobs (JpegQuality,
        // JpegSubsampling, ...) on @p cfg flow through unchanged.
        _ioConfig = cfg;
        _filename = cfg.getAs<String>(MediaConfig::Filename);

        // Direction is config-driven via MediaConfig::OpenMode.  Default
        // (Read) opens as a source; Write opens as a sink.
        Enum       modeEnum = cfg.get(MediaConfig::OpenMode).asEnum(MediaIOOpenMode::Type);
        const bool isWrite = modeEnum.value() == MediaIOOpenMode::Write.value();
        _isOpen = true;
        _isWrite = isWrite;

        _sequenceMode = false;
        _saveImgSeqEnabled = cfg.getAs<bool>(MediaConfig::SaveImgSeqEnabled, true);
        _saveImgSeqPath = cfg.getAs<String>(MediaConfig::SaveImgSeqPath, String());
        _saveImgSeqPathMode = cfg.get(MediaConfig::SaveImgSeqPathMode).asEnum(ImgSeqPathMode::Type, nullptr);
        _sidecarAudioEnabled = cfg.getAs<bool>(MediaConfig::SidecarAudioEnabled, true);

        // Resolve the reported frame rate with a documented priority:
        //   1. Writer with a valid pendingMediaDesc frame rate
        //   2. .imgseq sidecar frameRate (resolved below, reader only)
        //   3. Config value (MediaConfig::FrameRate — always present since the
        //      backend's defaultConfig pre-populates it)
        FrameRate fps = cfg.getAs<FrameRate>(MediaConfig::FrameRate, DefaultFrameRate);
        if (!fps.isValid()) fps = DefaultFrameRate;
        String frSource = kFrameRateSourceConfig;

        if (isWrite && cmd.pendingMediaDesc.frameRate().isValid()) {
                fps = cmd.pendingMediaDesc.frameRate();
                frSource = kFrameRateSourceFile;
        }

        MediaDesc  mediaDesc;
        mediaDesc.setFrameRate(fps);
        bool       canSeek = false;
        FrameCount frameCount = FrameCount::unknown();

        const bool isSidecar = filenameIsImgSeqSidecar(_filename);
        const bool hasMask = filenameHasMask(_filename);

        if (isSidecar || hasMask) {
                _sequenceMode = true;
                Error err = openSequence(cmd, mediaDesc, frSource, isWrite, canSeek, frameCount);
                if (err.isError()) return err;
        } else {
                Error err = openSingle(cmd, mediaDesc, frSource, isWrite, canSeek, frameCount);
                if (err.isError()) return err;
        }

        _writerFrameRate = mediaDesc.frameRate();

        MediaIOPortGroup *group = addPortGroup("imagefile");
        if (group == nullptr) return Error::Invalid;
        group->setFrameRate(mediaDesc.frameRate());
        group->setCanSeek(canSeek);
        group->setFrameCount(frameCount);
        if (isWrite) {
                if (addSink(group, mediaDesc) == nullptr) return Error::Invalid;
        } else {
                if (addSource(group, mediaDesc) == nullptr) return Error::Invalid;
        }
        return Error::Ok;
}

Error ImageFileMediaIO::openSingle(MediaIOCommandOpen &cmd, MediaDesc &mediaDesc, const String &frSource, bool isWrite,
                                   bool &canSeek, FrameCount &frameCount) {
        const MediaIO::Config &cfg = cmd.config;

        // MediaConfig::ImageFileID lives in the default config as 0
        // (ImageFile::Invalid) to document the knob; any non-zero value
        // from the caller is taken as an explicit override, otherwise
        // we fall back to auto-detection from the filename extension.
        _imageFileID = cfg.getAs<int>(MediaConfig::ImageFileID, ImageFile::Invalid);
        if (_imageFileID == ImageFile::Invalid) {
                _imageFileID = imageFileIDFromExtension(_filename);
        }

        if (_imageFileID == ImageFile::Invalid) {
                promekiErr("ImageFileMediaIO: cannot determine image format for '%s'", _filename.cstr());
                return Error::NotSupported;
        }

        const ImageFileIO *io = ImageFileIO::lookup(_imageFileID);
        if (!io->isValid()) {
                promekiErr("ImageFileMediaIO: no ImageFileIO backend for ID %d", _imageFileID);
                return Error::NotSupported;
        }
        if (!isWrite && !io->canLoad()) {
                promekiErr("ImageFileMediaIO: backend '%s' does not support loading", io->name().cstr());
                return Error::NotSupported;
        }
        if (isWrite && !io->canSave()) {
                promekiErr("ImageFileMediaIO: backend '%s' does not support saving", io->name().cstr());
                return Error::NotSupported;
        }

        if (!isWrite) {
                ImageFile imgFile(_imageFileID);
                imgFile.setFilename(_filename);

                Size2Du32 hintSize = cfg.getAs<Size2Du32>(MediaConfig::VideoSize, Size2Du32());
                if (hintSize.width() > 0 && hintSize.height() > 0) {
                        PixelFormat pd = cfg.getAs<PixelFormat>(MediaConfig::VideoPixelFormat, PixelFormat());
                        if (pd.isValid()) {
                                ImageDesc idesc(hintSize, pd);
                                auto      hint = UncompressedVideoPayload::Ptr::create(idesc);
                                Frame     hintFrame;
                                hintFrame.addPayload(hint);
                                imgFile.setFrame(hintFrame);
                        }
                }

                Error err = imgFile.load(_ioConfig);
                if (err.isError()) {
                        promekiErr("ImageFileMediaIO: load '%s' failed: %s", _filename.cstr(), err.name().cstr());
                        return err;
                }

                _frame = Frame::Ptr::create(imgFile.frame());

                auto vids = _frame->videoPayloads();
                if (!vids.isEmpty() && vids[0].isValid()) {
                        const ImageDesc &idesc = vids[0]->desc();
                        mediaDesc.imageList().pushToBack(idesc);
                }

                // Surface embedded audio (e.g. DPX user-data AUDIO blocks)
                // in the descriptor so downstream consumers wire up an
                // audio sink.
                auto auds = _frame->audioPayloads();
                if (!auds.isEmpty() && auds[0].isValid()) {
                        const AudioDesc &adesc = auds[0]->desc();
                        if (adesc.isValid()) {
                                mediaDesc.audioList().pushToBack(adesc);
                        }
                }

                Metadata meta = _frame->metadata();
                meta.set(Metadata::FrameRateSource, frSource);
                mediaDesc.metadata() = meta;
                frameCount = FrameCount(1);
                _loaded = false;
        } else {
                FrameRate savedFps = mediaDesc.frameRate();
                mediaDesc = cmd.pendingMediaDesc;
                if (!mediaDesc.frameRate().isValid()) {
                        mediaDesc.setFrameRate(savedFps);
                }
                Metadata meta = cmd.pendingMetadata;
                meta.set(Metadata::FrameRateSource, frSource);
                mediaDesc.metadata() = meta;
                _writeContainerMetadata = meta;
                _writeCount = 0;
                frameCount = FrameCount(0);
        }

        canSeek = false;
        return Error::Ok;
}

Error ImageFileMediaIO::openSequence(MediaIOCommandOpen &cmd, MediaDesc &mediaDesc, const String &frSourceIn,
                                     bool isWrite, bool &canSeek, FrameCount &frameCount) {
        const MediaIO::Config &cfg = cmd.config;
        String                 frSource = frSourceIn;

        NumName     pattern;
        FilePath    dir;
        int64_t     head = -1;
        int64_t     tail = -1;
        Size2Du32   hintSize = cfg.getAs<Size2Du32>(MediaConfig::VideoSize, Size2Du32());
        PixelFormat hintPixel = cfg.getAs<PixelFormat>(MediaConfig::VideoPixelFormat, PixelFormat());
        Metadata    sidecarMeta;
        FrameRate   sidecarFps;
        String      sidecarAudioFile;

        if (filenameIsImgSeqSidecar(_filename)) {
                Error  sErr;
                ImgSeq seq = ImgSeq::load(FilePath(_filename), &sErr);
                if (sErr.isError() || !seq.isValid()) {
                        promekiErr("ImageFileMediaIO: invalid .imgseq sidecar '%s'", _filename.cstr());
                        return Error::Invalid;
                }
                pattern = seq.name();
                FilePath sidecarDir = FilePath(_filename).parent();
                if (sidecarDir.isEmpty()) sidecarDir = FilePath(".");
                if (!seq.dir().isEmpty()) {
                        if (seq.dir().isAbsolute()) {
                                dir = seq.dir();
                        } else {
                                dir = sidecarDir / seq.dir();
                        }
                } else {
                        dir = sidecarDir;
                }
                if (seq.head() != 0 || seq.tail() != 0) {
                        head = static_cast<int64_t>(seq.head());
                        tail = static_cast<int64_t>(seq.tail());
                }
                if (seq.frameRate().isValid()) {
                        sidecarFps = seq.frameRate();
                }
                if (seq.videoSize().width() > 0 && seq.videoSize().height() > 0) {
                        hintSize = seq.videoSize();
                }
                if (seq.pixelFormat().isValid()) {
                        hintPixel = seq.pixelFormat();
                }
                sidecarMeta = seq.metadata();
                sidecarAudioFile = seq.audioFile();
        } else {
                FilePath full(_filename);
                dir = full.parent();
                if (dir.isEmpty()) dir = FilePath(".");
                String maskName = full.fileName();
                pattern = NumName::fromMask(maskName);

                if (_saveImgSeqEnabled && pattern.isValid()) {
                        FilePath imgseqPath = dir / sidecarImgSeqName(pattern);
                        if (imgseqPath.exists()) {
                                Error  sErr;
                                ImgSeq seq = ImgSeq::load(imgseqPath, &sErr);
                                if (sErr.isOk() && seq.isValid()) {
                                        if (seq.head() != 0 || seq.tail() != 0) {
                                                head = static_cast<int64_t>(seq.head());
                                                tail = static_cast<int64_t>(seq.tail());
                                        }
                                        if (seq.frameRate().isValid()) {
                                                sidecarFps = seq.frameRate();
                                        }
                                        if (seq.videoSize().width() > 0 && seq.videoSize().height() > 0) {
                                                hintSize = seq.videoSize();
                                        }
                                        if (seq.pixelFormat().isValid()) {
                                                hintPixel = seq.pixelFormat();
                                        }
                                        sidecarMeta = seq.metadata();
                                        sidecarAudioFile = seq.audioFile();
                                }
                        }
                }
        }

        if (!pattern.isValid()) {
                promekiErr("ImageFileMediaIO: cannot parse sequence mask from '%s'", _filename.cstr());
                return Error::Invalid;
        }

        _seqName = pattern;
        _seqDir = dir;
        _seqMetadata = sidecarMeta;
        _seqSize = hintSize;
        _seqPixelFormat = hintPixel;

        _imageFileID = cfg.getAs<int>(MediaConfig::ImageFileID, ImageFile::Invalid);
        if (_imageFileID == ImageFile::Invalid) {
                _imageFileID = imageFileIDFromExtension(pattern.suffix());
        }
        if (_imageFileID == ImageFile::Invalid) {
                promekiErr("ImageFileMediaIO: cannot determine image format for pattern '%s'",
                           pattern.hashmask().cstr());
                return Error::NotSupported;
        }

        const ImageFileIO *io = ImageFileIO::lookup(_imageFileID);
        if (!io->isValid()) {
                promekiErr("ImageFileMediaIO: no ImageFileIO backend for ID %d", _imageFileID);
                return Error::NotSupported;
        }
        if (!isWrite && !io->canLoad()) {
                promekiErr("ImageFileMediaIO: backend '%s' does not support loading", io->name().cstr());
                return Error::NotSupported;
        }
        if (isWrite && !io->canSave()) {
                promekiErr("ImageFileMediaIO: backend '%s' does not support saving", io->name().cstr());
                return Error::NotSupported;
        }

        if (frSource != kFrameRateSourceFile && sidecarFps.isValid()) {
                mediaDesc.setFrameRate(sidecarFps);
                frSource = kFrameRateSourceFile;
        }

        if (!isWrite) {
                if (head < 0 || tail < 0 || tail < head) {
                        bool    haveAny = false;
                        int64_t detHead = 0;
                        int64_t detTail = 0;
                        Dir     d(dir);
                        for (const FilePath &entry : d.entryList()) {
                                int     val = -1;
                                NumName parsed = NumName::parse(entry.fileName(), &val);
                                if (!parsed.isValid()) continue;
                                if (!pattern.isInSequence(parsed)) continue;
                                if (val < 0) continue;
                                if (!haveAny) {
                                        detHead = val;
                                        detTail = val;
                                        haveAny = true;
                                } else {
                                        if (val < detHead) detHead = val;
                                        if (val > detTail) detTail = val;
                                }
                        }
                        if (!haveAny) {
                                promekiErr("ImageFileMediaIO: no files found matching '%s' in '%s'",
                                           pattern.hashmask().cstr(), dir.toString().cstr());
                                return Error::NotExist;
                        }
                        head = detHead;
                        tail = detTail;
                }

                _seqHead = head;
                _seqTail = tail;
                _seqIndex = 0;
                _seqAtEnd = false;

                String    firstPath = (dir / pattern.name(static_cast<int>(head))).toString();
                ImageFile imgFile(_imageFileID);
                imgFile.setFilename(firstPath);
                if (_seqSize.width() > 0 && _seqSize.height() > 0 && _seqPixelFormat.isValid()) {
                        ImageDesc idesc(_seqSize, _seqPixelFormat);
                        auto      hint = UncompressedVideoPayload::Ptr::create(idesc);
                        Frame     hintFrame;
                        hintFrame.addPayload(hint);
                        imgFile.setFrame(hintFrame);
                }
                Error err = imgFile.load(_ioConfig);
                if (err.isError()) {
                        promekiErr("ImageFileMediaIO: failed to load head frame '%s': %s", firstPath.cstr(),
                                   err.name().cstr());
                        return err;
                }
                const Frame &f = imgFile.frame();
                auto         headVids = f.videoPayloads();
                if (!headVids.isEmpty() && headVids[0].isValid()) {
                        mediaDesc.imageList().pushToBack(headVids[0]->desc());
                }

                auto headAuds = f.audioPayloads();
                if (!headAuds.isEmpty() && headAuds[0].isValid()) {
                        const AudioDesc &adesc = headAuds[0]->desc();
                        if (adesc.isValid()) {
                                mediaDesc.audioList().pushToBack(adesc);
                        }
                }

                Metadata meta = f.metadata();
                meta.merge(_seqMetadata);
                meta.set(Metadata::FrameRateSource, frSource);
                mediaDesc.metadata() = meta;
                frameCount = FrameCount(_seqTail.value() - _seqHead.value() + 1);

                // --- Audio source selection (reader) ---
                bool hasEmbeddedAudio = !mediaDesc.audioList().isEmpty();
                bool hasSidecarAudio = false;

                if (_sidecarAudioEnabled) {
                        String audioPath;
                        if (!sidecarAudioFile.isEmpty()) {
                                FilePath ap(sidecarAudioFile);
                                if (ap.isAbsolute()) {
                                        audioPath = sidecarAudioFile;
                                } else {
                                        audioPath = (dir / sidecarAudioFile).toString();
                                }
                        } else {
                                String cfgPath = cfg.getAs<String>(MediaConfig::SidecarAudioPath, String());
                                if (!cfgPath.isEmpty()) {
                                        FilePath ap(cfgPath);
                                        if (ap.isAbsolute()) {
                                                audioPath = cfgPath;
                                        } else {
                                                audioPath = (dir / cfgPath).toString();
                                        }
                                } else {
                                        audioPath = (dir / sidecarAudioName(pattern)).toString();
                                }
                        }

                        if (FilePath(audioPath).exists()) {
                                _sidecarAudio = AudioFile::createReader(audioPath);
                                if (!_sidecarAudio.isValid()) {
                                        promekiErr("ImageFileMediaIO: failed to create sidecar audio reader for '%s'",
                                                   audioPath.cstr());
                                        return Error::NotSupported;
                                }
                                Error audioErr = _sidecarAudio.open();
                                if (audioErr.isError()) {
                                        promekiErr("ImageFileMediaIO: sidecar audio open '%s' failed: %s",
                                                   audioPath.cstr(), audioErr.name().cstr());
                                        return audioErr;
                                }
                                _sidecarAudioDesc = _sidecarAudio.desc();
                                _sidecarAudioPath = audioPath;
                                _sidecarFrameRate = mediaDesc.frameRate();
                                _sidecarSampleRate = static_cast<int64_t>(_sidecarAudioDesc.sampleRate());
                                hasSidecarAudio = true;
                        }
                }

                Enum sourceHint = cfg.get(MediaConfig::AudioSource).asEnum(AudioSourceHint::Type, nullptr);
                bool preferSidecar = !sourceHint.isValid() || sourceHint == AudioSourceHint::Sidecar;
                bool useSidecar;
                if (preferSidecar) {
                        useSidecar = hasSidecarAudio ? true : false;
                } else {
                        useSidecar = hasEmbeddedAudio ? false : hasSidecarAudio;
                }

                if (useSidecar) {
                        _sidecarAudioOpen = true;
                        mediaDesc.audioList().clear();
                        mediaDesc.audioList().pushToBack(_sidecarAudioDesc);
                } else if (hasSidecarAudio) {
                        _sidecarAudio.close();
                        _sidecarAudio = AudioFile();
                        _sidecarAudioDesc = AudioDesc();
                        _sidecarAudioPath = String();
                        _sidecarFrameRate = FrameRate();
                        _sidecarSampleRate = 0;
                }

        } else {
                Dir outDir(dir);
                if (!outDir.exists()) {
                        Error mkErr = outDir.mkpath();
                        if (mkErr.isError()) {
                                promekiErr("ImageFileMediaIO: failed to create output directory '%s': %s",
                                           dir.toString().cstr(), mkErr.name().cstr());
                                return mkErr;
                        }
                }

                int64_t writeHead = cfg.getAs<int>(MediaConfig::SequenceHead, DefaultSequenceHead);
                if (head >= 0) writeHead = head;
                _seqHead = writeHead;
                _seqTail = (tail >= 0) ? tail : _seqHead;
                _seqIndex = 0;
                _seqAtEnd = false;
                _writeCount = 0;

                if (!cmd.pendingMediaDesc.imageList().isEmpty()) {
                        const ImageDesc &id = cmd.pendingMediaDesc.imageList()[0];
                        _seqSize = id.size();
                        _seqPixelFormat = id.pixelFormat();
                }

                Metadata meta = cmd.pendingMetadata;
                meta.merge(_seqMetadata);
                meta.set(Metadata::FrameRateSource, frSource);
                mediaDesc.metadata() = meta;
                _writeContainerMetadata = meta;
                frameCount = FrameCount(0);

                if (_saveImgSeqEnabled && _saveImgSeqPath.isEmpty()) {
                        _saveImgSeqPath = (dir / sidecarImgSeqName(pattern)).toString();
                }

                if (_sidecarAudioEnabled) {
                        AudioDesc audioDesc;
                        if (cmd.pendingAudioDesc.isValid()) {
                                audioDesc = cmd.pendingAudioDesc;
                        } else if (!cmd.pendingMediaDesc.audioList().isEmpty()) {
                                audioDesc = cmd.pendingMediaDesc.audioList()[0];
                        }

                        if (audioDesc.isValid()) {
                                String cfgPath = cfg.getAs<String>(MediaConfig::SidecarAudioPath, String());
                                String audioPath;
                                String audioName;
                                if (!cfgPath.isEmpty()) {
                                        FilePath ap(cfgPath);
                                        if (ap.isAbsolute()) {
                                                audioPath = cfgPath;
                                                audioName = ap.fileName();
                                        } else {
                                                audioPath = (dir / cfgPath).toString();
                                                audioName = cfgPath;
                                        }
                                } else {
                                        audioName = sidecarAudioName(pattern);
                                        audioPath = (dir / audioName).toString();
                                }

                                Metadata audioMeta = _writeContainerMetadata;
                                audioMeta.set(Metadata::EnableBWF, true);
                                audioMeta.set(Metadata::FrameRate, _writerFrameRate.toDouble());
                                audioMeta.merge(audioDesc.metadata());
                                audioDesc.metadata() = std::move(audioMeta);

                                _sidecarAudio = AudioFile::createWriter(audioPath);
                                if (!_sidecarAudio.isValid()) {
                                        promekiErr("ImageFileMediaIO: failed to create sidecar audio writer for '%s'",
                                                   audioPath.cstr());
                                        return Error::NotSupported;
                                }
                                _sidecarAudio.setDesc(audioDesc);
                                Error audioErr = _sidecarAudio.open();
                                if (audioErr.isError()) {
                                        promekiErr("ImageFileMediaIO: sidecar audio open '%s' for write failed: %s",
                                                   audioPath.cstr(), audioErr.name().cstr());
                                        return audioErr;
                                }
                                _sidecarAudioDesc = audioDesc;
                                _sidecarAudioPath = audioPath;
                                _sidecarAudioName = audioName;
                                _sidecarAudioOpen = true;
                                _sidecarFrameRate = _writerFrameRate;
                                _sidecarSampleRate = static_cast<int64_t>(_sidecarAudioDesc.sampleRate());

                                mediaDesc.audioList().pushToBack(_sidecarAudioDesc);
                        }
                }
        }

        canSeek = !isWrite;
        return Error::Ok;
}

Error ImageFileMediaIO::writeImgSeqSidecar() {
        if (!_saveImgSeqEnabled) return Error::Ok;
        if (_saveImgSeqPath.isEmpty()) return Error::Ok;
        if (_writeCount <= 0) {
                promekiInfo("ImageFileMediaIO: skipping .imgseq sidecar — no frames written");
                return Error::Ok;
        }
        ImgSeq seq;
        seq.setName(_seqName);
        seq.setHead(static_cast<size_t>(_seqHead.value()));
        seq.setTail(static_cast<size_t>(_seqHead.value() + _writeCount.value() - 1));
        if (_writerFrameRate.isValid()) {
                seq.setFrameRate(_writerFrameRate);
        }
        if (_seqSize.width() > 0 && _seqSize.height() > 0) {
                seq.setVideoSize(_seqSize);
        }
        if (_seqPixelFormat.isValid()) {
                seq.setPixelFormat(_seqPixelFormat);
        }
        if (!_sidecarAudioName.isEmpty()) {
                seq.setAudioFile(_sidecarAudioName);
        }

        FilePath sidecarPath(_saveImgSeqPath);
        if (sidecarPath.isRelative()) {
                FilePath imageDir = _seqDir;
                if (imageDir.isEmpty()) imageDir = FilePath(".");
                sidecarPath = imageDir / sidecarPath;
        }

        FilePath sidecarDir = sidecarPath.parent();
        if (sidecarDir.isEmpty()) sidecarDir = FilePath(".");
        FilePath imageDir = _seqDir;
        if (imageDir.isEmpty()) imageDir = FilePath(".");

        bool absolute = _saveImgSeqPathMode.isValid() && _saveImgSeqPathMode == ImgSeqPathMode::Absolute;
        if (absolute) {
                seq.setDir(imageDir.absolutePath());
        } else {
                Result<FilePath> rel = imageDir.absolutePath().relativeTo(sidecarDir.absolutePath());
                if (rel.second().isOk() && rel.first().toString() != ".") {
                        seq.setDir(rel.first());
                }
        }

        Error err = seq.save(sidecarPath);
        if (err.isError()) {
                promekiErr("ImageFileMediaIO: failed to write .imgseq sidecar '%s': %s",
                           sidecarPath.toString().cstr(), err.name().cstr());
        } else {
                promekiInfo("ImageFileMediaIO: wrote .imgseq sidecar '%s'", sidecarPath.toString().cstr());
        }
        return err;
}

Error ImageFileMediaIO::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        if (_isWrite && _sequenceMode) {
                writeImgSeqSidecar();
        }

        if (_sidecarAudioOpen) {
                _sidecarAudio.close();
                _sidecarAudioOpen = false;
        }

        _frame = {};
        _filename = String();
        _imageFileID = ImageFile::Invalid;
        _isOpen = false;
        _isWrite = false;
        _readCount = 0;
        _writeCount = 0;
        _loaded = false;
        _writeContainerMetadata = Metadata();
        _ioConfig = MediaConfig();
        _saveImgSeqEnabled = true;
        _saveImgSeqPath = String();
        _saveImgSeqPathMode = Enum();
        _writerFrameRate = FrameRate();

        _sequenceMode = false;
        _seqName = NumName();
        _seqDir = FilePath();
        _seqHead = 0;
        _seqTail = 0;
        _seqIndex = 0;
        _seqAtEnd = false;
        _seqMetadata = Metadata();
        _seqSize = Size2Du32();
        _seqPixelFormat = PixelFormat();

        _sidecarAudio = AudioFile();
        _sidecarAudioDesc = AudioDesc();
        _sidecarAudioPath = String();
        _sidecarFrameRate = FrameRate();
        _sidecarSampleRate = 0;
        _sidecarAudioEnabled = true;
        _sidecarAudioName = String();
        return Error::Ok;
}

// ============================================================================
// Frame I/O
// ============================================================================

Error ImageFileMediaIO::executeCmd(MediaIOCommandRead &cmd) {
        return _sequenceMode ? readSequence(cmd) : readSingle(cmd);
}

Error ImageFileMediaIO::readSingle(MediaIOCommandRead &cmd) {
        if (_loaded && cmd.step != 0) return Error::EndOfFile;
        cmd.frame = _frame;
        _loaded = true;
        ++_readCount;
        cmd.currentFrame = toFrameNumber(_readCount);
        return Error::Ok;
}

Error ImageFileMediaIO::readSequence(MediaIOCommandRead &cmd) {
        if (_seqAtEnd) return Error::EndOfFile;

        const int64_t length = _seqTail.value() - _seqHead.value() + 1;
        if (length <= 0) return Error::EndOfFile;

        if (!_seqIndex.isValid() || _seqIndex.value() >= length) {
                _seqAtEnd = true;
                return Error::EndOfFile;
        }

        int64_t frameNum = _seqHead.value() + _seqIndex.value();
        String  fn = (_seqDir / _seqName.name(static_cast<int>(frameNum))).toString();

        ImageFile imgFile(_imageFileID);
        imgFile.setFilename(fn);
        if (_seqSize.width() > 0 && _seqSize.height() > 0 && _seqPixelFormat.isValid()) {
                ImageDesc idesc(_seqSize, _seqPixelFormat);
                auto      hint = UncompressedVideoPayload::Ptr::create(idesc);
                Frame     hintFrame;
                hintFrame.addPayload(hint);
                imgFile.setFrame(hintFrame);
        }
        Error err = imgFile.load(_ioConfig);
        if (err.isError()) {
                promekiErr("ImageFileMediaIO: failed to load sequence frame '%s': %s", fn.cstr(), err.name().cstr());
                return err;
        }

        Frame::Ptr frame = Frame::Ptr::create(imgFile.frame());
        Metadata  &fm = frame.modify()->metadata();
        fm.merge(_seqMetadata);
        fm.set(Metadata::FrameNumber, FrameNumber(frameNum));

        if (_sidecarAudioOpen) {
                size_t               spf = _sidecarFrameRate.samplesPerFrame(_sidecarSampleRate, _seqIndex.value());
                PcmAudioPayload::Ptr sidecarPayload;
                Error                audioErr = _sidecarAudio.read(sidecarPayload, spf);
                if (audioErr.isError()) {
                        promekiErr("ImageFileMediaIO: sidecar audio read failed: %s", audioErr.name().cstr());
                        return audioErr;
                }
                Frame                *fmut = frame.modify();
                MediaPayload::PtrList keep;
                keep.reserve(fmut->payloadList().size());
                for (MediaPayload::Ptr &p : fmut->payloadList()) {
                        if (!p.isValid()) {
                                keep.pushToBack(p);
                                continue;
                        }
                        if (p->kind() != MediaPayloadKind::Audio) keep.pushToBack(p);
                }
                fmut->payloadList() = std::move(keep);
                if (sidecarPayload.isValid()) fmut->addPayload(sidecarPayload);
        }

        cmd.frame = frame;
        cmd.currentFrame = _seqIndex + int64_t(1);

        int step = cmd.step;
        if (step == 0) {
                // Hold on the same frame — no state change.
        } else {
                _seqIndex += int64_t(step);
                if (!_seqIndex.isValid() || _seqIndex.value() >= length) {
                        _seqAtEnd = true;
                }
        }
        return Error::Ok;
}

Error ImageFileMediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        return _sequenceMode ? writeSequence(cmd) : writeSingle(cmd);
}

Error ImageFileMediaIO::writeSingle(MediaIOCommandWrite &cmd) {
        Frame    frame = *cmd.frame;
        Metadata merged = _writeContainerMetadata;
        merged.merge(frame.metadata());
        frame.metadata() = std::move(merged);

        ImageFile imgFile(_imageFileID);
        imgFile.setFilename(_filename);
        imgFile.setFrame(frame);
        Error err = imgFile.save(_ioConfig);
        if (err.isError()) {
                promekiErr("ImageFileMediaIO: save '%s' failed: %s", _filename.cstr(), err.name().cstr());
                return err;
        }
        ++_writeCount;
        cmd.currentFrame = toFrameNumber(_writeCount);
        cmd.frameCount = _writeCount;
        return Error::Ok;
}

Error ImageFileMediaIO::writeSequence(MediaIOCommandWrite &cmd) {
        int64_t frameNum = _seqHead.value() + _writeCount.value();
        String  fn = (_seqDir / _seqName.name(static_cast<int>(frameNum))).toString();

        Frame    frame = *cmd.frame;
        Metadata merged = _writeContainerMetadata;
        merged.merge(frame.metadata());
        frame.metadata() = std::move(merged);

        ImageFile imgFile(_imageFileID);
        imgFile.setFilename(fn);
        imgFile.setFrame(frame);
        Error err = imgFile.save(_ioConfig);
        if (err.isError()) {
                promekiErr("ImageFileMediaIO: save sequence frame '%s' failed: %s", fn.cstr(), err.name().cstr());
                return err;
        }

        if (_sidecarAudioOpen) {
                auto                   auds = cmd.frame->audioPayloads();
                const PcmAudioPayload *uap = nullptr;
                if (!auds.isEmpty() && auds[0].isValid()) {
                        uap = auds[0]->as<PcmAudioPayload>();
                }
                if (uap != nullptr) {
                        Error audioErr = _sidecarAudio.write(*uap);
                        if (audioErr.isError()) {
                                promekiErr("ImageFileMediaIO: sidecar audio write failed: %s",
                                           audioErr.name().cstr());
                                return audioErr;
                        }
                } else {
                        size_t       spf = _sidecarFrameRate.samplesPerFrame(_sidecarSampleRate, _writeCount.value());
                        const size_t bytes = _sidecarAudioDesc.bufferSize(spf);
                        auto         buf = Buffer::Ptr::create(bytes);
                        buf.modify()->setSize(bytes);
                        std::memset(buf.modify()->data(), 0, bytes);
                        BufferView planes;
                        planes.pushToBack(buf, 0, bytes);
                        auto  silence = PcmAudioPayload::Ptr::create(_sidecarAudioDesc, spf, planes);
                        Error audioErr = _sidecarAudio.write(*silence);
                        if (audioErr.isError()) {
                                promekiErr("ImageFileMediaIO: sidecar audio silence write failed: %s",
                                           audioErr.name().cstr());
                                return audioErr;
                        }
                }
        }

        ++_writeCount;
        if (frameNum > _seqTail.value()) _seqTail = FrameNumber(frameNum);
        cmd.currentFrame = toFrameNumber(_writeCount);
        cmd.frameCount = _writeCount;
        return Error::Ok;
}

Error ImageFileMediaIO::executeCmd(MediaIOCommandSeek &cmd) {
        if (!_sequenceMode || !_isOpen || _isWrite) {
                return Error::IllegalSeek;
        }

        const int64_t length = _seqTail.value() - _seqHead.value() + 1;
        if (length <= 0) return Error::IllegalSeek;

        int64_t target = cmd.frameNumber.isValid() ? cmd.frameNumber.value() : 0;
        if (target < 0) target = 0;
        if (target >= length) target = length - 1;
        _seqIndex = FrameNumber(target);
        _seqAtEnd = false;

        if (_sidecarAudioOpen) {
                size_t targetSample =
                        static_cast<size_t>(_sidecarFrameRate.cumulativeTicks(_sidecarSampleRate, target));
                Error audioErr = _sidecarAudio.seekToSample(targetSample);
                if (audioErr.isError()) {
                        promekiErr("ImageFileMediaIO: sidecar audio seek to sample %zu failed: %s", targetSample,
                                   audioErr.name().cstr());
                        return audioErr;
                }
        }

        cmd.currentFrame = _seqIndex;
        return Error::Ok;
}

// ============================================================================
// Negotiation overrides
// ============================================================================

namespace {

        String extractExt(const String &path) {
                const size_t dot = path.rfind('.');
                if (dot == String::npos || dot + 1 >= path.size()) return String();
                return path.mid(dot + 1).toLower();
        }

        int componentBits(const PixelFormat &pd) {
                if (!pd.isValid() || pd.isCompressed()) return 0;
                if (pd.memLayout().compCount() == 0) return 0;
                return static_cast<int>(pd.memLayout().compDesc(0).bits);
        }

        // True when the source's ColorModel family is YCbCr (luma +
        // chroma-difference) — used to pick a YUV-family writer target so
        // the inserted CSC stays inside the matching colour space.
        bool isYuvSource(const PixelFormat &pd) {
                if (!pd.isValid()) return false;
                return pd.colorModel().type() == ColorModel::TypeYCbCr;
        }

} // namespace

PixelFormat ImageFileMediaIO::preferredWriterPixelFormat(const String &filename, const PixelFormat &source) const {
        const String ext = extractExt(filename);
        if (ext.isEmpty()) return PixelFormat();

        const int srcBits = componentBits(source);

        if (ext == "dpx" || ext == "cin") {
                if (srcBits >= 16) return PixelFormat(PixelFormat::RGB16_BE_sRGB);
                if (srcBits >= 10) return PixelFormat(PixelFormat::RGB10_DPX_sRGB);
                if (srcBits >= 8) return PixelFormat(PixelFormat::RGBA8_sRGB);
                return PixelFormat(PixelFormat::RGB10_DPX_sRGB);
        }

        if (ext == "jpg" || ext == "jpeg" || ext == "jfif") {
                return isYuvSource(source) ? PixelFormat(PixelFormat::YUV8_422_Planar_Rec709)
                                           : PixelFormat(PixelFormat::RGBA8_sRGB);
        }

        if (ext == "png") {
                if (srcBits >= 16) return PixelFormat(PixelFormat::RGBA16_LE_sRGB);
                return PixelFormat(PixelFormat::RGBA8_sRGB);
        }

        if (ext == "tga") return PixelFormat(PixelFormat::RGBA8_sRGB);

        if (ext == "sgi" || ext == "rgb" || ext == "rgba" || ext == "bw") {
                if (srcBits >= 16) return PixelFormat(PixelFormat::RGBA16_BE_sRGB);
                return PixelFormat(PixelFormat::RGBA8_sRGB);
        }

        if (ext == "pnm" || ext == "ppm" || ext == "pgm" || ext == "pbm") {
                if (srcBits >= 16) return PixelFormat(PixelFormat::RGB16_BE_sRGB);
                return PixelFormat(PixelFormat::RGB8_sRGB);
        }

        return PixelFormat();
}

Error ImageFileMediaIO::proposeInput(const MediaDesc &offered, MediaDesc *preferred) const {
        if (preferred == nullptr) return Error::Invalid;
        if (offered.imageList().isEmpty()) {
                *preferred = offered;
                return Error::Ok;
        }

        const MediaIO::Config &cfg = config();
        const String           filename =
                cfg.contains(MediaConfig::Filename) ? cfg.getAs<String>(MediaConfig::Filename) : String();

        const PixelFormat &offeredPd = offered.imageList()[0].pixelFormat();
        const PixelFormat  target = preferredWriterPixelFormat(filename, offeredPd);

        if (!target.isValid() || target == offeredPd) {
                *preferred = offered;
                return Error::Ok;
        }

        MediaDesc want = offered;
        ImageDesc img(offered.imageList()[0].size(), target);
        img.setVideoScanMode(offered.imageList()[0].videoScanMode());
        want.imageList().clear();
        want.imageList().pushToBack(img);
        *preferred = want;
        return Error::Ok;
}

// ============================================================================
// Static umbrella + .imgseq sidecar registration.  Per-format ImgSeqXxx
// factory entries are registered by ImageFileIO::registerImageFileIO so
// the two registries stay in lockstep without cross-TU static-init
// ordering hazards.
// ============================================================================

namespace {

        StringList buildAllExtensions() {
                StringList exts;
                for (int id : ImageFileIO::registeredIDs()) {
                        const ImageFileIO *io = ImageFileIO::lookup(id);
                        if (io == nullptr || !io->isValid()) continue;
                        for (const auto &e : io->extensions()) {
                                bool dup = false;
                                for (const auto &seen : exts) {
                                        if (seen == e) {
                                                dup = true;
                                                break;
                                        }
                                }
                                if (!dup) exts.pushToBack(e);
                        }
                }
                exts.pushToBack(String(kImgSeqExtension));
                return exts;
        }

        int registerImageFileUmbrella() {
                MediaIOFactory::registerFactory(new ImageFileFactory(
                        String("ImageFile"), String("Image File"),
                        String("Single-image files and image sequences (DPX, Cineon, TGA, SGI, "
                               "PNM, PNG, JPEG, JPEG XS, RawYUV, .imgseq)"),
                        buildAllExtensions(), /*canBeSource*/ true, /*canBeSink*/ true));

                StringList seqExts;
                seqExts.pushToBack(String(kImgSeqExtension));
                MediaIOFactory::registerFactory(
                        new ImageFileFactory(String("ImgSeq"), String("Image Sequence (.imgseq)"),
                                             String("ImgSeq JSON sidecar (points at an underlying image format)"),
                                             std::move(seqExts), /*canBeSource*/ true, /*canBeSink*/ true));
                return 0;
        }

        [[maybe_unused]] static int __promeki_imagefile_umbrella_registered = registerImageFileUmbrella();

} // namespace

PROMEKI_NAMESPACE_END
