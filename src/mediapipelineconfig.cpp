/**
 * @file      mediapipelineconfig.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/proav/mediapipelineconfig.h>

PROMEKI_NAMESPACE_BEGIN

void MediaPipelineConfig::addNode(const MediaNodeConfig &config) {
        _nodes.pushToBack(config);
        return;
}

void MediaPipelineConfig::addNode(MediaNodeConfig &&config) {
        _nodes.pushToBack(std::move(config));
        return;
}

const MediaNodeConfig *MediaPipelineConfig::node(const String &name) const {
        for(const auto &cfg : _nodes) {
                if(cfg.name() == name) return &cfg;
        }
        return nullptr;
}

bool MediaPipelineConfig::isValid(Error *err) const {
        if(err != nullptr) *err = Error::Ok;

        if(_nodes.isEmpty()) {
                if(err != nullptr) *err = Error::Invalid;
                return false;
        }

        // Check each node config is valid and collect names
        StringList names;
        for(const auto &cfg : _nodes) {
                if(!cfg.isValid()) {
                        if(err != nullptr) *err = Error::Invalid;
                        return false;
                }
                if(names.indexOf(cfg.name()) >= 0) {
                        if(err != nullptr) *err = Error::Exists;
                        return false;
                }
                names.pushToBack(cfg.name());
        }

        // Check all connection targets reference existing node names
        for(const auto &cfg : _nodes) {
                StringList conns = cfg.connections();
                for(const auto &conn : conns) {
                        if(conn.isEmpty()) continue;
                        MediaNodeConfig::ParsedConnection pc = MediaNodeConfig::parseConnection(conn);
                        if(!pc.isValid()) {
                                if(err != nullptr) *err = Error::Invalid;
                                return false;
                        }
                        if(names.indexOf(pc.nodeName) < 0) {
                                if(err != nullptr) *err = Error::NotExist;
                                return false;
                        }
                }
        }

        return true;
}

PROMEKI_NAMESPACE_END
