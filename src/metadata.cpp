/**
 * @file      metadata.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/metadata.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

#define X(name, type) { Metadata::name, PROMEKI_STRINGIFY(name) },
static std::map<Metadata::ID, String> metadataIDToString = { PROMEKI_ENUM_METADATA_ID };
#undef X

#define X(name, type) { PROMEKI_STRINGIFY(name), Metadata::name },
static std::map<String, Metadata::ID> metadataStringToID = { PROMEKI_ENUM_METADATA_ID };
#undef X

const String &Metadata::idToString(ID id) {
        return metadataIDToString[id];
}

Metadata::ID Metadata::stringToID(const String &val) {
    return metadataStringToID[val];
}

Metadata Metadata::fromJson(const JsonObject &json, Error *err) {
    Metadata ret;
    bool good = true;
    json.forEach([&good, &ret](const String &key, const Variant &val) {
        ID id = stringToID(key);
        Variant mval;
        if(id == Invalid) {
            promekiWarn("Metadata::fromJson() got invalid key '%s'.  Will ignore.", key.cstr());
            good = false;
            return;
        }

#define X(name, type) case name: mval = val.get<type>(); break;
        switch(id) {
            PROMEKI_ENUM_METADATA_ID
            default: mval = val;
        }
#undef X
        if(!mval.isValid()) {
            promekiWarn("Metadata::fromJson() key '%s', data '%s' failed to convert to native type", key.cstr(), val.get<String>().cstr());
            good = false;
            return;
        }
        ret.set(id, mval);
        return;
    });
    if(err != nullptr) *err = good ? Error::Ok : Error::Invalid;
    return ret;
}

StringList Metadata::dump() const {
        StringList ret;
        for(const auto &[id, value] : _map) {
                String s = idToString(id);
                s += " [";
                s += value.typeName();
                s += "]: ";
                s += value.get<String>();
                ret += s;
        }
        return ret;
}

PROMEKI_NAMESPACE_END

