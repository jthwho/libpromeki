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

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO(MediaIOTask_ImageFile)

const MediaIO::ConfigID MediaIOTask_ImageFile::ConfigImageFileID("ImageFileID");
const MediaIO::ConfigID MediaIOTask_ImageFile::ConfigVideoWidth("VideoWidth");
const MediaIO::ConfigID MediaIOTask_ImageFile::ConfigVideoHeight("VideoHeight");
const MediaIO::ConfigID MediaIOTask_ImageFile::ConfigPixelDesc("PixelDesc");

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
                bool dup = false;
                for(const auto &e : exts) {
                        if(e == m.ext) { dup = true; break; }
                }
                if(!dup) exts.pushToBack(m.ext);
        }
        return exts;
}

MediaIO::FormatDesc MediaIOTask_ImageFile::formatDesc() {
        return {
                "ImageFile",
                "Single-image file formats (DPX, Cineon, TGA, SGI, PNM, PNG, RawYUV)",
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

        // Determine the ImageFile format ID
        if(cfg.contains(ConfigImageFileID)) {
                _imageFileID = cfg.getAs<int>(ConfigImageFileID);
        } else {
                String fn = cfg.getAs<String>(MediaIO::ConfigFilename);
                _imageFileID = imageFileIDFromExtension(fn);
        }

        if(_imageFileID == ImageFile::Invalid) {
                promekiErr("MediaIOTask_ImageFile: cannot determine image format");
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

        _filename = cfg.getAs<String>(MediaIO::ConfigFilename);
        _mode = cmd.mode;

        MediaDesc mediaDesc;

        if(cmd.mode == MediaIO::Reader) {
                ImageFile imgFile(_imageFileID);
                imgFile.setFilename(_filename);

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
                        promekiErr("MediaIOTask_ImageFile: load '%s' failed: %s",
                                _filename.cstr(), err.name().cstr());
                        return err;
                }

                _frame = Frame::Ptr::create(imgFile.frame());

                // Build MediaDesc from the loaded image
                if(!_frame->imageList().isEmpty()) {
                        const Image &img = *_frame->imageList()[0];
                        ImageDesc idesc(img.width(), img.height(), img.pixelDesc().id());
                        mediaDesc.imageList().pushToBack(idesc);
                }
                cmd.metadata = _frame->metadata();
                cmd.frameCount = 1;
                _loaded = false;
        } else {
                // Writer: use the pending mediaDesc/metadata if provided
                mediaDesc = cmd.pendingMediaDesc;
                cmd.metadata = cmd.pendingMetadata;
                _writeCount = 0;
                cmd.frameCount = 0;
        }

        cmd.mediaDesc = mediaDesc;
        cmd.canSeek = false;
        cmd.defaultStep = 0;  // ImageFile re-reads the same single frame by default
        return Error::Ok;
}

Error MediaIOTask_ImageFile::executeCmd(MediaIOCommandClose &cmd) {
        _frame = {};
        _filename = String();
        _imageFileID = ImageFile::Invalid;
        _mode = MediaIO_NotOpen;
        _readCount = 0;
        _writeCount = 0;
        _loaded = false;
        return Error::Ok;
}

// ============================================================================
// Frame I/O
// ============================================================================

Error MediaIOTask_ImageFile::executeCmd(MediaIOCommandRead &cmd) {
        // ImageFile is single-frame.  With step 0, deliver indefinitely.
        // With step != 0, deliver once then EOF.
        if(_loaded && cmd.step != 0) return Error::EndOfFile;
        cmd.frame = _frame;
        _loaded = true;
        _readCount++;
        cmd.currentFrame = _readCount;
        return Error::Ok;
}

Error MediaIOTask_ImageFile::executeCmd(MediaIOCommandWrite &cmd) {
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

PROMEKI_NAMESPACE_END
