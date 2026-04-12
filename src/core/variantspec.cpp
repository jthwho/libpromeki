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
#include <promeki/enumlist.h>
#include <promeki/framerate.h>
#include <promeki/pixeldesc.h>
#include <promeki/pixelformat.h>
#include <promeki/rational.h>
#include <promeki/size2d.h>
#include <promeki/stringlist.h>
#include <promeki/timecode.h>
#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <promeki/socketaddress.h>
#endif

PROMEKI_NAMESPACE_BEGIN

namespace {

/// Returns true if the Variant::Type is any integer or floating-point type.
bool isNumericType(Variant::Type t) {
        switch(t) {
                case Variant::TypeU8:
                case Variant::TypeS8:
                case Variant::TypeU16:
                case Variant::TypeS16:
                case Variant::TypeU32:
                case Variant::TypeS32:
                case Variant::TypeU64:
                case Variant::TypeS64:
                case Variant::TypeFloat:
                case Variant::TypeDouble:
                        return true;
                default:
                        return false;
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
        switch(t) {
                case Variant::TypeBool:         return "bool";
                case Variant::TypeU8:           return "uint8";
                case Variant::TypeS8:           return "int8";
                case Variant::TypeU16:          return "uint16";
                case Variant::TypeS16:          return "int16";
                case Variant::TypeU32:          return "uint";
                case Variant::TypeS32:          return "int";
                case Variant::TypeU64:          return "uint64";
                case Variant::TypeS64:          return "int64";
                case Variant::TypeFloat:        return "float";
                case Variant::TypeDouble:       return "double";
                case Variant::TypeString:       return "String";
                case Variant::TypeDateTime:     return "DateTime";
                case Variant::TypeTimeStamp:    return "TimeStamp";
                case Variant::TypeSize2D:       return "Size2D";
                case Variant::TypeUUID:         return "UUID";
                case Variant::TypeUMID:         return "UMID";
                case Variant::TypeTimecode:     return "Timecode";
                case Variant::TypeRational:     return "Rational";
                case Variant::TypeFrameRate:    return "FrameRate";
                case Variant::TypeStringList:   return "StringList";
                case Variant::TypeColor:        return "Color";
                case Variant::TypeColorModel:   return "ColorModel";
                case Variant::TypeMemSpace:     return "MemSpace";
                case Variant::TypePixelFormat:  return "PixelFormat";
                case Variant::TypePixelDesc:    return "PixelDesc";
                case Variant::TypeEnum: {
                        if(enumType.isValid()) return String("Enum ") + enumType.name();
                        return "Enum";
                }
                case Variant::TypeEnumList: {
                        if(enumType.isValid()) return String("EnumList ") + enumType.name();
                        return "EnumList";
                }
#if PROMEKI_ENABLE_NETWORK
                case Variant::TypeSocketAddress: return "SocketAddress";
                case Variant::TypeSdpSession:    return "SdpSession";
#endif
                default:                        return "unknown";
        }
}

/// Attempt to parse @p str as a specific Variant::Type.  Returns an
/// invalid Variant and sets @p err on failure.
Variant parseAsType(Variant::Type type, Enum::Type enumType,
                    const String &str, Error *err) {
        Variant src(str);
        Error ce;

        switch(type) {
                case Variant::TypeBool: {
                        String low = str.toLower();
                        if(low == "true" || low == "yes" || low == "1" || low == "on")
                                return Variant(true);
                        if(low == "false" || low == "no" || low == "0" || low == "off")
                                return Variant(false);
                        if(err) *err = Error::Invalid;
                        return Variant();
                }
                case Variant::TypeU8:  { auto v = src.get<uint8_t >(&ce); if(ce.isError()) break; return Variant(v); }
                case Variant::TypeS8:  { auto v = src.get<int8_t  >(&ce); if(ce.isError()) break; return Variant(v); }
                case Variant::TypeU16: { auto v = src.get<uint16_t>(&ce); if(ce.isError()) break; return Variant(v); }
                case Variant::TypeS16: { auto v = src.get<int16_t >(&ce); if(ce.isError()) break; return Variant(v); }
                case Variant::TypeU32: { auto v = src.get<uint32_t>(&ce); if(ce.isError()) break; return Variant(v); }
                case Variant::TypeS32: { auto v = src.get<int32_t >(&ce); if(ce.isError()) break; return Variant(v); }
                case Variant::TypeU64: { auto v = src.get<uint64_t>(&ce); if(ce.isError()) break; return Variant(v); }
                case Variant::TypeS64: { auto v = src.get<int64_t >(&ce); if(ce.isError()) break; return Variant(v); }
                case Variant::TypeFloat:  { auto v = src.get<float >(&ce); if(ce.isError()) break; return Variant(v); }
                case Variant::TypeDouble: { auto v = src.get<double>(&ce); if(ce.isError()) break; return Variant(v); }
                case Variant::TypeString:
                        return Variant(str);
                case Variant::TypeSize2D: {
                        auto r = Size2Du32::fromString(str);
                        if(r.second().isError() || !r.first().isValid()) break;
                        return Variant(r.first());
                }
                case Variant::TypeFrameRate: {
                        auto r = FrameRate::fromString(str);
                        if(r.second().isError() || !r.first().isValid()) break;
                        return Variant(r.first());
                }
                case Variant::TypeRational: {
                        size_t slash = str.find('/');
                        if(slash == String::npos) break;
                        Error ne, de;
                        int n = str.left(slash).to<int>(&ne);
                        int d = str.mid(slash + 1).to<int>(&de);
                        if(ne.isError() || de.isError() || d == 0) break;
                        return Variant(Rational<int>(n, d));
                }
                case Variant::TypeTimecode: {
                        auto r = Timecode::fromString(str);
                        if(r.second().isError()) break;
                        return Variant(r.first());
                }
                case Variant::TypeDateTime: {
                        Error de;
                        DateTime dt = DateTime::fromString(str, DateTime::DefaultFormat, &de);
                        if(de.isError()) break;
                        return Variant(dt);
                }
                case Variant::TypeColor: {
                        Color c = Color::fromString(str);
                        if(!c.isValid()) break;
                        return Variant(c);
                }
                case Variant::TypePixelDesc: {
                        PixelDesc pd = PixelDesc::lookup(str);
                        if(!pd.isValid()) break;
                        return Variant(pd);
                }
                case Variant::TypePixelFormat: {
                        PixelFormat pf = PixelFormat::lookup(str);
                        if(!pf.isValid()) break;
                        return Variant(pf);
                }
                case Variant::TypeColorModel: {
                        ColorModel cm = ColorModel::lookup(str);
                        if(!cm.isValid()) break;
                        return Variant(cm);
                }
                case Variant::TypeEnum: {
                        if(!enumType.isValid()) break;
                        Enum e(enumType, str);
                        if(e.hasListedValue()) return Variant(e);
                        // Fall back to fully qualified "TypeName::ValueName".
                        Error lookErr;
                        Enum fq = Enum::lookup(str, &lookErr);
                        if(lookErr.isOk() && fq.type() == enumType && fq.hasListedValue())
                                return Variant(fq);
                        break;
                }
                case Variant::TypeEnumList: {
                        if(!enumType.isValid()) break;
                        Error listErr;
                        EnumList list = EnumList::fromString(enumType, str, &listErr);
                        if(listErr.isError()) break;
                        return Variant(list);
                }
                case Variant::TypeStringList:
                        return Variant(str.split(","));
#if PROMEKI_ENABLE_NETWORK
                case Variant::TypeSocketAddress: {
                        auto r = SocketAddress::fromString(str);
                        if(r.second().isError() || r.first().isNull()) break;
                        return Variant(r.first());
                }
#endif
                default:
                        break;
        }
        if(err) *err = Error::ConversionFailed;
        return Variant();
}

} // anonymous namespace

// ============================================================================
// acceptsType / validate
// ============================================================================

bool VariantSpec::acceptsType(Type type) const {
        if(_types.isEmpty()) return true;
        for(size_t i = 0; i < _types.size(); i++) {
                if(_types[i] == type) return true;
                if(numericCompatible(_types[i], type)) return true;
        }
        return false;
}

bool VariantSpec::validate(const Variant &value, Error *err) const {
        // 1. Type check — numeric types are interchangeable since
        //    Variant converts between all numeric widths automatically.
        if(!_types.isEmpty()) {
                bool typeOk = false;
                for(size_t i = 0; i < _types.size(); i++) {
                        if(_types[i] == value.type()) {
                                typeOk = true;
                                break;
                        }
                        if(numericCompatible(_types[i], value.type())) {
                                typeOk = true;
                                break;
                        }
                }
                if(!typeOk) {
                        if(err) *err = Error::InvalidArgument;
                        return false;
                }
        }

        // 2. Range check (numeric values only)
        if(_min.isValid() || _max.isValid()) {
                Error convErr;
                double val = value.get<double>(&convErr);
                if(!convErr.isError()) {
                        if(_min.isValid()) {
                                double minVal = _min.get<double>();
                                if(val < minVal) {
                                        if(err) *err = Error::OutOfRange;
                                        return false;
                                }
                        }
                        if(_max.isValid()) {
                                double maxVal = _max.get<double>();
                                if(val > maxVal) {
                                        if(err) *err = Error::OutOfRange;
                                        return false;
                                }
                        }
                }
        }

        // 3. Enum type check — applies to both Enum and EnumList values,
        //    since EnumList pins every element to a single Enum::Type.
        if(_enumType.isValid() && value.type() == Variant::TypeEnum) {
                Enum e = value.get<Enum>();
                if(e.type() != _enumType) {
                        if(err) *err = Error::InvalidArgument;
                        return false;
                }
        }
        if(_enumType.isValid() && value.type() == Variant::TypeEnumList) {
                EnumList list = value.get<EnumList>();
                if(list.elementType() != _enumType) {
                        if(err) *err = Error::InvalidArgument;
                        return false;
                }
        }

        if(err) *err = Error::Ok;
        return true;
}

// ============================================================================
// Formatting
// ============================================================================

String VariantSpec::typeName() const {
        if(_types.isEmpty()) return "(any)";
        if(_types.size() == 1) return singleTypeName(_types[0], _enumType);
        String result;
        for(size_t i = 0; i < _types.size(); i++) {
                if(i > 0) result += " | ";
                result += singleTypeName(_types[i], _enumType);
        }
        return result;
}

namespace {

/// Format a numeric Variant as a compact string (no trailing zeros for floats).
String formatNumeric(const Variant &v) {
        if(v.type() == Variant::TypeFloat || v.type() == Variant::TypeDouble) {
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
        if(!_min.isValid() && !_max.isValid()) return String();
        if(_min.isValid() && _max.isValid()) {
                return formatNumeric(_min) + " - " + formatNumeric(_max);
        }
        if(_min.isValid()) {
                return ">= " + formatNumeric(_min);
        }
        return "<= " + formatNumeric(_max);
}

String VariantSpec::defaultString() const {
        if(!_default.isValid()) return "(none)";
        if(_default.type() == Variant::TypeEnum) {
                Enum e = _default.get<Enum>();
                String vn = e.valueName();
                return vn.isEmpty() ? "(invalid)" : vn;
        }
        if(_default.type() == Variant::TypeString) {
                String s = _default.get<String>();
                return s.isEmpty() ? "(empty)" : s;
        }
        if(_default.type() == Variant::TypeFloat || _default.type() == Variant::TypeDouble) {
                return formatNumeric(_default);
        }
        Error se;
        String s = _default.get<String>(&se);
        return se.isError() ? String("(") + _default.typeName() + ")" : s;
}

// ============================================================================
// String parsing
// ============================================================================

Variant VariantSpec::parseString(const String &str, Error *err) const {
        if(err) *err = Error::Ok;

        // No type constraint — return as String.
        if(_types.isEmpty()) return Variant(str);

        // For polymorphic specs, try each type in order.
        for(size_t i = 0; i < _types.size(); i++) {
                Error e;
                Variant result = parseAsType(_types[i], _enumType, str, &e);
                if(!e.isError()) return result;
        }

        if(err) *err = Error::ConversionFailed;
        return Variant();
}

// ============================================================================
// Help output
// ============================================================================

String VariantSpec::detailsString() const {
        // Compact "details" column: type, optional range, default.
        // "def" is deliberately short so the column stays tight even
        // when several backends declare many keys in the same help
        // block.
        String details = "(" + typeName() + ")";
        String range = rangeString();
        if(!range.isEmpty()) details += " [" + range + "]";
        details += " [def: " + defaultString() + "]";
        return details;
}

PROMEKI_NAMESPACE_END
