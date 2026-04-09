/**
 * @file      imagefileio_rawyuv.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/logger.h>
#include <promeki/imagefileio.h>
#include <promeki/imagefile.h>
#include <promeki/fileinfo.h>
#include <promeki/file.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(RawYUV)

// ---------------------------------------------------------------------------
// Well-known resolutions for dimension guessing
// ---------------------------------------------------------------------------

struct Resolution {
        size_t width;
        size_t height;
};

static constexpr Resolution wellKnownResolutions[] = {
        { 7680, 4320 },  // 8K UHD
        { 4096, 2160 },  // DCI 4K
        { 3840, 2160 },  // UHD 4K
        { 1920, 1080 },  // 1080p/i
        { 1280,  720 },  // 720p
        {  720,  576 },  // PAL SD
        {  720,  486 },  // NTSC SD
};

// ---------------------------------------------------------------------------
// Extension -> PixelDesc mapping
// ---------------------------------------------------------------------------

static PixelDesc::ID pixelDescFromExtension(const String &ext) {
        String lower = ext.toLower();
        if(lower == ".uyvy")                            return PixelDesc::YUV8_422_UYVY_Rec709;
        if(lower == ".yuyv" || lower == ".yuy2")        return PixelDesc::YUV8_422_Rec709;
        if(lower == ".v210")                            return PixelDesc::YUV10_422_v210_Rec709;
        if(lower == ".i420" || lower == ".yuv420p")     return PixelDesc::YUV8_420_Planar_Rec709;
        if(lower == ".nv12")                            return PixelDesc::YUV8_420_SemiPlanar_Rec709;
        if(lower == ".i422" || lower == ".yuv422p")     return PixelDesc::YUV8_422_Planar_Rec709;
        if(lower == ".yuv")                             return PixelDesc::Invalid; // handled by smart detection
        return PixelDesc::Invalid;
}

// ---------------------------------------------------------------------------
// Smart .yuv detection: try multiple formats against file size
// ---------------------------------------------------------------------------

static const PixelDesc::ID yuvCandidates[] = {
        PixelDesc::YUV8_420_Planar_Rec709,       // I420 — most common .yuv convention
        PixelDesc::YUV8_422_UYVY_Rec709,         // UYVY
        PixelDesc::YUV8_422_Planar_Rec709,       // YUV422P
        PixelDesc::YUV8_420_SemiPlanar_Rec709,   // NV12
};

static String fileExtension(const String &filename) {
        size_t dot = filename.rfind('.');
        if(dot == String::npos) return String();
        return filename.mid(dot);
}

// ---------------------------------------------------------------------------
// Compute expected file size for a given resolution and pixel description
// ---------------------------------------------------------------------------

static size_t expectedFileSize(size_t width, size_t height, const PixelDesc &pd) {
        size_t total = 0;
        for(size_t p = 0; p < pd.planeCount(); ++p) {
                total += pd.pixelFormat().planeSize(p, width, height);
        }
        return total;
}

// ---------------------------------------------------------------------------
// Try to guess dimensions from file size
// ---------------------------------------------------------------------------

static bool guessDimensions(size_t fileSize, const PixelDesc &pd,
                            size_t &outWidth, size_t &outHeight) {
        for(const auto &res : wellKnownResolutions) {
                if(expectedFileSize(res.width, res.height, pd) == fileSize) {
                        outWidth  = res.width;
                        outHeight = res.height;
                        return true;
                }
        }
        return false;
}

// ---------------------------------------------------------------------------
// ImageFileIO_RawYUV
// ---------------------------------------------------------------------------

class ImageFileIO_RawYUV : public ImageFileIO {
        public:
                ImageFileIO_RawYUV() {
                        _id = ImageFile::RawYUV;
                        _canLoad = true;
                        _canSave = true;
                        _name = "RawYUV";
                }

                Error load(ImageFile &imageFile, const MediaConfig &config) const override;
                Error save(ImageFile &imageFile, const MediaConfig &config) const override;
};
PROMEKI_REGISTER_IMAGEFILEIO(ImageFileIO_RawYUV);

Error ImageFileIO_RawYUV::load(ImageFile &imageFile, const MediaConfig &config) const {
        (void)config;
        const String &filename = imageFile.filename();

        // Determine pixel description from file extension
        String ext = fileExtension(filename);
        bool isGenericYuv = (ext.toLower() == ".yuv");
        PixelDesc::ID pdId = pixelDescFromExtension(ext);
        if(pdId == PixelDesc::Invalid && !isGenericYuv) {
                promekiErr("RawYUV load '%s': unrecognized extension '%s'",
                           filename.cstr(), ext.cstr());
                return Error::PixelFormatNotSupported;
        }

        // Determine dimensions: use caller-provided image if valid, else guess
        size_t width = 0;
        size_t height = 0;
        const Image hint = imageFile.image();
        if(hint.isValid()) {
                width  = hint.width();
                height = hint.height();
                if(pdId == PixelDesc::Invalid) pdId = hint.pixelDesc().id();
        } else {
                FileInfo fi(filename);
                size_t fileSize = fi.size();

                if(pdId != PixelDesc::Invalid) {
                        // Extension gave us a specific format — guess dimensions
                        PixelDesc candidate(pdId);
                        if(!guessDimensions(fileSize, candidate, width, height)) {
                                promekiErr("RawYUV load '%s': cannot guess dimensions from file size %zu",
                                           filename.cstr(), fileSize);
                                return Error::InvalidDimension;
                        }
                } else {
                        // Smart .yuv detection: try multiple formats
                        bool found = false;
                        for(auto candidateId : yuvCandidates) {
                                PixelDesc candidate(candidateId);
                                if(guessDimensions(fileSize, candidate, width, height)) {
                                        pdId = candidateId;
                                        found = true;
                                        break;
                                }
                        }
                        if(!found) {
                                promekiErr("RawYUV load '%s': cannot determine format from file size %zu",
                                           filename.cstr(), fileSize);
                                return Error::InvalidDimension;
                        }
                }
                promekiDebug("RawYUV load '%s': guessed %zux%zu %s from file size %zu",
                             filename.cstr(), width, height,
                             PixelDesc(pdId).name().cstr(), fileSize);
        }

        // Allocate the image
        PixelDesc pd(pdId);
        Image image(width, height, pdId);
        if(!image.isValid()) {
                promekiErr("RawYUV load '%s': failed to allocate %zux%zu image",
                           filename.cstr(), width, height);
                return Error::NoMem;
        }

        // Read raw data
        File file(filename);
        Error err = file.open(File::ReadOnly);
        if(err.isError()) {
                promekiErr("RawYUV load '%s': %s", filename.cstr(), err.name().cstr());
                return err;
        }

        for(size_t p = 0; p < pd.planeCount(); ++p) {
                size_t planeBytes = pd.pixelFormat().planeSize(p, width, height);
                int64_t bytesRead = file.read(image.data(p), planeBytes);
                if(bytesRead < 0 || static_cast<size_t>(bytesRead) != planeBytes) {
                        promekiErr("RawYUV load '%s': short read on plane %zu (expected %zu, got %lld)",
                                   filename.cstr(), p, planeBytes, (long long)bytesRead);
                        file.close();
                        return Error::IOError;
                }
        }

        file.close();
        imageFile.setImage(image);
        return Error::Ok;
}

Error ImageFileIO_RawYUV::save(ImageFile &imageFile, const MediaConfig &config) const {
        (void)config;
        const Image image = imageFile.image();
        const String &filename = imageFile.filename();

        if(!image.isValid()) {
                promekiErr("RawYUV save '%s': image is not valid", filename.cstr());
                return Error::Invalid;
        }

        File file(filename);
        Error err = file.open(File::WriteOnly, File::Create | File::Truncate);
        if(err.isError()) {
                promekiErr("RawYUV save '%s': %s", filename.cstr(), err.name().cstr());
                return err;
        }

        const PixelDesc &pd = image.pixelDesc();
        for(size_t p = 0; p < pd.planeCount(); ++p) {
                size_t planeBytes = pd.pixelFormat().planeSize(p, image.width(), image.height());
                int64_t written = file.write(image.data(p), planeBytes);
                if(written < 0 || static_cast<size_t>(written) != planeBytes) {
                        promekiErr("RawYUV save '%s': short write on plane %zu (expected %zu, wrote %lld)",
                                   filename.cstr(), p, planeBytes, (long long)written);
                        file.close();
                        return Error::IOError;
                }
        }

        file.close();
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
