/*****************************************************************************
 * metadata.cpp
 * April 30, 2023
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

Metadata Metadata::fromJson(const JsonObject &json, bool *ok) {
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
    if(ok != nullptr) *ok = good;
    return ret;
}

StringList Metadata::dump() const {
        StringList ret;
        for(const auto &[id, value] : d) {
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

