/**
 * @file      mediaiotask_imagefile.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <promeki/mediaiotask_imagefile.h>
#include <promeki/imagefileio.h>
#include <promeki/iodevice.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/logger.h>
#include <promeki/imgseq.h>
#include <promeki/dir.h>
#include <promeki/filepath.h>
#include <promeki/buffer.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO(MediaIOTask_ImageFile)

const MediaIO::ConfigID MediaIOTask_ImageFile::ConfigImageFileID("ImageFileID");
const MediaIO::ConfigID MediaIOTask_ImageFile::ConfigVideoSize("VideoSize");
const MediaIO::ConfigID MediaIOTask_ImageFile::ConfigPixelDesc("PixelDesc");
const MediaIO::ConfigID MediaIOTask_ImageFile::ConfigFrameRate("FrameRate");
const MediaIO::ConfigID MediaIOTask_ImageFile::ConfigSequenceHead("SequenceHead");

// ============================================================================
// Frame-rate source tags (values for Metadata::FrameRateSource)
// ============================================================================

static const char *const kFrameRateSourceFile    = "file";
static const char *const kFrameRateSourceConfig  = "config";
static const char *const kFrameRateSourceDefault = "default";

// ============================================================================
// Extension-to-ImageFile::ID mapping
// ============================================================================

struct ExtMap {
        const char *ext;
        int         id;
};

static const ExtMap extMap[] = {
        { "dpx",     ImageFile::DPX },
        { "cin",     ImageFile::Cineon },
        { "tga",     ImageFile::TGA },
        { "sgi",     ImageFile::SGI },
        { "rgb",     ImageFile::SGI },
        { "pnm",     ImageFile::PNM },
        { "ppm",     ImageFile::PNM },
        { "pgm",     ImageFile::PNM },
        { "png",     ImageFile::PNG },
        { "uyvy",    ImageFile::RawYUV },
        { "yuyv",    ImageFile::RawYUV },
        { "yuy2",    ImageFile::RawYUV },
        { "v210",    ImageFile::RawYUV },
        { "i420",    ImageFile::RawYUV },
        { "nv12",    ImageFile::RawYUV },
        { "yuv420p", ImageFile::RawYUV },
        { "i422",    ImageFile::RawYUV },
        { "yuv422p", ImageFile::RawYUV },
        { "yuv",     ImageFile::RawYUV },
};

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
        for(const auto &m : extMap) {
                if(ext == m.ext) return m.id;
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

static StringList buildExtensions() {
        StringList exts;
        for(const auto &m : extMap) {
                bool dup = false;
                for(const auto &e : exts) {
                        if(e == m.ext) { dup = true; break; }
                }
                if(!dup) exts.pushToBack(m.ext);
        }
        exts.pushToBack(kImgSeqExtension);
        return exts;
}

MediaIO::FormatDesc MediaIOTask_ImageFile::formatDesc() {
        return {
                "ImageFile",
                "Single-image files and image sequences (DPX, Cineon, TGA, SGI, PNM, PNG, RawYUV, .imgseq)",
                buildExtensions(),
                true,   // canRead
                true,   // canWrite
                false,  // canReadWrite
                []() -> MediaIOTask * {
                        return new MediaIOTask_ImageFile();
                },
                []() -> MediaIO::Config {
                        MediaIO::Config cfg;
                        cfg.set(MediaIO::ConfigType, "ImageFile");
                        cfg.set(ConfigFrameRate, DefaultFrameRate);
                        return cfg;
                },
                probeImageDevice
        };
}

// ============================================================================
// Lifecycle
// ============================================================================

MediaIOTask_ImageFile::~MediaIOTask_ImageFile() = default;

Error MediaIOTask_ImageFile::executeCmd(MediaIOCommandOpen &cmd) {
        const MediaIO::Config &cfg = cmd.config;

        _filename = cfg.getAs<String>(MediaIO::ConfigFilename);
        _mode = cmd.mode;
        _sequenceMode = false;

        // Resolve the reported frame rate with a documented priority:
        //   1. Writer with a valid pendingMediaDesc frame rate
        //   2. .imgseq sidecar frameRate (resolved below, reader only)
        //   3. Config override (ConfigFrameRate)
        //   4. DefaultFrameRate
        FrameRate fps = DefaultFrameRate;
        String frSource = kFrameRateSourceDefault;

        if(cmd.mode == MediaIO::Writer && cmd.pendingMediaDesc.frameRate().isValid()) {
                fps = cmd.pendingMediaDesc.frameRate();
                frSource = kFrameRateSourceFile;
        } else if(cfg.contains(ConfigFrameRate)) {
                FrameRate fromCfg = cfg.getAs<FrameRate>(ConfigFrameRate);
                if(fromCfg.isValid()) {
                        fps = fromCfg;
                        frSource = kFrameRateSourceConfig;
                }
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
                // If the caller set ConfigFrameRate, keep frSource as
                // "config" for now — openSequence() may upgrade it to
                // "file" if the sidecar supplies a rate.  If nothing is
                // configured yet, leave it at "default".
                Error err = openSequence(cmd, mediaDesc, frSource);
                if(err.isError()) return err;
        } else {
                Error err = openSingle(cmd, mediaDesc, frSource);
                if(err.isError()) return err;
        }

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

        // Determine the ImageFile format ID
        if(cfg.contains(ConfigImageFileID)) {
                _imageFileID = cfg.getAs<int>(ConfigImageFileID);
        } else {
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
        if(cmd.mode == MediaIO::Reader && !io->canLoad()) {
                promekiErr("MediaIOTask_ImageFile: backend '%s' does not support loading",
                        io->name().cstr());
                return Error::NotSupported;
        }
        if(cmd.mode == MediaIO::Writer && !io->canSave()) {
                promekiErr("MediaIOTask_ImageFile: backend '%s' does not support saving",
                        io->name().cstr());
                return Error::NotSupported;
        }

        if(cmd.mode == MediaIO::Reader) {
                ImageFile imgFile(_imageFileID);
                imgFile.setFilename(_filename);

                // For headerless formats, set hint image from config
                Size2Du32 hintSize = cfg.getAs<Size2Du32>(ConfigVideoSize, Size2Du32());
                if(hintSize.width() > 0 && hintSize.height() > 0) {
                        PixelDesc pd = cfg.getAs<PixelDesc>(ConfigPixelDesc, PixelDesc());
                        if(pd.isValid()) {
                                Image hint(hintSize.width(), hintSize.height(), pd.id());
                                imgFile.setImage(hint);
                        }
                }

                Error err = imgFile.load();
                if(err.isError()) {
                        promekiErr("MediaIOTask_ImageFile: load '%s' failed: %s",
                                _filename.cstr(), err.name().cstr());
                        return err;
                }

                _frame = Frame::Ptr::create(imgFile.frame());

                if(!_frame->imageList().isEmpty()) {
                        const Image &img = *_frame->imageList()[0];
                        ImageDesc idesc(img.width(), img.height(), img.pixelDesc().id());
                        mediaDesc.imageList().pushToBack(idesc);
                }

                // If the image backend loaded embedded audio (e.g.
                // DPX with an AUDIO user-data block), surface its
                // descriptor in the MediaDesc and on the command so
                // downstream consumers (SDL player, transcoders) know
                // to wire up an audio sink.
                if(!_frame->audioList().isEmpty()) {
                        const AudioDesc &adesc = _frame->audioList()[0]->desc();
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
        Size2Du32 hintSize  = cfg.getAs<Size2Du32>(ConfigVideoSize, Size2Du32());
        PixelDesc hintPixel = cfg.getAs<PixelDesc>(ConfigPixelDesc, PixelDesc());
        Metadata  sidecarMeta;
        FrameRate sidecarFps;

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
                dir = FilePath(_filename).parent();

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
                if(seq.pixelDesc().isValid()) {
                        hintPixel = seq.pixelDesc();
                }
                sidecarMeta = seq.metadata();
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
        _seqPixelDesc = hintPixel;

        // Determine the per-file ImageFile::ID from the pattern suffix.
        if(cfg.contains(ConfigImageFileID)) {
                _imageFileID = cfg.getAs<int>(ConfigImageFileID);
        } else {
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
        if(cmd.mode == MediaIO::Reader && !io->canLoad()) {
                promekiErr("MediaIOTask_ImageFile: backend '%s' does not support loading",
                        io->name().cstr());
                return Error::NotSupported;
        }
        if(cmd.mode == MediaIO::Writer && !io->canSave()) {
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

        if(cmd.mode == MediaIO::Reader) {
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
                if(_seqSize.width() > 0 && _seqSize.height() > 0 && _seqPixelDesc.isValid()) {
                        Image hint(_seqSize.width(), _seqSize.height(), _seqPixelDesc.id());
                        imgFile.setImage(hint);
                }
                Error err = imgFile.load();
                if(err.isError()) {
                        promekiErr("MediaIOTask_ImageFile: failed to load head frame '%s': %s",
                                firstPath.cstr(), err.name().cstr());
                        return err;
                }
                const Frame &f = imgFile.frame();
                if(!f.imageList().isEmpty()) {
                        const Image &img = *f.imageList()[0];
                        ImageDesc idesc(img.width(), img.height(), img.pixelDesc().id());
                        mediaDesc.imageList().pushToBack(idesc);
                }

                // Surface embedded audio in the descriptor.  A DPX
                // sequence written from a source with audio carries
                // per-frame AUDIO user-data blocks; without this the
                // controller would report "no audio" and downstream
                // consumers would never wire up an audio sink.
                if(!f.audioList().isEmpty()) {
                        const AudioDesc &adesc = f.audioList()[0]->desc();
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
        } else {
                // Writer: start at either the configured head or the
                // default.  The writer hasn't written anything yet, so
                // frameCount is 0 and will be updated after each write.
                int64_t writeHead = cfg.getAs<int>(ConfigSequenceHead, DefaultSequenceHead);
                if(head >= 0) writeHead = head;  // sidecar had explicit head
                _seqHead = writeHead;
                _seqTail = (tail >= 0) ? tail : _seqHead;  // tail grows on write if unset
                _seqIndex = 0;
                _seqAtEnd = false;
                _writeCount = 0;

                Metadata meta = cmd.pendingMetadata;
                meta.merge(_seqMetadata);
                meta.set(Metadata::FrameRateSource, frSource);
                cmd.metadata = meta;
                cmd.frameCount = 0;
        }

        cmd.canSeek = (cmd.mode == MediaIO::Reader);
        cmd.defaultStep = 1;  // sequences advance one frame at a time
        return Error::Ok;
}

// ----------------------------------------------------------------------------
// Close
// ----------------------------------------------------------------------------

Error MediaIOTask_ImageFile::executeCmd(MediaIOCommandClose &cmd) {
        _frame = {};
        _filename = String();
        _imageFileID = ImageFile::Invalid;
        _mode = MediaIO_NotOpen;
        _readCount = 0;
        _writeCount = 0;
        _loaded = false;

        _sequenceMode = false;
        _seqName = NumName();
        _seqDir = FilePath();
        _seqHead = 0;
        _seqTail = 0;
        _seqIndex = 0;
        _seqAtEnd = false;
        _seqMetadata = Metadata();
        _seqSize = Size2Du32();
        _seqPixelDesc = PixelDesc();
        return Error::Ok;
}

// ============================================================================
// Frame I/O
// ============================================================================

Error MediaIOTask_ImageFile::executeCmd(MediaIOCommandRead &cmd) {
        if(_sequenceMode) return readSequence(cmd);
        return readSingle(cmd);
}

Error MediaIOTask_ImageFile::readSingle(MediaIOCommandRead &cmd) {
        // ImageFile is single-frame.  With step 0, deliver indefinitely.
        // With step != 0, deliver once then EOF.
        if(_loaded && cmd.step != 0) return Error::EndOfFile;
        cmd.frame = _frame;
        _loaded = true;
        _readCount++;
        cmd.currentFrame = _readCount;
        return Error::Ok;
}

Error MediaIOTask_ImageFile::readSequence(MediaIOCommandRead &cmd) {
        if(_seqAtEnd) return Error::EndOfFile;

        const int64_t length = _seqTail - _seqHead + 1;
        if(length <= 0) return Error::EndOfFile;

        // Clamp the current index — it may have been set out of range
        // by seek.  Bounds check before we load.
        if(_seqIndex < 0 || _seqIndex >= length) {
                _seqAtEnd = true;
                return Error::EndOfFile;
        }

        int64_t frameNum = _seqHead + _seqIndex;
        String fn = (_seqDir / _seqName.name(static_cast<int>(frameNum))).toString();

        ImageFile imgFile(_imageFileID);
        imgFile.setFilename(fn);
        if(_seqSize.width() > 0 && _seqSize.height() > 0 && _seqPixelDesc.isValid()) {
                Image hint(_seqSize.width(), _seqSize.height(), _seqPixelDesc.id());
                imgFile.setImage(hint);
        }
        Error err = imgFile.load();
        if(err.isError()) {
                promekiErr("MediaIOTask_ImageFile: failed to load sequence frame '%s': %s",
                        fn.cstr(), err.name().cstr());
                return err;
        }

        Frame::Ptr frame = Frame::Ptr::create(imgFile.frame());
        // Merge sequence-level metadata onto this frame.
        Metadata &fm = frame.modify()->metadata();
        fm.merge(_seqMetadata);
        fm.set(Metadata::FrameNumber, frameNum);

        cmd.frame = frame;
        cmd.currentFrame = _seqIndex + 1;

        // Advance for the next read.  step==0 holds position, so we
        // intentionally don't latch EOF in that case.
        int step = cmd.step;
        if(step == 0) {
                // Stay on the same frame — no state change.
        } else {
                _seqIndex += step;
                if(_seqIndex < 0 || _seqIndex >= length) {
                        _seqAtEnd = true;
                }
        }
        return Error::Ok;
}

Error MediaIOTask_ImageFile::executeCmd(MediaIOCommandWrite &cmd) {
        if(_sequenceMode) return writeSequence(cmd);
        return writeSingle(cmd);
}

Error MediaIOTask_ImageFile::writeSingle(MediaIOCommandWrite &cmd) {
        ImageFile imgFile(_imageFileID);
        imgFile.setFilename(_filename);
        imgFile.setFrame(*cmd.frame);
        Error err = imgFile.save();
        if(err.isError()) {
                promekiErr("MediaIOTask_ImageFile: save '%s' failed: %s",
                        _filename.cstr(), err.name().cstr());
                return err;
        }
        _writeCount++;
        cmd.currentFrame = _writeCount;
        cmd.frameCount = _writeCount;
        return Error::Ok;
}

Error MediaIOTask_ImageFile::writeSequence(MediaIOCommandWrite &cmd) {
        int64_t frameNum = _seqHead + _writeCount;
        String fn = (_seqDir / _seqName.name(static_cast<int>(frameNum))).toString();

        ImageFile imgFile(_imageFileID);
        imgFile.setFilename(fn);
        imgFile.setFrame(*cmd.frame);
        Error err = imgFile.save();
        if(err.isError()) {
                promekiErr("MediaIOTask_ImageFile: save sequence frame '%s' failed: %s",
                        fn.cstr(), err.name().cstr());
                return err;
        }
        _writeCount++;
        if(frameNum > _seqTail) _seqTail = frameNum;
        cmd.currentFrame = _writeCount;
        cmd.frameCount = _writeCount;
        return Error::Ok;
}

Error MediaIOTask_ImageFile::executeCmd(MediaIOCommandSeek &cmd) {
        if(!_sequenceMode || _mode != MediaIO::Reader) {
                return Error::IllegalSeek;
        }

        const int64_t length = _seqTail - _seqHead + 1;
        if(length <= 0) return Error::IllegalSeek;

        int64_t target = cmd.frameNumber;
        if(target < 0) target = 0;
        if(target >= length) target = length - 1;
        _seqIndex = target;
        _seqAtEnd = false;
        cmd.currentFrame = _seqIndex;
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
