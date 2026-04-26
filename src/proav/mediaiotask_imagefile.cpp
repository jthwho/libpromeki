/**
 * @file      mediaiotask_imagefile.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <promeki/mediaiotask_imagefile.h>
#include <promeki/colormodel.h>
#include <promeki/imagefileio.h>
#include <promeki/iodevice.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/videopayload.h>
#include <promeki/imagedesc.h>
#include <promeki/logger.h>
#include <promeki/imgseq.h>
#include <promeki/dir.h>
#include <promeki/enums.h>
#include <promeki/filepath.h>
#include <promeki/buffer.h>
#include <promeki/metadata.h>
#include <promeki/stringlist.h>
#include <promeki/timecode.h>

PROMEKI_NAMESPACE_BEGIN

// Registration is driven entirely off the @ref ImageFileIO registry:
// every ImageFileIO subclass that hits @c PROMEKI_REGISTER_IMAGEFILEIO
// triggers a matching @ref MediaIO::registerFormat call via
// @ref MediaIOTask_ImageFile::buildFormatDescFor below.  The two
// registries stay in lock-step without any hard-coded per-format
// table here — a new ImageFileIO backend picks up a MediaIO
// presence for free the moment it supplies @c _extensions and
// @c _canLoad / @c _canSave in its constructor.
//
// A legacy @c "ImageFile" umbrella alias is additionally registered
// for backwards compatibility with existing unit tests and external
// callers that refer to the backend by that single name.  See
// @ref registerImageFileUmbrella below.

// ============================================================================
// Frame-rate source tags (values for Metadata::FrameRateSource)
// ============================================================================
//
// The backend's defaultConfig() pre-populates MediaConfig::FrameRate, so the
// "user explicitly set it" distinction is gone by design — every open
// call sees a valid config-supplied frame rate.  There are now only
// two distinct origins worth tracking: the frame rate came from a
// sidecar or the upstream MediaDesc (authoritative), or it came from
// the MediaConfig (either the caller's override or the backend
// default — both behave identically downstream).
static const char *const kFrameRateSourceFile    = "file";
static const char *const kFrameRateSourceConfig  = "config";

// ============================================================================
// Extension-to-ImageFile::ID mapping
// ============================================================================
//
// The per-format extension list lives on each @ref ImageFileIO
// backend (populated by its constructor).  The helpers below walk
// the @ref ImageFileIO registry to resolve an extension to its
// backend ID — no hard-coded table — so a new backend only has to
// ship its @c _extensions in its constructor to be visible to both
// the MediaIO format registry and the sequence-open path here.

// The .imgseq sidecar is recognized as its own extension but does not
// map to any ImageFile::ID — the backend looks inside the sidecar to
// find the referenced image format.
static const char *const kImgSeqExtension = "imgseq";

static String extensionOf(const String &filename) {
        size_t dot = filename.rfind('.');
        if(dot == String::npos || dot + 1 >= filename.size()) return String();
        return filename.mid(dot + 1).toLower();
}

static int imageFileIDFromExtension(const String &filename) {
        String ext = extensionOf(filename);
        if(ext.isEmpty()) return ImageFile::Invalid;
        for(int id : ImageFileIO::registeredIDs()) {
                const ImageFileIO *io = ImageFileIO::lookup(id);
                if(io == nullptr || !io->isValid()) continue;
                for(const auto &e : io->extensions()) {
                        if(e == ext) return id;
                }
        }
        return ImageFile::Invalid;
}

static bool filenameHasMask(const String &name) {
        const int len = static_cast<int>(name.size());
        // Hash-style: any '#' character
        for(int i = 0; i < len; i++) {
                if(name[i] == '#') return true;
        }
        // Printf-style: %[0]?N?d
        for(int i = 0; i < len; i++) {
                if(name[i] != '%') continue;
                int j = i + 1;
                if(j < len && name[j] == '0') j++;
                while(j < len && name[j] >= '0' && name[j] <= '9') j++;
                if(j < len && name[j] == 'd') return true;
        }
        return false;
}

static bool filenameIsImgSeqSidecar(const String &filename) {
        return extensionOf(filename) == kImgSeqExtension;
}

// Strip trailing non-alphanumeric characters from a NumName prefix.
// Shared by the audio and imgseq sidecar naming helpers.
static String sidecarPrefix(const NumName &nn) {
        String px = nn.prefix();
        while(!px.isEmpty()) {
                char c = px[px.size() - 1];
                if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9')) {
                        break;
                }
                px = px.left(px.size() - 1);
        }
        return px;
}

// Derive a sidecar audio filename from a sequence pattern.
// Strip trailing non-alphanumeric characters from the prefix and
// append ".wav".  If the prefix is empty, fall back to "audio.wav".
static String sidecarAudioName(const NumName &nn) {
        String px = sidecarPrefix(nn);
        if(px.isEmpty()) return String("audio.wav");
        return px + ".wav";
}

// Derive a sidecar .imgseq filename from a sequence pattern.
// Uses the same prefix-stripping logic as the audio sidecar:
// "shot_####.dpx" → "shot.imgseq", "####.dpx" → "sequence.imgseq".
static String sidecarImgSeqName(const NumName &nn) {
        String px = sidecarPrefix(nn);
        if(px.isEmpty()) return String("sequence.imgseq");
        return px + ".imgseq";
}

// ============================================================================
// Magic number probing
// ============================================================================

static bool probeImageDevice(IODevice *device) {
        uint8_t buf[8] = {};
        int64_t n = device->read(buf, 8);
        if(n < 2) return false;

        uint32_t magic32 = (uint32_t(buf[0]) << 24) |
                           (uint32_t(buf[1]) << 16) |
                           (uint32_t(buf[2]) << 8)  |
                           uint32_t(buf[3]);

        // DPX: "SDPX" in either endian
        if(magic32 == 0x53445058 || magic32 == 0x58504453) return true;

        // Cineon: either endian
        if(magic32 == 0x802A5FD7 || magic32 == 0xD75F2A80) return true;

        // PNG: first 4 bytes of 8-byte signature
        if(magic32 == 0x89504E47) return true;

        // JPEG (JFIF / Exif / raw JPEG): SOI marker FF D8 followed by
        // any marker (FF xx).  Matches all conforming JPEG streams.
        if(buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF) return true;

        // JPEG XS (ISO/IEC 21122): SOC marker FF 10.
        if(buf[0] == 0xFF && buf[1] == 0x10) return true;

        // SGI: first 2 bytes = 0x01DA
        uint16_t magic16 = (uint16_t(buf[0]) << 8) | uint16_t(buf[1]);
        if(magic16 == 0x01DA) return true;

        // PNM: 'P' followed by '1'..'6'
        if(buf[0] == 'P' && buf[1] >= '1' && buf[1] <= '6') return true;

        // .imgseq sidecar: JSON object starting with '{', possibly after
        // leading whitespace.  We slurp the rest of the file and check
        // for the TypeTag marker.  This keeps the device-based probe in
        // sync with extension-based detection.
        int leading = 0;
        while(leading < n && (buf[leading] == ' ' || buf[leading] == '\t' ||
                              buf[leading] == '\r' || buf[leading] == '\n')) {
                leading++;
        }
        if(leading < n && buf[leading] == '{') {
                // Read the remainder of the file (capped) and probe.
                device->seek(0);
                const int64_t cap = 64 * 1024;
                Buffer body(cap);
                int64_t total = device->read(body.data(), cap);
                if(total > 0) {
                        String text(static_cast<const char *>(body.data()),
                                    static_cast<size_t>(total));
                        if(ImgSeq::isImgSeqJson(text)) return true;
                }
        }

        return false;
}

// ============================================================================
// FormatDesc
// ============================================================================

// Flattens every registered ImageFileIO backend's extension list
// into a single StringList plus the .imgseq sidecar extension.
// Used by the legacy @c "ImageFile" umbrella FormatDesc so it
// claims every extension any registered backend could handle.
static StringList buildExtensions() {
        StringList exts;
        for(int id : ImageFileIO::registeredIDs()) {
                const ImageFileIO *io = ImageFileIO::lookup(id);
                if(io == nullptr || !io->isValid()) continue;
                for(const auto &e : io->extensions()) {
                        bool dup = false;
                        for(const auto &seen : exts) {
                                if(seen == e) { dup = true; break; }
                        }
                        if(!dup) exts.pushToBack(e);
                }
        }
        exts.pushToBack(String(kImgSeqExtension));
        return exts;
}

// Shared config specs used by every per-format ImageFile backend.
// Every backend (ImgSeqDPX, ImgSeqPNG, ...) routes through the same
// @ref MediaIOTask_ImageFile implementation, which infers the
// concrete @ref ImageFile::ID from the caller-supplied filename at
// open time.  The config schema is therefore identical across all
// variants — only @ref FormatDesc::extensions, @ref FormatDesc::name,
// and the load / save role flags differ.
static MediaIO::Config::SpecMap imageFileConfigSpecs() {
        MediaIO::Config::SpecMap specs;
        auto s = [&specs](MediaConfig::ID id, const Variant &def) {
                const VariantSpec *gs = MediaConfig::spec(id);
                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
        };
        // 0 == ImageFile::Invalid — the Open handler
        // treats this as "infer the backend from the
        // filename extension or the content probe".
        s(MediaConfig::ImageFileID, int32_t(0));
        // Empty size hint: only used by headerless
        // formats (RawYUV) that can't derive the
        // geometry from the file itself.
        s(MediaConfig::VideoSize, Size2Du32());
        s(MediaConfig::VideoPixelFormat, PixelFormat());
        s(MediaConfig::FrameRate, MediaIOTask_ImageFile::DefaultFrameRate);
        s(MediaConfig::SequenceHead,
          int32_t(MediaIOTask_ImageFile::DefaultSequenceHead));
        s(MediaConfig::SaveImgSeqEnabled, true);
        s(MediaConfig::SaveImgSeqPath, String());
        s(MediaConfig::SaveImgSeqPathMode, ImgSeqPathMode::Relative);
        s(MediaConfig::SidecarAudioEnabled, true);
        s(MediaConfig::SidecarAudioPath, String());
        s(MediaConfig::AudioSource, AudioSourceHint::Sidecar);
        return specs;
}

// Honored-metadata schema.  Each ImageFile backend consumes a
// different subset (DPX honors the biggest set — file info + film
// info + TV info; PNG honors @c Gamma; Cineon honors most of the
// DPX film set minus project/copyright) but the union fits in one
// place and keeps the per-format descriptors short.
static Metadata imageFileDefaultMetadata() {
        Metadata m;
        // Common (write-to-DPX-header) fields.
        m.set(Metadata::FileOrigName, String());
        m.set(Metadata::Date,         String());
        m.set(Metadata::Software,     String());
        m.set(Metadata::Project,      String());
        m.set(Metadata::Copyright,    String());
        m.set(Metadata::Reel,         String());
        m.set(Metadata::Timecode,     Timecode());
        m.set(Metadata::Gamma,        double(0.0));
        m.set(Metadata::FrameRate,    double(0.0));
        // Film information block.
        m.set(Metadata::FilmMfgID,    String());
        m.set(Metadata::FilmType,     String());
        m.set(Metadata::FilmOffset,   String());
        m.set(Metadata::FilmPrefix,   String());
        m.set(Metadata::FilmCount,    String());
        m.set(Metadata::FilmFormat,   String());
        m.set(Metadata::FilmSeqPos,   int(0));
        m.set(Metadata::FilmSeqLen,   int(0));
        m.set(Metadata::FilmHoldCount,int(1));
        m.set(Metadata::FilmShutter,  double(0.0));
        m.set(Metadata::FilmFrameID,  String());
        m.set(Metadata::FilmSlate,    String());
        // TV / image element block.
        m.set(Metadata::FieldID,                int(0));
        m.set(Metadata::TransferCharacteristic, int(0));
        m.set(Metadata::Colorimetric,           int(0));
        m.set(Metadata::Orientation,            int(0));
        // Sidecar audio (BWF) metadata forwarded to
        // the sidecar audio file when present.
        m.set(Metadata::EnableBWF,              false);
        m.set(Metadata::Description,            String());
        m.set(Metadata::Originator,             String());
        m.set(Metadata::OriginatorReference,    String());
        m.set(Metadata::OriginationDateTime,    String());
        m.set(Metadata::CodingHistory,          String());
        m.set(Metadata::UMID,                   String());
        return m;
}

static MediaIO::FormatDesc buildFormatDesc(const String &name,
                                           const String &displayName,
                                           const String &description,
                                           StringList extensions,
                                           bool canBeSource,
                                           bool canBeSink) {
        MediaIO::FormatDesc fd;
        fd.name            = name;
        fd.displayName     = displayName;
        fd.description     = description;
        fd.extensions      = std::move(extensions);
        fd.canBeSource     = canBeSource;
        fd.canBeSink       = canBeSink;
        fd.canBeTransform  = false;
        fd.create          = []() -> MediaIOTask * { return new MediaIOTask_ImageFile(); };
        fd.configSpecs     = &imageFileConfigSpecs;
        fd.defaultMetadata = &imageFileDefaultMetadata;
        fd.canHandleDevice = &probeImageDevice;
        return fd;
}

MediaIO::FormatDesc MediaIOTask_ImageFile::buildFormatDescFor(const ImageFileIO *io) {
        if(io == nullptr || !io->isValid()) return MediaIO::FormatDesc();
        StringList exts = io->extensions();
        // The .imgseq sidecar is handled by this same task but is
        // not specific to any one backend — it gets its own
        // registration below (see @ref registerImageFileUmbrella),
        // so per-backend FormatDescs never claim it.  Per-format
        // entries use the backend's description as the display name —
        // it already reads as a short human-readable label
        // ("DPX (.dpx)" etc).
        return buildFormatDesc(io->mediaIoName(), io->description(),
                               io->description(),
                               std::move(exts),
                               /*canBeSource*/ io->canLoad(),
                               /*canBeSink*/   io->canSave());
}

MediaIO::FormatDesc MediaIOTask_ImageFile::formatDesc() {
        // Backwards-compatible accessor — returns the legacy umbrella
        // entry so callers that stored a @ref MediaIO::FormatDesc
        // before the per-format split keep seeing the same shape.
        // New code should prefer @ref MediaIO::registeredFormats and
        // filter on @c name.startsWith("ImgSeq").
        return buildFormatDesc(String("ImageFile"),
                String("Image File"),
                String("Single-image files and image sequences (DPX, Cineon, TGA, SGI, "
                       "PNM, PNG, JPEG, JPEG XS, RawYUV, .imgseq)"),
                buildExtensions(), true, true);
}

// ============================================================================
// Lifecycle
// ============================================================================

MediaIOTask_ImageFile::~MediaIOTask_ImageFile() = default;

Error MediaIOTask_ImageFile::executeCmd(MediaIOCommandOpen &cmd) {
        const MediaIO::Config &cfg = cmd.config;

        // Stash a copy of the open-time config so every read / write
        // command forwards the same hints to the resolved
        // @ref ImageFileIO backend.  Codec-specific knobs (JpegQuality,
        // JpegSubsampling, …) on @p cfg flow through unchanged.
        _ioConfig = cfg;
        _filename = cfg.getAs<String>(MediaConfig::Filename);
        _mode = cmd.mode;
        _sequenceMode = false;
        _saveImgSeqEnabled = cfg.getAs<bool>(MediaConfig::SaveImgSeqEnabled, true);
        _saveImgSeqPath = cfg.getAs<String>(MediaConfig::SaveImgSeqPath, String());
        _saveImgSeqPathMode = cfg.get(MediaConfig::SaveImgSeqPathMode)
                .asEnum(ImgSeqPathMode::Type, nullptr);
        _sidecarAudioEnabled = cfg.getAs<bool>(MediaConfig::SidecarAudioEnabled, true);

        // Resolve the reported frame rate with a documented priority:
        //   1. Writer with a valid pendingMediaDesc frame rate
        //   2. .imgseq sidecar frameRate (resolved below, reader only)
        //   3. Config value (MediaConfig::FrameRate — always present since the
        //      backend's defaultConfig pre-populates it)
        FrameRate fps = cfg.getAs<FrameRate>(MediaConfig::FrameRate, DefaultFrameRate);
        if(!fps.isValid()) fps = DefaultFrameRate;
        String frSource = kFrameRateSourceConfig;

        if(cmd.mode == MediaIO::Sink && cmd.pendingMediaDesc.frameRate().isValid()) {
                fps = cmd.pendingMediaDesc.frameRate();
                frSource = kFrameRateSourceFile;
        }

        MediaDesc mediaDesc;
        mediaDesc.setFrameRate(fps);

        // ---- Routing ----
        //
        // If the filename is a .imgseq sidecar we always take the
        // sequence path.  If the filename contains a mask placeholder
        // (# or %d) we also take the sequence path.  Anything else
        // stays on the single-file path, preserving backwards
        // compatibility with callers that pass a plain image path.
        const bool isSidecar = filenameIsImgSeqSidecar(_filename);
        const bool hasMask   = filenameHasMask(_filename);

        if(isSidecar || hasMask) {
                _sequenceMode = true;
                // If the caller set MediaConfig::FrameRate, keep frSource as
                // "config" for now — openSequence() may upgrade it to
                // "file" if the sidecar supplies a rate.  If nothing is
                // configured yet, leave it at "default".
                Error err = openSequence(cmd, mediaDesc, frSource);
                if(err.isError()) return err;
        } else {
                Error err = openSingle(cmd, mediaDesc, frSource);
                if(err.isError()) return err;
        }

        _writerFrameRate = mediaDesc.frameRate();
        cmd.mediaDesc = mediaDesc;
        cmd.frameRate = mediaDesc.frameRate();
        return Error::Ok;
}

// ----------------------------------------------------------------------------
// Single-file open
// ----------------------------------------------------------------------------

Error MediaIOTask_ImageFile::openSingle(MediaIOCommandOpen &cmd,
                                        MediaDesc &mediaDesc,
                                        const String &frSource) {
        const MediaIO::Config &cfg = cmd.config;

        // Determine the ImageFile format ID.  MediaConfig::ImageFileID
        // lives in the default config as 0 (ImageFile::Invalid) to
        // document the knob; any non-zero value from the caller is
        // taken as an explicit override, otherwise we fall back to
        // auto-detection from the filename extension.
        _imageFileID = cfg.getAs<int>(MediaConfig::ImageFileID, ImageFile::Invalid);
        if(_imageFileID == ImageFile::Invalid) {
                _imageFileID = imageFileIDFromExtension(_filename);
        }

        if(_imageFileID == ImageFile::Invalid) {
                promekiErr("MediaIOTask_ImageFile: cannot determine image format for '%s'",
                        _filename.cstr());
                return Error::NotSupported;
        }

        // Validate that the backend exists and supports the requested mode
        const ImageFileIO *io = ImageFileIO::lookup(_imageFileID);
        if(!io->isValid()) {
                promekiErr("MediaIOTask_ImageFile: no ImageFileIO backend for ID %d", _imageFileID);
                return Error::NotSupported;
        }
        if(cmd.mode == MediaIO::Source && !io->canLoad()) {
                promekiErr("MediaIOTask_ImageFile: backend '%s' does not support loading",
                        io->name().cstr());
                return Error::NotSupported;
        }
        if(cmd.mode == MediaIO::Sink && !io->canSave()) {
                promekiErr("MediaIOTask_ImageFile: backend '%s' does not support saving",
                        io->name().cstr());
                return Error::NotSupported;
        }

        if(cmd.mode == MediaIO::Source) {
                ImageFile imgFile(_imageFileID);
                imgFile.setFilename(_filename);

                // For headerless formats, set hint payload from config
                Size2Du32 hintSize = cfg.getAs<Size2Du32>(MediaConfig::VideoSize, Size2Du32());
                if(hintSize.width() > 0 && hintSize.height() > 0) {
                        PixelFormat pd = cfg.getAs<PixelFormat>(MediaConfig::VideoPixelFormat, PixelFormat());
                        if(pd.isValid()) {
                                ImageDesc idesc(hintSize, pd);
                                auto hint = UncompressedVideoPayload::Ptr::create(idesc);
                                Frame hintFrame;
                                hintFrame.addPayload(hint);
                                imgFile.setFrame(hintFrame);
                        }
                }

                Error err = imgFile.load(_ioConfig);
                if(err.isError()) {
                        promekiErr("MediaIOTask_ImageFile: load '%s' failed: %s",
                                _filename.cstr(), err.name().cstr());
                        return err;
                }

                _frame = Frame::Ptr::create(imgFile.frame());

                auto vids = _frame->videoPayloads();
                if(!vids.isEmpty() && vids[0].isValid()) {
                        const ImageDesc &idesc = vids[0]->desc();
                        mediaDesc.imageList().pushToBack(idesc);
                }

                // If the image backend loaded embedded audio (e.g.
                // DPX with an AUDIO user-data block), surface its
                // descriptor in the MediaDesc and on the command so
                // downstream consumers (SDL player, transcoders) know
                // to wire up an audio sink.
                auto auds = _frame->audioPayloads();
                if(!auds.isEmpty() && auds[0].isValid()) {
                        const AudioDesc &adesc = auds[0]->desc();
                        if(adesc.isValid()) {
                                mediaDesc.audioList().pushToBack(adesc);
                                cmd.audioDesc = adesc;
                        }
                }

                cmd.metadata = _frame->metadata();
                cmd.metadata.set(Metadata::FrameRateSource, frSource);
                cmd.frameCount = 1;
                _loaded = false;
        } else {
                // Writer: use the pending mediaDesc/metadata if provided.
                // If the caller didn't set a frame rate on it, keep the
                // one our caller already resolved from config/default.
                FrameRate savedFps = mediaDesc.frameRate();
                mediaDesc = cmd.pendingMediaDesc;
                if(!mediaDesc.frameRate().isValid()) {
                        mediaDesc.setFrameRate(savedFps);
                }
                cmd.metadata = cmd.pendingMetadata;
                cmd.metadata.set(Metadata::FrameRateSource, frSource);
                // Stash the container metadata so writeSingle() can
                // merge it (MediaIO write defaults, caller-supplied
                // tags, etc.) into each saved frame — each single-file
                // write produces a standalone file and needs its own
                // container metadata in the frame.
                _writeContainerMetadata = cmd.metadata;
                _writeCount = 0;
                cmd.frameCount = 0;
        }

        cmd.canSeek = false;
        cmd.defaultStep = 0;  // single image re-reads indefinitely
        return Error::Ok;
}

// ----------------------------------------------------------------------------
// Sequence open
// ----------------------------------------------------------------------------

Error MediaIOTask_ImageFile::openSequence(MediaIOCommandOpen &cmd,
                                          MediaDesc &mediaDesc,
                                          const String &frSourceIn) {
        const MediaIO::Config &cfg = cmd.config;
        String frSource = frSourceIn;

        // Figure out: the pattern, the directory it lives in, the head
        // and tail, and any optional sequence-wide metadata/hints.
        NumName pattern;
        FilePath dir;
        int64_t head = -1;
        int64_t tail = -1;
        Size2Du32 hintSize  = cfg.getAs<Size2Du32>(MediaConfig::VideoSize, Size2Du32());
        PixelFormat hintPixel = cfg.getAs<PixelFormat>(MediaConfig::VideoPixelFormat, PixelFormat());
        Metadata  sidecarMeta;
        FrameRate sidecarFps;
        String    sidecarAudioFile;         // From .imgseq "audioFile" field

        if(filenameIsImgSeqSidecar(_filename)) {
                // Load the sidecar: this gives us the pattern and
                // optional extras.  The pattern is resolved relative to
                // the sidecar's directory.
                Error sErr;
                ImgSeq seq = ImgSeq::load(FilePath(_filename), &sErr);
                if(sErr.isError() || !seq.isValid()) {
                        promekiErr("MediaIOTask_ImageFile: invalid .imgseq sidecar '%s'",
                                _filename.cstr());
                        return Error::Invalid;
                }
                pattern = seq.name();
                FilePath sidecarDir = FilePath(_filename).parent();
                if(sidecarDir.isEmpty()) sidecarDir = FilePath(".");
                if(!seq.dir().isEmpty()) {
                        if(seq.dir().isAbsolute()) {
                                dir = seq.dir();
                        } else {
                                dir = sidecarDir / seq.dir();
                        }
                } else {
                        dir = sidecarDir;
                }

                if(seq.head() != 0 || seq.tail() != 0) {
                        head = static_cast<int64_t>(seq.head());
                        tail = static_cast<int64_t>(seq.tail());
                }

                if(seq.frameRate().isValid()) {
                        sidecarFps = seq.frameRate();
                }
                if(seq.videoSize().width() > 0 && seq.videoSize().height() > 0) {
                        hintSize = seq.videoSize();
                }
                if(seq.pixelFormat().isValid()) {
                        hintPixel = seq.pixelFormat();
                }
                sidecarMeta = seq.metadata();
                sidecarAudioFile = seq.audioFile();
        } else {
                // Mask supplied directly (e.g. "seq_####.dpx").  The
                // parent directory is the directory portion of the
                // mask; the pattern is the filename portion.  If no
                // directory was given, scan the current working
                // directory.
                FilePath full(_filename);
                dir = full.parent();
                if(dir.isEmpty()) dir = FilePath(".");
                String maskName = full.fileName();
                pattern = NumName::fromMask(maskName);

                // Auto-discover a conventionally-named .imgseq sidecar
                // sitting next to the image files.  If found, pull out
                // the optional extras (head/tail, frame rate, size,
                // pixel desc, metadata, audio file) so the mask-based
                // open gets the same benefits as opening the sidecar
                // directly.
                if(_saveImgSeqEnabled && pattern.isValid()) {
                        FilePath imgseqPath = dir / sidecarImgSeqName(pattern);
                        if(imgseqPath.exists()) {
                                Error sErr;
                                ImgSeq seq = ImgSeq::load(imgseqPath, &sErr);
                                if(sErr.isOk() && seq.isValid()) {
                                        if(seq.head() != 0 || seq.tail() != 0) {
                                                head = static_cast<int64_t>(seq.head());
                                                tail = static_cast<int64_t>(seq.tail());
                                        }
                                        if(seq.frameRate().isValid()) {
                                                sidecarFps = seq.frameRate();
                                        }
                                        if(seq.videoSize().width() > 0 &&
                                           seq.videoSize().height() > 0) {
                                                hintSize = seq.videoSize();
                                        }
                                        if(seq.pixelFormat().isValid()) {
                                                hintPixel = seq.pixelFormat();
                                        }
                                        sidecarMeta = seq.metadata();
                                        sidecarAudioFile = seq.audioFile();
                                }
                        }
                }
        }

        if(!pattern.isValid()) {
                promekiErr("MediaIOTask_ImageFile: cannot parse sequence mask from '%s'",
                        _filename.cstr());
                return Error::Invalid;
        }

        _seqName      = pattern;
        _seqDir       = dir;
        _seqMetadata  = sidecarMeta;
        _seqSize      = hintSize;
        _seqPixelFormat = hintPixel;

        // Determine the per-file ImageFile::ID from the pattern
        // suffix; MediaConfig::ImageFileID = 0 (ImageFile::Invalid) in the
        // default config triggers auto-detection, a non-zero caller
        // override wins.
        _imageFileID = cfg.getAs<int>(MediaConfig::ImageFileID, ImageFile::Invalid);
        if(_imageFileID == ImageFile::Invalid) {
                // NumName's suffix starts with '.' (e.g. ".dpx"), so
                // imageFileIDFromExtension() works directly.
                _imageFileID = imageFileIDFromExtension(pattern.suffix());
        }
        if(_imageFileID == ImageFile::Invalid) {
                promekiErr("MediaIOTask_ImageFile: cannot determine image format for pattern '%s'",
                        pattern.hashmask().cstr());
                return Error::NotSupported;
        }

        const ImageFileIO *io = ImageFileIO::lookup(_imageFileID);
        if(!io->isValid()) {
                promekiErr("MediaIOTask_ImageFile: no ImageFileIO backend for ID %d", _imageFileID);
                return Error::NotSupported;
        }
        if(cmd.mode == MediaIO::Source && !io->canLoad()) {
                promekiErr("MediaIOTask_ImageFile: backend '%s' does not support loading",
                        io->name().cstr());
                return Error::NotSupported;
        }
        if(cmd.mode == MediaIO::Sink && !io->canSave()) {
                promekiErr("MediaIOTask_ImageFile: backend '%s' does not support saving",
                        io->name().cstr());
                return Error::NotSupported;
        }

        // If the sidecar provided a frame rate, it takes precedence over
        // both the config override and the default — but a caller-supplied
        // writer MediaDesc still wins (frSource stays "file" in that case).
        if(frSource != kFrameRateSourceFile && sidecarFps.isValid()) {
                mediaDesc.setFrameRate(sidecarFps);
                frSource = kFrameRateSourceFile;
        }

        if(cmd.mode == MediaIO::Source) {
                // Detect head/tail from disk if we don't have them yet.
                if(head < 0 || tail < 0 || tail < head) {
                        bool haveAny = false;
                        int64_t detHead = 0;
                        int64_t detTail = 0;
                        Dir d(dir);
                        for(const FilePath &entry : d.entryList()) {
                                int val = -1;
                                NumName parsed = NumName::parse(entry.fileName(), &val);
                                if(!parsed.isValid()) continue;
                                if(!pattern.isInSequence(parsed)) continue;
                                if(val < 0) continue;
                                if(!haveAny) {
                                        detHead = val;
                                        detTail = val;
                                        haveAny = true;
                                } else {
                                        if(val < detHead) detHead = val;
                                        if(val > detTail) detTail = val;
                                }
                        }
                        if(!haveAny) {
                                promekiErr("MediaIOTask_ImageFile: no files found matching '%s' in '%s'",
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

                // Load the first frame to establish the MediaDesc.
                String firstPath = (dir / pattern.name(static_cast<int>(head))).toString();
                ImageFile imgFile(_imageFileID);
                imgFile.setFilename(firstPath);
                if(_seqSize.width() > 0 && _seqSize.height() > 0 && _seqPixelFormat.isValid()) {
                        ImageDesc idesc(_seqSize, _seqPixelFormat);
                        auto hint = UncompressedVideoPayload::Ptr::create(idesc);
                        Frame hintFrame;
                        hintFrame.addPayload(hint);
                        imgFile.setFrame(hintFrame);
                }
                Error err = imgFile.load(_ioConfig);
                if(err.isError()) {
                        promekiErr("MediaIOTask_ImageFile: failed to load head frame '%s': %s",
                                firstPath.cstr(), err.name().cstr());
                        return err;
                }
                const Frame &f = imgFile.frame();
                auto headVids = f.videoPayloads();
                if(!headVids.isEmpty() && headVids[0].isValid()) {
                        mediaDesc.imageList().pushToBack(headVids[0]->desc());
                }

                // Surface embedded audio in the descriptor.  A DPX
                // sequence written from a source with audio carries
                // per-frame AUDIO user-data blocks; without this the
                // controller would report "no audio" and downstream
                // consumers would never wire up an audio sink.
                auto headAuds = f.audioPayloads();
                if(!headAuds.isEmpty() && headAuds[0].isValid()) {
                        const AudioDesc &adesc = headAuds[0]->desc();
                        if(adesc.isValid()) {
                                mediaDesc.audioList().pushToBack(adesc);
                                cmd.audioDesc = adesc;
                        }
                }

                Metadata meta = f.metadata();
                // Layer sidecar metadata on top of per-frame metadata so
                // the sidecar acts as the sequence's authoritative source
                // of descriptive fields (title, project, etc.).
                meta.merge(_seqMetadata);
                meta.set(Metadata::FrameRateSource, frSource);
                cmd.metadata = meta;
                cmd.frameCount = _seqTail - _seqHead + 1;

                // --- Audio source selection (reader) ---
                //
                // Two audio sources may be available:
                //   1. Embedded per-frame audio (e.g. DPX user-data
                //      blocks) — already surfaced in mediaDesc above.
                //   2. Sidecar audio file (Broadcast WAV).
                //
                // AudioSourceHint selects which is preferred; if the
                // preferred source is unavailable, fall back to the
                // other.  SidecarAudioEnabled=false skips the sidecar
                // probe entirely.
                bool hasEmbeddedAudio = !mediaDesc.audioList().isEmpty();
                bool hasSidecarAudio = false;

                if(_sidecarAudioEnabled) {
                        // Resolve the sidecar path.  Priority: .imgseq
                        // audioFile field, config override, auto-derived.
                        String audioPath;
                        if(!sidecarAudioFile.isEmpty()) {
                                FilePath ap(sidecarAudioFile);
                                if(ap.isAbsolute()) {
                                        audioPath = sidecarAudioFile;
                                } else {
                                        audioPath = (dir / sidecarAudioFile).toString();
                                }
                        } else {
                                String cfgPath = cfg.getAs<String>(MediaConfig::SidecarAudioPath, String());
                                if(!cfgPath.isEmpty()) {
                                        FilePath ap(cfgPath);
                                        if(ap.isAbsolute()) {
                                                audioPath = cfgPath;
                                        } else {
                                                audioPath = (dir / cfgPath).toString();
                                        }
                                } else {
                                        audioPath = (dir / sidecarAudioName(pattern)).toString();
                                }
                        }

                        if(FilePath(audioPath).exists()) {
                                _sidecarAudio = AudioFile::createReader(audioPath);
                                if(!_sidecarAudio.isValid()) {
                                        promekiErr("MediaIOTask_ImageFile: failed to create sidecar audio reader for '%s'",
                                                audioPath.cstr());
                                        return Error::NotSupported;
                                }
                                Error audioErr = _sidecarAudio.open();
                                if(audioErr.isError()) {
                                        promekiErr("MediaIOTask_ImageFile: sidecar audio open '%s' failed: %s",
                                                audioPath.cstr(), audioErr.name().cstr());
                                        return audioErr;
                                }
                                _sidecarAudioDesc = _sidecarAudio.desc();
                                _sidecarAudioPath = audioPath;
                                _sidecarFrameRate = mediaDesc.frameRate();
                                _sidecarSampleRate = static_cast<int64_t>(
                                        _sidecarAudioDesc.sampleRate());
                                hasSidecarAudio = true;
                        }
                }

                // Decide which source to activate based on the hint.
                Enum sourceHint = cfg.get(MediaConfig::AudioSource)
                        .asEnum(AudioSourceHint::Type, nullptr);
                bool preferSidecar = !sourceHint.isValid() ||
                        sourceHint == AudioSourceHint::Sidecar;

                bool useSidecar;
                if(preferSidecar) {
                        useSidecar = hasSidecarAudio ? true : false;
                } else {
                        // Prefer embedded; fall back to sidecar.
                        useSidecar = hasEmbeddedAudio ? false : hasSidecarAudio;
                }

                if(useSidecar) {
                        _sidecarAudioOpen = true;
                        mediaDesc.audioList().clear();
                        mediaDesc.audioList().pushToBack(_sidecarAudioDesc);
                        cmd.audioDesc = _sidecarAudioDesc;
                } else if(hasSidecarAudio) {
                        // We opened the sidecar but won't use it — close.
                        _sidecarAudio.close();
                        _sidecarAudio = AudioFile();
                        _sidecarAudioDesc = AudioDesc();
                        _sidecarAudioPath = String();
                        _sidecarFrameRate = FrameRate();
                        _sidecarSampleRate = 0;
                }

        } else {
                // Ensure the output directory exists, creating any
                // missing parents along the way.
                Dir outDir(dir);
                if(!outDir.exists()) {
                        Error mkErr = outDir.mkpath();
                        if(mkErr.isError()) {
                                promekiErr("MediaIOTask_ImageFile: failed to create output directory '%s': %s",
                                        dir.toString().cstr(), mkErr.name().cstr());
                                return mkErr;
                        }
                }

                // Writer: start at either the configured head or the
                // default.  The writer hasn't written anything yet, so
                // frameCount is 0 and will be updated after each write.
                int64_t writeHead = cfg.getAs<int>(MediaConfig::SequenceHead, DefaultSequenceHead);
                if(head >= 0) writeHead = head;  // sidecar had explicit head
                _seqHead = writeHead;
                _seqTail = (tail >= 0) ? tail : _seqHead;  // tail grows on write if unset
                _seqIndex = 0;
                _seqAtEnd = false;
                _writeCount = 0;

                // Stash the video descriptor for the .imgseq sidecar.
                if(!cmd.pendingMediaDesc.imageList().isEmpty()) {
                        const ImageDesc &id = cmd.pendingMediaDesc.imageList()[0];
                        _seqSize = id.size();
                        _seqPixelFormat = id.pixelFormat();
                }

                Metadata meta = cmd.pendingMetadata;
                meta.merge(_seqMetadata);
                meta.set(Metadata::FrameRateSource, frSource);
                cmd.metadata = meta;
                // Stash the container metadata so writeSequence() can
                // apply it to every image in the sequence — each file
                // is a standalone image and needs its own copy of
                // Date, Software, Originator, UMID, etc.
                _writeContainerMetadata = meta;
                cmd.frameCount = 0;

                // --- Auto-derive .imgseq sidecar path (writer) ---
                if(_saveImgSeqEnabled && _saveImgSeqPath.isEmpty()) {
                        _saveImgSeqPath = (dir / sidecarImgSeqName(pattern)).toString();
                }

                // --- Sidecar audio setup (writer) ---
                if(_sidecarAudioEnabled) {
                        // Resolve AudioDesc from pending inputs.
                        AudioDesc audioDesc;
                        if(cmd.pendingAudioDesc.isValid()) {
                                audioDesc = cmd.pendingAudioDesc;
                        } else if(!cmd.pendingMediaDesc.audioList().isEmpty()) {
                                audioDesc = cmd.pendingMediaDesc.audioList()[0];
                        }

                        if(audioDesc.isValid()) {
                                // Derive sidecar audio path.
                                String cfgPath = cfg.getAs<String>(MediaConfig::SidecarAudioPath, String());
                                String audioPath;
                                String audioName;
                                if(!cfgPath.isEmpty()) {
                                        FilePath ap(cfgPath);
                                        if(ap.isAbsolute()) {
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

                                // Build audio metadata: start from the
                                // container metadata (which already has
                                // write defaults applied), enable BWF,
                                // and set the FrameRate for time_reference
                                // computation.
                                Metadata audioMeta = _writeContainerMetadata;
                                audioMeta.set(Metadata::EnableBWF, true);
                                audioMeta.set(Metadata::FrameRate,
                                        _writerFrameRate.toDouble());
                                // AudioDesc-level values win.
                                audioMeta.merge(audioDesc.metadata());
                                audioDesc.metadata() = std::move(audioMeta);

                                _sidecarAudio = AudioFile::createWriter(audioPath);
                                if(!_sidecarAudio.isValid()) {
                                        promekiErr("MediaIOTask_ImageFile: failed to create sidecar audio writer for '%s'",
                                                audioPath.cstr());
                                        return Error::NotSupported;
                                }
                                _sidecarAudio.setDesc(audioDesc);
                                Error audioErr = _sidecarAudio.open();
                                if(audioErr.isError()) {
                                        promekiErr("MediaIOTask_ImageFile: sidecar audio open '%s' for write failed: %s",
                                                audioPath.cstr(), audioErr.name().cstr());
                                        return audioErr;
                                }
                                _sidecarAudioDesc = audioDesc;
                                _sidecarAudioPath = audioPath;
                                _sidecarAudioName = audioName;
                                _sidecarAudioOpen = true;
                                _sidecarFrameRate = _writerFrameRate;
                                _sidecarSampleRate = static_cast<int64_t>(
                                        _sidecarAudioDesc.sampleRate());

                                mediaDesc.audioList().pushToBack(_sidecarAudioDesc);
                                cmd.audioDesc = _sidecarAudioDesc;
                        }
                }
        }

        cmd.canSeek = (cmd.mode == MediaIO::Source);
        cmd.defaultStep = 1;  // sequences advance one frame at a time
        return Error::Ok;
}

// ----------------------------------------------------------------------------
// Close
// ----------------------------------------------------------------------------

Error MediaIOTask_ImageFile::writeImgSeqSidecar() {
        if(!_saveImgSeqEnabled) return Error::Ok;
        if(_saveImgSeqPath.isEmpty()) return Error::Ok;
        if(_writeCount <= 0) {
                promekiInfo("MediaIOTask_ImageFile: skipping .imgseq sidecar — no frames written");
                return Error::Ok;
        }
        ImgSeq seq;
        seq.setName(_seqName);
        seq.setHead(static_cast<size_t>(_seqHead.value()));
        seq.setTail(static_cast<size_t>(_seqHead.value() + _writeCount.value() - 1));
        if(_writerFrameRate.isValid()) {
                seq.setFrameRate(_writerFrameRate);
        }
        if(_seqSize.width() > 0 && _seqSize.height() > 0) {
                seq.setVideoSize(_seqSize);
        }
        if(_seqPixelFormat.isValid()) {
                seq.setPixelFormat(_seqPixelFormat);
        }
        if(!_sidecarAudioName.isEmpty()) {
                seq.setAudioFile(_sidecarAudioName);
        }

        // If SaveImgSeqPath is relative, resolve it against the image
        // directory so the sidecar lands alongside the frames by default.
        FilePath sidecarPath(_saveImgSeqPath);
        if(sidecarPath.isRelative()) {
                FilePath imageDir = _seqDir;
                if(imageDir.isEmpty()) imageDir = FilePath(".");
                sidecarPath = imageDir / sidecarPath;
        }

        // Compute the dir field: a path from the sidecar location to
        // the image directory.  The sidecar's "name" is always a bare
        // filename pattern; "dir" tells the reader where to find the
        // files relative to the sidecar (or as an absolute path).
        FilePath sidecarDir = sidecarPath.parent();
        if(sidecarDir.isEmpty()) sidecarDir = FilePath(".");
        FilePath imageDir = _seqDir;
        if(imageDir.isEmpty()) imageDir = FilePath(".");

        bool absolute = _saveImgSeqPathMode.isValid() &&
                        _saveImgSeqPathMode == ImgSeqPathMode::Absolute;
        if(absolute) {
                seq.setDir(imageDir.absolutePath());
        } else {
                FilePath rel = imageDir.absolutePath().relativeTo(sidecarDir.absolutePath());
                // Only emit dir when it differs from the sidecar location.
                if(rel.toString() != ".") {
                        seq.setDir(rel);
                }
        }

        Error err = seq.save(sidecarPath);
        if(err.isError()) {
                promekiErr("MediaIOTask_ImageFile: failed to write .imgseq sidecar '%s': %s",
                        sidecarPath.toString().cstr(), err.name().cstr());
        } else {
                promekiInfo("MediaIOTask_ImageFile: wrote .imgseq sidecar '%s'",
                        sidecarPath.toString().cstr());
        }
        return err;
}

Error MediaIOTask_ImageFile::executeCmd(MediaIOCommandClose &cmd) {
        // Write the .imgseq sidecar before resetting state (writer + sequence only).
        if(_mode == MediaIO::Sink && _sequenceMode) {
                writeImgSeqSidecar();
        }

        // Close the sidecar audio file before resetting state.
        if(_sidecarAudioOpen) {
                _sidecarAudio.close();
                _sidecarAudioOpen = false;
        }

        _frame = {};
        _filename = String();
        _imageFileID = ImageFile::Invalid;
        _mode = MediaIO_NotOpen;
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

Error MediaIOTask_ImageFile::executeCmd(MediaIOCommandRead &cmd) {
        stampWorkBegin();
        Error err = _sequenceMode ? readSequence(cmd) : readSingle(cmd);
        stampWorkEnd();
        return err;
}

Error MediaIOTask_ImageFile::readSingle(MediaIOCommandRead &cmd) {
        // ImageFile is single-frame.  With step 0, deliver indefinitely.
        // With step != 0, deliver once then EOF.
        if(_loaded && cmd.step != 0) return Error::EndOfFile;
        cmd.frame = _frame;
        _loaded = true;
        ++_readCount;
        cmd.currentFrame = toFrameNumber(_readCount);
        return Error::Ok;
}

Error MediaIOTask_ImageFile::readSequence(MediaIOCommandRead &cmd) {
        if(_seqAtEnd) return Error::EndOfFile;

        const int64_t length = _seqTail.value() - _seqHead.value() + 1;
        if(length <= 0) return Error::EndOfFile;

        // Clamp the current index — it may have been set out of range
        // by seek.  Bounds check before we load.
        if(!_seqIndex.isValid() || _seqIndex.value() >= length) {
                _seqAtEnd = true;
                return Error::EndOfFile;
        }

        int64_t frameNum = _seqHead.value() + _seqIndex.value();
        String fn = (_seqDir / _seqName.name(static_cast<int>(frameNum))).toString();

        ImageFile imgFile(_imageFileID);
        imgFile.setFilename(fn);
        if(_seqSize.width() > 0 && _seqSize.height() > 0 && _seqPixelFormat.isValid()) {
                ImageDesc idesc(_seqSize, _seqPixelFormat);
                auto hint = UncompressedVideoPayload::Ptr::create(idesc);
                Frame hintFrame;
                hintFrame.addPayload(hint);
                imgFile.setFrame(hintFrame);
        }
        Error err = imgFile.load(_ioConfig);
        if(err.isError()) {
                promekiErr("MediaIOTask_ImageFile: failed to load sequence frame '%s': %s",
                        fn.cstr(), err.name().cstr());
                return err;
        }

        Frame::Ptr frame = Frame::Ptr::create(imgFile.frame());
        // Merge sequence-level metadata onto this frame.
        Metadata &fm = frame.modify()->metadata();
        fm.merge(_seqMetadata);
        fm.set(Metadata::FrameNumber, FrameNumber(frameNum));

        // Read sidecar audio for this frame.
        if(_sidecarAudioOpen) {
                size_t spf = _sidecarFrameRate.samplesPerFrame(
                        _sidecarSampleRate, _seqIndex.value());
                PcmAudioPayload::Ptr sidecarPayload;
                Error audioErr = _sidecarAudio.read(sidecarPayload, spf);
                if(audioErr.isError()) {
                        promekiErr("MediaIOTask_ImageFile: sidecar audio read failed: %s",
                                audioErr.name().cstr());
                        return audioErr;
                }
                // Sidecar audio replaces any embedded per-frame
                // audio.  Drop existing audio payloads (keep video)
                // and append the sidecar payload directly.
                Frame *fmut = frame.modify();
                MediaPayload::PtrList keep;
                keep.reserve(fmut->payloadList().size());
                for(MediaPayload::Ptr &p : fmut->payloadList()) {
                        if(!p.isValid()) { keep.pushToBack(p); continue; }
                        if(p->kind() != MediaPayloadKind::Audio) keep.pushToBack(p);
                }
                fmut->payloadList() = std::move(keep);
                if(sidecarPayload.isValid()) fmut->addPayload(sidecarPayload);
        }

        cmd.frame = frame;
        cmd.currentFrame = _seqIndex + int64_t(1);

        // Advance for the next read.  step==0 holds position, so we
        // intentionally don't latch EOF in that case.
        int step = cmd.step;
        if(step == 0) {
                // Stay on the same frame — no state change.
        } else {
                _seqIndex += int64_t(step);
                if(!_seqIndex.isValid() || _seqIndex.value() >= length) {
                        _seqAtEnd = true;
                }
        }
        return Error::Ok;
}

Error MediaIOTask_ImageFile::executeCmd(MediaIOCommandWrite &cmd) {
        stampWorkBegin();
        Error err = _sequenceMode ? writeSequence(cmd) : writeSingle(cmd);
        stampWorkEnd();
        return err;
}

Error MediaIOTask_ImageFile::writeSingle(MediaIOCommandWrite &cmd) {
        // Copy the frame so we can merge container metadata into its
        // Metadata without disturbing the caller's original.
        Frame frame = *cmd.frame;
        Metadata merged = _writeContainerMetadata;
        merged.merge(frame.metadata());   // Frame-level values win.
        frame.metadata() = std::move(merged);

        ImageFile imgFile(_imageFileID);
        imgFile.setFilename(_filename);
        imgFile.setFrame(frame);
        Error err = imgFile.save(_ioConfig);
        if(err.isError()) {
                promekiErr("MediaIOTask_ImageFile: save '%s' failed: %s",
                        _filename.cstr(), err.name().cstr());
                return err;
        }
        ++_writeCount;
        cmd.currentFrame = toFrameNumber(_writeCount);
        cmd.frameCount = _writeCount;
        return Error::Ok;
}

Error MediaIOTask_ImageFile::writeSequence(MediaIOCommandWrite &cmd) {
        int64_t frameNum = _seqHead.value() + _writeCount.value();
        String fn = (_seqDir / _seqName.name(static_cast<int>(frameNum))).toString();

        // Copy the frame so we can merge container metadata into its
        // Metadata without disturbing the caller's original.  Each
        // image in a sequence is a standalone file and needs its own
        // copy of the container-level defaults (Date, Software,
        // Originator, UMID, ...).
        Frame frame = *cmd.frame;
        Metadata merged = _writeContainerMetadata;
        merged.merge(frame.metadata());   // Frame-level values win.
        frame.metadata() = std::move(merged);

        ImageFile imgFile(_imageFileID);
        imgFile.setFilename(fn);
        imgFile.setFrame(frame);
        Error err = imgFile.save(_ioConfig);
        if(err.isError()) {
                promekiErr("MediaIOTask_ImageFile: save sequence frame '%s' failed: %s",
                        fn.cstr(), err.name().cstr());
                return err;
        }

        // Write audio to the sidecar file.
        if(_sidecarAudioOpen) {
                auto auds = cmd.frame->audioPayloads();
                const PcmAudioPayload *uap = nullptr;
                if(!auds.isEmpty() && auds[0].isValid()) {
                        uap = auds[0]->as<PcmAudioPayload>();
                }
                if(uap != nullptr) {
                        Error audioErr = _sidecarAudio.write(*uap);
                        if(audioErr.isError()) {
                                promekiErr("MediaIOTask_ImageFile: sidecar audio write failed: %s",
                                        audioErr.name().cstr());
                                return audioErr;
                        }
                } else {
                        // No audio on this frame — write silence to
                        // maintain frame-accurate sync.
                        size_t spf = _sidecarFrameRate.samplesPerFrame(
                                _sidecarSampleRate, _writeCount.value());
                        const size_t bytes = _sidecarAudioDesc.bufferSize(spf);
                        auto buf = Buffer::Ptr::create(bytes);
                        buf.modify()->setSize(bytes);
                        std::memset(buf.modify()->data(), 0, bytes);
                        BufferView planes;
                        planes.pushToBack(buf, 0, bytes);
                        auto silence = PcmAudioPayload::Ptr::create(
                                _sidecarAudioDesc, spf, planes);
                        Error audioErr = _sidecarAudio.write(*silence);
                        if(audioErr.isError()) {
                                promekiErr("MediaIOTask_ImageFile: sidecar audio silence write failed: %s",
                                        audioErr.name().cstr());
                                return audioErr;
                        }
                }
        }

        ++_writeCount;
        if(frameNum > _seqTail.value()) _seqTail = FrameNumber(frameNum);
        cmd.currentFrame = toFrameNumber(_writeCount);
        cmd.frameCount = _writeCount;
        return Error::Ok;
}

Error MediaIOTask_ImageFile::executeCmd(MediaIOCommandSeek &cmd) {
        if(!_sequenceMode || _mode != MediaIO::Source) {
                return Error::IllegalSeek;
        }

        const int64_t length = _seqTail.value() - _seqHead.value() + 1;
        if(length <= 0) return Error::IllegalSeek;

        int64_t target = cmd.frameNumber.isValid() ? cmd.frameNumber.value() : 0;
        if(target < 0) target = 0;
        if(target >= length) target = length - 1;
        _seqIndex = FrameNumber(target);
        _seqAtEnd = false;

        // Seek the sidecar audio to the corresponding sample position.
        if(_sidecarAudioOpen) {
                size_t targetSample = static_cast<size_t>(
                        _sidecarFrameRate.cumulativeTicks(_sidecarSampleRate, target));
                Error audioErr = _sidecarAudio.seekToSample(targetSample);
                if(audioErr.isError()) {
                        promekiErr("MediaIOTask_ImageFile: sidecar audio seek to sample %zu failed: %s",
                                targetSample, audioErr.name().cstr());
                        return audioErr;
                }
        }

        cmd.currentFrame = _seqIndex;
        return Error::Ok;
}

// ---- Phase 3 introspection / negotiation overrides ----

namespace {

// Returns the lowercase extension after the last '.' in @p path
// (without the dot), or an empty String when there is no extension.
String extractExt(const String &path) {
        const size_t dot = path.rfind('.');
        if(dot == String::npos || dot + 1 >= path.size()) return String();
        return path.mid(dot + 1).toLower();
}

// Returns component[0] bits for an uncompressed PixelFormat, or 0 for
// compressed / invalid PixelFormats.
int componentBits(const PixelFormat &pd) {
        if(!pd.isValid() || pd.isCompressed()) return 0;
        if(pd.memLayout().compCount() == 0) return 0;
        return static_cast<int>(pd.memLayout().compDesc(0).bits);
}

// True when the source's ColorModel family is YCbCr (luma + chroma-
// difference) — used to pick a YUV-family writer target so the
// inserted CSC stays inside the matching colour space when possible.
// PixelMemLayout::sampling() alone can't answer this: RGB formats are
// also Sampling444, so we have to look at the ColorModel::type
// instead.
bool isYuvSource(const PixelFormat &pd) {
        if(!pd.isValid()) return false;
        return pd.colorModel().type() == ColorModel::TypeYCbCr;
}

} // namespace

PixelFormat MediaIOTask_ImageFile::preferredWriterPixelFormat(
        const String &filename, const PixelFormat &source) const {
        const String ext = extractExt(filename);
        if(ext.isEmpty()) return PixelFormat();

        const int srcBits = componentBits(source);

        // ---- DPX / CIN ----
        // DPX is the workhorse VFX-grade still — accept any RGB(A)
        // bit depth from 8 to 16; default to 10-bit DPX since that
        // is the overwhelmingly common production form.  Match the
        // source bit depth where possible so the planner doesn't
        // drop precision unnecessarily.  The writer only emits BE
        // 16-bit today, so we advertise that variant; 12-bit has no
        // writer support yet and falls through to 10-bit DPX.
        if(ext == "dpx" || ext == "cin") {
                if(srcBits >= 16) return PixelFormat(PixelFormat::RGB16_BE_sRGB);
                if(srcBits >= 10) return PixelFormat(PixelFormat::RGB10_DPX_sRGB);
                if(srcBits >= 8)  return PixelFormat(PixelFormat::RGBA8_sRGB);
                return PixelFormat(PixelFormat::RGB10_DPX_sRGB);
        }

        // ---- JPEG / JPG ----
        // Baseline JPEG is 8-bit YUV (or RGB).  Pick the family
        // closest to the source so the inserted CSC stays cheap.
        if(ext == "jpg" || ext == "jpeg" || ext == "jfif") {
                return isYuvSource(source)
                        ? PixelFormat(PixelFormat::YUV8_422_Planar_Rec709)
                        : PixelFormat(PixelFormat::RGBA8_sRGB);
        }

        // ---- PNG ----
        // libspng round-trips 8-bit and 16-bit RGBA.
        if(ext == "png") {
                if(srcBits >= 16) return PixelFormat(PixelFormat::RGBA16_LE_sRGB);
                return PixelFormat(PixelFormat::RGBA8_sRGB);
        }

        // ---- TGA ----
        // 8-bit RGB(A) only.
        if(ext == "tga") return PixelFormat(PixelFormat::RGBA8_sRGB);

        // ---- SGI ----
        // 8 or 16-bit RGB(A).  SGI stores 16-bit channels big-endian on
        // disk, so we ask the pipeline for a BE-ordered input and let
        // the writer copy the bytes through unchanged.
        if(ext == "sgi" || ext == "rgb" || ext == "rgba" || ext == "bw") {
                if(srcBits >= 16) return PixelFormat(PixelFormat::RGBA16_BE_sRGB);
                return PixelFormat(PixelFormat::RGBA8_sRGB);
        }

        // ---- PNM family ----
        // 8 or 16-bit RGB.  PNM 16-bit is big-endian on disk per the
        // Netpbm specification, matching what our writer produces.
        if(ext == "pnm" || ext == "ppm" || ext == "pgm" || ext == "pbm") {
                if(srcBits >= 16) return PixelFormat(PixelFormat::RGB16_BE_sRGB);
                return PixelFormat(PixelFormat::RGB8_sRGB);
        }

        // For unknown extensions return invalid — the proposeInput
        // override then accepts whatever was offered.
        return PixelFormat();
}

Error MediaIOTask_ImageFile::proposeInput(const MediaDesc &offered,
                                          MediaDesc *preferred) const {
        if(preferred == nullptr) return Error::Invalid;
        if(offered.imageList().isEmpty()) {
                *preferred = offered;
                return Error::Ok;
        }

        const MediaIO *io = mediaIo();
        const MediaIO::Config &cfg = (io != nullptr) ? io->config()
                                                     : MediaIO::Config();
        const String filename = cfg.contains(MediaConfig::Filename)
                ? cfg.getAs<String>(MediaConfig::Filename) : String();

        const PixelFormat &offeredPd = offered.imageList()[0].pixelFormat();
        const PixelFormat target = preferredWriterPixelFormat(filename, offeredPd);

        if(!target.isValid() || target == offeredPd) {
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

namespace {

// Static initialiser that registers the legacy @c "ImageFile"
// umbrella entry and the @c "ImgSeq" sidecar entry.  Per-backend
// @c "ImgSeqXxx" entries are registered from
// @ref ImageFileIO::registerImageFileIO (see imagefileio.cpp) —
// that co-registration keeps the MediaIO and ImageFileIO registries
// in lockstep without cross-TU static-init ordering hazards.
int registerImageFileUmbrella() {
        // Legacy umbrella — matches every extension any registered
        // ImageFileIO could claim.  Retained for backwards
        // compatibility; new code should walk @ref MediaIO::registeredFormats
        // and pick up the per-backend entries.
        MediaIO::registerFormat(buildFormatDesc(String("ImageFile"),
                String("Image File"),
                String("Single-image files and image sequences (DPX, Cineon, TGA, SGI, "
                       "PNM, PNG, JPEG, JPEG XS, RawYUV, .imgseq)"),
                buildExtensions(), true, true));

        // .imgseq sidecar — dispatches to whichever image format the
        // JSON body references.  Advertised as source + sink so both
        // reading and writing a sidecar-backed sequence works.
        StringList seqExts;
        seqExts.pushToBack(String(kImgSeqExtension));
        MediaIO::registerFormat(buildFormatDesc(String("ImgSeq"),
                String("Image Sequence (.imgseq)"),
                String("ImgSeq JSON sidecar (points at an underlying image format)"),
                std::move(seqExts), true, true));
        return 0;
}

[[maybe_unused]] static int __promeki_imagefile_umbrella_registered =
        registerImageFileUmbrella();

} // namespace

PROMEKI_NAMESPACE_END
