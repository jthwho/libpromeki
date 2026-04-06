/**
 * @file      mediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaio.h>
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

static const MediaIO::FormatDesc *findFormatByExtension(const String &filename) {
        size_t dot = filename.rfind('.');
        if(dot == String::npos || dot + 1 >= filename.size()) return nullptr;
        String ext = filename.mid(dot + 1).toLower();
        const MediaIO::FormatDescList &list = formatRegistry();
        for(const auto &desc : list) {
                for(const auto &e : desc.extensions) {
                        if(ext == e) return &desc;
                }
        }
        return nullptr;
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
        const FormatDesc *desc = findFormatByExtension(filename);
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
}

Error MediaIO::open(Mode mode) {
        return Error::NotImplemented;
}

Error MediaIO::close() {
        return Error::Ok;
}

VideoDesc MediaIO::videoDesc() const {
        return VideoDesc();
}

Error MediaIO::setVideoDesc(const VideoDesc &desc) {
        return Error::NotSupported;
}

Metadata MediaIO::metadata() const {
        return Metadata();
}

Error MediaIO::setMetadata(const Metadata &meta) {
        return Error::NotSupported;
}

Error MediaIO::readFrame(Frame &frame) {
        return Error::NotSupported;
}

Error MediaIO::writeFrame(const Frame &frame) {
        return Error::NotSupported;
}

bool MediaIO::canSeek() const {
        return false;
}

Error MediaIO::seekToFrame(uint64_t frameNumber) {
        return Error::IllegalSeek;
}

uint64_t MediaIO::frameCount() const {
        return 0;
}

uint64_t MediaIO::currentFrame() const {
        return 0;
}

PROMEKI_NAMESPACE_END
