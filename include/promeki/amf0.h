/**
 * @file      amf0.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <promeki/namespace.h>
#include <promeki/pair.h>
#include <promeki/string.h>
#include <promeki/list.h>
#include <promeki/sharedptr.h>
#include <promeki/error.h>
#include <promeki/result.h>

PROMEKI_NAMESPACE_BEGIN

class Buffer;
class BufferView;

/**
 * @brief AMF0 (Action Message Format 0) value tree.
 * @ingroup serialization
 *
 * AMF0 is the legacy Adobe Flash Action-Message encoding used by RTMP
 * for every command exchange (`connect`, `_result`, `onStatus`,
 * `onMetaData`, …) that doesn't negotiate AMF3.  The spec lives in the
 * Adobe AMF0 1.0 reference document; the on-the-wire grammar is small
 * (one-byte type marker + length-prefixed payload) but the strict-mode
 * quirks (long string switch at 64 KiB, ECMA-array count hint, FMS
 * end-of-object sentinel) silently break interop unless every case is
 * covered.
 *
 * @par Storage and copy semantics
 * Amf0Value is a value-type handle backed by an internal
 * @c SharedPtr<Amf0Data>.  Copy is a refcount bump; mutating accessors
 * detach a private copy on first write (CoW), matching the
 * post-2026-05-07 convention used by @ref JsonObject / @ref JsonArray
 * and the lowered cost of passing AMF0 trees by value through long
 * call chains.
 *
 * @par Field ordering
 * Object / EcmaArray fields are insertion-ordered.  AMF0 objects are
 * insertion-ordered on the wire and FMS-clone servers (Wowza,
 * nginx-rtmp) reject @c connect bodies whose fields arrive in
 * unexpected order, so the field container is a list of pairs rather
 * than a sorted @c Map.  An opportunistic hash index sitting beside
 * the list keeps lookups O(1) on objects with more than a handful of
 * keys.
 *
 * @par What's not in v1
 * AMF3 is recognized but not parsed: the reader returns
 * @c Error::NotSupported when it sees the AMF3 switch marker
 * (@c 0x11), so a misconfigured peer surfaces as a typed error rather
 * than silently mis-parsing.  Reference values (@c 0x07) are readable
 * but never emitted by us.  RecordSet (@c 0x0E) and MovieClip
 * (@c 0x04) are reserved-for-Flash markers we neither emit nor parse.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently; concurrent access to a single instance must be
 * externally synchronized.
 */
class Amf0Value {
        public:
                /** @brief List of values — used for the StrictArray payload and as an
                 * Amf0Reader return type. */
                using List = promeki::List<Amf0Value>;

                /** @brief Object / EcmaArray field — name + value, insertion-ordered. */
                using Field = promeki::Pair<promeki::String, Amf0Value>;

                /** @brief Insertion-ordered list of object / ecma-array fields. */
                using FieldList = promeki::List<Field>;

                /** @brief Discriminant for the held value. */
                enum Type {
                        Null,         ///< AMF0 null marker.
                        Undefined,    ///< AMF0 undefined marker.
                        Boolean,      ///< AMF0 boolean.
                        Number,       ///< AMF0 number — IEEE 754 double.
                        String,       ///< AMF0 string (short or long; payload is the same).
                        Object,       ///< AMF0 object — ordered field list + terminator.
                        EcmaArray,    ///< AMF0 ECMA-array — count hint + ordered field list + terminator.
                        StrictArray,  ///< AMF0 strict-array — fixed-count value list, no keys.
                        Date,         ///< AMF0 date — ms-since-epoch double + 2-byte timezone.
                        XmlDocument,  ///< AMF0 xml-document — long-string-form payload.
                        TypedObject,  ///< AMF0 typed object — class name + field list.
                        Reference,    ///< AMF0 reference — 16-bit index into the peer's reference table.
                        Unsupported   ///< AMF0 unsupported marker (peer wishes to indicate "no value").
                };

                /**
                 * @brief Wire-format marker bytes.
                 *
                 * Exposed publicly so callers logging or interoperating
                 * with raw AMF0 buffers can refer to the same constants
                 * the codec uses internally.  Values match the AMF0 1.0
                 * reference exactly.
                 */
                enum Marker : uint8_t {
                        MarkerNumber        = 0x00,
                        MarkerBoolean       = 0x01,
                        MarkerString        = 0x02,
                        MarkerObject        = 0x03,
                        MarkerMovieClip     = 0x04, ///< Reserved (Flash-only).
                        MarkerNull          = 0x05,
                        MarkerUndefined     = 0x06,
                        MarkerReference     = 0x07,
                        MarkerEcmaArray     = 0x08,
                        MarkerObjectEnd     = 0x09, ///< Sentinel emitted at the end of every object / ecma-array.
                        MarkerStrictArray   = 0x0A,
                        MarkerDate          = 0x0B,
                        MarkerLongString    = 0x0C, ///< Used for any string >= 64 KiB.
                        MarkerUnsupported   = 0x0D,
                        MarkerRecordSet     = 0x0E, ///< Reserved (Flash-only).
                        MarkerXmlDocument   = 0x0F,
                        MarkerTypedObject   = 0x10,
                        MarkerAvmPlusObject = 0x11  ///< AMF3 switch marker — recognised, never emitted.
                };

                /** @brief Constructs a Null value. */
                Amf0Value();
                /** @brief Constructs a Boolean value. */
                Amf0Value(bool b);
                /** @brief Constructs a Number value. */
                Amf0Value(double d);
                /** @brief Constructs a Number value from a signed int. */
                Amf0Value(int i);
                /** @brief Constructs a Number value from a signed 64-bit int. */
                Amf0Value(int64_t i);
                /** @brief Constructs a String value. */
                Amf0Value(const promeki::String &s);
                /** @brief Constructs a String value. */
                Amf0Value(const char *s);

                /** @brief Copy-constructor — shares storage, no detach. */
                Amf0Value(const Amf0Value &);
                /** @brief Move-constructor. */
                Amf0Value(Amf0Value &&) noexcept;
                /** @brief Copy-assignment — shares storage, no detach. */
                Amf0Value &operator=(const Amf0Value &);
                /** @brief Move-assignment. */
                Amf0Value &operator=(Amf0Value &&) noexcept;
                /**
                 * @brief Destructor.  Out-of-line so the @c SharedPtr<Amf0Data>
                 *        instantiation only happens in @c amf0.cpp, where
                 *        @c Amf0Data is fully defined.  Inlining the
                 *        destructor here triggers a
                 *        @c -Wdelete-incomplete on every translation
                 *        unit that builds with the header's forward
                 *        declaration in scope.
                 */
                ~Amf0Value();

                /** @brief Returns an Undefined value. */
                static Amf0Value undefined();

                /** @brief Returns an Unsupported value. */
                static Amf0Value unsupported();

                /** @brief Returns a Reference value carrying @p index. */
                static Amf0Value reference(uint16_t index);

                /** @brief Builds an Object from an initializer list of fields. */
                static Amf0Value object(std::initializer_list<Field> kv = {});

                /**
                 * @brief Builds an EcmaArray.
                 * @param countHint The count-hint emitted on the wire ahead of the field map.
                 *                  Some peers use this for preallocation; correctness does not
                 *                  depend on the value matching the field count.  Defaults to 0
                 *                  if omitted.
                 */
                static Amf0Value ecmaArray(std::initializer_list<Field> kv = {}, uint32_t countHint = 0);

                /** @brief Builds a StrictArray from an initializer list. */
                static Amf0Value strictArray(std::initializer_list<Amf0Value> items = {});

                /**
                 * @brief Builds a Date value.
                 * @param msSinceEpoch  Milliseconds since the Unix epoch (AMF0 wire form).
                 * @param timezone      Mandated by FMS to be zero; carried verbatim
                 *                      for round-trip fidelity.
                 */
                static Amf0Value date(double msSinceEpoch, int16_t timezone = 0);

                /** @brief Builds an XmlDocument value carrying @p xml verbatim. */
                static Amf0Value xmlDocument(const promeki::String &xml);

                /** @brief Builds a TypedObject. */
                static Amf0Value typedObject(const promeki::String &className,
                                             std::initializer_list<Field> kv = {});

                /** @brief Returns the discriminant for the held value. */
                Type type() const;

                /** @brief True iff the value is @ref Null. */
                bool isNull() const { return type() == Null; }
                /** @brief True iff the value is @ref Undefined. */
                bool isUndefined() const { return type() == Undefined; }
                /** @brief True iff the value is @ref Boolean. */
                bool isBoolean() const { return type() == Boolean; }
                /** @brief True iff the value is @ref Number. */
                bool isNumber() const { return type() == Number; }
                /** @brief True iff the value is @ref String. */
                bool isString() const { return type() == String; }
                /** @brief True iff the value is @ref Object. */
                bool isObject() const { return type() == Object; }
                /** @brief True iff the value is @ref EcmaArray. */
                bool isEcmaArray() const { return type() == EcmaArray; }
                /** @brief True iff the value is @ref StrictArray. */
                bool isStrictArray() const { return type() == StrictArray; }
                /** @brief True iff the value is @ref Date. */
                bool isDate() const { return type() == Date; }
                /** @brief True iff the value is @ref XmlDocument. */
                bool isXmlDocument() const { return type() == XmlDocument; }
                /** @brief True iff the value is @ref TypedObject. */
                bool isTypedObject() const { return type() == TypedObject; }
                /** @brief True iff the value is @ref Reference. */
                bool isReference() const { return type() == Reference; }
                /** @brief True iff the value is @ref Unsupported. */
                bool isUnsupportedMarker() const { return type() == Unsupported; }

                /** @brief Returns the boolean value, or @p def if not a Boolean. */
                bool asBool(bool def = false) const;
                /** @brief Returns the numeric value, or @p def if not a Number. */
                double asNumber(double def = 0.0) const;
                /** @brief Returns the string value (for String / XmlDocument), or @p def. */
                promeki::String asString(const promeki::String &def = promeki::String()) const;

                /** @brief For Date: ms since epoch. */
                double dateMs() const;
                /** @brief For Date: timezone field carried for fidelity. */
                int16_t dateTimezone() const;

                /** @brief For Reference: peer reference table index. */
                uint16_t referenceIndex() const;

                /** @brief For TypedObject: the class name. */
                const promeki::String &className() const;

                /**
                 * @brief Returns the object / ecma-array / typed-object
                 *        field list (insertion-ordered).
                 *
                 * For non-field-bearing types returns an empty list.
                 */
                const FieldList &fields() const;

                /**
                 * @brief Mutable field list — CoW-detaches.
                 *
                 * Returning a reference that the caller may mutate
                 * preserves insertion order without forcing them
                 * through @ref setField.  The first call after a
                 * shared copy detaches.
                 */
                FieldList &fields();

                /**
                 * @brief Looks up @p key in the field list.
                 * @return Pointer to the value, or @c nullptr if absent.
                 *         Pointer remains valid until the next mutation.
                 */
                const Amf0Value *find(const promeki::String &key) const;

                /**
                 * @brief Mutable lookup — CoW-detaches.
                 * @return Pointer to the value, or @c nullptr if absent.
                 */
                Amf0Value *find(const promeki::String &key);

                /** @brief True iff the field list contains @p key. */
                bool contains(const promeki::String &key) const;

                /**
                 * @brief Inserts @p key with @p value, or replaces an
                 *        existing field's value in place.
                 *
                 * Insertion preserves position: a replace does not move
                 * the field to the back, and a new key is appended.
                 */
                void setField(const promeki::String &key, Amf0Value value);

                /** @brief For EcmaArray: the count-hint emitted on the wire. */
                uint32_t ecmaCountHint() const;

                /** @brief Sets the EcmaArray count hint.  CoW-detaches. */
                void setEcmaCountHint(uint32_t count);

                /** @brief For StrictArray: the items. */
                const List &items() const;

                /** @brief Mutable item list for StrictArray.  CoW-detaches. */
                List &items();

                /**
                 * @brief Serializes this value to its AMF0 wire form,
                 *        appended to @p out.
                 *
                 * Uses the LongString marker automatically when a string
                 * payload exceeds the 16-bit length field; emits the
                 * @c \\0\\0\\x09 sentinel at the end of every object /
                 * ecma-array; emits a 2-byte timezone after every Date.
                 *
                 * @return @c Error::Ok on success, @c Error::Invalid if
                 *         the value is structurally malformed (e.g. a
                 *         Reference index outside the 16-bit range).
                 */
                Error serialize(Buffer &out) const;

                /** @brief Returns a debug-friendly textual rendering. */
                promeki::String toDebugString(unsigned int indent = 0) const;

                /** @brief Equality on type and contents. */
                bool operator==(const Amf0Value &other) const;
                /** @brief Negated @ref operator==. */
                bool operator!=(const Amf0Value &other) const { return !(*this == other); }

        private:
                struct Amf0Data;
                SharedPtr<Amf0Data> _d;

                // Out-of-line so the SharedPtr<Amf0Data> temporary destructor
                // only instantiates inside amf0.cpp (where Amf0Data is fully
                // defined) rather than at every include site.
                explicit Amf0Value(SharedPtr<Amf0Data> d);
};

/**
 * @brief Decodes one or more AMF0 values from a byte buffer.
 * @ingroup serialization
 *
 * AMF0 messages on the RTMP wire are a concatenation of values rather
 * than a single tagged blob — for example, an @c onStatus reply is a
 * fixed sequence of @c (string command, double transactionId, null
 * arg, object info).  @ref Amf0Reader::read returns the full list so
 * callers can match positionally, mirroring how the wire is laid out.
 *
 * Errors:
 *  - @c Error::CorruptData on a malformed or unknown marker byte.
 *  - @c Error::OutOfRange on truncation (a field length runs past the
 *    end of the buffer, or an object misses its terminator).
 *  - @c Error::NotSupported when the AMF3 switch marker (@c 0x11) is
 *    encountered — surface to the caller so it can log and ask the
 *    peer to negotiate @c objectEncoding=0.
 */
class Amf0Reader {
        public:
                /** @brief Convenience: read every value out of a single-slice BufferView. */
                static Result<Amf0Value::List> read(const BufferView &view);

                /**
                 * @brief Read every value out of a raw byte range.
                 *
                 * This is the lower-level entry point — the
                 * BufferView overload defers to it after checking that
                 * the view is single-slice.  Multi-slice views are
                 * rejected with @c Error::NotSupported because the
                 * RTMP chunk-stream layer always delivers reassembled,
                 * single-slice messages anyway.
                 */
                static Result<Amf0Value::List> read(const uint8_t *data, size_t len);

                /**
                 * @brief Read a single value at @p offset, advancing it past
                 *        the bytes consumed.
                 *
                 * Used by the chunk-stream / session layer when a
                 * caller knows it has exactly N expected values and
                 * wants to walk them positionally.
                 */
                static Error readOne(const uint8_t *data, size_t len, size_t &offset, Amf0Value &out);
};

/**
 * @brief Streaming AMF0 writer.
 * @ingroup serialization
 *
 * @c Amf0Writer wraps a @c Buffer that the caller owns and provides
 * incremental @c writeXxx helpers — useful when a caller has a fixed
 * sequence of values to emit and building an @ref Amf0Value tree just
 * to serialize it once would be wasteful.  For tree-shaped data, build
 * an @ref Amf0Value and call @ref Amf0Value::serialize directly.
 *
 * The output buffer auto-grows on writes that overflow its current
 * @c availSize.  The writer does not own the buffer; the caller is
 * responsible for keeping the @ref Buffer alive for the duration of
 * the writer's lifetime.
 */
class Amf0Writer {
        public:
                /**
                 * @brief Constructs a writer that appends to @p out.
                 *
                 * Bytes are appended starting at the buffer's current
                 * @c size — callers can interleave manual writes with
                 * Amf0Writer calls as long as @c size is consistent.
                 */
                explicit Amf0Writer(Buffer &out);

                /** @brief Emits a Number value. */
                Error writeNumber(double v);
                /** @brief Emits a Boolean value. */
                Error writeBoolean(bool v);
                /**
                 * @brief Emits a String value (auto-promotes to LongString >= 64 KiB).
                 */
                Error writeString(const promeki::String &s);
                /** @brief Emits a Null value. */
                Error writeNull();
                /** @brief Emits an Undefined value. */
                Error writeUndefined();
                /** @brief Emits a Date value. */
                Error writeDate(double msSinceEpoch, int16_t timezone = 0);
                /** @brief Emits a StrictArray value containing every element of @p items. */
                Error writeStrictArray(const Amf0Value::List &items);
                /** @brief Emits an Object value containing every field. */
                Error writeObject(const Amf0Value::FieldList &fields);
                /** @brief Emits an EcmaArray value with the given count hint. */
                Error writeEcmaArray(const Amf0Value::FieldList &fields, uint32_t countHint = 0);
                /** @brief Emits a TypedObject value. */
                Error writeTypedObject(const promeki::String &className,
                                       const Amf0Value::FieldList &fields);
                /** @brief Emits an XmlDocument value. */
                Error writeXmlDocument(const promeki::String &xml);
                /** @brief Generic dispatch — emits whatever @p v holds. */
                Error writeValue(const Amf0Value &v);

                /** @brief Total bytes appended through this writer. */
                size_t bytesWritten() const { return _bytesWritten; }

        private:
                Buffer &_out;
                size_t  _bytesWritten = 0;

                Error appendBytes(const void *bytes, size_t len);
                Error appendByte(uint8_t b);
                Error appendU16BE(uint16_t v);
                Error appendU32BE(uint32_t v);
                Error appendDoubleBE(double v);
                Error appendShortString(const promeki::String &s);   // 16-bit length prefix
                Error appendLongString(const promeki::String &s);    // 32-bit length prefix
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
