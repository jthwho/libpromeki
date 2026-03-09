/**
 * @file      json.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information
 */

#pragma once

#include <sstream>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/variant.h>
#include <promeki/sharedptr.h>
#include <promeki/thirdparty/nlohmann/json.hpp>

PROMEKI_NAMESPACE_BEGIN

class JsonArray;

class JsonObject {
    public:
        static JsonObject parse(const String &str, Error *err = nullptr) {
            JsonObject ret;
            try {
                ret.d.modify()->j = nlohmann::json::parse(str.stds());
                if(!ret.d->j.is_object()) throw std::runtime_error("not an object");
                if(err) *err = Error::Ok;
            } catch(...) {
                ret.d.modify()->j = nlohmann::json::object();
                if(err) *err = Error::Invalid;
            }
            return ret;
        }

        JsonObject() : d(SharedPtr<Data>::create(nlohmann::json::object())) {}

        int size() const { return static_cast<int>(d->j.size()); }
        bool isValid() const { return size() > 0; }

        bool valueIsNull(const std::string &key) const {
            auto it = d->j.find(key);
            return it != d->j.end() && it->is_null();
        }

        bool valueIsObject(const std::string &key) const {
            auto it = d->j.find(key);
            return it != d->j.end() && it->is_object();
        }

        bool valueIsArray(const std::string &key) const {
            auto it = d->j.find(key);
            return it != d->j.end() && it->is_array();
        }

        bool contains(const std::string &key) const { return d->j.contains(key); }

        bool getBool(const std::string &key, Error *err = nullptr) const { return get<bool>(key, err); }
        int64_t getInt(const std::string &key, Error *err = nullptr) const { return get<int64_t>(key, err); }
        uint64_t getUInt(const std::string &key, Error *err = nullptr) const { return get<uint64_t>(key, err); }
        double getDouble(const std::string &key, Error *err = nullptr) const { return get<double>(key, err); }
        String getString(const std::string &key, Error *err = nullptr) const { return get<std::string>(key, err); }

        JsonObject getObject(const std::string &key, Error *err = nullptr) const {
            auto it = d->j.find(key);
            if(it == d->j.end() || !it->is_object()) {
                if(err) *err = Error::Invalid;
                return JsonObject();
            }
            if(err) *err = Error::Ok;
            JsonObject ret;
            ret.d.modify()->j = *it;
            return ret;
        }

        inline JsonArray getArray(const std::string &key, Error *err = nullptr) const;

        String toString(unsigned int indent = 0) const {
            if(indent == 0) return d->j.dump();
            return d->j.dump(static_cast<int>(indent));
        }

        void clear() { d.modify()->j.clear(); }

        void setNull(const std::string &key) { d.modify()->j[key] = nullptr; }
        void set(const std::string &key, const JsonObject &val) { d.modify()->j[key] = val.d->j; }
        inline void set(const std::string &key, const JsonArray &val);
        void set(const std::string &key, bool val) { d.modify()->j[key] = val; }
        void set(const std::string &key, int val) { d.modify()->j[key] = val; }
        void set(const std::string &key, unsigned int val) { d.modify()->j[key] = val; }
        void set(const std::string &key, int64_t val) { d.modify()->j[key] = val; }
        void set(const std::string &key, uint64_t val) { d.modify()->j[key] = val; }
        void set(const std::string &key, float val) { d.modify()->j[key] = val; }
        void set(const std::string &key, double val) { d.modify()->j[key] = val; }
        void set(const std::string &key, const char *val) { d.modify()->j[key] = std::string(val); }
        void set(const std::string &key, const String &val) { d.modify()->j[key] = val.stds(); }
        void set(const std::string &key, const UUID &val) { d.modify()->j[key] = val.toString().stds(); }

        void setFromVariant(const std::string &key, const Variant &val) {
            auto &j = d.modify()->j;
            switch(val.type()) {
                case Variant::TypeInvalid: j[key] = nullptr; break;
                case Variant::TypeBool:    j[key] = val.get<bool>(); break;
                case Variant::TypeU8:      j[key] = val.get<uint8_t>(); break;
                case Variant::TypeS8:      j[key] = val.get<int8_t>(); break;
                case Variant::TypeU16:     j[key] = val.get<uint16_t>(); break;
                case Variant::TypeS16:     j[key] = val.get<int16_t>(); break;
                case Variant::TypeU32:     j[key] = val.get<uint32_t>(); break;
                case Variant::TypeS32:     j[key] = val.get<int32_t>(); break;
                case Variant::TypeU64:     j[key] = val.get<uint64_t>(); break;
                case Variant::TypeS64:     j[key] = val.get<int64_t>(); break;
                case Variant::TypeFloat:   j[key] = val.get<float>(); break;
                case Variant::TypeDouble:  j[key] = val.get<double>(); break;
                default:                   j[key] = val.get<String>().stds(); break;
            }
        }

        template <typename Func> void forEach(Func &&func) const {
            for(auto it = d->j.begin(); it != d->j.end(); ++it) {
                String key = it.key();
                Variant val = Variant::fromJson(it.value());
                func(key, val);
            }
        }

        int referenceCount() const { return d.referenceCount(); }

    private:
        friend class JsonArray;

        class Data {
            PROMEKI_SHARED_FINAL(Data)
            public:
                nlohmann::json j;
                Data() = default;
                Data(const nlohmann::json &val) : j(val) {}
                Data(nlohmann::json &&val) : j(std::move(val)) {}
                Data(const Data &o) = default;
        };

        SharedPtr<Data> d;

        template <typename T>
        T get(const std::string &key, Error *err = nullptr) const {
            auto it = d->j.find(key);
            if(it == d->j.end()) {
                if(err) *err = Error::Invalid;
                return T{};
            }
            return getVal<T>(*it, err);
        }

        template <typename T>
        static T getVal(const nlohmann::json &val, Error *err) {
            T ret{};
            bool good = false;
            try {
                if constexpr (std::is_same_v<T, bool>) {
                    if(val.is_boolean()) { ret = val.get<bool>(); good = true; }
                    else if(val.is_number_integer()) { ret = val.get<int64_t>() != 0; good = true; }
                } else if constexpr (std::is_integral_v<T>) {
                    if(val.is_number()) { ret = static_cast<T>(val.get<int64_t>()); good = true; }
                    else if(val.is_boolean()) { ret = val.get<bool>() ? 1 : 0; good = true; }
                } else if constexpr (std::is_floating_point_v<T>) {
                    if(val.is_number()) { ret = val.get<T>(); good = true; }
                } else if constexpr (std::is_same_v<T, std::string>) {
                    if(val.is_string()) { ret = val.get<std::string>(); good = true; }
                    else if(!val.is_null()) { ret = val.dump(); good = true; }
                }
            } catch(...) {}
            if(err) *err = good ? Error::Ok : Error::Invalid;
            return ret;
        }
};

class JsonArray {
    public:
        static JsonArray parse(const String &str, Error *err = nullptr) {
            JsonArray ret;
            try {
                ret.d.modify()->j = nlohmann::json::parse(str.stds());
                if(!ret.d->j.is_array()) throw std::runtime_error("not an array");
                if(err) *err = Error::Ok;
            } catch(...) {
                ret.d.modify()->j = nlohmann::json::array();
                if(err) *err = Error::Invalid;
            }
            return ret;
        }

        JsonArray() : d(SharedPtr<Data>::create(nlohmann::json::array())) {}

        int size() const { return static_cast<int>(d->j.size()); }
        bool isValid() const { return size() > 0; }

        bool valueIsNull(int index) const { return index >= 0 && index < size() && d->j[index].is_null(); }
        bool valueIsObject(int index) const { return index >= 0 && index < size() && d->j[index].is_object(); }
        bool valueIsArray(int index) const { return index >= 0 && index < size() && d->j[index].is_array(); }

        bool getBool(int index, Error *err = nullptr) const { return get<bool>(index, err); }
        int64_t getInt(int index, Error *err = nullptr) const { return get<int64_t>(index, err); }
        uint64_t getUInt(int index, Error *err = nullptr) const { return get<uint64_t>(index, err); }
        double getDouble(int index, Error *err = nullptr) const { return get<double>(index, err); }
        String getString(int index, Error *err = nullptr) const { return get<std::string>(index, err); }

        JsonObject getObject(int index, Error *err = nullptr) const {
            if(index < 0 || index >= size() || !d->j[index].is_object()) {
                if(err) *err = Error::Invalid;
                return JsonObject();
            }
            if(err) *err = Error::Ok;
            JsonObject ret;
            ret.d.modify()->j = d->j[index];
            return ret;
        }

        JsonArray getArray(int index, Error *err = nullptr) const {
            if(index < 0 || index >= size() || !d->j[index].is_array()) {
                if(err) *err = Error::Invalid;
                return JsonArray();
            }
            if(err) *err = Error::Ok;
            JsonArray ret;
            ret.d.modify()->j = d->j[index];
            return ret;
        }

        String toString(unsigned int indent = 0) const {
            if(indent == 0) return d->j.dump();
            return d->j.dump(static_cast<int>(indent));
        }

        void clear() { d.modify()->j.clear(); }

        void addNull() { d.modify()->j.push_back(nullptr); }
        void add(const JsonObject &val) { d.modify()->j.push_back(val.d->j); }
        void add(const JsonArray &val) { d.modify()->j.push_back(val.d->j); }
        void add(bool val) { d.modify()->j.push_back(val); }
        void add(int val) { d.modify()->j.push_back(val); }
        void add(unsigned int val) { d.modify()->j.push_back(val); }
        void add(int64_t val) { d.modify()->j.push_back(val); }
        void add(uint64_t val) { d.modify()->j.push_back(val); }
        void add(float val) { d.modify()->j.push_back(val); }
        void add(double val) { d.modify()->j.push_back(val); }
        void add(const char *val) { d.modify()->j.push_back(std::string(val)); }
        void add(const String &val) { d.modify()->j.push_back(val.stds()); }
        void add(const UUID &val) { d.modify()->j.push_back(val.toString().stds()); }

        void addFromVariant(const Variant &val) {
            auto &j = d.modify()->j;
            switch(val.type()) {
                case Variant::TypeInvalid: j.push_back(nullptr); break;
                case Variant::TypeBool:    j.push_back(val.get<bool>()); break;
                case Variant::TypeU8:      j.push_back(val.get<uint8_t>()); break;
                case Variant::TypeS8:      j.push_back(val.get<int8_t>()); break;
                case Variant::TypeU16:     j.push_back(val.get<uint16_t>()); break;
                case Variant::TypeS16:     j.push_back(val.get<int16_t>()); break;
                case Variant::TypeU32:     j.push_back(val.get<uint32_t>()); break;
                case Variant::TypeS32:     j.push_back(val.get<int32_t>()); break;
                case Variant::TypeU64:     j.push_back(val.get<uint64_t>()); break;
                case Variant::TypeS64:     j.push_back(val.get<int64_t>()); break;
                case Variant::TypeFloat:   j.push_back(val.get<float>()); break;
                case Variant::TypeDouble:  j.push_back(val.get<double>()); break;
                default:                   j.push_back(val.get<String>().stds()); break;
            }
        }

        template <typename Func> void forEach(Func &&func) const {
            for(const auto &elem : d->j) {
                Variant val = Variant::fromJson(elem);
                func(val);
            }
        }

        int referenceCount() const { return d.referenceCount(); }

    private:
        friend class JsonObject;

        class Data {
            PROMEKI_SHARED_FINAL(Data)
            public:
                nlohmann::json j;
                Data() = default;
                Data(const nlohmann::json &val) : j(val) {}
                Data(nlohmann::json &&val) : j(std::move(val)) {}
                Data(const Data &o) = default;
        };

        SharedPtr<Data> d;

        template <typename T>
        T get(int index, Error *err = nullptr) const {
            if(index < 0 || index >= size()) {
                if(err) *err = Error::Invalid;
                return T{};
            }
            return JsonObject::getVal<T>(d->j[index], err);
        }
};

// Inline definitions that depend on JsonArray being complete
inline JsonArray JsonObject::getArray(const std::string &key, Error *err) const {
    auto it = d->j.find(key);
    if(it == d->j.end() || !it->is_array()) {
        if(err) *err = Error::Invalid;
        return JsonArray();
    }
    if(err) *err = Error::Ok;
    JsonArray ret;
    ret.d.modify()->j = *it;
    return ret;
}

inline void JsonObject::set(const std::string &key, const JsonArray &val) {
    d.modify()->j[key] = val.d->j;
}

PROMEKI_NAMESPACE_END
