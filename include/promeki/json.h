/**
 * @file      json.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/variant.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Parser.h>
#include <Poco/Dynamic/Var.h>

PROMEKI_NAMESPACE_BEGIN

template <typename PocoType, typename PocoPtr, typename KeyType>
class JsonInterface {
    public:
        static JsonInterface parse(const String &str, bool *ok = nullptr) {
            Poco::JSON::Parser parser;
            auto result = parser.parse(str.stds());
            PocoPtr ret = result.extract<PocoPtr>();
            if(ok != nullptr) *ok = ret != nullptr;
            return ret;
        }

        JsonInterface() : d(new PocoType()) {}
        JsonInterface(const PocoPtr &obj) : d(obj == nullptr ? new PocoType() : obj) {}

        operator PocoPtr() const { return d; }

        int size() const { return d->size(); }
        bool isValid() const { return size() > 0; }

        bool valueIsNull(const KeyType &key) const { return d->isNull(key); }
        bool valueIsObject(const KeyType &key) const { return d->isObject(key); }
        bool valueIsArray(const KeyType &key) const { return d->isArray(key); }

        bool contains(const KeyType &key) const { return d->has(key); }

        bool getBool(const KeyType &key, bool *ok = nullptr) const { return get<bool>(key, ok); }
        int64_t getInt(const KeyType &key, bool *ok = nullptr) const { return get<int64_t>(key, ok); }
        uint64_t getUInt(const KeyType &key, bool *ok = nullptr) const { return get<uint64_t>(key, ok); }
        double getDouble(const KeyType &key, bool *ok = nullptr) const { return get<double>(key, ok); }
        String getString(const KeyType &key, bool *ok = nullptr) const { return get<std::string>(key, ok); }

        Poco::JSON::Object::Ptr getObject(const KeyType &key, bool *ok = nullptr) const { 
            Poco::JSON::Object::Ptr ret = d->getObject(key); 
            if(ok != nullptr) *ok = (ret != nullptr);
            return ret;
        }

        Poco::JSON::Array::Ptr getArray(const KeyType &key, bool *ok = nullptr) const {
            Poco::JSON::Array::Ptr ret = d->getArray(key);
            if(ok != nullptr) *ok = (ret != nullptr);
            return ret;
        }
 
        String toString(unsigned int indent = 0) const {
            std::stringstream ss;
            d->stringify(ss, indent);
            return ss.str();
        }

        void clear() { d->clear(); }

        void setNull(const KeyType &key) { d->set(key, Poco::Dynamic::Var()); }
        void set(const KeyType &key, const Poco::JSON::Object::Ptr &val) { d->set(key, val); }
        void set(const KeyType &key, const Poco::JSON::Array::Ptr &val) { d->set(key, val); }
        void set(const KeyType &key, int val) { d->set(key, val); }
        void set(const KeyType &key, unsigned int val) { d->set(key, val); }
        void set(const KeyType &key, float val) { d->set(key, val); }
        void set(const KeyType &key, double val) { d->set(key, val); }
        void set(const KeyType &key, const char *val) { d->set(key, std::string(val)); }
        void set(const KeyType &key, const String &val) { d->set(key, val.stds()); }
        void set(const KeyType &key, const UUID &val) { d->set(key, val.toString().stds()); }

        void setFromVariant(const KeyType &key, const Variant &val) {
            switch(val.type()) {
                case Variant::TypeInvalid: d->set(key, Poco::Dynamic::Var()); break;
                case Variant::TypeBool: d->set(key, val.get<bool>()); break;
                case Variant::TypeU8: d->set(key, val.get<uint8_t>()); break;
                case Variant::TypeS8: d->set(key, val.get<int8_t>()); break;
                case Variant::TypeU16: d->set(key, val.get<uint16_t>()); break;
                case Variant::TypeS16: d->set(key, val.get<int16_t>()); break;
                case Variant::TypeU32: d->set(key, val.get<uint32_t>()); break;
                case Variant::TypeS32: d->set(key, val.get<int32_t>()); break;
                case Variant::TypeU64: d->set(key, val.get<uint64_t>()); break;
                case Variant::TypeS64: d->set(key, val.get<int64_t>()); break;
                case Variant::TypeFloat: d->set(key, val.get<float>()); break;
                case Variant::TypeDouble: d->set(key, val.get<double>()); break;
                default: d->set(key, val.get<String>().stds()); break;
            }
            return;
        }

        template <typename Func> void forEach(Func &&func) const {
            for(const auto &[id, value] : *d) {
                String _id = id;
                Variant _val = Variant::fromPocoVar(value);
                func(_id, _val);
            }
            return;
        }

    protected:
        PocoPtr d;

        template <typename T>
        T get(const KeyType &key, bool *ok = nullptr) const {
            auto v = d->get(key);
            T ret = {};
            bool good = false;
            try {
                ret = v.template convert<T>();
                good = true;
            } catch(...) { /* Do nothing, String() will be returned */ }
            if(ok != nullptr) *ok = good;
            return ret;
        }

};

template <typename PocoType, typename PocoPtr, typename KeyType>
class JsonInterfaceArray : public JsonInterface<PocoType, PocoPtr, KeyType> {
    public:
        void addNull() { JsonInterface<PocoType, PocoPtr, KeyType>::d->add(Poco::Dynamic::Var()); }
        void add(const Poco::JSON::Object::Ptr &val) { JsonInterface<PocoType, PocoPtr, KeyType>::d->add(val); }
        void add(const Poco::JSON::Array::Ptr &val) { JsonInterface<PocoType, PocoPtr, KeyType>::d->add(val); }
        void add(int val) { JsonInterface<PocoType, PocoPtr, KeyType>::d->add(val); }
        void add(unsigned int val) { JsonInterface<PocoType, PocoPtr, KeyType>::d->add(val); }
        void add(float val) { JsonInterface<PocoType, PocoPtr, KeyType>::d->add(val); }
        void add(double val) { JsonInterface<PocoType, PocoPtr, KeyType>::d->add(val); }
        void add(const char *val) { JsonInterface<PocoType, PocoPtr, KeyType>::d->add(std::string(val)); }
        void add(const String &val) { JsonInterface<PocoType, PocoPtr, KeyType>::d->add(val.stds()); }
        void add(const UUID &val) { JsonInterface<PocoType, PocoPtr, KeyType>::d->add(val.toString().stds()); }

        void addFromVariant(const Variant &val) {
            switch(val.type()) {
                case Variant::TypeInvalid: JsonInterface<PocoType, PocoPtr, KeyType>::d->add(Poco::Dynamic::Var()); break;
                case Variant::TypeBool: JsonInterface<PocoType, PocoPtr, KeyType>::d->add(val.get<bool>()); break;
                case Variant::TypeU8: JsonInterface<PocoType, PocoPtr, KeyType>::d->add(val.get<uint8_t>()); break;
                case Variant::TypeS8: JsonInterface<PocoType, PocoPtr, KeyType>::d->add(val.get<int8_t>()); break;
                case Variant::TypeU16: JsonInterface<PocoType, PocoPtr, KeyType>::d->add(val.get<uint16_t>()); break;
                case Variant::TypeS16: JsonInterface<PocoType, PocoPtr, KeyType>::d->add(val.get<int16_t>()); break;
                case Variant::TypeU32: JsonInterface<PocoType, PocoPtr, KeyType>::d->add(val.get<uint32_t>()); break;
                case Variant::TypeS32: JsonInterface<PocoType, PocoPtr, KeyType>::d->add(val.get<int32_t>()); break;
                case Variant::TypeU64: JsonInterface<PocoType, PocoPtr, KeyType>::d->add(val.get<uint64_t>()); break;
                case Variant::TypeS64: JsonInterface<PocoType, PocoPtr, KeyType>::d->add(val.get<int64_t>()); break;
                case Variant::TypeFloat: JsonInterface<PocoType, PocoPtr, KeyType>::d->add(val.get<float>()); break;
                case Variant::TypeDouble: JsonInterface<PocoType, PocoPtr, KeyType>::d->add(val.get<double>()); break;
                default: JsonInterface<PocoType, PocoPtr, KeyType>::d->add(val.get<String>().stds()); break;
            }
            return;
        }
       
};

using JsonObject = JsonInterface<Poco::JSON::Object, Poco::JSON::Object::Ptr, std::string>;
using JsonArray  = JsonInterfaceArray<Poco::JSON::Array, Poco::JSON::Array::Ptr, int>;

#if 0
/** Class to make working w/ Poco JSON Objects a tad easier */
class JsonObject {
    public:
        JsonObject() : d(new Poco::JSON::Object()) {}
        JsonObject(const Poco::JSON::Object::Ptr &obj) : d(obj == nullptr ? new Poco::JSON::Object() : obj) {}

        Poco::JSON::Object::Ptr pocoObject() const { return d; }
        int size() const { return d->size(); }
        bool isValid() const { return size() > 0; }

        int getInt(const String &key, bool *ok = nullptr) const { return get<int>(key, ok); }
        double getDouble(const String &key, bool *ok = nullptr) const { return get<double>(key, ok); }
        String getString(const String &key, bool *ok = nullptr) const { return get<std::string>(key, ok); }
        Poco::JSON::Object::Ptr getObject(const String &key, bool *ok = nullptr) const { 
            Poco::JSON::Object::Ptr ret = d->getObject(key); 
            if(ok != nullptr) *ok = (ret != nullptr);
            return ret;
        }
        Poco::JSON::Array::Ptr getArray(const String &key, bool *ok = nullptr) const {
            Poco::JSON::Array::Ptr ret = d->getArray(key);
            if(ok != nullptr) *ok = (ret != nullptr);
            return ret;
        }
        
        void set(const String &key, const JsonObject &val) { d->set(key, val.d); }

        void set(const String &key, int val) { d->set(key, val); }
        void set(const String &key, unsigned int val) { d->set(key, val); }
        void set(const String &key, float val) { d->set(key, val); }
        void set(const String &key, double val) { d->set(key, val); }
        void set(const String &key, const char *val) { d->set(key, std::string(val)); }
        void set(const String &key, const String &val) { d->set(key, val.stds()); }
        void set(const String &key, const Variant &val) {
            switch(val.type()) {
                case Variant::TypeInvalid: d->set(key, Poco::Dynamic::Var()); break;
                case Variant::TypeBool: d->set(key, val.get<bool>()); break;
                case Variant::TypeU8: d->set(key, val.get<uint8_t>()); break;
                case Variant::TypeS8: d->set(key, val.get<int8_t>()); break;
                case Variant::TypeU16: d->set(key, val.get<uint16_t>()); break;
                case Variant::TypeS16: d->set(key, val.get<int16_t>()); break;
                case Variant::TypeU32: d->set(key, val.get<uint32_t>()); break;
                case Variant::TypeS32: d->set(key, val.get<int32_t>()); break;
                case Variant::TypeU64: d->set(key, val.get<uint64_t>()); break;
                case Variant::TypeS64: d->set(key, val.get<int64_t>()); break;
                case Variant::TypeFloat: d->set(key, val.get<float>()); break;
                case Variant::TypeDouble: d->set(key, val.get<double>()); break;
                default: d->set(key, val.get<String>().stds()); break;
            }
            return;
        }
        void set(const String &key, const UUID &val) { d->set(key, val.toString().stds()); }

        String toString(unsigned int indent = 0) const {
            std::stringstream ss;
            d->stringify(ss, indent);
            return ss.str();
        }

    private:
        Poco::JSON::Object::Ptr d;

        template <typename T>
        T get(const String &key, bool *ok = nullptr) const {
            auto v = d->get(key);
            T ret = {};
            bool good = false;
            try {
                ret = v.convert<T>();
                good = true;
            } catch(...) { /* Do nothing, String() will be returned */ }
            if(ok != nullptr) *ok = good;
            return ret;
        }
};
#endif

PROMEKI_NAMESPACE_END

