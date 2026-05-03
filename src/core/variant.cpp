/**
 * @file      variant.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Hosts the explicit template instantiations matching the `extern template`
 * declarations at the bottom of variant.h, plus the out-of-line bodies
 * for @ref VariantList and @ref VariantMap.
 *
 * The two container types are kept in this TU because they pimpl over
 * @c List<Variant> / @c Map<String, Variant> — types that need
 * @ref Variant complete to instantiate.  Putting the implementations
 * here keeps @c variant.h free of those member-template instantiations
 * and lets us declare @c VariantList / @c VariantMap as Variant
 * alternatives without a circular header dependency.
 *
 * Centralizing the explicit @c VariantImpl instantiation also keeps
 * consumer TUs from re-instantiating the ~250-line get<T>() std::visit
 * lambda and the ~35²-branch operator== for every translation unit
 * that touches Variant.
 */

#include <cstdlib>
#include <promeki/variant.tpp>
#include <promeki/json.h>
#include <promeki/datastream.h>
#include <nlohmann/json.hpp>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// VariantList::Impl
// ============================================================================

struct VariantList::Impl {
                List<Variant> list;

                Impl() = default;
                explicit Impl(const List<Variant> &l) : list(l) {}
                explicit Impl(List<Variant> &&l) : list(std::move(l)) {}
                Impl(const Impl &other) : list(other.list) {}
                Impl(Impl &&other) noexcept : list(std::move(other.list)) {}

                static const List<Variant> &emptyList() {
                        static const List<Variant> kEmpty;
                        return kEmpty;
                }
};

namespace {

// Returns a static empty list reference for read-only paths on a
// null-impl VariantList (default-constructed-and-not-yet-mutated, or
// moved-from).
const List<Variant> &emptyVariantListStorage() { return VariantList::Impl::emptyList(); }

// Lazily allocates the Impl on the first mutating call.  Default
// construction and move-from are zero-allocation, so a moved-from
// VariantList is in a valid empty state without forcing a heap hit
// just to be observable.
List<Variant> &ensureImpl(UniquePtr<VariantList::Impl> &impl) {
        if (impl.isNull()) impl = UniquePtr<VariantList::Impl>::create();
        return impl->list;
}

} // namespace

VariantList::VariantList() = default;

VariantList::VariantList(std::initializer_list<Variant> il) {
        if (il.size() == 0) return;
        _impl = UniquePtr<Impl>::create();
        _impl->list.reserve(il.size());
        for (const auto &v : il) _impl->list.pushToBack(v);
}

VariantList::VariantList(const List<Variant> &other) {
        if (!other.isEmpty()) _impl = UniquePtr<Impl>::create(other);
}
VariantList::VariantList(List<Variant> &&other) {
        if (!other.isEmpty()) _impl = UniquePtr<Impl>::create(std::move(other));
}

VariantList::VariantList(const VariantList &other) {
        if (other._impl.isValid() && !other._impl->list.isEmpty()) {
                _impl = UniquePtr<Impl>::create(*other._impl);
        }
}
VariantList::VariantList(VariantList &&other) noexcept : _impl(std::move(other._impl)) {}

VariantList::~VariantList() = default;

VariantList &VariantList::operator=(const VariantList &other) {
        if (&other == this) return *this;
        if (other._impl.isNull() || other._impl->list.isEmpty()) {
                _impl.clear();
                return *this;
        }
        if (_impl.isNull()) _impl = UniquePtr<Impl>::create();
        _impl->list = other._impl->list;
        return *this;
}

VariantList &VariantList::operator=(VariantList &&other) noexcept {
        _impl = std::move(other._impl);
        return *this;
}

size_t VariantList::size() const    { return _impl.isNull() ? 0 : _impl->list.size(); }
bool   VariantList::isEmpty() const { return _impl.isNull() || _impl->list.isEmpty(); }
void   VariantList::clear()         { if (_impl.isValid()) _impl->list.clear(); }
void   VariantList::reserve(size_t cap) {
        if (cap == 0) return;
        ensureImpl(_impl).reserve(cap);
}

Variant       &VariantList::operator[](size_t i)       { return ensureImpl(_impl)[i]; }
const Variant &VariantList::operator[](size_t i) const { return _impl->list[i]; }
Variant       &VariantList::at(size_t i)               { return ensureImpl(_impl).at(i); }
const Variant &VariantList::at(size_t i) const {
        if (_impl.isNull()) {
                // Match List<T>::at()'s OOB behavior — throw on missing index.
                throw std::logic_error("VariantList::at index out of range");
        }
        return _impl->list.at(i);
}

void VariantList::pushToBack(const Variant &v) { ensureImpl(_impl).pushToBack(v); }
void VariantList::pushToBack(Variant &&v)      { ensureImpl(_impl).pushToBack(std::move(v)); }
void VariantList::popBack() {
        if (_impl.isValid() && !_impl->list.isEmpty()) _impl->list.popFromBack();
}

Variant       *VariantList::data()       { return _impl.isNull() ? nullptr : _impl->list.data(); }
const Variant *VariantList::data() const { return _impl.isNull() ? nullptr : _impl->list.data(); }

VariantList::Iterator      VariantList::begin()        { return data(); }
VariantList::Iterator      VariantList::end()          { return data() + size(); }
VariantList::ConstIterator VariantList::begin()  const { return data(); }
VariantList::ConstIterator VariantList::end()    const { return data() + size(); }
VariantList::ConstIterator VariantList::cbegin() const { return data(); }
VariantList::ConstIterator VariantList::cend()   const { return data() + size(); }

List<Variant>       &VariantList::list()       { return ensureImpl(_impl); }
const List<Variant> &VariantList::list() const {
        return _impl.isNull() ? emptyVariantListStorage() : _impl->list;
}

bool VariantList::operator==(const VariantList &other) const {
        const bool lhsEmpty = _impl.isNull() || _impl->list.isEmpty();
        const bool rhsEmpty = other._impl.isNull() || other._impl->list.isEmpty();
        if (lhsEmpty && rhsEmpty) return true;
        if (lhsEmpty != rhsEmpty) return false;
        return _impl->list == other._impl->list;
}

// ----------------------------------------------------------------------------
// VariantList JSON round-trip
// ----------------------------------------------------------------------------

namespace {

// Forward declaration: VariantList/VariantMap recurse through each other.
nlohmann::json variantToJson(const Variant &v);
Variant        jsonToVariant(const nlohmann::json &j);

nlohmann::json variantToJson(const Variant &v) {
        switch (v.type()) {
                case Variant::TypeInvalid: return nullptr;
                case Variant::TypeBool:    return v.get<bool>();
                case Variant::TypeU8:      return v.get<uint8_t>();
                case Variant::TypeS8:      return v.get<int8_t>();
                case Variant::TypeU16:     return v.get<uint16_t>();
                case Variant::TypeS16:     return v.get<int16_t>();
                case Variant::TypeU32:     return v.get<uint32_t>();
                case Variant::TypeS32:     return v.get<int32_t>();
                case Variant::TypeU64:     return v.get<uint64_t>();
                case Variant::TypeS64:     return v.get<int64_t>();
                case Variant::TypeFloat:   return v.get<float>();
                case Variant::TypeDouble:  return v.get<double>();
                case Variant::TypeVariantList: {
                        nlohmann::json arr = nlohmann::json::array();
                        const VariantList vl = v.get<VariantList>();
                        for (size_t i = 0; i < vl.size(); ++i) arr.push_back(variantToJson(vl[i]));
                        return arr;
                }
                case Variant::TypeVariantMap: {
                        nlohmann::json obj = nlohmann::json::object();
                        const VariantMap vm = v.get<VariantMap>();
                        vm.forEach([&obj](const String &k, const Variant &val) {
                                obj[k.str()] = variantToJson(val);
                        });
                        return obj;
                }
                default: return v.get<String>().str();
        }
}

Variant jsonToVariant(const nlohmann::json &j) {
        return Variant(Variant::Base::fromJson(j));
}

} // namespace

String VariantList::toJsonString() const {
        nlohmann::json arr = nlohmann::json::array();
        const size_t   n = size();
        for (size_t i = 0; i < n; ++i) arr.push_back(variantToJson((*this)[i]));
        return String(arr.dump());
}

VariantList VariantList::fromJsonString(const String &json, Error *err) {
        if (err != nullptr) *err = Error::Ok;
        try {
                nlohmann::json j = nlohmann::json::parse(json.cstr());
                if (!j.is_array()) {
                        if (err != nullptr) *err = Error::ParseFailed;
                        return VariantList();
                }
                VariantList list;
                list.reserve(j.size());
                for (const auto &item : j) list.pushToBack(jsonToVariant(item));
                return list;
        } catch (...) {
                if (err != nullptr) *err = Error::ParseFailed;
                return VariantList();
        }
}

// ============================================================================
// VariantMap::Impl
// ============================================================================

struct VariantMap::Impl {
                Map<String, Variant> map;

                Impl() = default;
                explicit Impl(const Map<String, Variant> &m) : map(m) {}
                explicit Impl(Map<String, Variant> &&m) : map(std::move(m)) {}
                Impl(const Impl &other) : map(other.map) {}
                Impl(Impl &&other) noexcept : map(std::move(other.map)) {}

                static const Map<String, Variant> &emptyMap() {
                        static const Map<String, Variant> kEmpty;
                        return kEmpty;
                }
};

namespace {

const Map<String, Variant> &emptyVariantMapStorage() { return VariantMap::Impl::emptyMap(); }

Map<String, Variant> &ensureMapImpl(UniquePtr<VariantMap::Impl> &impl) {
        if (impl.isNull()) impl = UniquePtr<VariantMap::Impl>::create();
        return impl->map;
}

} // namespace

VariantMap::VariantMap() = default;

VariantMap::VariantMap(std::initializer_list<std::pair<const String, Variant>> il) {
        if (il.size() == 0) return;
        _impl = UniquePtr<Impl>::create();
        for (const auto &kv : il) _impl->map.insert(kv.first, kv.second);
}

VariantMap::VariantMap(const Map<String, Variant> &other) {
        if (!other.isEmpty()) _impl = UniquePtr<Impl>::create(other);
}
VariantMap::VariantMap(Map<String, Variant> &&other) {
        if (!other.isEmpty()) _impl = UniquePtr<Impl>::create(std::move(other));
}

VariantMap::VariantMap(const VariantMap &other) {
        if (other._impl.isValid() && !other._impl->map.isEmpty()) {
                _impl = UniquePtr<Impl>::create(*other._impl);
        }
}
VariantMap::VariantMap(VariantMap &&other) noexcept : _impl(std::move(other._impl)) {}

VariantMap::~VariantMap() = default;

VariantMap &VariantMap::operator=(const VariantMap &other) {
        if (&other == this) return *this;
        if (other._impl.isNull() || other._impl->map.isEmpty()) {
                _impl.clear();
                return *this;
        }
        if (_impl.isNull()) _impl = UniquePtr<Impl>::create();
        _impl->map = other._impl->map;
        return *this;
}

VariantMap &VariantMap::operator=(VariantMap &&other) noexcept {
        _impl = std::move(other._impl);
        return *this;
}

size_t VariantMap::size() const                       { return _impl.isNull() ? 0 : _impl->map.size(); }
bool   VariantMap::isEmpty() const                    { return _impl.isNull() || _impl->map.isEmpty(); }
void   VariantMap::clear()                            { if (_impl.isValid()) _impl->map.clear(); }
bool   VariantMap::contains(const String &key) const  {
        return _impl.isValid() && _impl->map.contains(key);
}

void VariantMap::insert(const String &key, const Variant &value) {
        ensureMapImpl(_impl).insert(key, value);
}
void VariantMap::insert(const String &key, Variant &&value) {
        ensureMapImpl(_impl).insert(key, std::move(value));
}

bool VariantMap::remove(const String &key) {
        if (_impl.isNull()) return false;
        return _impl->map.remove(key);
}

Variant VariantMap::value(const String &key) const {
        if (_impl.isNull()) return Variant();
        auto it = _impl->map.find(key);
        if (it == _impl->map.end()) return Variant();
        return it->second;
}

Variant VariantMap::value(const String &key, const Variant &defaultValue) const {
        if (_impl.isNull()) return defaultValue;
        auto it = _impl->map.find(key);
        if (it == _impl->map.end()) return defaultValue;
        return it->second;
}

Variant *VariantMap::find(const String &key) {
        if (_impl.isNull()) return nullptr;
        auto it = _impl->map.find(key);
        if (it == _impl->map.end()) return nullptr;
        return &it->second;
}

const Variant *VariantMap::find(const String &key) const {
        if (_impl.isNull()) return nullptr;
        auto it = _impl->map.find(key);
        if (it == _impl->map.end()) return nullptr;
        return &it->second;
}

StringList VariantMap::keys() const {
        StringList out;
        if (_impl.isNull()) return out;
        for (auto it = _impl->map.cbegin(); it != _impl->map.cend(); ++it) {
                out.pushToBack(it->first);
        }
        return out;
}

void VariantMap::forEach(std::function<void(const String &, const Variant &)> fn) const {
        if (_impl.isNull()) return;
        for (auto it = _impl->map.cbegin(); it != _impl->map.cend(); ++it) {
                fn(it->first, it->second);
        }
}

Map<String, Variant>       &VariantMap::map()       { return ensureMapImpl(_impl); }
const Map<String, Variant> &VariantMap::map() const {
        return _impl.isNull() ? emptyVariantMapStorage() : _impl->map;
}

bool VariantMap::operator==(const VariantMap &other) const {
        const bool lhsEmpty = _impl.isNull() || _impl->map.isEmpty();
        const bool rhsEmpty = other._impl.isNull() || other._impl->map.isEmpty();
        if (lhsEmpty && rhsEmpty) return true;
        if (lhsEmpty != rhsEmpty) return false;
        return _impl->map == other._impl->map;
}

String VariantMap::toJsonString() const {
        nlohmann::json obj = nlohmann::json::object();
        forEach([&obj](const String &k, const Variant &v) { obj[k.str()] = variantToJson(v); });
        return String(obj.dump());
}

VariantMap VariantMap::fromJsonString(const String &json, Error *err) {
        if (err != nullptr) *err = Error::Ok;
        try {
                nlohmann::json j = nlohmann::json::parse(json.cstr());
                if (!j.is_object()) {
                        if (err != nullptr) *err = Error::ParseFailed;
                        return VariantMap();
                }
                VariantMap map;
                for (auto it = j.begin(); it != j.end(); ++it) {
                        map.insert(String(it.key()), jsonToVariant(it.value()));
                }
                return map;
        } catch (...) {
                if (err != nullptr) *err = Error::ParseFailed;
                return VariantMap();
        }
}

// ============================================================================
// DataStream operators for VariantList / VariantMap
//
// VariantList and VariantMap are Variant alternatives, so they each get a
// distinct wire tag (@c TypeVariantList / @c TypeVariantMap).  The on-wire
// payload mirrors the generic List<T> / Map<K,V> form (count + N entries)
// but with the type-specific tag instead of @c TypeList / @c TypeMap so
// the @c Variant write/read dispatch in @c datastream.cpp can recognise
// them by their leading tag.
// ============================================================================

DataStream &operator<<(DataStream &stream, const VariantList &list) {
        stream.writeTag(DataStream::TypeVariantList);
        stream << static_cast<uint32_t>(list.size());
        for (size_t i = 0; i < list.size(); ++i) stream << list[i];
        return stream;
}

DataStream &operator>>(DataStream &stream, VariantList &list) {
        list.clear();
        if (!stream.readTag(DataStream::TypeVariantList)) return stream;
        uint32_t count = 0;
        stream >> count;
        if (stream.status() != DataStream::Ok) return stream;
        list.reserve(count);
        for (uint32_t i = 0; i < count && stream.status() == DataStream::Ok; ++i) {
                Variant v;
                stream >> v;
                if (stream.status() != DataStream::Ok) return stream;
                list.pushToBack(std::move(v));
        }
        return stream;
}

DataStream &operator<<(DataStream &stream, const VariantMap &map) {
        stream.writeTag(DataStream::TypeVariantMap);
        stream << static_cast<uint32_t>(map.size());
        map.forEach([&stream](const String &k, const Variant &v) { stream << k << v; });
        return stream;
}

DataStream &operator>>(DataStream &stream, VariantMap &map) {
        map.clear();
        if (!stream.readTag(DataStream::TypeVariantMap)) return stream;
        uint32_t count = 0;
        stream >> count;
        if (stream.status() != DataStream::Ok) return stream;
        for (uint32_t i = 0; i < count && stream.status() == DataStream::Ok; ++i) {
                String  key;
                Variant value;
                stream >> key >> value;
                if (stream.status() != DataStream::Ok) return stream;
                map.insert(std::move(key), std::move(value));
        }
        return stream;
}

// ============================================================================
// promekiResolveVariantPath — Variant-tree walker
//
// Re-implements the same key grammar used by VariantLookup
// (segment := name ('[' index ']')?, key := segment ('.' segment)*) so
// callers can walk a Variant tree without going through the lookup
// registry.  Kept self-contained here to avoid pulling variantlookup.h
// into variant.cpp.
// ============================================================================

namespace {

struct PathSegment {
                String name;     // segment name; empty if the path started with '['
                String rest;     // everything after the first '.' (if any)
                size_t index = 0;
                bool   hasIndex = false;
                bool   hasRest = false;
};

bool parsePathSegment(const String &key, PathSegment &out, Error *err) {
        if (err != nullptr) *err = Error::Ok;
        const size_t len = key.byteCount();
        if (len == 0) {
                if (err != nullptr) *err = Error::ParseFailed;
                return false;
        }
        const char *s = key.cstr();
        size_t      i = 0;
        auto        isNameChar = [](char c) -> bool {
                return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
                       c == '@';
        };
        while (i < len && isNameChar(s[i])) ++i;
        out.name = (i > 0) ? String(s, i) : String();
        out.hasIndex = false;
        out.hasRest = false;
        out.rest = String();
        out.index = 0;

        if (i < len && s[i] == '[') {
                ++i;
                const size_t numStart = i;
                while (i < len && s[i] >= '0' && s[i] <= '9') ++i;
                if (i == numStart || i >= len || s[i] != ']') {
                        if (err != nullptr) *err = Error::ParseFailed;
                        return false;
                }
                char *endp = nullptr;
                out.index = static_cast<size_t>(std::strtoull(s + numStart, &endp, 10));
                out.hasIndex = true;
                ++i;
        } else if (out.name.isEmpty()) {
                // Neither name nor index — bad segment.
                if (err != nullptr) *err = Error::ParseFailed;
                return false;
        }

        if (i == len) return true;
        if (s[i] != '.') {
                if (err != nullptr) *err = Error::ParseFailed;
                return false;
        }
        ++i;
        if (i >= len) {
                if (err != nullptr) *err = Error::ParseFailed;
                return false;
        }
        out.rest = String(s + i, len - i);
        out.hasRest = true;
        return true;
}

} // anonymous namespace

Variant promekiResolveVariantPath(const Variant &root, const String &path, Error *err) {
        if (err != nullptr) *err = Error::Ok;
        if (path.isEmpty()) return root;

        PathSegment seg;
        if (!parsePathSegment(path, seg, err)) return Variant();

        // Resolve the leading segment to a Variant value.  Use peek<T>()
        // to borrow into the underlying VariantMap / VariantList without
        // copying the container — for deep trees this is the difference
        // between O(N) and O(N²) total allocations during a walk.
        Variant current;
        if (!seg.name.isEmpty()) {
                const VariantMap *m = root.peek<VariantMap>();
                if (m == nullptr) {
                        if (err != nullptr) *err = Error::Invalid;
                        return Variant();
                }
                const Variant *p = m->find(seg.name);
                if (p == nullptr) {
                        if (err != nullptr) *err = Error::IdNotFound;
                        return Variant();
                }
                current = *p;
        } else {
                // Empty name + has-index means treat root as the list directly.
                current = root;
        }

        if (seg.hasIndex) {
                const VariantList *vl = current.peek<VariantList>();
                if (vl == nullptr) {
                        if (err != nullptr) *err = Error::Invalid;
                        return Variant();
                }
                if (seg.index >= vl->size()) {
                        if (err != nullptr) *err = Error::OutOfRange;
                        return Variant();
                }
                current = (*vl)[seg.index];
        }

        if (!seg.hasRest) return current;
        return promekiResolveVariantPath(current, seg.rest, err);
}

// ============================================================================
// Explicit template instantiations
// ============================================================================

#define X(name, type) type,
template class VariantImpl<PROMEKI_VARIANT_TYPES detail::VariantEnd>;
#undef X

#define X(name, type) template type Variant::Base::get<type>(Error * err) const;
PROMEKI_VARIANT_TYPES
#undef X

PROMEKI_NAMESPACE_END
