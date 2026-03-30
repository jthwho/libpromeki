/**
 * @file      medianodeconfig.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/proav/medianodeconfig.h>

using namespace promeki;

// ============================================================================
// Default state
// ============================================================================

TEST_CASE("MediaNodeConfig_Default") {
    MediaNodeConfig cfg;
    CHECK(!cfg.isValid());
    CHECK(cfg.name().isEmpty());
    CHECK(cfg.type().isEmpty());
    CHECK(cfg.connections().isEmpty());
    CHECK(cfg.options().isEmpty());
}

// ============================================================================
// Construction
// ============================================================================

TEST_CASE("MediaNodeConfig_Construct") {
    MediaNodeConfig cfg("TestPatternNode", "pattern1");
    CHECK(cfg.isValid());
    CHECK(cfg.name() == "pattern1");
    CHECK(cfg.type() == "TestPatternNode");
}

// ============================================================================
// Name and type
// ============================================================================

TEST_CASE("MediaNodeConfig_SetNameType") {
    MediaNodeConfig cfg;
    cfg.setName("mynode");
    cfg.setType("MyNodeType");
    CHECK(cfg.isValid());
    CHECK(cfg.name() == "mynode");
    CHECK(cfg.type() == "MyNodeType");
}

// ============================================================================
// Connections
// ============================================================================

TEST_CASE("MediaNodeConfig_Connections") {
    MediaNodeConfig cfg("Type", "node");
    cfg.addConnection("source1[0]");
    cfg.addConnection("source2.Video");

    StringList conns = cfg.connections();
    CHECK(conns.size() == 2);
    CHECK(conns[0] == "source1[0]");
    CHECK(conns[1] == "source2.Video");
}

TEST_CASE("MediaNodeConfig_SetConnections") {
    MediaNodeConfig cfg("Type", "node");
    StringList conns;
    conns.pushToBack("a");
    conns.pushToBack("b");
    cfg.setConnections(conns);
    CHECK(cfg.connections().size() == 2);
    CHECK(cfg.connections()[0] == "a");
    CHECK(cfg.connections()[1] == "b");
}

// ============================================================================
// Options
// ============================================================================

TEST_CASE("MediaNodeConfig_Options") {
    MediaNodeConfig cfg("Type", "node");
    cfg.set("Width", uint32_t(1920));
    cfg.set("Height", uint32_t(1080));

    CHECK(cfg.contains("Width"));
    CHECK(cfg.get("Width").get<uint32_t>() == 1920);
    CHECK(cfg.get("Height").get<uint32_t>() == 1080);
    CHECK(!cfg.get("missing").isValid());
    CHECK(cfg.get("missing", int32_t(42)).get<int32_t>() == 42);
}

TEST_CASE("MediaNodeConfig_RemoveOption") {
    MediaNodeConfig cfg("Type", "node");
    cfg.set("key", "value");
    CHECK(cfg.contains("key"));
    CHECK(cfg.remove("key"));
    CHECK(!cfg.contains("key"));
    CHECK(!cfg.remove("nonexistent"));
}

// ============================================================================
// Standard keys stored in options map
// ============================================================================

TEST_CASE("MediaNodeConfig_StandardKeysInOptions") {
    MediaNodeConfig cfg("MyType", "myname");
    cfg.addConnection("src[0]");

    // Standard keys should be in the options map
    CHECK(cfg.contains(MediaNodeConfig::KeyName));
    CHECK(cfg.contains(MediaNodeConfig::KeyType));
    CHECK(cfg.contains(MediaNodeConfig::KeyConnections));

    CHECK(cfg.get(MediaNodeConfig::KeyName).get<String>() == "myname");
    CHECK(cfg.get(MediaNodeConfig::KeyType).get<String>() == "MyType");
}

TEST_CASE("MediaNodeConfig_IsStandardKey") {
    CHECK(MediaNodeConfig::isStandardKey("Name"));
    CHECK(MediaNodeConfig::isStandardKey("Type"));
    CHECK(MediaNodeConfig::isStandardKey("Connections"));
    CHECK(!MediaNodeConfig::isStandardKey("width"));
    CHECK(!MediaNodeConfig::isStandardKey(""));
}

// ============================================================================
// ParseConnection: index form
// ============================================================================

TEST_CASE("MediaNodeConfig_ParseConnection_Index") {
    Error err;
    auto pc = MediaNodeConfig::parseConnection("MySource[0]", &err);
    CHECK(err == Error::Ok);
    CHECK(pc.isValid());
    CHECK(pc.nodeName == "MySource");
    CHECK(pc.sourceIndex == 0);
    CHECK(pc.sourceName.isEmpty());
}

TEST_CASE("MediaNodeConfig_ParseConnection_IndexNonZero") {
    auto pc = MediaNodeConfig::parseConnection("Node[3]");
    CHECK(pc.isValid());
    CHECK(pc.nodeName == "Node");
    CHECK(pc.sourceIndex == 3);
}

// ============================================================================
// ParseConnection: name form
// ============================================================================

TEST_CASE("MediaNodeConfig_ParseConnection_Name") {
    Error err;
    auto pc = MediaNodeConfig::parseConnection("MySource.Video", &err);
    CHECK(err == Error::Ok);
    CHECK(pc.isValid());
    CHECK(pc.nodeName == "MySource");
    CHECK(pc.sourceIndex == -1);
    CHECK(pc.sourceName == "Video");
}

// ============================================================================
// ParseConnection: bare name (shorthand for index 0)
// ============================================================================

TEST_CASE("MediaNodeConfig_ParseConnection_Bare") {
    Error err;
    auto pc = MediaNodeConfig::parseConnection("MySource", &err);
    CHECK(err == Error::Ok);
    CHECK(pc.isValid());
    CHECK(pc.nodeName == "MySource");
    CHECK(pc.sourceIndex == 0);
}

// ============================================================================
// ParseConnection: invalid
// ============================================================================

TEST_CASE("MediaNodeConfig_ParseConnection_Empty") {
    Error err;
    auto pc = MediaNodeConfig::parseConnection("", &err);
    CHECK(err == Error::Invalid);
    CHECK(!pc.isValid());
}

TEST_CASE("MediaNodeConfig_ParseConnection_BadBracket") {
    Error err;
    auto pc = MediaNodeConfig::parseConnection("Node[abc]", &err);
    CHECK(err == Error::Invalid);
    CHECK(!pc.isValid());
}

TEST_CASE("MediaNodeConfig_ParseConnection_UnclosedBracket") {
    Error err;
    auto pc = MediaNodeConfig::parseConnection("Node[0", &err);
    CHECK(err == Error::Invalid);
    CHECK(!pc.isValid());
}

TEST_CASE("MediaNodeConfig_ParseConnection_EmptyDotName") {
    Error err;
    auto pc = MediaNodeConfig::parseConnection("Node.", &err);
    CHECK(err == Error::Invalid);
    CHECK(!pc.isValid());
}

TEST_CASE("MediaNodeConfig_ParseConnection_DotNoNode") {
    Error err;
    auto pc = MediaNodeConfig::parseConnection(".Source", &err);
    CHECK(err == Error::Invalid);
    CHECK(!pc.isValid());
}
