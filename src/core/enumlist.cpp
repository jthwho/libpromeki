/**
 * @file      enumlist.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/enumlist.h>

PROMEKI_NAMESPACE_BEGIN

bool EnumList::append(const Enum &e, Error *err) {
        if(!_type.isValid()) {
                if(err) *err = Error::InvalidArgument;
                return false;
        }
        if(e.type() != _type) {
                if(err) *err = Error::InvalidArgument;
                return false;
        }
        _values.pushToBack(e.value());
        if(err) *err = Error::Ok;
        return true;
}

bool EnumList::append(int value, Error *err) {
        if(!_type.isValid()) {
                if(err) *err = Error::InvalidArgument;
                return false;
        }
        // Raw integer append is not validated against the type's value
        // table: out-of-list integers are legal Enum values (per Enum's
        // own append semantics) and get rendered as decimal integers on
        // toString(), so they round-trip through fromString().
        _values.pushToBack(value);
        if(err) *err = Error::Ok;
        return true;
}

bool EnumList::append(const String &name, Error *err) {
        if(!_type.isValid()) {
                if(err) *err = Error::InvalidArgument;
                return false;
        }
        Error lookErr;
        int v = Enum::valueOf(_type, name, &lookErr);
        if(lookErr.isError()) {
                if(err) *err = lookErr;
                return false;
        }
        _values.pushToBack(v);
        if(err) *err = Error::Ok;
        return true;
}

EnumList EnumList::uniqueSorted() const {
        EnumList out(_type);
        if(_values.isEmpty()) return out;

        // Sort then run-length dedupe.  We stay on List<int> rather than
        // Set<int> so the callers don't have to worry about Set's
        // unordered iteration order — the sorted integer order doubles
        // as a stable key.
        List<int> sorted = _values.sort();
        int last = 0;
        bool haveLast = false;
        for(size_t i = 0; i < sorted.size(); ++i) {
                int v = sorted[i];
                if(haveLast && v == last) continue;
                out._values.pushToBack(v);
                last = v;
                haveLast = true;
        }
        return out;
}

String EnumList::toString() const {
        if(_values.isEmpty()) return String();
        String out;
        for(size_t i = 0; i < _values.size(); ++i) {
                if(i > 0) out += ',';
                // Prefer the registered name; fall back to the decimal
                // integer for out-of-list values so fromString() can
                // still round-trip the result.
                String name = Enum::nameOf(_type, _values[i]);
                if(name.isEmpty()) {
                        out += String::number(_values[i]);
                } else {
                        out += name;
                }
        }
        return out;
}

EnumList EnumList::fromString(Enum::Type type, const String &text, Error *err) {
        EnumList out(type);
        if(!type.isValid()) {
                if(err) *err = Error::InvalidArgument;
                return EnumList();
        }
        if(err) *err = Error::Ok;

        String trimmedText = text.trim();
        if(trimmedText.isEmpty()) {
                // Empty input is legal: yields a valid, empty list.
                return out;
        }

        StringList parts = trimmedText.split(",");
        for(size_t i = 0; i < parts.size(); ++i) {
                String entry = parts[i].trim();
                if(entry.isEmpty()) {
                        if(err) *err = Error::InvalidArgument;
                        return EnumList();
                }

                // Try the registered name first; fall back to integer
                // parsing so out-of-list decimal values round-trip
                // through toString() / fromString() cleanly.
                Error lookErr;
                int v = Enum::valueOf(type, entry, &lookErr);
                if(lookErr.isError()) {
                        Error intErr;
                        int parsed = entry.to<int>(&intErr);
                        if(intErr.isError()) {
                                if(err) *err = Error::InvalidArgument;
                                return EnumList();
                        }
                        v = parsed;
                }
                out._values.pushToBack(v);
        }
        return out;
}

PROMEKI_NAMESPACE_END
