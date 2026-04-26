/**
 * @file      variantdatabase.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <optional>
#include <type_traits>
#include <promeki/namespace.h>
#include <promeki/stringregistry.h>
#include <promeki/variant.h>
#include <promeki/variantspec.h>
#include <promeki/error.h>
#include <promeki/map.h>
#include <promeki/list.h>
#include <promeki/json.h>
#include <promeki/stringlist.h>
#include <promeki/textstream.h>
#include <promeki/datastream.h>
#include <promeki/readwritelock.h>
#include <promeki/logger.h>
#include <promeki/util.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Validation mode for VariantDatabase::set().
 * @ingroup util
 *
 * Controls whether values are checked against the registered
 * VariantSpec when stored via @ref VariantDatabase::set.
 */
enum class SpecValidation {
        None,   ///< @brief No validation — values are stored unconditionally.
        Warn,   ///< @brief Log a warning for out-of-spec values, but store them.
        Strict  ///< @brief Reject out-of-spec values (set returns false).  This is the default.
};

/**
 * @brief A collection of named Variant values keyed by string-registered IDs.
 * @ingroup util
 *
 * @tparam Name A compile-time string literal identifying this
 *         database's ID namespace.  Each unique name gets its own
 *         independent ID space via the underlying StringRegistry,
 *         and the name appears in any collision diagnostic so the
 *         responsible database is obvious from the failure message.
 *
 * VariantDatabase maps string names to Variant values using integer IDs
 * for fast lookup.  The nested ID type is a StringRegistry::Item scoped
 * to this database's Name.
 *
 * Each ID can optionally have a @ref VariantSpec registered via
 * @ref declareID.  Specs describe the accepted types, numeric range,
 * default value, and human-readable description.  When a spec is
 * registered, @ref set can optionally validate incoming values
 * (controlled by @ref setValidation).
 *
 * Supports serialization to/from JSON, TextStream, and DataStream.
 * All serialization formats use the string names (not integer IDs),
 * so data can be safely persisted and loaded across runs.
 *
 * @warning The integer IDs within ID are deterministic hashes of the
 *          string names, so they are stable across runs for the same
 *          name but not across renames.  Well-known IDs (declared via
 *          @ref declareID) use a strict registration path that aborts
 *          on hash collision; runtime-dynamic names use a probing
 *          path that never fails.  Serialization formats continue
 *          to carry the name, not the ID.
 *
 * @par Example
 * @code
 * using Config = VariantDatabase<"Config">;
 *
 * static inline const Config::ID Width = Config::declareID("Width",
 *     VariantSpec().setType(Variant::TypeS32).setDefault(1920)
 *                  .setRange(1, 8192).setDescription("Frame width in pixels"));
 *
 * Config cfg;
 * cfg.set(Width, 1920);
 * int w = cfg.getAs<int32_t>(Width); // 1920
 *
 * const VariantSpec *s = Config::spec(Width);
 * String desc = s->description(); // "Frame width in pixels"
 *
 * // Compile-time constant for a well-known name that doesn't need a
 * // spec.  Its ID matches any declareID("Height", ...) in the same Tag.
 * static constexpr Config::ID Height = Config::ID::literal("Height");
 * static_assert(Height.id() == Config::ID::literal("Height").id());
 * @endcode
 */
template <CompiledString Name>
class VariantDatabase {
        public:
                /** @brief Lightweight handle identifying an entry by name. */
                using ID = typename StringRegistry<Name>::Item;

                /** @brief Map of ID to VariantSpec for batch spec operations. */
                using SpecMap = Map<ID, VariantSpec>;

                // ============================================================
                // Static spec registry
                // ============================================================

                /**
                 * @brief Declares an ID and registers its VariantSpec.
                 *
                 * Prefer the @ref PROMEKI_DECLARE_ID macro at the
                 * declaration site: it expands to a `static constexpr ID`
                 * (so the value is usable in `switch`, `static_assert`,
                 * and other constant-expression contexts) plus a sibling
                 * `static inline` declaration that calls this function
                 * to register the spec.
                 *
                 * @param name The string name for the ID.
                 * @param spec The VariantSpec describing this ID.
                 * @return The newly created (or existing) ID.
                 *
                 * @par Example
                 * @code
                 * PROMEKI_DECLARE_ID(Width,
                 *     VariantSpec().setType(Variant::TypeS32)
                 *                  .setDefault(1920)
                 *                  .setDescription("Frame width"));
                 * @endcode
                 */
                static ID declareID(const String &name, const VariantSpec &spec) {
                        // Use the strict registration path so a hash collision
                        // between two well-known names aborts at static-init
                        // time instead of silently diverging from
                        // `ID::literal(name)`.
                        ID id = ID::fromId(StringRegistry<Name>::instance().findOrCreateStrict(name));
                        specRegistry().insert(id.id(), spec);
                        return id;
                }

                /**
                 * @brief Returns the VariantSpec for the given ID, or nullptr if none.
                 * @param id The ID to look up.
                 * @return A pointer to the spec, or nullptr if no spec was registered.
                 */
                static const VariantSpec *spec(ID id) {
                        return specRegistry().find(id.id());
                }

                /**
                 * @brief Returns the VariantSpec for a key name, or nullptr if none.
                 *
                 * Convenience wrapper that resolves @p name through the
                 * per-@c Name StringRegistry without registering it, then
                 * returns the registered spec (if any).  Used by
                 * @ref VariantLookup::specFor when a database-backed path
                 * needs to expose the declared type of a key to callers
                 * that only have the string form (query compilers, tools,
                 * introspectors).
                 *
                 * @param name The key name to look up.
                 * @return A pointer to the registered spec, or nullptr
                 *         when the name is unknown or no spec was
                 *         declared for it.
                 */
                static const VariantSpec *specFor(const String &name) {
                        ID id = ID::find(name);
                        if(!id.isValid()) return nullptr;
                        return spec(id);
                }

                /**
                 * @brief Returns a copy of the entire spec registry.
                 * @return A map from integer ID to VariantSpec.
                 */
                static Map<uint64_t, VariantSpec> registeredSpecs() {
                        return specRegistry().all();
                }

                /**
                 * @brief Builds a VariantDatabase from a SpecMap's default values.
                 *
                 * For each entry in @p specs, the spec's default value is
                 * stored under the corresponding ID.  This is the canonical
                 * way to convert a set of specs into a concrete configuration.
                 *
                 * @param specs The spec map to extract defaults from.
                 * @return A VariantDatabase populated with default values.
                 */
                static VariantDatabase fromSpecs(const SpecMap &specs) {
                        VariantDatabase db;
                        for(auto it = specs.cbegin(); it != specs.cend(); ++it) {
                                const Variant &def = it->second.defaultValue();
                                if(def.isValid()) db._data.insert(it->first.id(), def);
                        }
                        return db;
                }

                /**
                 * @brief Writes formatted help text for every spec in a SpecMap.
                 *
                 * Iterates the map in key-name order and renders each
                 * entry as a single line with three padded columns
                 * (`name`, `details`, `description`).  The details
                 * column is produced by @ref VariantSpec::detailsString.
                 *
                 * @param stream   The output stream.
                 * @param specs    The spec map to format.
                 * @param skipKeys Optional list of key names to omit from
                 *                 the output.  Callers use this to hide
                 *                 keys that are implied by other flags —
                 *                 for example mediaplay hides the `Type`
                 *                 key because it is already set by
                 *                 `-i` / `-o` / `-c`.
                 * @return         The widest physical line width emitted,
                 *                 in characters.  Callers use the value to
                 *                 size a visual border above and below
                 *                 the block.  Returns 0 when nothing was
                 *                 emitted.
                 */
                static int writeSpecMapHelp(TextStream &stream, const SpecMap &specs,
                                            const StringList &skipKeys = StringList()) {
                        // Collect the visible set first so the column
                        // width pass doesn't count skipped keys.
                        StringList names;
                        for(auto it = specs.cbegin(); it != specs.cend(); ++it) {
                                const String &n = it->first.name();
                                if(skipKeys.contains(n)) continue;
                                names.pushToBack(n);
                        }
                        names = names.sort();
                        if(names.isEmpty()) return 0;

                        // Three-column layout: name | details | description.
                        // We cache the per-row details string so the width
                        // pass and the emit pass don't rebuild it twice.
                        List<String> details;
                        details.resize(names.size());
                        int nameWidth = 0;
                        int detailsWidth = 0;
                        for(size_t i = 0; i < names.size(); ++i) {
                                ID id(names[i]);
                                auto it = specs.find(id);
                                if(it == specs.end()) continue;
                                int nw = static_cast<int>(names[i].size());
                                if(nw > nameWidth) nameWidth = nw;
                                details[i] = it->second.detailsString();
                                int dw = static_cast<int>(details[i].size());
                                if(dw > detailsWidth) detailsWidth = dw;
                        }

                        // Fixed structure: "  <name>  <details>  <desc>".
                        // Everything up to the description is a known
                        // size; the description's real width varies per
                        // row, so we track the widest actual line as we
                        // go and return that so the caller can draw a
                        // border matching the block.
                        const int prefixWidth = 2 + nameWidth + 2 + detailsWidth;
                        int maxLineWidth = 0;
                        for(size_t i = 0; i < names.size(); ++i) {
                                ID id(names[i]);
                                auto it = specs.find(id);
                                if(it == specs.end()) continue;

                                String nameCol = names[i];
                                while(static_cast<int>(nameCol.size()) < nameWidth) {
                                        nameCol += ' ';
                                }
                                String detailsCol = details[i];
                                while(static_cast<int>(detailsCol.size()) < detailsWidth) {
                                        detailsCol += ' ';
                                }
                                stream << "  " << nameCol << "  " << detailsCol;
                                const String &desc = it->second.description();
                                int lineWidth = prefixWidth;
                                if(!desc.isEmpty()) {
                                        stream << "  " << desc;
                                        lineWidth += 2 + static_cast<int>(desc.size());
                                }
                                stream << endl;
                                if(lineWidth > maxLineWidth) maxLineWidth = lineWidth;
                        }
                        return maxLineWidth;
                }

                // ============================================================
                // Construction
                // ============================================================

                /** @brief Constructs an empty database with Strict validation. */
                VariantDatabase() = default;

                /**
                 * @brief Sets @p id to @p value, coercing JSON-serialized
                 *        strings back to the spec's native type.
                 *
                 * Used by every "build from JSON" path (the base
                 * @ref fromJson plus subclass overrides such as
                 * @ref Metadata::fromJson) to restore the right
                 * @ref Variant type for keys whose native form is
                 * richer than a plain JSON primitive — @c Size2D,
                 * @c PixelFormat, @c Enum, @c Color, @c FrameRate, etc.
                 * @ref JsonObject::setFromVariant serializes those as
                 * their string form on the way out; this routine
                 * asks the registered @ref VariantSpec to re-parse
                 * that string with @ref VariantSpec::parseString on
                 * the way back.
                 *
                 * The coercion runs only when:
                 *  - @p value is @c Variant::TypeString, and
                 *  - a spec is registered for @p id, and
                 *  - the spec does @em not accept @c TypeString
                 *    natively (so JSON strings that are legitimately
                 *    strings stay strings), and
                 *  - @ref VariantSpec::parseString succeeds.
                 *
                 * Returns @ref Error::ParseFailed when spec-driven
                 * coercion was attempted but the string couldn't be
                 * parsed back into the spec's native type.  Otherwise
                 * delegates to @ref set, which surfaces the
                 * validator's @ref Error code (e.g. @ref Error::Invalid,
                 * @ref Error::OutOfRange, @ref Error::InvalidArgument)
                 * when the database is in Strict mode.
                 *
                 * @param id    The entry identifier.
                 * @param value The JSON-decoded value to store.
                 * @return @ref Error::Ok on success, or a specific
                 *         error code describing why the value was
                 *         rejected.
                 */
                Error setFromJson(ID id, const Variant &value) {
                        if(value.type() == Variant::TypeString) {
                                const VariantSpec *sp = spec(id);
                                if(sp != nullptr
                                   && !sp->acceptsType(Variant::TypeString)) {
                                        Error pe;
                                        Variant parsed = sp->parseString(
                                                value.get<String>(), &pe);
                                        if(pe.isOk()) {
                                                Error err;
                                                set(id, std::move(parsed), &err);
                                                return err;
                                        }
                                        // String didn't parse into the
                                        // spec's native type; report
                                        // that rather than falling back
                                        // to storing the raw string,
                                        // which would itself fail spec
                                        // validation under Strict mode.
                                        return pe;
                                }
                        }
                        Error err;
                        set(id, value, &err);
                        return err;
                }

                /**
                 * @brief Creates a VariantDatabase from a JsonObject.
                 *
                 * Each key in the JSON object becomes an ID (registered
                 * if new) and its value is routed through
                 * @ref setFromJson so the stored @ref Variant carries
                 * the spec-native type rather than whichever JSON
                 * primitive was used on the wire.  Subclasses that need
                 * different lifecycle semantics (e.g. a validity flag
                 * out-parameter) should delegate the per-entry work to
                 * @ref setFromJson rather than reimplementing the
                 * coercion rules.
                 *
                 * @param json The JsonObject to deserialize from.
                 * @return A VariantDatabase populated with the JSON contents.
                 */
                static VariantDatabase fromJson(const JsonObject &json) {
                        VariantDatabase db;
                        json.forEach([&db](const String &key, const Variant &val) {
                                db.setFromJson(ID(key), val);
                        });
                        return db;
                }

                // ============================================================
                // Validation mode
                // ============================================================

                /**
                 * @brief Sets the validation mode for this database instance.
                 *
                 * - @ref SpecValidation::None — no checking on set().
                 * - @ref SpecValidation::Warn — log a warning but store anyway (default).
                 * - @ref SpecValidation::Strict — reject out-of-spec values.
                 *
                 * @param mode The validation mode.
                 */
                void setValidation(SpecValidation mode) { _validation = mode; }

                /**
                 * @brief Returns the current validation mode.
                 * @return The validation mode.
                 */
                SpecValidation validation() const { return _validation; }

                // ============================================================
                // Value management
                // ============================================================

                /**
                 * @brief Sets the value for the given ID.
                 *
                 * If a VariantSpec is registered for this ID and validation
                 * is enabled, the value is checked before storing.  In Warn
                 * mode, a warning is logged but the value is stored.  In
                 * Strict mode, the value is rejected and false is returned.
                 *
                 * @param id    The entry identifier.
                 * @param value The value to store.
                 * @param err   Optional out-param: receives @ref Error::Ok
                 *              on success, or the specific @ref Error code
                 *              the validator returned (e.g.
                 *              @ref Error::Invalid, @ref Error::OutOfRange,
                 *              @ref Error::InvalidArgument) when Strict
                 *              mode rejected the value.  Always set to
                 *              @ref Error::Ok in Warn or None mode.
                 * @return True if the value was stored.  False only in Strict
                 *         mode when validation fails.
                 */
                bool set(ID id, const Variant &value, Error *err = nullptr) {
                        if(!validateOnSet(id, value, err)) return false;
                        _data.insert(id.id(), value);
                        return true;
                }

                /**
                 * @brief Sets the value for the given ID (move overload).
                 * @param id    The entry identifier.
                 * @param value The value to move-store.
                 * @param err   Optional out-param; see the const-ref overload.
                 * @return True if the value was stored.
                 */
                bool set(ID id, Variant &&value, Error *err = nullptr) {
                        if(!validateOnSet(id, value, err)) return false;
                        _data.insert(id.id(), std::move(value));
                        return true;
                }

                /**
                 * @brief Sets the value only if no entry exists for @p id.
                 *
                 * Unlike @ref set, which always overwrites, this call is a
                 * no-op when the key already has a value.  It is intended
                 * as a primitive for filling in defaults without
                 * clobbering caller-supplied values.
                 *
                 * @param id    The entry identifier.
                 * @param value The value to store if no existing entry.
                 * @return true if the value was stored, false if an entry
                 *         already existed for @p id.
                 */
                bool setIfMissing(ID id, const Variant &value) {
                        if(_data.contains(id.id())) return false;
                        _data.insert(id.id(), value);
                        return true;
                }

                /**
                 * @brief Move overload of @ref setIfMissing.
                 * @param id    The entry identifier.
                 * @param value The value to move-store if no existing entry.
                 * @return true if the value was stored, false if an entry
                 *         already existed for @p id.
                 */
                bool setIfMissing(ID id, Variant &&value) {
                        if(_data.contains(id.id())) return false;
                        _data.insert(id.id(), std::move(value));
                        return true;
                }

                /**
                 * @brief Returns the value for the given ID.
                 * @param id           The entry identifier.
                 * @param defaultValue Value returned if the ID is not present.
                 * @return The stored value, or defaultValue if not found.
                 */
                Variant get(ID id, const Variant &defaultValue = Variant()) const {
                        auto it = _data.find(id.id());
                        if(it == _data.end()) return defaultValue;
                        return it->second;
                }

                /**
                 * @brief Returns the stored value converted to the requested type.
                 *
                 * Combines get() and Variant::get<T>() in a single call.
                 *
                 * @tparam T           The desired result type.
                 * @param id           The entry identifier.
                 * @param defaultValue Value returned if the ID is not present or
                 *                     the conversion fails.
                 * @param err          Optional error output from the Variant conversion.
                 * @return The converted value, or defaultValue if not found or on
                 *         conversion failure.
                 */
                template <typename T>
                T getAs(ID id, const T &defaultValue = T{}, Error *err = nullptr) const {
                        auto it = _data.find(id.id());
                        if(it == _data.end()) {
                                if(err) *err = Error::IdNotFound;
                                return defaultValue;
                        }
                        Error e;
                        T result = it->second.template get<T>(&e);
                        if(e.isError()) {
                                if(err) *err = Error::ConversionFailed;
                                return defaultValue;
                        }
                        if(err) *err = Error::Ok;
                        return result;
                }

                /**
                 * @brief Substitutes @c {Key} / @c {Key:spec} tokens in a template string.
                 *
                 * Walks @p tmpl character by character.  Each balanced
                 * @c {…} token is split on the first @c ':' into a key
                 * name and an optional format spec; the key is looked
                 * up in this database (without registering it) and the
                 * stored Variant is rendered via @ref Variant::format,
                 * which dispatches the spec to the held type's own
                 * @c std::formatter.  Literal braces are escaped as
                 * @c "{{" and @c "}}", matching the @c std::format
                 * convention.
                 *
                 * When the database itself does not contain the key,
                 * the optional @p resolver callback is consulted with
                 * @c (keyName, spec).  A returned @c std::optional<String>
                 * holding a value is substituted as-is — typically the
                 * caller will format a Variant from another scope (a
                 * parent database, an environment lookup, etc.) and
                 * return the result, enabling nested resolution chains.
                 * A returned @c std::nullopt (or an absent resolver)
                 * leaves the key unresolved: the output gets the
                 * literal text @c "[UNKNOWN KEY: <name>]" and @p err is
                 * set to @c Error::IdNotFound.
                 *
                 * @p err is set to @c Error::Ok when every token was
                 * either found in this database or resolved by the
                 * callback, and to @c Error::IdNotFound if @em any
                 * token fell through to the @c "[UNKNOWN KEY: …]"
                 * path.  The output string is always returned (never
                 * empty on partial resolution) so the caller can both
                 * surface a clear error and still render best-effort
                 * text.
                 *
                 * @par Example: simple substitution
                 * @code
                 * cfg.set(VideoFormatKey, VideoFormat(VideoFormat::Smpte1080p29_97));
                 * cfg.set(TimecodeKey,    Timecode(Timecode::NDF24, 1, 0, 0, 0));
                 * String s = cfg.format("Format: {VideoFormat:smpte}, TC: {Timecode:smpte}");
                 * // "Format: 1080p29.97, TC: 01:00:00:00"
                 * @endcode
                 *
                 * @par Example: nested resolution via resolver callback
                 * @code
                 * Error err;
                 * String s = childDb.format("{Local} / {Inherited}",
                 *     [&parentDb](const String &key, const String &spec) -> std::optional<String> {
                 *         Error sub;
                 *         String r = parentDb.format("{" + key + (spec.isEmpty() ? "" : ":" + spec) + "}", &sub);
                 *         if(sub.isError()) return std::nullopt;
                 *         return r;
                 *     }, &err);
                 * @endcode
                 *
                 * @tparam Resolver  Callable with signature
                 *                   @c std::optional<String>(const String &key, const String &spec).
                 *                   Pass @c nullptr (or use the no-resolver overload) to
                 *                   skip the fallback path.
                 * @param tmpl       Template string with @c {Key[:spec]} placeholders.
                 * @param resolver   Optional fallback callback for keys not in this database.
                 * @param err        Optional error output, set to @c Error::Ok on full
                 *                   resolution or @c Error::IdNotFound if any key was
                 *                   unresolved.
                 * @return           The substituted output.
                 */
                template <typename Resolver>
                String format(const String &tmpl, Resolver &&resolver, Error *err = nullptr) const {
                        if(err != nullptr) *err = Error::Ok;
                        std::string out;
                        out.reserve(tmpl.byteCount());
                        const char *src = tmpl.cstr();
                        const size_t len = tmpl.byteCount();
                        bool sawUnresolved = false;
                        size_t i = 0;
                        while(i < len) {
                                char c = src[i];
                                if(c == '{') {
                                        if(i + 1 < len && src[i + 1] == '{') {
                                                out.push_back('{');
                                                i += 2;
                                                continue;
                                        }
                                        size_t end = i + 1;
                                        while(end < len && src[end] != '}') ++end;
                                        if(end >= len) {
                                                out.append(src + i, len - i);
                                                break;
                                        }
                                        std::string_view body(src + i + 1, end - (i + 1));
                                        size_t colon = body.find(':');
                                        std::string_view keyView = (colon == std::string_view::npos)
                                                ? body : body.substr(0, colon);
                                        std::string_view specView = (colon == std::string_view::npos)
                                                ? std::string_view() : body.substr(colon + 1);
                                        String keyName(keyView.data(), keyView.size());
                                        String specStr(specView.data(), specView.size());
                                        ID id = ID::find(keyName);
                                        auto it = id.isValid() ? _data.find(id.id()) : _data.end();
                                        if(it != _data.end()) {
                                                String rendered = it->second.format(specStr);
                                                out.append(rendered.cstr(), rendered.byteCount());
                                        } else {
                                                std::optional<String> resolved;
                                                if constexpr (!std::is_same_v<std::decay_t<Resolver>, std::nullptr_t>) {
                                                        resolved = resolver(keyName, specStr);
                                                }
                                                if(resolved.has_value()) {
                                                        const String &r = *resolved;
                                                        out.append(r.cstr(), r.byteCount());
                                                } else {
                                                        sawUnresolved = true;
                                                        out += "[UNKNOWN KEY: ";
                                                        out.append(keyView.data(), keyView.size());
                                                        out += ']';
                                                }
                                        }
                                        i = end + 1;
                                } else if(c == '}') {
                                        if(i + 1 < len && src[i + 1] == '}') {
                                                out.push_back('}');
                                                i += 2;
                                        } else {
                                                out.push_back('}');
                                                ++i;
                                        }
                                } else {
                                        out.push_back(c);
                                        ++i;
                                }
                        }
                        if(sawUnresolved && err != nullptr) *err = Error::IdNotFound;
                        return String(std::move(out));
                }

                /**
                 * @brief Convenience overload of @ref format with no resolver callback.
                 *
                 * Equivalent to calling the resolver overload with @c nullptr.
                 * Missing keys produce @c "[UNKNOWN KEY: <name>]" in the
                 * output and set @p err to @c Error::IdNotFound.
                 *
                 * @param tmpl Template string with @c {Key[:spec]} placeholders.
                 * @param err  Optional error output.
                 * @return     The substituted output.
                 */
                String format(const String &tmpl, Error *err = nullptr) const {
                        return format(tmpl, nullptr, err);
                }

                /**
                 * @brief Returns true if the database contains a value for the given ID.
                 * @param id The entry identifier.
                 * @return True if a value is stored for the ID.
                 */
                bool contains(ID id) const {
                        return _data.contains(id.id());
                }

                /**
                 * @brief Removes the value for the given ID.
                 * @param id The entry identifier.
                 * @return True if the entry was removed, false if it was not present.
                 */
                bool remove(ID id) {
                        return _data.remove(id.id());
                }

                /**
                 * @brief Returns the number of entries in the database.
                 * @return The number of stored key-value pairs.
                 */
                size_t size() const {
                        return _data.size();
                }

                /**
                 * @brief Returns true if the database has no entries.
                 * @return True if empty.
                 */
                bool isEmpty() const {
                        return _data.isEmpty();
                }

                /**
                 * @brief Removes all entries from the database.
                 */
                void clear() {
                        _data.clear();
                }

                /**
                 * @brief Returns a list of all IDs that have values in this database.
                 * @return A List of ID handles for every stored entry.
                 */
                List<ID> ids() const {
                        List<ID> ret;
                        for(auto it = _data.cbegin(); it != _data.cend(); ++it) {
                                ret.pushToBack(ID::fromId(it->first));
                        }
                        return ret;
                }

                /**
                 * @brief Returns the names of entries in the database that have
                 *        no @ref VariantSpec registered anywhere.
                 *
                 * A key is "unknown" when it has no spec in @p extraSpecs
                 * (typically a backend-specific spec map) and no spec in
                 * this database's per-@c Tag global registry.  Useful for
                 * detecting typos in a configuration without knowing any
                 * subsystem-specific key list — the caller decides what
                 * to do with the result (log a warning, reject with an
                 * error, prompt interactively, etc.).
                 *
                 * @param extraSpecs Optional extra spec map to consult
                 *                   before falling back to the global
                 *                   per-@c Tag registry.  Defaults to an
                 *                   empty map (global registry only).
                 * @return A StringList of key names, sorted in
                 *         insertion-agnostic lexicographic order for
                 *         stable logging.  Empty when every stored key
                 *         has a spec.
                 */
                StringList unknownKeys(const SpecMap &extraSpecs = SpecMap()) const {
                        StringList out;
                        for(auto it = _data.cbegin(); it != _data.cend(); ++it) {
                                ID id = ID::fromId(it->first);
                                if(extraSpecs.find(id) != extraSpecs.end()) continue;
                                if(spec(id) != nullptr) continue;
                                out.pushToBack(id.name());
                        }
                        // List<String>::sort() returns a List<String>, not a
                        // StringList — assign-through-base lets us keep
                        // StringList's type identity on the return value.
                        out = out.sort();
                        return out;
                }

                /**
                 * @brief Iterates over all entries in the database.
                 *
                 * @tparam Func Callable with signature void(ID id, const Variant &val).
                 * @param func The function to invoke for each entry.
                 */
                template <typename Func>
                void forEach(Func &&func) const {
                        for(auto it = _data.cbegin(); it != _data.cend(); ++it) {
                                func(ID::fromId(it->first), it->second);
                        }
                }

                /**
                 * @brief Merges entries from another database into this one.
                 *
                 * For each entry in @p other, the value is copied into this
                 * database.  Existing entries with the same ID are overwritten.
                 *
                 * @param other The database to merge from.
                 */
                void merge(const VariantDatabase &other) {
                        other.forEach([this](ID id, const Variant &val) {
                                set(id, val);
                        });
                }

                /**
                 * @brief Creates a new database containing only the specified IDs.
                 *
                 * IDs that are not present in this database are silently skipped.
                 *
                 * @param idList The list of IDs to extract.
                 * @return A new VariantDatabase containing only the matching entries.
                 */
                VariantDatabase extract(const List<ID> &idList) const {
                        VariantDatabase result;
                        for(size_t i = 0; i < idList.size(); ++i) {
                                auto it = _data.find(idList[i].id());
                                if(it != _data.end()) {
                                        result._data.insert(it->first, it->second);
                                }
                        }
                        return result;
                }

                // ============================================================
                // Comparison
                // ============================================================

                /**
                 * @brief Returns true if both databases contain the same entries.
                 *
                 * Two databases are equal if they hold the same set of IDs and each
                 * corresponding value compares equal via Variant::operator==.
                 *
                 * @par Example
                 * @code
                 * VariantDatabase<"MyDB"> a, b;
                 * VariantDatabase<"MyDB">::ID key("width");
                 * a.set(key, 1920);
                 * b.set(key, 1920);
                 * bool same = (a == b);  // true
                 * @endcode
                 */
                bool operator==(const VariantDatabase &other) const { return _data == other._data; }

                /** @brief Returns true if the databases differ. */
                bool operator!=(const VariantDatabase &other) const { return _data != other._data; }

                // ============================================================
                // JSON serialization
                // ============================================================

                /**
                 * @brief Serializes the database to a JsonObject.
                 *
                 * Each entry is stored as a key-value pair where the key is the
                 * string name of the ID and the value is set via
                 * JsonObject::setFromVariant().
                 *
                 * @return A JsonObject containing all entries.
                 */
                JsonObject toJson() const {
                        JsonObject json;
                        for(auto it = _data.cbegin(); it != _data.cend(); ++it) {
                                String name = StringRegistry<Name>::instance().name(it->first);
                                json.setFromVariant(name, it->second);
                        }
                        return json;
                }

                // ============================================================
                // DataStream serialization
                // ============================================================

                /**
                 * @brief Writes the database to a DataStream.
                 *
                 * Format: uint32_t entry count, then for each entry:
                 * String name followed by Variant value.
                 *
                 * @param stream The DataStream to write to.
                 */
                void writeTo(DataStream &stream) const {
                        stream << static_cast<uint32_t>(_data.size());
                        for(auto it = _data.cbegin(); it != _data.cend(); ++it) {
                                String name = StringRegistry<Name>::instance().name(it->first);
                                stream << name << it->second;
                        }
                }

                /**
                 * @brief Reads the database from a DataStream.
                 *
                 * The database is cleared before reading.  Each string name
                 * is registered as an ID (via findOrCreate) so that new IDs
                 * are created as needed.
                 *
                 * @param stream The DataStream to read from.
                 */
                void readFrom(DataStream &stream) {
                        _data.clear();
                        uint32_t count = 0;
                        stream >> count;
                        for(uint32_t i = 0; i < count && stream.status() == DataStream::Ok; ++i) {
                                String name;
                                Variant value;
                                stream >> name >> value;
                                if(stream.status() == DataStream::Ok) {
                                        set(ID(name), std::move(value));
                                }
                        }
                }

                // ============================================================
                // TextStream serialization
                // ============================================================

                /**
                 * @brief Writes the database to a TextStream.
                 *
                 * Each entry is written as a line in the form:
                 * @code
                 * name = value
                 * @endcode
                 * where value is the Variant's string representation.
                 *
                 * @param stream The TextStream to write to.
                 */
                void writeTo(TextStream &stream) const {
                        for(auto it = _data.cbegin(); it != _data.cend(); ++it) {
                                String name = StringRegistry<Name>::instance().name(it->first);
                                stream << name << " = " << it->second << endl;
                        }
                }

        private:
                Map<uint64_t, Variant>  _data;
                SpecValidation          _validation = SpecValidation::Strict;

                /**
                 * @brief Thread-safe spec registry for this Tag's ID namespace.
                 *
                 * Mirrors StringRegistry's singleton pattern: one registry per
                 * Tag, accessed via a function-local static.  Read/write locked
                 * so that static-init-time registrations from multiple TUs are safe.
                 */
                struct SpecRegistry {
                        static SpecRegistry &instance() {
                                static SpecRegistry reg;
                                return reg;
                        }

                        void insert(uint64_t id, const VariantSpec &spec) {
                                ReadWriteLock::WriteLocker lock(_lock);
                                _specs.insert(id, spec);
                        }

                        const VariantSpec *find(uint64_t id) const {
                                ReadWriteLock::ReadLocker lock(_lock);
                                auto it = _specs.find(id);
                                if(it == _specs.end()) return nullptr;
                                return &it->second;
                        }

                        Map<uint64_t, VariantSpec> all() const {
                                ReadWriteLock::ReadLocker lock(_lock);
                                return _specs;
                        }

                private:
                        SpecRegistry() = default;
                        mutable ReadWriteLock           _lock;
                        Map<uint64_t, VariantSpec>      _specs;
                };

                static SpecRegistry &specRegistry() { return SpecRegistry::instance(); }

                /**
                 * @brief Validates @p value against the spec for @p id.
                 *
                 * Called by set() when validation is enabled.  Returns true
                 * if the value should be stored, false if Strict mode rejects it.
                 *
                 * @param id    The entry identifier.
                 * @param value The value to validate.
                 * @param err   Optional out-param: receives the specific
                 *              @ref Error code from the spec when
                 *              validation fails (and Strict mode rejects),
                 *              or @ref Error::Ok otherwise.  In Warn mode
                 *              the validator's error is logged but @p err
                 *              is still cleared to @ref Error::Ok because
                 *              the value will be stored.
                 */
                bool validateOnSet(ID id, const Variant &value, Error *err = nullptr) {
                        if(err) *err = Error::Ok;
                        if(_validation == SpecValidation::None) return true;
                        const VariantSpec *s = specRegistry().find(id.id());
                        if(!s) return true;
                        Error verr;
                        if(!s->validate(value, &verr)) {
                                if(_validation == SpecValidation::Warn) {
                                        promekiWarn("VariantDatabase: value for '%s' fails spec (%s)",
                                                    id.name().cstr(), verr.name().cstr());
                                        return true;
                                }
                                if(err) *err = verr;
                                return false; // Strict: reject
                        }
                        return true;
                }
};

/**
 * @brief Writes a VariantDatabase to a DataStream.
 * @param stream The DataStream to write to.
 * @param db     The VariantDatabase to serialize.
 * @return A reference to the stream.
 */
template <CompiledString Name>
DataStream &operator<<(DataStream &stream, const VariantDatabase<Name> &db) {
        db.writeTo(stream);
        return stream;
}

/**
 * @brief Reads a VariantDatabase from a DataStream.
 * @param stream The DataStream to read from.
 * @param db     The VariantDatabase to populate (cleared first).
 * @return A reference to the stream.
 */
template <CompiledString Name>
DataStream &operator>>(DataStream &stream, VariantDatabase<Name> &db) {
        db.readFrom(stream);
        return stream;
}

/**
 * @brief Writes a VariantDatabase to a TextStream.
 * @param stream The TextStream to write to.
 * @param db     The VariantDatabase to serialize.
 * @return A reference to the stream.
 */
template <CompiledString Name>
TextStream &operator<<(TextStream &stream, const VariantDatabase<Name> &db) {
        db.writeTo(stream);
        return stream;
}

PROMEKI_NAMESPACE_END

/**
 * @brief Declares a well-known VariantDatabase key as a compile-time constant.
 * @ingroup util
 *
 * Expands, inside a class derived from @ref VariantDatabase, to two
 * declarations that together give the key both a `constexpr` ID and a
 * registered @ref VariantSpec :
 *
 * 1. `static constexpr ID <Name>` initialized from the pure FNV-1a hash of
 *    `"<Name>"` via @ref StringRegistry::Item::literal — usable in `switch`
 *    labels, `static_assert`, template parameters, and other constant
 *    expression contexts.
 * 2. A sibling `static inline const ID` whose initializer calls
 *    @ref VariantDatabase::declareID with the same name to register the
 *    spec (and the name, for reverse lookup) at static-init time.  The
 *    strict-hash path guarantees the two IDs agree; a hash collision with
 *    another well-known key will abort the process before `main()` runs.
 *
 * The name appears once in the macro call, so there is no risk of the
 * literal hash and the registered name drifting apart.
 *
 * @par Example
 * @code
 * class Metadata : public VariantDatabase<"Metadata"> {
 *     public:
 *         PROMEKI_DECLARE_ID(Title,
 *             VariantSpec().setType(Variant::TypeString)
 *                          .setDefault(String())
 *                          .setDescription("Title of the media."));
 *
 *         // Title.id() is now a constant expression:
 *         static_assert(Title.id() == ID::literal("Title").id());
 * };
 * @endcode
 */
#define PROMEKI_DECLARE_ID(Name, ...)                                           \
        static constexpr ID Name = ID::literal(#Name);                          \
        [[maybe_unused]] static inline const ID                                 \
                PROMEKI_CONCAT(_promeki_spec_reg_, Name) =                      \
                        declareID(#Name, __VA_ARGS__)
