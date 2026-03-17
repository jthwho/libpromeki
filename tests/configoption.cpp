/**
 * @file      configoption.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/configoption.h>

using namespace promeki;

TEST_CASE("Config::ID: default is invalid") {
        Config::ID opt;
        CHECK_FALSE(opt.isValid());
}

TEST_CASE("Config::ID: construct from string is valid") {
        Config::ID opt("cfg.test.option");
        CHECK(opt.isValid());
}

TEST_CASE("Config::ID: same name produces same ID") {
        Config::ID a("cfg.same.name");
        Config::ID b("cfg.same.name");
        CHECK(a == b);
}

TEST_CASE("Config::ID: different names produce different IDs") {
        Config::ID a("cfg.alpha");
        Config::ID b("cfg.beta");
        CHECK(a != b);
}

TEST_CASE("Config::ID: name roundtrip") {
        Config::ID opt("cfg.roundtrip");
        CHECK(opt.name() == "cfg.roundtrip");
}

TEST_CASE("Config: set and get values") {
        Config cfg;
        Config::ID width("cfg.video.width");
        Config::ID height("cfg.video.height");

        cfg.set(width, 1920);
        cfg.set(height, 1080);

        CHECK(cfg.get(width).get<int32_t>() == 1920);
        CHECK(cfg.get(height).get<int32_t>() == 1080);
}

TEST_CASE("Config: get returns default for missing ID") {
        Config cfg;
        Config::ID missing("cfg.no.such.key");
        CHECK_FALSE(cfg.get(missing).isValid());
}

TEST_CASE("Config: contains") {
        Config cfg;
        Config::ID key("cfg.contains.test");
        CHECK_FALSE(cfg.contains(key));
        cfg.set(key, String("value"));
        CHECK(cfg.contains(key));
}

TEST_CASE("Config: remove") {
        Config cfg;
        Config::ID key("cfg.remove.test");
        cfg.set(key, 42);
        CHECK(cfg.remove(key));
        CHECK_FALSE(cfg.contains(key));
}
