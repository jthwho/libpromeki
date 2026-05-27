/**
 * @file      amf0.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * AMF0 (Adobe Action Message Format 0) codec implementation.  The
 * grammar is a one-byte type marker followed by marker-specific
 * payload; see the AMF0 1.0 reference for the canonical layout.
 *
 * The internal representation (see @ref Amf0Data) carries every
 * payload variant in a single struct rather than @c std::variant —
 * AMF0 trees are tiny and the few extra bytes are paid for by
 * keeping the @c PROMEKI_SHARED_FINAL clone path trivially copyable.
 */

#include <cstring>
#include <cmath>
#include <cstdio>
#include <promeki/amf0.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Internal storage
// ============================================================================

struct Amf0Value::Amf0Data {
                PROMEKI_SHARED_FINAL(Amf0Data)

                Amf0Value::Type      type = Amf0Value::Null;
                bool                 b = false;
                double               n = 0.0;
                promeki::String      s;             // String / XmlDocument / TypedObject's class name
                Amf0Value::FieldList fields;        // Object / EcmaArray / TypedObject
                Amf0Value::List      items;         // StrictArray
                int16_t              dateTimezone = 0;
                uint32_t             ecmaCountHint = 0;
                uint16_t             referenceIndex = 0;
};

// ============================================================================
// Big-endian byte helpers (file-local — used by both reader and writer)
// ============================================================================

namespace {

        inline uint16_t loadU16BE(const uint8_t *p) {
                return (static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]);
        }

        inline uint32_t loadU32BE(const uint8_t *p) {
                return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                       (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
        }

        inline uint64_t loadU64BE(const uint8_t *p) {
                uint64_t hi = loadU32BE(p);
                uint64_t lo = loadU32BE(p + 4);
                return (hi << 32) | lo;
        }

        inline double loadDoubleBE(const uint8_t *p) {
                uint64_t bits = loadU64BE(p);
                double   d;
                std::memcpy(&d, &bits, sizeof(d));
                return d;
        }

        inline void storeU16BE(uint8_t *p, uint16_t v) {
                p[0] = static_cast<uint8_t>(v >> 8);
                p[1] = static_cast<uint8_t>(v);
        }

        inline void storeU32BE(uint8_t *p, uint32_t v) {
                p[0] = static_cast<uint8_t>(v >> 24);
                p[1] = static_cast<uint8_t>(v >> 16);
                p[2] = static_cast<uint8_t>(v >> 8);
                p[3] = static_cast<uint8_t>(v);
        }

        inline void storeDoubleBE(uint8_t *p, double v) {
                uint64_t bits;
                std::memcpy(&bits, &v, sizeof(bits));
                storeU32BE(p, static_cast<uint32_t>(bits >> 32));
                storeU32BE(p + 4, static_cast<uint32_t>(bits));
        }

        // Append @p len bytes to @p out, growing the underlying allocation
        // if necessary.  Buffer is a CoW value handle, so reassigning @c out
        // to a larger backing impl is the standard way to "grow" it.
        Error appendToBuffer(Buffer &out, const void *bytes, size_t len) {
                if (len == 0) return Error::Ok;
                size_t cur = out.size();
                if (cur + len > out.availSize()) {
                        size_t newAlloc = out.allocSize() * 2;
                        if (newAlloc < cur + len) newAlloc = cur + len;
                        if (newAlloc < 256) newAlloc = 256;
                        Buffer bigger(newAlloc);
                        if (cur > 0 && out.data() != nullptr) {
                                std::memcpy(bigger.data(), out.data(), cur);
                        }
                        bigger.setSize(cur);
                        out = bigger;
                }
                uint8_t *base = static_cast<uint8_t *>(out.data());
                if (base == nullptr) return Error::NotHostAccessible;
                std::memcpy(base + cur, bytes, len);
                out.setSize(cur + len);
                return Error::Ok;
        }

} // anonymous namespace

// ============================================================================
// Amf0Value — construction
// ============================================================================

Amf0Value::Amf0Value() : _d(SharedPtr<Amf0Data>::create()) {
        _d.modify()->type = Null;
}

Amf0Value::Amf0Value(bool b) : _d(SharedPtr<Amf0Data>::create()) {
        _d.modify()->type = Boolean;
        _d.modify()->b = b;
}

Amf0Value::Amf0Value(double d) : _d(SharedPtr<Amf0Data>::create()) {
        _d.modify()->type = Number;
        _d.modify()->n = d;
}

Amf0Value::Amf0Value(int i) : Amf0Value(static_cast<double>(i)) {}
Amf0Value::Amf0Value(int64_t i) : Amf0Value(static_cast<double>(i)) {}

Amf0Value::Amf0Value(const promeki::String &s) : _d(SharedPtr<Amf0Data>::create()) {
        _d.modify()->type = String;
        _d.modify()->s = s;
}

Amf0Value::Amf0Value(const char *s) : Amf0Value(promeki::String(s)) {}

// Out-of-line copy / move / destructor — keep the @c SharedPtr<Amf0Data>
// instantiation inside this TU where @c Amf0Data is fully defined.
Amf0Value::Amf0Value(const Amf0Value &) = default;
Amf0Value::Amf0Value(Amf0Value &&) noexcept = default;
Amf0Value &Amf0Value::operator=(const Amf0Value &) = default;
Amf0Value &Amf0Value::operator=(Amf0Value &&) noexcept = default;
Amf0Value::~Amf0Value() = default;

Amf0Value::Amf0Value(SharedPtr<Amf0Data> d) : _d(std::move(d)) {}

Amf0Value Amf0Value::undefined() {
        Amf0Value v;
        v._d.modify()->type = Undefined;
        return v;
}

Amf0Value Amf0Value::unsupported() {
        Amf0Value v;
        v._d.modify()->type = Unsupported;
        return v;
}

Amf0Value Amf0Value::reference(uint16_t index) {
        Amf0Value v;
        v._d.modify()->type           = Reference;
        v._d.modify()->referenceIndex = index;
        return v;
}

Amf0Value Amf0Value::object(std::initializer_list<Field> kv) {
        Amf0Value v;
        v._d.modify()->type = Object;
        for (const auto &f : kv) v.setField(f.first(), f.second());
        return v;
}

Amf0Value Amf0Value::ecmaArray(std::initializer_list<Field> kv, uint32_t countHint) {
        Amf0Value v;
        v._d.modify()->type          = EcmaArray;
        v._d.modify()->ecmaCountHint = countHint;
        for (const auto &f : kv) v.setField(f.first(), f.second());
        return v;
}

Amf0Value Amf0Value::strictArray(std::initializer_list<Amf0Value> items) {
        Amf0Value v;
        v._d.modify()->type = StrictArray;
        for (const auto &i : items) v._d.modify()->items.pushToBack(i);
        return v;
}

Amf0Value Amf0Value::date(double msSinceEpoch, int16_t timezone) {
        Amf0Value v;
        v._d.modify()->type         = Date;
        v._d.modify()->n            = msSinceEpoch;
        v._d.modify()->dateTimezone = timezone;
        return v;
}

Amf0Value Amf0Value::xmlDocument(const promeki::String &xml) {
        Amf0Value v;
        v._d.modify()->type = XmlDocument;
        v._d.modify()->s    = xml;
        return v;
}

Amf0Value Amf0Value::typedObject(const promeki::String &className, std::initializer_list<Field> kv) {
        Amf0Value v;
        v._d.modify()->type = TypedObject;
        v._d.modify()->s    = className;
        for (const auto &f : kv) v.setField(f.first(), f.second());
        return v;
}

// ============================================================================
// Amf0Value — accessors
// ============================================================================

Amf0Value::Type Amf0Value::type() const { return _d->type; }
bool            Amf0Value::asBool(bool def) const { return _d->type == Boolean ? _d->b : def; }
double          Amf0Value::asNumber(double def) const { return _d->type == Number ? _d->n : def; }

promeki::String Amf0Value::asString(const promeki::String &def) const {
        if (_d->type == String || _d->type == XmlDocument) return _d->s;
        return def;
}

double  Amf0Value::dateMs() const { return _d->type == Date ? _d->n : 0.0; }
int16_t Amf0Value::dateTimezone() const { return _d->type == Date ? _d->dateTimezone : 0; }
uint16_t Amf0Value::referenceIndex() const { return _d->type == Reference ? _d->referenceIndex : 0; }

const promeki::String &Amf0Value::className() const {
        // For non-typed-object values this returns whatever string is in @c s,
        // which is empty by default — matches the "not present" contract.
        return _d->s;
}

const Amf0Value::FieldList &Amf0Value::fields() const { return _d->fields; }
Amf0Value::FieldList       &Amf0Value::fields() { return _d.modify()->fields; }

const Amf0Value *Amf0Value::find(const promeki::String &key) const {
        for (const auto &f : _d->fields) {
                if (f.first() == key) return &f.second();
        }
        return nullptr;
}

Amf0Value *Amf0Value::find(const promeki::String &key) {
        FieldList &fl = _d.modify()->fields;
        for (auto &f : fl) {
                if (f.first() == key) return &f.second();
        }
        return nullptr;
}

bool Amf0Value::contains(const promeki::String &key) const { return find(key) != nullptr; }

void Amf0Value::setField(const promeki::String &key, Amf0Value value) {
        FieldList &fl = _d.modify()->fields;
        for (auto &f : fl) {
                if (f.first() == key) {
                        f.second() = std::move(value);
                        return;
                }
        }
        fl.pushToBack(Field(key, std::move(value)));
}

uint32_t                  Amf0Value::ecmaCountHint() const { return _d->ecmaCountHint; }
void                      Amf0Value::setEcmaCountHint(uint32_t count) { _d.modify()->ecmaCountHint = count; }
const Amf0Value::List    &Amf0Value::items() const { return _d->items; }
Amf0Value::List          &Amf0Value::items() { return _d.modify()->items; }

bool Amf0Value::operator==(const Amf0Value &other) const {
        if (_d.referenceCount() && _d == other._d) return true;
        if (_d->type != other._d->type) return false;
        switch (_d->type) {
                case Null:
                case Undefined:
                case Unsupported:
                        return true;
                case Boolean:    return _d->b == other._d->b;
                case Number:     return _d->n == other._d->n;
                case String:
                case XmlDocument:
                        return _d->s == other._d->s;
                case Reference:  return _d->referenceIndex == other._d->referenceIndex;
                case Date:
                        return _d->n == other._d->n && _d->dateTimezone == other._d->dateTimezone;
                case StrictArray:
                        if (_d->items.size() != other._d->items.size()) return false;
                        for (size_t i = 0; i < _d->items.size(); ++i) {
                                if (!(_d->items[i] == other._d->items[i])) return false;
                        }
                        return true;
                case Object:
                case TypedObject:
                case EcmaArray:
                        // Field equality is order-sensitive (matches the wire contract).
                        if (_d->fields.size() != other._d->fields.size()) return false;
                        if (_d->type == TypedObject && _d->s != other._d->s) return false;
                        if (_d->type == EcmaArray && _d->ecmaCountHint != other._d->ecmaCountHint)
                                return false;
                        for (size_t i = 0; i < _d->fields.size(); ++i) {
                                if (_d->fields[i].first() != other._d->fields[i].first()) return false;
                                if (!(_d->fields[i].second() == other._d->fields[i].second())) return false;
                        }
                        return true;
        }
        return false;
}

// ============================================================================
// Amf0Value — debug rendering
// ============================================================================

namespace {

        void appendIndent(promeki::String &out, unsigned int indent, unsigned int depth) {
                if (indent == 0) return;
                out += "\n";
                for (unsigned int i = 0; i < indent * depth; ++i) out += ' ';
        }

        void appendDebug(const Amf0Value &v, promeki::String &out, unsigned int indent, unsigned int depth) {
                switch (v.type()) {
                        case Amf0Value::Null:        out += "null"; return;
                        case Amf0Value::Undefined:   out += "undefined"; return;
                        case Amf0Value::Unsupported: out += "unsupported"; return;
                        case Amf0Value::Boolean:     out += v.asBool() ? "true" : "false"; return;
                        case Amf0Value::Number: {
                                char buf[64];
                                std::snprintf(buf, sizeof(buf), "%g", v.asNumber());
                                out += buf;
                                return;
                        }
                        case Amf0Value::String:
                        case Amf0Value::XmlDocument: {
                                out += '"';
                                out += v.asString();
                                out += '"';
                                return;
                        }
                        case Amf0Value::Reference: {
                                char buf[32];
                                std::snprintf(buf, sizeof(buf), "ref(%u)", v.referenceIndex());
                                out += buf;
                                return;
                        }
                        case Amf0Value::Date: {
                                char buf[64];
                                std::snprintf(buf, sizeof(buf), "date(%g, tz=%d)", v.dateMs(), v.dateTimezone());
                                out += buf;
                                return;
                        }
                        case Amf0Value::StrictArray: {
                                out += '[';
                                for (size_t i = 0; i < v.items().size(); ++i) {
                                        if (i) out += ", ";
                                        appendIndent(out, indent, depth + 1);
                                        appendDebug(v.items()[i], out, indent, depth + 1);
                                }
                                appendIndent(out, indent, depth);
                                out += ']';
                                return;
                        }
                        case Amf0Value::Object:
                        case Amf0Value::EcmaArray:
                        case Amf0Value::TypedObject: {
                                if (v.type() == Amf0Value::TypedObject) {
                                        out += "@";
                                        out += v.className();
                                }
                                if (v.type() == Amf0Value::EcmaArray) {
                                        char buf[32];
                                        std::snprintf(buf, sizeof(buf), "ecma(%u)", v.ecmaCountHint());
                                        out += buf;
                                }
                                out += '{';
                                bool first = true;
                                for (const auto &f : v.fields()) {
                                        if (!first) out += ", ";
                                        first = false;
                                        appendIndent(out, indent, depth + 1);
                                        out += f.first();
                                        out += ": ";
                                        appendDebug(f.second(), out, indent, depth + 1);
                                }
                                appendIndent(out, indent, depth);
                                out += '}';
                                return;
                        }
                }
        }

} // anonymous namespace

promeki::String Amf0Value::toDebugString(unsigned int indent) const {
        promeki::String out;
        appendDebug(*this, out, indent, 0);
        return out;
}

// ============================================================================
// Amf0Writer
// ============================================================================

Amf0Writer::Amf0Writer(Buffer &out) : _out(out) {}

Error Amf0Writer::appendBytes(const void *bytes, size_t len) {
        Error err = appendToBuffer(_out, bytes, len);
        if (err.isOk()) _bytesWritten += len;
        return err;
}

Error Amf0Writer::appendByte(uint8_t b) { return appendBytes(&b, 1); }

Error Amf0Writer::appendU16BE(uint16_t v) {
        uint8_t buf[2];
        storeU16BE(buf, v);
        return appendBytes(buf, 2);
}

Error Amf0Writer::appendU32BE(uint32_t v) {
        uint8_t buf[4];
        storeU32BE(buf, v);
        return appendBytes(buf, 4);
}

Error Amf0Writer::appendDoubleBE(double v) {
        uint8_t buf[8];
        storeDoubleBE(buf, v);
        return appendBytes(buf, 8);
}

Error Amf0Writer::appendShortString(const promeki::String &s) {
        size_t len = s.byteCount();
        if (len > 0xFFFF) return Error::OutOfRange;
        Error err = appendU16BE(static_cast<uint16_t>(len));
        if (err.isError()) return err;
        if (len > 0) {
                err = appendBytes(s.cstr(), len);
                if (err.isError()) return err;
        }
        return Error::Ok;
}

Error Amf0Writer::appendLongString(const promeki::String &s) {
        size_t len = s.byteCount();
        if (len > 0xFFFFFFFFu) return Error::OutOfRange;
        Error err = appendU32BE(static_cast<uint32_t>(len));
        if (err.isError()) return err;
        if (len > 0) {
                err = appendBytes(s.cstr(), len);
                if (err.isError()) return err;
        }
        return Error::Ok;
}

Error Amf0Writer::writeNumber(double v) {
        Error err = appendByte(Amf0Value::MarkerNumber);
        if (err.isError()) return err;
        return appendDoubleBE(v);
}

Error Amf0Writer::writeBoolean(bool v) {
        Error err = appendByte(Amf0Value::MarkerBoolean);
        if (err.isError()) return err;
        return appendByte(v ? 0x01 : 0x00);
}

Error Amf0Writer::writeString(const promeki::String &s) {
        if (s.byteCount() <= 0xFFFF) {
                Error err = appendByte(Amf0Value::MarkerString);
                if (err.isError()) return err;
                return appendShortString(s);
        }
        Error err = appendByte(Amf0Value::MarkerLongString);
        if (err.isError()) return err;
        return appendLongString(s);
}

Error Amf0Writer::writeNull()      { return appendByte(Amf0Value::MarkerNull); }
Error Amf0Writer::writeUndefined() { return appendByte(Amf0Value::MarkerUndefined); }

Error Amf0Writer::writeDate(double msSinceEpoch, int16_t timezone) {
        Error err = appendByte(Amf0Value::MarkerDate);
        if (err.isError()) return err;
        err = appendDoubleBE(msSinceEpoch);
        if (err.isError()) return err;
        return appendU16BE(static_cast<uint16_t>(timezone));
}

Error Amf0Writer::writeStrictArray(const Amf0Value::List &items) {
        Error err = appendByte(Amf0Value::MarkerStrictArray);
        if (err.isError()) return err;
        err = appendU32BE(static_cast<uint32_t>(items.size()));
        if (err.isError()) return err;
        for (const auto &item : items) {
                err = writeValue(item);
                if (err.isError()) return err;
        }
        return Error::Ok;
}

namespace {

        // Common terminator: 16-bit zero key length + ObjectEnd marker.
        Error appendObjectEnd(Amf0Writer &w, Buffer &out, size_t &written) {
                static const uint8_t kEnd[3] = {0x00, 0x00, Amf0Value::MarkerObjectEnd};
                Error                err = appendToBuffer(out, kEnd, 3);
                if (err.isOk()) written += 3;
                (void)w;
                return err;
        }

} // anonymous namespace

Error Amf0Writer::writeObject(const Amf0Value::FieldList &fields) {
        Error err = appendByte(Amf0Value::MarkerObject);
        if (err.isError()) return err;
        for (const auto &f : fields) {
                if (f.first().byteCount() > 0xFFFF) return Error::OutOfRange;
                err = appendShortString(f.first());
                if (err.isError()) return err;
                err = writeValue(f.second());
                if (err.isError()) return err;
        }
        return appendObjectEnd(*this, _out, _bytesWritten);
}

Error Amf0Writer::writeEcmaArray(const Amf0Value::FieldList &fields, uint32_t countHint) {
        Error err = appendByte(Amf0Value::MarkerEcmaArray);
        if (err.isError()) return err;
        err = appendU32BE(countHint);
        if (err.isError()) return err;
        for (const auto &f : fields) {
                if (f.first().byteCount() > 0xFFFF) return Error::OutOfRange;
                err = appendShortString(f.first());
                if (err.isError()) return err;
                err = writeValue(f.second());
                if (err.isError()) return err;
        }
        return appendObjectEnd(*this, _out, _bytesWritten);
}

Error Amf0Writer::writeTypedObject(const promeki::String &className, const Amf0Value::FieldList &fields) {
        if (className.byteCount() > 0xFFFF) return Error::OutOfRange;
        Error err = appendByte(Amf0Value::MarkerTypedObject);
        if (err.isError()) return err;
        err = appendShortString(className);
        if (err.isError()) return err;
        for (const auto &f : fields) {
                if (f.first().byteCount() > 0xFFFF) return Error::OutOfRange;
                err = appendShortString(f.first());
                if (err.isError()) return err;
                err = writeValue(f.second());
                if (err.isError()) return err;
        }
        return appendObjectEnd(*this, _out, _bytesWritten);
}

Error Amf0Writer::writeXmlDocument(const promeki::String &xml) {
        Error err = appendByte(Amf0Value::MarkerXmlDocument);
        if (err.isError()) return err;
        return appendLongString(xml);
}

Error Amf0Writer::writeValue(const Amf0Value &v) {
        switch (v.type()) {
                case Amf0Value::Null:        return writeNull();
                case Amf0Value::Undefined:   return writeUndefined();
                case Amf0Value::Boolean:     return writeBoolean(v.asBool());
                case Amf0Value::Number:      return writeNumber(v.asNumber());
                case Amf0Value::String:      return writeString(v.asString());
                case Amf0Value::XmlDocument: return writeXmlDocument(v.asString());
                case Amf0Value::Date:        return writeDate(v.dateMs(), v.dateTimezone());
                case Amf0Value::StrictArray: return writeStrictArray(v.items());
                case Amf0Value::Object:      return writeObject(v.fields());
                case Amf0Value::EcmaArray:   return writeEcmaArray(v.fields(), v.ecmaCountHint());
                case Amf0Value::TypedObject: return writeTypedObject(v.className(), v.fields());
                case Amf0Value::Reference: {
                        Error err = appendByte(Amf0Value::MarkerReference);
                        if (err.isError()) return err;
                        return appendU16BE(v.referenceIndex());
                }
                case Amf0Value::Unsupported: return appendByte(Amf0Value::MarkerUnsupported);
        }
        return Error::Invalid;
}

// ============================================================================
// Amf0Value::serialize — convenience shim on Amf0Writer
// ============================================================================

Error Amf0Value::serialize(Buffer &out) const {
        Amf0Writer w(out);
        return w.writeValue(*this);
}

// ============================================================================
// Amf0Reader
// ============================================================================

namespace {

        // Read a UTF-8 byte string of @p len bytes at @p offset, advancing it.
        Error readBytesString(const uint8_t *data, size_t len, size_t &offset, size_t byteLen,
                              promeki::String &out) {
                if (offset + byteLen > len) return Error::OutOfRange;
                out = promeki::String::fromUtf8(reinterpret_cast<const char *>(data + offset), byteLen);
                offset += byteLen;
                return Error::Ok;
        }

        Error readShortString(const uint8_t *data, size_t len, size_t &offset, promeki::String &out) {
                if (offset + 2 > len) return Error::OutOfRange;
                uint16_t slen = loadU16BE(data + offset);
                offset += 2;
                return readBytesString(data, len, offset, slen, out);
        }

        Error readLongString(const uint8_t *data, size_t len, size_t &offset, promeki::String &out) {
                if (offset + 4 > len) return Error::OutOfRange;
                uint32_t slen = loadU32BE(data + offset);
                offset += 4;
                return readBytesString(data, len, offset, slen, out);
        }

        // Forward declaration — readOne and the object-body loop are mutually recursive.
        Error readValue(const uint8_t *data, size_t len, size_t &offset, Amf0Value &out);

        // Read object / ecma-array / typed-object body terminated by 0x0000 0x09.
        // Caller has already consumed the marker byte (and any preamble like
        // count-hint or class-name).
        Error readObjectBody(const uint8_t *data, size_t len, size_t &offset, Amf0Value::FieldList &fields) {
                while (true) {
                        if (offset + 2 > len) return Error::OutOfRange;
                        uint16_t keyLen = loadU16BE(data + offset);
                        if (keyLen == 0) {
                                // Empty key signals end-of-object; the next byte
                                // must be the ObjectEnd marker.
                                if (offset + 3 > len) return Error::OutOfRange;
                                if (data[offset + 2] != Amf0Value::MarkerObjectEnd) return Error::CorruptData;
                                offset += 3;
                                return Error::Ok;
                        }
                        if (offset + 2 + keyLen > len) return Error::OutOfRange;
                        promeki::String key =
                            promeki::String::fromUtf8(reinterpret_cast<const char *>(data + offset + 2), keyLen);
                        offset += 2 + keyLen;
                        Amf0Value v;
                        Error     err = readValue(data, len, offset, v);
                        if (err.isError()) return err;
                        fields.pushToBack(Amf0Value::Field(std::move(key), std::move(v)));
                }
        }

        Error readValue(const uint8_t *data, size_t len, size_t &offset, Amf0Value &out) {
                if (offset >= len) return Error::OutOfRange;
                uint8_t marker = data[offset++];
                switch (marker) {
                        case Amf0Value::MarkerNumber: {
                                if (offset + 8 > len) return Error::OutOfRange;
                                out = Amf0Value(loadDoubleBE(data + offset));
                                offset += 8;
                                return Error::Ok;
                        }
                        case Amf0Value::MarkerBoolean: {
                                if (offset + 1 > len) return Error::OutOfRange;
                                out = Amf0Value(static_cast<bool>(data[offset] != 0));
                                offset += 1;
                                return Error::Ok;
                        }
                        case Amf0Value::MarkerString: {
                                promeki::String s;
                                Error           err = readShortString(data, len, offset, s);
                                if (err.isError()) return err;
                                out = Amf0Value(s);
                                return Error::Ok;
                        }
                        case Amf0Value::MarkerLongString: {
                                promeki::String s;
                                Error           err = readLongString(data, len, offset, s);
                                if (err.isError()) return err;
                                out = Amf0Value(s);
                                return Error::Ok;
                        }
                        case Amf0Value::MarkerNull:
                                out = Amf0Value();
                                return Error::Ok;
                        case Amf0Value::MarkerUndefined:
                                out = Amf0Value::undefined();
                                return Error::Ok;
                        case Amf0Value::MarkerUnsupported:
                                out = Amf0Value::unsupported();
                                return Error::Ok;
                        case Amf0Value::MarkerReference: {
                                if (offset + 2 > len) return Error::OutOfRange;
                                out = Amf0Value::reference(loadU16BE(data + offset));
                                offset += 2;
                                return Error::Ok;
                        }
                        case Amf0Value::MarkerDate: {
                                if (offset + 10 > len) return Error::OutOfRange;
                                double  ms = loadDoubleBE(data + offset);
                                int16_t tz = static_cast<int16_t>(loadU16BE(data + offset + 8));
                                offset += 10;
                                out = Amf0Value::date(ms, tz);
                                return Error::Ok;
                        }
                        case Amf0Value::MarkerStrictArray: {
                                if (offset + 4 > len) return Error::OutOfRange;
                                uint32_t count = loadU32BE(data + offset);
                                offset += 4;
                                Amf0Value             arr = Amf0Value::strictArray();
                                Amf0Value::List      &dst = arr.items();
                                for (uint32_t i = 0; i < count; ++i) {
                                        Amf0Value v;
                                        Error     err = readValue(data, len, offset, v);
                                        if (err.isError()) return err;
                                        dst.pushToBack(std::move(v));
                                }
                                out = arr;
                                return Error::Ok;
                        }
                        case Amf0Value::MarkerObject: {
                                Amf0Value             obj = Amf0Value::object();
                                Amf0Value::FieldList &fl  = obj.fields();
                                Error                 err = readObjectBody(data, len, offset, fl);
                                if (err.isError()) return err;
                                out = obj;
                                return Error::Ok;
                        }
                        case Amf0Value::MarkerEcmaArray: {
                                if (offset + 4 > len) return Error::OutOfRange;
                                uint32_t hint = loadU32BE(data + offset);
                                offset += 4;
                                Amf0Value             arr = Amf0Value::ecmaArray({}, hint);
                                Amf0Value::FieldList &fl  = arr.fields();
                                Error                 err = readObjectBody(data, len, offset, fl);
                                if (err.isError()) return err;
                                out = arr;
                                return Error::Ok;
                        }
                        case Amf0Value::MarkerTypedObject: {
                                promeki::String cls;
                                Error           err = readShortString(data, len, offset, cls);
                                if (err.isError()) return err;
                                Amf0Value             obj = Amf0Value::typedObject(cls);
                                Amf0Value::FieldList &fl  = obj.fields();
                                err                       = readObjectBody(data, len, offset, fl);
                                if (err.isError()) return err;
                                out = obj;
                                return Error::Ok;
                        }
                        case Amf0Value::MarkerXmlDocument: {
                                promeki::String s;
                                Error           err = readLongString(data, len, offset, s);
                                if (err.isError()) return err;
                                out = Amf0Value::xmlDocument(s);
                                return Error::Ok;
                        }
                        case Amf0Value::MarkerObjectEnd:
                                // ObjectEnd is only valid inside an object body and is
                                // consumed by @c readObjectBody.  Seeing it at value
                                // position means the wire was malformed.
                                return Error::CorruptData;
                        case Amf0Value::MarkerAvmPlusObject:
                                // AMF3 switch — surface to the caller per the v1 contract.
                                return Error::NotSupported;
                        default:
                                // MovieClip (0x04) and RecordSet (0x0E) are reserved
                                // markers we don't model; treat as malformed.
                                return Error::CorruptData;
                }
        }

} // anonymous namespace

Error Amf0Reader::readOne(const uint8_t *data, size_t len, size_t &offset, Amf0Value &out) {
        return readValue(data, len, offset, out);
}

Result<Amf0Value::List> Amf0Reader::read(const uint8_t *data, size_t len) {
        Amf0Value::List values;
        size_t          offset = 0;
        while (offset < len) {
                Amf0Value v;
                Error     err = readValue(data, len, offset, v);
                if (err.isError()) {
                        return Result<Amf0Value::List>(values, err);
                }
                values.pushToBack(std::move(v));
        }
        return Result<Amf0Value::List>(values, Error::Ok);
}

Result<Amf0Value::List> Amf0Reader::read(const BufferView &view) {
        if (view.count() > 1) {
                // Multi-slice input would need flattening.  The chunk-stream
                // layer reassembles every RTMP message into a single slice
                // before handing it to AMF0, so this case never arises in
                // practice — surface explicitly rather than silently
                // concatenate.
                return Result<Amf0Value::List>(Amf0Value::List(), Error::NotSupported);
        }
        if (view.isEmpty() || view.data() == nullptr) {
                return Result<Amf0Value::List>(Amf0Value::List(), Error::Ok);
        }
        return read(view.data(), view.size());
}

PROMEKI_NAMESPACE_END
