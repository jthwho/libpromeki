/**
 * @file      variant.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Implementation of @ref promeki::Variant on top of the @ref promeki::DataType
 * registry.  The Variant API surface stays unchanged; the storage moves from
 * a closed @c std::variant alternative list to a refcounted @c VariantBox
 * payload whose type is identified by a registered @ref DataType ID.
 *
 * Two classes of state live here:
 *
 *  - @ref VariantBox memory management — the trailing-payload allocator,
 *    the hand-rolled @c SharedPtr contract (RefCount + custom
 *    @c _promeki_clone), and the @c Detail:: thunks the inline header
 *    template definitions in @c variant.h delegate to.
 *
 *  - Conversion: @c convertOne<From,To> hosts the per-(From,To)
 *    compile-time branching lifted from the legacy @c variant.tpp.  A
 *    small static converter registry maps @c (FromId,ToId) pairs to
 *    lambdas that invoke the right @c convertOne instantiation;
 *    @c Variant::get<T> consults it through @ref Variant::findConverter.
 *    @ref Variant::registerBuiltinConverters wires every bespoke
 *    (From, To) pair in the builtin set; it is called from
 *    @ref registerBuiltinDataTypes in @c datatype.cpp after every type
 *    record is in the registry.
 *
 * Builtin @ref DataType records themselves are registered from
 * @c datatype.cpp's @ref registerBuiltinDataTypes; auto-discoverable
 * @c String <-> @p T converters are wired up implicitly per type by
 * @c Detail::registerAutoConverters (defined in @c variant.h).
 */

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>
#include <promeki/variant.h>
#include <promeki/datatype.h>
#include <promeki/datastream.h>
#include <promeki/json.h>
#include <promeki/hashmap.h>
#include <promeki/mutex.h>
#include <promeki/buffer.h>
#include <promeki/logger.h>
// Types referenced by convertOne<From, To> and the builtin converter
// groups below — variant.cpp needs them complete so the converter
// lambdas can read / write their payloads.  Types whose only role is
// to be in the DataType registry are registered (and included) from
// datatype.cpp instead.
#include <promeki/timestamp.h>
#include <promeki/mediatimestamp.h>
#include <promeki/framenumber.h>
#include <promeki/framecount.h>
#include <promeki/duration.h>
#include <promeki/mediaduration.h>
#include <promeki/datetime.h>
#include <promeki/size2d.h>
#include <promeki/uuid.h>
#include <promeki/umid.h>
#include <promeki/timecode.h>
#include <promeki/rational.h>
#include <promeki/framerate.h>
#include <promeki/videoformat.h>
#include <promeki/stringlist.h>
#include <promeki/color.h>
#include <promeki/ancformat.h>
#include <promeki/audiochannelmap.h>
#include <promeki/audiocodec.h>
#include <promeki/audioformat.h>
#include <promeki/audiostreamdesc.h>
#include <promeki/colormodel.h>
#include <promeki/memspace.h>
#include <promeki/pixelmemlayout.h>
#include <promeki/pixelformat.h>
#include <promeki/videocodec.h>
#include <promeki/masteringdisplay.h>
#include <promeki/contentlightlevel.h>
#include <promeki/enumlist.h>
#include <promeki/url.h>
#include <promeki/windowedstat.h>
#include <promeki/socketaddress.h>
#include <promeki/sdpsession.h>
#include <promeki/macaddress.h>
#include <promeki/eui64.h>
#include <nlohmann/json.hpp>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(Variant)

// ============================================================================
// VariantBox out-of-line member functions
//
// The struct layout (RefCount + typeData pointer) and the inline
// helpers (payloadOffset / totalSize) live in variant.h so that
// SharedPtr<VariantBox>'s IsSharedObject SFINAE check sees the
// _promeki_refct member without requiring this TU.  The functions
// below are the lifetime-critical operations that need access to
// the registered ops table.
// ============================================================================

void *VariantBox::payload() {
        return reinterpret_cast<uint8_t *>(this) + payloadOffset(typeData->align);
}

const void *VariantBox::payload() const {
        return reinterpret_cast<const uint8_t *>(this) + payloadOffset(typeData->align);
}

VariantBox *VariantBox::_promeki_clone() const {
        return allocate(typeData, payload());
}

VariantBox *VariantBox::allocate(const DataType::Data *td, const void *src) {
        if (td == nullptr) return nullptr;
        const size_t total = totalSize(td);
        const size_t headerAlign = alignof(VariantBox);
        const size_t needAlign = headerAlign > td->align ? headerAlign : td->align;
        void        *mem = ::operator new(total, std::align_val_t(needAlign));
        VariantBox  *box = ::new (mem) VariantBox;
        box->typeData = td;
        if (src != nullptr && td->ops.copyConstruct != nullptr) {
                td->ops.copyConstruct(box->payload(), src);
        } else if (src == nullptr && td->ops.defaultConstruct != nullptr) {
                td->ops.defaultConstruct(box->payload());
        }
        return box;
}

void VariantBox::operator delete(void *p, std::size_t /*sz*/) noexcept {
        if (p == nullptr) return;
        VariantBox *box = static_cast<VariantBox *>(p);
        if (box->typeData != nullptr && box->typeData->ops.destroy != nullptr) {
                box->typeData->ops.destroy(box->payload());
        }
        const size_t headerAlign = alignof(VariantBox);
        const size_t payloadAlign = (box->typeData != nullptr) ? box->typeData->align : 1;
        const size_t needAlign = headerAlign > payloadAlign ? headerAlign : payloadAlign;
        box->~VariantBox();
        ::operator delete(p, std::align_val_t(needAlign));
        return;
}

// ============================================================================
// Detail thunks the inline templates in variant.h delegate to
// ============================================================================

namespace Detail {

SharedPtr<VariantBox> makeVariantBox(const DataType::Data *typeData, const void *value) {
        if (typeData == nullptr) return SharedPtr<VariantBox>();
        VariantBox *raw = VariantBox::allocate(typeData, value);
        if (raw == nullptr) return SharedPtr<VariantBox>();
        return SharedPtr<VariantBox>::takeOwnership(raw);
}

SharedPtr<VariantBox> makeVariantBoxMove(const DataType::Data *typeData, void *value) {
        if (typeData == nullptr) return SharedPtr<VariantBox>();
        // Allocate without copy/default; we'll move-construct the
        // payload bytes ourselves so the source is left in its
        // moved-from state rather than copied.
        const size_t total = VariantBox::totalSize(typeData);
        const size_t headerAlign = alignof(VariantBox);
        const size_t needAlign   = headerAlign > typeData->align ? headerAlign : typeData->align;
        void        *mem = ::operator new(total, std::align_val_t(needAlign));
        VariantBox  *raw = ::new (mem) VariantBox;
        raw->typeData = typeData;
        if (value != nullptr && typeData->ops.moveConstruct != nullptr) {
                typeData->ops.moveConstruct(raw->payload(), value);
        } else if (value != nullptr && typeData->ops.copyConstruct != nullptr) {
                typeData->ops.copyConstruct(raw->payload(), value);
        } else if (value == nullptr && typeData->ops.defaultConstruct != nullptr) {
                typeData->ops.defaultConstruct(raw->payload());
        }
        return SharedPtr<VariantBox>::takeOwnership(raw);
}

} // namespace Detail

// ============================================================================
// Converter registry — public API on Variant
// ============================================================================

namespace {

using ConverterMap = HashMap<uint32_t, Variant::ConverterFn>;

struct ConverterRegistry {
                Mutex        mutex;
                ConverterMap map;
};

ConverterRegistry &converterRegistry() {
                static ConverterRegistry r;
                return r;
}

constexpr uint32_t converterKey(DataTypeID from, DataTypeID to) {
        return (static_cast<uint32_t>(static_cast<uint16_t>(from)) << 16) |
                static_cast<uint32_t>(static_cast<uint16_t>(to));
}

} // anonymous namespace

void Variant::registerConverter(DataTypeID from, DataTypeID to, ConverterFn fn) {
        auto         &r = converterRegistry();
        Mutex::Locker lock(r.mutex);
        r.map.insert(converterKey(from, to), fn);
        return;
}

Variant::ConverterFn Variant::findConverter(DataTypeID from, DataTypeID to) {
        auto         &r = converterRegistry();
        Mutex::Locker lock(r.mutex);
        auto          it = r.map.find(converterKey(from, to));
        return (it != r.map.end()) ? it->second : nullptr;
}

Variant Variant::convertTo(DataTypeID to, Error *err) const {
        if (err != nullptr) *err = Error::Ok;
        if (!isValid()) {
                if (err != nullptr) *err = Error::Invalid;
                return Variant();
        }
        ConverterFn fn = findConverter(type(), to);
        if (fn == nullptr) {
                if (err != nullptr) *err = Error::Invalid;
                return Variant();
        }
        return fn(*this, err);
}

// ============================================================================
// Variant non-template methods
// ============================================================================

DataTypeID Variant::type() const {
        if (_box.isNull()) return DataTypeInvalid;
        const DataType::Data *td = _box->typeData;
        return td != nullptr ? td->id : DataTypeInvalid;
}

DataType Variant::dataType() const {
        return _box.isNull() ? DataType() : DataType(_box->typeData);
}

const void *Variant::payloadPtr() const {
        return _box.isNull() ? nullptr : _box->payload();
}

Variant Variant::createDefault(const DataType &dt) {
        if (!dt.isValid() || dt.data()->ops.defaultConstruct == nullptr) return Variant();
        Variant out;
        out._box = Detail::makeVariantBox(dt.data(), nullptr);
        return out;
}

Variant Variant::readFromStream(DataStream &stream, const DataType &dt) {
        if (!dt.isValid()) return Variant();
        const DataType::Data *td = dt.data();
        if (td->ops.defaultConstruct == nullptr || td->ops.readStream == nullptr) return Variant();
        VariantBox *raw = VariantBox::allocate(td, nullptr);
        if (raw == nullptr) return Variant();
        td->ops.readStream(stream, raw->payload());
        Variant out;
        out._box = SharedPtr<VariantBox>::takeOwnership(raw);
        return out;
}

const char *Variant::typeName(DataTypeID id) {
        DataType dt(id);
        return dt.isValid() ? dt.name() : "Invalid";
}

String Variant::toString(Error *err) const {
        if (err != nullptr) *err = Error::Ok;
        if (_box.isNull()) return String();
        const DataType::Data *td = _box->typeData;
        if (td == nullptr || td->ops.toString == nullptr) {
                if (err != nullptr) *err = Error::Invalid;
                return String();
        }
        return td->ops.toString(_box->payload(), err);
}

Variant Variant::toStandardType() const {
        if (!isValid()) return *this;
        DataTypeID t = type();
        switch (t) {
                case DataTypeBool:
                case DataTypeUInt8:
                case DataTypeInt8:
                case DataTypeUInt16:
                case DataTypeInt16:
                case DataTypeUInt32:
                case DataTypeInt32:
                case DataTypeUInt64:
                case DataTypeInt64:
                case DataTypeFloat:
                case DataTypeDouble: return *this;
                default:         return get<String>();
        }
}

// ----------------------------------------------------------------------------
// Variant::format — applies a type-specific format spec to the held value.
// ----------------------------------------------------------------------------

namespace {

template <typename T>
String tryFormatAs(const std::string &fmtStr, const T &val, const String &fallback, bool &handled) {
        if constexpr (std::is_default_constructible_v<std::formatter<T, char>>) {
                handled = true;
                return String(std::vformat(fmtStr, std::make_format_args(val)));
        } else {
                (void)val;
                handled = true;
                std::string_view sv(fallback.cstr(), fallback.byteCount());
                return String(std::vformat(fmtStr, std::make_format_args(sv)));
        }
}

} // namespace

String Variant::format(const String &spec, Error *err) const {
        if (err != nullptr) *err = Error::Ok;
        String defaultStr = get<String>();
        if (spec.isEmpty()) return defaultStr;

        std::string fmtStr;
        fmtStr.reserve(spec.byteCount() + 3);
        fmtStr += "{:";
        fmtStr.append(spec.cstr(), spec.byteCount());
        fmtStr += '}';

        try {
                bool handled = false;
                switch (type()) {
                        case DataTypeInvalid: return String();
                        case DataTypeBool:    return tryFormatAs<bool>(fmtStr, *peek<bool>(), defaultStr, handled);
                        case DataTypeUInt8:      return tryFormatAs<uint8_t>(fmtStr, *peek<uint8_t>(), defaultStr, handled);
                        case DataTypeInt8:      return tryFormatAs<int8_t>(fmtStr, *peek<int8_t>(), defaultStr, handled);
                        case DataTypeUInt16:     return tryFormatAs<uint16_t>(fmtStr, *peek<uint16_t>(), defaultStr, handled);
                        case DataTypeInt16:     return tryFormatAs<int16_t>(fmtStr, *peek<int16_t>(), defaultStr, handled);
                        case DataTypeUInt32:     return tryFormatAs<uint32_t>(fmtStr, *peek<uint32_t>(), defaultStr, handled);
                        case DataTypeInt32:     return tryFormatAs<int32_t>(fmtStr, *peek<int32_t>(), defaultStr, handled);
                        case DataTypeUInt64:     return tryFormatAs<uint64_t>(fmtStr, *peek<uint64_t>(), defaultStr, handled);
                        case DataTypeInt64:     return tryFormatAs<int64_t>(fmtStr, *peek<int64_t>(), defaultStr, handled);
                        case DataTypeFloat:   return tryFormatAs<float>(fmtStr, *peek<float>(), defaultStr, handled);
                        case DataTypeDouble:  return tryFormatAs<double>(fmtStr, *peek<double>(), defaultStr, handled);
                        case DataTypeTimecode:
                                return tryFormatAs<Timecode>(fmtStr, *peek<Timecode>(), defaultStr, handled);
#if PROMEKI_ENABLE_PROAV
                        case DataTypeVideoFormat:
                                return tryFormatAs<VideoFormat>(fmtStr, *peek<VideoFormat>(), defaultStr,
                                                                handled);
#endif
                        case DataTypeString:  return tryFormatAs<String>(fmtStr, *peek<String>(), defaultStr, handled);
                        default: {
                                // Fall back to formatting the spec against the
                                // String form so width / fill / alignment still
                                // work even when the held type has no
                                // std::formatter specialisation.
                                std::string_view sv(defaultStr.cstr(), defaultStr.byteCount());
                                return String(std::vformat(fmtStr, std::make_format_args(sv)));
                        }
                }
        } catch (const std::format_error &) {
                if (err != nullptr) *err = Error::Invalid;
                return defaultStr;
        }
}

Enum Variant::asEnum(Enum::Type enumType, Error *err) const {
        if (!enumType.isValid()) {
                if (err != nullptr) *err = Error::InvalidArgument;
                return Enum();
        }
        auto setOk  = [err]() { if (err != nullptr) *err = Error::Ok; };
        auto setErr = [err]() { if (err != nullptr) *err = Error::Invalid; };

        switch (type()) {
                case DataTypeInvalid: setOk(); return Enum(enumType);
                case DataTypeEnum: {
                        Enum e = get<Enum>();
                        if (e.type() != enumType) {
                                setErr();
                                return Enum();
                        }
                        setOk();
                        return e;
                }
                case DataTypeString: {
                        String s = get<String>();
                        if (s.contains("::")) {
                                Error parseErr;
                                Enum  e = Enum::lookup(s, &parseErr);
                                if (parseErr.isOk() && e.type() == enumType) {
                                        setOk();
                                        return e;
                                }
                                setErr();
                                return Enum();
                        }
                        Enum byName(enumType, s);
                        if (byName.hasListedValue()) {
                                setOk();
                                return byName;
                        }
                        Error intErr;
                        int   iv = s.template to<int>(&intErr);
                        if (intErr.isOk()) {
                                setOk();
                                return Enum(enumType, iv);
                        }
                        setErr();
                        return Enum();
                }
                case DataTypeBool:
                case DataTypeUInt8:
                case DataTypeInt8:
                case DataTypeUInt16:
                case DataTypeInt16:
                case DataTypeUInt32:
                case DataTypeInt32:
                case DataTypeUInt64:
                case DataTypeInt64: {
                        Error ge;
                        int   iv = get<int32_t>(&ge);
                        if (ge.isError()) {
                                setErr();
                                return Enum();
                        }
                        setOk();
                        return Enum(enumType, iv);
                }
                default: break;
        }
        setErr();
        return Enum();
}

// ----------------------------------------------------------------------------
// Variant::operator== — three-tier comparison (same type / numeric / convertible).
// ----------------------------------------------------------------------------

namespace {

bool isNumericType(DataTypeID t) {
        switch (t) {
                case DataTypeBool:
                case DataTypeUInt8:
                case DataTypeInt8:
                case DataTypeUInt16:
                case DataTypeInt16:
                case DataTypeUInt32:
                case DataTypeInt32:
                case DataTypeUInt64:
                case DataTypeInt64:
                case DataTypeFloat:
                case DataTypeDouble: return true;
                default:                  return false;
        }
}

bool isFloatType(DataTypeID t) {
        return t == DataTypeFloat || t == DataTypeDouble;
}

bool isSignedIntType(DataTypeID t) {
        switch (t) {
                case DataTypeInt8:
                case DataTypeInt16:
                case DataTypeInt32:
                case DataTypeInt64: return true;
                default:               return false;
        }
}

double extractAsDouble(const Variant &v) {
        switch (v.type()) {
                case DataTypeBool:   return *v.peek<bool>() ? 1.0 : 0.0;
                case DataTypeUInt8:     return static_cast<double>(*v.peek<uint8_t>());
                case DataTypeInt8:     return static_cast<double>(*v.peek<int8_t>());
                case DataTypeUInt16:    return static_cast<double>(*v.peek<uint16_t>());
                case DataTypeInt16:    return static_cast<double>(*v.peek<int16_t>());
                case DataTypeUInt32:    return static_cast<double>(*v.peek<uint32_t>());
                case DataTypeInt32:    return static_cast<double>(*v.peek<int32_t>());
                case DataTypeUInt64:    return static_cast<double>(*v.peek<uint64_t>());
                case DataTypeInt64:    return static_cast<double>(*v.peek<int64_t>());
                case DataTypeFloat:  return static_cast<double>(*v.peek<float>());
                case DataTypeDouble: return *v.peek<double>();
                default:                  return 0.0;
        }
}

int64_t extractAsInt64(const Variant &v) {
        switch (v.type()) {
                case DataTypeBool:   return *v.peek<bool>() ? 1 : 0;
                case DataTypeUInt8:     return *v.peek<uint8_t>();
                case DataTypeInt8:     return *v.peek<int8_t>();
                case DataTypeUInt16:    return *v.peek<uint16_t>();
                case DataTypeInt16:    return *v.peek<int16_t>();
                case DataTypeUInt32:    return *v.peek<uint32_t>();
                case DataTypeInt32:    return *v.peek<int32_t>();
                case DataTypeUInt64:    return static_cast<int64_t>(*v.peek<uint64_t>());
                case DataTypeInt64:    return *v.peek<int64_t>();
                case DataTypeFloat:  return static_cast<int64_t>(*v.peek<float>());
                case DataTypeDouble: return static_cast<int64_t>(*v.peek<double>());
                default:                  return 0;
        }
}

uint64_t extractAsUInt64(const Variant &v) {
        switch (v.type()) {
                case DataTypeBool:   return *v.peek<bool>() ? 1u : 0u;
                case DataTypeUInt8:     return *v.peek<uint8_t>();
                case DataTypeInt8:     return static_cast<uint64_t>(*v.peek<int8_t>());
                case DataTypeUInt16:    return *v.peek<uint16_t>();
                case DataTypeInt16:    return static_cast<uint64_t>(*v.peek<int16_t>());
                case DataTypeUInt32:    return *v.peek<uint32_t>();
                case DataTypeInt32:    return static_cast<uint64_t>(*v.peek<int32_t>());
                case DataTypeUInt64:    return *v.peek<uint64_t>();
                case DataTypeInt64:    return static_cast<uint64_t>(*v.peek<int64_t>());
                case DataTypeFloat:  return static_cast<uint64_t>(*v.peek<float>());
                case DataTypeDouble: return static_cast<uint64_t>(*v.peek<double>());
                default:                  return 0;
        }
}

bool isNegative(const Variant &v) {
        switch (v.type()) {
                case DataTypeInt8:     return *v.peek<int8_t>()  < 0;
                case DataTypeInt16:    return *v.peek<int16_t>() < 0;
                case DataTypeInt32:    return *v.peek<int32_t>() < 0;
                case DataTypeInt64:    return *v.peek<int64_t>() < 0;
                case DataTypeFloat:  return *v.peek<float>()   < 0.0f;
                case DataTypeDouble: return *v.peek<double>()  < 0.0;
                default:                  return false;
        }
}

} // namespace

bool Variant::operator==(const Variant &other) const {
        const bool lhsValid = isValid();
        const bool rhsValid = other.isValid();
        if (!lhsValid && !rhsValid) return true;
        if (lhsValid != rhsValid) return false;

        DataTypeID a = type();
        DataTypeID b = other.type();
        if (a == b) {
                const DataType::Data *td = _box.isNull() ? nullptr : _box->typeData;
                if (td == nullptr || td->ops.equal == nullptr) return false;
                return td->ops.equal(_box->payload(), other._box->payload());
        }

        // Cross-type numeric promotion.
        if (isNumericType(a) && isNumericType(b)) {
                if (isFloatType(a) || isFloatType(b)) {
                        return extractAsDouble(*this) == extractAsDouble(other);
                }
                const bool aSigned = isSignedIntType(a);
                const bool bSigned = isSignedIntType(b);
                if (aSigned && !bSigned) {
                        if (isNegative(*this)) return false;
                        return extractAsUInt64(*this) == extractAsUInt64(other);
                }
                if (!aSigned && bSigned) {
                        if (isNegative(other)) return false;
                        return extractAsUInt64(*this) == extractAsUInt64(other);
                }
                return extractAsInt64(*this) == extractAsInt64(other);
        }

        // Cross-type convertible: try in both directions.
        Error err;
        Variant a2b = convertTo(b, &err);
        if (err.isOk() && a2b.isValid() && a2b == other) return true;
        Variant b2a = other.convertTo(a, &err);
        if (err.isOk() && b2a.isValid() && b2a == *this) return true;
        return false;
}

// ============================================================================
// Variant::fromJson
// ============================================================================

namespace {

Variant jsonToVariant(const nlohmann::json &val);   // fwd decl

Variant jsonToVariant(const nlohmann::json &val) {
        if (val.is_null()) return Variant();
        if (val.is_boolean()) return Variant(val.get<bool>());
        if (val.is_number_integer()) {
                if (val.is_number_unsigned()) return Variant(val.get<uint64_t>());
                return Variant(val.get<int64_t>());
        }
        if (val.is_number_float()) return Variant(val.get<double>());
        if (val.is_string()) return Variant(String(val.get<std::string>()));
        if (val.is_array()) {
                VariantList list;
                list.reserve(val.size());
                for (const auto &item : val) list.pushToBack(jsonToVariant(item));
                return Variant(list);
        }
        if (val.is_object()) {
                VariantMap map;
                for (auto it = val.begin(); it != val.end(); ++it) {
                        map.insert(String(it.key()), jsonToVariant(it.value()));
                }
                return Variant(map);
        }
        return Variant(String(val.dump()));
}

} // namespace

Variant Variant::fromJson(const nlohmann::json &val) {
        return jsonToVariant(val);
}

// ============================================================================
// convertOne<From,To> — the lifted (From, To) compile-time conversion matrix
// ============================================================================

namespace {

// Type-registry detector (parallels detail::is_type_registry_v in the
// legacy variant.h).
template <typename T> struct is_typereg : std::false_type {};
template <> struct is_typereg<ColorModel>     : std::true_type {};
template <> struct is_typereg<MemSpace>       : std::true_type {};
#if PROMEKI_ENABLE_PROAV
template <> struct is_typereg<AncFormat>      : std::true_type {};
template <> struct is_typereg<AudioCodec>     : std::true_type {};
template <> struct is_typereg<AudioFormat>    : std::true_type {};
template <> struct is_typereg<PixelMemLayout> : std::true_type {};
template <> struct is_typereg<PixelFormat>    : std::true_type {};
template <> struct is_typereg<VideoCodec>     : std::true_type {};
#endif
template <typename T> inline constexpr bool is_typereg_v = is_typereg<T>::value;

// Helper to extract the `value()` from a Result<T> in the codebase's
// makeResult / makeError shape.
template <typename R> auto resultValue(R &&r) -> decltype(r.first()) { return r.first(); }
template <typename R> auto resultError(R &&r) -> decltype(r.second()) { return r.second(); }

template <typename From, typename To>
To convertOne(const From &val, Error *err) {
        if (err != nullptr) *err = Error::Ok;
        if constexpr (std::is_same_v<From, To>) {
                return val;

        } else if constexpr (std::is_same_v<To, bool>) {
                if constexpr (std::is_integral_v<From> || std::is_floating_point_v<From>) return val ? true : false;
                if constexpr (std::is_same_v<From, String>) return val.template to<To>(err);

        } else if constexpr (std::is_integral_v<To>) {
                // T -> Integer for TypeRegistry / Enum / FrameNumber /
                // FrameCount is auto-wired by
                // Detail::registerAutoConverters via the type's
                // ops.toInt slot — no per-type branches needed here.
                if constexpr (std::is_same_v<From, bool>) return !!val;
                if constexpr (std::is_integral_v<From> || std::is_floating_point_v<From>)
                        return promekiConvert<To>(val, err);
                if constexpr (std::is_same_v<From, String>) return val.template to<To>(err);

        } else if constexpr (std::is_same_v<To, float>) {
                // T -> float for Rational / FrameRate / FrameNumber /
                // FrameCount is auto-wired via ops.toFloat — bespoke
                // branches removed.
                if constexpr (std::is_same_v<From, bool>) return !!val;
                if constexpr (std::is_integral_v<From> || std::is_floating_point_v<From>)
                        return promekiConvert<To>(val, err);
                if constexpr (std::is_same_v<From, String>) return val.template to<To>(err);

        } else if constexpr (std::is_same_v<To, double>) {
                if constexpr (std::is_same_v<From, bool>) return !!val;
                if constexpr (std::is_integral_v<From> || std::is_floating_point_v<From>)
                        return promekiConvert<To>(val, err);
                if constexpr (std::is_same_v<From, String>) return val.template to<To>(err);

        } else if constexpr (std::is_same_v<To, FrameRate>) {
                if constexpr (std::is_same_v<From, Rational<int>>) {
                        return FrameRate(FrameRate::RationalType(static_cast<unsigned int>(val.numerator()),
                                                                 static_cast<unsigned int>(val.denominator())));
                }

        } else if constexpr (std::is_same_v<To, StringList>) {
                // String -> StringList is auto-discovered via
                // StringList::fromString (split-by-comma).  Only the
                // VariantList -> StringList path is bespoke.
                if constexpr (std::is_same_v<From, VariantList>) {
                        StringList sl;
                        for (const auto &v : val) sl.pushToBack(v.template get<String>());
                        return sl;
                }

        } else if constexpr (std::is_same_v<To, VariantList>) {
                if constexpr (std::is_same_v<From, StringList>) {
                        VariantList vl;
                        vl.reserve(val.size());
                        for (size_t i = 0; i < val.size(); ++i) vl.pushToBack(Variant(val[i]));
                        return vl;
                }

        // Integer -> TypeRegistry / FrameNumber / FrameCount conversions
        // are auto-wired by Detail::registerAutoConverters via the
        // type's ops.fromInt slot — no per-type branches needed.

        } else if constexpr (std::is_same_v<To, String>) {
                // Numerics use String::number — there's no member
                // toString on the primitive types so the
                // auto-discovered converter pair can't pick them up.
                if constexpr (std::is_same_v<From, bool>) return String::number(val);
                if constexpr (std::is_integral_v<From>)        return String::number(val);
                if constexpr (std::is_floating_point_v<From>)  return String::number(val);
                // Every other Type -> String pair is wired up
                // implicitly by Detail::registerAutoConverters via the
                // type's ops.toString slot.  No bespoke per-type
                // branches needed any more.

        }
        if (err != nullptr) *err = Error::Invalid;
        return To{};
}

// ----------------------------------------------------------------------------
// Registration shims.
//
// Each registered (From, To) pair represents an *actually supported*
// conversion path through @ref convertOne above.  The legacy code
// registered every Cartesian (From, To) pair in the builtin type
// list — ~2500 entries, of which only ~250 corresponded to real
// conversion paths.  The unsupported entries just returned
// @c To{} with @c err=Invalid, which is exactly what
// @c Variant::convertTo already does when @c findConverter returns
// @c nullptr.  Registering only the real paths shrinks the binary
// by an order of magnitude and makes @c findConverter == @c nullptr
// the canonical "no path" sentinel rather than "a converter that
// always fails."
// ----------------------------------------------------------------------------

template <typename From, typename To>
Variant makeConvertedVariant(const Variant &src, Error *err) {
        const From *p = src.peek<From>();
        if (p == nullptr) {
                if (err != nullptr) *err = Error::Invalid;
                return Variant();
        }
        return Variant(convertOne<From, To>(*p, err));
}

template <typename From, typename To>
void registerOnePair() {
        // Bypass DataType::of<T> here — we're called from inside
        // Variant::registerBuiltinConverters, which itself is called
        // from registerBuiltinDataTypes after every builtin type record
        // is already in the registry.  Going through DataType::of would
        // re-enter registerBuiltinDataTypes via its safety net and
        // deadlock the C++ magic-static guarding it.
        Variant::registerConverter(DataType::byCppType(std::type_index(typeid(From))).id(),
                                   DataType::byCppType(std::type_index(typeid(To))).id(),
                                   &makeConvertedVariant<From, To>);
        return;
}

// (From, ToGroup...) — register From → each ToGroup type.
template <typename From, typename... Tos>
void registerFromOneToGroup(std::tuple<Tos...> *) {
        (registerOnePair<From, Tos>(), ...);
}

// (FromGroup..., To) — register each FromGroup type → To.
template <typename To, typename... Froms>
void registerToOneFromGroup(std::tuple<Froms...> *) {
        (registerOnePair<Froms, To>(), ...);
}

// (FromGroup..., ToGroup...) — register every (F, T) pair.
template <typename... Froms, typename... Tos>
void registerGroupCross(std::tuple<Froms...> *fg, std::tuple<Tos...> *tg) {
        (void)fg;
        (registerFromOneToGroup<Froms>(tg), ...);
}

// ----------------------------------------------------------------------------
// Built-in converter registration — only the (From, To) pairs that
// @ref convertOne actually has a non-fallthrough branch for.
//
// Pairs are organised into the same conceptual groups used by
// @ref convertOne so the listing mirrors the implementation.  When a
// new conversion path is added to @ref convertOne, add the pair below
// (or extend the relevant group); when one is removed, drop the
// corresponding registration.  No more N×N entries that always set
// @c err=Invalid — @c Variant::findConverter == nullptr now means
// "no path."
// ----------------------------------------------------------------------------

// Integer numeric Variant alternatives — these are the @c From types
// that satisfy @c std::is_integral_v.  Used wherever @ref convertOne
// gates a path on integral input (typereg / Enum / FrameNumber /
// FrameCount construction).
using IntegerGroup = std::tuple<bool, uint8_t, int8_t, uint16_t, int16_t,
                                uint32_t, int32_t, uint64_t, int64_t>;

// Full numeric group — integer + float.  Used for the
// numeric ↔ numeric Cartesian, the numeric ↔ String pairings, and the
// numeric source of FrameNumber/FrameCount → numeric.
using NumericGroup = std::tuple<bool, uint8_t, int8_t, uint16_t, int16_t,
                                uint32_t, int32_t, uint64_t, int64_t, float, double>;

void registerAllBuiltinConverters() {
        // Numeric ↔ Numeric — full Cartesian product within the group
        // (every numeric promotes / demotes / truncates to every other
        // numeric via promekiConvert).
        NumericGroup *nums = nullptr;
        registerGroupCross(nums, nums);

        // Numeric → String and String → Numeric.  Primitives have no
        // toString / fromString member shape that
        // @c Detail::registerAutoConverters can match, so the bespoke
        // numeric branches in @ref convertOne wire these explicitly.
        registerToOneFromGroup<String>(nums);
        registerFromOneToGroup<String>(nums);

        // (T <-> String) and (T <-> Integer) for every registered type
        // that populates ops.toString / ops.fromString / ops.toInt /
        // ops.fromInt is now wired implicitly by
        // Detail::registerAutoConverters.  Nothing to register here
        // for the TypeRegistry, FrameNumber, FrameCount or Enum
        // groups any more.

        // Rational ↔ FrameRate is a bespoke cross-type wrap that
        // doesn't go through any of the ops slots.  The numeric
        // (T, float) / (T, double) pairs for Rational / FrameRate /
        // FrameNumber / FrameCount are auto-wired via ops.toFloat /
        // ops.fromFloat — IEEE NaN / Inf carry the sentinel state.
        registerOnePair<Rational<int>, FrameRate>();

        // StringList ↔ VariantList — cross-container coercions that
        // don't go through the (String, T) round-trip path.  String ->
        // StringList is auto-discovered now that StringList has a
        // Result<StringList> fromString.
        registerOnePair<StringList, VariantList>();
        registerOnePair<VariantList, StringList>();

        // Identity (T → T) is handled by the same-type fast path in
        // @ref Variant::get, so no registration is needed.
        return;
}

} // anonymous namespace

void Variant::registerBuiltinConverters() {
        // Idempotent: function-local-static initialization runs the
        // body exactly once across all threads (C++11 magic-statics).
        // Subsequent calls hit the cached @c bool with no further work.
        static const bool once = []() {
                registerAllBuiltinConverters();
                return true;
        }();
        (void)once;
        return;
}

// ============================================================================
// VariantList
//
// Storage is a UniquePtr<ItemList> handle held directly — the
// underlying List<Variant> is lazily allocated on first mutation so
// the empty-list case stays at one machine word.  An anonymous
// helper returns a stable empty reference for the const list()
// accessor.
// ============================================================================

namespace {

const VariantList::ItemList &emptyVariantListStorage() {
        static const VariantList::ItemList kEmpty;
        return kEmpty;
}

VariantList::ItemList &ensureItemList(UniquePtr<VariantList::ItemList> &p) {
        if (p.isNull()) p = UniquePtr<VariantList::ItemList>::create();
        return *p;
}

} // namespace

VariantList::VariantList() = default;

VariantList::VariantList(std::initializer_list<Variant> il) {
        if (il.size() == 0) return;
        _list = UniquePtr<ItemList>::create();
        _list->reserve(il.size());
        for (const auto &v : il) _list->pushToBack(v);
}

VariantList::VariantList(const ItemList &other) {
        if (!other.isEmpty()) _list = UniquePtr<ItemList>::create(other);
}
VariantList::VariantList(ItemList &&other) {
        if (!other.isEmpty()) _list = UniquePtr<ItemList>::create(std::move(other));
}

VariantList::VariantList(const VariantList &other) {
        if (other._list.isValid() && !other._list->isEmpty()) {
                _list = UniquePtr<ItemList>::create(*other._list);
        }
}
VariantList::VariantList(VariantList &&other) noexcept : _list(std::move(other._list)) {}

VariantList::~VariantList() = default;

VariantList &VariantList::operator=(const VariantList &other) {
        if (&other == this) return *this;
        if (other._list.isNull() || other._list->isEmpty()) {
                _list.clear();
                return *this;
        }
        if (_list.isNull()) _list = UniquePtr<ItemList>::create();
        *_list = *other._list;
        return *this;
}

VariantList &VariantList::operator=(VariantList &&other) noexcept {
        _list = std::move(other._list);
        return *this;
}

size_t VariantList::size() const    { return _list.isNull() ? 0 : _list->size(); }
bool   VariantList::isEmpty() const { return _list.isNull() || _list->isEmpty(); }
void   VariantList::clear()         { if (_list.isValid()) _list->clear(); }
void   VariantList::reserve(size_t cap) {
        if (cap == 0) return;
        ensureItemList(_list).reserve(cap);
}

Variant       &VariantList::operator[](size_t i)       { return ensureItemList(_list)[i]; }
const Variant &VariantList::operator[](size_t i) const { return (*_list)[i]; }
Variant       &VariantList::at(size_t i)               { return ensureItemList(_list).at(i); }
const Variant &VariantList::at(size_t i) const {
        if (_list.isNull()) {
                throw std::logic_error("VariantList::at index out of range");
        }
        return _list->at(i);
}

void VariantList::pushToBack(const Variant &v) { ensureItemList(_list).pushToBack(v); }
void VariantList::pushToBack(Variant &&v)      { ensureItemList(_list).pushToBack(std::move(v)); }
void VariantList::popBack() {
        if (_list.isValid() && !_list->isEmpty()) _list->popFromBack();
}

Variant       *VariantList::data()       { return _list.isNull() ? nullptr : _list->data(); }
const Variant *VariantList::data() const { return _list.isNull() ? nullptr : _list->data(); }

VariantList::Iterator      VariantList::begin()        { return data(); }
VariantList::Iterator      VariantList::end()          { return data() + size(); }
VariantList::ConstIterator VariantList::begin() const  { return data(); }
VariantList::ConstIterator VariantList::end() const    { return data() + size(); }
VariantList::ConstIterator VariantList::cbegin() const { return data(); }
VariantList::ConstIterator VariantList::cend() const   { return data() + size(); }

VariantList::ItemList       &VariantList::list()       { return ensureItemList(_list); }
const VariantList::ItemList &VariantList::list() const {
        return _list.isNull() ? emptyVariantListStorage() : *_list;
}

bool VariantList::operator==(const VariantList &other) const {
        const bool lhsEmpty = _list.isNull() || _list->isEmpty();
        const bool rhsEmpty = other._list.isNull() || other._list->isEmpty();
        if (lhsEmpty && rhsEmpty) return true;
        if (lhsEmpty != rhsEmpty) return false;
        return *_list == *other._list;
}

namespace {

nlohmann::json variantToJson(const Variant &v);

nlohmann::json variantToJson(const Variant &v) {
        switch (v.type()) {
                case DataTypeInvalid: return nullptr;
                case DataTypeBool:    return v.get<bool>();
                case DataTypeUInt8:      return v.get<uint8_t>();
                case DataTypeInt8:      return v.get<int8_t>();
                case DataTypeUInt16:     return v.get<uint16_t>();
                case DataTypeInt16:     return v.get<int16_t>();
                case DataTypeUInt32:     return v.get<uint32_t>();
                case DataTypeInt32:     return v.get<int32_t>();
                case DataTypeUInt64:     return v.get<uint64_t>();
                case DataTypeInt64:     return v.get<int64_t>();
                case DataTypeFloat:   return v.get<float>();
                case DataTypeDouble:  return v.get<double>();
                case DataTypeVariantList: {
                        nlohmann::json arr = nlohmann::json::array();
                        const VariantList vl = v.get<VariantList>();
                        for (size_t i = 0; i < vl.size(); ++i) arr.push_back(variantToJson(vl[i]));
                        return arr;
                }
                case DataTypeVariantMap: {
                        nlohmann::json    obj = nlohmann::json::object();
                        const VariantMap  vm  = v.get<VariantMap>();
                        vm.forEach([&obj](const String &k, const Variant &val) {
                                obj[k.str()] = variantToJson(val);
                        });
                        return obj;
                }
                default: return v.get<String>().str();
        }
}

} // namespace

String VariantList::toJsonString() const {
        nlohmann::json arr = nlohmann::json::array();
        const size_t   n   = size();
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
// VariantMap
//
// Same shape as VariantList: a UniquePtr<EntryMap> held directly,
// allocated lazily on first mutation.  emptyVariantMapStorage() backs
// the const map() accessor when the handle is null.
// ============================================================================

namespace {

const VariantMap::EntryMap &emptyVariantMapStorage() {
        static const VariantMap::EntryMap kEmpty;
        return kEmpty;
}

VariantMap::EntryMap &ensureEntryMap(UniquePtr<VariantMap::EntryMap> &p) {
        if (p.isNull()) p = UniquePtr<VariantMap::EntryMap>::create();
        return *p;
}

} // namespace

VariantMap::VariantMap() = default;

VariantMap::VariantMap(std::initializer_list<EntryPair> il) {
        if (il.size() == 0) return;
        _map = UniquePtr<EntryMap>::create();
        for (const auto &kv : il) _map->insert(kv.first(), kv.second());
}

VariantMap::VariantMap(const EntryMap &other) {
        if (!other.isEmpty()) _map = UniquePtr<EntryMap>::create(other);
}
VariantMap::VariantMap(EntryMap &&other) {
        if (!other.isEmpty()) _map = UniquePtr<EntryMap>::create(std::move(other));
}

VariantMap::VariantMap(const VariantMap &other) {
        if (other._map.isValid() && !other._map->isEmpty()) {
                _map = UniquePtr<EntryMap>::create(*other._map);
        }
}
VariantMap::VariantMap(VariantMap &&other) noexcept : _map(std::move(other._map)) {}

VariantMap::~VariantMap() = default;

VariantMap &VariantMap::operator=(const VariantMap &other) {
        if (&other == this) return *this;
        if (other._map.isNull() || other._map->isEmpty()) {
                _map.clear();
                return *this;
        }
        if (_map.isNull()) _map = UniquePtr<EntryMap>::create();
        *_map = *other._map;
        return *this;
}

VariantMap &VariantMap::operator=(VariantMap &&other) noexcept {
        _map = std::move(other._map);
        return *this;
}

size_t VariantMap::size() const                      { return _map.isNull() ? 0 : _map->size(); }
bool   VariantMap::isEmpty() const                   { return _map.isNull() || _map->isEmpty(); }
void   VariantMap::clear()                           { if (_map.isValid()) _map->clear(); }
bool   VariantMap::contains(const String &key) const { return _map.isValid() && _map->contains(key); }

void VariantMap::insert(const String &key, const Variant &value) { ensureEntryMap(_map).insert(key, value); }
void VariantMap::insert(const String &key, Variant &&value) {
        ensureEntryMap(_map).insert(key, std::move(value));
}

bool VariantMap::remove(const String &key) {
        if (_map.isNull()) return false;
        return _map->remove(key);
}

Variant VariantMap::value(const String &key) const {
        if (_map.isNull()) return Variant();
        auto it = _map->find(key);
        if (it == _map->end()) return Variant();
        return it->second;
}

Variant VariantMap::value(const String &key, const Variant &defaultValue) const {
        if (_map.isNull()) return defaultValue;
        auto it = _map->find(key);
        if (it == _map->end()) return defaultValue;
        return it->second;
}

Variant *VariantMap::find(const String &key) {
        if (_map.isNull()) return nullptr;
        auto it = _map->find(key);
        if (it == _map->end()) return nullptr;
        return &it->second;
}

const Variant *VariantMap::find(const String &key) const {
        if (_map.isNull()) return nullptr;
        auto it = _map->find(key);
        if (it == _map->end()) return nullptr;
        return &it->second;
}

StringList VariantMap::keys() const {
        StringList out;
        if (_map.isNull()) return out;
        for (auto it = _map->cbegin(); it != _map->cend(); ++it) {
                out.pushToBack(it->first);
        }
        return out;
}

void VariantMap::forEach(ForEachFn fn) const {
        if (_map.isNull()) return;
        for (auto it = _map->cbegin(); it != _map->cend(); ++it) {
                fn(it->first, it->second);
        }
}

VariantMap::EntryMap       &VariantMap::map()       { return ensureEntryMap(_map); }
const VariantMap::EntryMap &VariantMap::map() const {
        return _map.isNull() ? emptyVariantMapStorage() : *_map;
}

bool VariantMap::operator==(const VariantMap &other) const {
        const bool lhsEmpty = _map.isNull() || _map->isEmpty();
        const bool rhsEmpty = other._map.isNull() || other._map->isEmpty();
        if (lhsEmpty && rhsEmpty) return true;
        if (lhsEmpty != rhsEmpty) return false;
        return *_map == *other._map;
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
// DataStream serialization for VariantList / VariantMap
// ============================================================================

Error VariantList::writeToStream(DataStream &s) const {
        s << static_cast<uint32_t>(size());
        for (size_t i = 0; i < size(); ++i) s << (*this)[i];
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<VariantList> VariantList::readFromStream<1>(DataStream &s) {
        uint32_t count = 0;
        s >> count;
        if (s.status() != DataStream::Ok) return makeError<VariantList>(s.toError());
        VariantList list;
        list.reserve(count);
        for (uint32_t i = 0; i < count && s.status() == DataStream::Ok; ++i) {
                Variant v;
                s >> v;
                if (s.status() != DataStream::Ok) return makeError<VariantList>(s.toError());
                list.pushToBack(std::move(v));
        }
        return makeResult(std::move(list));
}

Error VariantMap::writeToStream(DataStream &s) const {
        s << static_cast<uint32_t>(size());
        forEach([&s](const String &k, const Variant &v) { s << k << v; });
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<VariantMap> VariantMap::readFromStream<1>(DataStream &s) {
        uint32_t count = 0;
        s >> count;
        if (s.status() != DataStream::Ok) return makeError<VariantMap>(s.toError());
        VariantMap map;
        for (uint32_t i = 0; i < count && s.status() == DataStream::Ok; ++i) {
                String  key;
                Variant value;
                s >> key >> value;
                if (s.status() != DataStream::Ok) return makeError<VariantMap>(s.toError());
                map.insert(std::move(key), std::move(value));
        }
        return makeResult(std::move(map));
}

// ============================================================================
// promekiResolveVariantPath
// ============================================================================

namespace {

struct PathSegment {
                String name;
                String rest;
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

} // namespace

Variant promekiResolveVariantPath(const Variant &root, const String &path, Error *err) {
        if (err != nullptr) *err = Error::Ok;
        if (path.isEmpty()) return root;

        PathSegment seg;
        if (!parsePathSegment(path, seg, err)) return Variant();

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

PROMEKI_NAMESPACE_END
