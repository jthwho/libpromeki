/**
 * @file      variantquery.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <optional>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/result.h>
#include <promeki/uniqueptr.h>
#include <promeki/variant.h>
#include <promeki/variantspec.h>
#include <promeki/variantlookup.h>

PROMEKI_NAMESPACE_BEGIN

class Frame;

namespace detail {

        /**
 * @brief Opaque AST root for a parsed @ref VariantQuery.
 *
 * The lexer, parser and concrete node subclasses live entirely in
 * @c variantquery.cpp.  The forward declaration here is enough to hold
 * a node in a @ref UniquePtr because every @ref VariantQuery special
 * member is explicit-instantiated in the same translation unit that
 * defines @ref VariantQueryNode, so the implicit destructor never
 * needs to see a complete type at a call site.
 */
        class VariantQueryNode;

        /** @brief Unique-ownership pointer to a @ref VariantQueryNode. */
        using VariantQueryNodeUPtr = UniquePtr<VariantQueryNode>;

        /**
 * @brief Evaluation inputs injected into the AST by @ref VariantQuery::match.
 *
 * The AST itself is type-erased — every concrete node leans on
 * @c std::function callbacks supplied through this context rather
 * than referencing @c T directly — so the same parse result can be
 * reused against any @c T for which a @c VariantLookup<T> has been
 * registered.
 */
        struct VariantQueryContext {
                        /**
         * @brief Resolves a full dotted key against the target instance.
         *
         * Must never be null.  @ref VariantQuery::match wires this to
         * @c VariantLookup<T>::resolve(instance, key).
         */
                        std::function<std::optional<Variant>(const String &)> resolve;

                        /**
         * @brief Returns the declared @ref VariantSpec for a key, or nullptr.
         *
         * Used by the AST to coerce string literals on one side of a
         * comparison to the spec-declared type of the key on the
         * other side.  @ref VariantQuery::match wires this to
         * @c VariantLookup<T>::specFor(key).  May be null when the
         * caller wants to skip spec-based coercion entirely.
         */
                        std::function<const VariantSpec *(const String &)> specFor;
        };

        /**
 * @brief Parses @p expr into an AST shared by every @ref VariantQuery instantiation.
 *
 * Defined in @c variantquery.cpp.  The parser is generic over the
 * target type because the AST references keys as strings; typing
 * happens at @ref VariantQuery::match via the
 * @ref VariantQueryContext injected from the enclosing template.
 *
 * @param expr        The query source.
 * @param errorDetail Populated with a human-readable diagnostic
 *                    (column, token, reason) when parsing fails.
 * @return A UniquePtr to the parsed AST, or null on parse failure.
 */
        VariantQueryNodeUPtr parseVariantQueryExpr(const String &expr, String &errorDetail);

        /**
 * @brief Evaluates a parsed AST against @p ctx.
 *
 * Defined in @c variantquery.cpp alongside the concrete node
 * subclasses so the header does not need to expose them.
 */
        bool evalVariantQuery(const VariantQueryNode *root, const VariantQueryContext &ctx);

} // namespace detail

/**
 * @brief Compiled predicate that matches any VariantLookup-registered type against an expression.
 * @ingroup util
 *
 * Parses an expression once into an AST; evaluates it against any
 * @c T for which @ref VariantLookup "VariantLookup<T>" has been
 * registered via @ref PROMEKI_LOOKUP_REGISTER.  The expression
 * vocabulary mirrors @c VariantLookup<T>::resolve / @c ::format, so
 * anything that can be rendered into a burn template or read by the
 * key resolver can also drive a query.
 *
 * @par Grammar
 * @code
 * expr         := or_expr
 * or_expr      := and_expr ( '||' and_expr )*
 * and_expr     := not_expr ( '&&' not_expr )*
 * not_expr     := '!' not_expr | primary
 * primary      := '(' expr ')' | has_form | comparison
 * has_form     := 'has' '(' key ')'
 * comparison   := operand ( relop operand )?
 * relop        := '==' | '!=' | '<' | '<=' | '>' | '>=' | '~' | '~~'
 * operand      := literal | key
 * literal      := integer | float | string | regex | bool
 * integer      := [-]? [0-9]+
 * float        := [-]? [0-9]+ '.' [0-9]+ (exponent allowed)
 * string       := '"' ... '"'          (backslash escapes: \\ \" \n \t)
 * regex        := '/' ... '/'          (ECMAScript regex; same-char escape)
 * bool         := 'true' | 'false'
 * key          := ident ( '[' N ']' )? ( '.' key )?
 * @endcode
 *
 * @par Key references
 * All the forms accepted by @c VariantLookup<T>::resolve are valid
 * keys.  For @ref Frame:
 *  - Metadata key (behind @c "Meta." prefix): @c "Meta.Title"
 *  - Frame scalar: @c "VideoCount"
 *  - Subscripted video / audio: @c "Video[0].Width", @c "Audio[0].Meta.Album"
 *  - Subscripted scalar: @c "VideoFormat[1]"
 *
 * @par Operators
 * - @c == / @c != — equality via @ref Variant::operator==.  When one
 *   side is a bare String literal and the other is a key, the
 *   literal is coerced to the key's declared type by consulting
 *   @c VariantLookup<T>::specFor so queries like
 *   @c Meta.Timecode == "01:00:00:00" and
 *   @c Meta.OutputPixelFormat == "RGBA8_sRGB" work out of the box.
 *   When no spec is declared, @ref Variant "Variant's" own cross-type
 *   conversion still handles common cases (e.g.
 *   @c Video[0].PixelFormat == "RGBA8_sRGB").
 * - @c < / @c <= / @c > / @c >= — ordering.  Tries numeric (both sides
 *   convertible to @c double), else @ref Timecode (when one side is
 *   a Timecode), else String lexicographic.  Returns false when
 *   neither side is ordered comparable.
 * - @c ~  — regex match.  RHS must be a regex literal @c /.../.
 * - @c ~~ — substring match.  RHS must be a string literal or a key
 *   whose value renders to a string.
 * - @c has(Key) — true when the key resolves to a value on the
 *   instance.
 * - @c && / @c || / @c ! — short-circuit logical ops.
 *
 * @par Examples
 * @code
 * auto [q, err] = VariantQuery<Frame>::parse("Meta.Timecode >= \"01:00:00:00\"");
 * if(err.isError()) throw ...;
 * for(const Frame::Ptr &f : frames) {
 *     if(q.match(*f)) std::cout << "match\n";
 * }
 *
 * VariantQuery<VideoPayload>::parse("Width > 1920 && PixelFormat == \"RGBA8_sRGB\"");
 * VariantQuery<AudioPayload>::parse("Channels == 2 && SampleRate >= 48000");
 * @endcode
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used concurrently.
 * After @c parse() returns, the resulting compiled query is effectively
 * immutable — concurrent calls to @c match() / @c isValid() / @c errorDetail()
 * on a single parsed query are safe.  Calling @c parse() on an instance
 * while another thread is reading or matching against it must be externally
 * synchronized.
 *
 * @tparam T The target type.  Must have @c VariantLookup<T>
 *           registrations for the keys the query references.
 *           Explicit template instantiations are provided for
 *           @ref Frame, @ref Image and @ref Audio; other types can
 *           be instantiated by adding a
 *           @c template class VariantQuery<MyType>; line in the
 *           translation unit that owns @c MyType 's registrations.
 */
template <typename T> class VariantQuery {
        public:
                /**
                 * @brief Parses an expression into a compiled query.
                 *
                 * On success returns the query and @ref Error::Ok.  On
                 * failure the returned query is empty
                 * (@ref isValid returns false) and @ref errorDetail
                 * carries a human-readable diagnostic (column, token,
                 * reason).
                 */
                static Result<VariantQuery<T>> parse(const String &expr);

                /** @brief Constructs an empty (invalid) query. */
                VariantQuery();
                ~VariantQuery();

                VariantQuery(const VariantQuery &) = delete;
                VariantQuery &operator=(const VariantQuery &) = delete;
                VariantQuery(VariantQuery &&) noexcept;
                VariantQuery &operator=(VariantQuery &&) noexcept;

                /** @brief True when this query parsed successfully. */
                bool isValid() const;

                /**
                 * @brief Evaluates the query against @p instance.
                 *
                 * Invalid queries always return false.  The
                 * @ref VariantQueryContext is built fresh per call
                 * and captures @p instance by reference, so the AST
                 * never retains a pointer past the return.
                 */
                bool match(const T &instance) const;

                /** @brief The original expression source, as parsed. */
                const String &source() const;

                /**
                 * @brief Human-readable parse-error detail.
                 *
                 * Empty on success.  On failure, contains a message
                 * like @c "expected ')' at col 23, got '&&'".
                 */
                const String &errorDetail() const;

        private:
                VariantQuery(String source, detail::VariantQueryNodeUPtr root);

                String                       _source;
                String                       _errorDetail;
                detail::VariantQueryNodeUPtr _root;
};

extern template class VariantQuery<Frame>;

PROMEKI_NAMESPACE_END
