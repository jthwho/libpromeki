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

Metadata Metadata::fromJson(const JsonObject &json, Error *err) {
        Metadata ret;
        bool good = true;
        json.forEach([&good, &ret](const String &key, const Variant &val) {
                if(!val.isValid()) {
                        promekiWarn("Metadata::fromJson() key '%s' has invalid value.  Will ignore.", key.cstr());
                        good = false;
                        return;
                }
                ret.set(ID(key), val);
        });
        if(err) *err = good ? Error::Ok : Error::Invalid;
        return ret;
}

StringList Metadata::dump() const {
        StringList ret;
        forEach([&ret](ID id, const Variant &value) {
                String s = id.name();
                s += " [";
                s += value.typeName();
                s += "]: ";
                s += value.get<String>();
                ret += s;
        });
        return ret;
}

bool Metadata::operator==(const Metadata &other) const {
        return static_cast<const Base &>(*this) == static_cast<const Base &>(other);
}

PROMEKI_NAMESPACE_END
