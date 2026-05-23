/**
 * @file      enumlist.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/enumlist.h>
#include <promeki/datastream.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

bool EnumList::append(const Enum &e, Error *err) {
        if (!_type.isValid()) {
                promekiWarn("EnumList::append(Enum) refused: list has no element type bound");
                if (err) *err = Error::InvalidArgument;
                return false;
        }
        if (e.type() != _type) {
                promekiWarn("EnumList::append(Enum) refused: type mismatch (list=%s, value=%s)",
                            _type.name().cstr(), e.type().name().cstr());
                if (err) *err = Error::InvalidArgument;
                return false;
        }
        _values.pushToBack(e.value());
        if (err) *err = Error::Ok;
        return true;
}

bool EnumList::append(int value, Error *err) {
        if (!_type.isValid()) {
                if (err) *err = Error::InvalidArgument;
                return false;
        }
        // Raw integer append is not validated against the type's value
        // table: out-of-list integers are legal Enum values (per Enum's
        // own append semantics) and get rendered as decimal integers on
        // toString(), so they round-trip through fromString().
        _values.pushToBack(value);
        if (err) *err = Error::Ok;
        return true;
}

bool EnumList::append(const String &name, Error *err) {
        if (!_type.isValid()) {
                promekiWarn("EnumList::append('%s') refused: list has no element type bound",
                            name.cstr());
                if (err) *err = Error::InvalidArgument;
                return false;
        }
        auto lookup = Enum::valueOf(_type, name);
        if (lookup.second().isError()) {
                promekiWarn("EnumList::append('%s') failed: not a valid value of enum %s",
                            name.cstr(), _type.name().cstr());
                if (err) *err = lookup.second();
                return false;
        }
        _values.pushToBack(lookup.first());
        if (err) *err = Error::Ok;
        return true;
}

EnumList EnumList::uniqueSorted() const {
        EnumList out(_type);
        if (_values.isEmpty()) return out;

        // Sort then run-length dedupe.  We stay on List<int> rather than
        // Set<int> so the callers don't have to worry about Set's
        // unordered iteration order — the sorted integer order doubles
        // as a stable key.
        List<int> sorted = _values.sort();
        int       last = 0;
        bool      haveLast = false;
        for (size_t i = 0; i < sorted.size(); ++i) {
                int v = sorted[i];
                if (haveLast && v == last) continue;
                out._values.pushToBack(v);
                last = v;
                haveLast = true;
        }
        return out;
}

String EnumList::toString() const {
        if (_values.isEmpty()) return String();
        if (!_type.isValid()) return String();
        String out;
        for (size_t i = 0; i < _values.size(); ++i) {
                if (i > 0) out += ',';
                // Emit just the short value name for readability.  The
                // single-arg fromString round-trip needs the qualified
                // form; Variant supplies the type via the bespoke
                // String -> EnumList path in @c variantspec.cpp when
                // the destination is known from context.  Out-of-list
                // values fall back to the decimal integer.
                String name = Enum::nameOf(_type, _values[i]);
                if (name.isEmpty()) out += String::number(_values[i]);
                else                out += name;
        }
        return out;
}

Result<EnumList> EnumList::fromString(const String &text) {
        String trimmedText = text.trim();
        if (trimmedText.isEmpty()) {
                // Empty input is legal: yields an empty, unbound list.
                return makeResult(EnumList());
        }

        StringList parts = trimmedText.split(",");
        Enum::Type type;
        EnumList   out;
        for (size_t i = 0; i < parts.size(); ++i) {
                String entry = parts[i].trim();
                if (entry.isEmpty()) {
                        // Tolerate empty entries between commas — they
                        // mirror the legacy parser's behavior and keep
                        // trailing-comma input working.
                        continue;
                }
                Error  le;
                Enum   e = Enum::lookup(entry, &le);
                if (le.isError() || !e.type().isValid()) {
                        promekiWarn("EnumList::fromString failed: invalid enum entry '%s' (in input '%s')",
                                    entry.cstr(), text.cstr());
                        return makeError<EnumList>(Error::ParseFailed);
                }
                if (!type.isValid()) {
                        type = e.type();
                        out = EnumList(type);
                } else if (e.type() != type) {
                        // Every entry must share the type deduced from
                        // the first entry — mixing types in a single
                        // EnumList is meaningless.
                        promekiWarn("EnumList::fromString failed: mixed enum types (deduced=%s, got=%s)",
                                    type.name().cstr(), e.type().name().cstr());
                        return makeError<EnumList>(Error::ParseFailed);
                }
                out._values.pushToBack(e.value());
        }
        return makeResult(std::move(out));
}

// ============================================================================
// DataStream wire format (v1: element type name + uint32 count + N int32 values).
// ============================================================================

Error EnumList::writeToStream(DataStream &s) const {
        // Element type name (empty for an invalid EnumList).
        s << String(elementType().name());
        s << static_cast<uint32_t>(_values.size());
        for (size_t i = 0; i < _values.size(); ++i) {
                s << static_cast<int32_t>(_values[i]);
        }
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<EnumList> EnumList::readFromStream<1>(DataStream &s) {
        String typeName;
        uint32_t count = 0;
        s >> typeName >> count;
        if (s.status() != DataStream::Ok) return makeError<EnumList>(s.toError());
        if (count > (256u * 1024u * 1024u)) {
                promekiWarn("EnumList::readFromStream rejected absurd count %u", (unsigned)count);
                return makeError<EnumList>(Error::CorruptData);
        }
        Enum::Type t = typeName.isEmpty() ? Enum::Type() : Enum::findType(typeName);
        EnumList out(t);
        for (uint32_t i = 0; i < count; ++i) {
                int32_t v = 0;
                s >> v;
                if (s.status() != DataStream::Ok) return makeError<EnumList>(s.toError());
                out._values.pushToBack(static_cast<int>(v));
        }
        return makeResult(std::move(out));
}

PROMEKI_NAMESPACE_END
