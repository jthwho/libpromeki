/**
 * @file      audiofile_libsndfile.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <sndfile.h>
#include <promeki/audiofilefactory.h>
#include <promeki/audiofile.h>
#include <promeki/iodevice.h>
#include <promeki/file.h>
#include <promeki/logger.h>
#include <promeki/util.h>

PROMEKI_NAMESPACE_BEGIN

struct MetadataMap {
        Metadata::ID    metadataID;
        int             sndfileID;
};

static const MetadataMap metadataMap[] = {
        { Metadata::Title,              SF_STR_TITLE },
        { Metadata::Copyright,          SF_STR_COPYRIGHT },
        { Metadata::Software,           SF_STR_SOFTWARE },
        { Metadata::Artist,             SF_STR_ARTIST },
        { Metadata::Comment,            SF_STR_COMMENT },
        { Metadata::Date,               SF_STR_DATE },
        { Metadata::Album,              SF_STR_ALBUM },
        { Metadata::License,            SF_STR_LICENSE },
        { Metadata::TrackNumber,        SF_STR_TRACKNUMBER },
        { Metadata::Genre,              SF_STR_GENRE }
};

// SF_VIRTUAL_IO callbacks — user_data is IODevice*.

static sf_count_t sfvio_get_filelen(void *user_data) {
        auto *dev = static_cast<IODevice *>(user_data);
        auto [sz, err] = dev->size();
        if(err.isError()) return -1;
        return static_cast<sf_count_t>(sz);
}

static sf_count_t sfvio_seek(sf_count_t offset, int whence, void *user_data) {
        auto *dev = static_cast<IODevice *>(user_data);
        int64_t absPos = 0;
        switch(whence) {
                case SEEK_SET:
                        absPos = offset;
                        break;
                case SEEK_CUR:
                        absPos = dev->pos() + offset;
                        break;
                case SEEK_END: {
                        auto [sz, err] = dev->size();
                        if(err.isError()) return -1;
                        absPos = sz + offset;
                        break;
                }
                default:
                        return -1;
        }
        Error err = dev->seek(absPos);
        if(err.isError()) return -1;
        return static_cast<sf_count_t>(dev->pos());
}

static sf_count_t sfvio_read(void *ptr, sf_count_t count, void *user_data) {
        auto *dev = static_cast<IODevice *>(user_data);
        int64_t ret = dev->read(ptr, static_cast<int64_t>(count));
        if(ret < 0) return 0;
        return static_cast<sf_count_t>(ret);
}

static sf_count_t sfvio_write(const void *ptr, sf_count_t count, void *user_data) {
        auto *dev = static_cast<IODevice *>(user_data);
        int64_t ret = dev->write(ptr, static_cast<int64_t>(count));
        if(ret < 0) return 0;
        return static_cast<sf_count_t>(ret);
}

static sf_count_t sfvio_tell(void *user_data) {
        auto *dev = static_cast<IODevice *>(user_data);
        return static_cast<sf_count_t>(dev->pos());
}

static SF_VIRTUAL_IO sfVirtualIO = {
        sfvio_get_filelen,
        sfvio_seek,
        sfvio_read,
        sfvio_write,
        sfvio_tell
};

class AudioFile_LibSndFile : public AudioFile::Impl {
        PROMEKI_SHARED_DERIVED(AudioFile::Impl, AudioFile_LibSndFile)
        public:
                AudioFile_LibSndFile(AudioFile::Operation op) : AudioFile::Impl(op) {
                        memset(&_info, 0, sizeof(_info));
                }

                ~AudioFile_LibSndFile() {
                        close();
                }

                String effectiveExtension() const {
                        // Prefer format hint, fall back to filename extension.
                        if(!_formatHint.isEmpty()) return _formatHint.toLower();
                        size_t dot = _filename.rfind('.');
                        if(dot == String::npos || dot + 1 >= _filename.size()) return String();
                        return _filename.mid(dot + 1).toLower();
                }

                int computeLibSndFormat() {
                        int ret = 0;
                        bool pcm = false;
                        String ext = effectiveExtension();
                        if(ext == "wav" || ext == "bwf") {
                                ret |= SF_FORMAT_WAV;
                                pcm = true;
                        } else if(ext == "aif" || ext == "aiff") {
                                ret |= SF_FORMAT_AIFF;
                                pcm = true;
                        } else if(ext == "ogg") {
                                ret |= (SF_FORMAT_OGG | SF_FORMAT_VORBIS);
                                pcm = false;
                        } else {
                                promekiWarn("computeLibSndFormat: invalid extension '%s'", ext.cstr());
                                return 0;
                        }
                        if(pcm) {
                                switch(_desc.format().id()) {
                                        case AudioFormat::PCMI_Float32LE: ret |= (SF_FORMAT_FLOAT | SF_ENDIAN_LITTLE); break;
                                        case AudioFormat::PCMI_Float32BE: ret |= (SF_FORMAT_FLOAT | SF_ENDIAN_BIG); break;
                                        case AudioFormat::PCMI_S8: ret |= SF_FORMAT_PCM_S8; break;
                                        case AudioFormat::PCMI_U8: ret |= SF_FORMAT_PCM_U8; break;
                                        case AudioFormat::PCMI_S16LE: ret |= (SF_FORMAT_PCM_16 | SF_ENDIAN_LITTLE); break;
                                        case AudioFormat::PCMI_S16BE: ret |= (SF_FORMAT_PCM_16 | SF_ENDIAN_BIG); break;
                                        case AudioFormat::PCMI_S24LE: ret |= (SF_FORMAT_PCM_24 | SF_ENDIAN_LITTLE); break;
                                        case AudioFormat::PCMI_S24BE: ret |= (SF_FORMAT_PCM_24 | SF_ENDIAN_BIG); break;
                                        case AudioFormat::PCMI_S32LE: ret |= (SF_FORMAT_PCM_32 | SF_ENDIAN_LITTLE); break;
                                        case AudioFormat::PCMI_S32BE: ret |= (SF_FORMAT_PCM_32 | SF_ENDIAN_BIG); break;
                                        default:
                                                promekiWarn("computeLibSndFormat: incompatible audio desc: %s",
                                                        _desc.toString().cstr());
                                        return 0;
                                }
                        }
                        return ret;
                }

                void writeBroadcastInfo() {
                        if(_file == nullptr || _operation != AudioFile::Writer) return;
                        if(!_desc.metadata().contains(Metadata::EnableBWF)) return;
                        if(!_desc.metadata().get(Metadata::EnableBWF).get<bool>()) return;
                        SF_BROADCAST_INFO bcinfo;
                        Timecode tc = _desc.metadata().get(Metadata::Timecode).get<Timecode>();
                        double frameRate = _desc.metadata().get(Metadata::FrameRate).get<double>();
                        FrameNumber fn = tc.toFrameNumber();
                        uint64_t timeref = fn.isValid()
                                ? static_cast<uint64_t>(fn.value()) * static_cast<uint64_t>(frameRate * _desc.sampleRate())
                                : 0u;
                        DateTime datetime = _desc.metadata().contains(Metadata::OriginationDateTime) ?
                                _desc.metadata().get(Metadata::OriginationDateTime).get<DateTime>() :
                                DateTime::now();
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
                        strncpy(bcinfo.coding_history, codingHistory.cstr(), sizeof(bcinfo.coding_history));
                        strncpy(bcinfo.originator, originator.cstr(), sizeof(bcinfo.originator));
                        strncpy(bcinfo.originator_reference, originatorReference.cstr(), sizeof(bcinfo.originator_reference));
                        strncpy(bcinfo.description, description.cstr(), sizeof(bcinfo.description));
                        strncpy(bcinfo.origination_date, date.cstr(), sizeof(bcinfo.origination_date));
                        strncpy(bcinfo.origination_time, time.cstr(), sizeof(bcinfo.origination_time));
                        int ret = sf_command(_file, SFC_SET_BROADCAST_INFO, &bcinfo, sizeof(bcinfo));
                        if(ret != SF_TRUE) {
                                promekiWarn("writeBroadcastInfo: failed to write broadcast info");
                        }
                        return;
                }

                AudioDesc computeDesc() {
                        int major = _info.format & SF_FORMAT_TYPEMASK;
                        int sub   = _info.format & SF_FORMAT_SUBMASK;
                        int endian = _info.format & SF_FORMAT_ENDMASK;

                        // Determine default endianness from the major type.
                        bool le = true;
                        if(endian == SF_ENDIAN_LITTLE) {
                                le = true;
                        } else if(endian == SF_ENDIAN_BIG) {
                                le = false;
                        } else {
                                // SF_ENDIAN_FILE: WAV is LE, AIFF is BE.
                                le = (major != SF_FORMAT_AIFF);
                        }

                        AudioFormat::ID dt = AudioFormat::Invalid;
                        switch(sub) {
                                case SF_FORMAT_PCM_S8:
                                        dt = AudioFormat::PCMI_S8;
                                        break;
                                case SF_FORMAT_PCM_U8:
                                        dt = AudioFormat::PCMI_U8;
                                        break;
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
                                default:
                                        promekiWarn("computeDesc: unsupported subformat 0x%x", sub);
                                        return AudioDesc();
                        }
                        return AudioDesc(dt, _info.samplerate, _info.channels);
                }

                Error ensureDevice() {
                        if(_device != nullptr) return Error::Ok;
                        // No device set — create a File IODevice from the filename.
                        if(_filename.isEmpty()) {
                                promekiWarn("ensureDevice: no device and no filename");
                                return Error::InvalidArgument;
                        }
                        auto *file = new File(_filename);
                        _device = file;
                        _ownsDevice = true;
                        // Open the device in the appropriate mode.
                        IODevice::OpenMode mode = IODevice::NotOpen;
                        switch(_operation) {
                                case AudioFile::Reader:
                                        mode = IODevice::ReadOnly;
                                        break;
                                case AudioFile::Writer:
                                        mode = IODevice::ReadWrite;
                                        break;
                                default:
                                        break;
                        }
                        int fileFlags = 0;
                        if(_operation == AudioFile::Writer) {
                                fileFlags = File::Create | File::Truncate;
                        }
                        Error err = file->open(mode, fileFlags);
                        if(err.isError()) {
                                promekiWarn("ensureDevice: failed to open '%s': %s",
                                        _filename.cstr(), err.name().cstr());
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
                        if(_file != nullptr) {
                                int ret = sf_close(_file);
                                if(ret) promekiWarn("close: sf_close failed with %d", ret);
                                _file = nullptr;
                        }
                        if(_ownsDevice && _device != nullptr) {
                                if(_device->isOpen()) _device->close();
                                delete _device;
                                _device = nullptr;
                                _ownsDevice = false;
                        }
                }

                size_t sampleCount() const override {
                        return _info.frames;
                }

                Error seekToSample(size_t sample) override {
                        if(_file == nullptr) return Error::NotOpen;
                        sf_count_t ret = sf_seek(_file, static_cast<sf_count_t>(sample), SEEK_SET);
                        if(ret < 0) return Error::IllegalSeek;
                        return Error::Ok;
                }

                Error open() override {
                        if(_file != nullptr) {
                                promekiWarn("open: Attempt to open again");
                                return Error::AlreadyOpen;
                        }
                        // Reject sequential devices.
                        if(_device != nullptr && _device->isSequential()) {
                                return Error::NotSupported;
                        }
                        int mode = 0;
                        switch(_operation) {
                                case AudioFile::Reader: {
                                        mode = SFM_READ;
                                }
                                break;

                                case AudioFile::Writer: {
                                        mode = SFM_WRITE;
                                        if(!_desc.isValid()) {
                                                promekiWarn("open: Can't open for write, desc isn't valid");
                                                return Error::OpenFailed;
                                        }
                                        std::memset(&_info, 0, sizeof(_info));
                                        _info.samplerate = _desc.sampleRate();
                                        _info.channels = _desc.channels();
                                        _info.format = computeLibSndFormat();
                                        if(_info.format == 0) {
                                                promekiWarn("open: Can't write, format not supported");
                                                return Error::NotSupported;
                                        }
                                }
                                break;

                                default:
                                        promekiWarn("open: operation %d not supported", _operation);
                                        return Error::NotSupported;
                        }
                        // Ensure we have a device (create File if needed).
                        Error devErr = ensureDevice();
                        if(devErr.isError()) return devErr;
                        // All paths go through sf_open_virtual().
                        _file = sf_open_virtual(&sfVirtualIO, mode, &_info, _device);
                        if(_file == nullptr) {
                                promekiWarn("open: sf_open_virtual failed: %s", sf_strerror(nullptr));
                                return Error::OpenFailed;
                        }
                        switch(_operation) {
                                case AudioFile::Reader:
                                        _desc = computeDesc();
                                        if(!_desc.isValid()) {
                                                promekiWarn("open: file data isn't a supported format");
                                                return Error::NotSupported;
                                        }
                                        break;

                                case AudioFile::Writer:
                                        for(size_t i = 0; i < PROMEKI_ARRAY_SIZE(metadataMap); ++i) {
                                                auto &m = _desc.metadata();
                                                const auto &map = metadataMap[i];
                                                if(m.contains(map.metadataID)) {
                                                        int ret = sf_set_string(_file, map.sndfileID,
                                                                m.get(map.metadataID).get<String>().cstr());
                                                        if(ret) promekiWarn("open: Failed to write metadata %s",
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

                Error write(const UncompressedAudioPayload &payload) override {
                        if(_operation != AudioFile::Writer) {
                                promekiWarn("write: Attempt to write but in operation %d", _operation);
                                return Error::Invalid;
                        }
                        if(_file == nullptr) {
                                promekiWarn("write: Attempt to write but not open");
                                return Error::NotOpen;
                        }
                        if(!payload.desc().formatEquals(_desc)) {
                                promekiWarn("write: Attempt to write with '%s', but set to '%s'",
                                        payload.desc().toString().cstr(),
                                        _desc.toString().cstr());
                                return Error::Invalid;
                        }
                        if(payload.planeCount() == 0) return Error::Invalid;
                        auto view = payload.plane(0);
                        const void *data = view.data();
                        const sf_count_t samples = static_cast<sf_count_t>(payload.sampleCount());
                        sf_count_t ct = 0;
                        switch(_desc.format().id()) {
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

                                default:
                                        return Error::NotSupported;
                        }
                        (void)ct;
                        return Error::Ok;
                }

                Error read(UncompressedAudioPayload::Ptr &out, size_t samples) override {
                        if(_operation != AudioFile::Reader) {
                                promekiWarn("read: Attempt to read but in operation %d", _operation);
                                return Error::Invalid;
                        }
                        if(_file == nullptr) {
                                promekiWarn("read: Attempt to read but not open");
                                return Error::NotOpen;
                        }
                        const size_t bytes = _desc.bufferSize(samples);
                        auto buf = Buffer::Ptr::create(bytes);
                        buf.modify()->setSize(bytes);
                        void *raw = buf.modify()->data();

                        sf_count_t ct = 0;
                        switch(_desc.format().id()) {
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

                                default:
                                        return Error::NotSupported;
                        }
                        if(ct == 0) return Error::EndOfFile;
                        const size_t actualBytes = _desc.bufferSize(static_cast<size_t>(ct));
                        buf.modify()->setSize(actualBytes);
                        BufferView planes;
                        planes.pushToBack(buf, 0, actualBytes);
                        out = UncompressedAudioPayload::Ptr::create(_desc,
                                static_cast<size_t>(ct), planes);
                        return Error::Ok;
                }

        private:
                SNDFILE         *_file = nullptr;
                SF_INFO         _info;
};

class AudioFileFactory_LibSndFile : public AudioFileFactory {
        public:
                AudioFileFactory_LibSndFile() {
                        _name = "libsndfile";
                        _exts = { "wav", "bwf", "aiff", "aif", "ogg" };
                }

                ~AudioFileFactory_LibSndFile() {

                }

                bool canDoOperation(const Context &ctx) const override {
                        // Check extension from filename or format hint.
                        bool extOk = false;
                        if(!ctx.filename.isEmpty()) extOk = isExtensionSupported(ctx.filename);
                        if(!extOk && !ctx.formatHint.isEmpty()) extOk = isHintSupported(ctx.formatHint);
                        // If an IODevice is provided for reading with no extension
                        // info, try probing the device.
                        if(!extOk && ctx.device != nullptr && ctx.operation == AudioFile::Reader) {
                                extOk = probeDevice(ctx.device);
                        }
                        if(!extOk) return false;
                        switch(ctx.operation) {
                                case AudioFile::Reader:
                                        // If we have a filename and no device, probe the file.
                                        if(ctx.device == nullptr && !ctx.filename.isEmpty()) {
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
                        if(file == nullptr) return false;
                        bool readable = info.channels > 0 &&
                                        info.samplerate > 0 &&
                                        info.format != 0;
                        sf_close(file);
                        return readable;
                }

                bool probeDevice(IODevice *dev) const {
                        if(dev == nullptr || dev->isSequential()) return false;
                        // Save position, attempt sf_open_virtual, restore position.
                        int64_t savedPos = dev->pos();
                        SF_INFO info;
                        std::memset(&info, 0, sizeof(info));
                        SNDFILE *file = sf_open_virtual(&sfVirtualIO, SFM_READ, &info, dev);
                        bool ok = (file != nullptr);
                        if(file != nullptr) sf_close(file);
                        dev->seek(savedPos);
                        return ok;
                }

};

PROMEKI_REGISTER_AUDIOFILE_FACTORY(AudioFileFactory_LibSndFile);

PROMEKI_NAMESPACE_END
