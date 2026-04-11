/**
 * @file      benchparams.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include "benchparams.h"

PROMEKI_NAMESPACE_BEGIN
namespace benchutil {

BenchParams &benchParams() {
        static BenchParams params;
        return params;
}

void BenchParams::set(const String &key, const String &value) {
        StringList list;
        list.pushToBack(value);
        _values.insert(key, list);
        return;
}

void BenchParams::append(const String &key, const String &value) {
        if(_values.contains(key)) {
                _values[key].pushToBack(value);
        } else {
                StringList list;
                list.pushToBack(value);
                _values.insert(key, list);
        }
        return;
}

void BenchParams::clear(const String &key) {
        _values.remove(key);
        return;
}

void BenchParams::clearAll() {
        _values.clear();
        return;
}

bool BenchParams::contains(const String &key) const {
        return _values.contains(key);
}

bool BenchParams::isEmpty() const {
        return _values.isEmpty();
}

String BenchParams::getString(const String &key, const String &def) const {
        if(!_values.contains(key)) return def;
        const StringList &list = _values[key];
        if(list.isEmpty()) return def;
        return list.back();
}

StringList BenchParams::getStringList(const String &key) const {
        if(!_values.contains(key)) return StringList();
        return _values[key];
}

int BenchParams::getInt(const String &key, int def) const {
        if(!_values.contains(key)) return def;
        const StringList &list = _values[key];
        if(list.isEmpty()) return def;
        Error err;
        int v = list.back().toInt(&err);
        return err.isError() ? def : v;
}

double BenchParams::getDouble(const String &key, double def) const {
        if(!_values.contains(key)) return def;
        const StringList &list = _values[key];
        if(list.isEmpty()) return def;
        Error err;
        double v = list.back().toDouble(&err);
        return err.isError() ? def : v;
}

bool BenchParams::getBool(const String &key, bool def) const {
        if(!_values.contains(key)) return def;
        const StringList &list = _values[key];
        if(list.isEmpty()) return def;
        Error err;
        bool v = list.back().toBool(&err);
        return err.isError() ? def : v;
}

Error BenchParams::parseArg(const String &arg) {
        if(arg.isEmpty()) return Error::InvalidArgument;

        // Look for `+=` first so `key+=value` is not mis-parsed as
        // `key+` with value `value`.
        size_t plusEq = arg.find(String("+="));
        if(plusEq != String::npos) {
                String key = arg.left(plusEq);
                String value = arg.mid(plusEq + 2);
                if(key.isEmpty()) return Error::InvalidArgument;
                append(key, value);
                return Error::Ok;
        }

        size_t eq = arg.find('=');
        if(eq != String::npos) {
                String key = arg.left(eq);
                String value = arg.mid(eq + 1);
                if(key.isEmpty()) return Error::InvalidArgument;
                set(key, value);
                return Error::Ok;
        }

        // Bare key — treat as a flag with an empty value.
        set(arg, String());
        return Error::Ok;
}

} // namespace benchutil
PROMEKI_NAMESPACE_END
