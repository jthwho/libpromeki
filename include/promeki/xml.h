/**
 * @file      xml.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <functional>
#include <utility>
#include <promeki/function.h>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/error.h>
#include <promeki/sharedptr.h>
#include <promeki/datastream.h>
#include <pugixml.hpp>

PROMEKI_NAMESPACE_BEGIN

class XmlName;
class XmlAttribute;
class XmlElement;
class XmlDocument;
class XmlNode;
class XmlParseError;

/**
 * @brief Namespace-qualified XML name: @c (uri, prefix, localName).
 * @ingroup util
 *
 * Plain value type used by every namespace-aware accessor on
 * @ref XmlElement.  The @c prefix field is a serialization hint
 * only — equality is determined by @c (uri, local), as required by
 * the XML Namespaces 1.0 specification.  When mutating an element
 * with a namespaced setter, @c prefix biases the prefix chosen for
 * any newly-injected @c xmlns binding; if the URI is already bound
 * to a different prefix in scope the existing binding wins.
 *
 * @par Two text forms
 * - @ref qualified renders as @c "prefix:local" (or just @c "local"
 *   when the prefix is empty).  Suitable for raw @c name() return
 *   values matching what pugixml stores in the tree.
 * - @ref clark renders as @c "{uri}local" (Clark notation).
 *   Suitable for unambiguous logging and equality dumps.
 *
 * @par Constructors
 * - @c XmlName(local) — element/attribute in no namespace.
 * - @c XmlName(uri, local) — namespaced; prefix to be auto-chosen.
 * - @c XmlName(uri, prefix, local) — namespaced with explicit prefix
 *   hint.
 */
class XmlName {
        public:
                /** @brief Constructs an empty (invalid) name. */
                XmlName() = default;

                /** @brief Constructs an unqualified name in no namespace. */
                explicit XmlName(const String &local) : _local(local) {}

                /** @brief Constructs a namespace-qualified name with no prefix hint. */
                XmlName(const String &uri, const String &local) : _uri(uri), _local(local) {}

                /** @brief Constructs a namespace-qualified name with a prefix hint. */
                XmlName(const String &uri, const String &prefix, const String &local)
                        : _uri(uri), _prefix(prefix), _local(local) {}

                /**
                 * @brief Parses Clark notation @c "{uri}local" into an XmlName.
                 *
                 * Accepts both @c "{uri}local" and the un-bracketed bare
                 * @c "local" forms.  The prefix is left empty.
                 */
                static XmlName parseClark(const String &clark);

                /** @brief True iff the name has a non-empty local part. */
                bool isValid() const { return !_local.isEmpty(); }

                /** @brief The namespace URI, or empty if none. */
                const String &uri() const { return _uri; }

                /** @brief The prefix hint, or empty for default-namespace / no-namespace. */
                const String &prefix() const { return _prefix; }

                /** @brief The local part of the name (the part after any @c "prefix:"). */
                const String &local() const { return _local; }

                /** @brief True iff @ref uri is non-empty. */
                bool hasNamespace() const { return !_uri.isEmpty(); }

                /** @brief Returns @c "prefix:local" or just @c "local" when prefix is empty. */
                String qualified() const;

                /** @brief Returns @c "{uri}local" or just @c "local" when uri is empty. */
                String clark() const;

                /** @brief Default text form: Clark notation. */
                String toString() const { return clark(); }

                /** @brief Equality compares @c (uri, local). The prefix hint is not authoritative. */
                bool operator==(const XmlName &other) const {
                        return _uri == other._uri && _local == other._local;
                }

                /** @brief Inequality. */
                bool operator!=(const XmlName &other) const { return !(*this == other); }

        private:
                String _uri;
                String _prefix;
                String _local;
};

/**
 * @brief Rich result of an XML parse attempt.
 * @ingroup util
 *
 * Captures pugixml's @c xml_parse_result plus a 1-based
 * line/column derived from the byte offset.  Default-constructed
 * (or after a successful parse) it represents "no error" — every
 * accessor returns the empty / zero value and @ref ok is true.
 *
 * @par Use as an output parameter
 * @code
 * XmlParseError err;
 * XmlDocument  doc = XmlDocument::parse(text, &err);
 * if (!err) {
 *     log("parse failed: %s", err.toString().cstr());
 *     // Or grab the raw fields:
 *     log("at byte %zu (line %d, col %d): %s",
 *         err.offset(), err.line(), err.column(), err.message().cstr());
 * }
 * @endcode
 *
 * Callers that only care about success/failure can simply ignore
 * the optional output parameter and check @ref XmlDocument::isValid
 * on the returned handle — the API matches @ref JsonObject::parse
 * shape for that case.
 */
class XmlParseError {
        public:
                /** @brief Default-constructed: success, no message. */
                XmlParseError() = default;

                /** @brief True iff the parse succeeded. */
                bool ok() const { return _status == pugi::status_ok; }

                /** @brief Inverse of @ref ok. */
                bool isError() const { return !ok(); }

                /** @brief Implicit-bool: true on success. */
                explicit operator bool() const { return ok(); }

                /** @brief The raw pugi parse status. */
                pugi::xml_parse_status pugiStatus() const { return _status; }

                /** @brief Pugi's human-readable description (empty on success). */
                const String &message() const { return _message; }

                /** @brief Byte offset in the source where parsing failed (0 on success). */
                size_t offset() const { return _offset; }

                /** @brief 1-based line number derived from @ref offset (0 on success). */
                int line() const { return _line; }

                /** @brief 1-based column number derived from @ref offset (0 on success). */
                int column() const { return _column; }

                /**
                 * @brief Maps the pugi status onto an @ref Error code.
                 *
                 * Most pugi errors fold into @c Error::ParseFailed; the
                 * resource-shaped failures (@c status_out_of_memory,
                 * @c status_io_error, @c status_file_not_found) map to
                 * @c Error::NoMem / @c Error::IOError / @c Error::NotExist
                 * respectively.  Returns @c Error::Ok on success.
                 */
                Error toError() const;

                /** @brief Renders @c "line N, col M: message" or empty on success. */
                String toString() const;

                /** @brief Equality compares status, offset, and message. */
                bool operator==(const XmlParseError &other) const {
                        return _status == other._status && _offset == other._offset &&
                               _message == other._message;
                }

                /** @brief Inequality. */
                bool operator!=(const XmlParseError &other) const { return !(*this == other); }

        private:
                friend class XmlDocument;
                friend class XmlElement;

                /**
                 * @brief Internal: populate from a pugi parse result against the source text.
                 *
                 * Computes the 1-based line / column by scanning @p source
                 * up to @c result.offset for newline characters.  Safe on
                 * the success path (status_ok with offset=0).
                 */
                void setFromPugi(const pugi::xml_parse_result &result, const String &source);

                pugi::xml_parse_status _status  = pugi::status_ok;
                String                 _message;
                size_t                 _offset = 0;
                int                    _line   = 0;
                int                    _column = 0;
};

/**
 * @brief Internal CoW storage shared by every XML handle.
 *
 * Wraps a single @c pugi::xml_document.  The pugi document is what
 * actually owns the parsed node memory; every public XML handle in
 * this header (XmlDocument, XmlElement, XmlNode) is a value type that
 * keeps a @c SharedPtr<XmlData> and presents a self-contained view on
 * the document's contents:
 *
 * - XmlDocument's underlying document is a full XML document
 *   (declaration, doctype, root element, top-level comments / PIs).
 * - XmlElement's underlying document holds exactly one element at
 *   the document root — i.e. an "element subtree" packaged as its
 *   own pugi document.  This mirrors how nlohmann::json subtrees are
 *   independent JSON values inside @c JsonObject / @c JsonArray.
 *
 * Pugi documents are non-copyable, so this struct provides a copy
 * constructor that performs a deep clone via
 * @c pugi::xml_document::reset.
 *
 * The struct is exposed in the public header purely so the handle
 * types can hold @c SharedPtr<XmlData> directly.  All data members
 * are public for the handles' implementations; consumers should not
 * touch @c doc directly.
 */
struct XmlData {
                PROMEKI_SHARED_FINAL(XmlData)
                pugi::xml_document doc;

                XmlData() = default;
                XmlData(const XmlData &other);
                XmlData &operator=(const XmlData &other);
                XmlData(XmlData &&) = delete;
                XmlData &operator=(XmlData &&) = delete;
};

/**
 * @brief Lightweight name/value pair representing one XML attribute.
 * @ingroup util
 *
 * Plain value type — does @b not use the SharedPtr/CoW machinery
 * because attributes are tiny (a few strings) and aren't shared the
 * way trees are.  Lists of attributes are returned by value from
 * @ref XmlElement::attributes; each entry's @ref qname is resolved
 * against the owning element's in-scope @c xmlns bindings at that
 * call.
 */
class XmlAttribute {
        public:
                /** @brief Default-constructed (empty name, empty value). */
                XmlAttribute() = default;

                /** @brief Constructs an attribute with a raw qualified name. */
                XmlAttribute(String rawName, String value)
                        : _name(std::move(rawName)), _value(std::move(value)) {}

                /** @brief Constructs a namespace-qualified attribute. */
                XmlAttribute(XmlName qname, String value);

                /** @brief Internal: build with both raw name and resolved @c XmlName. */
                XmlAttribute(String rawName, XmlName qname, String value)
                        : _name(std::move(rawName)), _qname(std::move(qname)), _value(std::move(value)) {}

                /** @brief Returns the raw qualified attribute name as it appears in source XML. */
                const String &name() const { return _name; }

                /** @brief Returns the namespace-qualified name (resolved against in-scope @c xmlns). */
                const XmlName &qname() const { return _qname; }

                /** @brief Returns the attribute value. */
                const String &value() const { return _value; }

                /** @brief Replaces the raw attribute name (does not affect @c qname). */
                void setName(const String &name) { _name = name; }

                /** @brief Replaces the namespace-qualified name. */
                void setQName(const XmlName &qname) { _qname = qname; }

                /** @brief Replaces the attribute value. */
                void setValue(const String &value) { _value = value; }

                /** @brief Returns true if the attribute has a non-empty name. */
                bool isValid() const { return !_name.isEmpty(); }

                /** @brief Equality compares both name and value. */
                bool operator==(const XmlAttribute &other) const {
                        return _name == other._name && _value == other._value;
                }

                /** @brief Inequality. */
                bool operator!=(const XmlAttribute &other) const { return !(*this == other); }

        private:
                String  _name;
                XmlName _qname;
                String  _value;
};

/**
 * @brief Value-type handle to a single XML element subtree.
 * @ingroup util
 *
 * @par Storage and copy semantics
 * XmlElement is a value type with internal copy-on-write sharing.
 * Each XmlElement wraps a @c SharedPtr<XmlData> whose underlying
 * pugi document holds exactly one element at the document root.
 * Copying an XmlElement is O(1) and shares storage; mutating
 * accessors detach a private copy on first write via
 * @c _d.modify().  Subtree accessors (@ref child, @ref elements,
 * @ref attributes) return values that are independent of the parent
 * element — pulling a child element out of a parent costs one deep
 * pugi clone of that subtree.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently; concurrent access to a single instance must be
 * externally synchronized.  Static @c parse helpers are reentrant.
 *
 * @par Example
 * @code
 * XmlElement clip("Clip");
 * clip.setAttribute("id", "001");
 * clip.appendElement("Width").setText("1920");
 * clip.appendElement("Height").setText("1080");
 * String xml = clip.toString(2);   // pretty-printed
 *
 * Error err;
 * auto parsed = XmlElement::parse(xml, &err);
 * String w = parsed.child("Width").text();
 * @endcode
 */
class XmlElement {
        public:
                /**
                 * @brief Parses an XML element fragment from a string.
                 * @param str The XML source containing exactly one root element.
                 * @param err Optional rich error output; carries the pugi
                 *            status, byte offset, line/column, and a
                 *            human-readable description on failure.
                 * @return The parsed XmlElement, or an invalid element on failure.
                 *
                 * Callers that only need a yes/no result can omit @p err
                 * and check @ref isValid on the returned handle.
                 */
                static XmlElement parse(const String &str, XmlParseError *err = nullptr);

                /** @brief Constructs an invalid (empty) XmlElement. */
                XmlElement() = default;

                /** @brief Constructs an element with the given name and no children. */
                explicit XmlElement(const String &name);

                /** @brief True when the element handle wraps a real element node. */
                bool isValid() const;

                /** @brief Returns the element's tag name (raw qualified form, e.g. @c "foo:bar"). */
                String name() const;

                /**
                 * @brief Returns the element's namespace-qualified name.
                 *
                 * The prefix is looked up against the element's own
                 * @c xmlns / @c xmlns:foo declarations and any inherited
                 * from ancestors that the deep-copy preserved.
                 */
                XmlName qname() const;

                /** @brief Replaces the element's tag name (raw qualified form). */
                void setName(const String &name);

                /**
                 * @brief Returns the @c xmlns bindings in scope on this element.
                 *
                 * Keys are prefixes (empty key = default namespace);
                 * values are URIs.  Includes self-declared bindings and
                 * any inherited from ancestors of the element when it
                 * was extracted from its parent.
                 */
                Map<String, String> namespaces() const;

                // --- Attributes ---

                /** @brief Returns true if the element has an attribute with @p name (raw qualified form). */
                bool hasAttribute(const String &name) const;

                /** @brief Returns true if the element has an attribute matching @p qname. */
                bool hasAttribute(const XmlName &qname) const;

                /**
                 * @brief Returns the attribute value for @p name (raw qualified form).
                 * @param name The raw qualified attribute name (e.g. @c "foo:bar").
                 * @param err  Optional error output; set to Error::Invalid when missing.
                 * @return The attribute value, or an empty string on failure.
                 */
                String attribute(const String &name, Error *err = nullptr) const;

                /**
                 * @brief Returns the attribute value for the given namespace-qualified name.
                 *
                 * Match is on @c (uri, local) — any prefix bound to the
                 * URI in scope is accepted.  Unprefixed attributes have
                 * an empty URI per the XML namespaces specification, so
                 * @c XmlName("local") matches an unprefixed attribute
                 * even when a default @c xmlns is declared.
                 */
                String attribute(const XmlName &qname, Error *err = nullptr) const;

                /** @brief Sets / replaces the attribute @p name (raw qualified form) to @p value. */
                void setAttribute(const String &name, const String &value);

                /**
                 * @brief Sets / replaces a namespace-qualified attribute.
                 *
                 * If @c qname.uri is non-empty, the implementation finds
                 * an in-scope prefix bound to that URI; if none, it
                 * injects a new @c xmlns:prefix attribute on this
                 * element using @c qname.prefix as a hint or
                 * @c xmlns:auto<N> when the hint is empty or already in
                 * use for a different URI.  Unprefixed attributes
                 * (@c qname.uri empty) are written as plain attributes.
                 */
                void setAttribute(const XmlName &qname, const String &value);

                /**
                 * @brief Removes the attribute with raw name @p name if present.
                 * @return true if the attribute was present and removed.
                 */
                bool removeAttribute(const String &name);

                /**
                 * @brief Removes the attribute matching the namespace-qualified @p qname.
                 * @return true if the attribute was present and removed.
                 */
                bool removeAttribute(const XmlName &qname);

                /** @brief Returns every attribute on the element in document order. */
                List<XmlAttribute> attributes() const;

                // --- Element children ---

                /** @brief True if the element has at least one element child with raw name @p name. */
                bool hasChild(const String &name) const;

                /** @brief True if the element has at least one element child matching @p qname. */
                bool hasChild(const XmlName &qname) const;

                /**
                 * @brief Returns the first element child with raw name @p name, or an invalid element if none.
                 */
                XmlElement child(const String &name) const;

                /**
                 * @brief Returns the first element child matching the namespace-qualified @p qname.
                 *
                 * Match is on @c (uri, local).  Returns an invalid element
                 * if none.
                 */
                XmlElement child(const XmlName &qname) const;

                /** @brief Returns every element child in document order. */
                List<XmlElement> elements() const;

                /** @brief Returns every element child whose raw name matches @p name in document order. */
                List<XmlElement> elementsNamed(const String &name) const;

                /** @brief Returns every element child whose namespace-qualified name matches @p qname. */
                List<XmlElement> elementsNamed(const XmlName &qname) const;

                // --- XPath ---

                /**
                 * @brief Returns the first element matching XPath @p query, evaluated relative to this element.
                 * @param query  XPath 1.0 expression.
                 * @param err    Optional error output; @c Error::ParseFailed on a malformed query.
                 * @return The first matching element (deep-copied with inherited @c xmlns), or invalid if none.
                 *
                 * Non-element results (attribute / numeric / boolean /
                 * string) are filtered out — use
                 * @ref selectFirstAttribute for attribute-yielding
                 * queries.
                 */
                XmlElement selectFirst(const String &query, Error *err = nullptr) const;

                /**
                 * @brief Returns every element matching XPath @p query, in document order.
                 * @param query  XPath 1.0 expression.
                 * @param err    Optional error output; @c Error::ParseFailed on a malformed query.
                 * @return List of matching elements, deep-copied. Empty when no match.
                 */
                List<XmlElement> selectAll(const String &query, Error *err = nullptr) const;

                /**
                 * @brief Returns the first attribute matching XPath @p query.
                 *
                 * Useful for queries like @c "foo/\@bar" that select
                 * attributes.  The returned @ref XmlAttribute's @c qname
                 * is resolved against the owning element's in-scope
                 * @c xmlns bindings.
                 */
                XmlAttribute selectFirstAttribute(const String &query, Error *err = nullptr) const;

                /** @brief Returns every attribute matching XPath @p query, in document order. */
                List<XmlAttribute> selectAllAttributes(const String &query, Error *err = nullptr) const;

                // --- Path navigation (lightweight) ---

                /**
                 * @brief Returns the first element reachable by the @c '/'-delimited @p path.
                 *
                 * Ergonomic shortcut for simple direct-child chains —
                 * for anything beyond that prefer @ref selectFirst.  An
                 * empty segment at the start anchors to this element.
                 * Returns invalid if any segment is missing.
                 *
                 * @code
                 * XmlElement child = root.elementByPath("Header/Identity/UUID");
                 * @endcode
                 */
                XmlElement elementByPath(const String &path) const;

                /** @brief Returns every direct child node (element / text / cdata / comment / PI). */
                List<XmlNode> children() const;

                // --- Text content ---

                /**
                 * @brief Returns the concatenated text of all direct text and CDATA children.
                 *
                 * Mixed-content elements concatenate every direct text and
                 * CDATA section in document order; elements nested inside
                 * are skipped.  Whitespace is preserved as-is.
                 */
                String text() const;

                /**
                 * @brief Replaces every direct text / CDATA child with a single text node containing @p text.
                 *
                 * Element / comment / PI children are left in place.
                 */
                void setText(const String &text);

                // --- Mutation: append children ---

                /**
                 * @brief Creates an empty child element with raw name @p name and appends it.
                 *
                 * To populate the new child with attributes, text, or
                 * children, build it as a separate @ref XmlElement and
                 * pass it to @ref appendChild — the same pattern used by
                 * @ref JsonObject::set / @ref JsonArray::add for nested
                 * subtrees.  Or use the @c (name, text) overload below
                 * for the common single-text-child case.
                 */
                void appendElement(const String &name);

                /** @brief Convenience: appends @c \<name\>text\</name\> as a single text-only child. */
                void appendElement(const String &name, const String &text);

                /**
                 * @brief Creates an empty child element with namespace-qualified name @p qname and appends it.
                 *
                 * If @c qname.uri is non-empty and not already in scope
                 * for this element, an @c xmlns / @c xmlns:prefix
                 * declaration is auto-injected on the new child.  See
                 * @ref setAttribute(const XmlName &, const String &) for
                 * the prefix-selection rules.
                 */
                void appendElement(const XmlName &qname);

                /** @brief Convenience: appends a namespaced single-text-child element. */
                void appendElement(const XmlName &qname, const String &text);

                /** @brief Appends a text node containing @p text. */
                void appendText(const String &text);

                /** @brief Appends a CDATA section containing @p text. */
                void appendCData(const String &text);

                /** @brief Appends a comment node. */
                void appendComment(const String &text);

                /** @brief Appends a processing instruction. */
                void appendProcessingInstruction(const String &target, const String &data);

                /** @brief Appends a deep copy of @p child as the last element child. */
                void appendChild(const XmlElement &child);

                /** @brief Appends @p node as the last child (deep copy if Element). */
                void appendChild(const XmlNode &node);

                /**
                 * @brief Appends a deep copy of @p doc's root element as the last child.
                 *
                 * Document-level metadata (declaration, doctype, top-level
                 * comments / PIs) is dropped — only the root element is
                 * grafted in.  Equivalent to @c appendChild(doc.root()).
                 * Convenient when a fragment was prepared as its own
                 * document and now needs to be merged into a parent tree.
                 */
                void appendChild(const XmlDocument &doc);

                /**
                 * @brief Returns this element wrapped as the root of a fresh @ref XmlDocument.
                 *
                 * Equivalent to @c XmlDocument(*this).  No declaration or
                 * doctype is added; callers wanting one should follow up
                 * with @ref XmlDocument::setDeclaration.  Reads naturally
                 * in chains:
                 * @code
                 * XmlDocument standalone = parent.child("Foo").toDocument();
                 * @endcode
                 */
                XmlDocument toDocument() const;

                // --- Prepend / insert at position ---

                /** @brief Prepends an empty child element with raw name @p name. */
                void prependElement(const String &name);

                /** @brief Convenience: prepends @c \<name\>text\</name\>. */
                void prependElement(const String &name, const String &text);

                /** @brief Prepends a deep copy of @p child as the first child. */
                void prependChild(const XmlElement &child);

                /** @brief Prepends @p node as the first child (deep copy if Element). */
                void prependChild(const XmlNode &node);

                /** @brief Prepends a deep copy of @p doc's root as the first child. */
                void prependChild(const XmlDocument &doc);

                /**
                 * @brief Inserts @p child as the @p index'th child.
                 *
                 * @p index is clamped to @c [0, childCount()] — passing
                 * an out-of-range index is equivalent to
                 * @ref appendChild (or @ref prependChild for 0).
                 * Operates on the full child list (including text /
                 * comment / PI nodes), not just elements.
                 */
                void insertChildAt(size_t index, const XmlElement &child);

                /// @copydoc insertChildAt(size_t, const XmlElement &)
                void insertChildAt(size_t index, const XmlNode &node);

                // --- Targeted removal ---

                /**
                 * @brief Removes the first element child with raw name @p name.
                 * @return true if a matching child was found and removed.
                 */
                bool removeChild(const String &name);

                /**
                 * @brief Removes the first element child whose qname matches @p qname.
                 * @return true if a matching child was found and removed.
                 */
                bool removeChild(const XmlName &qname);

                /**
                 * @brief Removes the @p index'th child (across all node types).
                 * @return true if @p index was in range.
                 */
                bool removeChildAt(size_t index);

                /**
                 * @brief Removes every element child with raw name @p name.
                 * @return The number of removed children.
                 */
                size_t removeAllNamed(const String &name);

                /**
                 * @brief Removes every element child whose qname matches @p qname.
                 * @return The number of removed children.
                 */
                size_t removeAllNamed(const XmlName &qname);

                /** @brief Returns the total number of direct child nodes (any type). */
                size_t childCount() const;

                // --- Attribute order primitives ---

                /** @brief Prepends a raw-named attribute. */
                void prependAttribute(const String &name, const String &value);

                /** @brief Prepends a namespace-qualified attribute. */
                void prependAttribute(const XmlName &qname, const String &value);

                /**
                 * @brief Inserts an attribute at the @p index'th position.
                 *
                 * @p index is clamped to @c [0, attributeCount()].
                 */
                void insertAttributeAt(size_t index, const String &name, const String &value);

                /// @copydoc insertAttributeAt(size_t, const String &, const String &)
                void insertAttributeAt(size_t index, const XmlName &qname, const String &value);

                /** @brief Returns the number of attributes on this element. */
                size_t attributeCount() const;

                /** @brief Removes every attribute and child node. */
                void clear();

                /**
                 * @brief Serializes the element to an XML string.
                 * @param indent Spaces per indentation level. Zero produces compact single-line output.
                 */
                String toString(unsigned int indent = 0) const;

                /** @brief Iterates over every element child in document order. */
                void forEachElement(Function<void(const XmlElement &)> func) const;

                /** @brief Returns true when both elements serialize identically. */
                bool operator==(const XmlElement &other) const;

                /** @brief Inequality. */
                bool operator!=(const XmlElement &other) const { return !(*this == other); }

        private:
                friend class XmlDocument;
                friend class XmlNode;

                SharedPtr<XmlData> _d = SharedPtr<XmlData>::create();

                explicit XmlElement(const SharedPtr<XmlData> &d) : _d(d) {}

                /** @brief Returns the document's root element node, or an empty pugi node if none. */
                pugi::xml_node rootNode() const;

                /** @brief Replaces the document's contents with a deep copy of @p node. */
                void resetTo(const pugi::xml_node &node);
};

/**
 * @brief Value-type handle to a complete XML document.
 * @ingroup util
 *
 * Wraps a full pugi document — optional XML declaration, optional
 * DOCTYPE, exactly one root element (when valid), plus any
 * document-level comments or processing instructions.
 *
 * @par Storage and copy semantics
 * Identical to @ref XmlElement — copy is O(1) via internal
 * @c SharedPtr<XmlData> sharing; first mutation detaches a private
 * copy.  Subtree accessors (@ref root) deep-copy out into
 * independent values.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently; concurrent access to a single instance must be
 * externally synchronized.  @c parse is reentrant.
 *
 * @par Example
 * @code
 * Error err;
 * auto doc = XmlDocument::parse("<?xml version=\"1.0\"?>\n<Clip id=\"001\"/>", &err);
 * XmlElement root = doc.root();
 * String name = root.name();           // "Clip"
 * String id   = root.attribute("id");  // "001"
 * @endcode
 */
class XmlDocument {
        public:
                /**
                 * @brief Parses an XML document from a string.
                 * @param str The XML source to parse.
                 * @param err Optional rich error output; carries the pugi
                 *            status, byte offset, line/column, and a
                 *            human-readable description on failure.
                 * @return The parsed XmlDocument, or an invalid document on failure.
                 *
                 * Callers that only need a yes/no result can omit @p err
                 * and check @ref isValid on the returned handle.
                 */
                static XmlDocument parse(const String &str, XmlParseError *err = nullptr);

                /**
                 * @brief Loads an XML document from a filesystem or resource path.
                 *
                 * Reads via @ref File so any path supported there works,
                 * including the @c :/ resource namespace.  IO failures
                 * are surfaced through @p err with a synthesised pugi
                 * status (@c status_file_not_found / @c status_io_error)
                 * so callers see a uniform error path with @ref parse.
                 */
                static XmlDocument loadFromPath(const String &path, XmlParseError *err = nullptr);

                /**
                 * @brief Saves this document to a filesystem path.
                 *
                 * Writes via @ref File so the @c :/ resource namespace is
                 * accepted (read-only resources will fail with
                 * @c Error::PermissionDenied).  Truncates and creates
                 * the file as needed.
                 *
                 * @param path   Filesystem path.
                 * @param indent Spaces per indentation level (0 = compact).
                 * @return @c Error::Ok on success, or a system error.
                 */
                Error saveToPath(const String &path, unsigned int indent = 0) const;

                /** @brief Constructs an empty (invalid) XmlDocument. */
                XmlDocument() = default;

                /**
                 * @brief Constructs a document whose root is a deep copy of @p root.
                 *
                 * No declaration is inserted; callers wanting one should
                 * follow up with @ref setDeclaration.
                 */
                explicit XmlDocument(const XmlElement &root);

                /** @brief True when the document has a root element. */
                bool isValid() const;

                // --- Declaration ---

                /** @brief Returns the @c version attribute of the XML declaration, or empty. */
                String declarationVersion() const;

                /** @brief Returns the @c encoding attribute of the XML declaration, or empty. */
                String declarationEncoding() const;

                /** @brief Returns the @c standalone attribute of the XML declaration, or empty. */
                String declarationStandalone() const;

                /**
                 * @brief Sets (or replaces) the XML declaration.
                 * @param version    Required version string (typically @c "1.0").
                 * @param encoding   Optional encoding (e.g. @c "UTF-8"). Omitted when empty.
                 * @param standalone Optional standalone attribute (e.g. @c "yes"). Omitted when empty.
                 */
                void setDeclaration(const String &version = "1.0",
                                    const String &encoding = "UTF-8",
                                    const String &standalone = String());

                // --- Doctype ---

                /** @brief Returns the raw DOCTYPE body, or empty if none. */
                String doctype() const;

                /** @brief Replaces (or sets) the DOCTYPE; an empty string removes it. */
                void setDoctype(const String &doctype);

                // --- Root element ---

                /**
                 * @brief Returns a deep copy of the document's root element, or invalid if none.
                 *
                 * Mutations to the returned XmlElement do not affect the document.
                 */
                XmlElement root() const;

                /// @brief Document-level shortcut for @ref XmlElement::selectFirst on the root.
                XmlElement selectFirst(const String &query, Error *err = nullptr) const {
                        return root().selectFirst(query, err);
                }

                /// @brief Document-level shortcut for @ref XmlElement::selectAll on the root.
                List<XmlElement> selectAll(const String &query, Error *err = nullptr) const {
                        return root().selectAll(query, err);
                }

                /// @brief Document-level shortcut for @ref XmlElement::elementByPath on the root.
                XmlElement elementByPath(const String &path) const { return root().elementByPath(path); }

                /**
                 * @brief Replaces the document's root element with a deep copy of @p elem.
                 *
                 * Existing top-level comments and PIs are preserved; the
                 * declaration and DOCTYPE are preserved.
                 */
                void setRoot(const XmlElement &elem);

                /** @brief Appends a top-level comment node (after the existing root). */
                void appendComment(const String &text);

                /** @brief Appends a top-level processing instruction. */
                void appendProcessingInstruction(const String &target, const String &data);

                /** @brief Removes everything from the document (declaration, doctype, root, top-level nodes). */
                void clear();

                /**
                 * @brief Serializes the document to an XML string.
                 * @param indent Spaces per indentation level. Zero produces compact output.
                 */
                String toString(unsigned int indent = 0) const;

                /** @brief Returns true when both documents serialize identically. */
                bool operator==(const XmlDocument &other) const;

                /** @brief Inequality. */
                bool operator!=(const XmlDocument &other) const { return !(*this == other); }

        private:
                friend class XmlElement;
                friend class XmlNode;

                SharedPtr<XmlData> _d = SharedPtr<XmlData>::create();

                explicit XmlDocument(const SharedPtr<XmlData> &d) : _d(d) {}
};

/**
 * @brief Tagged-union handle for any direct child node of an XmlElement.
 * @ingroup util
 *
 * Returned by @ref XmlElement::children when callers need to walk
 * mixed content (text interleaved with child elements, comments,
 * processing instructions, etc.).  For the common
 * elements-only case prefer @ref XmlElement::elements.
 *
 * @par Variants
 * - @ref Element — wraps an @ref XmlElement (which is itself a CoW handle).
 * - @ref Text / @ref CData / @ref Comment — carries the textual body.
 * - @ref ProcessingInstruction — carries both target and data.
 * - @ref Null — default-constructed; valid but empty.
 * - @ref Undefined — synthetic state for "no such child"-style lookups.
 */
class XmlNode {
        public:
                /** @brief Discriminant for the held node. */
                enum Type {
                        Null,                  ///< Empty default-constructed node.
                        Element,               ///< XmlElement child.
                        Text,                  ///< Plain text node.
                        CData,                 ///< CDATA section.
                        Comment,               ///< XML comment.
                        ProcessingInstruction, ///< XML processing instruction.
                        Undefined              ///< Synthetic "no such node" state.
                };

                /** @brief Default-constructs a Null node. */
                XmlNode() = default;

                /** @brief Returns the synthetic Undefined node. */
                static XmlNode undefined();

                /** @brief Wraps an XmlElement as a node of type @ref Element. */
                XmlNode(const XmlElement &elem);

                /** @brief Constructs a Text node with @p body. */
                static XmlNode makeText(const String &body);

                /** @brief Constructs a CDATA node with @p body. */
                static XmlNode makeCData(const String &body);

                /** @brief Constructs a Comment node with @p body. */
                static XmlNode makeComment(const String &body);

                /** @brief Constructs a processing-instruction node. */
                static XmlNode makeProcessingInstruction(const String &target, const String &data);

                /** @brief Returns the discriminant. */
                Type type() const { return _type; }

                /** @brief True iff @c type() == Null. */
                bool isNull() const { return _type == Null; }
                /** @brief True iff @c type() == Undefined. */
                bool isUndefined() const { return _type == Undefined; }
                /** @brief True iff @c type() == Element. */
                bool isElement() const { return _type == Element; }
                /** @brief True iff @c type() == Text. */
                bool isText() const { return _type == Text; }
                /** @brief True iff @c type() == CData. */
                bool isCData() const { return _type == CData; }
                /** @brief True iff @c type() == Comment. */
                bool isComment() const { return _type == Comment; }
                /** @brief True iff @c type() == ProcessingInstruction. */
                bool isProcessingInstruction() const { return _type == ProcessingInstruction; }

                /** @brief Returns the wrapped XmlElement, or an invalid element for non-Element types. */
                XmlElement toElement() const;

                /**
                 * @brief Returns the textual body of the node.
                 *
                 * For Text/CData/Comment this is the literal body; for
                 * ProcessingInstruction it is the data portion (use
                 * @ref piTarget for the target name); for Element this
                 * is @ref XmlElement::text; otherwise empty.
                 */
                String text() const;

                /** @brief Returns the PI target name (empty unless @c type()==ProcessingInstruction). */
                String piTarget() const;

                /** @brief Serializes this node as an XML fragment. */
                String toString() const;

                /** @brief Equality compares type and contents. */
                bool operator==(const XmlNode &other) const;

                /** @brief Inequality. */
                bool operator!=(const XmlNode &other) const { return !(*this == other); }

        private:
                Type       _type = Null;
                XmlElement _element;
                String     _content;
                String     _piTarget;
};

// ============================================================================
// DataStream serialization (text-form, length-prefixed, matching JSON pattern)
// ============================================================================

/**
 * @brief Writes an XmlDocument as a tagged, length-prefixed XML string.
 */
inline DataStream &operator<<(DataStream &stream, const XmlDocument &doc) {
        stream.beginFrame(DataStream::TypeXmlDocument, 1);
        stream << doc.toString(0);
        stream.endFrame();
        return stream;
}

/**
 * @brief Reads an XmlDocument from a tagged, length-prefixed XML string.
 */
inline DataStream &operator>>(DataStream &stream, XmlDocument &doc) {
        if (!stream.readFrame(DataStream::TypeXmlDocument)) {
                doc = XmlDocument();
                return stream;
        }
        String text;
        stream >> text;
        if (stream.status() != DataStream::Ok) {
                doc = XmlDocument();
                return stream;
        }
        // A default-constructed XmlDocument serializes to an empty
        // string; round-trip it as a default rather than feeding the
        // parser an empty buffer.
        if (text.isEmpty()) {
                doc = XmlDocument();
                return stream;
        }
        XmlParseError perr;
        doc = XmlDocument::parse(text, &perr);
        if (!perr) {
                stream.setError(DataStream::ReadCorruptData,
                                String("XmlDocument::parse failed: ") + perr.toString());
        }
        return stream;
}

/**
 * @brief Writes an XmlElement as a tagged, length-prefixed XML string.
 */
inline DataStream &operator<<(DataStream &stream, const XmlElement &elem) {
        stream.beginFrame(DataStream::TypeXmlElement, 1);
        stream << elem.toString(0);
        stream.endFrame();
        return stream;
}

/**
 * @brief Reads an XmlElement from a tagged, length-prefixed XML string.
 */
inline DataStream &operator>>(DataStream &stream, XmlElement &elem) {
        if (!stream.readFrame(DataStream::TypeXmlElement)) {
                elem = XmlElement();
                return stream;
        }
        String text;
        stream >> text;
        if (stream.status() != DataStream::Ok) {
                elem = XmlElement();
                return stream;
        }
        // A default-constructed XmlElement serializes to an empty
        // string; round-trip it as a default rather than feeding the
        // parser an empty buffer.
        if (text.isEmpty()) {
                elem = XmlElement();
                return stream;
        }
        XmlParseError perr;
        elem = XmlElement::parse(text, &perr);
        if (!perr) {
                stream.setError(DataStream::ReadCorruptData,
                                String("XmlElement::parse failed: ") + perr.toString());
        }
        return stream;
}

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::XmlDocument);
PROMEKI_FORMAT_VIA_TOSTRING(promeki::XmlElement);

#endif // PROMEKI_ENABLE_CORE
