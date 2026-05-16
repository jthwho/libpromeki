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
 * Three classes of state live here:
 *
 *  - @ref VariantBox memory management — the trailing-payload allocator,
 *    the hand-rolled @c SharedPtr contract (RefCount + custom
 *    @c _promeki_clone), and the @c Detail:: thunks the inline header
 *    template definitions in @c variant.h delegate to.
 *
 *  - Conversion: @c convertOne<From,To> hosts the per-(From,To) compile-time
 *    branching lifted from the legacy @c variant.tpp.  A small static
 *    converter registry maps @c (FromId,ToId) pairs to lambdas that
 *    invoke the right @c convertOne instantiation; @c Variant::get<T>
 *    consults it via @c Detail::convertViaRegistry.
 *
 *  - Builtin registration: at first use, this TU registers every legacy
 *    Variant alternative as a @ref DataType (pinning its ID to the
 *    corresponding @c DataStream::TypeId value so the wire format is
 *    stable) and registers a converter for every (From,To) pair in
 *    that builtin set.  User-defined types use the public registration
 *    macros and never touch this file.
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
// All the registered Variant alternatives — variant.cpp needs them
// complete to register each type and to instantiate convertOne<From,To>
// for every (From, To) pair.
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
#include <promeki/cea608packet.h>
#include <promeki/cea708cdp.h>
#include <promeki/subtitle.h>
#include <promeki/audiochannelmap.h>
#include <promeki/audiocodec.h>
#include <promeki/audioformat.h>
#include <promeki/audiomarker.h>
#include <promeki/audiostreamdesc.h>
#include <promeki/colormodel.h>
#include <promeki/memspace.h>
#include <promeki/pixelmemlayout.h>
#include <promeki/pixelformat.h>
#include <promeki/videocodec.h>
#include <promeki/masteringdisplay.h>
#include <promeki/contentlightlevel.h>
#include <promeki/hdrstaticmetadata.h>
#include <promeki/hdrdynamic2094_40.h>
#include <promeki/cea708service.h>
#include <promeki/enumlist.h>
#include <promeki/url.h>
#include <promeki/windowedstat.h>
#include <promeki/xml.h>
#if PROMEKI_ENABLE_NETWORK
#include <promeki/socketaddress.h>
#include <promeki/sdpsession.h>
#include <promeki/macaddress.h>
#include <promeki/eui64.h>
#endif
#if PROMEKI_ENABLE_TLS
#include <promeki/sslcontext.h>
#endif
#include <nlohmann/json.hpp>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(Variant)

// ============================================================================
// Free-function DataStream operator allowlist
//
// Types whose wire operator is a free function (rather than a
// DataStream member) need to be flagged so @ref Detail::makeDefaultOps
// generates the writeStream / readStream slot for them.  The
// previous std::variant-backed Variant had the same registry baked
// into the compile-time coverage static_asserts; the entries below
// mirror that list one-for-one.
// ============================================================================

namespace Detail {

template <typename T> struct HasFreeDataStreamWrite<Size2DTemplate<T>> : std::true_type {};
template <typename T> struct HasFreeDataStreamRead<Size2DTemplate<T>>  : std::true_type {};
template <typename T> struct HasFreeDataStreamWrite<Rational<T>>       : std::true_type {};
template <typename T> struct HasFreeDataStreamRead<Rational<T>>        : std::true_type {};
template <> struct HasFreeDataStreamWrite<MasteringDisplay>            : std::true_type {};
template <> struct HasFreeDataStreamRead<MasteringDisplay>             : std::true_type {};
template <> struct HasFreeDataStreamWrite<ContentLightLevel>           : std::true_type {};
template <> struct HasFreeDataStreamRead<ContentLightLevel>            : std::true_type {};
template <> struct HasFreeDataStreamWrite<AudioStreamDesc>             : std::true_type {};
template <> struct HasFreeDataStreamRead<AudioStreamDesc>              : std::true_type {};
template <> struct HasFreeDataStreamWrite<AudioChannelMap>             : std::true_type {};
template <> struct HasFreeDataStreamRead<AudioChannelMap>              : std::true_type {};
template <> struct HasFreeDataStreamWrite<AudioMarkerList>             : std::true_type {};
template <> struct HasFreeDataStreamRead<AudioMarkerList>              : std::true_type {};
template <> struct HasFreeDataStreamWrite<WindowedStat>                : std::true_type {};
template <> struct HasFreeDataStreamRead<WindowedStat>                 : std::true_type {};
template <> struct HasFreeDataStreamWrite<VariantList>                 : std::true_type {};
template <> struct HasFreeDataStreamRead<VariantList>                  : std::true_type {};
template <> struct HasFreeDataStreamWrite<VariantMap>                  : std::true_type {};
template <> struct HasFreeDataStreamRead<VariantMap>                   : std::true_type {};
template <> struct HasFreeDataStreamWrite<XmlDocument>                 : std::true_type {};
template <> struct HasFreeDataStreamRead<XmlDocument>                  : std::true_type {};
template <> struct HasFreeDataStreamWrite<Cea708Cdp>                   : std::true_type {};
template <> struct HasFreeDataStreamRead<Cea708Cdp>                    : std::true_type {};
template <> struct HasFreeDataStreamWrite<Cea608Packet>                : std::true_type {};
template <> struct HasFreeDataStreamRead<Cea608Packet>                 : std::true_type {};
template <> struct HasFreeDataStreamWrite<Subtitle>                    : std::true_type {};
template <> struct HasFreeDataStreamRead<Subtitle>                     : std::true_type {};
template <> struct HasFreeDataStreamWrite<Cea708Service>               : std::true_type {};
template <> struct HasFreeDataStreamRead<Cea708Service>                : std::true_type {};
template <> struct HasFreeDataStreamWrite<Cea708DtvccPacket>           : std::true_type {};
template <> struct HasFreeDataStreamRead<Cea708DtvccPacket>            : std::true_type {};
template <> struct HasFreeDataStreamWrite<HdrStaticMetadata>           : std::true_type {};
template <> struct HasFreeDataStreamRead<HdrStaticMetadata>            : std::true_type {};
template <> struct HasFreeDataStreamWrite<HdrDynamic2094_40>           : std::true_type {};
template <> struct HasFreeDataStreamRead<HdrDynamic2094_40>            : std::true_type {};

} // namespace Detail

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

constexpr uint32_t converterKey(Variant::Type from, Variant::Type to) {
        return (static_cast<uint32_t>(static_cast<uint16_t>(from)) << 16) |
                static_cast<uint32_t>(static_cast<uint16_t>(to));
}

} // anonymous namespace

void Variant::registerConverter(Type from, Type to, ConverterFn fn) {
        auto         &r = converterRegistry();
        Mutex::Locker lock(r.mutex);
        r.map.insert(converterKey(from, to), fn);
        return;
}

Variant::ConverterFn Variant::findConverter(Type from, Type to) {
        auto         &r = converterRegistry();
        Mutex::Locker lock(r.mutex);
        auto          it = r.map.find(converterKey(from, to));
        return (it != r.map.end()) ? it->second : nullptr;
}

Variant Variant::convertTo(Type to, Error *err) const {
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

Variant::Type Variant::type() const {
        if (_box.isNull()) return TypeInvalid;
        const DataType::Data *td = _box->typeData;
        return td != nullptr ? td->id : TypeInvalid;
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

const char *Variant::typeName(Type id) {
        DataType dt(id);
        return dt.isValid() ? dt.name() : "Invalid";
}

Variant Variant::toStandardType() const {
        if (!isValid()) return *this;
        Type t = type();
        switch (t) {
                case TypeBool:
                case TypeU8:
                case TypeS8:
                case TypeU16:
                case TypeS16:
                case TypeU32:
                case TypeS32:
                case TypeU64:
                case TypeS64:
                case TypeFloat:
                case TypeDouble: return *this;
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
                        case TypeInvalid: return String();
                        case TypeBool:    return tryFormatAs<bool>(fmtStr, *peek<bool>(), defaultStr, handled);
                        case TypeU8:      return tryFormatAs<uint8_t>(fmtStr, *peek<uint8_t>(), defaultStr, handled);
                        case TypeS8:      return tryFormatAs<int8_t>(fmtStr, *peek<int8_t>(), defaultStr, handled);
                        case TypeU16:     return tryFormatAs<uint16_t>(fmtStr, *peek<uint16_t>(), defaultStr, handled);
                        case TypeS16:     return tryFormatAs<int16_t>(fmtStr, *peek<int16_t>(), defaultStr, handled);
                        case TypeU32:     return tryFormatAs<uint32_t>(fmtStr, *peek<uint32_t>(), defaultStr, handled);
                        case TypeS32:     return tryFormatAs<int32_t>(fmtStr, *peek<int32_t>(), defaultStr, handled);
                        case TypeU64:     return tryFormatAs<uint64_t>(fmtStr, *peek<uint64_t>(), defaultStr, handled);
                        case TypeS64:     return tryFormatAs<int64_t>(fmtStr, *peek<int64_t>(), defaultStr, handled);
                        case TypeFloat:   return tryFormatAs<float>(fmtStr, *peek<float>(), defaultStr, handled);
                        case TypeDouble:  return tryFormatAs<double>(fmtStr, *peek<double>(), defaultStr, handled);
                        case TypeTimecode:
                                return tryFormatAs<Timecode>(fmtStr, *peek<Timecode>(), defaultStr, handled);
                        case TypeVideoFormat:
                                return tryFormatAs<VideoFormat>(fmtStr, *peek<VideoFormat>(), defaultStr,
                                                                handled);
                        case TypeString:  return tryFormatAs<String>(fmtStr, *peek<String>(), defaultStr, handled);
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
                case TypeInvalid: setOk(); return Enum(enumType);
                case TypeEnum: {
                        Enum e = get<Enum>();
                        if (e.type() != enumType) {
                                setErr();
                                return Enum();
                        }
                        setOk();
                        return e;
                }
                case TypeString: {
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
                case TypeBool:
                case TypeU8:
                case TypeS8:
                case TypeU16:
                case TypeS16:
                case TypeU32:
                case TypeS32:
                case TypeU64:
                case TypeS64: {
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

bool isNumericType(Variant::Type t) {
        switch (t) {
                case Variant::TypeBool:
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
                default:                  return false;
        }
}

bool isFloatType(Variant::Type t) {
        return t == Variant::TypeFloat || t == Variant::TypeDouble;
}

bool isSignedIntType(Variant::Type t) {
        switch (t) {
                case Variant::TypeS8:
                case Variant::TypeS16:
                case Variant::TypeS32:
                case Variant::TypeS64: return true;
                default:               return false;
        }
}

double extractAsDouble(const Variant &v) {
        switch (v.type()) {
                case Variant::TypeBool:   return *v.peek<bool>() ? 1.0 : 0.0;
                case Variant::TypeU8:     return static_cast<double>(*v.peek<uint8_t>());
                case Variant::TypeS8:     return static_cast<double>(*v.peek<int8_t>());
                case Variant::TypeU16:    return static_cast<double>(*v.peek<uint16_t>());
                case Variant::TypeS16:    return static_cast<double>(*v.peek<int16_t>());
                case Variant::TypeU32:    return static_cast<double>(*v.peek<uint32_t>());
                case Variant::TypeS32:    return static_cast<double>(*v.peek<int32_t>());
                case Variant::TypeU64:    return static_cast<double>(*v.peek<uint64_t>());
                case Variant::TypeS64:    return static_cast<double>(*v.peek<int64_t>());
                case Variant::TypeFloat:  return static_cast<double>(*v.peek<float>());
                case Variant::TypeDouble: return *v.peek<double>();
                default:                  return 0.0;
        }
}

int64_t extractAsInt64(const Variant &v) {
        switch (v.type()) {
                case Variant::TypeBool:   return *v.peek<bool>() ? 1 : 0;
                case Variant::TypeU8:     return *v.peek<uint8_t>();
                case Variant::TypeS8:     return *v.peek<int8_t>();
                case Variant::TypeU16:    return *v.peek<uint16_t>();
                case Variant::TypeS16:    return *v.peek<int16_t>();
                case Variant::TypeU32:    return *v.peek<uint32_t>();
                case Variant::TypeS32:    return *v.peek<int32_t>();
                case Variant::TypeU64:    return static_cast<int64_t>(*v.peek<uint64_t>());
                case Variant::TypeS64:    return *v.peek<int64_t>();
                case Variant::TypeFloat:  return static_cast<int64_t>(*v.peek<float>());
                case Variant::TypeDouble: return static_cast<int64_t>(*v.peek<double>());
                default:                  return 0;
        }
}

uint64_t extractAsUInt64(const Variant &v) {
        switch (v.type()) {
                case Variant::TypeBool:   return *v.peek<bool>() ? 1u : 0u;
                case Variant::TypeU8:     return *v.peek<uint8_t>();
                case Variant::TypeS8:     return static_cast<uint64_t>(*v.peek<int8_t>());
                case Variant::TypeU16:    return *v.peek<uint16_t>();
                case Variant::TypeS16:    return static_cast<uint64_t>(*v.peek<int16_t>());
                case Variant::TypeU32:    return *v.peek<uint32_t>();
                case Variant::TypeS32:    return static_cast<uint64_t>(*v.peek<int32_t>());
                case Variant::TypeU64:    return *v.peek<uint64_t>();
                case Variant::TypeS64:    return static_cast<uint64_t>(*v.peek<int64_t>());
                case Variant::TypeFloat:  return static_cast<uint64_t>(*v.peek<float>());
                case Variant::TypeDouble: return static_cast<uint64_t>(*v.peek<double>());
                default:                  return 0;
        }
}

bool isNegative(const Variant &v) {
        switch (v.type()) {
                case Variant::TypeS8:     return *v.peek<int8_t>()  < 0;
                case Variant::TypeS16:    return *v.peek<int16_t>() < 0;
                case Variant::TypeS32:    return *v.peek<int32_t>() < 0;
                case Variant::TypeS64:    return *v.peek<int64_t>() < 0;
                case Variant::TypeFloat:  return *v.peek<float>()   < 0.0f;
                case Variant::TypeDouble: return *v.peek<double>()  < 0.0;
                default:                  return false;
        }
}

} // namespace

bool Variant::operator==(const Variant &other) const {
        const bool lhsValid = isValid();
        const bool rhsValid = other.isValid();
        if (!lhsValid && !rhsValid) return true;
        if (lhsValid != rhsValid) return false;

        Type a = type();
        Type b = other.type();
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
template <> struct is_typereg<AncFormat>      : std::true_type {};
template <> struct is_typereg<AudioCodec>     : std::true_type {};
template <> struct is_typereg<AudioFormat>    : std::true_type {};
template <> struct is_typereg<ColorModel>     : std::true_type {};
template <> struct is_typereg<MemSpace>       : std::true_type {};
template <> struct is_typereg<PixelMemLayout> : std::true_type {};
template <> struct is_typereg<PixelFormat>    : std::true_type {};
template <> struct is_typereg<VideoCodec>     : std::true_type {};
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
                if constexpr (std::is_same_v<From, bool>) return !!val;
                if constexpr (std::is_integral_v<From> || std::is_floating_point_v<From>)
                        return promekiConvert<To>(val, err);
                if constexpr (std::is_same_v<From, String>) return val.template to<To>(err);
                if constexpr (is_typereg_v<From>) return static_cast<To>(val.id());
                if constexpr (std::is_same_v<From, Enum>) return static_cast<To>(val.value());
                if constexpr (std::is_same_v<From, FrameNumber> || std::is_same_v<From, FrameCount>)
                        return static_cast<To>(val.value());

        } else if constexpr (std::is_same_v<To, float>) {
                if constexpr (std::is_same_v<From, bool>) return !!val;
                if constexpr (std::is_integral_v<From> || std::is_floating_point_v<From>)
                        return promekiConvert<To>(val, err);
                if constexpr (std::is_same_v<From, String>) return val.template to<To>(err);
                if constexpr (std::is_same_v<From, Rational<int>>) return static_cast<float>(val.toDouble());
                if constexpr (std::is_same_v<From, FrameRate>) return static_cast<float>(val.toDouble());
                if constexpr (std::is_same_v<From, FrameNumber>) {
                        return val.isValid() ? static_cast<float>(val.value())
                                             : std::numeric_limits<float>::quiet_NaN();
                }
                if constexpr (std::is_same_v<From, FrameCount>) {
                        if (val.isUnknown()) return std::numeric_limits<float>::quiet_NaN();
                        if (val.isInfinite()) return std::numeric_limits<float>::infinity();
                        return static_cast<float>(val.value());
                }

        } else if constexpr (std::is_same_v<To, double>) {
                if constexpr (std::is_same_v<From, bool>) return !!val;
                if constexpr (std::is_integral_v<From> || std::is_floating_point_v<From>)
                        return promekiConvert<To>(val, err);
                if constexpr (std::is_same_v<From, String>) return val.template to<To>(err);
                if constexpr (std::is_same_v<From, Rational<int>>) return val.toDouble();
                if constexpr (std::is_same_v<From, FrameRate>) return val.toDouble();
                if constexpr (std::is_same_v<From, FrameNumber>) {
                        return val.isValid() ? static_cast<double>(val.value())
                                             : std::numeric_limits<double>::quiet_NaN();
                }
                if constexpr (std::is_same_v<From, FrameCount>) {
                        if (val.isUnknown()) return std::numeric_limits<double>::quiet_NaN();
                        if (val.isInfinite()) return std::numeric_limits<double>::infinity();
                        return static_cast<double>(val.value());
                }

        } else if constexpr (std::is_same_v<To, FrameRate>) {
                if constexpr (std::is_same_v<From, Rational<int>>) {
                        return FrameRate(FrameRate::RationalType(static_cast<unsigned int>(val.numerator()),
                                                                 static_cast<unsigned int>(val.denominator())));
                }

        } else if constexpr (std::is_same_v<To, StringList>) {
                if constexpr (std::is_same_v<From, String>) return val.split(",");
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

        } else if constexpr (std::is_same_v<To, ColorModel>) {
                if constexpr (std::is_integral_v<From>) return ColorModel(static_cast<ColorModel::ID>(val));

        } else if constexpr (std::is_same_v<To, MemSpace>) {
                if constexpr (std::is_integral_v<From>) return MemSpace(static_cast<MemSpace::ID>(val));

        } else if constexpr (std::is_same_v<To, PixelMemLayout>) {
                if constexpr (std::is_integral_v<From>) return PixelMemLayout(static_cast<PixelMemLayout::ID>(val));

        } else if constexpr (std::is_same_v<To, PixelFormat>) {
                if constexpr (std::is_integral_v<From>) return PixelFormat(static_cast<PixelFormat::ID>(val));

        } else if constexpr (std::is_same_v<To, VideoCodec>) {
                if constexpr (std::is_integral_v<From>) return VideoCodec(static_cast<VideoCodec::ID>(val));

        } else if constexpr (std::is_same_v<To, AudioCodec>) {
                if constexpr (std::is_integral_v<From>) return AudioCodec(static_cast<AudioCodec::ID>(val));

        } else if constexpr (std::is_same_v<To, AudioFormat>) {
                if constexpr (std::is_integral_v<From>) return AudioFormat(static_cast<AudioFormat::ID>(val));

        } else if constexpr (std::is_same_v<To, AudioStreamDesc>) {
                if constexpr (std::is_integral_v<From>)
                        return AudioStreamDesc(static_cast<AudioStreamDesc::ID>(val));

        } else if constexpr (std::is_same_v<To, FrameNumber>) {
                if constexpr (std::is_integral_v<From>) return FrameNumber(static_cast<int64_t>(val));

        } else if constexpr (std::is_same_v<To, FrameCount>) {
                if constexpr (std::is_integral_v<From>) return FrameCount(static_cast<int64_t>(val));

        } else if constexpr (std::is_same_v<To, String>) {
                if constexpr (std::is_same_v<From, bool>) return String::number(val);
                if constexpr (std::is_same_v<From, int8_t>) return String::number(val);
                if constexpr (std::is_same_v<From, uint8_t>) return String::number(val);
                if constexpr (std::is_same_v<From, int16_t>) return String::number(val);
                if constexpr (std::is_same_v<From, uint16_t>) return String::number(val);
                if constexpr (std::is_same_v<From, int32_t>) return String::number(val);
                if constexpr (std::is_same_v<From, uint32_t>) return String::number(val);
                if constexpr (std::is_same_v<From, int64_t>) return String::number(val);
                if constexpr (std::is_same_v<From, uint64_t>) return String::number(val);
                if constexpr (std::is_same_v<From, float>) return String::number(val);
                if constexpr (std::is_same_v<From, double>) return String::number(val);
                if constexpr (std::is_same_v<From, DateTime>) return val.toString();
                if constexpr (std::is_same_v<From, TimeStamp>) return val.toString();
                if constexpr (std::is_same_v<From, MediaTimeStamp>) return val.toString();
                if constexpr (std::is_same_v<From, Size2Du32>) return val.toString();
                if constexpr (std::is_same_v<From, UUID>) return val.toString();
                if constexpr (std::is_same_v<From, UMID>) return val.toString();
                if constexpr (std::is_same_v<From, Timecode>) return val.toString().first();
                if constexpr (std::is_same_v<From, FrameNumber>) return val.toString();
                if constexpr (std::is_same_v<From, FrameCount>) return val.toString();
                if constexpr (std::is_same_v<From, MediaDuration>) return val.toString();
                if constexpr (std::is_same_v<From, Duration>) return val.toString();
                if constexpr (std::is_same_v<From, Rational<int>>) return val.toString();
                if constexpr (std::is_same_v<From, FrameRate>) return val.toString();
                if constexpr (std::is_same_v<From, VideoFormat>) return val.toString();
                if constexpr (std::is_same_v<From, StringList>) return val.join(",");
                if constexpr (std::is_same_v<From, VariantList>) return val.toJsonString();
                if constexpr (std::is_same_v<From, VariantMap>) return val.toJsonString();
                if constexpr (std::is_same_v<From, Color>) return val.toString();
                if constexpr (std::is_same_v<From, VideoCodec>) return val.toString();
                if constexpr (std::is_same_v<From, AudioCodec>) return val.toString();
                if constexpr (std::is_same_v<From, AudioStreamDesc>) return val.toString();
                if constexpr (std::is_same_v<From, AudioChannelMap>) return val.toString();
                if constexpr (is_typereg_v<From>) return val.name();
                if constexpr (std::is_same_v<From, Enum>) return val.toString();
                if constexpr (std::is_same_v<From, EnumList>) return val.toString();
                if constexpr (std::is_same_v<From, Url>) return val.toString();
                if constexpr (std::is_same_v<From, WindowedStat>) return val.toSerializedString();
#if PROMEKI_ENABLE_NETWORK
                if constexpr (std::is_same_v<From, SocketAddress>) return val.toString();
                if constexpr (std::is_same_v<From, SdpSession>) return val.toString();
                if constexpr (std::is_same_v<From, MacAddress>) return val.toString();
                if constexpr (std::is_same_v<From, EUI64>) return val.toString();
#endif

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

// Generic String → @p To converter routed through the registered
// @c DataType::Ops::fromString slot.  Replaces the per-type
// @c if-constexpr branches that previously called @c To::fromString
// inline inside @ref convertOne — every target with a
// @c Result<To>(const String &) shape (i.e. satisfies
// @ref Detail::HasResultFromString) is now reachable through this
// single function template instead.
template <typename To>
Variant convertViaOpsFromString(const Variant &src, Error *err) {
        const String *s = src.peek<String>();
        if (s == nullptr) {
                if (err != nullptr) *err = Error::Invalid;
                return Variant();
        }
        const DataType        dt = DataType::of<To>();
        const DataType::Data *td = dt.data();
        if (td == nullptr || td->ops.fromString == nullptr) {
                if (err != nullptr) *err = Error::Invalid;
                return Variant();
        }
        To    tmp{};
        Error e;
        if (!td->ops.fromString(*s, &tmp, &e)) {
                if (err != nullptr) *err = e.isError() ? e : Error::Invalid;
                return Variant();
        }
        if (err != nullptr) *err = Error::Ok;
        return Variant(std::move(tmp));
}

template <typename From, typename To>
void registerOnePair() {
        // Bypass DataType::of<T> here — we're called from inside
        // ensureBuiltinDataTypesRegistered() and recursing into the
        // lazy-init trigger would deadlock the C++ magic-static.
        // The builtins were just registered before this function
        // runs, so byCppType always finds them.
        Variant::registerConverter(DataType::byCppType(std::type_index(typeid(From))).id(),
                                   DataType::byCppType(std::type_index(typeid(To))).id(),
                                   &makeConvertedVariant<From, To>);
        return;
}

// Registers (String, @p To) using @ref convertViaOpsFromString instead
// of the @ref makeConvertedVariant / @ref convertOne path.  Used for
// every typed target whose @c Result<To>(String) shape is detected by
// @ref Detail::HasResultFromString and populated into
// @c ops.fromString.
template <typename To>
void registerStringToVia() {
        Variant::registerConverter(
                DataType::byCppType(std::type_index(typeid(String))).id(),
                DataType::byCppType(std::type_index(typeid(To))).id(),
                &convertViaOpsFromString<To>);
        return;
}

// Same shape as @ref registerToOneFromGroup but routes through
// @ref convertViaOpsFromString — one registration per target type in
// the group, all going through the registry's @c ops.fromString.
template <typename... Tos>
void registerStringToGroupVia(std::tuple<Tos...> *) {
        (registerStringToVia<Tos>(), ...);
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
// Builtin type + converter registration.
// ----------------------------------------------------------------------------

template <typename T>
void registerBuiltin(const char *name, Variant::Type id) {
        DataType::registerType<T>(name, id);
}

void registerBuiltinTypes() {
        registerBuiltin<bool>("bool", Variant::TypeBool);
        registerBuiltin<uint8_t>("uint8_t", Variant::TypeU8);
        registerBuiltin<int8_t>("int8_t", Variant::TypeS8);
        registerBuiltin<uint16_t>("uint16_t", Variant::TypeU16);
        registerBuiltin<int16_t>("int16_t", Variant::TypeS16);
        registerBuiltin<uint32_t>("uint32_t", Variant::TypeU32);
        registerBuiltin<int32_t>("int32_t", Variant::TypeS32);
        registerBuiltin<uint64_t>("uint64_t", Variant::TypeU64);
        registerBuiltin<int64_t>("int64_t", Variant::TypeS64);
        registerBuiltin<float>("float", Variant::TypeFloat);
        registerBuiltin<double>("double", Variant::TypeDouble);
        registerBuiltin<String>("String", Variant::TypeString);
        registerBuiltin<DateTime>("DateTime", Variant::TypeDateTime);
        registerBuiltin<TimeStamp>("TimeStamp", Variant::TypeTimeStamp);
        registerBuiltin<MediaTimeStamp>("MediaTimeStamp", Variant::TypeMediaTimeStamp);
        registerBuiltin<FrameNumber>("FrameNumber", Variant::TypeFrameNumber);
        registerBuiltin<FrameCount>("FrameCount", Variant::TypeFrameCount);
        registerBuiltin<MediaDuration>("MediaDuration", Variant::TypeMediaDuration);
        registerBuiltin<Duration>("Duration", Variant::TypeDuration);
        registerBuiltin<Size2Du32>("Size2Du32", Variant::TypeSize2D);
        registerBuiltin<UUID>("UUID", Variant::TypeUUID);
        registerBuiltin<UMID>("UMID", Variant::TypeUMID);
        registerBuiltin<Timecode>("Timecode", Variant::TypeTimecode);
        registerBuiltin<Rational<int>>("Rational<int>", Variant::TypeRational);
        registerBuiltin<FrameRate>("FrameRate", Variant::TypeFrameRate);
        registerBuiltin<VideoFormat>("VideoFormat", Variant::TypeVideoFormat);
        registerBuiltin<StringList>("StringList", Variant::TypeStringList);
        registerBuiltin<Color>("Color", Variant::TypeColor);
        registerBuiltin<ColorModel>("ColorModel", Variant::TypeColorModel);
        registerBuiltin<MemSpace>("MemSpace", Variant::TypeMemSpace);
        registerBuiltin<PixelMemLayout>("PixelMemLayout", Variant::TypePixelMemLayout);
        registerBuiltin<PixelFormat>("PixelFormat", Variant::TypePixelFormat);
        registerBuiltin<VideoCodec>("VideoCodec", Variant::TypeVideoCodec);
        registerBuiltin<AudioCodec>("AudioCodec", Variant::TypeAudioCodec);
        registerBuiltin<AudioFormat>("AudioFormat", Variant::TypeAudioFormat);
        registerBuiltin<AncFormat>("AncFormat", Variant::TypeAncFormat);
        registerBuiltin<AudioStreamDesc>("AudioStreamDesc", Variant::TypeAudioStreamDesc);
        registerBuiltin<AudioChannelMap>("AudioChannelMap", Variant::TypeAudioChannelMap);
        registerBuiltin<AudioMarkerList>("AudioMarkerList", Variant::TypeAudioMarkerList);
        registerBuiltin<Enum>("Enum", Variant::TypeEnum);
        registerBuiltin<EnumList>("EnumList", Variant::TypeEnumList);
        registerBuiltin<MasteringDisplay>("MasteringDisplay", Variant::TypeMasteringDisplay);
        registerBuiltin<ContentLightLevel>("ContentLightLevel", Variant::TypeContentLightLevel);
        registerBuiltin<Url>("Url", Variant::TypeUrl);
        registerBuiltin<WindowedStat>("WindowedStat", Variant::TypeWindowedStat);
        registerBuiltin<VariantList>("VariantList", Variant::TypeVariantList);
        registerBuiltin<VariantMap>("VariantMap", Variant::TypeVariantMap);
        registerBuiltin<XmlDocument>("XmlDocument", Variant::TypeXmlDocument);
        registerBuiltin<Cea708Cdp>("Cea708Cdp", Variant::TypeCea708Cdp);
        registerBuiltin<Cea708Service>("Cea708Service", Variant::TypeCea708Service);
        registerBuiltin<Cea708DtvccPacket>("Cea708DtvccPacket", Variant::TypeCea708DtvccPacket);
        registerBuiltin<Cea608Packet>("Cea608Packet", Variant::TypeCea608);
        registerBuiltin<Subtitle>("Subtitle", Variant::TypeSubtitle);
        registerBuiltin<HdrStaticMetadata>("HdrStaticMetadata", Variant::TypeHdrStaticMetadata);
        registerBuiltin<HdrDynamic2094_40>("HdrDynamic2094_40", Variant::TypeHdrDynamic2094_40);
#if PROMEKI_ENABLE_NETWORK
        registerBuiltin<SocketAddress>("SocketAddress", Variant::TypeSocketAddress);
        registerBuiltin<SdpSession>("SdpSession", Variant::TypeSdpSession);
        registerBuiltin<MacAddress>("MacAddress", Variant::TypeMacAddress);
        registerBuiltin<EUI64>("EUI64", Variant::TypeEUI64);
#endif
#if PROMEKI_ENABLE_TLS
        registerBuiltin<SslContext::Ptr>("SslContext::Ptr", Variant::TypeSslContext);
#endif
        return;
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

// TypeRegistry-backed wrappers that accept String → wrapper via
// lookup() / fromString().  MemSpace and AncFormat are deliberately
// absent — they have no String → wrapper branch in @ref convertOne.
using TypeRegFromStringGroup = std::tuple<ColorModel, PixelMemLayout, PixelFormat, VideoCodec,
                                          AudioCodec, AudioFormat, AudioStreamDesc>;

// TypeRegistry wrappers that accept integer → wrapper via the
// ID cast.  AncFormat is absent (no integer → AncFormat branch in
// @ref convertOne).
using TypeRegFromIntegerGroup = std::tuple<ColorModel, MemSpace, PixelMemLayout, PixelFormat,
                                           VideoCodec, AudioCodec, AudioFormat, AudioStreamDesc>;

// TypeRegistry wrappers that produce wrapper → String via name() and
// wrapper → integer via id().  Every is_typereg_v specialization in
// @ref convertOne participates — including AncFormat.
using TypeRegToStringIntegerGroup = std::tuple<ColorModel, MemSpace, PixelMemLayout, PixelFormat,
                                               VideoCodec, AudioCodec, AudioFormat,
                                               AudioStreamDesc, AncFormat>;

// Types that round-trip through their canonical String form via
// fromString / toString.
using StringRoundTripGroup = std::tuple<DateTime, UUID, UMID, Timecode, FrameRate, VideoFormat,
                                        VariantList, VariantMap, Color, Enum, MediaTimeStamp,
                                        FrameNumber, FrameCount, MediaDuration, Url, WindowedStat,
                                        AudioChannelMap>;

// Types that have a toString() branch in @ref convertOne's To=String
// case but no inverse (one-way to String only).
using ToStringOnlyGroup = std::tuple<StringList, Duration, Rational<int>, Size2Du32, TimeStamp,
                                     EnumList>;

#if PROMEKI_ENABLE_NETWORK
using NetworkStringRoundTripGroup = std::tuple<SocketAddress, SdpSession, MacAddress, EUI64>;
#endif

void registerAllBuiltinConverters() {
        // Numeric ↔ Numeric — full Cartesian product within the group
        // (every numeric promotes / demotes / truncates to every other
        // numeric via promekiConvert).
        NumericGroup *nums = nullptr;
        registerGroupCross(nums, nums);

        // Numeric → String and String → Numeric.
        registerToOneFromGroup<String>(nums);
        registerFromOneToGroup<String>(nums);

        // String ↔ (round-tripping types).  The String → T direction
        // routes through the registry's @c ops.fromString slot via
        // @ref convertViaOpsFromString, replacing the per-type
        // @c if-constexpr branches that used to live in @ref convertOne.
        StringRoundTripGroup *strRT = nullptr;
        registerStringToGroupVia(strRT);
        registerToOneFromGroup<String>(strRT);

        // One-way → String for types that don't parse back.
        ToStringOnlyGroup *oneWay = nullptr;
        registerToOneFromGroup<String>(oneWay);

        // TypeRegistry wrappers: lookup-by-name (String → wrapper) and
        // name() (wrapper → String).  Two membership lists because not
        // every wrapper accepts the String input direction.  The
        // String → wrapper direction routes through
        // @ref convertViaOpsFromString — every typereg wrapper now
        // publishes the @c Result<T> @c fromString sibling that
        // @ref Detail::HasResultFromString detects.
        TypeRegFromStringGroup       *trFromStr = nullptr;
        TypeRegToStringIntegerGroup  *trToAny   = nullptr;
        registerStringToGroupVia(trFromStr);
        registerToOneFromGroup<String>(trToAny);

        // TypeRegistry wrappers ↔ integer via the raw ID cast.
        // From-integer membership excludes AncFormat (no input branch
        // for it); to-integer membership includes every wrapper.
        TypeRegFromIntegerGroup *trFromInt = nullptr;
        IntegerGroup            *ints      = nullptr;
        registerGroupCross(ints, trFromInt);
        registerGroupCross(trToAny, ints);

        // Rational ↔ FrameRate, and both → float/double.  Rational and
        // FrameRate are not in any of the groups above because their
        // conversion paths are bespoke.
        registerOnePair<Rational<int>, FrameRate>();
        registerOnePair<Rational<int>, float>();
        registerOnePair<Rational<int>, double>();
        registerOnePair<FrameRate, float>();
        registerOnePair<FrameRate, double>();

        // FrameNumber / FrameCount → every numeric (integers via
        // value(), floats with NaN / infinity for unknown / infinite).
        registerFromOneToGroup<FrameNumber>(nums);
        registerFromOneToGroup<FrameCount>(nums);
        // Integer → FrameNumber / FrameCount via the integral
        // construction branch in convertOne.  Floats are deliberately
        // excluded — convertOne doesn't gate them.
        registerToOneFromGroup<FrameNumber>(ints);
        registerToOneFromGroup<FrameCount>(ints);

        // Enum → integer via Enum::value().  No reverse — integer →
        // Enum requires a target Enum::Type to be sensible and isn't
        // wired into convertOne.
        registerFromOneToGroup<Enum>(ints);

        // String → StringList (val.split(",")) and the StringList ↔
        // VariantList pair.
        registerOnePair<String, StringList>();
        registerOnePair<StringList, VariantList>();
        registerOnePair<VariantList, StringList>();

#if PROMEKI_ENABLE_NETWORK
        NetworkStringRoundTripGroup *netRT = nullptr;
        registerStringToGroupVia(netRT);
        registerToOneFromGroup<String>(netRT);
#endif

        // Identity (T → T) is handled by the same-type fast path in
        // @ref Variant::get, so no registration is needed.
        return;
}

// Construct-on-first-use trigger.  The first call to
// @ref ensureBuiltinDataTypesRegistered constructs this and runs the
// registrations; subsequent calls are no-ops (the static is already
// alive).  Function-local-static initialization is thread-safe per
// the C++11 magic-statics guarantee.
struct RegistryInit {
                RegistryInit() {
                        registerBuiltinTypes();
                        registerAllBuiltinConverters();
                }
};

} // anonymous namespace

void ensureBuiltinDataTypesRegistered() {
        static const RegistryInit r;
        (void)r;
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
// DataStream operators for VariantList / VariantMap
// ============================================================================

DataStream &operator<<(DataStream &stream, const VariantList &list) {
        stream.beginFrame(DataStream::TypeVariantList, 1);
        stream << static_cast<uint32_t>(list.size());
        for (size_t i = 0; i < list.size(); ++i) stream << list[i];
        stream.endFrame();
        return stream;
}

DataStream &operator>>(DataStream &stream, VariantList &list) {
        list.clear();
        if (!stream.readFrame(DataStream::TypeVariantList)) return stream;
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
        stream.beginFrame(DataStream::TypeVariantMap, 1);
        stream << static_cast<uint32_t>(map.size());
        map.forEach([&stream](const String &k, const Variant &v) { stream << k << v; });
        stream.endFrame();
        return stream;
}

DataStream &operator>>(DataStream &stream, VariantMap &map) {
        map.clear();
        if (!stream.readFrame(DataStream::TypeVariantMap)) return stream;
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
