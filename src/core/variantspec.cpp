/**
 * @file      variantspec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <promeki/variantspec.h>
#include <promeki/color.h>
#include <promeki/colormodel.h>
#include <promeki/datetime.h>
#include <promeki/duration.h>
#include <promeki/enumlist.h>
#include <promeki/framerate.h>
#include <promeki/audiocodec.h>
#include <promeki/pixelformat.h>
#include <promeki/pixelmemlayout.h>
#include <promeki/rational.h>
#include <promeki/videocodec.h>
#include <promeki/videoformat.h>
#include <promeki/size2d.h>
#include <promeki/stringlist.h>
#include <promeki/timecode.h>
#include <promeki/url.h>
#include <promeki/windowedstat.h>
#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <promeki/socketaddress.h>
#endif

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Special members — out-of-line so the UniquePtr<VariantSpec> sub-spec
// holders can deep-clone with VariantSpec complete.
// ============================================================================

VariantSpec::VariantSpec(const VariantSpec &other)
    : _types(other._types),
      _default(other._default),
      _min(other._min),
      _max(other._max),
      _enumType(other._enumType),
      _description(other._description) {
        if (other._elementSpec.isValid()) {
                _elementSpec = UniquePtr<VariantSpec>::create(*other._elementSpec);
        }
        if (other._valueSpec.isValid()) {
                _valueSpec = UniquePtr<VariantSpec>::create(*other._valueSpec);
        }
}

VariantSpec &VariantSpec::operator=(const VariantSpec &other) {
        if (&other == this) return *this;
        _types = other._types;
        _default = other._default;
        _min = other._min;
        _max = other._max;
        _enumType = other._enumType;
        _description = other._description;
        _elementSpec = other._elementSpec.isValid() ? UniquePtr<VariantSpec>::create(*other._elementSpec)
                                                   : UniquePtr<VariantSpec>();
        _valueSpec = other._valueSpec.isValid() ? UniquePtr<VariantSpec>::create(*other._valueSpec)
                                               : UniquePtr<VariantSpec>();
        return *this;
}

VariantSpec &VariantSpec::setElementSpec(const VariantSpec &spec) {
        _elementSpec = UniquePtr<VariantSpec>::create(spec);
        return *this;
}

VariantSpec &VariantSpec::setValueSpec(const VariantSpec &spec) {
        _valueSpec = UniquePtr<VariantSpec>::create(spec);
        return *this;
}

const VariantSpec *VariantSpec::elementSpec() const { return _elementSpec.get(); }
const VariantSpec *VariantSpec::valueSpec() const { return _valueSpec.get(); }

namespace {

        /// Returns true if the Variant::Type is any integer or floating-point type.
        bool isNumericType(Variant::Type t) {
                switch (t) {
                        case Variant::TypeU8:
                        case Variant::TypeS8:
                        case Variant::TypeU16:
                        case Variant::TypeS16:
                        case Variant::TypeU32:
                        case Variant::TypeS32:
                        case Variant::TypeU64:
                        case Variant::TypeS64:
                        case Variant::TypeFloat:
                        case Variant::TypeDouble: return true;
                        default: return false;
                }
        }

        /// Returns true if @p a and @p b are both numeric types and thus
        /// interchangeable for validation purposes (Variant converts between
        /// all numeric types automatically).
        bool numericCompatible(Variant::Type a, Variant::Type b) {
                return isNumericType(a) && isNumericType(b);
        }

        /// Returns a short human-readable label for a single Variant::Type.
        String singleTypeName(Variant::Type t, Enum::Type enumType) {
                switch (t) {
                        case Variant::TypeBool: return "bool";
                        case Variant::TypeU8: return "uint8";
                        case Variant::TypeS8: return "int8";
                        case Variant::TypeU16: return "uint16";
                        case Variant::TypeS16: return "int16";
                        case Variant::TypeU32: return "uint";
                        case Variant::TypeS32: return "int";
                        case Variant::TypeU64: return "uint64";
                        case Variant::TypeS64: return "int64";
                        case Variant::TypeFloat: return "float";
                        case Variant::TypeDouble: return "double";
                        case Variant::TypeString: return "String";
                        case Variant::TypeDateTime: return "DateTime";
                        case Variant::TypeTimeStamp: return "TimeStamp";
                        case Variant::TypeSize2D: return "Size2D";
                        case Variant::TypeUUID: return "UUID";
                        case Variant::TypeUMID: return "UMID";
                        case Variant::TypeTimecode: return "Timecode";
                        case Variant::TypeFrameNumber: return "FrameNumber";
                        case Variant::TypeFrameCount: return "FrameCount";
                        case Variant::TypeMediaDuration: return "MediaDuration";
                        case Variant::TypeDuration: return "Duration";
                        case Variant::TypeRational: return "Rational";
                        case Variant::TypeFrameRate: return "FrameRate";
                        case Variant::TypeVideoFormat: return "VideoFormat";
                        case Variant::TypeStringList: return "StringList";
                        case Variant::TypeColor: return "Color";
                        case Variant::TypeColorModel: return "ColorModel";
                        case Variant::TypeMemSpace: return "MemSpace";
                        case Variant::TypePixelMemLayout: return "PixelMemLayout";
                        case Variant::TypePixelFormat: return "PixelFormat";
                        case Variant::TypeVideoCodec: return "VideoCodec";
                        case Variant::TypeAudioCodec: return "AudioCodec";
                        case Variant::TypeAudioFormat: return "AudioFormat";
                        case Variant::TypeAudioStreamDesc: return "AudioStreamDesc";
                        case Variant::TypeAudioChannelMap: return "AudioChannelMap";
                        case Variant::TypeAudioMarkerList: return "AudioMarkerList";
                        case Variant::TypeEnum: {
                                if (enumType.isValid()) return String("Enum ") + enumType.name();
                                return "Enum";
                        }
                        case Variant::TypeEnumList: {
                                if (enumType.isValid()) return String("EnumList ") + enumType.name();
                                return "EnumList";
                        }
                        case Variant::TypeUrl: return "Url";
                        case Variant::TypeWindowedStat: return "WindowedStat";
                        case Variant::TypeVariantList: return "VariantList";
                        case Variant::TypeVariantMap: return "VariantMap";
#if PROMEKI_ENABLE_NETWORK
                        case Variant::TypeSocketAddress: return "SocketAddress";
                        case Variant::TypeSdpSession: return "SdpSession";
#endif
                        default: return "unknown";
                }
        }

        /// Attempt to parse @p str as a specific Variant::Type.  Returns an
        /// invalid Variant and sets @p err on failure.
        Variant parseAsType(Variant::Type type, Enum::Type enumType, const String &str, Error *err) {
                Variant src(str);
                Error   ce;

                switch (type) {
                        case Variant::TypeBool: {
                                String low = str.toLower();
                                if (low == "true" || low == "yes" || low == "1" || low == "on") return Variant(true);
                                if (low == "false" || low == "no" || low == "0" || low == "off") return Variant(false);
                                if (err) *err = Error::Invalid;
                                return Variant();
                        }
                        case Variant::TypeU8: {
                                auto v = src.get<uint8_t>(&ce);
                                if (ce.isError()) break;
                                return Variant(v);
                        }
                        case Variant::TypeS8: {
                                auto v = src.get<int8_t>(&ce);
                                if (ce.isError()) break;
                                return Variant(v);
                        }
                        case Variant::TypeU16: {
                                auto v = src.get<uint16_t>(&ce);
                                if (ce.isError()) break;
                                return Variant(v);
                        }
                        case Variant::TypeS16: {
                                auto v = src.get<int16_t>(&ce);
                                if (ce.isError()) break;
                                return Variant(v);
                        }
                        case Variant::TypeU32: {
                                auto v = src.get<uint32_t>(&ce);
                                if (ce.isError()) break;
                                return Variant(v);
                        }
                        case Variant::TypeS32: {
                                auto v = src.get<int32_t>(&ce);
                                if (ce.isError()) break;
                                return Variant(v);
                        }
                        case Variant::TypeU64: {
                                auto v = src.get<uint64_t>(&ce);
                                if (ce.isError()) break;
                                return Variant(v);
                        }
                        case Variant::TypeS64: {
                                auto v = src.get<int64_t>(&ce);
                                if (ce.isError()) break;
                                return Variant(v);
                        }
                        case Variant::TypeFloat: {
                                auto v = src.get<float>(&ce);
                                if (ce.isError()) break;
                                return Variant(v);
                        }
                        case Variant::TypeDouble: {
                                auto v = src.get<double>(&ce);
                                if (ce.isError()) break;
                                return Variant(v);
                        }
                        case Variant::TypeString: return Variant(str);
                        case Variant::TypeSize2D: {
                                // A successful parse yields a valid Variant of
                                // type TypeSize2D regardless of whether the
                                // resulting size is geometrically valid (W>0
                                // and H>0).  Defaults frequently start at 0x0
                                // as an "unset" marker, and the JSON
                                // round-trip for those defaults must not be
                                // rejected at the parse stage — the spec's
                                // own validate() (or a higher-level open()
                                // check) is the right place for geometric
                                // validation.
                                auto r = Size2Du32::fromString(str);
                                if (r.second().isError()) break;
                                return Variant(r.first());
                        }
                        case Variant::TypeFrameRate: {
                                // See the TypeSize2D rationale above — a
                                // syntactically successful parse is a parse
                                // success, even if the value happens to be
                                // FrameRate() or 0/0.  Don't conflate parse
                                // success with semantic validity.
                                auto r = FrameRate::fromString(str);
                                if (r.second().isError()) break;
                                return Variant(r.first());
                        }
                        case Variant::TypeVideoFormat: {
                                // See the TypeSize2D rationale above.
                                auto r = VideoFormat::fromString(str);
                                if (r.second().isError()) break;
                                return Variant(r.first());
                        }
                        case Variant::TypeRational: {
                                size_t slash = str.find('/');
                                if (slash == String::npos) break;
                                Error ne, de;
                                int   n = str.left(slash).to<int>(&ne);
                                int   d = str.mid(slash + 1).to<int>(&de);
                                if (ne.isError() || de.isError() || d == 0) break;
                                return Variant(Rational<int>(n, d));
                        }
                        case Variant::TypeTimecode: {
                                auto r = Timecode::fromString(str);
                                if (r.second().isError()) break;
                                return Variant(r.first());
                        }
                        case Variant::TypeFrameNumber: {
                                Error       pe;
                                FrameNumber fn = FrameNumber::fromString(str, &pe);
                                if (pe.isError()) break;
                                return Variant(fn);
                        }
                        case Variant::TypeFrameCount: {
                                Error      pe;
                                FrameCount fc = FrameCount::fromString(str, &pe);
                                if (pe.isError()) break;
                                return Variant(fc);
                        }
                        case Variant::TypeMediaDuration: {
                                Error         pe;
                                MediaDuration md = MediaDuration::fromString(str, &pe);
                                if (pe.isError()) break;
                                return Variant(md);
                        }
                        case Variant::TypeDuration: {
                                Error    pe;
                                Duration d = Duration::fromString(str, &pe);
                                if (pe.isError()) break;
                                return Variant(d);
                        }
                        case Variant::TypeDateTime: {
                                Error    de;
                                DateTime dt = DateTime::fromString(str, DateTime::DefaultFormat, &de);
                                if (de.isError()) break;
                                return Variant(dt);
                        }
                        case Variant::TypeColor: {
                                Color c = Color::fromString(str);
                                if (!c.isValid()) break;
                                return Variant(c);
                        }
                        case Variant::TypePixelFormat: {
                                // Use the error-aware lookup so the canonical
                                // "Invalid" sentinel name still parses cleanly
                                // — defaults that intentionally use Invalid as
                                // a pass-through marker (CSC's
                                // OutputPixelFormat) need to round-trip through
                                // JSON without the parse failing.
                                Error       pe;
                                PixelFormat pd = PixelFormat::lookup(str, &pe);
                                if (pe.isError()) break;
                                return Variant(pd);
                        }
                        case Variant::TypePixelMemLayout: {
                                Error          pe;
                                PixelMemLayout pf = PixelMemLayout::lookup(str, &pe);
                                if (pe.isError()) break;
                                return Variant(pf);
                        }
                        case Variant::TypeColorModel: {
                                ColorModel cm = ColorModel::lookup(str);
                                if (!cm.isValid()) break;
                                return Variant(cm);
                        }
                        case Variant::TypeVideoCodec: {
                                auto r = VideoCodec::fromString(str);
                                if (error(r).isError()) break;
                                return Variant(value(r));
                        }
                        case Variant::TypeAudioCodec: {
                                auto r = AudioCodec::fromString(str);
                                if (error(r).isError()) break;
                                return Variant(value(r));
                        }
                        case Variant::TypeAudioFormat: {
                                auto r = AudioFormat::fromString(str);
                                if (error(r).isError()) break;
                                return Variant(value(r));
                        }
                        case Variant::TypeAudioStreamDesc: {
                                auto r = AudioStreamDesc::fromString(str);
                                if (error(r).isError()) break;
                                return Variant(value(r));
                        }
                        case Variant::TypeAudioChannelMap: {
                                auto r = AudioChannelMap::fromString(str);
                                if (error(r).isError()) break;
                                return Variant(value(r));
                        }
                        case Variant::TypeAudioMarkerList: {
                                auto r = AudioMarkerList::fromString(str);
                                if (error(r).isError()) break;
                                return Variant(value(r));
                        }
                        case Variant::TypeEnum: {
                                if (!enumType.isValid()) break;
                                // Canonical empty form "::" maps back to a
                                // typeless / unset Enum — defaults declared
                                // as setDefault(Enum()) (e.g. V4l2AutoExposure
                                // for "use device default") emit "::" via
                                // Enum::toString and need to round-trip
                                // through JSON cleanly.  validate() honours
                                // this by accepting an invalid Enum when an
                                // enumType is set.
                                if (str == "::") return Variant(Enum());
                                Enum e(enumType, str);
                                if (e.hasListedValue()) return Variant(e);
                                // Fall back to fully qualified "TypeName::ValueName".
                                Error lookErr;
                                Enum  fq = Enum::lookup(str, &lookErr);
                                if (lookErr.isOk() && fq.type() == enumType && fq.hasListedValue()) return Variant(fq);
                                break;
                        }
                        case Variant::TypeEnumList: {
                                if (!enumType.isValid()) break;
                                Error    listErr;
                                EnumList list = EnumList::fromString(enumType, str, &listErr);
                                if (listErr.isError()) break;
                                return Variant(list);
                        }
                        case Variant::TypeStringList: return Variant(str.split(","));
                        case Variant::TypeUrl: {
                                Result<Url> r = Url::fromString(str);
                                if (r.second().isError() || !r.first().isValid()) break;
                                return Variant(r.first());
                        }
                        case Variant::TypeWindowedStat: {
                                auto r = WindowedStat::fromString(str);
                                if (error(r).isError()) break;
                                return Variant(value(r));
                        }
                        case Variant::TypeVariantList: {
                                Error       pe;
                                VariantList vl = VariantList::fromJsonString(str, &pe);
                                if (pe.isError()) break;
                                return Variant(vl);
                        }
                        case Variant::TypeVariantMap: {
                                Error      pe;
                                VariantMap vm = VariantMap::fromJsonString(str, &pe);
                                if (pe.isError()) break;
                                return Variant(vm);
                        }
#if PROMEKI_ENABLE_NETWORK
                        case Variant::TypeSocketAddress: {
                                // SocketAddress() (null) serializes to an empty
                                // String, so accept that as the canonical
                                // sentinel form — defaults that mark "no
                                // destination configured" must round-trip
                                // through JSON.  A non-empty string still has
                                // to parse successfully.
                                if (str.isEmpty()) return Variant(SocketAddress());
                                auto r = SocketAddress::fromString(str);
                                if (r.second().isError()) break;
                                return Variant(r.first());
                        }
#endif
                        default: break;
                }
                if (err) *err = Error::ConversionFailed;
                return Variant();
        }

} // anonymous namespace

// ============================================================================
// acceptsType / validate
// ============================================================================

bool VariantSpec::acceptsType(Type type) const {
        if (_types.isEmpty()) return true;
        for (size_t i = 0; i < _types.size(); i++) {
                if (_types[i] == type) return true;
                if (numericCompatible(_types[i], type)) return true;
        }
        return false;
}

bool VariantSpec::validate(const Variant &value, Error *err) const {
        // 1. Type check — numeric types are interchangeable since
        //    Variant converts between all numeric widths automatically.
        if (!_types.isEmpty()) {
                bool typeOk = false;
                for (size_t i = 0; i < _types.size(); i++) {
                        if (_types[i] == value.type()) {
                                typeOk = true;
                                break;
                        }
                        if (numericCompatible(_types[i], value.type())) {
                                typeOk = true;
                                break;
                        }
                }
                if (!typeOk) {
                        if (err) *err = Error::InvalidArgument;
                        return false;
                }
        }

        // 2. Range check (numeric values only)
        if (_min.isValid() || _max.isValid()) {
                Error  convErr;
                double val = value.get<double>(&convErr);
                if (!convErr.isError()) {
                        if (_min.isValid()) {
                                double minVal = _min.get<double>();
                                if (val < minVal) {
                                        if (err) *err = Error::OutOfRange;
                                        return false;
                                }
                        }
                        if (_max.isValid()) {
                                double maxVal = _max.get<double>();
                                if (val > maxVal) {
                                        if (err) *err = Error::OutOfRange;
                                        return false;
                                }
                        }
                }
        }

        // 3. Enum type check — applies to both Enum and EnumList values,
        //    since EnumList pins every element to a single Enum::Type.
        //    A typeless Enum() is treated as "unset" and accepted when
        //    an enumType is declared, so defaults like
        //    setDefault(Enum()) (used to mean "use the backend's own
        //    fallback") survive a JSON round-trip.
        if (_enumType.isValid() && value.type() == Variant::TypeEnum) {
                Enum e = value.get<Enum>();
                if (e.isValid() && e.type() != _enumType) {
                        if (err) *err = Error::InvalidArgument;
                        return false;
                }
        }
        if (_enumType.isValid() && value.type() == Variant::TypeEnumList) {
                EnumList list = value.get<EnumList>();
                if (list.elementType() != _enumType) {
                        if (err) *err = Error::InvalidArgument;
                        return false;
                }
        }

        // 4. Nested-element validation for VariantList: every element
        //    must satisfy the declared element spec, if one exists.
        //    peek<>() borrows the list rather than deep-copying it.
        if (_elementSpec.isValid()) {
                if (const VariantList *vl = value.peek<VariantList>()) {
                        const size_t n = vl->size();
                        for (size_t i = 0; i < n; ++i) {
                                Error eErr;
                                if (!_elementSpec->validate((*vl)[i], &eErr)) {
                                        if (err) *err = eErr.isError() ? eErr : Error::InvalidArgument;
                                        return false;
                                }
                        }
                }
        }

        // 5. Nested-value validation for VariantMap: every value must
        //    satisfy the declared value spec.  Keys are not validated
        //    here — they are always String and any per-key constraints
        //    belong to the consumer.
        if (_valueSpec.isValid()) {
                if (const VariantMap *vm = value.peek<VariantMap>()) {
                        Error mapErr = Error::Ok;
                        vm->forEach([&](const String &, const Variant &v) {
                                if (mapErr.isError()) return;
                                Error vErr;
                                if (!_valueSpec->validate(v, &vErr)) {
                                        mapErr = vErr.isError() ? vErr : Error::InvalidArgument;
                                }
                        });
                        if (mapErr.isError()) {
                                if (err) *err = mapErr;
                                return false;
                        }
                }
        }

        if (err) *err = Error::Ok;
        return true;
}

// ============================================================================
// Formatting
// ============================================================================

String VariantSpec::typeName() const {
        if (_types.isEmpty()) return "(any)";
        auto annotate = [this](Variant::Type t) -> String {
                String base = singleTypeName(t, _enumType);
                // For container types, surface the element / value shape
                // so help output and introspection can show "VariantList<int>"
                // or "VariantMap<int>" instead of just the bare container.
                if (t == Variant::TypeVariantList && _elementSpec.isValid()) {
                        base += "<" + _elementSpec->typeName() + ">";
                } else if (t == Variant::TypeVariantMap && _valueSpec.isValid()) {
                        base += "<" + _valueSpec->typeName() + ">";
                }
                return base;
        };
        if (_types.size() == 1) return annotate(_types[0]);
        String result;
        for (size_t i = 0; i < _types.size(); i++) {
                if (i > 0) result += " | ";
                result += annotate(_types[i]);
        }
        return result;
}

namespace {

        /// Format a numeric Variant as a compact string (no trailing zeros for floats).
        String formatNumeric(const Variant &v) {
                if (v.type() == Variant::TypeFloat || v.type() == Variant::TypeDouble) {
                        double d = v.get<double>();
                        // Use %g for compact representation (strips trailing zeros).
                        char buf[64];
                        std::snprintf(buf, sizeof(buf), "%g", d);
                        return String(buf);
                }
                Error e;
                return v.get<String>(&e);
        }

} // anonymous namespace

String VariantSpec::rangeString() const {
        if (!_min.isValid() && !_max.isValid()) return String();
        if (_min.isValid() && _max.isValid()) {
                return formatNumeric(_min) + " - " + formatNumeric(_max);
        }
        if (_min.isValid()) {
                return ">= " + formatNumeric(_min);
        }
        return "<= " + formatNumeric(_max);
}

String VariantSpec::defaultString() const {
        if (!_default.isValid()) return "(none)";
        if (_default.type() == Variant::TypeEnum) {
                Enum   e = _default.get<Enum>();
                String vn = e.valueName();
                return vn.isEmpty() ? "(invalid)" : vn;
        }
        if (_default.type() == Variant::TypeString) {
                String s = _default.get<String>();
                return s.isEmpty() ? "(empty)" : s;
        }
        if (_default.type() == Variant::TypeFloat || _default.type() == Variant::TypeDouble) {
                return formatNumeric(_default);
        }
        Error  se;
        String s = _default.get<String>(&se);
        return se.isError() ? String("(") + _default.typeName() + ")" : s;
}

// ============================================================================
// String parsing
// ============================================================================

Variant VariantSpec::parseString(const String &str, Error *err) const {
        if (err) *err = Error::Ok;

        // No type constraint — return as String.
        if (_types.isEmpty()) return Variant(str);

        // For polymorphic specs, try each type in order.
        for (size_t i = 0; i < _types.size(); i++) {
                Error   e;
                Variant result = parseAsType(_types[i], _enumType, str, &e);
                if (!e.isError()) return result;
        }

        if (err) *err = Error::ConversionFailed;
        return Variant();
}

// ============================================================================
// Help output
// ============================================================================

Variant VariantSpec::coerce(const Variant &val, Error *err) const {
        if (err) *err = Error::Ok;

        // Top-level String coercion: spec wants a non-String native type
        // and the JSON layer handed us a String — feed it through
        // parseString so the database stores the right Variant alternative.
        if (val.type() == Variant::TypeString && !_types.isEmpty() && !acceptsType(Variant::TypeString)) {
                Error   pe;
                Variant parsed = parseString(val.get<String>(), &pe);
                if (pe.isError()) {
                        if (err) *err = pe;
                        return Variant();
                }
                return parsed;
        }

        // VariantList with declared element-spec — recurse.  We rebuild
        // the list element-by-element rather than mutating in place so
        // the original Variant is left untouched.  peek<>() borrows the
        // source list to avoid an extra deep copy on entry.
        if (_elementSpec.isValid()) {
                if (const VariantList *src = val.peek<VariantList>()) {
                        const size_t n = src->size();
                        VariantList  out;
                        out.reserve(n);
                        for (size_t i = 0; i < n; ++i) {
                                Error   eErr;
                                Variant coerced = _elementSpec->coerce((*src)[i], &eErr);
                                if (eErr.isError()) {
                                        if (err) *err = eErr;
                                        return Variant();
                                }
                                out.pushToBack(std::move(coerced));
                        }
                        return Variant(std::move(out));
                }
        }

        // VariantMap with declared value-spec — recurse over values.
        // Keys remain as-is (always String).
        if (_valueSpec.isValid()) {
                if (const VariantMap *src = val.peek<VariantMap>()) {
                        VariantMap out;
                        Error      mapErr = Error::Ok;
                        src->forEach([&](const String &k, const Variant &v) {
                                if (mapErr.isError()) return;
                                Error   vErr;
                                Variant coerced = _valueSpec->coerce(v, &vErr);
                                if (vErr.isError()) {
                                        mapErr = vErr;
                                        return;
                                }
                                out.insert(k, std::move(coerced));
                        });
                        if (mapErr.isError()) {
                                if (err) *err = mapErr;
                                return Variant();
                        }
                        return Variant(std::move(out));
                }
        }

        return val;
}

String VariantSpec::detailsString() const {
        // Compact "details" column: type, optional range, default.
        // "def" is deliberately short so the column stays tight even
        // when several backends declare many keys in the same help
        // block.
        String details = "(" + typeName() + ")";
        String range = rangeString();
        if (!range.isEmpty()) details += " [" + range + "]";
        details += " [def: " + defaultString() + "]";
        return details;
}

PROMEKI_NAMESPACE_END
