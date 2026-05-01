/**
 * @file      windowedstatsbundle.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/windowedstatsbundle.h>

#include <promeki/logger.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

WindowedStat WindowedStatsBundle::get(ID id) const {
        auto it = _windows.find(id);
        if (it == _windows.cend()) return WindowedStat();
        return it->second;
}

void WindowedStatsBundle::remove(ID id) {
        auto it = _windows.find(id);
        if (it == _windows.end()) return;
        _windows.remove(it);
}

StringList WindowedStatsBundle::describe(const ValueFormatter &formatter) const {
        StringList out;
        for (auto it = _windows.cbegin(); it != _windows.cend(); ++it) {
                WindowedStat::ValueFormatter perValue;
                if (formatter) perValue = formatter(it->first);
                String line = it->first.name();
                line += ": ";
                line += it->second.toString(perValue);
                out.pushToBack(line);
        }
        return out;
}

JsonObject WindowedStatsBundle::toJson() const {
        JsonObject j;
        for (auto it = _windows.cbegin(); it != _windows.cend(); ++it) {
                // Each entry's value is the canonical
                // "cap=N:[...]" form so a JSON consumer can re-hydrate
                // it via WindowedStat::fromString.
                j.set(it->first.name(), it->second.toSerializedString());
        }
        return j;
}

WindowedStatsBundle WindowedStatsBundle::fromJson(const JsonObject &obj, Error *err) {
        WindowedStatsBundle out;
        bool                good = true;
        obj.forEach([&](const String &key, const Variant &val) {
                String s;
                if (val.type() == Variant::TypeString) {
                        s = val.get<String>();
                } else {
                        // Tolerate non-string payloads by routing
                        // through the Variant->String conversion; any
                        // numeric value will fail fromString and we'll
                        // drop the entry below.
                        s = val.get<String>();
                }
                auto r = WindowedStat::fromString(s);
                if (error(r).isError()) {
                        promekiWarn("WindowedStatsBundle::fromJson: rejected key '%s' (%s)", key.cstr(),
                                    error(r).desc().cstr());
                        good = false;
                        return;
                }
                out._windows.insert(ID(key), value(r));
        });
        if (err) *err = good ? Error::Ok : Error::Invalid;
        return out;
}

DataStream &operator<<(DataStream &stream, const WindowedStatsBundle &b) {
        stream.writeTag(DataStream::TypeWindowedStatsBundle);
        const WindowedStatsBundle::Map &m = b.windows();
        stream << static_cast<uint32_t>(m.size());
        for (auto it = m.cbegin(); it != m.cend(); ++it) {
                stream << it->first.name();
                stream << it->second;
        }
        return stream;
}

DataStream &operator>>(DataStream &stream, WindowedStatsBundle &b) {
        b.clear();
        if (!stream.readTag(DataStream::TypeWindowedStatsBundle)) return stream;
        uint32_t count = 0;
        stream >> count;
        for (uint32_t i = 0; i < count && stream.status() == DataStream::Ok; ++i) {
                String       name;
                WindowedStat ws;
                stream >> name >> ws;
                if (stream.status() == DataStream::Ok) {
                        b.windows().insert(WindowedStatsBundle::ID(name), std::move(ws));
                }
        }
        return stream;
}

PROMEKI_NAMESPACE_END
