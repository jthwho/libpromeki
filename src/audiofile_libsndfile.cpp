/*****************************************************************************
 * audiofile_libsndfile.cpp
 * May 18, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

#include <cstring>
#include <sndfile.h>
#include <promeki/audiofilefactory.h>
#include <promeki/audiofile.h>
#include <promeki/logger.h>
#include <promeki/fileinfo.h>
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

class AudioFile_LibSndFile : public AudioFile::Impl {
        public:
                AudioFile_LibSndFile(AudioFile::Operation op) : AudioFile::Impl(op) {
                        memset(&_info, 0, sizeof(_info));
                }

                ~AudioFile_LibSndFile() {
                        close();
                }

                int computeLibSndFormat() {
                        int ret = 0;
                        bool pcm = false;
                        String ext = FileInfo(_filename).suffix().toLower();
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
                                promekiWarn("%s: invalid extension", _filename.cstr());
                                return 0;
                        }
                        if(pcm) {
                                switch(_desc.dataType()) {
                                        case AudioDesc::PCMI_Float32LE: ret |= (SF_FORMAT_FLOAT | SF_ENDIAN_LITTLE); break;
                                        case AudioDesc::PCMI_Float32BE: ret |= (SF_FORMAT_FLOAT | SF_ENDIAN_BIG); break;
                                        case AudioDesc::PCMI_S8: ret |= SF_FORMAT_PCM_S8; break;
                                        case AudioDesc::PCMI_U8: ret |= SF_FORMAT_PCM_U8; break;
                                        case AudioDesc::PCMI_S16LE: ret |= (SF_FORMAT_PCM_16 | SF_ENDIAN_LITTLE); break;
                                        case AudioDesc::PCMI_S16BE: ret |= (SF_FORMAT_PCM_16 | SF_ENDIAN_BIG); break;
                                        case AudioDesc::PCMI_S24LE: ret |= (SF_FORMAT_PCM_24 | SF_ENDIAN_LITTLE); break;
                                        case AudioDesc::PCMI_S24BE: ret |= (SF_FORMAT_PCM_24 | SF_ENDIAN_BIG); break;
                                        case AudioDesc::PCMI_S32LE: ret |= (SF_FORMAT_PCM_32 | SF_ENDIAN_LITTLE); break;
                                        case AudioDesc::PCMI_S32BE: ret |= (SF_FORMAT_PCM_32 | SF_ENDIAN_BIG); break;
                                        default:
                                                promekiWarn("%s: incompatible audio desc: %s\n", 
                                                        _filename.cstr(), _desc.toString().cstr());
                                        return 0;
                                }
                        }
                        return ret;
                }

                void writeBroadcastInfo() {
                        if(_file == nullptr || _operation != AudioFile::Writer) return;
                        if(!_desc.metadata().get(Metadata::EnableBWF).get<bool>()) return;
                        SF_BROADCAST_INFO bcinfo;
                        Timecode tc = _desc.metadata().get(Metadata::Timecode).get<Timecode>();
                        double frameRate = _desc.metadata().get(Metadata::FrameRate).get<double>();
                        uint64_t timeref = tc.toFrameNumber().first * frameRate * _desc.sampleRate();
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
                                promekiWarn("%s: failed to write broadcast info", _filename.cstr());
                        }
                        return;
                }

                AudioDesc computeDesc() {
                        return AudioDesc();
                }

                void close() override {
                        if(_file == nullptr) return;
                        int ret = sf_close(_file);
                        if(ret) promekiWarn("%s: sf_close failed with %d", _filename.cstr(), ret);
                        _file = nullptr;
                }

                size_t sampleCount() const override {
                        return _info.frames;
                }

                Error open() override {
                        if(_file != nullptr) {
                                promekiWarn("%s: Attempt to open again", _filename.cstr());
                                return Error::AlreadyOpen;
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
                                                promekiWarn("%s: Can't open for write, desc isn't valid", _filename.cstr());
                                                return Error::OpenFailed;
                                        }
                                        std::memset(&_info, 0, sizeof(_info));
                                        _info.samplerate = _desc.sampleRate();
                                        _info.channels = _desc.channels();
                                        _info.format = computeLibSndFormat();
                                        if(_info.format == 0) {
                                                promekiWarn("%s: Can't write, format not supported", _filename.cstr());
                                                return Error::NotSupported;
                                        }
                                }
                                break;

                                default:
                                        promekiWarn("%s: Can't open, operation %d not supported", _filename.cstr(), _operation);
                                        return Error::NotSupported;
                        }
                        _file = sf_open(_filename.cstr(), mode, &_info);
                        if(_file == nullptr) {
                                promekiWarn("%s: Failed to open: %s", _filename.cstr(), sf_strerror(nullptr));
                                return Error::OpenFailed;
                        }
                        switch(_operation) {
                                case AudioFile::Reader:
                                        _desc = computeDesc();
                                        if(!_desc.isValid()) {
                                                promekiWarn("%s: Can't open, file data isn't a supported format", _filename.cstr());
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
                                                        if(ret) promekiWarn("%s: Failed to write metadata %d", 
                                                                _filename.cstr(), map.metadataID);
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

                Error write(const Audio &audio) override {
                        if(_operation != AudioFile::Writer) {
                                promekiWarn("%s: Attempt to write but in operation %d", _filename.cstr(), _operation);
                                return Error::Invalid;
                        }
                        if(_file == nullptr) {
                                promekiWarn("%s: Attempt to write but not open", _filename.cstr());
                                return Error::NotOpen;
                        }
                        if(audio.desc() != _desc) {
                                promekiWarn("%s: Attempt to write with '%s', but set to '%s'",
                                        _filename.cstr(), audio.desc().toString().cstr(),
                                        _desc.toString().cstr());
                                return Error::Invalid;
                        }
                        sf_count_t ct = 0;
                        switch(_desc.dataType()) {
                                case AudioDesc::PCMI_Float32LE:
                                case AudioDesc::PCMI_Float32BE:
                                        ct = sf_writef_float(_file, audio.data<float>(), audio.samples());
                                        break;

                                case AudioDesc::PCMI_S16LE:
                                case AudioDesc::PCMI_S16BE:
                                        ct = sf_writef_short(_file, audio.data<int16_t>(), audio.samples());
                                        break;

                                case AudioDesc::PCMI_S32LE:
                                case AudioDesc::PCMI_S32BE:
                                        ct = sf_writef_int(_file, audio.data<int32_t>(), audio.samples());
                                        break;

                                default:
                                        return Error::NotSupported;
                        }
                        return Error::Ok;
                }

                Error read(Audio &out, size_t samples) override {
                        if(_operation != AudioFile::Reader) {
                                promekiWarn("%s: Attempt to read but in operation %d", _filename.cstr(), _operation);
                                return Error::Invalid;
                        }
                        if(_file == nullptr) {
                                promekiWarn("%s: Attempt to read to but not open", _filename.cstr());
                                return Error::NotOpen;
                        }
                        Audio audio(_desc, samples);

                        sf_count_t ct = 0;
                        switch(_desc.dataType()) {
                                case AudioDesc::PCMI_Float32LE:
                                case AudioDesc::PCMI_Float32BE:
                                        ct = sf_readf_float(_file, audio.data<float>(), audio.samples());
                                        break;

                                case AudioDesc::PCMI_S16LE:
                                case AudioDesc::PCMI_S16BE:
                                        ct = sf_readf_short(_file, audio.data<int16_t>(), audio.samples());
                                        break;

                                case AudioDesc::PCMI_S32LE:
                                case AudioDesc::PCMI_S32BE:
                                        ct = sf_readf_int(_file, audio.data<int32_t>(), audio.samples());
                                        break;

                                default:
                                        return Error::NotSupported;
                        }
                        if(ct == 0) return Error::EndOfFile;
                        audio.resize(ct);
                        out = audio;
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

                bool canDoOperation(int operation, const String &filename) const override {
                        bool ret = false;
                        if(!isExtensionSupported(filename)) return false;
                        switch(operation) {
                                case AudioFile::Reader: ret = fileIsReadable(filename); break;
                                case AudioFile::Writer: ret = true;
                        }
                        return ret;
                }

                AudioFile createForOperation(int operation) const override {
                        return new AudioFile_LibSndFile(static_cast<AudioFile::Operation>(operation));
                }

                bool fileIsReadable(const String &fn) const {
                        SF_INFO info;
                        SNDFILE *file = sf_open(fn.cstr(), SFM_READ, &info);
                        if(file == nullptr) return false;
                        // FIXME: Have a look at the info struct and decide if we actually can read it.
                        sf_close(file);
                        return false;
                }

};

PROMEKI_REGISTER_AUDIOFILE_FACTORY(AudioFileFactory_LibSndFile);

PROMEKI_NAMESPACE_END

