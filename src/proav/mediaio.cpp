/**
 * @file      mediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaio.h>
#include <promeki/file.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

const MediaIO::ConfigID MediaIO::ConfigFilename("Filename");
const MediaIO::ConfigID MediaIO::ConfigType("Type");

static MediaIO::FormatDescList &formatRegistry() {
        static MediaIO::FormatDescList list;
        return list;
}

int MediaIO::registerFormat(const FormatDesc &desc) {
        FormatDescList &list = formatRegistry();
        int ret = list.size();
        list.pushToBack(desc);
        promekiInfo("Registered MediaIO '%s'", desc.name.cstr());
        return ret;
}

const MediaIO::FormatDescList &MediaIO::registeredFormats() {
        return formatRegistry();
}

static const MediaIO::FormatDesc *findFormatByName(const String &name) {
        const MediaIO::FormatDescList &list = formatRegistry();
        for(const auto &desc : list) {
                if(desc.name == name) return &desc;
        }
        return nullptr;
}

static String extractExtension(const String &filename) {
        size_t dot = filename.rfind('.');
        if(dot == String::npos || dot + 1 >= filename.size()) return String();
        return filename.mid(dot + 1).toLower();
}

static const MediaIO::FormatDesc *findFormatByExtension(const String &filename) {
        String ext = extractExtension(filename);
        if(ext.isEmpty()) return nullptr;
        const MediaIO::FormatDescList &list = formatRegistry();
        for(const auto &desc : list) {
                for(const auto &e : desc.extensions) {
                        if(ext == e) return &desc;
                }
        }
        return nullptr;
}

static const MediaIO::FormatDesc *findFormatForFileRead(const String &filename) {
        String ext = extractExtension(filename);
        const MediaIO::FormatDescList &list = formatRegistry();

        // Pass 1: extension match (fast path, always trusts the extension)
        if(!ext.isEmpty()) {
                for(const auto &desc : list) {
                        if(!desc.canRead) continue;
                        for(const auto &e : desc.extensions) {
                                if(ext == e) return &desc;
                        }
                }
        }

        // Pass 2: content-based probe via IODevice (for missing/wrong extensions)
        File probeFile(filename);
        if(probeFile.open(IODevice::ReadOnly).isError()) return nullptr;
        const MediaIO::FormatDesc *result = nullptr;
        for(const auto &desc : list) {
                if(!desc.canRead) continue;
                if(!desc.canHandleDevice) continue;
                probeFile.seek(0);
                if(desc.canHandleDevice(&probeFile)) {
                        result = &desc;
                        break;
                }
        }
        probeFile.close();
        return result;
}

MediaIO::Config MediaIO::defaultConfig(const String &typeName) {
        const FormatDesc *desc = findFormatByName(typeName);
        if(desc == nullptr || !desc->defaultConfig) return Config();
        Config cfg = desc->defaultConfig();
        cfg.set(ConfigType, typeName);
        return cfg;
}

MediaIO *MediaIO::create(const Config &config, ObjectBase *parent) {
        const FormatDesc *desc = nullptr;

        // Try explicit type first
        if(config.contains(ConfigType)) {
                String typeName = config.getAs<String>(ConfigType);
                desc = findFormatByName(typeName);
                if(desc == nullptr) {
                        promekiWarn("MediaIO::create: unknown type '%s'", typeName.cstr());
                        return nullptr;
                }
        }

        // Fall back to filename extension
        if(desc == nullptr && config.contains(ConfigFilename)) {
                String filename = config.getAs<String>(ConfigFilename);
                desc = findFormatByExtension(filename);
                if(desc == nullptr) {
                        promekiWarn("MediaIO::create: no backend for '%s'", filename.cstr());
                        return nullptr;
                }
        }

        if(desc == nullptr) {
                promekiWarn("MediaIO::create: config has neither Type nor Filename");
                return nullptr;
        }

        MediaIO *io = desc->create(parent);
        if(io == nullptr) {
                promekiWarn("MediaIO::create: factory for '%s' returned null", desc->name.cstr());
                return nullptr;
        }
        io->setConfig(config);
        return io;
}

MediaIO *MediaIO::createForFileRead(const String &filename, ObjectBase *parent) {
        const FormatDesc *desc = findFormatForFileRead(filename);
        if(desc == nullptr) {
                promekiWarn("MediaIO::createForFileRead: no backend for '%s'", filename.cstr());
                return nullptr;
        }
        if(!desc->canRead) {
                promekiWarn("MediaIO::createForFileRead: '%s' does not support reading", desc->name.cstr());
                return nullptr;
        }
        MediaIO *io = desc->create(parent);
        if(io == nullptr) return nullptr;
        Config config;
        config.set(ConfigFilename, filename);
        io->setConfig(config);
        return io;
}

MediaIO *MediaIO::createForFileWrite(const String &filename, ObjectBase *parent) {
        const FormatDesc *desc = findFormatByExtension(filename);
        if(desc == nullptr) {
                promekiWarn("MediaIO::createForFileWrite: no backend for '%s'", filename.cstr());
                return nullptr;
        }
        if(!desc->canWrite) {
                promekiWarn("MediaIO::createForFileWrite: '%s' does not support writing", desc->name.cstr());
                return nullptr;
        }
        MediaIO *io = desc->create(parent);
        if(io == nullptr) return nullptr;
        Config config;
        config.set(ConfigFilename, filename);
        io->setConfig(config);
        return io;
}

MediaIO::~MediaIO() {
        assert(!isOpen() && "MediaIO destroyed while still open — backend must call close() in its destructor");
}

Error MediaIO::open(Mode mode) {
        if(isOpen()) return Error::AlreadyOpen;
        if(mode == NotOpen) return Error::InvalidArgument;
        Error err = onOpen(mode);
        if(err.isOk()) _mode = mode;
        return err;
}

Error MediaIO::close() {
        if(!isOpen()) return Error::NotOpen;
        Error err = onClose();
        _mode = NotOpen;
        return err;
}

Error MediaIO::onOpen(Mode mode) {
        return Error::NotImplemented;
}

Error MediaIO::onClose() {
        return Error::Ok;
}

MediaDesc MediaIO::mediaDesc() const {
        return MediaDesc();
}

Error MediaIO::setMediaDesc(const MediaDesc &desc) {
        return Error::NotSupported;
}

FrameRate MediaIO::frameRate() const {
        return mediaDesc().frameRate();
}

AudioDesc MediaIO::audioDesc() const {
        MediaDesc md = mediaDesc();
        if(md.audioList().isEmpty()) return AudioDesc();
        return md.audioList()[0];
}

Error MediaIO::setAudioDesc(const AudioDesc &desc) {
        return Error::NotSupported;
}

Metadata MediaIO::metadata() const {
        return Metadata();
}

Error MediaIO::setMetadata(const Metadata &meta) {
        return Error::NotSupported;
}

Error MediaIO::readFrame(Frame &frame) {
        if(!isOpen()) return Error::NotOpen;
        if(_mode != Reader) return Error::NotSupported;
        return onReadFrame(frame);
}

Error MediaIO::writeFrame(const Frame &frame) {
        if(!isOpen()) return Error::NotOpen;
        if(_mode != Writer) return Error::NotSupported;
        return onWriteFrame(frame);
}

Error MediaIO::onReadFrame(Frame &frame) {
        return Error::NotSupported;
}

Error MediaIO::onWriteFrame(const Frame &frame) {
        return Error::NotSupported;
}

bool MediaIO::canSeek() const {
        return false;
}

Error MediaIO::seekToFrame(int64_t frameNumber) {
        return Error::IllegalSeek;
}

int64_t MediaIO::frameCount() const {
        return 0;
}

uint64_t MediaIO::currentFrame() const {
        return 0;
}

PROMEKI_NAMESPACE_END
