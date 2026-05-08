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

// ============================================================================
// JsonValue
// ============================================================================

JsonValue::JsonValue() : _d(SharedPtr<JsonData>::create(JsonData(nlohmann::json(nullptr)))) {}
JsonValue::JsonValue(bool b) : _d(SharedPtr<JsonData>::create(JsonData(nlohmann::json(b)))) {}
JsonValue::JsonValue(int i) : _d(SharedPtr<JsonData>::create(JsonData(nlohmann::json(i)))) {}
JsonValue::JsonValue(int64_t i) : _d(SharedPtr<JsonData>::create(JsonData(nlohmann::json(i)))) {}
JsonValue::JsonValue(uint64_t i) : _d(SharedPtr<JsonData>::create(JsonData(nlohmann::json(i)))) {}
JsonValue::JsonValue(double d) : _d(SharedPtr<JsonData>::create(JsonData(nlohmann::json(d)))) {}
JsonValue::JsonValue(const char *s) : _d(SharedPtr<JsonData>::create(JsonData(nlohmann::json(std::string(s))))) {}
JsonValue::JsonValue(const promeki::String &s) :
        _d(SharedPtr<JsonData>::create(JsonData(nlohmann::json(s.str())))) {}

JsonValue JsonValue::undefined() {
        return JsonValue(nlohmann::json(nlohmann::json::value_t::discarded));
}

JsonValue JsonValue::fromVariant(const Variant &val) { return JsonValue(encodeVariant(val)); }

JsonValue::Type JsonValue::type() const {
        if (_d->j.is_discarded()) return Undefined;
        if (_d->j.is_null()) return Null;
        if (_d->j.is_boolean()) return Bool;
        if (_d->j.is_number()) return Double;
        if (_d->j.is_string()) return String;
        if (_d->j.is_array()) return Array;
        if (_d->j.is_object()) return Object;
        return Undefined;
}

bool    JsonValue::toBool(bool def) const { return isBool() ? _d->j.get<bool>() : def; }
int64_t JsonValue::toInt(int64_t def) const { return isDouble() ? _d->j.get<int64_t>() : def; }
uint64_t JsonValue::toUInt(uint64_t def) const { return isDouble() ? _d->j.get<uint64_t>() : def; }
double JsonValue::toDouble(double def) const { return isDouble() ? _d->j.get<double>() : def; }

promeki::String JsonValue::toString(const promeki::String &def) const {
        return isString() ? promeki::String(_d->j.get<std::string>()) : def;
}

Variant JsonValue::toVariant() const {
        if (isUndefined()) return Variant();
        return Variant::fromJson(_d->j);
}

promeki::String JsonValue::toJsonString(unsigned int indent) const {
        if (isUndefined()) return promeki::String();
        if (indent == 0) return _d->j.dump();
        return _d->j.dump(static_cast<int>(indent));
}

bool JsonValue::operator==(const JsonValue &other) const {
        if (_d == other._d) return true;
        // discarded values are not equal to anything per nlohmann; treat
        // two undefined values as equal for our purposes.
        if (isUndefined() && other.isUndefined()) return true;
        if (isUndefined() || other.isUndefined()) return false;
        return _d->j == other._d->j;
}

// ============================================================================
// JsonObject
// ============================================================================

void JsonObject::set(const String &key, const UUID &val) { _d.modify()->j[key.str()] = val.toString().str(); }

void JsonObject::set(const String &key, const JsonValue &val) {
        if (val.isUndefined()) {
                _d.modify()->j[key.str()] = nullptr;
        } else {
                _d.modify()->j[key.str()] = val._d->j;
        }
}

void JsonObject::setFromVariant(const String &key, const Variant &val) {
        _d.modify()->j[key.str()] = encodeVariant(val);
}

JsonObject JsonObject::fromVariantMap(const VariantMap &map) {
        nlohmann::json j = nlohmann::json::object();
        map.forEach([&j](const String &k, const Variant &v) { j[k.str()] = encodeVariant(v); });
        return JsonObject(std::move(j));
}

VariantMap JsonObject::toVariantMap() const {
        Variant           v = Variant::fromJson(_d->j);
        const VariantMap *m = v.peek<VariantMap>();
        return m != nullptr ? *m : VariantMap();
}

List<String> JsonObject::keys() const {
        List<String> out;
        out.reserve(static_cast<size_t>(size()));
        for (auto it = _d->j.begin(); it != _d->j.end(); ++it) {
                out.pushToBack(String(it.key()));
        }
        return out;
}

JsonValue JsonObject::value(const String &key) const {
        auto it = _d->j.find(key.str());
        if (it == _d->j.end()) return JsonValue::undefined();
        return JsonValue(*it);
}

bool JsonObject::remove(const String &key) {
        if (!_d->j.contains(key.str())) return false;
        _d.modify()->j.erase(key.str());
        return true;
}

JsonValue JsonObject::take(const String &key) {
        auto it = _d->j.find(key.str());
        if (it == _d->j.end()) return JsonValue::undefined();
        nlohmann::json removed = std::move(*_d.modify()->j.find(key.str()));
        _d.modify()->j.erase(key.str());
        return JsonValue(std::move(removed));
}

void JsonObject::forEach(std::function<void(const String &, const Variant &)> func) const {
        for (auto it = _d->j.begin(); it != _d->j.end(); ++it) {
                String  key = it.key();
                Variant val = Variant::fromJson(it.value());
                func(key, val);
        }
}

// ============================================================================
// JsonArray
// ============================================================================

void JsonArray::add(const UUID &val) { _d.modify()->j.push_back(val.toString().str()); }

void JsonArray::add(const JsonValue &val) {
        if (val.isUndefined()) {
                _d.modify()->j.push_back(nullptr);
        } else {
                _d.modify()->j.push_back(val._d->j);
        }
}

void JsonArray::addFromVariant(const Variant &val) { _d.modify()->j.push_back(encodeVariant(val)); }

JsonArray JsonArray::fromVariantList(const VariantList &list) {
        nlohmann::json j = nlohmann::json::array();
        const size_t   n = list.size();
        for (size_t i = 0; i < n; ++i) j.push_back(encodeVariant(list[i]));
        return JsonArray(std::move(j));
}

VariantList JsonArray::toVariantList() const {
        Variant            v = Variant::fromJson(_d->j);
        const VariantList *l = v.peek<VariantList>();
        return l != nullptr ? *l : VariantList();
}

JsonValue JsonArray::at(int index) const {
        if (index < 0 || index >= size()) return JsonValue::undefined();
        return JsonValue(_d->j[index]);
}

bool JsonArray::removeAt(int index) {
        if (index < 0 || index >= size()) return false;
        auto &j = _d.modify()->j;
        j.erase(j.begin() + index);
        return true;
}

JsonValue JsonArray::takeAt(int index) {
        if (index < 0 || index >= size()) return JsonValue::undefined();
        auto          &j = _d.modify()->j;
        nlohmann::json removed = std::move(j[index]);
        j.erase(j.begin() + index);
        return JsonValue(std::move(removed));
}

void JsonArray::insert(int index, const JsonValue &val) {
        auto &j = _d.modify()->j;
        if (index < 0) index = 0;
        if (index > static_cast<int>(j.size())) index = static_cast<int>(j.size());
        nlohmann::json node = val.isUndefined() ? nlohmann::json(nullptr) : val._d->j;
        j.insert(j.begin() + index, std::move(node));
}

void JsonArray::forEach(std::function<void(const Variant &)> func) const {
        for (const auto &elem : _d->j) {
                Variant val = Variant::fromJson(elem);
                func(val);
        }
}

PROMEKI_NAMESPACE_END
