/**
 * @file      medianodeconfig.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/proav/medianodeconfig.h>

PROMEKI_NAMESPACE_BEGIN

const String MediaNodeConfig::KeyName("Name");
const String MediaNodeConfig::KeyType("Type");
const String MediaNodeConfig::KeyConnections("Connections");

MediaNodeConfig::MediaNodeConfig(const String &type, const String &name) {
        _options.insert(KeyName, Variant(name));
        _options.insert(KeyType, Variant(type));
        return;
}

String MediaNodeConfig::name() const {
        if(!_options.contains(KeyName)) return String();
        return _options[KeyName].get<String>();
}

void MediaNodeConfig::setName(const String &name) {
        _options[KeyName] = Variant(name);
        return;
}

String MediaNodeConfig::type() const {
        if(!_options.contains(KeyType)) return String();
        return _options[KeyType].get<String>();
}

void MediaNodeConfig::setType(const String &type) {
        _options[KeyType] = Variant(type);
        return;
}

StringList MediaNodeConfig::connections() const {
        if(!_options.contains(KeyConnections)) return StringList();
        return _options[KeyConnections].get<StringList>();
}

void MediaNodeConfig::setConnections(const StringList &connections) {
        _options[KeyConnections] = Variant(connections);
        return;
}

void MediaNodeConfig::addConnection(const String &connection) {
        StringList conns = connections();
        conns.pushToBack(connection);
        _options[KeyConnections] = Variant(conns);
        return;
}

void MediaNodeConfig::set(const String &key, const Variant &value) {
        _options[key] = value;
        return;
}

Variant MediaNodeConfig::get(const String &key, const Variant &defaultValue) const {
        if(!_options.contains(key)) return defaultValue;
        return _options[key];
}

bool MediaNodeConfig::contains(const String &key) const {
        return _options.contains(key);
}

bool MediaNodeConfig::remove(const String &key) {
        if(!_options.contains(key)) return false;
        _options.remove(key);
        return true;
}

bool MediaNodeConfig::isValid() const {
        return !name().isEmpty() && !type().isEmpty();
}

bool MediaNodeConfig::isStandardKey(const String &key) {
        return key == KeyName || key == KeyType || key == KeyConnections;
}

MediaNodeConfig::ParsedConnection MediaNodeConfig::parseConnection(const String &connStr, Error *err) {
        ParsedConnection result;
        if(err != nullptr) *err = Error::Ok;

        if(connStr.isEmpty()) {
                if(err != nullptr) *err = Error::Invalid;
                return result;
        }

        // Check for index form: "NodeName[Index]"
        size_t bracketOpen = connStr.find('[');
        if(bracketOpen != String::npos) {
                size_t bracketClose = connStr.find(']', bracketOpen);
                if(bracketClose == String::npos || bracketClose != connStr.length() - 1) {
                        if(err != nullptr) *err = Error::Invalid;
                        return result;
                }
                result.nodeName = connStr.left(bracketOpen);
                String indexStr = connStr.mid(bracketOpen + 1, bracketClose - bracketOpen - 1);
                Error convErr;
                result.sourceIndex = indexStr.toInt(&convErr);
                if(convErr.isError() || result.nodeName.isEmpty() || result.sourceIndex < 0) {
                        result = ParsedConnection();
                        if(err != nullptr) *err = Error::Invalid;
                }
                return result;
        }

        // Check for name form: "NodeName.SourceName"
        size_t dotPos = connStr.find('.');
        if(dotPos != String::npos) {
                result.nodeName = connStr.left(dotPos);
                result.sourceName = connStr.mid(dotPos + 1);
                if(result.nodeName.isEmpty() || result.sourceName.isEmpty()) {
                        result = ParsedConnection();
                        if(err != nullptr) *err = Error::Invalid;
                }
                return result;
        }

        // Bare name form: "NodeName" -> index 0
        result.nodeName = connStr;
        result.sourceIndex = 0;
        return result;
}

PROMEKI_NAMESPACE_END
