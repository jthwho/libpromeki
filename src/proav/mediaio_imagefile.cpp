/**
 * @file      mediaio_imagefile.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <promeki/mediaio_imagefile.h>
#include <promeki/imagefileio.h>
#include <promeki/iodevice.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO(MediaIO_ImageFile)

const MediaIO::ConfigID MediaIO_ImageFile::ConfigImageFileID("ImageFileID");
const MediaIO::ConfigID MediaIO_ImageFile::ConfigVideoWidth("VideoWidth");
const MediaIO::ConfigID MediaIO_ImageFile::ConfigVideoHeight("VideoHeight");
const MediaIO::ConfigID MediaIO_ImageFile::ConfigPixelDesc("PixelDesc");

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

static int imageFileIDFromExtension(const String &filename) {
        size_t dot = filename.rfind('.');
        if(dot == String::npos || dot + 1 >= filename.size()) return ImageFile::Invalid;
        String ext = filename.mid(dot + 1).toLower();
        for(const auto &m : extMap) {
                if(ext == m.ext) return m.id;
        }
        return ImageFile::Invalid;
}

// ============================================================================
// Magic number probing
// ============================================================================

static bool probeImageDevice(IODevice *device) {
        uint8_t buf[4] = {};
        int64_t n = device->read(buf, 4);
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

        return false;
}

// ============================================================================
// FormatDesc
// ============================================================================

static StringList buildExtensions() {
        StringList exts;
        for(const auto &m : extMap) {
                // Avoid duplicates (SGI has sgi+rgb, PNM has pnm+ppm+pgm, etc.)
                bool dup = false;
                for(const auto &e : exts) {
                        if(e == m.ext) { dup = true; break; }
                }
                if(!dup) exts.pushToBack(m.ext);
        }
        return exts;
}

MediaIO::FormatDesc MediaIO_ImageFile::formatDesc() {
        return {
                "ImageFile",
                "Single-image file formats (DPX, Cineon, TGA, SGI, PNM, PNG, RawYUV)",
                buildExtensions(),
                true,   // canRead
                true,   // canWrite
                [](ObjectBase *parent) -> MediaIO * {
                        return new MediaIO_ImageFile(parent);
                },
                []() -> MediaIO::Config {
                        MediaIO::Config cfg;
                        cfg.set(MediaIO::ConfigType, "ImageFile");
                        return cfg;
                },
                probeImageDevice
        };
}

// ============================================================================
// Lifecycle
// ============================================================================

MediaIO_ImageFile::~MediaIO_ImageFile() {
        if(isOpen()) close();
}

Error MediaIO_ImageFile::onOpen(Mode mode) {
        const Config &cfg = config();

        // Determine the ImageFile format ID
        if(cfg.contains(ConfigImageFileID)) {
                _imageFileID = cfg.getAs<int>(ConfigImageFileID);
        } else {
                String filename = cfg.getAs<String>(MediaIO::ConfigFilename);
                _imageFileID = imageFileIDFromExtension(filename);
        }

        if(_imageFileID == ImageFile::Invalid) {
                promekiErr("MediaIO_ImageFile: cannot determine image format");
                return Error::NotSupported;
        }

        // Validate that the backend exists and supports the requested mode
        const ImageFileIO *io = ImageFileIO::lookup(_imageFileID);
        if(!io->isValid()) {
                promekiErr("MediaIO_ImageFile: no ImageFileIO backend for ID %d", _imageFileID);
                return Error::NotSupported;
        }

        if(mode == Reader && !io->canLoad()) {
                promekiErr("MediaIO_ImageFile: backend '%s' does not support loading",
                        io->name().cstr());
                return Error::NotSupported;
        }
        if(mode == Writer && !io->canSave()) {
                promekiErr("MediaIO_ImageFile: backend '%s' does not support saving",
                        io->name().cstr());
                return Error::NotSupported;
        }

        if(mode == Reader) {
                String filename = cfg.getAs<String>(MediaIO::ConfigFilename);
                ImageFile imgFile(_imageFileID);
                imgFile.setFilename(filename);

                // For headerless formats, set hint image from config
                int hintW = cfg.getAs<int>(ConfigVideoWidth, 0);
                int hintH = cfg.getAs<int>(ConfigVideoHeight, 0);
                if(hintW > 0 && hintH > 0) {
                        PixelDesc pd = cfg.getAs<PixelDesc>(ConfigPixelDesc, PixelDesc());
                        if(pd.isValid()) {
                                Image hint(hintW, hintH, pd.id());
                                imgFile.setImage(hint);
                        }
                }

                Error err = imgFile.load();
                if(err.isError()) {
                        promekiErr("MediaIO_ImageFile: load '%s' failed: %s",
                                filename.cstr(), err.name().cstr());
                        return err;
                }

                _frame = imgFile.frame();

                // Build MediaDesc from the loaded image
                if(!_frame.imageList().isEmpty()) {
                        const Image &img = *_frame.imageList()[0];
                        ImageDesc idesc(img.width(), img.height(), img.pixelDesc().id());
                        _mediaDesc.imageList().pushToBack(idesc);
                }
                _metadata = _frame.metadata();
                _loaded = false;
                _currentFrame = 0;
        }

        return Error::Ok;
}

Error MediaIO_ImageFile::onClose() {
        _frame = Frame();
        _mediaDesc = MediaDesc();
        _metadata = Metadata();
        _imageFileID = ImageFile::Invalid;
        _currentFrame = 0;
        _loaded = false;
        return Error::Ok;
}

// ============================================================================
// Descriptors
// ============================================================================

MediaDesc MediaIO_ImageFile::mediaDesc() const {
        return _mediaDesc;
}

Metadata MediaIO_ImageFile::metadata() const {
        return _metadata;
}

Error MediaIO_ImageFile::setMediaDesc(const MediaDesc &desc) {
        if(isOpen()) return Error::AlreadyOpen;
        _mediaDesc = desc;
        return Error::Ok;
}

Error MediaIO_ImageFile::setMetadata(const Metadata &meta) {
        if(isOpen()) return Error::AlreadyOpen;
        _metadata = meta;
        return Error::Ok;
}

// ============================================================================
// Frame I/O
// ============================================================================

Error MediaIO_ImageFile::onReadFrame(Frame &frame) {
        // With step 0 (default), re-read indefinitely.
        // With step != 0, deliver once then EOF.
        if(_loaded && step() != 0) return Error::EndOfFile;
        frame = _frame;
        _loaded = true;
        _currentFrame++;
        return Error::Ok;
}

Error MediaIO_ImageFile::onWriteFrame(const Frame &frame) {
        String filename = config().getAs<String>(MediaIO::ConfigFilename);
        ImageFile imgFile(_imageFileID);
        imgFile.setFilename(filename);
        imgFile.setFrame(frame);

        Error err = imgFile.save();
        if(err.isError()) {
                promekiErr("MediaIO_ImageFile: save '%s' failed: %s",
                        filename.cstr(), err.name().cstr());
                return err;
        }
        _currentFrame++;
        return Error::Ok;
}

// ============================================================================
// Navigation
// ============================================================================

int64_t MediaIO_ImageFile::frameCount() const {
        if(isOpen() && mode() == Reader) return 1;
        if(isOpen() && mode() == Writer) return _currentFrame;
        return 0;
}

uint64_t MediaIO_ImageFile::currentFrame() const {
        return _currentFrame;
}

PROMEKI_NAMESPACE_END
