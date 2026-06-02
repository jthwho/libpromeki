/**
 * @file      v4l2m2mcodec.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_V4L2

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#include <promeki/v4l2m2mcodec.h>
#include <promeki/dir.h>
#include <promeki/logger.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(V4l2M2mCodec)

// Restart-on-EINTR ioctl wrapper (mirrors V4l2MediaIO).
static int xioctl(int fd, unsigned long request, void *arg) {
        int r;
        do {
                r = ioctl(fd, request, arg);
        } while (r == -1 && errno == EINTR);
        return r;
}

// A FourCC printed for diagnostics ('H','2','6','4' → "H264").
static String fourccStr(uint32_t f) {
        char c[5] = {static_cast<char>(f & 0xff), static_cast<char>((f >> 8) & 0xff),
                     static_cast<char>((f >> 16) & 0xff), static_cast<char>((f >> 24) & 0xff), 0};
        return String(c);
}

// True when `type` (an OUTPUT- or CAPTURE-mplane buffer type) enumerates
// `fourcc` on the open fd via VIDIOC_ENUM_FMT.
static bool queueHasFormat(int fd, uint32_t type, uint32_t fourcc) {
        struct v4l2_fmtdesc fmtdesc;
        std::memset(&fmtdesc, 0, sizeof(fmtdesc));
        fmtdesc.type = type;
        while (xioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
                if (fmtdesc.pixelformat == fourcc) return true;
                fmtdesc.index++;
        }
        return false;
}

// ---------------------------------------------------------------------------

V4l2M2mCodec::V4l2M2mCodec() = default;

V4l2M2mCodec::~V4l2M2mCodec() {
        close();
}

uint32_t V4l2M2mCodec::outputStride(uint32_t plane) const {
        return (plane < 4) ? _outStride[plane] : 0;
}

uint32_t V4l2M2mCodec::captureStride(uint32_t plane) const {
        return (plane < 4) ? _capStride[plane] : 0;
}

String V4l2M2mCodec::findDevice(Role role, uint32_t outputFourcc, uint32_t captureFourcc) {
        (void)role;

        Dir devDir("/dev");
        if (!devDir.exists()) return String();
        auto entries = devDir.entryList("video*");
        for (const auto &entry : entries) {
                String path = entry.toString();
                int    fd = ::open(path.cstr(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
                if (fd < 0) continue;

                struct v4l2_capability cap;
                std::memset(&cap, 0, sizeof(cap));
                bool match = false;
                if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
                        const uint32_t caps =
                                (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
                        const bool mplane = (caps & V4L2_CAP_VIDEO_M2M_MPLANE) != 0;
                        const bool splane = (caps & V4L2_CAP_VIDEO_M2M) != 0;
                        if (mplane || splane) {
                                const uint32_t outType = mplane ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE
                                                                : V4L2_BUF_TYPE_VIDEO_OUTPUT;
                                const uint32_t capType = mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                                                                : V4L2_BUF_TYPE_VIDEO_CAPTURE;
                                if (queueHasFormat(fd, outType, outputFourcc) &&
                                    queueHasFormat(fd, capType, captureFourcc)) {
                                        match = true;
                                }
                        }
                }
                ::close(fd);
                if (match) return path;
        }
        return String();
}

Error V4l2M2mCodec::open(const OpenParams &params) {
        close();

        _role = params.role;

        String path = params.devPath;
        if (path.isEmpty()) {
                path = findDevice(params.role, params.outputFourcc, params.captureFourcc);
                if (path.isEmpty()) {
                        promekiErr("V4l2M2mCodec: no mem2mem codec node accepts OUTPUT=%s CAPTURE=%s",
                                   fourccStr(params.outputFourcc).cstr(),
                                   fourccStr(params.captureFourcc).cstr());
                        return Error::DeviceNotFound;
                }
        }

        _fd = ::open(path.cstr(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (_fd < 0) {
                promekiErr("V4l2M2mCodec: open(%s) failed: %s", path.cstr(), std::strerror(errno));
                return Error::OpenFailed;
        }
        _devPath = path;

        struct v4l2_capability cap;
        std::memset(&cap, 0, sizeof(cap));
        if (xioctl(_fd, VIDIOC_QUERYCAP, &cap) < 0) {
                promekiErr("V4l2M2mCodec: VIDIOC_QUERYCAP failed on %s", path.cstr());
                close();
                return Error::DeviceError;
        }
        _driver = String(reinterpret_cast<const char *>(cap.driver));
        const uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
        if (caps & V4L2_CAP_VIDEO_M2M_MPLANE) {
                _mplane = true;
                _outputType = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
                _captureType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        } else if (caps & V4L2_CAP_VIDEO_M2M) {
                _mplane = false;
                _outputType = V4L2_BUF_TYPE_VIDEO_OUTPUT;
                _captureType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        } else {
                promekiErr("V4l2M2mCodec: %s is not a mem2mem device", path.cstr());
                close();
                return Error::NotSupported;
        }

        _outputMemory = (params.outputMemory == Memory::Dmabuf) ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP;
        _captureMemory = (params.captureMemory == Memory::Dmabuf) ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP;
        _outputBufferCount = params.outputBufferCount ? params.outputBufferCount : 6;
        _captureBufferCount = params.captureBufferCount ? params.captureBufferCount : 6;

        if (Error err = negotiateFormats(params); err.isError()) {
                close();
                return err;
        }

        // A decoder must subscribe to the resolution/EOS event channel before
        // streaming so it can observe the initial SOURCE_CHANGE.
        if (_role == Role::Decoder) {
                if (Error err = subscribeEvents(); err.isError()) {
                        close();
                        return err;
                }
        }
        return Error::Ok;
}

Error V4l2M2mCodec::subscribeEvents() {
        const uint32_t events[] = {V4L2_EVENT_SOURCE_CHANGE, V4L2_EVENT_EOS};
        for (uint32_t ev : events) {
                struct v4l2_event_subscription sub;
                std::memset(&sub, 0, sizeof(sub));
                sub.type = ev;
                if (xioctl(_fd, VIDIOC_SUBSCRIBE_EVENT, &sub) < 0) {
                        promekiErr("V4l2M2mCodec: SUBSCRIBE_EVENT(0x%08x) failed: %s", ev,
                                   std::strerror(errno));
                        return Error::DeviceError;
                }
        }
        return Error::Ok;
}

Error V4l2M2mCodec::negotiateFormats(const OpenParams &p) {
        // Per the kernel stateful-encoder init sequence, set the coded queue
        // format first, then the raw queue.  For an encoder the coded queue is
        // CAPTURE; for a decoder it is OUTPUT.
        const bool encoder = (_role == Role::Encoder);
        const uint32_t rawType = encoder ? _outputType : _captureType;
        const uint32_t codedType = encoder ? _captureType : _outputType;
        const uint32_t rawFourcc = encoder ? p.outputFourcc : p.captureFourcc;
        const uint32_t codedFourcc = encoder ? p.captureFourcc : p.outputFourcc;
        _rawFourccWanted = rawFourcc; // CAPTURE raw FourCC the decoder later pins.

        const uint32_t codedSize =
                p.codedBufferSize ? p.codedBufferSize
                                  : (p.size.width() * p.size.height() * 3 / 2 + (1u << 20));

        // --- Coded queue ---
        struct v4l2_format cfmt;
        std::memset(&cfmt, 0, sizeof(cfmt));
        cfmt.type = codedType;
        if (_mplane) {
                cfmt.fmt.pix_mp.width = p.size.width();
                cfmt.fmt.pix_mp.height = p.size.height();
                cfmt.fmt.pix_mp.pixelformat = codedFourcc;
                cfmt.fmt.pix_mp.num_planes = 1;
                cfmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
                cfmt.fmt.pix_mp.plane_fmt[0].sizeimage = codedSize;
        } else {
                cfmt.fmt.pix.width = p.size.width();
                cfmt.fmt.pix.height = p.size.height();
                cfmt.fmt.pix.pixelformat = codedFourcc;
                cfmt.fmt.pix.field = V4L2_FIELD_NONE;
                cfmt.fmt.pix.sizeimage = codedSize;
        }
        if (xioctl(_fd, VIDIOC_S_FMT, &cfmt) < 0) {
                promekiErr("V4l2M2mCodec: S_FMT(coded %s) failed: %s", fourccStr(codedFourcc).cstr(),
                           std::strerror(errno));
                return Error::DeviceError;
        }
        const uint32_t gotCoded = _mplane ? cfmt.fmt.pix_mp.pixelformat : cfmt.fmt.pix.pixelformat;
        if (gotCoded != codedFourcc) {
                promekiErr("V4l2M2mCodec: driver rejected coded format %s (gave %s)",
                           fourccStr(codedFourcc).cstr(), fourccStr(gotCoded).cstr());
                return Error::PixelFormatNotSupported;
        }

        // --- Raw queue ---
        // A stateful decoder cannot set its raw CAPTURE format yet — the
        // geometry is unknown until the driver parses the stream and raises a
        // SOURCE_CHANGE event.  setupCapture() configures it later.  Only the
        // encoder (whose raw OUTPUT geometry is known up front) negotiates it
        // here.
        if (!encoder) {
                return Error::Ok;
        }
        struct v4l2_format rfmt;
        std::memset(&rfmt, 0, sizeof(rfmt));
        rfmt.type = rawType;
        if (_mplane) {
                rfmt.fmt.pix_mp.width = p.size.width();
                rfmt.fmt.pix_mp.height = p.size.height();
                rfmt.fmt.pix_mp.pixelformat = rawFourcc;
                rfmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
                rfmt.fmt.pix_mp.colorspace = p.colorspace;
                rfmt.fmt.pix_mp.ycbcr_enc = p.ycbcrEnc;
                rfmt.fmt.pix_mp.xfer_func = p.xferFunc;
                rfmt.fmt.pix_mp.quantization = p.quantization;
        } else {
                rfmt.fmt.pix.width = p.size.width();
                rfmt.fmt.pix.height = p.size.height();
                rfmt.fmt.pix.pixelformat = rawFourcc;
                rfmt.fmt.pix.field = V4L2_FIELD_NONE;
                rfmt.fmt.pix.colorspace = p.colorspace;
                // The extended single-planar colorimetry fields (ycbcr_enc /
                // quantization / xfer_func) are only honoured when priv carries
                // the magic marker; without it the driver ignores them.
                rfmt.fmt.pix.priv = V4L2_PIX_FMT_PRIV_MAGIC;
                rfmt.fmt.pix.ycbcr_enc = p.ycbcrEnc;
                rfmt.fmt.pix.xfer_func = p.xferFunc;
                rfmt.fmt.pix.quantization = p.quantization;
        }
        if (xioctl(_fd, VIDIOC_S_FMT, &rfmt) < 0) {
                promekiErr("V4l2M2mCodec: S_FMT(raw %s) failed: %s", fourccStr(rawFourcc).cstr(),
                           std::strerror(errno));
                return Error::DeviceError;
        }
        const uint32_t gotRaw = _mplane ? rfmt.fmt.pix_mp.pixelformat : rfmt.fmt.pix.pixelformat;
        if (gotRaw != rawFourcc) {
                promekiErr("V4l2M2mCodec: driver rejected raw format %s (gave %s)", fourccStr(rawFourcc).cstr(),
                           fourccStr(gotRaw).cstr());
                return Error::PixelFormatNotSupported;
        }

        // The driver may have adjusted geometry (alignment); record what it
        // accepted along with the raw-queue line strides for the caller.
        if (_mplane) {
                _width = rfmt.fmt.pix_mp.width;
                _height = rfmt.fmt.pix_mp.height;
                const struct v4l2_pix_format_mplane &rawmp = rfmt.fmt.pix_mp;
                const uint32_t rawPlanes = rawmp.num_planes ? rawmp.num_planes : 1;
                _outPlaneCount = rawPlanes;
                for (uint32_t i = 0; i < rawPlanes && i < 4; ++i) {
                        _outStride[i] = rawmp.plane_fmt[i].bytesperline;
                }
                _colorspace = rawmp.colorspace;
                _ycbcrEnc = rawmp.ycbcr_enc;
                _xferFunc = rawmp.xfer_func;
                _quantization = rawmp.quantization;
        } else {
                _width = rfmt.fmt.pix.width;
                _height = rfmt.fmt.pix.height;
                _outStride[0] = rfmt.fmt.pix.bytesperline;
                _outPlaneCount = 1;
                _colorspace = rfmt.fmt.pix.colorspace;
                _ycbcrEnc = rfmt.fmt.pix.ycbcr_enc;
                _xferFunc = rfmt.fmt.pix.xfer_func;
                _quantization = rfmt.fmt.pix.quantization;
        }
        return Error::Ok;
}

Error V4l2M2mCodec::allocQueue(uint32_t type, uint32_t memory, uint32_t count, List<MappedBuffer> &pool,
                               uint32_t &planeCount) {
        struct v4l2_requestbuffers req;
        std::memset(&req, 0, sizeof(req));
        req.count = count;
        req.type = type;
        req.memory = memory;
        if (xioctl(_fd, VIDIOC_REQBUFS, &req) < 0) {
                promekiErr("V4l2M2mCodec: REQBUFS(type=%u) failed: %s", type, std::strerror(errno));
                return Error::DeviceError;
        }
        if (req.count == 0) {
                promekiErr("V4l2M2mCodec: REQBUFS(type=%u) returned 0 buffers", type);
                return Error::DeviceError;
        }

        // DMABUF queues import a caller-supplied fd per QBUF — no driver
        // memory to query / mmap.  Just create empty slots.
        if (memory == V4L2_MEMORY_DMABUF) {
                if (planeCount == 0) planeCount = 1;
                for (uint32_t i = 0; i < req.count; ++i) pool.pushToBack(MappedBuffer{});
                return Error::Ok;
        }

        for (uint32_t i = 0; i < req.count; ++i) {
                struct v4l2_buffer buf;
                struct v4l2_plane  planes[VIDEO_MAX_PLANES];
                std::memset(&buf, 0, sizeof(buf));
                std::memset(planes, 0, sizeof(planes));
                buf.type = type;
                buf.memory = V4L2_MEMORY_MMAP;
                buf.index = i;
                if (_mplane) {
                        buf.length = VIDEO_MAX_PLANES;
                        buf.m.planes = planes;
                }
                if (xioctl(_fd, VIDIOC_QUERYBUF, &buf) < 0) {
                        promekiErr("V4l2M2mCodec: QUERYBUF(type=%u idx=%u) failed: %s", type, i,
                                   std::strerror(errno));
                        return Error::DeviceError;
                }

                MappedBuffer mb;
                if (_mplane) {
                        planeCount = buf.length ? buf.length : 1;
                        for (uint32_t pl = 0; pl < planeCount; ++pl) {
                                MappedPlane mp;
                                mp.length = planes[pl].length;
                                mp.start = ::mmap(nullptr, planes[pl].length, PROT_READ | PROT_WRITE,
                                                  MAP_SHARED, _fd, planes[pl].m.mem_offset);
                                if (mp.start == MAP_FAILED) {
                                        promekiErr("V4l2M2mCodec: mmap(type=%u idx=%u plane=%u) failed: %s",
                                                   type, i, pl, std::strerror(errno));
                                        mp.start = nullptr;
                                        return Error::DeviceError;
                                }
                                mb.planes.pushToBack(mp);
                        }
                } else {
                        planeCount = 1;
                        MappedPlane mp;
                        mp.length = buf.length;
                        mp.start = ::mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, _fd,
                                          buf.m.offset);
                        if (mp.start == MAP_FAILED) {
                                promekiErr("V4l2M2mCodec: mmap(type=%u idx=%u) failed: %s", type, i,
                                           std::strerror(errno));
                                return Error::DeviceError;
                        }
                        mb.planes.pushToBack(mp);
                }
                pool.pushToBack(std::move(mb));
        }
        return Error::Ok;
}

void V4l2M2mCodec::unmapQueue(List<MappedBuffer> &pool) {
        for (auto &mb : pool) {
                for (auto &mp : mb.planes) {
                        if (mp.start) ::munmap(mp.start, mp.length);
                }
        }
        pool.clear();
}

Error V4l2M2mCodec::queueAllCapture() {
        for (int i = 0; i < static_cast<int>(_capturePool.size()); ++i) {
                struct v4l2_buffer buf;
                struct v4l2_plane  planes[VIDEO_MAX_PLANES];
                std::memset(&buf, 0, sizeof(buf));
                std::memset(planes, 0, sizeof(planes));
                buf.type = _captureType;
                buf.memory = _captureMemory;
                buf.index = i;
                if (_mplane) {
                        buf.length = _capPlaneCount;
                        buf.m.planes = planes;
                }
                if (xioctl(_fd, VIDIOC_QBUF, &buf) < 0) {
                        promekiErr("V4l2M2mCodec: initial QBUF(CAPTURE idx=%d) failed: %s", i,
                                   std::strerror(errno));
                        return Error::DeviceError;
                }
                _capturePool[i].queued = true;
        }
        return Error::Ok;
}

Error V4l2M2mCodec::setControl(uint32_t id, int32_t value, bool optional) {
        if (_fd < 0) return Error::NotReady;
        struct v4l2_ext_control ctrl;
        struct v4l2_ext_controls ctrls;
        std::memset(&ctrl, 0, sizeof(ctrl));
        std::memset(&ctrls, 0, sizeof(ctrls));
        ctrl.id = id;
        ctrl.value = value;
        ctrls.which = V4L2_CTRL_WHICH_CUR_VAL;
        ctrls.count = 1;
        ctrls.controls = &ctrl;
        if (xioctl(_fd, VIDIOC_S_EXT_CTRLS, &ctrls) < 0) {
                if (optional) {
                        promekiDebug("V4l2M2mCodec: control 0x%08x not applied (%s); continuing", id,
                                     std::strerror(errno));
                        return Error::Ok;
                }
                promekiErr("V4l2M2mCodec: S_EXT_CTRLS(0x%08x=%d) failed: %s", id, value, std::strerror(errno));
                return Error::DeviceError;
        }
        return Error::Ok;
}

Error V4l2M2mCodec::setControlCompound(uint32_t id, void *payload, uint32_t size, bool optional) {
        if (_fd < 0) return Error::NotReady;
        struct v4l2_ext_control ctrl;
        struct v4l2_ext_controls ctrls;
        std::memset(&ctrl, 0, sizeof(ctrl));
        std::memset(&ctrls, 0, sizeof(ctrls));
        ctrl.id = id;
        ctrl.size = size;
        ctrl.ptr = payload;
        ctrls.which = V4L2_CTRL_WHICH_CUR_VAL;
        ctrls.count = 1;
        ctrls.controls = &ctrl;
        if (xioctl(_fd, VIDIOC_S_EXT_CTRLS, &ctrls) < 0) {
                if (optional) {
                        promekiDebug("V4l2M2mCodec: compound control 0x%08x not applied (%s); continuing", id,
                                     std::strerror(errno));
                        return Error::Ok;
                }
                promekiErr("V4l2M2mCodec: S_EXT_CTRLS(compound 0x%08x) failed: %s", id, std::strerror(errno));
                return Error::DeviceError;
        }
        return Error::Ok;
}

Error V4l2M2mCodec::start() {
        if (_streaming) return Error::Ok;
        // Allocate both pools now that formats and controls are set.
        if (Error err = allocQueue(_outputType, _outputMemory, _outputBufferCount, _outputPool, _outPlaneCount); err.isError()) return err;
        if (Error err = allocQueue(_captureType, _captureMemory, _captureBufferCount, _capturePool, _capPlaneCount); err.isError()) return err;

        // Pre-queue every CAPTURE buffer so the codec has somewhere to write.
        if (Error err = queueAllCapture(); err.isError()) return err;

        int type = static_cast<int>(_outputType);
        if (xioctl(_fd, VIDIOC_STREAMON, &type) < 0) {
                promekiErr("V4l2M2mCodec: STREAMON(OUTPUT) failed: %s", std::strerror(errno));
                return Error::DeviceError;
        }
        type = static_cast<int>(_captureType);
        if (xioctl(_fd, VIDIOC_STREAMON, &type) < 0) {
                promekiErr("V4l2M2mCodec: STREAMON(CAPTURE) failed: %s", std::strerror(errno));
                return Error::DeviceError;
        }
        _streaming = true;
        _captureDrained = false;
        return Error::Ok;
}

Error V4l2M2mCodec::stop() {
        if (_fd < 0 || !_streaming) return Error::Ok;
        int type = static_cast<int>(_outputType);
        xioctl(_fd, VIDIOC_STREAMOFF, &type);
        type = static_cast<int>(_captureType);
        xioctl(_fd, VIDIOC_STREAMOFF, &type);
        for (auto &mb : _outputPool) mb.queued = false;
        for (auto &mb : _capturePool) mb.queued = false;
        _streaming = false;
        _captureConfigured = false;
        return Error::Ok;
}

Error V4l2M2mCodec::startOutput() {
        if (_streaming) return Error::Ok;
        if (Error err = allocQueue(_outputType, _outputMemory, _outputBufferCount, _outputPool, _outPlaneCount); err.isError()) return err;
        int type = static_cast<int>(_outputType);
        if (xioctl(_fd, VIDIOC_STREAMON, &type) < 0) {
                promekiErr("V4l2M2mCodec: STREAMON(OUTPUT) failed: %s", std::strerror(errno));
                return Error::DeviceError;
        }
        _streaming = true;
        return Error::Ok;
}

Error V4l2M2mCodec::dequeueEvent(bool &sourceChange, bool &eos) {
        sourceChange = false;
        eos = false;
        if (_fd < 0) return Error::NotReady;
        struct v4l2_event ev;
        std::memset(&ev, 0, sizeof(ev));
        if (xioctl(_fd, VIDIOC_DQEVENT, &ev) < 0) {
                if (errno == ENOENT || errno == EAGAIN) return Error::NotReady;
                promekiErr("V4l2M2mCodec: DQEVENT failed: %s", std::strerror(errno));
                return Error::DeviceError;
        }
        if (ev.type == V4L2_EVENT_SOURCE_CHANGE) {
                sourceChange = (ev.u.src_change.changes & V4L2_EVENT_SRC_CH_RESOLUTION) != 0;
        } else if (ev.type == V4L2_EVENT_EOS) {
                eos = true;
        }
        return Error::Ok;
}

Error V4l2M2mCodec::setupCapture() {
        if (_captureConfigured) return Error::Ok;

        // Learn the driver's chosen raw format, then pin the requested FourCC
        // (e.g. NV12) at the same geometry if it differs.
        struct v4l2_format gfmt;
        std::memset(&gfmt, 0, sizeof(gfmt));
        gfmt.type = _captureType;
        if (xioctl(_fd, VIDIOC_G_FMT, &gfmt) < 0) {
                promekiErr("V4l2M2mCodec: G_FMT(CAPTURE) failed: %s", std::strerror(errno));
                return Error::DeviceError;
        }
        const uint32_t gotFourcc = _mplane ? gfmt.fmt.pix_mp.pixelformat : gfmt.fmt.pix.pixelformat;
        if (_rawFourccWanted && gotFourcc != _rawFourccWanted) {
                struct v4l2_format sfmt = gfmt;
                if (_mplane) {
                        sfmt.fmt.pix_mp.pixelformat = _rawFourccWanted;
                } else {
                        sfmt.fmt.pix.pixelformat = _rawFourccWanted;
                }
                if (xioctl(_fd, VIDIOC_S_FMT, &sfmt) == 0) {
                        gfmt = sfmt; // S_FMT returns the adjusted format.
                } else {
                        promekiDebug("V4l2M2mCodec: CAPTURE S_FMT(%s) rejected; using driver default %s",
                                     fourccStr(_rawFourccWanted).cstr(), fourccStr(gotFourcc).cstr());
                }
        }

        // Record the negotiated raw geometry.
        if (_mplane) {
                _capWidth = gfmt.fmt.pix_mp.width;
                _capHeight = gfmt.fmt.pix_mp.height;
                _capFourcc = gfmt.fmt.pix_mp.pixelformat;
                const uint32_t planes = gfmt.fmt.pix_mp.num_planes ? gfmt.fmt.pix_mp.num_planes : 1;
                for (uint32_t i = 0; i < planes && i < 4; ++i) {
                        _capStride[i] = gfmt.fmt.pix_mp.plane_fmt[i].bytesperline;
                }
        } else {
                _capWidth = gfmt.fmt.pix.width;
                _capHeight = gfmt.fmt.pix.height;
                _capFourcc = gfmt.fmt.pix.pixelformat;
                _capStride[0] = gfmt.fmt.pix.bytesperline;
        }

        if (Error err = allocQueue(_captureType, _captureMemory, _captureBufferCount, _capturePool, _capPlaneCount); err.isError()) return err;
        if (Error err = queueAllCapture(); err.isError()) return err;

        int type = static_cast<int>(_captureType);
        if (xioctl(_fd, VIDIOC_STREAMON, &type) < 0) {
                promekiErr("V4l2M2mCodec: STREAMON(CAPTURE) failed: %s", std::strerror(errno));
                return Error::DeviceError;
        }
        _captureConfigured = true;
        _captureDrained = false;
        return Error::Ok;
}

Error V4l2M2mCodec::dequeueRawFrame(List<CapturePlane> &planes, int &index, int64_t &ptsUsec,
                                    bool &endOfStream) {
        planes.clear();
        index = -1;
        ptsUsec = 0;
        endOfStream = false;
        if (_fd < 0 || !_captureConfigured) return Error::NotReady;
        if (_captureDrained) {
                endOfStream = true;
                return Error::NotReady;
        }

        struct v4l2_buffer buf;
        struct v4l2_plane  bplanes[VIDEO_MAX_PLANES];
        std::memset(&buf, 0, sizeof(buf));
        std::memset(bplanes, 0, sizeof(bplanes));
        buf.type = _captureType;
        buf.memory = _captureMemory;
        if (_mplane) {
                buf.length = _capPlaneCount;
                buf.m.planes = bplanes;
        }
        if (xioctl(_fd, VIDIOC_DQBUF, &buf) < 0) {
                if (errno == EAGAIN) return Error::NotReady;
                if (errno == EPIPE) {
                        _captureDrained = true;
                        endOfStream = true;
                        return Error::NotReady;
                }
                promekiErr("V4l2M2mCodec: DQBUF(CAPTURE raw) failed: %s", std::strerror(errno));
                return Error::DeviceError;
        }

        const uint32_t idx = buf.index;
        const bool     last = (buf.flags & V4L2_BUF_FLAG_LAST) != 0;
        ptsUsec = static_cast<int64_t>(buf.timestamp.tv_sec) * 1000000 + buf.timestamp.tv_usec;

        size_t totalUsed = 0;
        if (idx < _capturePool.size()) {
                const MappedBuffer &mb = _capturePool[idx];
                for (uint32_t pl = 0; pl < mb.planes.size(); ++pl) {
                        CapturePlane cp;
                        cp.data = static_cast<const uint8_t *>(mb.planes[pl].start);
                        cp.size = _mplane ? bplanes[pl].bytesused : buf.bytesused;
                        cp.stride = (pl < 4) ? _capStride[pl] : 0;
                        totalUsed += cp.size;
                        planes.pushToBack(cp);
                }
        }
        index = static_cast<int>(idx);

        if (last) {
                _captureDrained = true;
                endOfStream = true;
        }
        if (totalUsed > 0) return Error::Ok;

        // Dequeued an empty, non-final buffer (rare): return it to the codec
        // immediately so the pool slot is not stranded.  The caller only
        // requeues on Error::Ok, so without this the slot would be lost.
        if (!last && idx < _capturePool.size()) {
                requeueRawFrame(static_cast<int>(idx));
                index = -1;
        }
        return Error::NotReady;
}

Error V4l2M2mCodec::requeueRawFrame(int index) {
        if (_fd < 0 || index < 0 || index >= static_cast<int>(_capturePool.size())) return Error::Invalid;
        struct v4l2_buffer buf;
        struct v4l2_plane  planes[VIDEO_MAX_PLANES];
        std::memset(&buf, 0, sizeof(buf));
        std::memset(planes, 0, sizeof(planes));
        buf.type = _captureType;
        buf.memory = _captureMemory;
        buf.index = index;
        if (_mplane) {
                buf.length = _capPlaneCount;
                buf.m.planes = planes;
        }
        if (xioctl(_fd, VIDIOC_QBUF, &buf) < 0) {
                promekiErr("V4l2M2mCodec: requeueRawFrame QBUF(CAPTURE idx=%d) failed: %s", index,
                           std::strerror(errno));
                return Error::DeviceError;
        }
        return Error::Ok;
}

void V4l2M2mCodec::reclaimOutput() {
        if (_fd < 0 || !_streaming) return;
        for (;;) {
                struct v4l2_buffer buf;
                struct v4l2_plane  planes[VIDEO_MAX_PLANES];
                std::memset(&buf, 0, sizeof(buf));
                std::memset(planes, 0, sizeof(planes));
                buf.type = _outputType;
                buf.memory = _outputMemory;
                if (_mplane) {
                        buf.length = _outPlaneCount;
                        buf.m.planes = planes;
                }
                if (xioctl(_fd, VIDIOC_DQBUF, &buf) < 0) break; // EAGAIN: nothing more to reclaim.
                if (buf.index < _outputPool.size()) _outputPool[buf.index].queued = false;
        }
}

Error V4l2M2mCodec::acquireOutput(int &index, List<OutPlane> &planes) {
        index = -1;
        planes.clear();
        if (_fd < 0 || !_streaming) return Error::NotReady;
        reclaimOutput();
        int free = -1;
        for (int i = 0; i < static_cast<int>(_outputPool.size()); ++i) {
                if (!_outputPool[i].queued) {
                        free = i;
                        break;
                }
        }
        if (free < 0) return Error::NotReady;

        const MappedBuffer &mb = _outputPool[free];
        for (uint32_t pl = 0; pl < mb.planes.size(); ++pl) {
                OutPlane op;
                op.data = static_cast<uint8_t *>(mb.planes[pl].start);
                op.capacity = mb.planes[pl].length;
                op.stride = (pl < 4) ? _outStride[pl] : 0;
                planes.pushToBack(op);
        }
        index = free;
        return Error::Ok;
}

Error V4l2M2mCodec::submitOutput(int index, const List<size_t> &bytesused, int64_t ptsUsec) {
        if (_fd < 0 || index < 0 || index >= static_cast<int>(_outputPool.size())) return Error::Invalid;
        struct v4l2_buffer buf;
        struct v4l2_plane  planes[VIDEO_MAX_PLANES];
        std::memset(&buf, 0, sizeof(buf));
        std::memset(planes, 0, sizeof(planes));
        buf.type = _outputType;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = index;
        if (_mplane) {
                buf.length = _outPlaneCount;
                buf.m.planes = planes;
                for (uint32_t pl = 0; pl < _outPlaneCount; ++pl) {
                        const size_t used = (pl < bytesused.size()) ? bytesused[pl] : 0;
                        planes[pl].bytesused = static_cast<uint32_t>(used);
                        planes[pl].length = static_cast<uint32_t>(_outputPool[index].planes[pl].length);
                        planes[pl].data_offset = 0;
                }
        } else {
                buf.bytesused = static_cast<uint32_t>(bytesused.isEmpty() ? 0 : bytesused[0]);
                buf.length = static_cast<uint32_t>(_outputPool[index].planes[0].length);
        }
        buf.timestamp.tv_sec = static_cast<long>(ptsUsec / 1000000);
        buf.timestamp.tv_usec = static_cast<long>(ptsUsec % 1000000);
        if (xioctl(_fd, VIDIOC_QBUF, &buf) < 0) {
                promekiErr("V4l2M2mCodec: QBUF(OUTPUT idx=%d) failed: %s", index, std::strerror(errno));
                return Error::DeviceError;
        }
        _outputPool[index].queued = true;
        return Error::Ok;
}

Error V4l2M2mCodec::queueOutputDmabuf(int fd, size_t bytesused, int64_t ptsUsec, bool &queued) {
        queued = false;
        if (_fd < 0 || !_streaming) return Error::NotReady;
        if (_outputMemory != V4L2_MEMORY_DMABUF) {
                return Error::Invalid; // queue was not opened for dma-buf import
        }
        reclaimOutput();
        int free = -1;
        for (int i = 0; i < static_cast<int>(_outputPool.size()); ++i) {
                if (!_outputPool[i].queued) {
                        free = i;
                        break;
                }
        }
        if (free < 0) return Error::NotReady;

        struct v4l2_buffer buf;
        struct v4l2_plane  planes[VIDEO_MAX_PLANES];
        std::memset(&buf, 0, sizeof(buf));
        std::memset(planes, 0, sizeof(planes));
        buf.type = _outputType;
        buf.memory = V4L2_MEMORY_DMABUF;
        buf.index = free;
        if (_mplane) {
                buf.length = _outPlaneCount;
                buf.m.planes = planes;
                planes[0].m.fd = fd;
                planes[0].bytesused = static_cast<uint32_t>(bytesused);
                planes[0].data_offset = 0;
        } else {
                buf.m.fd = fd;
                buf.bytesused = static_cast<uint32_t>(bytesused);
        }
        buf.timestamp.tv_sec = static_cast<long>(ptsUsec / 1000000);
        buf.timestamp.tv_usec = static_cast<long>(ptsUsec % 1000000);
        if (xioctl(_fd, VIDIOC_QBUF, &buf) < 0) {
                promekiErr("V4l2M2mCodec: QBUF(OUTPUT dma-buf idx=%d fd=%d) failed: %s", free, fd,
                           std::strerror(errno));
                return Error::DeviceError;
        }
        _outputPool[free].queued = true;
        queued = true;
        return Error::Ok;
}

Result<int> V4l2M2mCodec::exportBuffer(bool capture, int index) {
        if (_fd < 0) return {-1, Error::NotReady};
        struct v4l2_exportbuffer expbuf;
        std::memset(&expbuf, 0, sizeof(expbuf));
        expbuf.type = capture ? _captureType : _outputType;
        expbuf.index = static_cast<uint32_t>(index);
        expbuf.plane = 0;
        expbuf.flags = O_RDWR | O_CLOEXEC;
        if (xioctl(_fd, VIDIOC_EXPBUF, &expbuf) < 0) {
                promekiErr("V4l2M2mCodec: EXPBUF(%s idx=%d) failed: %s", capture ? "CAPTURE" : "OUTPUT", index,
                           std::strerror(errno));
                return {-1, Error::DeviceError};
        }
        return {expbuf.fd, Error::Ok};
}

Error V4l2M2mCodec::dequeueCapture(Buffer &out, int64_t &ptsUsec, bool &keyframe, bool &endOfStream) {
        keyframe = false;
        endOfStream = false;
        ptsUsec = 0;
        if (_fd < 0 || !_streaming) return Error::NotReady;
        if (_captureDrained) {
                endOfStream = true;
                return Error::NotReady;
        }

        struct v4l2_buffer buf;
        struct v4l2_plane  planes[VIDEO_MAX_PLANES];
        std::memset(&buf, 0, sizeof(buf));
        std::memset(planes, 0, sizeof(planes));
        buf.type = _captureType;
        buf.memory = _captureMemory;
        if (_mplane) {
                buf.length = _capPlaneCount;
                buf.m.planes = planes;
        }
        if (xioctl(_fd, VIDIOC_DQBUF, &buf) < 0) {
                if (errno == EAGAIN) return Error::NotReady;
                if (errno == EPIPE) { // Some drivers signal end-of-drain with EPIPE.
                        _captureDrained = true;
                        endOfStream = true;
                        return Error::NotReady;
                }
                promekiErr("V4l2M2mCodec: DQBUF(CAPTURE) failed: %s", std::strerror(errno));
                return Error::DeviceError;
        }

        const uint32_t idx = buf.index;
        const size_t   used = _mplane ? planes[0].bytesused : buf.bytesused;
        keyframe = (buf.flags & V4L2_BUF_FLAG_KEYFRAME) != 0;
        const bool last = (buf.flags & V4L2_BUF_FLAG_LAST) != 0;
        ptsUsec = static_cast<int64_t>(buf.timestamp.tv_sec) * 1000000 + buf.timestamp.tv_usec;

        bool produced = false;
        if (used > 0 && idx < _capturePool.size() && !_capturePool[idx].planes.isEmpty()) {
                const uint8_t *src = static_cast<const uint8_t *>(_capturePool[idx].planes[0].start);
                out = Buffer(used);
                std::memcpy(out.data(), src, used);
                out.setSize(used);
                produced = true;
        }

        if (last) {
                // Final drain buffer: do not requeue.  The stream is complete.
                _captureDrained = true;
                endOfStream = true;
                if (idx < _capturePool.size()) _capturePool[idx].queued = false;
        } else if (idx < _capturePool.size()) {
                // Requeue so the codec can keep producing.
                struct v4l2_buffer rq;
                struct v4l2_plane  rqp[VIDEO_MAX_PLANES];
                std::memset(&rq, 0, sizeof(rq));
                std::memset(rqp, 0, sizeof(rqp));
                rq.type = _captureType;
                rq.memory = _captureMemory;
                rq.index = idx;
                if (_mplane) {
                        rq.length = _capPlaneCount;
                        rq.m.planes = rqp;
                }
                if (xioctl(_fd, VIDIOC_QBUF, &rq) < 0) {
                        promekiErr("V4l2M2mCodec: requeue QBUF(CAPTURE idx=%u) failed: %s", idx,
                                   std::strerror(errno));
                }
        }

        return produced ? Error::Ok : Error::NotReady;
}

Error V4l2M2mCodec::poll(bool &outputWritable, bool &captureReadable, int timeoutMs) {
        bool evt = false;
        return pollEvents(outputWritable, captureReadable, evt, timeoutMs);
}

Error V4l2M2mCodec::pollEvents(bool &outputWritable, bool &captureReadable, bool &eventPending, int timeoutMs) {
        outputWritable = false;
        captureReadable = false;
        eventPending = false;
        if (_fd < 0) return Error::NotReady;
        struct pollfd pfd;
        std::memset(&pfd, 0, sizeof(pfd));
        pfd.fd = _fd;
        pfd.events = POLLIN | POLLOUT | POLLPRI;
        const int r = ::poll(&pfd, 1, timeoutMs);
        if (r < 0) {
                if (errno == EINTR) return Error::NotReady;
                return Error::DeviceError;
        }
        if (r == 0) return Error::Timeout;
        if (pfd.revents & POLLOUT) outputWritable = true;
        if (pfd.revents & POLLIN) captureReadable = true;
        if (pfd.revents & POLLPRI) eventPending = true;
        return Error::Ok;
}

Error V4l2M2mCodec::sendStop() {
        if (_fd < 0) return Error::NotReady;
        if (_role == Role::Encoder) {
                struct v4l2_encoder_cmd cmd;
                std::memset(&cmd, 0, sizeof(cmd));
                cmd.cmd = V4L2_ENC_CMD_STOP;
                if (xioctl(_fd, VIDIOC_ENCODER_CMD, &cmd) < 0) {
                        promekiDebug("V4l2M2mCodec: ENCODER_CMD STOP unsupported (%s)", std::strerror(errno));
                        return Error::NotSupported;
                }
        } else {
                struct v4l2_decoder_cmd cmd;
                std::memset(&cmd, 0, sizeof(cmd));
                cmd.cmd = V4L2_DEC_CMD_STOP;
                if (xioctl(_fd, VIDIOC_DECODER_CMD, &cmd) < 0) {
                        promekiDebug("V4l2M2mCodec: DECODER_CMD STOP unsupported (%s)", std::strerror(errno));
                        return Error::NotSupported;
                }
        }
        return Error::Ok;
}

void V4l2M2mCodec::close() {
        stop();
        unmapQueue(_outputPool);
        unmapQueue(_capturePool);
        if (_fd >= 0) {
                ::close(_fd);
                _fd = -1;
        }
        _devPath = String();
        _driver = String();
        _width = _height = 0;
        _outPlaneCount = _capPlaneCount = 1;
        for (int i = 0; i < 4; ++i) _outStride[i] = 0;
        _capWidth = _capHeight = _capFourcc = 0;
        _rawFourccWanted = 0;
        for (int i = 0; i < 4; ++i) _capStride[i] = 0;
        _colorspace = _ycbcrEnc = _xferFunc = _quantization = 0;
        _outputMemory = _captureMemory = 0;
        _streaming = false;
        _captureConfigured = false;
        _captureDrained = false;
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_V4L2
