/**
 * @file      audiofile_libsndfile.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <sndfile.h>
#include <promeki/config.h>
#include <promeki/audiofilefactory.h>
#include <promeki/audiofile.h>
#include <promeki/iodevice.h>
#include <promeki/file.h>
#include <promeki/logger.h>
#include <promeki/platform.h>
#include <promeki/util.h>

PROMEKI_NAMESPACE_BEGIN

struct MetadataMap {
                Metadata::ID metadataID;
                int          sndfileID;
};

static const MetadataMap metadataMap[] = {{Metadata::Title, SF_STR_TITLE},
                                          {Metadata::Copyright, SF_STR_COPYRIGHT},
                                          {Metadata::Software, SF_STR_SOFTWARE},
                                          {Metadata::Artist, SF_STR_ARTIST},
                                          {Metadata::Comment, SF_STR_COMMENT},
                                          {Metadata::Date, SF_STR_DATE},
                                          {Metadata::Album, SF_STR_ALBUM},
                                          {Metadata::License, SF_STR_LICENSE},
                                          {Metadata::TrackNumber, SF_STR_TRACKNUMBER},
                                          {Metadata::Genre, SF_STR_GENRE}};

// SF_VIRTUAL_IO callbacks — user_data is IODevice*.

static sf_count_t sfvio_get_filelen(void *user_data) {
        auto *dev = static_cast<IODevice *>(user_data);
        auto [sz, err] = dev->size();
        if (err.isError()) return -1;
        return static_cast<sf_count_t>(sz);
}

static sf_count_t sfvio_seek(sf_count_t offset, int whence, void *user_data) {
        auto   *dev = static_cast<IODevice *>(user_data);
        int64_t absPos = 0;
        switch (whence) {
                case SEEK_SET: absPos = offset; break;
                case SEEK_CUR: absPos = dev->pos() + offset; break;
                case SEEK_END: {
                        auto [sz, err] = dev->size();
                        if (err.isError()) return -1;
                        absPos = sz + offset;
                        break;
                }
                default: return -1;
        }
        Error err = dev->seek(absPos);
        if (err.isError()) return -1;
        return static_cast<sf_count_t>(dev->pos());
}

static sf_count_t sfvio_read(void *ptr, sf_count_t count, void *user_data) {
        auto   *dev = static_cast<IODevice *>(user_data);
        int64_t ret = dev->read(ptr, static_cast<int64_t>(count));
        if (ret < 0) return 0;
        return static_cast<sf_count_t>(ret);
}

static sf_count_t sfvio_write(const void *ptr, sf_count_t count, void *user_data) {
        auto   *dev = static_cast<IODevice *>(user_data);
        int64_t ret = dev->write(ptr, static_cast<int64_t>(count));
        if (ret < 0) return 0;
        return static_cast<sf_count_t>(ret);
}

static sf_count_t sfvio_tell(void *user_data) {
        auto *dev = static_cast<IODevice *>(user_data);
        return static_cast<sf_count_t>(dev->pos());
}

static SF_VIRTUAL_IO sfVirtualIO = {sfvio_get_filelen, sfvio_seek, sfvio_read, sfvio_write, sfvio_tell};

class AudioFile_LibSndFile : public AudioFile::Impl {
                PROMEKI_SHARED_DERIVED(AudioFile_LibSndFile)
        public:
                AudioFile_LibSndFile(AudioFile::Operation op) : AudioFile::Impl(op) {
                        memset(&_info, 0, sizeof(_info));
                }

                ~AudioFile_LibSndFile() { close(); }

                String effectiveExtension() const {
                        // Prefer format hint, fall back to filename extension.
                        if (!_formatHint.isEmpty()) return _formatHint.toLower();
                        size_t dot = _filename.rfind('.');
                        if (dot == String::npos || dot + 1 >= _filename.size()) return String();
                        return _filename.mid(dot + 1).toLower();
                }

                int computeLibSndFormat() {
                        int    ret = 0;
                        bool   pcm = false;
                        // Only WAV / AIFF / RAW carry an explicit endian
                        // flag in the libsndfile SF_INFO::format field.
                        // FLAC stores native PCM with its own metadata
                        // (no endian flag); OGG-Vorbis / MPEG are
                        // self-describing codecs.  Setting the endian
                        // bits on these formats makes sf_format_check()
                        // reject the descriptor at sf_open() time.
                        bool   wantEndian = false;
                        String ext = effectiveExtension();
                        if (ext == "wav" || ext == "bwf") {
                                ret |= SF_FORMAT_WAV;
                                pcm = true;
                                wantEndian = true;
                        } else if (ext == "aif" || ext == "aiff") {
                                ret |= SF_FORMAT_AIFF;
                                pcm = true;
                                wantEndian = true;
#if PROMEKI_ENABLE_FLAC
                        } else if (ext == "flac") {
                                // FLAC stores native integer PCM (8/16/24-bit).
                                // The subformat is set by the pcm-mapping switch
                                // below from the AudioDesc's element type.
                                ret |= SF_FORMAT_FLAC;
                                pcm = true;
                                wantEndian = false;
#endif
#if PROMEKI_ENABLE_VORBIS
                        } else if (ext == "ogg" || ext == "oga") {
                                ret |= (SF_FORMAT_OGG | SF_FORMAT_VORBIS);
                                pcm = false;
#endif
#if PROMEKI_ENABLE_MP3
                        } else if (ext == "mp3" || ext == "mpeg") {
                                // Layer III is the only practically-useful MPEG
                                // audio variant; libsndfile's MPEG encoder
                                // (via libmp3lame) accepts float/int16 input.
                                ret |= (SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III);
                                pcm = false;
#endif
                        } else {
                                promekiWarn("computeLibSndFormat: invalid extension '%s'", ext.cstr());
                                return 0;
                        }
                        if (pcm) {
                                // For containers that don't carry an
                                // endian flag (FLAC), the helper below
                                // ignores the LE/BE distinction: the
                                // codec stores samples in its own
                                // canonical order and round-trips
                                // exactly through whichever AudioFormat
                                // the caller asked for.
                                const int leEndian = wantEndian ? SF_ENDIAN_LITTLE : 0;
                                const int beEndian = wantEndian ? SF_ENDIAN_BIG : 0;
                                switch (_desc.format().id()) {
                                        case AudioFormat::PCMI_Float32LE:
                                                ret |= (SF_FORMAT_FLOAT | leEndian);
                                                break;
                                        case AudioFormat::PCMI_Float32BE:
                                                ret |= (SF_FORMAT_FLOAT | beEndian);
                                                break;
                                        case AudioFormat::PCMI_S8: ret |= SF_FORMAT_PCM_S8; break;
                                        case AudioFormat::PCMI_U8: ret |= SF_FORMAT_PCM_U8; break;
                                        case AudioFormat::PCMI_S16LE:
                                                ret |= (SF_FORMAT_PCM_16 | leEndian);
                                                break;
                                        case AudioFormat::PCMI_S16BE: ret |= (SF_FORMAT_PCM_16 | beEndian); break;
                                        case AudioFormat::PCMI_S24LE:
                                                ret |= (SF_FORMAT_PCM_24 | leEndian);
                                                break;
                                        case AudioFormat::PCMI_S24BE: ret |= (SF_FORMAT_PCM_24 | beEndian); break;
                                        case AudioFormat::PCMI_S32LE:
                                                ret |= (SF_FORMAT_PCM_32 | leEndian);
                                                break;
                                        case AudioFormat::PCMI_S32BE: ret |= (SF_FORMAT_PCM_32 | beEndian); break;
                                        default:
                                                promekiWarn("computeLibSndFormat: incompatible audio desc: %s",
                                                            _desc.toString().cstr());
                                                return 0;
                                }
                        }
                        return ret;
                }

                void writeBroadcastInfo() {
                        if (_file == nullptr || _operation != AudioFile::Writer) return;
                        if (!_desc.metadata().contains(Metadata::EnableBWF)) return;
                        if (!_desc.metadata().get(Metadata::EnableBWF).get<bool>()) return;
                        SF_BROADCAST_INFO bcinfo;
                        Timecode          tc = _desc.metadata().get(Metadata::Timecode).get<Timecode>();
                        double            frameRate = _desc.metadata().get(Metadata::FrameRate).get<double>();
                        FrameNumber       fn = tc.toFrameNumber();
                        uint64_t          timeref = fn.isValid() ? static_cast<uint64_t>(fn.value()) *
                                                                  static_cast<uint64_t>(frameRate * _desc.sampleRate())
                                                                 : 0u;
                        DateTime          datetime =
                                _desc.metadata().contains(Metadata::OriginationDateTime)
                                                 ? _desc.metadata().get(Metadata::OriginationDateTime).get<DateTime>()
                                                 : DateTime::now();
                        String time = datetime.toString("%H:%M:%S");
                        String date = datetime.toString("%Y-%m-%d");
                        String description = _desc.metadata().get(Metadata::Description).get<String>();
                        String originator = _desc.metadata().get(Metadata::Originator).get<String>();
                        String originatorReference = _desc.metadata().get(Metadata::OriginatorReference).get<String>();
                        String codingHistory = _desc.metadata().get(Metadata::CodingHistory).get<String>();

                        memset(&bcinfo, 0, sizeof(bcinfo));
                        bcinfo.version = 1;
                        bcinfo.time_reference_low = timeref & 0xFFFFFFFF;
                        bcinfo.time_reference_high = timeref >> 32;
                        bcinfo.coding_history_size = codingHistory.size();
                        // BWF SF_BROADCAST_INFO fields are documented as fixed-width,
                        // not-necessarily-null-terminated character arrays.  Truncation at
                        // the field boundary is the intended behaviour, so silence GCC's
                        // -Wstringop-truncation for this block.
#if defined(PROMEKI_COMPILER_GCC)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
#endif
                        strncpy(bcinfo.coding_history, codingHistory.cstr(), sizeof(bcinfo.coding_history));
                        strncpy(bcinfo.originator, originator.cstr(), sizeof(bcinfo.originator));
                        strncpy(bcinfo.originator_reference, originatorReference.cstr(),
                                sizeof(bcinfo.originator_reference));
                        strncpy(bcinfo.description, description.cstr(), sizeof(bcinfo.description));
                        strncpy(bcinfo.origination_date, date.cstr(), sizeof(bcinfo.origination_date));
                        strncpy(bcinfo.origination_time, time.cstr(), sizeof(bcinfo.origination_time));
#if defined(PROMEKI_COMPILER_GCC)
#pragma GCC diagnostic pop
#endif
                        int ret = sf_command(_file, SFC_SET_BROADCAST_INFO, &bcinfo, sizeof(bcinfo));
                        if (ret != SF_TRUE) {
                                promekiWarn("writeBroadcastInfo: failed to write broadcast info");
                        }
                        return;
                }

                AudioDesc computeDesc() {
                        int major = _info.format & SF_FORMAT_TYPEMASK;
                        int sub = _info.format & SF_FORMAT_SUBMASK;
                        int endian = _info.format & SF_FORMAT_ENDMASK;

                        // Determine default endianness from the major type.
                        bool le = true;
                        if (endian == SF_ENDIAN_LITTLE) {
                                le = true;
                        } else if (endian == SF_ENDIAN_BIG) {
                                le = false;
                        } else {
                                // SF_ENDIAN_FILE: WAV is LE, AIFF is BE.
                                le = (major != SF_FORMAT_AIFF);
                        }

                        AudioFormat::ID dt = AudioFormat::Invalid;
                        switch (sub) {
                                case SF_FORMAT_PCM_S8: dt = AudioFormat::PCMI_S8; break;
                                case SF_FORMAT_PCM_U8: dt = AudioFormat::PCMI_U8; break;
                                case SF_FORMAT_PCM_16:
                                        dt = le ? AudioFormat::PCMI_S16LE : AudioFormat::PCMI_S16BE;
                                        break;
                                case SF_FORMAT_PCM_24:
                                        dt = le ? AudioFormat::PCMI_S24LE : AudioFormat::PCMI_S24BE;
                                        break;
                                case SF_FORMAT_PCM_32:
                                        dt = le ? AudioFormat::PCMI_S32LE : AudioFormat::PCMI_S32BE;
                                        break;
                                case SF_FORMAT_FLOAT:
                                        dt = le ? AudioFormat::PCMI_Float32LE : AudioFormat::PCMI_Float32BE;
                                        break;
                                case SF_FORMAT_VORBIS:
                                        // Vorbis decoded as native float.
                                        dt = AudioFormat::PCMI_Float32LE;
                                        break;
#if PROMEKI_ENABLE_MP3
                                case SF_FORMAT_MPEG_LAYER_I:
                                case SF_FORMAT_MPEG_LAYER_II:
                                case SF_FORMAT_MPEG_LAYER_III:
                                        // libsndfile's MPEG decoder (mpg123)
                                        // decodes layer-I/II/III streams to
                                        // native-float PCM.
                                        dt = AudioFormat::PCMI_Float32LE;
                                        break;
#endif
                                default:
                                        promekiWarn("computeDesc: unsupported subformat 0x%x", sub);
                                        return AudioDesc();
                        }
                        return AudioDesc(dt, _info.samplerate, _info.channels);
                }

                Error ensureDevice() {
                        if (_device != nullptr) return Error::Ok;
                        // No device set — create a File IODevice from the filename.
                        if (_filename.isEmpty()) {
                                promekiWarn("ensureDevice: no device and no filename");
                                return Error::InvalidArgument;
                        }
                        auto *file = new File(_filename);
                        _device = file;
                        _ownsDevice = true;
                        // Open the device in the appropriate mode.
                        IODevice::OpenMode mode = IODevice::NotOpen;
                        switch (_operation) {
                                case AudioFile::Reader: mode = IODevice::ReadOnly; break;
                                case AudioFile::Writer: mode = IODevice::ReadWrite; break;
                                default: break;
                        }
                        int fileFlags = 0;
                        if (_operation == AudioFile::Writer) {
                                fileFlags = File::Create | File::Truncate;
                        }
                        Error err = file->open(mode, fileFlags);
                        if (err.isError()) {
                                promekiWarn("ensureDevice: failed to open '%s': %s", _filename.cstr(),
                                            err.name().cstr());
                                delete file;
                                _device = nullptr;
                                _ownsDevice = false;
                                return err;
                        }
                        return Error::Ok;
                }

                void close() override {
                        // sf_close() must happen before closing the IODevice
                        // since libsndfile writes final headers via the callbacks.
                        if (_file != nullptr) {
                                int ret = sf_close(_file);
                                if (ret) promekiWarn("close: sf_close failed with %d", ret);
                                _file = nullptr;
                        }
                        if (_ownsDevice && _device != nullptr) {
                                if (_device->isOpen()) _device->close();
                                delete _device;
                                _device = nullptr;
                                _ownsDevice = false;
                        }
                }

                size_t sampleCount() const override { return _info.frames; }

                Error seekToSample(size_t sample) override {
                        if (_file == nullptr) return Error::NotOpen;
                        sf_count_t ret = sf_seek(_file, static_cast<sf_count_t>(sample), SEEK_SET);
                        if (ret < 0) return Error::IllegalSeek;
                        return Error::Ok;
                }

                Error open() override {
                        if (_file != nullptr) {
                                promekiWarn("open: Attempt to open again");
                                return Error::AlreadyOpen;
                        }
                        // Reject sequential devices.
                        if (_device != nullptr && _device->isSequential()) {
                                return Error::NotSupported;
                        }
                        int mode = 0;
                        switch (_operation) {
                                case AudioFile::Reader: {
                                        mode = SFM_READ;
                                } break;

                                case AudioFile::Writer: {
                                        mode = SFM_WRITE;
                                        if (!_desc.isValid()) {
                                                promekiWarn("open: Can't open for write, desc isn't valid");
                                                return Error::OpenFailed;
                                        }
                                        std::memset(&_info, 0, sizeof(_info));
                                        _info.samplerate = _desc.sampleRate();
                                        _info.channels = _desc.channels();
                                        _info.format = computeLibSndFormat();
                                        if (_info.format == 0) {
                                                promekiWarn("open: Can't write, format not supported");
                                                return Error::NotSupported;
                                        }
                                        // sf_format_check() reports whether this libsndfile
                                        // build can actually encode the requested major+sub
                                        // format combo.  Optional codecs (Vorbis, FLAC,
                                        // Opus, …) are compile-time toggles in libsndfile,
                                        // so a perfectly-formed format mask can still come
                                        // back as unsupported on a stripped-down build.
                                        // Distinguishing this here lets callers treat it as
                                        // a planner gap (NotSupported) instead of a generic
                                        // open failure.
                                        if (sf_format_check(&_info) == 0) {
                                                promekiWarn("open: libsndfile build does not support "
                                                            "format 0x%x for write",
                                                            _info.format);
                                                return Error::NotSupported;
                                        }
                                } break;

                                default:
                                        promekiWarn("open: operation %d not supported", _operation);
                                        return Error::NotSupported;
                        }
                        // Ensure we have a device (create File if needed).
                        Error devErr = ensureDevice();
                        if (devErr.isError()) return devErr;
                        // All paths go through sf_open_virtual().
                        _file = sf_open_virtual(&sfVirtualIO, mode, &_info, _device);
                        if (_file == nullptr) {
                                promekiWarn("open: sf_open_virtual failed: %s", sf_strerror(nullptr));
                                return Error::OpenFailed;
                        }
                        switch (_operation) {
                                case AudioFile::Reader:
                                        _desc = computeDesc();
                                        if (!_desc.isValid()) {
                                                promekiWarn("open: file data isn't a supported format");
                                                return Error::NotSupported;
                                        }
                                        break;

                                case AudioFile::Writer:
                                        for (size_t i = 0; i < PROMEKI_ARRAY_SIZE(metadataMap); ++i) {
                                                auto       &m = _desc.metadata();
                                                const auto &map = metadataMap[i];
                                                if (m.contains(map.metadataID)) {
                                                        int ret = sf_set_string(
                                                                _file, map.sndfileID,
                                                                m.get(map.metadataID).get<String>().cstr());
                                                        if (ret)
                                                                promekiWarn("open: Failed to write metadata %s",
                                                                            map.metadataID.name().cstr());
                                                }
                                        }
                                        writeBroadcastInfo();
                                        break;
                                default:
                                        /* Do Nothing */
                                        break;
                        }
                        return Error::Ok;
                }

                Error write(const PcmAudioPayload &payload) override {
                        if (_operation != AudioFile::Writer) {
                                promekiWarn("write: Attempt to write but in operation %d", _operation);
                                return Error::Invalid;
                        }
                        if (_file == nullptr) {
                                promekiWarn("write: Attempt to write but not open");
                                return Error::NotOpen;
                        }
                        if (!payload.desc().formatEquals(_desc)) {
                                promekiWarn("write: Attempt to write with '%s', but set to '%s'",
                                            payload.desc().toString().cstr(), _desc.toString().cstr());
                                return Error::Invalid;
                        }
                        if (payload.planeCount() == 0) return Error::Invalid;
                        auto             view = payload.plane(0);
                        const void      *data = view.data();
                        const sf_count_t samples = static_cast<sf_count_t>(payload.sampleCount());
                        sf_count_t       ct = 0;
                        switch (_desc.format().id()) {
                                case AudioFormat::PCMI_Float32LE:
                                case AudioFormat::PCMI_Float32BE:
                                        ct = sf_writef_float(_file, static_cast<const float *>(data), samples);
                                        break;

                                case AudioFormat::PCMI_S16LE:
                                case AudioFormat::PCMI_S16BE:
                                        ct = sf_writef_short(_file, static_cast<const int16_t *>(data), samples);
                                        break;

                                case AudioFormat::PCMI_S32LE:
                                case AudioFormat::PCMI_S32BE:
                                        ct = sf_writef_int(_file, static_cast<const int32_t *>(data), samples);
                                        break;

                                default: return Error::NotSupported;
                        }
                        (void)ct;
                        return Error::Ok;
                }

                Error read(PcmAudioPayload::Ptr &out, size_t samples) override {
                        if (_operation != AudioFile::Reader) {
                                promekiWarn("read: Attempt to read but in operation %d", _operation);
                                return Error::Invalid;
                        }
                        if (_file == nullptr) {
                                promekiWarn("read: Attempt to read but not open");
                                return Error::NotOpen;
                        }
                        const size_t bytes = _desc.bufferSize(samples);
                        auto         buf = Buffer(bytes);
                        buf.setSize(bytes);
                        void *raw = buf.data();

                        sf_count_t ct = 0;
                        switch (_desc.format().id()) {
                                case AudioFormat::PCMI_Float32LE:
                                case AudioFormat::PCMI_Float32BE:
                                        ct = sf_readf_float(_file, static_cast<float *>(raw),
                                                            static_cast<sf_count_t>(samples));
                                        break;

                                case AudioFormat::PCMI_S16LE:
                                case AudioFormat::PCMI_S16BE:
                                        ct = sf_readf_short(_file, static_cast<int16_t *>(raw),
                                                            static_cast<sf_count_t>(samples));
                                        break;

                                case AudioFormat::PCMI_S32LE:
                                case AudioFormat::PCMI_S32BE:
                                        ct = sf_readf_int(_file, static_cast<int32_t *>(raw),
                                                          static_cast<sf_count_t>(samples));
                                        break;

                                default: return Error::NotSupported;
                        }
                        if (ct == 0) return Error::EndOfFile;
                        const size_t actualBytes = _desc.bufferSize(static_cast<size_t>(ct));
                        buf.setSize(actualBytes);
                        BufferView planes;
                        planes.pushToBack(buf, 0, actualBytes);
                        out = PcmAudioPayload::Ptr::create(_desc, static_cast<size_t>(ct), planes);
                        return Error::Ok;
                }

        private:
                SNDFILE *_file = nullptr;
                SF_INFO  _info;
};

class AudioFileFactory_LibSndFile : public AudioFileFactory {
        public:
                AudioFileFactory_LibSndFile() {
                        _name = "libsndfile";
                        // libsndfile's optional codecs (Vorbis, FLAC, Opus,
                        // MPEG, …) are compile-time toggles; a stripped
                        // build still declares the extensions in its
                        // headers but won't actually open them.  Build
                        // _exts by intersecting the formats this code
                        // knows how to drive against the major-format
                        // list libsndfile reports for the running build.
                        // The result is that callers see a clean lookup
                        // miss (and a planner Skip) instead of a runtime
                        // open failure for codecs that simply aren't
                        // there.
                        //
                        // The flac/mp3/ogg entries each ride on the
                        // matching PROMEKI_ENABLE_* gate so that when the
                        // codec is compiled out of libsndfile (and the
                        // wider build), the factory advertises a smaller
                        // extension set and consumers fall through to
                        // another backend (or fail cleanly).
                        StringList candidates = {"wav", "bwf", "aiff", "aif"};
#if PROMEKI_ENABLE_FLAC
                        candidates.pushToBack("flac");
#endif
#if PROMEKI_ENABLE_VORBIS
                        candidates.pushToBack("ogg");
                        candidates.pushToBack("oga");
#endif
#if PROMEKI_ENABLE_MP3
                        candidates.pushToBack("mp3");
                        candidates.pushToBack("mpeg");
#endif
                        StringList majors = enumerateLibSndMajorExts();
                        for (const auto &ext : candidates) {
                                // libsndfile only reports one canonical
                                // extension per major format, so we
                                // collapse aliases into that canonical
                                // string before probing.  Mapping:
                                //   bwf  -> wav   (BWF rides WAV major)
                                //   aif  -> aiff  (file-suffix alias)
                                //   ogg  -> oga   (libsndfile reports "oga")
                                //   mp3  -> m1a   (libsndfile reports "m1a"
                                //                  for MPEG-1/2 Audio)
                                //   mpeg -> m1a   (same)
                                String probe = ext;
                                if (ext == "bwf") probe = "wav";
                                else if (ext == "aif") probe = "aiff";
                                else if (ext == "ogg") probe = "oga";
                                else if (ext == "mp3" || ext == "mpeg") probe = "m1a";
                                if (majors.contains(probe)) _exts.pushToBack(ext);
                        }
                }

                static StringList enumerateLibSndMajorExts() {
                        StringList out;
                        int        count = 0;
                        if (sf_command(nullptr, SFC_GET_FORMAT_MAJOR_COUNT, &count, sizeof(count)) != 0) {
                                return out;
                        }
                        for (int i = 0; i < count; ++i) {
                                SF_FORMAT_INFO info;
                                std::memset(&info, 0, sizeof(info));
                                info.format = i;
                                if (sf_command(nullptr, SFC_GET_FORMAT_MAJOR, &info, sizeof(info)) != 0) {
                                        continue;
                                }
                                if (info.extension == nullptr) continue;
                                String ext = String(info.extension).toLower();
                                if (!out.contains(ext)) out.pushToBack(ext);
                        }
                        return out;
                }

                ~AudioFileFactory_LibSndFile() {}

                bool canDoOperation(const Context &ctx) const override {
                        // Check extension from filename or format hint.
                        bool extOk = false;
                        if (!ctx.filename.isEmpty()) extOk = isExtensionSupported(ctx.filename);
                        if (!extOk && !ctx.formatHint.isEmpty()) extOk = isHintSupported(ctx.formatHint);
                        // If an IODevice is provided for reading with no extension
                        // info, try probing the device.
                        if (!extOk && ctx.device != nullptr && ctx.operation == AudioFile::Reader) {
                                extOk = probeDevice(ctx.device);
                        }
                        if (!extOk) return false;
                        switch (ctx.operation) {
                                case AudioFile::Reader:
                                        // If we have a filename and no device, probe the file.
                                        if (ctx.device == nullptr && !ctx.filename.isEmpty()) {
                                                return fileIsReadable(ctx.filename);
                                        }
                                        return true;
                                case AudioFile::Writer: return true;
                        }
                        return false;
                }

                Result<AudioFile> createForOperation(const Context &ctx) const override {
                        auto op = static_cast<AudioFile::Operation>(ctx.operation);
                        return makeResult(AudioFile(new AudioFile_LibSndFile(op)));
                }

                bool fileIsReadable(const String &fn) const {
                        SF_INFO info;
                        std::memset(&info, 0, sizeof(info));
                        SNDFILE *file = sf_open(fn.cstr(), SFM_READ, &info);
                        if (file == nullptr) return false;
                        bool readable = info.channels > 0 && info.samplerate > 0 && info.format != 0;
                        sf_close(file);
                        return readable;
                }

                bool probeDevice(IODevice *dev) const {
                        if (dev == nullptr || dev->isSequential()) return false;
                        // Save position, attempt sf_open_virtual, restore position.
                        int64_t savedPos = dev->pos();
                        SF_INFO info;
                        std::memset(&info, 0, sizeof(info));
                        SNDFILE *file = sf_open_virtual(&sfVirtualIO, SFM_READ, &info, dev);
                        bool     ok = (file != nullptr);
                        if (file != nullptr) sf_close(file);
                        dev->seek(savedPos);
                        return ok;
                }
};

PROMEKI_REGISTER_AUDIOFILE_FACTORY(AudioFileFactory_LibSndFile);

PROMEKI_NAMESPACE_END
