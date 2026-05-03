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

namespace {

// Single recursive Variant → nlohmann::json walker shared by
// setFromVariant / addFromVariant.  Keeping the encode logic in one
// place means there's no toJsonString round-trip to re-parse — the
// nested VariantList / VariantMap branches recurse directly into
// child nodes.
nlohmann::json encodeVariant(const Variant &val) {
        switch (val.type()) {
                case Variant::TypeInvalid: return nullptr;
                case Variant::TypeBool: return val.get<bool>();
                case Variant::TypeU8: return val.get<uint8_t>();
                case Variant::TypeS8: return val.get<int8_t>();
                case Variant::TypeU16: return val.get<uint16_t>();
                case Variant::TypeS16: return val.get<int16_t>();
                case Variant::TypeU32: return val.get<uint32_t>();
                case Variant::TypeS32: return val.get<int32_t>();
                case Variant::TypeU64: return val.get<uint64_t>();
                case Variant::TypeS64: return val.get<int64_t>();
                case Variant::TypeFloat: return val.get<float>();
                case Variant::TypeDouble: return val.get<double>();
                case Variant::TypeVariantList: {
                        nlohmann::json     arr = nlohmann::json::array();
                        const VariantList *vl = val.peek<VariantList>();
                        if (vl != nullptr) {
                                const size_t n = vl->size();
                                for (size_t i = 0; i < n; ++i) arr.push_back(encodeVariant((*vl)[i]));
                        }
                        return arr;
                }
                case Variant::TypeVariantMap: {
                        nlohmann::json    obj = nlohmann::json::object();
                        const VariantMap *vm = val.peek<VariantMap>();
                        if (vm != nullptr) {
                                vm->forEach([&obj](const String &k, const Variant &v) {
                                        obj[k.str()] = encodeVariant(v);
                                });
                        }
                        return obj;
                }
                default: return val.get<String>().str();
        }
}

} // anonymous namespace

void JsonObject::set(const String &key, const UUID &val) {
        _j[key.str()] = val.toString().str();
}

void JsonArray::add(const UUID &val) {
        _j.push_back(val.toString().str());
}

void JsonObject::setFromVariant(const String &key, const Variant &val) {
        _j[key.str()] = encodeVariant(val);
}

void JsonObject::forEach(std::function<void(const String &, const Variant &)> func) const {
        for (auto it = _j.begin(); it != _j.end(); ++it) {
                String  key = it.key();
                Variant val = Variant::fromJson(it.value());
                func(key, val);
        }
}

void JsonArray::addFromVariant(const Variant &val) {
        _j.push_back(encodeVariant(val));
}

void JsonArray::forEach(std::function<void(const Variant &)> func) const {
        for (const auto &elem : _j) {
                Variant val = Variant::fromJson(elem);
                func(val);
        }
}

PROMEKI_NAMESPACE_END
