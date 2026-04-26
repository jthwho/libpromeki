/**
 * @file      json.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/json.h>
#include <promeki/variant.h>
#include <promeki/uuid.h>

PROMEKI_NAMESPACE_BEGIN

void JsonObject::set(const String &key, const UUID &val) {
        _j[key.str()] = val.toString().str();
}

void JsonArray::add(const UUID &val) {
        _j.push_back(val.toString().str());
}

void JsonObject::setFromVariant(const String &key, const Variant &val) {
        const auto &k = key.str();
        switch (val.type()) {
                case Variant::TypeInvalid: _j[k] = nullptr; break;
                case Variant::TypeBool: _j[k] = val.get<bool>(); break;
                case Variant::TypeU8: _j[k] = val.get<uint8_t>(); break;
                case Variant::TypeS8: _j[k] = val.get<int8_t>(); break;
                case Variant::TypeU16: _j[k] = val.get<uint16_t>(); break;
                case Variant::TypeS16: _j[k] = val.get<int16_t>(); break;
                case Variant::TypeU32: _j[k] = val.get<uint32_t>(); break;
                case Variant::TypeS32: _j[k] = val.get<int32_t>(); break;
                case Variant::TypeU64: _j[k] = val.get<uint64_t>(); break;
                case Variant::TypeS64: _j[k] = val.get<int64_t>(); break;
                case Variant::TypeFloat: _j[k] = val.get<float>(); break;
                case Variant::TypeDouble: _j[k] = val.get<double>(); break;
                default: _j[k] = val.get<String>().str(); break;
        }
}

void JsonObject::forEach(std::function<void(const String &, const Variant &)> func) const {
        for (auto it = _j.begin(); it != _j.end(); ++it) {
                String  key = it.key();
                Variant val = Variant::fromJson(it.value());
                func(key, val);
        }
}

void JsonArray::addFromVariant(const Variant &val) {
        switch (val.type()) {
                case Variant::TypeInvalid: _j.push_back(nullptr); break;
                case Variant::TypeBool: _j.push_back(val.get<bool>()); break;
                case Variant::TypeU8: _j.push_back(val.get<uint8_t>()); break;
                case Variant::TypeS8: _j.push_back(val.get<int8_t>()); break;
                case Variant::TypeU16: _j.push_back(val.get<uint16_t>()); break;
                case Variant::TypeS16: _j.push_back(val.get<int16_t>()); break;
                case Variant::TypeU32: _j.push_back(val.get<uint32_t>()); break;
                case Variant::TypeS32: _j.push_back(val.get<int32_t>()); break;
                case Variant::TypeU64: _j.push_back(val.get<uint64_t>()); break;
                case Variant::TypeS64: _j.push_back(val.get<int64_t>()); break;
                case Variant::TypeFloat: _j.push_back(val.get<float>()); break;
                case Variant::TypeDouble: _j.push_back(val.get<double>()); break;
                default: _j.push_back(val.get<String>().str()); break;
        }
}

void JsonArray::forEach(std::function<void(const Variant &)> func) const {
        for (const auto &elem : _j) {
                Variant val = Variant::fromJson(elem);
                func(val);
        }
}

PROMEKI_NAMESPACE_END
