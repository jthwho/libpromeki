/**
 * @file      xml.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/xml.h>
#include <promeki/datastream.h>
#include <promeki/buffer.h>
#include <promeki/bufferiodevice.h>
#include <promeki/variant.h>
#include <promeki/dir.h>

using namespace promeki;

namespace {
struct StreamFixture {
                Buffer         buf;
                BufferIODevice dev;
                StreamFixture() : buf(8192), dev(&buf) { dev.open(IODevice::ReadWrite); }
};
} // namespace

// ============================================================================
// XmlElement basics
// ============================================================================

TEST_CASE("XmlElement_DefaultIsInvalid") {
        XmlElement e;
        CHECK_FALSE(e.isValid());
        CHECK(e.name() == "");
        CHECK(e.attributes().isEmpty());
        CHECK(e.elements().isEmpty());
}

TEST_CASE("XmlElement_BuildAndQuery") {
        XmlElement clip("Clip");
        CHECK(clip.isValid());
        CHECK(clip.name() == "Clip");

        clip.setAttribute("id", "001");
        clip.setAttribute("hdr", "true");
        CHECK(clip.hasAttribute("id"));
        CHECK(clip.attribute("id") == "001");
        CHECK(clip.attribute("hdr") == "true");

        Error attrErr;
        CHECK(clip.attribute("missing", &attrErr) == "");
        CHECK(attrErr.isError());

        clip.appendElement("Width", "1920");
        clip.appendElement("Height", "1080");
        CHECK(clip.elements().size() == 2);
        CHECK(clip.child("Width").text() == "1920");
        CHECK(clip.child("Height").text() == "1080");
        CHECK_FALSE(clip.child("Missing").isValid());
}

TEST_CASE("XmlElement_RoundTrip") {
        const String src = "<a x=\"1\"><b>hello</b><c/></a>";
        XmlParseError err;
        XmlElement e = XmlElement::parse(src, &err);
        REQUIRE_FALSE(err.isError());
        CHECK(e.name() == "a");
        CHECK(e.attribute("x") == "1");

        // Serialize compact, parse again, must be identical.
        String     out = e.toString(0);
        XmlElement e2  = XmlElement::parse(out, &err);
        REQUIRE_FALSE(err.isError());
        CHECK(e == e2);
        CHECK(e2.toString(0) == out);
}

TEST_CASE("XmlElement_SetTextReplacesPCData") {
        XmlElement e("p");
        e.appendText("hello ");
        e.appendElement("b", "world");
        e.appendText("!");
        CHECK(e.text() == "hello !");

        e.setText("replaced");
        CHECK(e.text() == "replaced");
        // Element child should still be present.
        CHECK(e.elements().size() == 1);
        CHECK(e.child("b").text() == "world");
}

TEST_CASE("XmlElement_RemoveAttribute") {
        XmlElement e("x");
        e.setAttribute("a", "1");
        e.setAttribute("b", "2");
        CHECK(e.removeAttribute("a"));
        CHECK_FALSE(e.removeAttribute("a"));
        CHECK_FALSE(e.hasAttribute("a"));
        CHECK(e.hasAttribute("b"));
}

TEST_CASE("XmlElement_Children_MixedContent") {
        XmlParseError err;
        auto  e = XmlElement::parse("<r>a<b/>c<!--note--><?p inst?>d</r>", &err);
        REQUIRE_FALSE(err.isError());

        auto kids = e.children();
        REQUIRE(kids.size() == 6);
        CHECK(kids[0].isText());
        CHECK(kids[0].text() == "a");
        CHECK(kids[1].isElement());
        CHECK(kids[1].toElement().name() == "b");
        CHECK(kids[2].isText());
        CHECK(kids[2].text() == "c");
        CHECK(kids[3].isComment());
        CHECK(kids[3].text() == "note");
        CHECK(kids[4].isProcessingInstruction());
        CHECK(kids[4].piTarget() == "p");
        CHECK(kids[4].text() == "inst");
        CHECK(kids[5].isText());
        CHECK(kids[5].text() == "d");
}

TEST_CASE("XmlElement_CData") {
        XmlElement e("script");
        e.appendCData("if (x < y) { ... }");
        CHECK(e.text() == "if (x < y) { ... }");
        // CDATA round-trips through toString as <![CDATA[...]]>.
        String out = e.toString(0);
        CHECK(out.contains("<![CDATA[if (x < y) { ... }]]>"));

        XmlParseError err;
        auto  parsed = XmlElement::parse(out, &err);
        REQUIRE_FALSE(err.isError());
        CHECK(parsed.text() == "if (x < y) { ... }");
}

TEST_CASE("XmlElement_AppendChildDeepCopy") {
        XmlElement parent("a");
        XmlElement child("b");
        child.setAttribute("k", "v");
        parent.appendChild(child);

        // Mutating the child after append must not touch the parent's copy.
        child.setAttribute("k", "MUTATED");
        CHECK(parent.child("b").attribute("k") == "v");
}

TEST_CASE("XmlElement_AppendChild_Document") {
        // Build a fragment as its own document, then graft it into a parent.
        XmlElement fragRoot("Fragment");
        fragRoot.setAttribute("v", "1");
        fragRoot.appendElement("Inner", "x");
        XmlDocument frag(fragRoot);
        frag.setDeclaration("1.0", "UTF-8");
        // Top-level comment on the fragment doc should NOT survive the graft —
        // only the root element flows.
        frag.appendComment("doc-level comment");

        XmlElement parent("Root");
        parent.appendChild(frag);

        CHECK(parent.child("Fragment").attribute("v") == "1");
        CHECK(parent.child("Fragment").child("Inner").text() == "x");
        // No declaration / doctype / top-level comment carried in.
        String out = parent.toString(0);
        CHECK_FALSE(out.contains("<?xml"));
        CHECK_FALSE(out.contains("doc-level comment"));
}

// ============================================================================
// XPath
// ============================================================================

TEST_CASE("XmlElement_XPath_DirectChildAndDescendant") {
        XmlParseError perr;
        auto          doc = XmlDocument::parse(
                "<root><a><b id=\"1\"/></a><a><b id=\"2\"/></a><c/></root>", &perr);
        REQUIRE_FALSE(perr.isError());
        XmlElement root = doc.root();

        // Direct child
        auto first = root.selectFirst("a");
        CHECK(first.isValid());
        CHECK(first.name() == "a");

        // Descendant axis returns both b's; sorted in document order
        auto bs = root.selectAll(".//b");
        REQUIRE(bs.size() == 2);
        CHECK(bs[0].attribute("id") == "1");
        CHECK(bs[1].attribute("id") == "2");
}

TEST_CASE("XmlElement_XPath_AttributeSelect") {
        XmlParseError perr;
        auto          doc = XmlDocument::parse("<r><x v=\"a\"/><x v=\"b\"/></r>", &perr);
        REQUIRE_FALSE(perr.isError());

        auto attrs = doc.root().selectAllAttributes(".//x/@v");
        REQUIRE(attrs.size() == 2);
        CHECK(attrs[0].value() == "a");
        CHECK(attrs[1].value() == "b");

        auto first = doc.root().selectFirstAttribute(".//x/@v");
        CHECK(first.isValid());
        CHECK(first.value() == "a");
}

TEST_CASE("XmlElement_XPath_BadQuery") {
        XmlElement r("x");
        Error      err;
        auto       hit = r.selectFirst("[[[invalid", &err);
        CHECK(err == Error::ParseFailed);
        CHECK_FALSE(hit.isValid());
}

TEST_CASE("XmlDocument_XPath_DocumentLevel") {
        XmlParseError perr;
        auto          doc = XmlDocument::parse("<r><a><b/></a></r>", &perr);
        REQUIRE_FALSE(perr.isError());
        // Document-level selectFirst is rooted at the document element.
        auto b = doc.selectFirst(".//b");
        CHECK(b.isValid());
        CHECK(b.name() == "b");
}

// ============================================================================
// elementByPath
// ============================================================================

TEST_CASE("XmlElement_ElementByPath") {
        XmlElement r("Header");
        XmlElement id("Identity");
        id.appendElement("UUID", "abc-123");
        r.appendChild(id);

        auto found = r.elementByPath("Identity/UUID");
        CHECK(found.isValid());
        CHECK(found.text() == "abc-123");

        auto missing = r.elementByPath("Identity/Missing");
        CHECK_FALSE(missing.isValid());
}

// ============================================================================
// Prepend / insert / remove for children
// ============================================================================

TEST_CASE("XmlElement_PrependElement") {
        XmlElement r("a");
        r.appendElement("Z");
        r.prependElement("A");
        auto kids = r.elements();
        REQUIRE(kids.size() == 2);
        CHECK(kids[0].name() == "A");
        CHECK(kids[1].name() == "Z");
}

TEST_CASE("XmlElement_PrependElement_WithText") {
        XmlElement r("a");
        r.appendElement("Z", "z");
        r.prependElement("A", "a");
        auto kids = r.elements();
        REQUIRE(kids.size() == 2);
        CHECK(kids[0].text() == "a");
        CHECK(kids[1].text() == "z");
}

TEST_CASE("XmlElement_PrependChild_DeepCopy") {
        XmlElement r("a");
        r.appendElement("Z");
        XmlElement c("B");
        c.setAttribute("k", "v");
        r.prependChild(c);
        c.setAttribute("k", "MUTATED");
        // Original untouched
        CHECK(r.child("B").attribute("k") == "v");
        // And it's at position 0
        CHECK(r.elements()[0].name() == "B");
}

TEST_CASE("XmlElement_InsertChildAt") {
        XmlElement r("a");
        r.appendElement("First");
        r.appendElement("Last");

        XmlElement mid("Middle");
        r.insertChildAt(1, mid);
        auto kids = r.elements();
        REQUIRE(kids.size() == 3);
        CHECK(kids[0].name() == "First");
        CHECK(kids[1].name() == "Middle");
        CHECK(kids[2].name() == "Last");

        // Out-of-range index → append
        XmlElement tail("Tail");
        r.insertChildAt(99, tail);
        CHECK(r.elements().back().name() == "Tail");

        // Index 0 → prepend
        XmlElement head("Head");
        r.insertChildAt(0, head);
        CHECK(r.elements().front().name() == "Head");
}

TEST_CASE("XmlElement_RemoveChild_ByName") {
        XmlElement r("a");
        r.appendElement("X");
        r.appendElement("Y");
        CHECK(r.removeChild(String("X")));
        CHECK_FALSE(r.removeChild(String("X")));
        CHECK(r.elements().size() == 1);
        CHECK(r.elements()[0].name() == "Y");
}

TEST_CASE("XmlElement_RemoveChildAt") {
        XmlElement r("a");
        r.appendElement("X");
        r.appendElement("Y");
        r.appendElement("Z");
        CHECK(r.removeChildAt(1));
        auto kids = r.elements();
        REQUIRE(kids.size() == 2);
        CHECK(kids[0].name() == "X");
        CHECK(kids[1].name() == "Z");
        CHECK_FALSE(r.removeChildAt(99));
}

TEST_CASE("XmlElement_RemoveAllNamed") {
        XmlElement r("a");
        r.appendElement("X");
        r.appendElement("Y");
        r.appendElement("X");
        r.appendElement("X");
        CHECK(r.removeAllNamed(String("X")) == 3);
        auto kids = r.elements();
        REQUIRE(kids.size() == 1);
        CHECK(kids[0].name() == "Y");
}

TEST_CASE("XmlElement_ChildCount") {
        XmlElement r("a");
        r.appendElement("X");
        r.appendText("hi");
        r.appendComment("c");
        CHECK(r.childCount() == 3);
}

// ============================================================================
// Attribute order primitives
// ============================================================================

TEST_CASE("XmlElement_PrependAttribute") {
        XmlElement r("a");
        r.setAttribute("z", "1");
        r.prependAttribute("x", "0");
        auto attrs = r.attributes();
        REQUIRE(attrs.size() == 2);
        CHECK(attrs[0].name() == "x");
        CHECK(attrs[1].name() == "z");
}

TEST_CASE("XmlElement_PrependAttribute_ReplaceExisting") {
        XmlElement r("a");
        r.setAttribute("k", "1");
        r.setAttribute("z", "2");
        r.prependAttribute("k", "fresh");
        auto attrs = r.attributes();
        REQUIRE(attrs.size() == 2);
        CHECK(attrs[0].name() == "k");
        CHECK(attrs[0].value() == "fresh");
        CHECK(attrs[1].name() == "z");
}

TEST_CASE("XmlElement_InsertAttributeAt") {
        XmlElement r("a");
        r.setAttribute("first", "1");
        r.setAttribute("third", "3");
        r.insertAttributeAt(1, "second", "2");
        auto attrs = r.attributes();
        REQUIRE(attrs.size() == 3);
        CHECK(attrs[0].name() == "first");
        CHECK(attrs[1].name() == "second");
        CHECK(attrs[2].name() == "third");
}

TEST_CASE("XmlElement_AttributeCount") {
        XmlElement r("a");
        r.setAttribute("a", "1");
        r.setAttribute("b", "2");
        CHECK(r.attributeCount() == 2);
        r.removeAttribute("a");
        CHECK(r.attributeCount() == 1);
}

// ============================================================================
// File I/O
// ============================================================================

TEST_CASE("XmlDocument_SaveAndLoad_Roundtrip") {
        Dir    scratch = Dir::temp();
        String path    = (scratch.path() / "xml_roundtrip.xml").toString();

        XmlElement r("Root");
        r.setAttribute("id", "001");
        r.appendElement("Child", "value");
        XmlDocument original(r);
        original.setDeclaration("1.0", "UTF-8");

        Error saveErr = original.saveToPath(path, 2);
        CHECK(saveErr == Error::Ok);

        XmlParseError perr;
        XmlDocument   loaded = XmlDocument::loadFromPath(path, &perr);
        CHECK(perr.ok());
        CHECK(loaded == original);
}

TEST_CASE("XmlDocument_LoadFromPath_Missing") {
        XmlParseError perr;
        XmlDocument   doc =
                XmlDocument::loadFromPath("/mnt/data/tmp/promeki/does-not-exist.xml", &perr);
        CHECK_FALSE(doc.isValid());
        CHECK(perr.isError());
        CHECK(perr.pugiStatus() == pugi::status_file_not_found);
        CHECK(perr.toError() == Error::NotExist);
}

TEST_CASE("XmlElement_ToDocument_Promotion") {
        XmlElement r("Root");
        r.appendElement("Wanted", "y");
        r.appendElement("Other", "z");

        XmlDocument standalone = r.child("Wanted").toDocument();
        CHECK(standalone.isValid());
        CHECK(standalone.root().name() == "Wanted");
        CHECK(standalone.root().text() == "y");
        // No declaration auto-added.
        CHECK(standalone.declarationVersion() == "");

        // Original parent is untouched.
        CHECK(r.elements().size() == 2);
        CHECK(r.child("Wanted").text() == "y");

        // Mutating the standalone doc doesn't bleed back.
        standalone.root().setText("MUTATED");
        CHECK(r.child("Wanted").text() == "y");
}

TEST_CASE("XmlElement_Clear") {
        XmlElement e("a");
        e.setAttribute("x", "1");
        e.appendElement("b");
        e.appendText("text");
        e.clear();
        CHECK(e.isValid());           // root element survives
        CHECK(e.attributes().isEmpty());
        CHECK(e.elements().isEmpty());
        CHECK(e.text() == "");
}

TEST_CASE("XmlElement_ParseInvalid") {
        XmlParseError err;
        auto  e = XmlElement::parse("not <valid xml", &err);
        CHECK(err.isError());
        CHECK_FALSE(e.isValid());
}

TEST_CASE("XmlParseError_RichDetails") {
        // Multi-line input where the malformed token sits on line 3.
        const String src = "<root>\n  <a/>\n  <b junk\n";
        XmlParseError err;
        auto          doc = XmlDocument::parse(src, &err);
        CHECK_FALSE(doc.isValid());
        CHECK(err.isError());
        CHECK_FALSE(err.ok());
        CHECK(static_cast<bool>(err) == false);
        CHECK(err.pugiStatus() != pugi::status_ok);
        CHECK(err.line() >= 3);
        CHECK(err.column() >= 1);
        CHECK_FALSE(err.message().isEmpty());
        // toString() is non-empty and mentions the line.
        String msg = err.toString();
        CHECK(msg.contains("line "));
        CHECK(msg.contains("col "));
        // toError() maps generic parse failures to ParseFailed.
        CHECK(err.toError() == Error::ParseFailed);
}

TEST_CASE("XmlParseError_DefaultIsOk") {
        XmlParseError err;
        CHECK(err.ok());
        CHECK_FALSE(err.isError());
        CHECK(static_cast<bool>(err) == true);
        CHECK(err.toError() == Error::Ok);
        CHECK(err.message() == "");
        CHECK(err.line() == 0);
        CHECK(err.column() == 0);
        CHECK(err.toString() == "");
}

TEST_CASE("XmlParseError_NoDocumentElement") {
        // Valid XML syntax but no element → status_no_document_element from
        // our XmlElement::parse synthesis path.
        XmlParseError err;
        auto          e = XmlElement::parse("<!-- only a comment -->", &err);
        CHECK_FALSE(e.isValid());
        CHECK(err.isError());
        CHECK(err.pugiStatus() == pugi::status_no_document_element);
}

TEST_CASE("XmlElement_PrettyPrint") {
        XmlElement e("a");
        e.appendElement("b", "x");
        e.appendElement("c", "y");
        String pretty = e.toString(2);
        CHECK(pretty.contains("\n"));
        CHECK(pretty.contains("  <b>x</b>"));
}

// ============================================================================
// XmlDocument
// ============================================================================

TEST_CASE("XmlDocument_DefaultIsInvalid") {
        XmlDocument doc;
        CHECK_FALSE(doc.isValid());
        CHECK_FALSE(doc.root().isValid());
}

TEST_CASE("XmlDocument_FromElement") {
        XmlElement e("Root");
        e.setAttribute("v", "1");
        XmlDocument doc(e);
        CHECK(doc.isValid());
        CHECK(doc.root().name() == "Root");
        CHECK(doc.root().attribute("v") == "1");
}

TEST_CASE("XmlDocument_DeclarationRoundtrip") {
        const String src = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<a/>";
        XmlParseError err;
        XmlDocument doc = XmlDocument::parse(src, &err);
        REQUIRE_FALSE(err.isError());
        CHECK(doc.declarationVersion() == "1.0");
        CHECK(doc.declarationEncoding() == "UTF-8");
        CHECK(doc.toString(0).contains("<?xml"));
}

TEST_CASE("XmlDocument_NoDeclaration_NotInjected") {
        XmlParseError err;
        auto  doc = XmlDocument::parse("<a/>", &err);
        REQUIRE_FALSE(err.isError());
        // Without a declaration in the tree, toString must not auto-emit one.
        String out = doc.toString(0);
        CHECK_FALSE(out.contains("<?xml"));
}

TEST_CASE("XmlDocument_SetDeclaration") {
        XmlDocument doc(XmlElement("a"));
        doc.setDeclaration("1.0", "UTF-8");
        CHECK(doc.declarationVersion() == "1.0");
        CHECK(doc.declarationEncoding() == "UTF-8");
        CHECK(doc.toString(0).contains("<?xml"));
}

TEST_CASE("XmlDocument_SetRoot") {
        XmlDocument doc(XmlElement("oldRoot"));
        doc.setRoot(XmlElement("newRoot"));
        CHECK(doc.root().name() == "newRoot");
}

TEST_CASE("XmlDocument_TopLevelComments") {
        XmlDocument doc(XmlElement("a"));
        doc.appendComment("trailing");
        CHECK(doc.toString(0).contains("<!--trailing-->"));
}

// ============================================================================
// Copy-on-write semantics
// ============================================================================

TEST_CASE("XmlElement_CoW_CopyShares") {
        XmlElement orig("a");
        orig.setAttribute("k", "v");
        XmlElement copy = orig;

        // Both observe the original state.
        CHECK(copy.attribute("k") == "v");

        // First mutation on the copy detaches; original is unaffected.
        copy.setAttribute("k", "x");
        CHECK(orig.attribute("k") == "v");
        CHECK(copy.attribute("k") == "x");
}

TEST_CASE("XmlElement_SubtreeIndependence") {
        XmlElement parent("a");
        parent.appendElement("b", "orig");

        XmlElement extracted = parent.child("b");
        extracted.setText("changed");

        // Mutating the extracted subtree must not touch the parent.
        CHECK(parent.child("b").text() == "orig");
}

TEST_CASE("XmlDocument_CoW_CopyShares") {
        XmlDocument orig(XmlElement("a"));
        XmlDocument copy = orig;
        CHECK(copy.isValid());

        copy.setRoot(XmlElement("b"));
        CHECK(orig.root().name() == "a");
        CHECK(copy.root().name() == "b");
}

// ============================================================================
// DataStream round-trip
// ============================================================================

TEST_CASE("XmlDocument_DataStreamRoundtrip") {
        XmlElement r("Root");
        r.setAttribute("id", "001");
        XmlDocument doc(r);
        doc.setDeclaration("1.0", "UTF-8");

        StreamFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << doc;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream  rs = DataStream::createReader(&f.dev);
                XmlDocument round;
                rs >> round;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(round == doc);
        }
}

TEST_CASE("XmlElement_DataStreamRoundtrip") {
        XmlElement e("Foo");
        e.setAttribute("a", "1");
        e.appendElement("Bar", "baz");

        StreamFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << e;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                XmlElement round;
                rs >> round;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(round == e);
        }
}

// ============================================================================
// XmlNode
// ============================================================================

TEST_CASE("XmlNode_FactoriesAndType") {
        CHECK(XmlNode().type() == XmlNode::Null);
        CHECK(XmlNode::undefined().isUndefined());
        CHECK(XmlNode::makeText("hi").isText());
        CHECK(XmlNode::makeText("hi").text() == "hi");
        CHECK(XmlNode::makeCData("x").isCData());
        CHECK(XmlNode::makeComment("c").isComment());
        auto pi = XmlNode::makeProcessingInstruction("xml-stylesheet", "href=\"x.css\"");
        CHECK(pi.isProcessingInstruction());
        CHECK(pi.piTarget() == "xml-stylesheet");
        CHECK(pi.text() == "href=\"x.css\"");
}

TEST_CASE("XmlNode_Equality") {
        CHECK(XmlNode::makeText("a") == XmlNode::makeText("a"));
        CHECK(XmlNode::makeText("a") != XmlNode::makeText("b"));
        CHECK(XmlNode::makeText("a") != XmlNode::makeCData("a"));
}

// ============================================================================
// XmlName
// ============================================================================

TEST_CASE("XmlName_TextForms") {
        XmlName n("http://example.com/ns", "ex", "elem");
        CHECK(n.qualified() == "ex:elem");
        CHECK(n.clark() == "{http://example.com/ns}elem");
        CHECK(n.toString() == "{http://example.com/ns}elem");

        XmlName plain(String("local"));
        CHECK(plain.qualified() == "local");
        CHECK(plain.clark() == "local");
        CHECK_FALSE(plain.hasNamespace());
}

TEST_CASE("XmlName_ParseClark") {
        XmlName n = XmlName::parseClark("{http://x}foo");
        CHECK(n.uri() == "http://x");
        CHECK(n.local() == "foo");
        CHECK(n.prefix() == "");

        XmlName bare = XmlName::parseClark("foo");
        CHECK(bare.uri() == "");
        CHECK(bare.local() == "foo");
}

TEST_CASE("XmlName_EqualityIgnoresPrefix") {
        XmlName a("uri", "p1", "loc");
        XmlName b("uri", "p2", "loc");
        XmlName c("other", "p1", "loc");
        CHECK(a == b);                  // prefix is just a hint
        CHECK(a != c);                  // different URIs differ
        CHECK(XmlName(String("x")) != XmlName("uri", String(), "x"));
}

// ============================================================================
// Namespace lookups
// ============================================================================

TEST_CASE("XmlElement_Namespaces_DefaultInherited") {
        XmlParseError err;
        auto  doc = XmlDocument::parse(
                "<root xmlns=\"http://example.com/n\"><Child/></root>", &err);
        REQUIRE_FALSE(err.isError());

        XmlElement root  = doc.root();
        XmlElement child = root.child(XmlName("http://example.com/n", "Child"));
        CHECK(child.isValid());
        CHECK(child.qname().uri() == "http://example.com/n");
        CHECK(child.qname().local() == "Child");
}

TEST_CASE("XmlElement_Namespaces_PrefixedLookup") {
        XmlParseError err;
        auto  doc = XmlDocument::parse(
                "<root xmlns:foo=\"http://x\"><foo:bar v=\"1\"/></root>", &err);
        REQUIRE_FALSE(err.isError());

        XmlElement bar = doc.root().child(XmlName("http://x", "bar"));
        CHECK(bar.isValid());
        // Prefix used at lookup is irrelevant — match is on (uri, local).
        XmlElement bar2 = doc.root().child(XmlName("http://x", "anything", "bar"));
        CHECK(bar2.isValid());
        CHECK(bar.attribute("v") == "1");
}

TEST_CASE("XmlElement_Attributes_NoDefaultNsForUnprefixed") {
        // Per XML namespaces spec, unprefixed attrs are NOT in the default ns.
        XmlParseError err;
        auto  doc = XmlDocument::parse(
                "<root xmlns=\"http://default\" attr=\"v\"/>", &err);
        REQUIRE_FALSE(err.isError());

        XmlElement r = doc.root();
        // The element IS in the default namespace.
        CHECK(r.qname().uri() == "http://default");
        // The unprefixed attribute is NOT.
        CHECK(r.attribute(XmlName(String("attr"))) == "v");
        CHECK_FALSE(r.hasAttribute(XmlName("http://default", "attr")));
}

TEST_CASE("XmlElement_Namespaces_AccessorReturnsScope") {
        XmlParseError err;
        auto  doc = XmlDocument::parse(
                "<r xmlns=\"http://d\" xmlns:a=\"http://a\"><inner xmlns:b=\"http://b\"/></r>",
                &err);
        REQUIRE_FALSE(err.isError());

        XmlElement inner = doc.root().child(XmlName("http://d", "inner"));
        REQUIRE(inner.isValid());
        auto ns = inner.namespaces();
        CHECK(ns.value("") == "http://d");
        CHECK(ns.value("a") == "http://a");
        CHECK(ns.value("b") == "http://b");
}

TEST_CASE("XmlElement_SubtreeExtractInheritsScope") {
        XmlParseError err;
        auto  doc = XmlDocument::parse(
                "<r xmlns:a=\"http://a\"><a:inner/></r>", &err);
        REQUIRE_FALSE(err.isError());

        XmlElement inner = doc.root().child(XmlName("http://a", "inner"));
        REQUIRE(inner.isValid());
        // The extracted subtree should resolve "a:inner" without needing
        // its parent — scope was inherited on extraction.
        CHECK(inner.qname().uri() == "http://a");
        // Round-trip through the wire: parse output → still resolves.
        String roundtripped = inner.toString(0);
        auto   reparsed     = XmlElement::parse(roundtripped, &err);
        REQUIRE_FALSE(err.isError());
        CHECK(reparsed.qname().uri() == "http://a");
}

// ============================================================================
// Namespace-aware mutation
// ============================================================================

TEST_CASE("XmlElement_SetAttribute_QName_ReusesInScopePrefix") {
        XmlElement r("r");
        r.setAttribute("xmlns:foo", "http://x");
        r.setAttribute(XmlName("http://x", "k"), "v");
        // Should reuse the existing 'foo' prefix.
        CHECK(r.toString(0).contains("foo:k=\"v\""));
        // And the resolved attribute is reachable via the qname.
        CHECK(r.attribute(XmlName("http://x", "k")) == "v");
}

TEST_CASE("XmlElement_SetAttribute_QName_InjectsAutoPrefix") {
        XmlElement r("r");
        r.setAttribute(XmlName("http://x", "k"), "v");
        CHECK(r.attribute(XmlName("http://x", "k")) == "v");
        // An xmlns:auto1="..." declaration must have been injected.
        String out = r.toString(0);
        CHECK(out.contains("xmlns:auto1=\"http://x\""));
        CHECK(out.contains("auto1:k=\"v\""));
}

TEST_CASE("XmlElement_AppendElement_QName_DefaultNamespace") {
        XmlElement r("r");
        // Empty prefix hint with a URI → declare as default namespace on child.
        r.appendElement(XmlName("http://x", "child"));
        String out = r.toString(0);
        CHECK(out.contains("<child"));
        CHECK(out.contains("xmlns=\"http://x\""));
}

TEST_CASE("XmlElement_AppendElement_QName_PrefixedHint") {
        XmlElement r("r");
        r.appendElement(XmlName("http://x", "p", "child"), "body");
        String out = r.toString(0);
        CHECK(out.contains("<p:child"));
        CHECK(out.contains("xmlns:p=\"http://x\""));
        CHECK(out.contains(">body</p:child>"));
}

TEST_CASE("XmlElement_RemoveAttribute_QName") {
        XmlElement r("r");
        r.setAttribute("xmlns:foo", "http://x");
        r.setAttribute(XmlName("http://x", "k"), "v");
        CHECK(r.removeAttribute(XmlName("http://x", "k")));
        CHECK_FALSE(r.hasAttribute(XmlName("http://x", "k")));
        // The xmlns binding survives.
        CHECK(r.toString(0).contains("xmlns:foo"));
}

// ============================================================================
// Variant carry
// ============================================================================

TEST_CASE("Variant_HoldsXmlDocument") {
        XmlElement root("Clip");
        root.setAttribute("id", "001");
        XmlDocument doc(root);
        doc.setDeclaration("1.0", "UTF-8");

        Variant v = doc;
        CHECK(v.type() == Variant::TypeXmlDocument);
        XmlDocument out = v.get<XmlDocument>();
        CHECK(out == doc);
        CHECK(out.root().attribute("id") == "001");
}

TEST_CASE("Variant_XmlDocument_DataStreamRoundtrip") {
        XmlElement r("Foo");
        r.setAttribute("a", "1");
        r.appendElement("Bar", "baz");
        XmlDocument original(r);

        Variant       in = original;
        StreamFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << in;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Variant    out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.type() == Variant::TypeXmlDocument);
                CHECK(out.get<XmlDocument>() == original);
        }
}

// ============================================================================
// XmlAttribute QName population
// ============================================================================

TEST_CASE("XmlElement_AttributesList_PopulatesQName") {
        XmlParseError err;
        auto  doc = XmlDocument::parse(
                "<r xmlns:foo=\"http://x\" foo:k=\"v\" plain=\"p\"/>", &err);
        REQUIRE_FALSE(err.isError());

        auto attrs = doc.root().attributes();
        // xmlns:foo + foo:k + plain → 3 attributes
        REQUIRE(attrs.size() == 3);
        bool sawNs = false, sawK = false, sawPlain = false;
        for (const auto &a : attrs) {
                if (a.name() == "xmlns:foo") sawNs = true;
                if (a.name() == "foo:k") {
                        sawK = true;
                        CHECK(a.qname().uri() == "http://x");
                        CHECK(a.qname().local() == "k");
                        CHECK(a.value() == "v");
                }
                if (a.name() == "plain") {
                        sawPlain = true;
                        CHECK(a.qname().uri() == "");  // unprefixed → no namespace
                        CHECK(a.qname().local() == "plain");
                }
        }
        CHECK(sawNs);
        CHECK(sawK);
        CHECK(sawPlain);
}
