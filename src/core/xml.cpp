/**
 * @file      xml.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <sstream>
#include <string>
#include <promeki/optional.h>
#include <promeki/function.h>
#include <promeki/xml.h>
#include <promeki/file.h>
#include <promeki/list.h>
#include <promeki/pair.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

// Parse flags for full-fidelity round-trip: declaration, doctype, comments,
// and PIs are preserved.  Whitespace-only PCDATA between siblings is still
// dropped (default behaviour) so children() returns clean structural data.
constexpr unsigned int kParseFlags = pugi::parse_full;

/**
 * @brief pugi::xml_writer adapter that accumulates into a std::string.
 */
class StringWriter : public pugi::xml_writer {
        public:
                std::string out;
                void        write(const void *data, size_t size) override {
                        out.append(static_cast<const char *>(data), size);
                }
};

/** @brief Strip a trailing '\n' if pugi added one. */
String trimTrailingNewline(std::string s) {
        if (!s.empty() && s.back() == '\n') s.pop_back();
        return String(std::move(s));
}

// ---------------------------------------------------------------------------
// Namespace helpers
// ---------------------------------------------------------------------------

/**
 * @brief Splits a raw qualified XML name into (prefix, local).
 * @return @c (prefix, local). Prefix is empty when there is no colon.
 */
Pair<String, String> splitQName(const String &raw) {
        size_t colon = raw.find(':');
        if (colon == String::npos) return {String(), raw};
        return {raw.substr(0, colon), raw.substr(colon + 1)};
}

/**
 * @brief Walks @p node and its ancestors collecting in-scope @c xmlns bindings.
 *
 * Inner declarations win over outer; the empty key represents the
 * default namespace.
 */
Map<String, String> collectInScope(pugi::xml_node node) {
        Map<String, String> scope;
        for (auto n = node; n; n = n.parent()) {
                for (auto a = n.first_attribute(); a; a = a.next_attribute()) {
                        String name(a.name());
                        if (name == "xmlns") {
                                if (!scope.contains(String())) scope.insert(String(), String(a.value()));
                        } else if (name.startsWith("xmlns:")) {
                                String prefix = name.substr(6);
                                if (!scope.contains(prefix)) scope.insert(prefix, String(a.value()));
                        }
                }
        }
        return scope;
}

/**
 * @brief Returns the URI bound to @p prefix in scope at @p node, or empty if none.
 *
 * Empty @p prefix means the default namespace.
 */
String lookupNamespaceUri(pugi::xml_node node, const String &prefix) {
        const char *attrName = prefix.isEmpty() ? "xmlns" : nullptr;
        String      buf;
        if (!attrName) {
                buf      = String("xmlns:") + prefix;
                attrName = buf.cstr();
        }
        for (auto n = node; n; n = n.parent()) {
                auto a = n.attribute(attrName);
                if (a) return String(a.value());
        }
        return String();
}

/**
 * @brief Returns the namespace-qualified XmlName for an element node.
 *
 * Empty prefix → default namespace (or no namespace if no default in
 * scope).  Unbound prefixes resolve to an empty URI.
 */
XmlName resolveElementName(pugi::xml_node node) {
        if (!node) return XmlName();
        auto [prefix, local] = splitQName(String(node.name()));
        String uri           = lookupNamespaceUri(node, prefix);
        return XmlName(uri, prefix, local);
}

/**
 * @brief Returns the namespace-qualified XmlName for an attribute on @p owner.
 *
 * Per the XML Namespaces spec, unprefixed attributes are NOT in the
 * default namespace — they sit in no namespace.
 */
XmlName resolveAttributeName(pugi::xml_node owner, pugi::xml_attribute attr) {
        if (!attr) return XmlName();
        auto [prefix, local] = splitQName(String(attr.name()));
        if (prefix.isEmpty()) return XmlName(String(), String(), local);
        String uri = lookupNamespaceUri(owner, prefix);
        return XmlName(uri, prefix, local);
}

/**
 * @brief Returns the prefix the given @p uri is bound to in scope at @p node, or std::nullopt.
 *
 * Searches inner-first; the empty string represents the default
 * namespace binding.
 */
struct PrefixLookup {
                bool   found = false;
                String prefix; // empty → default namespace
};
PrefixLookup findPrefixForUri(pugi::xml_node node, const String &uri) {
        PrefixLookup ret;
        for (auto n = node; n; n = n.parent()) {
                for (auto a = n.first_attribute(); a; a = a.next_attribute()) {
                        String name(a.name());
                        if (name == "xmlns") {
                                if (String(a.value()) == uri) {
                                        ret.found  = true;
                                        ret.prefix = String();
                                        return ret;
                                }
                        } else if (name.startsWith("xmlns:")) {
                                if (String(a.value()) == uri) {
                                        ret.found  = true;
                                        ret.prefix = name.substr(6);
                                        return ret;
                                }
                        }
                }
        }
        return ret;
}

/**
 * @brief Returns true if @p prefix is currently bound (to anything) in scope at @p node.
 *
 * Empty @p prefix asks about the default namespace binding.
 */
bool isPrefixBound(pugi::xml_node node, const String &prefix) {
        return !lookupNamespaceUri(node, prefix).isEmpty();
}

/**
 * @brief Picks an unused @c xmlns:auto<N> prefix at @p node.
 */
String generateAutoPrefix(pugi::xml_node node) {
        for (int i = 1; i < 1000; ++i) {
                String candidate = String("auto") + String::number(i);
                if (!isPrefixBound(node, candidate)) return candidate;
        }
        return String("auto"); // pathological fallback
}

/**
 * @brief Resolves (or injects on @p target) a prefix for @p uri.
 *
 * @param target          The element receiving the binding when
 *                        an injection is needed.
 * @param uri             Desired namespace URI; empty means "no
 *                        namespace" — caller should not invoke this.
 * @param preferredPrefix Hint to use when none is in scope; @c "" means
 *                        "use default namespace".
 * @return The prefix to use ("" for default namespace).
 */
String findOrInjectPrefix(pugi::xml_node target, const String &uri, const String &preferredPrefix) {
        PrefixLookup found = findPrefixForUri(target, uri);
        if (found.found) return found.prefix;

        // No existing binding for this URI in scope. Decide where to inject.
        String chosen = preferredPrefix;
        if (!chosen.isEmpty()) {
                // Hint provided — make sure it's not already bound to something else.
                if (isPrefixBound(target, chosen)) chosen = generateAutoPrefix(target);
        } else {
                // Empty hint: only acceptable if no default namespace is in scope.
                // Otherwise generate an auto-prefix.
                if (isPrefixBound(target, String())) chosen = generateAutoPrefix(target);
        }

        String declAttr = chosen.isEmpty() ? String("xmlns") : (String("xmlns:") + chosen);
        target.append_attribute(declAttr.cstr()).set_value(uri.cstr());
        return chosen;
}

/**
 * @brief Resolves the raw qualified name for a namespaced attribute write.
 *
 * If @p qname has no URI, returns the local part as-is.  Otherwise
 * locates an in-scope prefix (or injects @c xmlns:auto<N> on
 * @p target) and returns @c "prefix:local".  Attributes can't sit in
 * the default namespace, so an empty prefix hint always falls through
 * to an auto-generated prefix when no other in-scope binding works.
 */
String resolveAttributeRawName(pugi::xml_node target, const XmlName &qname) {
        if (qname.uri().isEmpty()) return qname.local();
        String hint   = qname.prefix().isEmpty() ? generateAutoPrefix(target) : qname.prefix();
        String prefix = findOrInjectPrefix(target, qname.uri(), hint);
        if (prefix.isEmpty()) {
                prefix = generateAutoPrefix(target);
                target.append_attribute((String("xmlns:") + prefix).cstr()).set_value(qname.uri().cstr());
        }
        return prefix + String(":") + qname.local();
}

/**
 * @brief Returns the @p index'th direct child of @p parent, or null if out of range.
 *
 * Counts every child (element, text, CDATA, comment, PI) — matching
 * the index space the child-mutation API uses externally.
 */
pugi::xml_node childAtIndex(pugi::xml_node parent, size_t index) {
        if (!parent) return pugi::xml_node();
        size_t i = 0;
        for (auto c = parent.first_child(); c; c = c.next_sibling(), ++i) {
                if (i == index) return c;
        }
        return pugi::xml_node();
}

/**
 * @brief Returns the @p index'th attribute on @p target, or null if out of range.
 */
pugi::xml_attribute attributeAtIndex(pugi::xml_node target, size_t index) {
        if (!target) return pugi::xml_attribute();
        size_t i = 0;
        for (auto a = target.first_attribute(); a; a = a.next_attribute(), ++i) {
                if (i == index) return a;
        }
        return pugi::xml_attribute();
}

/**
 * @brief Inject @c xmlns / @c xmlns:foo declarations from @p sourceContext's
 * scope onto @p target unless they're already bound there.
 *
 * Used after deep-copying a subtree out of a parent so that the
 * extracted root remains namespace-resolvable in isolation.
 */
void inheritScopeOnto(pugi::xml_node target, pugi::xml_node sourceContext) {
        if (!target || !sourceContext) return;
        auto scope = collectInScope(sourceContext);
        for (auto it = scope.constBegin(); it != scope.constEnd(); ++it) {
                const String &prefix = it->first;
                const String &uri    = it->second;
                String declAttr      = prefix.isEmpty() ? String("xmlns") : (String("xmlns:") + prefix);
                if (target.attribute(declAttr.cstr())) continue;
                target.append_attribute(declAttr.cstr()).set_value(uri.cstr());
        }
}

} // namespace

// ---------------------------------------------------------------------------
// XmlName
// ---------------------------------------------------------------------------

XmlName XmlName::parseClark(const String &clark) {
        if (clark.isEmpty()) return XmlName();
        if (clark.startsWith('{')) {
                size_t close = clark.find('}', 1);
                if (close == String::npos) return XmlName();
                return XmlName(clark.substr(1, close - 1), String(), clark.substr(close + 1));
        }
        return XmlName(clark);
}

String XmlName::qualified() const {
        if (_prefix.isEmpty()) return _local;
        return _prefix + String(":") + _local;
}

String XmlName::clark() const {
        if (_uri.isEmpty()) return _local;
        return String("{") + _uri + String("}") + _local;
}

// ---------------------------------------------------------------------------
// XmlAttribute
// ---------------------------------------------------------------------------

XmlAttribute::XmlAttribute(XmlName qname, String value)
        : _name(qname.qualified()), _qname(std::move(qname)), _value(std::move(value)) {}

// ---------------------------------------------------------------------------
// XmlParseError
// ---------------------------------------------------------------------------

void XmlParseError::setFromPugi(const pugi::xml_parse_result &result, const String &source) {
        _status  = result.status;
        _message = String(result.description());
        if (result.status == pugi::status_ok) {
                _offset = 0;
                _line   = 0;
                _column = 0;
                return;
        }
        _offset = static_cast<size_t>(result.offset < 0 ? 0 : result.offset);
        // Compute 1-based line / column by scanning the source up to offset.
        // pugi reports offset in bytes; clamp to the source length to be safe.
        const std::string &src = source.str();
        size_t              cap = std::min(_offset, src.size());
        int                 line = 1;
        int                 col  = 1;
        for (size_t i = 0; i < cap; ++i) {
                if (src[i] == '\n') {
                        ++line;
                        col = 1;
                } else {
                        ++col;
                }
        }
        _line   = line;
        _column = col;
}

Error XmlParseError::toError() const {
        switch (_status) {
                case pugi::status_ok: return Error::Ok;
                case pugi::status_out_of_memory: return Error::NoMem;
                case pugi::status_io_error: return Error::IOError;
                case pugi::status_file_not_found: return Error::NotExist;
                default: return Error::ParseFailed;
        }
}

String XmlParseError::toString() const {
        if (ok()) return String();
        // "line N, col M: message"
        return String("line ") + String::number(_line) + String(", col ") + String::number(_column)
             + String(": ") + _message;
}

// ---------------------------------------------------------------------------
// XmlData
// ---------------------------------------------------------------------------

XmlData::XmlData(const XmlData &other) { doc.reset(other.doc); }

XmlData &XmlData::operator=(const XmlData &other) {
        if (this != &other) doc.reset(other.doc);
        return *this;
}

// ---------------------------------------------------------------------------
// XmlElement
// ---------------------------------------------------------------------------

XmlElement XmlElement::parse(const String &str, XmlParseError *err) {
        XmlElement             ret;
        pugi::xml_parse_result res = ret._d.modify()->doc.load_string(str.cstr(), kParseFlags);
        if (!res) {
                if (err) err->setFromPugi(res, str);
                return XmlElement();
        }
        if (!ret._d->doc.document_element()) {
                // Successful parse but no element — synthesise a no-document-element status.
                pugi::xml_parse_result synth = res;
                synth.status                 = pugi::status_no_document_element;
                synth.offset                 = 0;
                if (err) err->setFromPugi(synth, str);
                return XmlElement();
        }
        // Strip everything except the first element so the document holds
        // exactly one element at root — the XmlElement invariant.
        auto &doc       = ret._d.modify()->doc;
        auto  firstElem = doc.document_element();
        for (auto child = doc.first_child(); child;) {
                auto next = child.next_sibling();
                if (child != firstElem) doc.remove_child(child);
                child = next;
        }
        if (err) err->setFromPugi(res, str);
        return ret;
}

XmlElement::XmlElement(const String &name) {
        _d.modify()->doc.append_child(name.cstr());
}

bool XmlElement::isValid() const { return static_cast<bool>(_d->doc.document_element()); }

pugi::xml_node XmlElement::rootNode() const { return _d->doc.document_element(); }

void XmlElement::resetTo(const pugi::xml_node &node) {
        auto &doc = _d.modify()->doc;
        doc.reset();
        if (!node) return;
        auto target = doc.append_copy(node);
        // Walk the source's ancestors and inject any in-scope xmlns
        // bindings missing on the extracted root, so the subtree remains
        // self-contained even after CoW deep-copy out of its parent.
        inheritScopeOnto(target, node.parent());
}

String XmlElement::name() const {
        auto root = rootNode();
        if (!root) return String();
        return String(root.name());
}

void XmlElement::setName(const String &name) {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (root) root.set_name(name.cstr());
        else doc.append_child(name.cstr());
}

XmlName XmlElement::qname() const { return resolveElementName(rootNode()); }

Map<String, String> XmlElement::namespaces() const { return collectInScope(rootNode()); }

bool XmlElement::hasAttribute(const String &name) const {
        auto root = rootNode();
        if (!root) return false;
        return static_cast<bool>(root.attribute(name.cstr()));
}

bool XmlElement::hasAttribute(const XmlName &qname) const {
        auto root = rootNode();
        if (!root) return false;
        for (auto a = root.first_attribute(); a; a = a.next_attribute()) {
                if (resolveAttributeName(root, a) == qname) return true;
        }
        return false;
}

String XmlElement::attribute(const String &name, Error *err) const {
        auto root = rootNode();
        if (!root) {
                if (err) *err = Error::Invalid;
                return String();
        }
        auto attr = root.attribute(name.cstr());
        if (!attr) {
                if (err) *err = Error::Invalid;
                return String();
        }
        if (err) *err = Error::Ok;
        return String(attr.value());
}

String XmlElement::attribute(const XmlName &qname, Error *err) const {
        auto root = rootNode();
        if (!root) {
                if (err) *err = Error::Invalid;
                return String();
        }
        for (auto a = root.first_attribute(); a; a = a.next_attribute()) {
                if (resolveAttributeName(root, a) == qname) {
                        if (err) *err = Error::Ok;
                        return String(a.value());
                }
        }
        if (err) *err = Error::Invalid;
        return String();
}

void XmlElement::setAttribute(const String &name, const String &value) {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return;
        auto attr = root.attribute(name.cstr());
        if (!attr) attr = root.append_attribute(name.cstr());
        attr.set_value(value.cstr());
}

void XmlElement::setAttribute(const XmlName &qname, const String &value) {
        if (!qname.isValid()) return;
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return;
        // Look for an existing attribute with the same (uri, local).
        for (auto a = root.first_attribute(); a; a = a.next_attribute()) {
                if (resolveAttributeName(root, a) == qname) {
                        a.set_value(value.cstr());
                        return;
                }
        }
        String rawName = resolveAttributeRawName(root, qname);
        root.append_attribute(rawName.cstr()).set_value(value.cstr());
}

bool XmlElement::removeAttribute(const String &name) {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return false;
        return root.remove_attribute(name.cstr());
}

bool XmlElement::removeAttribute(const XmlName &qname) {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return false;
        for (auto a = root.first_attribute(); a; a = a.next_attribute()) {
                if (resolveAttributeName(root, a) == qname) return root.remove_attribute(a);
        }
        return false;
}

List<XmlAttribute> XmlElement::attributes() const {
        List<XmlAttribute> ret;
        auto               root = rootNode();
        if (!root) return ret;
        for (auto a = root.first_attribute(); a; a = a.next_attribute()) {
                XmlName qn = resolveAttributeName(root, a);
                ret.pushToBack(XmlAttribute(String(a.name()), std::move(qn), String(a.value())));
        }
        return ret;
}

bool XmlElement::hasChild(const String &name) const {
        auto root = rootNode();
        if (!root) return false;
        return static_cast<bool>(root.child(name.cstr()));
}

bool XmlElement::hasChild(const XmlName &qname) const {
        auto root = rootNode();
        if (!root) return false;
        for (auto c = root.first_child(); c; c = c.next_sibling()) {
                if (c.type() != pugi::node_element) continue;
                if (resolveElementName(c) == qname) return true;
        }
        return false;
}

XmlElement XmlElement::child(const String &name) const {
        XmlElement ret;
        auto       root = rootNode();
        if (!root) return ret;
        auto found = root.child(name.cstr());
        if (!found) return ret;
        ret.resetTo(found);
        return ret;
}

XmlElement XmlElement::child(const XmlName &qname) const {
        XmlElement ret;
        auto       root = rootNode();
        if (!root) return ret;
        for (auto c = root.first_child(); c; c = c.next_sibling()) {
                if (c.type() != pugi::node_element) continue;
                if (resolveElementName(c) == qname) {
                        ret.resetTo(c);
                        return ret;
                }
        }
        return ret;
}

List<XmlElement> XmlElement::elements() const {
        List<XmlElement> ret;
        auto             root = rootNode();
        if (!root) return ret;
        for (auto c = root.first_child(); c; c = c.next_sibling()) {
                if (c.type() != pugi::node_element) continue;
                XmlElement e;
                e.resetTo(c);
                ret.pushToBack(std::move(e));
        }
        return ret;
}

List<XmlElement> XmlElement::elementsNamed(const String &name) const {
        List<XmlElement> ret;
        auto             root = rootNode();
        if (!root) return ret;
        for (auto c = root.child(name.cstr()); c; c = c.next_sibling(name.cstr())) {
                XmlElement e;
                e.resetTo(c);
                ret.pushToBack(std::move(e));
        }
        return ret;
}

List<XmlElement> XmlElement::elementsNamed(const XmlName &qname) const {
        List<XmlElement> ret;
        auto             root = rootNode();
        if (!root) return ret;
        for (auto c = root.first_child(); c; c = c.next_sibling()) {
                if (c.type() != pugi::node_element) continue;
                if (resolveElementName(c) == qname) {
                        XmlElement e;
                        e.resetTo(c);
                        ret.pushToBack(std::move(e));
                }
        }
        return ret;
}

// ---------------------------------------------------------------------------
// XPath
// ---------------------------------------------------------------------------

XmlElement XmlElement::selectFirst(const String &query, Error *err) const {
        XmlElement ret;
        auto       root = rootNode();
        if (!root) {
                if (err) *err = Error::Invalid;
                return ret;
        }
        try {
                pugi::xpath_node hit = root.select_node(query.cstr());
                if (err) *err = Error::Ok;
                if (!hit) return ret;
                if (auto n = hit.node(); n && n.type() == pugi::node_element) {
                        ret.resetTo(n);
                }
                return ret;
        } catch (const pugi::xpath_exception &) {
                if (err) *err = Error::ParseFailed;
                return XmlElement();
        }
}

List<XmlElement> XmlElement::selectAll(const String &query, Error *err) const {
        List<XmlElement> ret;
        auto             root = rootNode();
        if (!root) {
                if (err) *err = Error::Invalid;
                return ret;
        }
        try {
                pugi::xpath_node_set hits = root.select_nodes(query.cstr());
                if (err) *err = Error::Ok;
                hits.sort();
                for (const auto &h : hits) {
                        auto n = h.node();
                        if (n && n.type() == pugi::node_element) {
                                XmlElement e;
                                e.resetTo(n);
                                ret.pushToBack(std::move(e));
                        }
                }
                return ret;
        } catch (const pugi::xpath_exception &) {
                if (err) *err = Error::ParseFailed;
                return List<XmlElement>();
        }
}

XmlAttribute XmlElement::selectFirstAttribute(const String &query, Error *err) const {
        auto root = rootNode();
        if (!root) {
                if (err) *err = Error::Invalid;
                return XmlAttribute();
        }
        try {
                pugi::xpath_node hit = root.select_node(query.cstr());
                if (err) *err = Error::Ok;
                if (!hit || !hit.attribute()) return XmlAttribute();
                auto attr  = hit.attribute();
                auto owner = hit.parent();
                XmlName qn = resolveAttributeName(owner, attr);
                return XmlAttribute(String(attr.name()), std::move(qn), String(attr.value()));
        } catch (const pugi::xpath_exception &) {
                if (err) *err = Error::ParseFailed;
                return XmlAttribute();
        }
}

List<XmlAttribute> XmlElement::selectAllAttributes(const String &query, Error *err) const {
        List<XmlAttribute> ret;
        auto               root = rootNode();
        if (!root) {
                if (err) *err = Error::Invalid;
                return ret;
        }
        try {
                pugi::xpath_node_set hits = root.select_nodes(query.cstr());
                if (err) *err = Error::Ok;
                hits.sort();
                for (const auto &h : hits) {
                        auto attr = h.attribute();
                        if (!attr) continue;
                        auto    owner = h.parent();
                        XmlName qn    = resolveAttributeName(owner, attr);
                        ret.pushToBack(
                                XmlAttribute(String(attr.name()), std::move(qn), String(attr.value())));
                }
                return ret;
        } catch (const pugi::xpath_exception &) {
                if (err) *err = Error::ParseFailed;
                return List<XmlAttribute>();
        }
}

// ---------------------------------------------------------------------------
// Path navigation
// ---------------------------------------------------------------------------

XmlElement XmlElement::elementByPath(const String &path) const {
        XmlElement ret;
        auto       root = rootNode();
        if (!root) return ret;
        auto found = root.first_element_by_path(path.cstr(), '/');
        if (!found) return ret;
        ret.resetTo(found);
        return ret;
}

List<XmlNode> XmlElement::children() const {
        List<XmlNode> ret;
        auto          root = rootNode();
        if (!root) return ret;
        for (auto c = root.first_child(); c; c = c.next_sibling()) {
                switch (c.type()) {
                        case pugi::node_element: {
                                XmlElement e;
                                e.resetTo(c);
                                ret.pushToBack(XmlNode(e));
                                break;
                        }
                        case pugi::node_pcdata:
                                ret.pushToBack(XmlNode::makeText(String(c.value())));
                                break;
                        case pugi::node_cdata:
                                ret.pushToBack(XmlNode::makeCData(String(c.value())));
                                break;
                        case pugi::node_comment:
                                ret.pushToBack(XmlNode::makeComment(String(c.value())));
                                break;
                        case pugi::node_pi:
                                ret.pushToBack(
                                        XmlNode::makeProcessingInstruction(String(c.name()), String(c.value())));
                                break;
                        default: break;
                }
        }
        return ret;
}

String XmlElement::text() const {
        auto root = rootNode();
        if (!root) return String();
        String out;
        for (auto c = root.first_child(); c; c = c.next_sibling()) {
                if (c.type() == pugi::node_pcdata || c.type() == pugi::node_cdata) {
                        out += c.value();
                }
        }
        return out;
}

void XmlElement::setText(const String &text) {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return;
        // Remove all existing text/cdata children.
        for (auto c = root.first_child(); c;) {
                auto next = c.next_sibling();
                if (c.type() == pugi::node_pcdata || c.type() == pugi::node_cdata) {
                        root.remove_child(c);
                }
                c = next;
        }
        auto t = root.append_child(pugi::node_pcdata);
        t.set_value(text.cstr());
}

void XmlElement::appendElement(const String &name) {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return;
        root.append_child(name.cstr());
}

void XmlElement::appendElement(const String &name, const String &text) {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return;
        auto child = root.append_child(name.cstr());
        auto t     = child.append_child(pugi::node_pcdata);
        t.set_value(text.cstr());
}

namespace {
/**
 * @brief Common helper: append a new element under @p parent with the
 * namespace-qualified @p qname, injecting an @c xmlns binding on the
 * new child if the URI isn't already in scope.
 *
 * @return The newly appended pugi node.
 */
pugi::xml_node appendNamespacedElement(pugi::xml_node parent, const XmlName &qname) {
        if (!parent || !qname.isValid()) return pugi::xml_node();
        // Step 1: append the child with a placeholder name so that
        // findOrInjectPrefix can see the new element as an ancestor for
        // scoping purposes.
        auto child = parent.append_child("placeholder");
        if (qname.uri().isEmpty()) {
                child.set_name(qname.local().cstr());
                return child;
        }
        // Choose a prefix; inject an xmlns binding on the child if needed.
        // Empty prefix hint means "use default namespace".
        String prefix = findOrInjectPrefix(child, qname.uri(), qname.prefix());
        String fullName = prefix.isEmpty() ? qname.local() : (prefix + String(":") + qname.local());
        child.set_name(fullName.cstr());
        return child;
}
} // namespace

void XmlElement::appendElement(const XmlName &qname) {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        appendNamespacedElement(root, qname);
}

void XmlElement::appendElement(const XmlName &qname, const String &text) {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        auto  child = appendNamespacedElement(root, qname);
        if (!child) return;
        auto t = child.append_child(pugi::node_pcdata);
        t.set_value(text.cstr());
}

void XmlElement::appendText(const String &text) {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return;
        auto t = root.append_child(pugi::node_pcdata);
        t.set_value(text.cstr());
}

void XmlElement::appendCData(const String &text) {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return;
        auto t = root.append_child(pugi::node_cdata);
        t.set_value(text.cstr());
}

void XmlElement::appendComment(const String &text) {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return;
        auto c = root.append_child(pugi::node_comment);
        c.set_value(text.cstr());
}

void XmlElement::appendProcessingInstruction(const String &target, const String &data) {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return;
        auto pi = root.append_child(pugi::node_pi);
        pi.set_name(target.cstr());
        pi.set_value(data.cstr());
}

void XmlElement::appendChild(const XmlElement &childElem) {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return;
        auto src = childElem.rootNode();
        if (!src) return;
        root.append_copy(src);
}

void XmlElement::appendChild(const XmlNode &node) {
        switch (node.type()) {
                case XmlNode::Element: appendChild(node.toElement()); break;
                case XmlNode::Text: appendText(node.text()); break;
                case XmlNode::CData: appendCData(node.text()); break;
                case XmlNode::Comment: appendComment(node.text()); break;
                case XmlNode::ProcessingInstruction:
                        appendProcessingInstruction(node.piTarget(), node.text());
                        break;
                case XmlNode::Null:
                case XmlNode::Undefined: break;
        }
}

void XmlElement::appendChild(const XmlDocument &doc) { appendChild(doc.root()); }

XmlDocument XmlElement::toDocument() const { return XmlDocument(*this); }

void XmlElement::clear() {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return;
        // Remove all attributes.
        while (auto a = root.first_attribute()) root.remove_attribute(a);
        // Remove all children.
        while (auto c = root.first_child()) root.remove_child(c);
}

// ---------------------------------------------------------------------------
// Prepend / insert at position
// ---------------------------------------------------------------------------

void XmlElement::prependElement(const String &name) {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return;
        root.prepend_child(name.cstr());
}

void XmlElement::prependElement(const String &name, const String &text) {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return;
        auto child = root.prepend_child(name.cstr());
        auto t     = child.append_child(pugi::node_pcdata);
        t.set_value(text.cstr());
}

void XmlElement::prependChild(const XmlElement &child) {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return;
        auto src = child.rootNode();
        if (!src) return;
        root.prepend_copy(src);
}

void XmlElement::prependChild(const XmlNode &node) {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return;
        switch (node.type()) {
                case XmlNode::Element: {
                        auto src = node.toElement().rootNode();
                        if (src) root.prepend_copy(src);
                        break;
                }
                case XmlNode::Text: {
                        auto t = root.prepend_child(pugi::node_pcdata);
                        t.set_value(node.text().cstr());
                        break;
                }
                case XmlNode::CData: {
                        auto t = root.prepend_child(pugi::node_cdata);
                        t.set_value(node.text().cstr());
                        break;
                }
                case XmlNode::Comment: {
                        auto t = root.prepend_child(pugi::node_comment);
                        t.set_value(node.text().cstr());
                        break;
                }
                case XmlNode::ProcessingInstruction: {
                        auto pi = root.prepend_child(pugi::node_pi);
                        pi.set_name(node.piTarget().cstr());
                        pi.set_value(node.text().cstr());
                        break;
                }
                case XmlNode::Null:
                case XmlNode::Undefined: break;
        }
}

void XmlElement::prependChild(const XmlDocument &doc) { prependChild(doc.root()); }

void XmlElement::insertChildAt(size_t index, const XmlElement &child) {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return;
        auto src = child.rootNode();
        if (!src) return;
        auto target = childAtIndex(root, index);
        if (!target) {
                root.append_copy(src);
        } else if (index == 0) {
                root.prepend_copy(src);
        } else {
                root.insert_copy_before(src, target);
        }
}

void XmlElement::insertChildAt(size_t index, const XmlNode &node) {
        // Append-then-move keeps the per-type creation logic in one place.
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return;
        size_t origCount = childCount();
        appendChild(node);
        // appendChild may no-op for Null/Undefined.
        if (childCount() == origCount) return;
        if (index >= origCount) return; // already at the end.
        auto last   = root.last_child();
        auto target = childAtIndex(root, index);
        if (!last || !target) return;
        // Move `last` to be the new child at `index`. pugi has no public
        // node-move API so we copy-insert then delete the original.
        root.insert_copy_before(last, target);
        root.remove_child(last);
}

// ---------------------------------------------------------------------------
// Targeted removal
// ---------------------------------------------------------------------------

bool XmlElement::removeChild(const String &name) {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return false;
        auto found = root.child(name.cstr());
        if (!found) return false;
        return root.remove_child(found);
}

bool XmlElement::removeChild(const XmlName &qname) {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return false;
        for (auto c = root.first_child(); c; c = c.next_sibling()) {
                if (c.type() != pugi::node_element) continue;
                if (resolveElementName(c) == qname) return root.remove_child(c);
        }
        return false;
}

bool XmlElement::removeChildAt(size_t index) {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return false;
        auto target = childAtIndex(root, index);
        if (!target) return false;
        return root.remove_child(target);
}

size_t XmlElement::removeAllNamed(const String &name) {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return 0;
        size_t removed = 0;
        for (auto c = root.first_child(); c;) {
                auto next = c.next_sibling();
                if (c.type() == pugi::node_element && String(c.name()) == name) {
                        if (root.remove_child(c)) ++removed;
                }
                c = next;
        }
        return removed;
}

size_t XmlElement::removeAllNamed(const XmlName &qname) {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return 0;
        size_t removed = 0;
        for (auto c = root.first_child(); c;) {
                auto next = c.next_sibling();
                if (c.type() == pugi::node_element && resolveElementName(c) == qname) {
                        if (root.remove_child(c)) ++removed;
                }
                c = next;
        }
        return removed;
}

size_t XmlElement::childCount() const {
        auto root = rootNode();
        if (!root) return 0;
        size_t n = 0;
        for (auto c = root.first_child(); c; c = c.next_sibling()) ++n;
        return n;
}

// ---------------------------------------------------------------------------
// Attribute order primitives
// ---------------------------------------------------------------------------

void XmlElement::prependAttribute(const String &name, const String &value) {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return;
        // Drop any existing attribute with the same name so we don't end
        // up with duplicates.
        root.remove_attribute(name.cstr());
        auto a = root.prepend_attribute(name.cstr());
        a.set_value(value.cstr());
}

void XmlElement::prependAttribute(const XmlName &qname, const String &value) {
        if (!qname.isValid()) return;
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return;
        // Remove any existing attribute matching this qname.
        for (auto a = root.first_attribute(); a; a = a.next_attribute()) {
                if (resolveAttributeName(root, a) == qname) {
                        root.remove_attribute(a);
                        break;
                }
        }
        String rawName = resolveAttributeRawName(root, qname);
        auto   a       = root.prepend_attribute(rawName.cstr());
        a.set_value(value.cstr());
}

void XmlElement::insertAttributeAt(size_t index, const String &name, const String &value) {
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return;
        root.remove_attribute(name.cstr());
        auto target = attributeAtIndex(root, index);
        pugi::xml_attribute a;
        if (!target) a = root.append_attribute(name.cstr());
        else if (index == 0) a = root.prepend_attribute(name.cstr());
        else a = root.insert_attribute_before(name.cstr(), target);
        a.set_value(value.cstr());
}

void XmlElement::insertAttributeAt(size_t index, const XmlName &qname, const String &value) {
        if (!qname.isValid()) return;
        auto &doc  = _d.modify()->doc;
        auto  root = doc.document_element();
        if (!root) return;
        for (auto a = root.first_attribute(); a; a = a.next_attribute()) {
                if (resolveAttributeName(root, a) == qname) {
                        root.remove_attribute(a);
                        break;
                }
        }
        String rawName = resolveAttributeRawName(root, qname);
        // resolveAttributeRawName may have just appended an xmlns:autoN
        // declaration on root, which counts as an attribute — so refresh
        // the index target after that potential injection.
        auto                target = attributeAtIndex(root, index);
        pugi::xml_attribute a;
        if (!target) a = root.append_attribute(rawName.cstr());
        else if (index == 0) a = root.prepend_attribute(rawName.cstr());
        else a = root.insert_attribute_before(rawName.cstr(), target);
        a.set_value(value.cstr());
}

size_t XmlElement::attributeCount() const {
        auto root = rootNode();
        if (!root) return 0;
        size_t n = 0;
        for (auto a = root.first_attribute(); a; a = a.next_attribute()) ++n;
        return n;
}

String XmlElement::toString(unsigned int indent) const {
        auto root = rootNode();
        if (!root) return String();
        StringWriter w;
        if (indent == 0) {
                root.print(w, "", pugi::format_raw);
        } else {
                String indentStr(indent, ' ');
                root.print(w, indentStr.cstr(), pugi::format_indent);
        }
        return trimTrailingNewline(std::move(w.out));
}

void XmlElement::forEachElement(Function<void(const XmlElement &)> func) const {
        for (const auto &e : elements()) func(e);
}

bool XmlElement::operator==(const XmlElement &other) const {
        if (_d == other._d) return true;
        return toString(0) == other.toString(0);
}

// ---------------------------------------------------------------------------
// XmlDocument
// ---------------------------------------------------------------------------

XmlDocument XmlDocument::parse(const String &str, XmlParseError *err) {
        XmlDocument            ret;
        pugi::xml_parse_result res = ret._d.modify()->doc.load_string(str.cstr(), kParseFlags);
        if (err) err->setFromPugi(res, str);
        if (!res) return XmlDocument();
        return ret;
}

Result<XmlDocument> XmlDocument::fromString(const String &str) {
        XmlParseError perr;
        XmlDocument   doc = parse(str, &perr);
        if (!doc.isValid()) return makeError<XmlDocument>(Error::ParseFailed);
        return makeResult(std::move(doc));
}

namespace {
/**
 * @brief Synthesises a pugi parse result for a file-IO failure.
 *
 * Lets @ref loadFromPath funnel IO and parse errors through the same
 * @ref XmlParseError out-param so callers see one error path.
 */
pugi::xml_parse_result synthIoFailure(pugi::xml_parse_status status) {
        pugi::xml_parse_result r;
        r.status = status;
        r.offset = 0;
        return r;
}
} // namespace

XmlDocument XmlDocument::loadFromPath(const String &path, XmlParseError *err) {
        File  f(path);
        Error openErr = f.open(IODevice::ReadOnly);
        if (openErr.isError()) {
                pugi::xml_parse_status status = (openErr == Error::NotExist)
                                                        ? pugi::status_file_not_found
                                                        : pugi::status_io_error;
                if (err) err->setFromPugi(synthIoFailure(status), String());
                return XmlDocument();
        }
        Result<int64_t> sz       = f.size();
        int64_t         fileSize = sz.second().isOk() ? sz.first() : 0;
        if (fileSize <= 0) fileSize = 16384; // grow-as-we-go fallback
        promeki::List<char> scratch;
        scratch.resize(static_cast<size_t>(fileSize));
        int64_t nread = f.read(scratch.data(), static_cast<int64_t>(scratch.size()));
        f.close();
        if (nread < 0) {
                if (err) err->setFromPugi(synthIoFailure(pugi::status_io_error), String());
                return XmlDocument();
        }
        String text(scratch.data(), static_cast<size_t>(nread));
        return parse(text, err);
}

Error XmlDocument::saveToPath(const String &path, unsigned int indent) const {
        File  f(path);
        Error openErr = f.open(IODevice::WriteOnly, File::Create | File::Truncate);
        if (openErr.isError()) return openErr;
        String  text = toString(indent);
        int64_t want = static_cast<int64_t>(text.size());
        int64_t got  = f.write(text.cstr(), want);
        Error   closeErr = f.close();
        if (got != want) return Error::IOError;
        return closeErr;
}

XmlDocument::XmlDocument(const XmlElement &rootElem) {
        auto src = rootElem.rootNode();
        if (src) _d.modify()->doc.append_copy(src);
}

bool XmlDocument::isValid() const { return static_cast<bool>(_d->doc.document_element()); }

namespace {
pugi::xml_node findDeclaration(pugi::xml_document &doc) {
        for (auto c = doc.first_child(); c; c = c.next_sibling()) {
                if (c.type() == pugi::node_declaration) return c;
        }
        return pugi::xml_node();
}

pugi::xml_node findDoctype(pugi::xml_document &doc) {
        for (auto c = doc.first_child(); c; c = c.next_sibling()) {
                if (c.type() == pugi::node_doctype) return c;
        }
        return pugi::xml_node();
}
} // namespace

String XmlDocument::declarationVersion() const {
        auto decl = findDeclaration(const_cast<pugi::xml_document &>(_d->doc));
        if (!decl) return String();
        return String(decl.attribute("version").value());
}

String XmlDocument::declarationEncoding() const {
        auto decl = findDeclaration(const_cast<pugi::xml_document &>(_d->doc));
        if (!decl) return String();
        return String(decl.attribute("encoding").value());
}

String XmlDocument::declarationStandalone() const {
        auto decl = findDeclaration(const_cast<pugi::xml_document &>(_d->doc));
        if (!decl) return String();
        return String(decl.attribute("standalone").value());
}

void XmlDocument::setDeclaration(const String &version,
                                 const String &encoding,
                                 const String &standalone) {
        auto &doc  = _d.modify()->doc;
        auto  decl = findDeclaration(doc);
        if (!decl) {
                decl = doc.prepend_child(pugi::node_declaration);
        }
        // Replace attributes wholesale.
        while (auto a = decl.first_attribute()) decl.remove_attribute(a);
        decl.append_attribute("version").set_value(version.cstr());
        if (!encoding.isEmpty()) decl.append_attribute("encoding").set_value(encoding.cstr());
        if (!standalone.isEmpty()) decl.append_attribute("standalone").set_value(standalone.cstr());
}

String XmlDocument::doctype() const {
        auto dt = findDoctype(const_cast<pugi::xml_document &>(_d->doc));
        if (!dt) return String();
        return String(dt.value());
}

void XmlDocument::setDoctype(const String &dtBody) {
        auto &doc = _d.modify()->doc;
        auto  dt  = findDoctype(doc);
        if (dtBody.isEmpty()) {
                if (dt) doc.remove_child(dt);
                return;
        }
        if (!dt) {
                // Insert after declaration if present, else at top.
                auto decl = findDeclaration(doc);
                if (decl) dt = doc.insert_child_after(pugi::node_doctype, decl);
                else dt = doc.prepend_child(pugi::node_doctype);
        }
        dt.set_value(dtBody.cstr());
}

XmlElement XmlDocument::root() const {
        XmlElement ret;
        auto       r = _d->doc.document_element();
        if (!r) return ret;
        ret.resetTo(r);
        return ret;
}

void XmlDocument::setRoot(const XmlElement &elem) {
        auto &doc      = _d.modify()->doc;
        auto  existing = doc.document_element();
        if (existing) doc.remove_child(existing);
        auto src = elem.rootNode();
        if (src) doc.append_copy(src);
}

void XmlDocument::appendComment(const String &text) {
        auto &doc = _d.modify()->doc;
        auto  c   = doc.append_child(pugi::node_comment);
        c.set_value(text.cstr());
}

void XmlDocument::appendProcessingInstruction(const String &target, const String &data) {
        auto &doc = _d.modify()->doc;
        auto  pi  = doc.append_child(pugi::node_pi);
        pi.set_name(target.cstr());
        pi.set_value(data.cstr());
}

void XmlDocument::clear() { _d.modify()->doc.reset(); }

String XmlDocument::toString(unsigned int indent) const {
        StringWriter w;
        unsigned int flags    = (indent == 0) ? pugi::format_raw : pugi::format_indent;
        // Suppress declaration auto-generation when none is present in the tree —
        // pugi otherwise injects "<?xml version=\"1.0\"?>" on save.
        bool         hasDecl  = false;
        for (auto c = _d->doc.first_child(); c; c = c.next_sibling()) {
                if (c.type() == pugi::node_declaration) {
                        hasDecl = true;
                        break;
                }
        }
        if (!hasDecl) flags |= pugi::format_no_declaration;
        String indentStr;
        if (indent > 0) indentStr = String(indent, ' ');
        _d->doc.save(w, indent == 0 ? "" : indentStr.cstr(), flags);
        return trimTrailingNewline(std::move(w.out));
}

bool XmlDocument::operator==(const XmlDocument &other) const {
        if (_d == other._d) return true;
        return toString(0) == other.toString(0);
}

// ---------------------------------------------------------------------------
// XmlNode
// ---------------------------------------------------------------------------

XmlNode XmlNode::undefined() {
        XmlNode n;
        n._type = Undefined;
        return n;
}

XmlNode::XmlNode(const XmlElement &elem) : _type(Element), _element(elem) {}

XmlNode XmlNode::makeText(const String &body) {
        XmlNode n;
        n._type    = Text;
        n._content = body;
        return n;
}

XmlNode XmlNode::makeCData(const String &body) {
        XmlNode n;
        n._type    = CData;
        n._content = body;
        return n;
}

XmlNode XmlNode::makeComment(const String &body) {
        XmlNode n;
        n._type    = Comment;
        n._content = body;
        return n;
}

XmlNode XmlNode::makeProcessingInstruction(const String &target, const String &data) {
        XmlNode n;
        n._type     = ProcessingInstruction;
        n._piTarget = target;
        n._content  = data;
        return n;
}

XmlElement XmlNode::toElement() const {
        if (_type == Element) return _element;
        return XmlElement();
}

String XmlNode::text() const {
        switch (_type) {
                case Text:
                case CData:
                case Comment:
                case ProcessingInstruction: return _content;
                case Element: return _element.text();
                case Null:
                case Undefined: return String();
        }
        return String();
}

String XmlNode::piTarget() const { return _piTarget; }

String XmlNode::toString() const {
        switch (_type) {
                case Element: return _element.toString(0);
                case Text: return _content;
                case CData: return String("<![CDATA[") + _content + String("]]>");
                case Comment: return String("<!--") + _content + String("-->");
                case ProcessingInstruction:
                        if (_content.isEmpty()) return String("<?") + _piTarget + String("?>");
                        return String("<?") + _piTarget + String(" ") + _content + String("?>");
                case Null:
                case Undefined: return String();
        }
        return String();
}

bool XmlNode::operator==(const XmlNode &other) const {
        if (_type != other._type) return false;
        switch (_type) {
                case Element: return _element == other._element;
                case Text:
                case CData:
                case Comment: return _content == other._content;
                case ProcessingInstruction:
                        return _piTarget == other._piTarget && _content == other._content;
                case Null:
                case Undefined: return true;
        }
        return false;
}

Error XmlDocument::writeToStream(DataStream &s) const {
        s << toString(0);
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<XmlDocument> XmlDocument::readFromStream<1>(DataStream &s) {
        String text;
        s >> text;
        if (s.status() != DataStream::Ok) return makeError<XmlDocument>(s.toError());
        // A default-constructed XmlDocument serializes to an empty
        // string; round-trip it as a default rather than feeding the
        // parser an empty buffer.
        if (text.isEmpty()) return makeResult(XmlDocument());
        XmlParseError perr;
        XmlDocument   doc = XmlDocument::parse(text, &perr);
        if (!perr) {
                s.setError(DataStream::ReadCorruptData,
                           String("XmlDocument::parse failed: ") + perr.toString());
                return makeError<XmlDocument>(Error::CorruptData);
        }
        return makeResult(std::move(doc));
}

PROMEKI_NAMESPACE_END
