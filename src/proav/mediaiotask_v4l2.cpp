/**
 * @file      mediaiotask_v4l2.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_V4L2

#include <cerrno>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <alsa/asoundlib.h>
#include <promeki/mediaiotask_v4l2.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/frame.h>
#include <promeki/pixeldesc.h>
#include <promeki/mediadesc.h>
#include <promeki/metadata.h>
#include <promeki/timestamp.h>
#include <promeki/duration.h>
#include <promeki/logger.h>
#include <promeki/thread.h>
#include <promeki/dir.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO(MediaIOTask_V4L2)
PROMEKI_DEBUG(MediaIOTask_V4L2)

// ============================================================================
// V4L2 ↔ PixelDesc mapping
// ============================================================================

struct V4l2FormatMapping {
        uint32_t        v4l2;
        PixelDesc::ID   pd;
};

static const V4l2FormatMapping formatMap[] = {
        { V4L2_PIX_FMT_YUYV,   PixelDesc::YUV8_422_Rec709                 },
        { V4L2_PIX_FMT_UYVY,   PixelDesc::YUV8_422_UYVY_Rec709           },
        { V4L2_PIX_FMT_NV12,   PixelDesc::YUV8_420_SemiPlanar_Rec709     },
        { V4L2_PIX_FMT_RGB24,  PixelDesc::RGB8_sRGB                       },
        { V4L2_PIX_FMT_BGR24,  PixelDesc::BGR8_sRGB                       },
        { V4L2_PIX_FMT_MJPEG,  PixelDesc::JPEG_YUV8_422_Rec709           },
        { V4L2_PIX_FMT_JPEG,   PixelDesc::JPEG_YUV8_422_Rec709           },
        { V4L2_PIX_FMT_ABGR32, PixelDesc::BGRA8_sRGB                     },
};

static constexpr int FormatMapCount = sizeof(formatMap) / sizeof(formatMap[0]);

PixelDesc::ID MediaIOTask_V4L2::v4l2ToPixelDesc(uint32_t v4l2fmt) {
        for(int i = 0; i < FormatMapCount; i++) {
                if(formatMap[i].v4l2 == v4l2fmt) return formatMap[i].pd;
        }
        return PixelDesc::Invalid;
}

uint32_t MediaIOTask_V4L2::pixelDescToV4l2(PixelDesc::ID pd) {
        for(int i = 0; i < FormatMapCount; i++) {
                if(formatMap[i].pd == pd) return formatMap[i].v4l2;
        }
        return 0;
}

// ============================================================================
// ioctl wrapper with EINTR retry
// ============================================================================

static int xioctl(int fd, unsigned long request, void *arg) {
        int r;
        do {
                r = ioctl(fd, request, arg);
        } while(r == -1 && errno == EINTR);
        return r;
}

// ============================================================================
// V4L2 CID ↔ MediaConfig mapping
// ============================================================================

// Static table of well-known V4L2 controls with curated config key
// names.  Controls NOT in this table are mapped dynamically from the
// V4L2 control name (e.g. "Exposure, Dynamic Framerate" becomes
// "V4l2ExposureDynamicFramerate").

struct V4l2ControlMapping {
        uint32_t                cid;
        MediaConfig::ID         configId;
};

static const V4l2ControlMapping controlMap[] = {
        { V4L2_CID_BRIGHTNESS,              MediaConfig::V4l2Brightness      },
        { V4L2_CID_CONTRAST,                MediaConfig::V4l2Contrast        },
        { V4L2_CID_SATURATION,              MediaConfig::V4l2Saturation      },
        { V4L2_CID_HUE,                     MediaConfig::V4l2Hue             },
        { V4L2_CID_GAMMA,                   MediaConfig::V4l2Gamma           },
        { V4L2_CID_SHARPNESS,               MediaConfig::V4l2Sharpness       },
        { V4L2_CID_BACKLIGHT_COMPENSATION,   MediaConfig::V4l2BacklightComp   },
        { V4L2_CID_WHITE_BALANCE_TEMPERATURE,MediaConfig::V4l2WhiteBalanceTemp},
        { V4L2_CID_AUTO_WHITE_BALANCE,       MediaConfig::V4l2AutoWhiteBalance},
        { V4L2_CID_EXPOSURE_ABSOLUTE,        MediaConfig::V4l2ExposureAbsolute},
        { V4L2_CID_EXPOSURE_AUTO,            MediaConfig::V4l2AutoExposure    },
        { V4L2_CID_GAIN,                    MediaConfig::V4l2Gain            },
        { V4L2_CID_POWER_LINE_FREQUENCY,     MediaConfig::V4l2PowerLineFreq   },
        { V4L2_CID_JPEG_COMPRESSION_QUALITY, MediaConfig::V4l2JpegQuality     },
};

static constexpr int ControlMapCount = sizeof(controlMap) / sizeof(controlMap[0]);

/// Looks up the static config key for a V4L2 CID, or returns an
/// invalid ID if the CID is not in the static table.
static MediaConfig::ID findStaticControlKey(uint32_t cid) {
        for(int i = 0; i < ControlMapCount; i++) {
                if(controlMap[i].cid == cid) return controlMap[i].configId;
        }
        return MediaConfig::ID();
}

/// Builds a config key name from a V4L2 control name string.
/// "Exposure, Dynamic Framerate" → "V4l2ExposureDynamicFramerate"
static String sanitizeControlName(const char *v4l2Name) {
        String result("V4l2");
        bool capitalizeNext = true;
        for(const char *p = v4l2Name; *p; p++) {
                char c = *p;
                if(c == ' ' || c == ',' || c == '-' || c == '_') {
                        capitalizeNext = true;
                        continue;
                }
                if(capitalizeNext) {
                        if(c >= 'a' && c <= 'z') c -= 32;
                        capitalizeNext = false;
                } else if(c >= 'A' && c <= 'Z') {
                        c += 32;
                }
                result += c;
        }
        return result;
}

/// Enumerates all V4L2 controls on the device and registers a
/// MediaConfig::ID for each one (using the static table for
/// well-known CIDs, generated names for the rest).  Returns the
/// runtime mapping so callers can apply or print them.
struct RuntimeControl {
        uint32_t        cid;
        MediaConfig::ID configId;
        int32_t         minimum;
        int32_t         maximum;
        int32_t         defaultValue;
        String          v4l2Name;
};

static List<RuntimeControl> enumerateAndRegisterControls(int fd) {
        List<RuntimeControl> result;
        struct v4l2_queryctrl qc;
        std::memset(&qc, 0, sizeof(qc));
        qc.id = V4L2_CTRL_FLAG_NEXT_CTRL;
        while(xioctl(fd, VIDIOC_QUERYCTRL, &qc) == 0) {
                if(qc.flags & V4L2_CTRL_FLAG_DISABLED) {
                        qc.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
                        continue;
                }
                if(qc.type != V4L2_CTRL_TYPE_INTEGER &&
                   qc.type != V4L2_CTRL_TYPE_BOOLEAN &&
                   qc.type != V4L2_CTRL_TYPE_MENU) {
                        qc.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
                        continue;
                }

                // Use static name if available, otherwise generate one
                MediaConfig::ID id = findStaticControlKey(qc.id);
                if(!id.isValid()) {
                        String keyName = sanitizeControlName(
                                reinterpret_cast<const char *>(qc.name));
                        VariantSpec vs;
                        // Menu controls → register a dynamic Enum type
                        // with the device's actual menu items.
                        if(qc.type == V4L2_CTRL_TYPE_MENU) {
                                Enum::ValueList values;
                                struct v4l2_querymenu qm;
                                std::memset(&qm, 0, sizeof(qm));
                                qm.id = qc.id;
                                for(qm.index = static_cast<uint32_t>(qc.minimum);
                                    static_cast<int>(qm.index) <= qc.maximum;
                                    qm.index++) {
                                        if(xioctl(fd, VIDIOC_QUERYMENU, &qm) == 0) {
                                                values.pushToBack(Enum::Value(
                                                        String(reinterpret_cast<const char *>(qm.name)),
                                                        static_cast<int>(qm.index)));
                                        }
                                }
                                if(!values.isEmpty()) {
                                        Enum::Type et = Enum::registerType(keyName, values,
                                                        values[0].second());
                                        vs.setType(Variant::TypeEnum)
                                          .setDefault(Enum())
                                          .setEnumType(et);
                                } else {
                                        vs.setType(Variant::TypeS32)
                                          .setDefault(int32_t(-1));
                                }
                        } else {
                                vs.setType(Variant::TypeS32)
                                  .setDefault(int32_t(-1))
                                  .setRange(int32_t(-1),
                                            static_cast<int32_t>(qc.maximum));
                        }
                        vs.setDescription(String::sprintf(
                                "%s (empty/unset = device default).", qc.name));
                        id = MediaConfig::declareID(keyName, vs);
                }

                RuntimeControl rc;
                rc.cid          = qc.id;
                rc.configId     = id;
                rc.minimum      = qc.minimum;
                rc.maximum      = qc.maximum;
                rc.defaultValue = qc.default_value;
                rc.v4l2Name     = String(reinterpret_cast<const char *>(qc.name));
                result.pushToBack(rc);
                qc.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
        }
        return result;
}

/// Extracts the integer value from a config entry that may be
/// stored as an int32 (regular controls) or Enum (menu controls).
/// Returns -1 if the entry is unset or at its default sentinel.
static int32_t controlValueFromConfig(const MediaIO::Config &cfg,
                                      const MediaConfig::ID &id) {
        Variant v = cfg.get(id);
        if(!v.isValid()) return -1;
        if(v.type() == Variant::TypeEnum) {
                Enum e = v.get<Enum>();
                if(!e.hasListedValue()) return -1;
                return static_cast<int32_t>(e.value());
        }
        Error err;
        int32_t val = v.get<int32_t>(&err);
        if(err.isError()) return -1;
        return val;
}

/// Applies all user-configured V4L2 controls to the open device.
/// Enumerates ALL device controls and checks config for each.
static void applyV4l2Controls(int fd, const MediaIO::Config &cfg) {
        auto controls = enumerateAndRegisterControls(fd);
        for(const auto &rc : controls) {
                int32_t val = controlValueFromConfig(cfg, rc.configId);
                if(val == -1) continue;

                struct v4l2_control ctrl;
                ctrl.id = rc.cid;
                ctrl.value = val;
                if(xioctl(fd, VIDIOC_S_CTRL, &ctrl) < 0) {
                        promekiWarn("MediaIOTask_V4L2: failed to set %s=%d: %s",
                                    rc.v4l2Name.cstr(), val,
                                    std::strerror(errno));
                }
        }
}

// ============================================================================
// Paired ALSA device auto-detection
// ============================================================================

// Finds the ALSA capture device that shares the same USB parent device
// as the given V4L2 video device.  Returns an ALSA device string
// (e.g. "hw:2,0") on success, or an empty string if no paired device
// is found.
//
// Algorithm:
//   1. Resolve /sys/class/video4linux/<dev>/device to get the V4L2
//      sysfs interface path (e.g. .../usb1/1-1/1-1:1.0).
//   2. Walk up one level to the USB device (e.g. .../usb1/1-1).
//   3. For each ALSA card, resolve /sys/class/sound/cardN/device
//      and check whether it lives under the same USB device path.
//   4. Verify the card has a capture PCM device.
//
// Note: promekiDebug calls here rely on the file-scope
// PROMEKI_DEBUG(MediaIOTask_V4L2) declaration above.

static String findPairedAlsaDevice(const String &v4l2DevPath) {
        namespace fs = std::filesystem;

        // Extract the device name (e.g. "video0") from "/dev/video0"
        fs::path devNode(v4l2DevPath.cstr());
        std::string devName = devNode.filename().string();

        // Resolve the V4L2 sysfs device symlink to its real path
        fs::path v4l2SysfsLink = fs::path("/sys/class/video4linux") / devName / "device";
        std::error_code ec;
        fs::path v4l2InterfacePath = fs::canonical(v4l2SysfsLink, ec);
        if(ec) {
                promekiDebug("MediaIOTask_V4L2: auto-detect: cannot resolve %s: %s",
                             v4l2SysfsLink.c_str(), ec.message().c_str());
                return String();
        }

        // The V4L2 interface path ends with the USB interface
        // (e.g. "1-1:1.0").  The parent is the USB device.
        fs::path usbDevicePath = v4l2InterfacePath.parent_path();
        std::string usbDeviceStr = usbDevicePath.string();
        if(usbDeviceStr.empty()) return String();

        promekiDebug("MediaIOTask_V4L2: auto-detect: %s interface=%s  "
                     "usbDevice=%s",
                     devName.c_str(),
                     v4l2InterfacePath.c_str(),
                     usbDeviceStr.c_str());

        // Scan ALSA cards for one whose parent device is under the
        // same USB device.
        Dir soundDir("/sys/class/sound");
        if(!soundDir.exists()) return String();
        auto cards = soundDir.entryList("card*");
        for(const auto &cardEntry : cards) {
                String cardName = cardEntry.fileName();
                fs::path cardDevLink = fs::path("/sys/class/sound") /
                                       cardName.cstr() / "device";
                fs::path cardDevPath = fs::canonical(cardDevLink, ec);
                if(ec) { ec.clear(); continue; }

                std::string cardDevStr = cardDevPath.string();
                // Check whether the ALSA card's device path is under
                // the same USB device (common parent).
                if(cardDevStr.find(usbDeviceStr) == std::string::npos) {
                        promekiDebug("MediaIOTask_V4L2: auto-detect: %s → %s  "
                                     "not under USB device, skipping",
                                     cardName.cstr(), cardDevStr.c_str());
                        continue;
                }

                // Extract card number from "cardN"
                String numStr = cardName.mid(4);  // skip "card"
                if(numStr.isEmpty()) continue;
                int cardNum = numStr.toInt();

                // Check that a capture PCM device exists for this card
                String captureNode = String::sprintf("/dev/snd/pcmC%dD0c", cardNum);
                if(!fs::exists(captureNode.cstr(), ec)) {
                        promekiDebug("MediaIOTask_V4L2: auto-detect: %s matches USB "
                                     "parent but %s not found",
                                     cardName.cstr(), captureNode.cstr());
                        continue;
                }

                String alsaDev = String::sprintf("hw:%d,0", cardNum);
                promekiInfo("MediaIOTask_V4L2: auto-detected paired ALSA "
                            "capture device %s (card %s)",
                            alsaDev.cstr(), cardName.cstr());
                return alsaDev;
        }
        promekiDebug("MediaIOTask_V4L2: auto-detect: no ALSA card shares USB "
                     "parent %s", usbDeviceStr.c_str());
        return String();
}

// ============================================================================
// Device enumeration
// ============================================================================

static StringList enumerateV4l2Devices() {
        StringList result;
        Dir devDir("/dev");
        if(!devDir.exists()) return result;
        auto entries = devDir.entryList("video*");
        for(const auto &entry : entries) {
                String path = entry.toString();
                int fd = ::open(path.cstr(), O_RDWR | O_NONBLOCK);
                if(fd < 0) continue;
                struct v4l2_capability cap;
                std::memset(&cap, 0, sizeof(cap));
                if(xioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
                        if(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
                                result.pushToBack(path);
                        }
                }
                ::close(fd);
        }
        return result;
}

// ============================================================================
// Path-based probe
// ============================================================================

static bool v4l2CanHandlePath(const String &path) {
        // Quick check: must look like /dev/video*
        if(!path.startsWith("/dev/video")) return false;
        int fd = ::open(path.cstr(), O_RDWR | O_NONBLOCK);
        if(fd < 0) return false;
        struct v4l2_capability cap;
        std::memset(&cap, 0, sizeof(cap));
        bool ok = (xioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) &&
                  (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE);
        if(ok) {
                // Register all device controls in the global config
                // registry so they pass strict validation when the
                // user sets them via --sc before open().
                enumerateAndRegisterControls(fd);
        }
        ::close(fd);
        return ok;
}

// ============================================================================
// Device capability query
// ============================================================================

static List<MediaDesc> v4l2QueryDevice(const MediaIO::Config &config) {
        List<MediaDesc> result;
        String devPath = config.getAs<String>(MediaConfig::V4l2DevicePath, String());
        if(devPath.isEmpty())
                devPath = config.getAs<String>(MediaConfig::Filename, String());
        if(devPath.isEmpty()) return result;

        int fd = ::open(devPath.cstr(), O_RDWR | O_NONBLOCK);
        if(fd < 0) return result;

        // Enumerate pixel formats
        struct v4l2_fmtdesc fmtdesc;
        std::memset(&fmtdesc, 0, sizeof(fmtdesc));
        fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        while(xioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
                PixelDesc::ID pdId = MediaIOTask_V4L2::v4l2ToPixelDesc(
                        fmtdesc.pixelformat);
                if(pdId == PixelDesc::Invalid) {
                        fmtdesc.index++;
                        continue;
                }

                // Enumerate frame sizes for this format
                struct v4l2_frmsizeenum frmsize;
                std::memset(&frmsize, 0, sizeof(frmsize));
                frmsize.pixel_format = fmtdesc.pixelformat;
                while(xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {
                        if(frmsize.type != V4L2_FRMSIZE_TYPE_DISCRETE) {
                                frmsize.index++;
                                continue;
                        }
                        uint32_t w = frmsize.discrete.width;
                        uint32_t h = frmsize.discrete.height;

                        // Enumerate frame intervals for this format+size
                        struct v4l2_frmivalenum frmival;
                        std::memset(&frmival, 0, sizeof(frmival));
                        frmival.pixel_format = fmtdesc.pixelformat;
                        frmival.width = w;
                        frmival.height = h;
                        while(xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS,
                                     &frmival) == 0) {
                                if(frmival.type != V4L2_FRMIVAL_TYPE_DISCRETE) {
                                        frmival.index++;
                                        continue;
                                }
                                // interval = num/den → fps = den/num
                                if(frmival.discrete.numerator > 0) {
                                        FrameRate fps(FrameRate::RationalType(
                                                frmival.discrete.denominator,
                                                frmival.discrete.numerator));
                                        MediaDesc md;
                                        md.setFrameRate(fps);
                                        md.imageList().pushToBack(
                                                ImageDesc(w, h, pdId));
                                        result.pushToBack(md);
                                }
                                frmival.index++;
                        }
                        frmsize.index++;
                }
                fmtdesc.index++;
        }
        ::close(fd);
        return result;
}

/// Enumerates V4L2 controls on a device and prints them to stdout.
/// Called from the --probe path in mediaplay.
static void v4l2PrintControls(const String &devPath) {
        int fd = ::open(devPath.cstr(), O_RDWR | O_NONBLOCK);
        if(fd < 0) return;

        auto controls = enumerateAndRegisterControls(fd);
        ::close(fd);

        if(controls.isEmpty()) return;
        fprintf(stdout, "\nControls:\n");
        for(const auto &rc : controls) {
                fprintf(stdout, "  %-34s min=%-6d max=%-6d default=%-6d  [--sc %s:N]\n",
                        rc.v4l2Name.cstr(),
                        rc.minimum, rc.maximum, rc.defaultValue,
                        rc.configId.name().cstr());
        }
}

// ============================================================================
// FormatDesc
// ============================================================================

MediaIO::FormatDesc MediaIOTask_V4L2::formatDesc() {
        return {
                "V4L2",
                "V4L2 video capture with optional ALSA audio (Linux)",
                {},     // No file extensions — device source
                true,   // canOutput
                false,  // canInput
                false,  // canInputAndOutput
                []() -> MediaIOTask * {
                        return new MediaIOTask_V4L2();
                },
                []() -> MediaIO::Config::SpecMap {
                        MediaIO::Config::SpecMap specs;
                        auto s = [&specs](MediaConfig::ID id, const Variant &def) {
                                const VariantSpec *gs = MediaConfig::spec(id);
                                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
                        };
                        // V4L2
                        s(MediaConfig::V4l2DevicePath,  String());
                        s(MediaConfig::V4l2BufferCount, int32_t(4));
                        s(MediaConfig::V4l2AudioDevice, String("auto"));
                        // Video
                        s(MediaConfig::VideoSize,       Size2Du32(1920, 1080));
                        s(MediaConfig::VideoPixelFormat, PixelDesc(PixelDesc::YUV8_422_Rec709));
                        s(MediaConfig::FrameRate,       FrameRate(FrameRate::FPS_30));
                        // Audio
                        s(MediaConfig::AudioRate,       48000.0f);
                        s(MediaConfig::AudioChannels,   int32_t(2));
                        // Camera controls (all default to -1 = "don't touch")
                        for(int i = 0; i < ControlMapCount; i++) {
                                s(controlMap[i].configId, int32_t(-1));
                        }
                        return specs;
                },
                nullptr,                // defaultMetadata
                nullptr,                // canHandleDevice (not file-based)
                enumerateV4l2Devices,   // enumerate
                v4l2CanHandlePath,      // canHandlePath
                v4l2QueryDevice,        // queryDevice
                [](const MediaIO::Config &cfg) {  // printDeviceInfo
                        String p = cfg.getAs<String>(MediaConfig::V4l2DevicePath, String());
                        if(p.isEmpty()) p = cfg.getAs<String>(MediaConfig::Filename, String());
                        if(!p.isEmpty()) v4l2PrintControls(p);
                }
        };
}

// ============================================================================
// Destructor
// ============================================================================

MediaIOTask_V4L2::~MediaIOTask_V4L2() {
        stopThreads();
        closeAudio();
        closeVideo();
}

// ============================================================================
// V4L2 helpers
// ============================================================================

Error MediaIOTask_V4L2::openVideo(const MediaIO::Config &cfg) {
        String devPath = cfg.getAs<String>(MediaConfig::V4l2DevicePath, String());
        if(devPath.isEmpty()) {
                // Fall back to Filename — set by createForFileRead when
                // the user passes a device path directly.
                devPath = cfg.getAs<String>(MediaConfig::Filename, String());
        }
        if(devPath.isEmpty()) {
                promekiErr("MediaIOTask_V4L2: V4l2DevicePath is empty");
                return Error::InvalidArgument;
        }

        promekiDebug("MediaIOTask_V4L2: opening device %s", devPath.cstr());

        _fd = ::open(devPath.cstr(), O_RDWR);
        if(_fd < 0) {
                promekiErr("MediaIOTask_V4L2: cannot open %s: %s",
                           devPath.cstr(), std::strerror(errno));
                return Error::DeviceNotFound;
        }

        // Query capabilities
        struct v4l2_capability cap;
        std::memset(&cap, 0, sizeof(cap));
        if(xioctl(_fd, VIDIOC_QUERYCAP, &cap) < 0) {
                promekiErr("MediaIOTask_V4L2: VIDIOC_QUERYCAP failed on %s",
                           devPath.cstr());
                return Error::DeviceError;
        }
        if(!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
                promekiErr("MediaIOTask_V4L2: %s does not support video capture",
                           devPath.cstr());
                return Error::NotSupported;
        }
        if(!(cap.capabilities & V4L2_CAP_STREAMING)) {
                promekiErr("MediaIOTask_V4L2: %s does not support streaming I/O",
                           devPath.cstr());
                return Error::NotSupported;
        }

        promekiDebug("MediaIOTask_V4L2: device %s  driver=%s  card=%s  bus=%s  caps=0x%08x",
                     devPath.cstr(),
                     reinterpret_cast<const char *>(cap.driver),
                     reinterpret_cast<const char *>(cap.card),
                     reinterpret_cast<const char *>(cap.bus_info),
                     cap.capabilities);

        // Negotiate pixel format and resolution.
        //
        // Many USB cameras only support high resolutions with MJPEG
        // (USB 2.0 bandwidth limits uncompressed YUYV to ~640x480).
        // Strategy: try the requested format first; if the driver
        // can't achieve the requested resolution, try every mapped
        // format at the requested size and take the first one that
        // hits.  This allows a request for 1920x1080 YUYV to
        // automatically fall back to 1920x1080 MJPEG.
        Size2Du32 reqSize = cfg.getAs<Size2Du32>(MediaConfig::VideoSize, Size2Du32(1920, 1080));
        PixelDesc reqPd = cfg.getAs<PixelDesc>(MediaConfig::VideoPixelFormat,
                                               PixelDesc(PixelDesc::YUV8_422_Rec709));
        uint32_t v4l2fmt = pixelDescToV4l2(reqPd.id());
        if(v4l2fmt == 0) v4l2fmt = V4L2_PIX_FMT_YUYV;

        // Helper: attempt S_FMT with a given V4L2 pixel format and
        // the requested resolution.  Returns true if the driver
        // accepted the requested size (it may still adjust slightly).
        struct v4l2_format fmt;
        auto tryFormat = [&](uint32_t pixfmt) -> bool {
                std::memset(&fmt, 0, sizeof(fmt));
                fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                fmt.fmt.pix.width = reqSize.width();
                fmt.fmt.pix.height = reqSize.height();
                fmt.fmt.pix.pixelformat = pixfmt;
                fmt.fmt.pix.field = V4L2_FIELD_NONE;
                if(xioctl(_fd, VIDIOC_S_FMT, &fmt) < 0) return false;
                return fmt.fmt.pix.width  == reqSize.width() &&
                       fmt.fmt.pix.height == reqSize.height();
        };

        bool sizeOk = tryFormat(v4l2fmt);
        if(!sizeOk) {
                // The requested format can't deliver the requested
                // size.  Try every other mapped format.
                for(int i = 0; i < FormatMapCount; i++) {
                        if(formatMap[i].v4l2 == v4l2fmt) continue;
                        if(tryFormat(formatMap[i].v4l2)) {
                                sizeOk = true;
                                break;
                        }
                }
        }
        if(!sizeOk) {
                // No format supports the requested size.  Fall back:
                // re-apply the original requested format so the driver
                // picks its closest match.
                tryFormat(v4l2fmt);
                promekiWarn("MediaIOTask_V4L2: device cannot provide %ux%u; "
                            "using %ux%u",
                            reqSize.width(), reqSize.height(),
                            fmt.fmt.pix.width, fmt.fmt.pix.height);
        }

        // Read back what the driver actually selected
        PixelDesc::ID actualPd = v4l2ToPixelDesc(fmt.fmt.pix.pixelformat);
        if(actualPd == PixelDesc::Invalid) {
                promekiErr("MediaIOTask_V4L2: driver selected unsupported pixel format 0x%08x",
                           fmt.fmt.pix.pixelformat);
                return Error::NotSupported;
        }
        if(fmt.fmt.pix.pixelformat != v4l2fmt) {
                PixelDesc selectedPd(actualPd);
                promekiInfo("MediaIOTask_V4L2: pixel format changed from %s to %s "
                            "to achieve %ux%u",
                            reqPd.name().cstr(), selectedPd.name().cstr(),
                            fmt.fmt.pix.width, fmt.fmt.pix.height);
        }
        _imageDesc = ImageDesc(fmt.fmt.pix.width, fmt.fmt.pix.height, actualPd);
        if(!_imageDesc.isValid()) {
                promekiErr("MediaIOTask_V4L2: resulting ImageDesc is invalid");
                return Error::InvalidDimension;
        }

        // Negotiate frame rate
        FrameRate reqFps = cfg.getAs<FrameRate>(MediaConfig::FrameRate, FrameRate(FrameRate::FPS_30));
        struct v4l2_streamparm parm;
        std::memset(&parm, 0, sizeof(parm));
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        parm.parm.capture.timeperframe.numerator = reqFps.denominator();
        parm.parm.capture.timeperframe.denominator = reqFps.numerator();
        if(xioctl(_fd, VIDIOC_S_PARM, &parm) < 0) {
                promekiWarn("MediaIOTask_V4L2: VIDIOC_S_PARM failed: %s (using device default)",
                            std::strerror(errno));
        }

        // Read back actual frame rate
        std::memset(&parm, 0, sizeof(parm));
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if(xioctl(_fd, VIDIOC_G_PARM, &parm) == 0 &&
           parm.parm.capture.timeperframe.denominator > 0 &&
           parm.parm.capture.timeperframe.numerator > 0) {
                // V4L2 timeperframe is duration (num/den), so fps = den/num.
                _frameRate = FrameRate(FrameRate::RationalType(
                        parm.parm.capture.timeperframe.denominator,
                        parm.parm.capture.timeperframe.numerator));
        } else {
                _frameRate = reqFps;
        }
        if(_frameRate != reqFps) {
                promekiInfo("MediaIOTask_V4L2: frame rate changed from %s to %s "
                            "(device does not support requested rate)",
                            reqFps.toString().cstr(),
                            _frameRate.toString().cstr());
        }

        promekiDebug("MediaIOTask_V4L2: negotiated %ux%u %s @ %s "
                     "(requested %ux%u %s @ %s)",
                     _imageDesc.size().width(), _imageDesc.size().height(),
                     PixelDesc(actualPd).name().cstr(),
                     _frameRate.toString().cstr(),
                     reqSize.width(), reqSize.height(),
                     reqPd.name().cstr(),
                     reqFps.toString().cstr());

        // Apply user-configured V4L2 camera controls
        applyV4l2Controls(_fd, cfg);

        // Request MMAP buffers
        int reqBufs = cfg.getAs<int>(MediaConfig::V4l2BufferCount, 4);
        struct v4l2_requestbuffers req;
        std::memset(&req, 0, sizeof(req));
        req.count = static_cast<uint32_t>(reqBufs);
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        if(xioctl(_fd, VIDIOC_REQBUFS, &req) < 0) {
                promekiErr("MediaIOTask_V4L2: VIDIOC_REQBUFS failed: %s",
                           std::strerror(errno));
                return Error::DeviceError;
        }
        if(req.count < 2) {
                promekiErr("MediaIOTask_V4L2: insufficient buffer memory (got %u)",
                           req.count);
                return Error::NoMem;
        }

        // Map buffers
        _buffers.resize(static_cast<int>(req.count));
        for(uint32_t i = 0; i < req.count; i++) {
                struct v4l2_buffer buf;
                std::memset(&buf, 0, sizeof(buf));
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;
                buf.index = i;
                if(xioctl(_fd, VIDIOC_QUERYBUF, &buf) < 0) {
                        promekiErr("MediaIOTask_V4L2: VIDIOC_QUERYBUF failed for buffer %u", i);
                        return Error::DeviceError;
                }
                _buffers[static_cast<int>(i)].length = buf.length;
                _buffers[static_cast<int>(i)].start = mmap(
                        nullptr, buf.length,
                        PROT_READ | PROT_WRITE, MAP_SHARED,
                        _fd, buf.m.offset);
                if(_buffers[static_cast<int>(i)].start == MAP_FAILED) {
                        _buffers[static_cast<int>(i)].start = nullptr;
                        promekiErr("MediaIOTask_V4L2: mmap failed for buffer %u: %s",
                                   i, std::strerror(errno));
                        return Error::NoMem;
                }
        }

        promekiDebug("MediaIOTask_V4L2: mapped %zu MMAP buffers (%zu bytes each)",
                     _buffers.size(),
                     _buffers.isEmpty() ? size_t(0) : _buffers[0].length);

        return Error::Ok;
}

Error MediaIOTask_V4L2::startStreaming() {
        // Queue all buffers
        for(int i = 0; i < _buffers.size(); i++) {
                struct v4l2_buffer buf;
                std::memset(&buf, 0, sizeof(buf));
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;
                buf.index = static_cast<uint32_t>(i);
                if(xioctl(_fd, VIDIOC_QBUF, &buf) < 0) {
                        promekiErr("MediaIOTask_V4L2: VIDIOC_QBUF failed for buffer %d: %s",
                                   i, std::strerror(errno));
                        return Error::DeviceError;
                }
        }

        // Start the stream
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if(xioctl(_fd, VIDIOC_STREAMON, &type) < 0) {
                promekiErr("MediaIOTask_V4L2: VIDIOC_STREAMON failed: %s",
                           std::strerror(errno));
                return Error::DeviceError;
        }
        _streaming = true;
        promekiDebug("MediaIOTask_V4L2: STREAMON — capture started with %zu buffers",
                     _buffers.size());
        return Error::Ok;
}

void MediaIOTask_V4L2::stopStreaming() {
        if(!_streaming || _fd < 0) return;
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(_fd, VIDIOC_STREAMOFF, &type);
        _streaming = false;
}

void MediaIOTask_V4L2::closeVideo() {
        if(_fd >= 0) {
                promekiDebug("MediaIOTask_V4L2: closing video device  "
                             "captured=%lld  delivered=%lld",
                             static_cast<long long>(
                                     _framesCaptured.load(std::memory_order_relaxed)),
                             static_cast<long long>(_frameCount));
        }
        stopStreaming();
        for(int i = 0; i < _buffers.size(); i++) {
                if(_buffers[i].start != nullptr) {
                        munmap(_buffers[i].start, _buffers[i].length);
                }
        }
        _buffers.clear();
        if(_fd >= 0) {
                ::close(_fd);
                _fd = -1;
        }
        _imageDesc = ImageDesc();
}

// ============================================================================
// ALSA helpers
// ============================================================================

Error MediaIOTask_V4L2::openAudio(const MediaIO::Config &cfg) {
        String alsaDev = cfg.getAs<String>(MediaConfig::V4l2AudioDevice, String("auto"));

        // "none" or empty → audio disabled
        if(alsaDev.isEmpty() || alsaDev == "none") {
                promekiDebug("MediaIOTask_V4L2: audio disabled (V4l2AudioDevice=%s)",
                             alsaDev.isEmpty() ? "empty" : "none");
                _audioEnabled = false;
                return Error::Ok;
        }

        // "auto" → auto-detect paired USB audio device
        if(alsaDev == "auto") {
                String v4l2Path = cfg.getAs<String>(MediaConfig::V4l2DevicePath, String());
                if(v4l2Path.isEmpty())
                        v4l2Path = cfg.getAs<String>(MediaConfig::Filename, String());
                alsaDev = findPairedAlsaDevice(v4l2Path);
                if(alsaDev.isEmpty()) {
                        promekiDebug("MediaIOTask_V4L2: no paired audio device found, "
                                     "audio disabled");
                        _audioEnabled = false;
                        return Error::Ok;
                }
        }

        // alsaDev is now either the auto-detected device or a
        // user-specified device name — open it directly.
        promekiDebug("MediaIOTask_V4L2: opening ALSA device %s", alsaDev.cstr());

        float sampleRate = cfg.getAs<float>(MediaConfig::AudioRate, 48000.0f);
        int channels = cfg.getAs<int>(MediaConfig::AudioChannels, 2);

        int err = snd_pcm_open(&_pcm, alsaDev.cstr(), SND_PCM_STREAM_CAPTURE, 0);
        if(err < 0) {
                promekiErr("MediaIOTask_V4L2: snd_pcm_open(%s) failed: %s",
                           alsaDev.cstr(), snd_strerror(err));
                return Error::DeviceError;
        }

        snd_pcm_hw_params_t *hwparams;
        snd_pcm_hw_params_alloca(&hwparams);
        snd_pcm_hw_params_any(_pcm, hwparams);

        err = snd_pcm_hw_params_set_access(_pcm, hwparams,
                                           SND_PCM_ACCESS_RW_INTERLEAVED);
        if(err < 0) {
                promekiErr("MediaIOTask_V4L2: cannot set interleaved access: %s",
                           snd_strerror(err));
                return Error::DeviceError;
        }

        // Use float32 native format to match AudioDesc::NativeType
        err = snd_pcm_hw_params_set_format(_pcm, hwparams, SND_PCM_FORMAT_FLOAT_LE);
        if(err < 0) {
                // Fall back to S16_LE if float is not supported
                err = snd_pcm_hw_params_set_format(_pcm, hwparams, SND_PCM_FORMAT_S16_LE);
                if(err < 0) {
                        promekiErr("MediaIOTask_V4L2: cannot set PCM format: %s",
                                   snd_strerror(err));
                        return Error::DeviceError;
                }
                _audioDesc = AudioDesc(AudioDesc::PCMI_S16LE, sampleRate,
                                       static_cast<unsigned int>(channels));
        } else {
                _audioDesc = AudioDesc(AudioDesc::PCMI_Float32LE, sampleRate,
                                       static_cast<unsigned int>(channels));
        }

        unsigned int rate = static_cast<unsigned int>(sampleRate);
        err = snd_pcm_hw_params_set_rate_near(_pcm, hwparams, &rate, nullptr);
        if(err < 0) {
                promekiErr("MediaIOTask_V4L2: cannot set sample rate %u: %s",
                           rate, snd_strerror(err));
                return Error::DeviceError;
        }

        err = snd_pcm_hw_params_set_channels(_pcm, hwparams,
                                             static_cast<unsigned int>(channels));
        if(err < 0) {
                promekiErr("MediaIOTask_V4L2: cannot set %d channels: %s",
                           channels, snd_strerror(err));
                return Error::DeviceError;
        }

        // Set a buffer size large enough to avoid overruns — 1 second
        // of audio, with a minimum of 8192 frames.
        snd_pcm_uframes_t bufferSize = static_cast<snd_pcm_uframes_t>(rate);
        if(bufferSize < 8192) bufferSize = 8192;
        snd_pcm_hw_params_set_buffer_size_near(_pcm, hwparams, &bufferSize);

        err = snd_pcm_hw_params(_pcm, hwparams);
        if(err < 0) {
                promekiErr("MediaIOTask_V4L2: snd_pcm_hw_params failed: %s",
                           snd_strerror(err));
                return Error::DeviceError;
        }

        // Update the audio desc with the actual rate the driver selected
        if(rate != static_cast<unsigned int>(sampleRate)) {
                promekiWarn("MediaIOTask_V4L2: ALSA sample rate adjusted to %u", rate);
                _audioDesc = AudioDesc(_audioDesc.dataType(),
                                       static_cast<float>(rate),
                                       static_cast<unsigned int>(channels));
        }

        err = snd_pcm_prepare(_pcm);
        if(err < 0) {
                promekiErr("MediaIOTask_V4L2: snd_pcm_prepare failed: %s",
                           snd_strerror(err));
                return Error::DeviceError;
        }

        // Set up AudioBuffer ring — store in native float, 2 seconds of headroom.
        // setInputFormat tells it to convert from the ALSA capture format on push.
        AudioDesc ringDesc(AudioDesc::NativeType,
                           static_cast<float>(rate),
                           static_cast<unsigned int>(channels));
        _audioRing = AudioBuffer(ringDesc, rate * 2);
        _audioRing.setInputFormat(_audioDesc);

        _audioEnabled = true;
        promekiDebug("MediaIOTask_V4L2: ALSA opened %s  format=%s  rate=%u  "
                     "channels=%d  bufferSize=%lu  ringCapacity=%u",
                     alsaDev.cstr(),
                     _audioDesc.dataTypeName().cstr(),
                     rate, channels,
                     static_cast<unsigned long>(bufferSize),
                     rate * 2);
        return Error::Ok;
}

void MediaIOTask_V4L2::closeAudio() {
        if(_pcm != nullptr) {
                promekiDebug("MediaIOTask_V4L2: closing ALSA device  "
                             "overruns=%lld",
                             static_cast<long long>(
                                     _alsaOverruns.load(std::memory_order_relaxed)));
                snd_pcm_drop(_pcm);
                snd_pcm_close(_pcm);
                _pcm = nullptr;
        }
        _audioEnabled = false;
        _audioDesc = AudioDesc();
        _audioRing = AudioBuffer();
}

// ============================================================================
// Capture threads
// ============================================================================

void MediaIOTask_V4L2::stopThreads() {
        _stopFlag.store(true, std::memory_order_release);
        if(_videoThread.joinable()) _videoThread.join();
        if(_audioThread.joinable()) _audioThread.join();
        _stopFlag.store(false, std::memory_order_release);
        _videoQueue.clear();
}

void MediaIOTask_V4L2::videoCaptureLoop() {
        Thread::setCurrentThreadName("v4l2-video");
        promekiDebug("MediaIOTask_V4L2: video capture thread started  "
                     "image=%ux%u %s  queueDepth=%d",
                     _imageDesc.size().width(), _imageDesc.size().height(),
                     PixelDesc(_imageDesc.pixelDesc()).name().cstr(),
                     VideoQueueDepth);
        while(!_stopFlag.load(std::memory_order_acquire)) {
                // Poll with a short timeout so we can check _stopFlag
                struct pollfd pfd;
                pfd.fd = _fd;
                pfd.events = POLLIN;
                int pr = poll(&pfd, 1, 100);
                if(pr < 0) {
                        if(errno == EINTR) continue;
                        promekiErr("MediaIOTask_V4L2: poll failed: %s",
                                   std::strerror(errno));
                        _deviceError.store(errno, std::memory_order_release);
                        break;
                }
                if(pr == 0) continue;

                struct v4l2_buffer vbuf;
                std::memset(&vbuf, 0, sizeof(vbuf));
                vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                vbuf.memory = V4L2_MEMORY_MMAP;
                if(xioctl(_fd, VIDIOC_DQBUF, &vbuf) < 0) {
                        if(errno == EAGAIN) continue;
                        int e = errno;
                        promekiErr("MediaIOTask_V4L2: VIDIOC_DQBUF failed: %s",
                                   std::strerror(e));
                        _deviceError.store(e, std::memory_order_release);
                        break;
                }

                int bufIdx = static_cast<int>(vbuf.index);
                const void *src = _buffers[bufIdx].start;
                size_t bytesUsed = vbuf.bytesused;

                // Convert the V4L2 capture timestamp (CLOCK_MONOTONIC)
                // to a TimeStamp for image metadata.
                int64_t captureNs = static_cast<int64_t>(vbuf.timestamp.tv_sec) * 1000000000LL +
                                    static_cast<int64_t>(vbuf.timestamp.tv_usec) * 1000LL;
                TimeStamp captureTime{TimeStamp::Clock::time_point{
                        std::chrono::nanoseconds{captureNs}}};

                PixelDesc pd(_imageDesc.pixelDesc());
                Buffer::Ptr imgBuf = Buffer::Ptr::create(bytesUsed);
                std::memcpy(imgBuf->data(), src, bytesUsed);
                imgBuf->setSize(bytesUsed);

                // Re-queue the buffer immediately
                if(xioctl(_fd, VIDIOC_QBUF, &vbuf) < 0) {
                        int e = errno;
                        promekiErr("MediaIOTask_V4L2: VIDIOC_QBUF re-queue failed: %s",
                                   std::strerror(e));
                        if(e == ENODEV) {
                                _deviceError.store(e, std::memory_order_release);
                                break;
                        }
                }

                Metadata imgMeta;
                imgMeta.set(Metadata::CaptureTime, captureTime);

                Image img = Image::fromBuffer(imgBuf,
                                              _imageDesc.size().width(),
                                              _imageDesc.size().height(),
                                              pd, imgMeta);
                if(!img.isValid()) {
                        promekiWarn("MediaIOTask_V4L2: Image::fromBuffer returned "
                                    "invalid image (bytesUsed=%zu, expected=%zu, pd=%s)",
                                    bytesUsed,
                                    pd.planeSize(0, _imageDesc),
                                    pd.name().cstr());
                        noteFrameDropped();
                        continue;
                }
                Image::Ptr imgPtr = Image::Ptr::create(img);

                // Drop oldest if queue is over depth to keep latency low
                while(_videoQueue.size() >= static_cast<size_t>(VideoQueueDepth)) {
                        Image::Ptr discard;
                        if(!_videoQueue.popOrFail(discard)) break;
                        noteFrameDropped();
                }
                _videoQueue.push(std::move(imgPtr));
                _framesCaptured.fetch_add(1, std::memory_order_relaxed);
        }
        promekiDebug("MediaIOTask_V4L2: video capture thread exiting  "
                     "captured=%lld",
                     static_cast<long long>(
                             _framesCaptured.load(std::memory_order_relaxed)));
}

void MediaIOTask_V4L2::audioCaptureLoop() {
        Thread::setCurrentThreadName("v4l2-audio");
        promekiDebug("MediaIOTask_V4L2: audio capture thread started  "
                     "format=%s  rate=%.0f  channels=%u  chunkSamples=512",
                     _audioDesc.dataTypeName().cstr(),
                     _audioDesc.sampleRate(),
                     _audioDesc.channels());
        // Read in chunks of ~512 samples at a time
        const size_t chunkSamples = 512;
        const size_t frameBytes = _audioDesc.bytesPerSample() * _audioDesc.channels();
        List<uint8_t> tmpBuf(static_cast<int>(chunkSamples * frameBytes));

        while(!_stopFlag.load(std::memory_order_acquire)) {
                snd_pcm_sframes_t rd = snd_pcm_readi(
                        _pcm, tmpBuf.data(),
                        static_cast<snd_pcm_uframes_t>(chunkSamples));
                if(rd == -EPIPE) {
                        promekiWarn("MediaIOTask_V4L2: ALSA overrun, recovering");
                        _alsaOverruns.fetch_add(1, std::memory_order_relaxed);
                        snd_pcm_prepare(_pcm);
                        continue;
                }
                if(rd == -ENODEV) {
                        promekiErr("MediaIOTask_V4L2: ALSA device removed");
                        _deviceError.store(ENODEV, std::memory_order_release);
                        break;
                }
                if(rd < 0) {
                        if(_stopFlag.load(std::memory_order_acquire)) break;
                        promekiWarn("MediaIOTask_V4L2: snd_pcm_readi error: %s",
                                    snd_strerror(static_cast<int>(rd)));
                        snd_pcm_prepare(_pcm);
                        continue;
                }
                if(rd > 0) {
                        _audioRing.push(tmpBuf.data(),
                                        static_cast<size_t>(rd),
                                        _audioDesc);
                }
        }
        promekiDebug("MediaIOTask_V4L2: audio capture thread exiting  "
                     "overruns=%lld",
                     static_cast<long long>(
                             _alsaOverruns.load(std::memory_order_relaxed)));
}

// ============================================================================
// Command handlers
// ============================================================================

Error MediaIOTask_V4L2::executeCmd(MediaIOCommandOpen &cmd) {
        if(cmd.mode != MediaIO::Output) return Error::NotSupported;

        const MediaIO::Config &cfg = cmd.config;

        // Open video device
        Error err = openVideo(cfg);
        if(err.isError()) return err;

        // Open audio device (optional)
        err = openAudio(cfg);
        if(err.isError()) return err;

        // Start video streaming
        err = startStreaming();
        if(err.isError()) return err;

        _frameCount = 0;
        _framesCaptured.store(0, std::memory_order_relaxed);
        _alsaOverruns.store(0, std::memory_order_relaxed);
        _deviceError.store(0, std::memory_order_relaxed);
        _ringAccum = 0;
        _ringAccumFrames = 0;
        _ringAvgBaseline = 0.0;
        _ringBaselineTime = TimeStamp();
        _ringBaselineSet = false;
        _ringOverflowWarned = false;
        _lastCaptureTime = TimeStamp();
        _firstCaptureTime = TimeStamp();
        _firstCaptureFrame = -1;
        _frameDeltaSum = 0.0;
        _frameDeltaSqSum = 0.0;
        _frameDeltaCount = 0;
        _prevPeriodFps = 0.0;

        // Set up periodic debug reporting (1 Hz)
        if(_promeki_debug_enabled) {
                _debugReport = PeriodicCallback(1.0, [this] {
                        int64_t captured = _framesCaptured.load(std::memory_order_relaxed);
                        size_t qDepth = _videoQueue.size();

                        // -- Video timing from V4L2 capture timestamps --
                        double avgFps = 0.0;
                        double periodFps = 0.0;
                        double fpsChange = 0.0;
                        double jitterUs = 0.0;

                        // Long-term average fps from first timestamp
                        if(_firstCaptureFrame >= 0 &&
                           _frameCount > _firstCaptureFrame + 1) {
                                double totalSec = (_lastCaptureTime - _firstCaptureTime)
                                                  .toSecondsDouble();
                                if(totalSec > 0.0) {
                                        avgFps = (double)(_frameCount - _firstCaptureFrame) /
                                                 totalSec;
                                }
                        }

                        // Period fps and jitter from accumulated deltas
                        if(_frameDeltaCount > 0) {
                                double meanDelta = _frameDeltaSum / (double)_frameDeltaCount;
                                if(meanDelta > 0.0) periodFps = 1.0 / meanDelta;
                                fpsChange = periodFps - _prevPeriodFps;
                                if(_prevPeriodFps == 0.0) fpsChange = 0.0;
                                _prevPeriodFps = periodFps;

                                // Jitter = stddev of inter-frame interval
                                double variance = (_frameDeltaSqSum / (double)_frameDeltaCount) -
                                                  (meanDelta * meanDelta);
                                if(variance > 0.0) jitterUs = std::sqrt(variance) * 1e6;

                                _frameDeltaSum = 0.0;
                                _frameDeltaSqSum = 0.0;
                                _frameDeltaCount = 0;
                        }

                        if(_audioEnabled) {
                                size_t ringAvail = _audioRing.available();
                                size_t ringCap = _audioRing.capacity();
                                int64_t overruns = _alsaOverruns.load(std::memory_order_relaxed);
                                double ringAvg = 0.0;
                                double ringDrift = 0.0;
                                if(_ringAccumFrames > 0) {
                                        ringAvg = (double)_ringAccum / (double)_ringAccumFrames;
                                        _ringAccum = 0;
                                        _ringAccumFrames = 0;
                                        if(!_ringBaselineSet) {
                                                _ringAvgBaseline = ringAvg;
                                                _ringBaselineTime = _lastCaptureTime;
                                                _ringBaselineSet = true;
                                        } else {
                                                double elapsedSec = (_lastCaptureTime - _ringBaselineTime)
                                                                    .toSecondsDouble();
                                                if(elapsedSec > 0.0) {
                                                        ringDrift = (ringAvg - _ringAvgBaseline) / elapsedSec;
                                                }
                                        }
                                }
                                promekiDebug("f=%lld cap=%lld q=%zu/%d "
                                             "fps=%.3f pfps=%.3f fpsd=%+.3f jit=%.0fus "
                                             "ring=%zu/%zu ovr=%lld "
                                             "ravg=%.1f drift=%+.1f s/s",
                                             static_cast<long long>(_frameCount),
                                             static_cast<long long>(captured),
                                             qDepth, VideoQueueDepth,
                                             avgFps, periodFps, fpsChange,
                                             jitterUs,
                                             ringAvail, ringCap,
                                             static_cast<long long>(overruns),
                                             ringAvg, ringDrift);
                        } else {
                                promekiDebug("f=%lld cap=%lld q=%zu/%d "
                                             "fps=%.3f pfps=%.3f fpsd=%+.3f jit=%.0fus "
                                             "audio=off",
                                             static_cast<long long>(_frameCount),
                                             static_cast<long long>(captured),
                                             qDepth, VideoQueueDepth,
                                             avgFps, periodFps, fpsChange,
                                             jitterUs);
                        }
                });
        }

        // Launch video capture thread.  ALSA capture is deferred to
        // the first executeCmd(Read) so audio doesn't accumulate in
        // ALSA's kernel buffer during the startup gap before the
        // first video frame arrives.
        _stopFlag.store(false, std::memory_order_release);
        _videoThread = std::thread(&MediaIOTask_V4L2::videoCaptureLoop, this);

        // Fill output fields
        MediaDesc mediaDesc;
        mediaDesc.setFrameRate(_frameRate);
        mediaDesc.imageList().pushToBack(_imageDesc);
        if(_audioEnabled) {
                AudioDesc outDesc = _audioRing.format();
                mediaDesc.audioList().pushToBack(outDesc);
                cmd.audioDesc = outDesc;
        }
        cmd.mediaDesc = mediaDesc;
        cmd.frameRate = _frameRate;
        cmd.canSeek = false;
        cmd.frameCount = MediaIO::FrameCountInfinite;
        return Error::Ok;
}

Error MediaIOTask_V4L2::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        stopThreads();
        closeAudio();
        closeVideo();
        _debugReport = PeriodicCallback();
        _frameRate = FrameRate();
        _frameCount = 0;
        return Error::Ok;
}

Error MediaIOTask_V4L2::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsCaptured,
                      _framesCaptured.load(std::memory_order_relaxed));
        cmd.stats.set(StatsAlsaOverruns,
                      _alsaOverruns.load(std::memory_order_relaxed));
        cmd.stats.set(MediaIOStats::QueueDepth,
                      static_cast<int64_t>(_videoQueue.size()));
        cmd.stats.set(MediaIOStats::QueueCapacity,
                      static_cast<int64_t>(VideoQueueDepth));
        return Error::Ok;
}

Error MediaIOTask_V4L2::executeCmd(MediaIOCommandRead &cmd) {
        // Check for device failure (hot-unplug, hardware error)
        int devErr = _deviceError.load(std::memory_order_acquire);
        if(devErr != 0) {
                promekiErr("MediaIOTask_V4L2: device error: %s",
                           std::strerror(devErr));
                return Error::DeviceError;
        }

        stampWorkBegin();

        // Wait for the next captured image from the video thread.
        // The capture thread delivers frames at the device frame rate,
        // so this normally returns almost immediately.  The 200ms
        // timeout guards against device stalls.
        auto [imgPtr, popErr] = _videoQueue.pop(200);
        if(popErr != Error::Ok) {
                // Check again — the timeout may have been caused by
                // a device failure that happened while we were waiting.
                devErr = _deviceError.load(std::memory_order_acquire);
                if(devErr != 0) {
                        promekiErr("MediaIOTask_V4L2: device error: %s",
                                   std::strerror(devErr));
                        return Error::DeviceError;
                }
                noteFrameDropped();
                return Error::TryAgain;
        }

        // Extract the V4L2 capture timestamp and accumulate
        // frame-to-frame timing for the periodic debug report.
        Variant ctVar = imgPtr->metadata().get(Metadata::CaptureTime);
        if(ctVar.isValid()) {
                TimeStamp ct = ctVar.get<TimeStamp>();
                if(_firstCaptureFrame < 0) {
                        _firstCaptureTime = ct;
                        _firstCaptureFrame = _frameCount;
                } else if(_lastCaptureTime.nanoseconds() > 0) {
                        double delta = (ct - _lastCaptureTime).toSecondsDouble();
                        if(delta > 0.0) {
                                _frameDeltaSum += delta;
                                _frameDeltaSqSum += delta * delta;
                                _frameDeltaCount++;
                        }
                }
                _lastCaptureTime = ct;
        }

        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(std::move(imgPtr));

        // Wait for enough audio samples to fill this frame.  The
        // audio capture thread pushes continuously at the ALSA rate;
        // popWait blocks until the ring has accumulated a full
        // frame's worth so the downstream audio stream is gap-free.
        // A 200ms timeout matches the video pop timeout above —
        // if audio stalls that long something is wrong.
        if(_audioEnabled) {
                // Start ALSA capture on the first read, after the
                // first video frame has arrived.  This ensures the
                // audio stream is synchronized to the video stream
                // with no startup gap.
                if(_frameCount == 0) {
                        int serr = snd_pcm_start(_pcm);
                        if(serr < 0) {
                                promekiErr("MediaIOTask_V4L2: snd_pcm_start failed: %s",
                                           snd_strerror(serr));
                                return Error::DeviceError;
                        }
                        _audioThread = std::thread(
                                &MediaIOTask_V4L2::audioCaptureLoop, this);
                        promekiDebug("MediaIOTask_V4L2: ALSA capture started on "
                                     "first video frame");
                }

                size_t samplesNeeded = _frameRate.samplesPerFrame(
                        static_cast<int64_t>(_audioDesc.sampleRate()),
                        _frameCount);
                if(samplesNeeded > 0) {
                        AudioDesc nativeDesc = _audioRing.format();
                        Audio audio(nativeDesc, samplesNeeded);
                        auto [got, err] = _audioRing.popWait(audio, samplesNeeded, 200);
                        if(err == Error::Timeout) {
                                promekiWarn("MediaIOTask_V4L2: audio ring timeout "
                                            "(needed %zu, available %zu)",
                                            samplesNeeded, _audioRing.available());
                                return Error::Timeout;
                        }
                        if(err.isError()) return err;
                        frame.modify()->audioList().pushToBack(
                                Audio::Ptr::create(audio));
                }

                // Sample the ring level for the periodic average
                size_t avail = _audioRing.available();
                _ringAccum += static_cast<int64_t>(avail);
                _ringAccumFrames++;

                // Guard against ring overflow from USB audio/video
                // clock drift.  When the ALSA and V4L2 endpoints use
                // independent oscillators (depends on the USB host
                // controller path at enumeration time), the audio
                // sample rate can differ from nominal by up to ~1000
                // ppm, slowly filling the ring.  Drop excess samples
                // when we approach capacity to prevent data loss.
                // A future audio resampler will eliminate this.
                size_t cap = _audioRing.capacity();
                if(avail > cap * 3 / 4) {
                        size_t excess = avail - cap / 4;
                        _audioRing.drop(excess);
                        if(!_ringOverflowWarned) {
                                _ringOverflowWarned = true;
                                double driftRate = 0.0;
                                double driftPpm = 0.0;
                                double elapsedSec = 0.0;
                                if(_ringBaselineSet) {
                                        elapsedSec = (_lastCaptureTime - _ringBaselineTime)
                                                     .toSecondsDouble();
                                        if(elapsedSec > 0.0) {
                                                driftRate = (double)avail / elapsedSec;
                                                driftPpm = (driftRate / _audioDesc.sampleRate()) * 1e6;
                                        }
                                }
                                promekiErr("MediaIOTask_V4L2: audio ring drift overflow — "
                                           "dropped %zu samples (ring was %zu/%zu, "
                                           "drift %+.1f samp/sec, %.0f ppm, "
                                           "%.0f sec into capture).  "
                                           "The USB audio and video clocks are not "
                                           "locked on this host controller path.  "
                                           "Audio/video sync will drift until a "
                                           "resampler is available.  This can vary "
                                           "between device plug-ins depending on which "
                                           "USB hub or controller the OS enumerates "
                                           "the device on.",
                                           excess, avail, cap,
                                           driftRate, driftPpm, elapsedSec);
                        }
                }
        }

        _frameCount++;
        cmd.frame = std::move(frame);
        cmd.currentFrame = _frameCount;
        stampWorkEnd();
        _debugReport.service();
        return Error::Ok;
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_V4L2
