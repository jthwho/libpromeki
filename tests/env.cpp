/**
 * @file      env.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/env.h>
#include <promeki/regex.h>

using namespace promeki;

TEST_CASE("Env: get returns empty string for unset variable") {
        String val = Env::get("PROMEKI_TEST_NONEXISTENT_VAR_12345");
        CHECK(val.isEmpty());
}

TEST_CASE("Env: isSet returns false for unset variable") {
        CHECK_FALSE(Env::isSet("PROMEKI_TEST_NONEXISTENT_VAR_12345"));
}

TEST_CASE("Env: set and get round-trip") {
        const char *name = "PROMEKI_TEST_ENV_ROUNDTRIP";
        Env::unset(name);
        CHECK_FALSE(Env::isSet(name));

        CHECK(Env::set(name, "hello"));
        CHECK(Env::isSet(name));
        CHECK(Env::get(name) == "hello");

        Env::unset(name);
}

TEST_CASE("Env: get with default returns default when unset") {
        String val = Env::get("PROMEKI_TEST_NONEXISTENT_VAR_12345", "fallback");
        CHECK(val == "fallback");
}

TEST_CASE("Env: get with default returns value when set") {
        const char *name = "PROMEKI_TEST_ENV_DEFAULT";
        Env::set(name, "actual");
        String val = Env::get(name, "fallback");
        CHECK(val == "actual");
        Env::unset(name);
}

TEST_CASE("Env: set with overwrite=false does not overwrite") {
        const char *name = "PROMEKI_TEST_ENV_NOOVERWRITE";
        Env::set(name, "first");
        CHECK(Env::set(name, "second", false));
        CHECK(Env::get(name) == "first");
        Env::unset(name);
}

TEST_CASE("Env: set with overwrite=true does overwrite") {
        const char *name = "PROMEKI_TEST_ENV_OVERWRITE";
        Env::set(name, "first");
        CHECK(Env::set(name, "second", true));
        CHECK(Env::get(name) == "second");
        Env::unset(name);
}

TEST_CASE("Env: unset removes the variable") {
        const char *name = "PROMEKI_TEST_ENV_UNSET";
        Env::set(name, "value");
        CHECK(Env::isSet(name));
        CHECK(Env::unset(name));
        CHECK_FALSE(Env::isSet(name));
}

TEST_CASE("Env: get known variable PATH") {
        // PATH should always be set in a normal environment
        String path = Env::get("PATH");
        CHECK_FALSE(path.isEmpty());
}

TEST_CASE("Env: set and get empty value") {
        const char *name = "PROMEKI_TEST_ENV_EMPTY";
        CHECK(Env::set(name, ""));
        CHECK(Env::isSet(name));
        CHECK(Env::get(name).isEmpty());
        Env::unset(name);
}

TEST_CASE("Env: list returns all variables including a known one") {
        const char *name = "PROMEKI_TEST_LIST_ALL";
        Env::set(name, "listval");
        Map<String, String> all = Env::list();
        CHECK(all.contains(name));
        CHECK(all.value(name) == "listval");
        // PATH should also be present in a normal environment.
        CHECK(all.contains("PATH"));
        Env::unset(name);
}

TEST_CASE("Env: list with regex filters by name") {
        const char *a = "PROMEKI_TEST_LIST_ALPHA";
        const char *b = "PROMEKI_TEST_LIST_BETA";
        const char *other = "PROMEKI_TEST_UNRELATED_VAR";
        Env::set(a, "1");
        Env::set(b, "2");
        Env::set(other, "3");

        Map<String, String> filtered = Env::list(RegEx("^PROMEKI_TEST_LIST_"));
        CHECK(filtered.contains(a));
        CHECK(filtered.contains(b));
        CHECK_FALSE(filtered.contains(other));
        CHECK(filtered.value(a) == "1");
        CHECK(filtered.value(b) == "2");

        Env::unset(a);
        Env::unset(b);
        Env::unset(other);
}
