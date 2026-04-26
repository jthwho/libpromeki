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
#include <promeki/uncompressedvideopayload.h>
#include <promeki/videopayload.h>

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
// Extension -> PixelFormat mapping
// ---------------------------------------------------------------------------

static PixelFormat::ID pixelFormatFromExtension(const String &ext) {
        String lower = ext.toLower();
        if(lower == ".uyvy")                            return PixelFormat::YUV8_422_UYVY_Rec709;
        if(lower == ".yuyv" || lower == ".yuy2")        return PixelFormat::YUV8_422_Rec709;
        if(lower == ".v210")                            return PixelFormat::YUV10_422_v210_Rec709;
        if(lower == ".i420" || lower == ".yuv420p")     return PixelFormat::YUV8_420_Planar_Rec709;
        if(lower == ".nv12")                            return PixelFormat::YUV8_420_SemiPlanar_Rec709;
        if(lower == ".i422" || lower == ".yuv422p")     return PixelFormat::YUV8_422_Planar_Rec709;
        if(lower == ".yuv")                             return PixelFormat::Invalid; // handled by smart detection
        return PixelFormat::Invalid;
}

// ---------------------------------------------------------------------------
// Smart .yuv detection: try multiple formats against file size
// ---------------------------------------------------------------------------

static const PixelFormat::ID yuvCandidates[] = {
        PixelFormat::YUV8_420_Planar_Rec709,       // I420 — most common .yuv convention
        PixelFormat::YUV8_422_UYVY_Rec709,         // UYVY
        PixelFormat::YUV8_422_Planar_Rec709,       // YUV422P
        PixelFormat::YUV8_420_SemiPlanar_Rec709,   // NV12
};

static String fileExtension(const String &filename) {
        size_t dot = filename.rfind('.');
        if(dot == String::npos) return String();
        return filename.mid(dot);
}

// ---------------------------------------------------------------------------
// Compute expected file size for a given resolution and pixel description
// ---------------------------------------------------------------------------

static size_t expectedFileSize(size_t width, size_t height, const PixelFormat &pd) {
        size_t total = 0;
        for(size_t p = 0; p < pd.planeCount(); ++p) {
                total += pd.memLayout().planeSize(p, width, height);
        }
        return total;
}

// ---------------------------------------------------------------------------
// Try to guess dimensions from file size
// ---------------------------------------------------------------------------

static bool guessDimensions(size_t fileSize, const PixelFormat &pd,
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
                        _description = "Headerless raw YUV image sequence";
                        _extensions = { "uyvy", "yuyv", "yuy2", "v210",
                                        "i420", "nv12", "yuv420p",
                                        "i422", "yuv422p", "yuv" };
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
        PixelFormat::ID pdId = pixelFormatFromExtension(ext);
        if(pdId == PixelFormat::Invalid && !isGenericYuv) {
                promekiErr("RawYUV load '%s': unrecognized extension '%s'",
                           filename.cstr(), ext.cstr());
                return Error::PixelFormatNotSupported;
        }

        // Determine dimensions: use caller-provided payload if valid, else guess
        size_t width = 0;
        size_t height = 0;
        VideoPayload::PtrList hintVps = imageFile.frame().videoPayloads();
        const VideoPayload *hint = (!hintVps.isEmpty() && hintVps[0].isValid()) ?
                hintVps[0].ptr() : nullptr;
        if(hint != nullptr && hint->desc().isValid()) {
                width  = hint->desc().size().width();
                height = hint->desc().size().height();
                if(pdId == PixelFormat::Invalid) pdId = hint->desc().pixelFormat().id();
        } else {
                FileInfo fi(filename);
                auto [fileSizeI64, fileSizeErr] = fi.size();
                if(fileSizeErr != Error::Ok) {
                        promekiErr("RawYUV load '%s': cannot determine file size: %s",
                                   filename.cstr(), fileSizeErr.name().cstr());
                        return fileSizeErr;
                }
                size_t fileSize = static_cast<size_t>(fileSizeI64);

                if(pdId != PixelFormat::Invalid) {
                        // Extension gave us a specific format — guess dimensions
                        PixelFormat candidate(pdId);
                        if(!guessDimensions(fileSize, candidate, width, height)) {
                                promekiErr("RawYUV load '%s': cannot guess dimensions from file size %zu",
                                           filename.cstr(), fileSize);
                                return Error::InvalidDimension;
                        }
                } else {
                        // Smart .yuv detection: try multiple formats
                        bool found = false;
                        for(auto candidateId : yuvCandidates) {
                                PixelFormat candidate(candidateId);
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
                             PixelFormat(pdId).name().cstr(), fileSize);
        }

        // Allocate the payload
        PixelFormat pd(pdId);
        ImageDesc idesc(width, height, pd);
        auto payload = UncompressedVideoPayload::allocate(idesc);
        if(!payload.isValid()) {
                promekiErr("RawYUV load '%s': failed to allocate %zux%zu payload",
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

        UncompressedVideoPayload *raw = payload.modify();
        for(size_t p = 0; p < pd.planeCount(); ++p) {
                auto view = raw->data()[p];
                size_t planeBytes = view.size();
                int64_t bytesRead = file.read(view.data(), planeBytes);
                if(bytesRead < 0 || static_cast<size_t>(bytesRead) != planeBytes) {
                        promekiErr("RawYUV load '%s': short read on plane %zu (expected %zu, got %lld)",
                                   filename.cstr(), p, planeBytes, (long long)bytesRead);
                        file.close();
                        return Error::IOError;
                }
        }

        file.close();
        Frame frame;
        frame.addPayload(payload);
        imageFile.setFrame(frame);
        return Error::Ok;
}

Error ImageFileIO_RawYUV::save(ImageFile &imageFile, const MediaConfig &config) const {
        (void)config;
        const String &filename = imageFile.filename();

        VideoPayload::PtrList vps = imageFile.frame().videoPayloads();
        const UncompressedVideoPayload *uvp = nullptr;
        if(!vps.isEmpty() && vps[0].isValid()) uvp = vps[0]->as<UncompressedVideoPayload>();
        if(uvp == nullptr || !uvp->desc().isValid() || uvp->planeCount() == 0) {
                promekiErr("RawYUV save '%s': no uncompressed video payload", filename.cstr());
                return Error::Invalid;
        }

        File file(filename);
        Error err = file.open(File::WriteOnly, File::Create | File::Truncate);
        if(err.isError()) {
                promekiErr("RawYUV save '%s': %s", filename.cstr(), err.name().cstr());
                return err;
        }

        for(size_t p = 0; p < uvp->planeCount(); ++p) {
                auto view = uvp->plane(p);
                size_t planeBytes = view.size();
                int64_t written = file.write(view.data(), planeBytes);
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
