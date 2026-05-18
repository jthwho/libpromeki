/**
 * @file      datatype.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdlib>
#include <promeki/datatype.h>
#include <promeki/hashmap.h>
#include <promeki/map.h>
#include <promeki/mutex.h>
#include <promeki/logger.h>
// registerBuiltinDataTypes needs every library-provided type complete so
// it can invoke registerDataType<T>() with the right ops table.  The
// list mirrors what variant.cpp used to pull in before the builtin
// registration moved here.
#include <promeki/datastream.h>
#include <promeki/variant.h>
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
#include <promeki/stringlist.h>
#include <promeki/color.h>
#include <promeki/colormodel.h>
#include <promeki/memspace.h>
#include <promeki/enum.h>
#include <promeki/enumlist.h>
#include <promeki/hdmisignalconfig.h>
#include <promeki/masteringdisplay.h>
#include <promeki/contentlightlevel.h>
#include <promeki/json.h>
#include <promeki/sdioutputfanoutconfig.h>
#include <promeki/sdisignalconfig.h>
#include <promeki/url.h>
#include <promeki/videoportref.h>
#include <promeki/videoreferenceconfig.h>
#include <promeki/windowedstat.h>
#include <promeki/xml.h>
#include <promeki/xyzcolor.h>
#if PROMEKI_ENABLE_PROAV
#include <promeki/videoformat.h>
#include <promeki/pixelmemlayout.h>
#include <promeki/pixelformat.h>
#include <promeki/videocodec.h>
#include <promeki/audiocodec.h>
#include <promeki/audioformat.h>
#include <promeki/ancformat.h>
#include <promeki/audiostreamdesc.h>
#include <promeki/audiochannelmap.h>
#include <promeki/audiomarker.h>
#include <promeki/cea708cdp.h>
#include <promeki/cea708service.h>
#include <promeki/cea608packet.h>
#include <promeki/subtitle.h>
#include <promeki/hdrstaticmetadata.h>
#include <promeki/hdrdynamic2094_40.h>
#endif
#if PROMEKI_ENABLE_NETWORK
#include <promeki/socketaddress.h>
#include <promeki/sdpsession.h>
#include <promeki/macaddress.h>
#include <promeki/eui64.h>
#endif
#if PROMEKI_ENABLE_TLS
#include <promeki/sslcontext.h>
#endif

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(DataType)

namespace {

/**
 * @brief Process-lifetime registry backing @ref DataType.
 *
 * Three parallel indexes keep every form of lookup fast:
 *  - @c byId      — wire-format tag → record.
 *  - @c byCppType — std::type_index → record.
 *  - @c byName    — string name → record.
 *
 * Data records are heap-allocated by @c registerType and never freed.
 * The pointers handed out via @ref DataType are stable for the
 * lifetime of the process.
 */
struct Registry {
        Mutex                                              mutex;
        Map<DataTypeID, const DataType::Data *>            byId;
        HashMap<std::type_index, const DataType::Data *>   byCppType;
        Map<String, const DataType::Data *>                byName;
};

/**
 * @brief Construct-on-first-use accessor for the registry.
 */
Registry &registry() {
        static Registry r;
        return r;
}

} // anonymous namespace

DataType::DataType(DataTypeID id) {
        Registry         &r = registry();
        Mutex::Locker     lock(r.mutex);
        auto              it = r.byId.find(id);
        _data = (it != r.byId.end()) ? it->second : nullptr;
        return;
}

const DataType::Ops &DataType::ops() const {
        if (_data == nullptr) {
                promekiErr("DataType::ops() called on invalid handle");
                std::abort();
        }
        return _data->ops;
}

DataType DataType::registerType(DataTypeID id, const char *name, uint32_t version,
                                std::type_index ti, size_t size, size_t align, Ops ops) {
        if (name == nullptr || *name == '\0') {
                promekiWarn("DataType::registerType: empty name");
                return DataType();
        }
        if (id == DataTypeInvalid) {
                promekiWarn("DataType::registerType: refusing to register '%s' with DataTypeInvalid id",
                            name);
                return DataType();
        }
        Registry      &r = registry();
        Mutex::Locker  lock(r.mutex);

        // Reject re-registration of the same C++ type.  Catches
        // duplicate registrations in two TUs.
        if (auto it = r.byCppType.find(ti); it != r.byCppType.end()) {
                promekiWarn("DataType::registerType: type already registered as '%s' (id 0x%04x); "
                            "attempted to register as '%s' (id 0x%04x)",
                            it->second->name, static_cast<unsigned>(it->second->id),
                            name, static_cast<unsigned>(id));
                return DataType(it->second);
        }

        if (auto it = r.byId.find(id); it != r.byId.end()) {
                promekiWarn("DataType::registerType: id 0x%04x already in use by '%s'; "
                            "rejected registration of '%s'",
                            static_cast<unsigned>(id), it->second->name, name);
                return DataType();
        }

        if (auto it = r.byName.find(String(name)); it != r.byName.end()) {
                promekiWarn("DataType::registerType: name '%s' already in use by id 0x%04x; "
                            "rejecting registration",
                            name, static_cast<unsigned>(it->second->id));
                return DataType();
        }

        Data *d = new Data;
        d->id      = id;
        d->name    = name;
        d->version = version;
        d->size    = size;
        d->align   = align;
        d->cppType = ti;
        d->ops     = std::move(ops);

        r.byId.insert(id, d);
        r.byCppType.insert(ti, d);
        r.byName.insert(String(name), d);

        promekiDebug("DataType: registered '%s' id 0x%04x ver %u size %zu align %zu", name,
                     static_cast<unsigned>(id), static_cast<unsigned>(version), size, align);
        return DataType(d);
}

List<DataTypeID> DataType::registeredIds() {
        Registry         &r = registry();
        Mutex::Locker     lock(r.mutex);
        List<DataTypeID>  out;
        out.reserve(r.byId.size());
        for (auto it = r.byId.cbegin(); it != r.byId.cend(); ++it) {
                out.pushToBack(it->first);
        }
        return out;
}

DataType DataType::byId(DataTypeID id) {
        if (id == DataTypeInvalid) return DataType();
        Registry      &r = registry();
        Mutex::Locker  lock(r.mutex);
        auto           it = r.byId.find(id);
        return (it != r.byId.end()) ? DataType(it->second) : DataType();
}

DataType DataType::byCppType(std::type_index ti) {
        Registry      &r = registry();
        Mutex::Locker  lock(r.mutex);
        auto           it = r.byCppType.find(ti);
        return (it != r.byCppType.end()) ? DataType(it->second) : DataType();
}

DataType DataType::byName(const char *name) {
        if (name == nullptr || *name == '\0') return DataType();
        Registry      &r = registry();
        Mutex::Locker  lock(r.mutex);
        auto           it = r.byName.find(String(name));
        return (it != r.byName.end()) ? DataType(it->second) : DataType();
}

// ============================================================================
// Builtin DataType registration
//
// Centralised list of every library-provided type that participates in
// Variant / DataStream.  Each entry funnels through one of the two
// registerDataType<T>() overloads in datatype.h:
//
//   - Types annotated with PROMEKI_DATATYPE (the overwhelming majority
//     of library types) use the no-argument form that picks up the
//     macro's id / name / version trait.
//   - Primitives and template instantiations that cannot host the
//     macro use the explicit (id, name, version) form.
//
// After every type is in the registry, Variant::registerBuiltinConverters()
// wires up the bespoke (From, To) converter pairs that have no
// concept-based discovery path (numeric Cartesian, FrameRate
// arithmetic, TypeRegistry ID casts, ...).  Auto-discoverable
// String <-> T converters are wired up implicitly by each
// registerDataType<T>() call via Detail::registerAutoConverters.
// ============================================================================

void registerBuiltinDataTypes() {
        // Guarded by a process-lifetime static @c bool — the lambda body
        // runs exactly once across all threads, and subsequent calls
        // hit the cached @c true with no further work.
        static const bool once = []() {
                // Primitives — no PROMEKI_DATATYPE macro, so we hand the
                // pinned id / name / version explicitly.
                registerDataType<bool>(DataTypeBool, "bool");
                registerDataType<uint8_t>(DataTypeUInt8, "uint8_t");
                registerDataType<int8_t>(DataTypeInt8, "int8_t");
                registerDataType<uint16_t>(DataTypeUInt16, "uint16_t");
                registerDataType<int16_t>(DataTypeInt16, "int16_t");
                registerDataType<uint32_t>(DataTypeUInt32, "uint32_t");
                registerDataType<int32_t>(DataTypeInt32, "int32_t");
                registerDataType<uint64_t>(DataTypeUInt64, "uint64_t");
                registerDataType<int64_t>(DataTypeInt64, "int64_t");
                registerDataType<float>(DataTypeFloat, "float");
                registerDataType<double>(DataTypeDouble, "double");
                registerDataType<String>(DataTypeString, "String");

                // Template instantiations — can't host the macro, so
                // use the explicit overload.
                registerDataType<Size2Du32>(DataTypeSize2D, "Size2Du32");
                registerDataType<Rational<int>>(DataTypeRational, "Rational<int>");

                // JsonObject / JsonArray can't host the macro either —
                // they predate the registry and ship their wire
                // operators as free functions (see HasFreeDataStream*
                // specializations in json.h).
                registerDataType<JsonObject>(DataTypeJsonObject, "JsonObject");
                registerDataType<JsonArray>(DataTypeJsonArray, "JsonArray");

                // Types with PROMEKI_DATATYPE — the no-arg overload
                // picks up id / name / version from the macro trait.
                registerDataType<DateTime>();
                registerDataType<TimeStamp>();
                registerDataType<MediaTimeStamp>();
                registerDataType<FrameNumber>();
                registerDataType<FrameCount>();
                registerDataType<MediaDuration>();
                registerDataType<Duration>();
                registerDataType<UUID>();
                registerDataType<UMID>();
                registerDataType<Timecode>();
                registerDataType<FrameRate>();
                registerDataType<StringList>();
                registerDataType<Color>();
                registerDataType<ColorModel>();
                registerDataType<XYZColor>();
                registerDataType<MemSpace>();
                registerDataType<Enum>();
                registerDataType<EnumList>();
                registerDataType<MasteringDisplay>();
                registerDataType<ContentLightLevel>();
                registerDataType<Url>();
                registerDataType<VideoPortRef>();
                registerDataType<SdiSignalConfig>();
                registerDataType<SdiOutputFanoutConfig>();
                registerDataType<HdmiSignalConfig>();
                registerDataType<VideoReferenceConfig>();

                registerDataType<WindowedStat>();
                registerDataType<VariantList>();
                registerDataType<VariantMap>();
                registerDataType<XmlDocument>();

#if PROMEKI_ENABLE_PROAV
                registerDataType<VideoFormat>();
                registerDataType<PixelMemLayout>();
                registerDataType<PixelFormat>();
                registerDataType<VideoCodec>();
                registerDataType<AudioCodec>();
                registerDataType<AudioFormat>();
                registerDataType<AncFormat>();
                registerDataType<AudioStreamDesc>();
                registerDataType<AudioChannelMap>();
                registerDataType<AudioMarkerList>();
                registerDataType<Cea708Cdp>();
                registerDataType<Cea708Service>();
                registerDataType<Cea708DtvccPacket>();
                registerDataType<Cea608Packet>();
                registerDataType<Subtitle>();
                registerDataType<HdrStaticMetadata>();
                registerDataType<HdrDynamic2094_40>();
#endif
#if PROMEKI_ENABLE_NETWORK
                registerDataType<SocketAddress>();
                registerDataType<SdpSession>();
                registerDataType<MacAddress>();
                registerDataType<EUI64>();
#endif
#if PROMEKI_ENABLE_TLS
                // @ref SslContext carries the @ref PROMEKI_DATATYPE
                // annotation, but its @c writeToStream and
                // @c readFromStream stubs deliberately return
                // @ref Error::NotSupported.  TLS contexts hold cert
                // and private-key material, and the DataStream
                // surface is used for persistence and IPC — letting
                // those bytes ride that wire would leak credentials
                // anywhere a @ref Variant carrying an SslContext
                // gets serialized.  The trait still lets the type
                // participate in Variant typing / round-tripping (by
                // shared reference), it just refuses to flatten the
                // mbedTLS state onto a byte stream.
                registerDataType<SslContext>();
#endif

                // Bespoke converters — numeric Cartesian, FrameRate
                // arithmetic, TypeRegistry ID casts, FrameNumber /
                // FrameCount numeric, Enum → integer, etc.  These have
                // no concept-based discovery path so they live in
                // variant.cpp and are wired up after every type record
                // is in place.
                Variant::registerBuiltinConverters();

                return true;
        }();
        (void)once;
        return;
}

PROMEKI_NAMESPACE_END
