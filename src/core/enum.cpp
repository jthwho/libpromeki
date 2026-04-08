/**
 * @file      enum.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/enum.h>
#include <promeki/map.h>
#include <promeki/readwritelock.h>
#include <promeki/logger.h>
#include <promeki/util.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Immutable per-type record describing a registered enum kind.
 *
 * A Definition is allocated once at registerType() time, its contents are
 * never modified afterward, and it lives for the lifetime of the process.
 * Enum and Enum::Type instances hold direct const pointers to it, so all
 * accessors on either class are lock-free.
 */
class Enum::Definition {
        public:
                uint32_t         typeIndex = 0;             ///< Monotonic id assigned by the registry.
                String           name;                      ///< Human-readable type name.
                ValueList        entries;                   ///< (name, int) pairs in registration order.
                Map<String, int> nameToInt;                 ///< Fast name -> int lookup.
                Map<int, String> intToName;                 ///< Fast int -> name lookup.
                int              defaultValue = InvalidValue;  ///< Default integer for this type.
};

namespace {

/**
 * @brief Thread-safe append-only registry of Enum::Definition pointers.
 *
 * Definitions are allocated on the heap and never freed; pointers remain
 * stable for the process lifetime.  Only the registry's index structures
 * require locking — holders of a Definition pointer can dereference it
 * without any synchronization.
 */
class EnumRegistry {
        public:
                static EnumRegistry &instance() {
                        static EnumRegistry reg;
                        return reg;
                }

                /// @brief Looks up a Definition by name, returning nullptr if absent.
                const Enum::Definition *find(const String &name) const {
                        ReadWriteLock::ReadLocker lock(_lock);
                        auto it = _byName.find(name);
                        if(it == _byName.end()) return nullptr;
                        return it->second;
                }

                /**
                 * @brief Registers a new Definition.
                 *
                 * The registry takes ownership of @p def.  The Definition's
                 * @c typeIndex is assigned here based on registration order.
                 * Asserts if a Definition with the same name is already
                 * registered.
                 *
                 * @param def  Newly allocated Definition; must not be null.
                 * @return Stable pointer to the stored Definition.
                 */
                const Enum::Definition *registerDef(Enum::Definition *def) {
                        ReadWriteLock::WriteLocker lock(_lock);
                        PROMEKI_ASSERT(def != nullptr);
                        PROMEKI_ASSERT(!_byName.contains(def->name));
                        def->typeIndex = static_cast<uint32_t>(_all.size());
                        _all.pushToBack(def);
                        _byName.insert(def->name, def);
                        return def;
                }

                /// @brief Returns all registered definitions in registration order.
                List<const Enum::Definition *> all() const {
                        ReadWriteLock::ReadLocker lock(_lock);
                        List<const Enum::Definition *> out;
                        out.reserve(_all.size());
                        for(size_t i = 0; i < _all.size(); ++i) {
                                out.pushToBack(_all[i]);
                        }
                        return out;
                }

        private:
                EnumRegistry() = default;
                mutable ReadWriteLock           _lock;
                Map<String, Enum::Definition *> _byName;   ///< Name -> Definition pointer.
                List<Enum::Definition *>        _all;      ///< Owns all Definitions; never cleared.
};

} // namespace

// ---------------------------------------------------------------------------
// Enum::Type
// ---------------------------------------------------------------------------

String Enum::Type::name() const {
        if(_def == nullptr) return String();
        return _def->name;
}

uint32_t Enum::Type::id() const {
        if(_def == nullptr) return UINT32_MAX;
        return _def->typeIndex;
}

// ---------------------------------------------------------------------------
// Registration / static API
// ---------------------------------------------------------------------------

Enum::Type Enum::registerType(const String &typeName,
                              const ValueList &values,
                              int defaultValue) {
        PROMEKI_ASSERT(!typeName.isEmpty());
        PROMEKI_ASSERT(!values.isEmpty());

        // Build the definition record on the heap; the registry owns it from
        // here on and never frees it — Definitions live until process exit.
        Definition *def = new Definition();
        def->name = typeName;
        def->entries = values;
        def->defaultValue = defaultValue;
        bool defaultFound = false;
        for(const Value &entry : values) {
                const String &valueName = entry.first();
                int           v         = entry.second();
                PROMEKI_ASSERT(!valueName.isEmpty());
                PROMEKI_ASSERT(!def->nameToInt.contains(valueName));
                PROMEKI_ASSERT(!def->intToName.contains(v));
                def->nameToInt.insert(valueName, v);
                def->intToName.insert(v, valueName);
                if(v == defaultValue) defaultFound = true;
        }
        PROMEKI_ASSERT(defaultFound);

        // registerDef() asserts if the type has already been registered.
        return Type(EnumRegistry::instance().registerDef(def));
}

Enum::Type Enum::findType(const String &typeName) {
        return Type(EnumRegistry::instance().find(typeName));
}

Enum::TypeList Enum::registeredTypes() {
        TypeList out;
        List<const Definition *> all = EnumRegistry::instance().all();
        for(size_t i = 0; i < all.size(); ++i) {
                out.pushToBack(all[i]->name);
        }
        return out;
}

Enum::ValueList Enum::values(Type type) {
        if(type._def == nullptr) return ValueList{};
        return type._def->entries;
}

int Enum::defaultValue(Type type) {
        if(type._def == nullptr) return InvalidValue;
        return type._def->defaultValue;
}

int Enum::valueOf(Type type, const String &name, Error *err) {
        if(type._def == nullptr) {
                if(err != nullptr) *err = Error::IdNotFound;
                return InvalidValue;
        }
        auto it = type._def->nameToInt.find(name);
        if(it == type._def->nameToInt.end()) {
                if(err != nullptr) *err = Error::IdNotFound;
                return InvalidValue;
        }
        if(err != nullptr) *err = Error::Ok;
        return it->second;
}

String Enum::nameOf(Type type, int value, Error *err) {
        if(type._def == nullptr) {
                if(err != nullptr) *err = Error::IdNotFound;
                return String();
        }
        auto it = type._def->intToName.find(value);
        if(it == type._def->intToName.end()) {
                if(err != nullptr) *err = Error::IdNotFound;
                return String();
        }
        if(err != nullptr) *err = Error::Ok;
        return it->second;
}

// ---------------------------------------------------------------------------
// String form / lookup
// ---------------------------------------------------------------------------

Enum Enum::lookup(const String &text, Error *err) {
        size_t pos = text.find("::");
        if(pos == String::npos) {
                if(err != nullptr) *err = Error::InvalidArgument;
                return Enum();
        }
        String typeName  = text.mid(0, pos);
        String valueName = text.mid(pos + 2);
        Type t = findType(typeName);
        if(!t.isValid()) {
                if(err != nullptr) *err = Error::IdNotFound;
                return Enum();
        }
        // First try to look up the value segment as a registered name.
        Error nameErr;
        int v = valueOf(t, valueName, &nameErr);
        if(nameErr.isOk()) {
                if(err != nullptr) *err = Error::Ok;
                return Enum(t, v);
        }
        // Fall back to a signed decimal parse so that out-of-list values
        // produced by toString() ("Codec::100") round-trip cleanly.
        Error intErr;
        int parsed = valueName.template to<int>(&intErr);
        if(intErr.isOk()) {
                if(err != nullptr) *err = Error::Ok;
                return Enum(t, parsed);
        }
        if(err != nullptr) *err = Error::IdNotFound;
        return Enum();
}

// ---------------------------------------------------------------------------
// Constructors / instance accessors
// ---------------------------------------------------------------------------

Enum::Enum(Type type) : _def(type._def) {
        _value = (_def != nullptr) ? _def->defaultValue : InvalidValue;
}

Enum::Enum(Type type, const String &name) : _def(type._def) {
        if(_def == nullptr) {
                _value = InvalidValue;
                return;
        }
        auto it = _def->nameToInt.find(name);
        _value = (it != _def->nameToInt.end()) ? it->second : InvalidValue;
}

String Enum::valueName() const {
        if(_def == nullptr) return String();
        auto it = _def->intToName.find(_value);
        if(it == _def->intToName.end()) return String();
        return it->second;
}

String Enum::typeName() const {
        if(_def == nullptr) return String();
        return _def->name;
}

String Enum::toString() const {
        if(_def == nullptr) return String("::");
        String out = _def->name;
        out += "::";
        auto it = _def->intToName.find(_value);
        if(it != _def->intToName.end()) {
                out += it->second;
        } else {
                // Out-of-list value — emit the signed decimal form.
                out += String::number(static_cast<int32_t>(_value));
        }
        return out;
}

bool Enum::isValid() const {
        return _def != nullptr;
}

bool Enum::hasListedValue() const {
        if(_def == nullptr) return false;
        return _def->intToName.contains(_value);
}

PROMEKI_NAMESPACE_END
