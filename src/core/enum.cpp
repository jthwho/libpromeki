/**
 * @file      enum.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <new>
#include <promeki/enum.h>
#include <promeki/map.h>
#include <promeki/readwritelock.h>
#include <promeki/logger.h>
#include <promeki/util.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        /**
 * @brief Thread-safe append-only registry of Enum::Definition pointers.
 *
 * Definitions are never freed: static-storage ones are owned by the
 * client, heap-allocated ones (via @ref Enum::registerType) are leaked
 * intentionally so their pointers remain stable for the process
 * lifetime.  Only the registry's index structures require locking —
 * holders of a Definition pointer can dereference it without any
 * synchronization.
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
                                auto                      it = _byName.find(name);
                                if (it == _byName.end()) return nullptr;
                                return it->second;
                        }

                        /**
                 * @brief Registers a Definition that already has storage.
                 *
                 * Stamps @c def->typeIndex with the next monotonic id and
                 * records the pointer.  Asserts when another definition
                 * with the same name is already registered.
                 */
                        const Enum::Definition *registerDef(Enum::Definition *def) {
                                ReadWriteLock::WriteLocker lock(_lock);
                                PROMEKI_ASSERT(def != nullptr);
                                PROMEKI_ASSERT(def->name != nullptr);
                                PROMEKI_ASSERT(def->entries != nullptr);
                                PROMEKI_ASSERT(def->entryCount > 0);
                                String nameStr(def->name);
                                PROMEKI_ASSERT(!_byName.contains(nameStr));
                                def->typeIndex = static_cast<uint32_t>(_all.size());
                                _all.pushToBack(def);
                                _byName.insert(std::move(nameStr), def);
                                return def;
                        }

                        /// @brief Returns all registered definitions in registration order.
                        List<const Enum::Definition *> all() const {
                                ReadWriteLock::ReadLocker      lock(_lock);
                                List<const Enum::Definition *> out;
                                out.reserve(_all.size());
                                for (size_t i = 0; i < _all.size(); ++i) {
                                        out.pushToBack(_all[i]);
                                }
                                return out;
                        }

                private:
                        EnumRegistry() = default;
                        mutable ReadWriteLock                 _lock;
                        Map<String, const Enum::Definition *> _byName; ///< Name -> Definition pointer.
                        List<const Enum::Definition *>        _all;    ///< Definitions in registration order.
        };

        /**
 * @brief Character-buffer + Entry[] companion that backs a heap-allocated
 *        Enum::Definition.
 *
 * @ref Enum::registerType needs stable `const char *` pointers for the
 * Definition's name and each entry's name.  The Value pairs the caller
 * supplies hold @ref String objects whose storage vanishes once the
 * caller's brace-initializer goes away, so we copy everything into a
 * single leaked heap allocation.  Pointer stability for the process
 * lifetime is all that's required.
 */
        struct DynamicStorage {
                        Enum::Definition def;
                        // followed by: Entry[entryCount] + char[namebuf]
        };

} // namespace

// ---------------------------------------------------------------------------
// Helpers: construct StringLiteralData blocks for a Definition.
// ---------------------------------------------------------------------------

namespace {

        /**
 * @brief Fills in the Definition's literal-backed @ref String caches.
 *
 * Three caches are populated up front so that every @ref String
 * returned by an Enum/Type/Definition accessor afterwards wraps one of
 * them via @ref String::fromLiteralData — no bytes copied:
 *
 * - @c nameLit                — the type name (backed directly by
 *                               @c def->name, already null-terminated).
 * - @c entryNameLits[i]       — each entry's short name.
 * - @c entryQualifiedLits[i]  — the fully qualified @c "Type::Entry"
 *                               form that @ref Enum::toString returns
 *                               directly for in-list values.
 *
 * A single @c "Type::Entry\0" heap buffer is allocated per entry and
 * backs both the qualified literal (offset 0, full length) and the
 * short-name literal (offset @c typeNameLen+2, length @c entryNameLen).
 * Both share the trailing @c '\0' so either is safe to treat as a C
 * string via @c cstr().  All allocations are leaked intentionally and
 * share the Definition's process-lifetime model.
 */
        void populateStringLiterals(Enum::Definition *def) {
                size_t typeNameLen = std::strlen(def->name);
                def->nameLit = new StringLiteralData(def->name, typeNameLen);
                PROMEKI_INTENTIONAL_LEAK(def->nameLit);
                if (def->entryCount == 0) {
                        def->entryNameLits = nullptr;
                        def->entryQualifiedLits = nullptr;
                        return;
                }
                void *rawNames = ::operator new(sizeof(StringLiteralData) * def->entryCount);
                void *rawQualified = ::operator new(sizeof(StringLiteralData) * def->entryCount);
                PROMEKI_INTENTIONAL_LEAK(rawNames);
                PROMEKI_INTENTIONAL_LEAK(rawQualified);
                auto *nameLits = static_cast<StringLiteralData *>(rawNames);
                auto *qualifiedLits = static_cast<StringLiteralData *>(rawQualified);
                for (size_t i = 0; i < def->entryCount; ++i) {
                        const char  *entryName = def->entries[i].name;
                        const size_t entryNameLen = std::strlen(entryName);
                        const size_t qualifiedLen = typeNameLen + 2 + entryNameLen;

                        char *qualifiedBuf = new char[qualifiedLen + 1];
                        PROMEKI_INTENTIONAL_LEAK(qualifiedBuf);
                        std::memcpy(qualifiedBuf, def->name, typeNameLen);
                        qualifiedBuf[typeNameLen] = ':';
                        qualifiedBuf[typeNameLen + 1] = ':';
                        std::memcpy(qualifiedBuf + typeNameLen + 2, entryName, entryNameLen);
                        qualifiedBuf[qualifiedLen] = '\0';

                        // Both StringLiteralData records point into the same buffer.
                        new (&qualifiedLits[i]) StringLiteralData(qualifiedBuf, qualifiedLen);
                        new (&nameLits[i]) StringLiteralData(qualifiedBuf + typeNameLen + 2, entryNameLen);
                }
                def->entryNameLits = nameLits;
                def->entryQualifiedLits = qualifiedLits;
        }

} // namespace

// ---------------------------------------------------------------------------
// Enum::Type accessors
// ---------------------------------------------------------------------------

String Enum::Type::name() const {
        if (_def == nullptr) return String();
        return String::fromLiteralData(_def->nameLit);
}

uint32_t Enum::Type::id() const {
        if (_def == nullptr) return UINT32_MAX;
        return _def->typeIndex;
}

// ---------------------------------------------------------------------------
// Enum::Definition linear-search helpers
// ---------------------------------------------------------------------------

const Enum::Entry *Enum::Definition::findByName(const String &n) const {
        for (size_t i = 0; i < entryCount; ++i) {
                if (n == entries[i].name) return &entries[i];
        }
        return nullptr;
}

const Enum::Entry *Enum::Definition::findByValue(int v) const {
        for (size_t i = 0; i < entryCount; ++i) {
                if (entries[i].value == v) return &entries[i];
        }
        return nullptr;
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

Enum::Type Enum::registerDefinition(Definition *def) {
        // Static-storage path: validate once and hand the pointer to
        // the registry.  Duplicates within the table are caught at
        // compile time by PROMEKI_REGISTER_ENUM_TYPE, but double-check
        // the default-value invariant here since dynamic callers that
        // build a Definition by hand bypass the macro's static_asserts.
        PROMEKI_ASSERT(def != nullptr);
        bool defaultFound = false;
        for (size_t i = 0; i < def->entryCount; ++i) {
                if (def->entries[i].value == def->defaultValue) {
                        defaultFound = true;
                        break;
                }
        }
        PROMEKI_ASSERT(defaultFound);
        populateStringLiterals(def);
        return Type(EnumRegistry::instance().registerDef(def));
}

Enum::Type Enum::registerType(const String &typeName, const ValueList &values, int defaultValue) {
        PROMEKI_ASSERT(!typeName.isEmpty());
        PROMEKI_ASSERT(!values.isEmpty());

        // Compute the byte count for the one-shot heap allocation.
        size_t entryCount = values.size();
        size_t nameBufSize = typeName.byteCount() + 1;
        for (const Value &v : values) {
                nameBufSize += v.first().byteCount() + 1;
        }
        size_t totalBytes = sizeof(DynamicStorage) + sizeof(Enum::Entry) * entryCount + nameBufSize;

        // Leaked on purpose: definitions live for the process lifetime.
        auto *raw = static_cast<uint8_t *>(::operator new(totalBytes));
        PROMEKI_INTENTIONAL_LEAK(raw);
        auto *storage = new (raw) DynamicStorage();

        auto *entryBuf = reinterpret_cast<Enum::Entry *>(raw + sizeof(DynamicStorage));
        char *nameBuf = reinterpret_cast<char *>(entryBuf + entryCount);
        char *cursor = nameBuf;

        // Copy the type name.
        std::memcpy(cursor, typeName.cstr(), typeName.byteCount() + 1);
        const char *defName = cursor;
        cursor += typeName.byteCount() + 1;

        // Copy each entry's name, assert uniqueness, locate default.
        bool defaultFound = false;
        for (size_t i = 0; i < entryCount; ++i) {
                const Value &v = values[i];
                PROMEKI_ASSERT(!v.first().isEmpty());
                // Detect duplicates.
                for (size_t j = 0; j < i; ++j) {
                        PROMEKI_ASSERT(entryBuf[j].value != v.second());
                        PROMEKI_ASSERT(v.first() != entryBuf[j].name);
                }
                std::memcpy(cursor, v.first().cstr(), v.first().byteCount() + 1);
                entryBuf[i].name = cursor;
                entryBuf[i].value = v.second();
                cursor += v.first().byteCount() + 1;
                if (v.second() == defaultValue) defaultFound = true;
        }
        PROMEKI_ASSERT(defaultFound);

        storage->def.name = defName;
        storage->def.entries = entryBuf;
        storage->def.entryCount = entryCount;
        storage->def.defaultValue = defaultValue;
        storage->def.typeIndex = 0; // stamped by the registry.

        populateStringLiterals(&storage->def);
        return Type(EnumRegistry::instance().registerDef(&storage->def));
}

Enum::Type Enum::findType(const String &typeName) {
        return Type(EnumRegistry::instance().find(typeName));
}

Enum::TypeList Enum::registeredTypes() {
        TypeList                 out;
        List<const Definition *> all = EnumRegistry::instance().all();
        for (size_t i = 0; i < all.size(); ++i) {
                out.pushToBack(String::fromLiteralData(all[i]->nameLit));
        }
        return out;
}

Enum::ValueList Enum::values(Type type) {
        ValueList out;
        if (type._def == nullptr) return out;
        out.reserve(type._def->entryCount);
        for (size_t i = 0; i < type._def->entryCount; ++i) {
                out.pushToBack(
                        Value(String::fromLiteralData(&type._def->entryNameLits[i]), type._def->entries[i].value));
        }
        return out;
}

int Enum::defaultValue(Type type) {
        if (type._def == nullptr) return InvalidValue;
        return type._def->defaultValue;
}

Result<int> Enum::valueOf(Type type, const String &name) {
        if (type._def == nullptr) return makeError<int>(Error::IdNotFound);
        const Entry *entry = type._def->findByName(name);
        if (entry == nullptr) return makeError<int>(Error::IdNotFound);
        return makeResult(entry->value);
}

String Enum::nameOf(Type type, int value, Error *err) {
        if (type._def == nullptr) {
                if (err != nullptr) *err = Error::IdNotFound;
                return String();
        }
        const Entry *entry = type._def->findByValue(value);
        if (entry == nullptr) {
                if (err != nullptr) *err = Error::IdNotFound;
                return String();
        }
        if (err != nullptr) *err = Error::Ok;
        const size_t index = static_cast<size_t>(entry - type._def->entries);
        return String::fromLiteralData(&type._def->entryNameLits[index]);
}

// ---------------------------------------------------------------------------
// String form / lookup
// ---------------------------------------------------------------------------

Enum Enum::lookup(const String &text, Error *err) {
        size_t pos = text.find("::");
        if (pos == String::npos) {
                if (err != nullptr) *err = Error::InvalidArgument;
                return Enum();
        }
        String typeName = text.mid(0, pos);
        String valueName = text.mid(pos + 2);
        Type   t = findType(typeName);
        if (!t.isValid()) {
                if (err != nullptr) *err = Error::IdNotFound;
                return Enum();
        }
        // First try to look up the value segment as a registered name.
        auto nameLookup = valueOf(t, valueName);
        if (nameLookup.second().isOk()) {
                if (err != nullptr) *err = Error::Ok;
                return Enum(t, nameLookup.first());
        }
        // Fall back to a signed decimal parse so that out-of-list values
        // produced by toString() ("Codec::100") round-trip cleanly.
        Error intErr;
        int   parsed = valueName.template to<int>(&intErr);
        if (intErr.isOk()) {
                if (err != nullptr) *err = Error::Ok;
                return Enum(t, parsed);
        }
        if (err != nullptr) *err = Error::IdNotFound;
        return Enum();
}

// ---------------------------------------------------------------------------
// Constructors / instance accessors
// ---------------------------------------------------------------------------

Enum::Enum(Type type) : _def(type._def) {
        _value = (_def != nullptr) ? _def->defaultValue : InvalidValue;
}

Enum::Enum(Type type, const String &name) : _def(type._def) {
        if (_def == nullptr) {
                _value = InvalidValue;
                return;
        }
        const Entry *entry = _def->findByName(name);
        _value = (entry != nullptr) ? entry->value : InvalidValue;
}

String Enum::valueName() const {
        if (_def == nullptr) return String();
        const Entry *entry = _def->findByValue(_value);
        if (entry == nullptr) return String();
        const size_t index = static_cast<size_t>(entry - _def->entries);
        return String::fromLiteralData(&_def->entryNameLits[index]);
}

String Enum::typeName() const {
        if (_def == nullptr) return String();
        return String::fromLiteralData(_def->nameLit);
}

String Enum::toString() const {
        if (_def == nullptr) return String("::");
        const Entry *entry = _def->findByValue(_value);
        if (entry != nullptr) {
                // In-list: return the pre-built qualified literal directly.
                // Two Enums with the same value hand back Strings backed by
                // the same StringLiteralData, so subsequent String ==
                // comparisons short-circuit on pointer identity.
                const size_t index = static_cast<size_t>(entry - _def->entries);
                return String::fromLiteralData(&_def->entryQualifiedLits[index]);
        }
        // Out-of-list: no pre-built form exists, so we fall back to
        // concatenation.  Seeding with the literal-backed type name keeps
        // the first half zero-copy; the subsequent += triggers COW.
        String out = String::fromLiteralData(_def->nameLit);
        out += "::";
        out += String::number(static_cast<int32_t>(_value));
        return out;
}

bool Enum::isValid() const {
        return _def != nullptr;
}

bool Enum::hasListedValue() const {
        if (_def == nullptr) return false;
        return _def->findByValue(_value) != nullptr;
}

PROMEKI_NAMESPACE_END
