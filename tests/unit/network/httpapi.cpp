/**
 * @file      httpapi.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/application.h>
#include <promeki/httpapi.h>
#include <promeki/httpserver.h>
#include <promeki/httpmethod.h>
#include <promeki/json.h>
#include <promeki/variantspec.h>
#include <promeki/enum.h>

using namespace promeki;

namespace {

        // Minimal Application setup — HttpServer's constructor needs an
        // EventLoop, and Application::mainEventLoop is what it falls back to
        // when no thread-local loop is current.
        struct ApiFixture {
                        // Pin the prefix to "/api" so the relative paths the tests
                        // use (e.g. "/health") land at predictable absolute URLs.
                        ApiFixture() : app(0, nullptr), server(), api(server, "/api") {}
                        Application app;
                        HttpServer  server;
                        HttpApi     api;
        };

} // namespace

TEST_CASE("HttpApi: empty catalog has metadata only") {
        ApiFixture f;
        f.api.setTitle("Test API");
        f.api.setVersion("9.9.9");
        f.api.setDescription("hello");

        const JsonObject cat = f.api.toCatalog();
        CHECK(cat.getString("title") == String("Test API"));
        CHECK(cat.getString("version") == String("9.9.9"));
        CHECK(cat.getString("description") == String("hello"));
        CHECK(cat.getArray("endpoints").size() == 0);
}

TEST_CASE("HttpApi: route() registers endpoint and rejects duplicates") {
        ApiFixture        f;
        HttpApi::Endpoint ep;
        ep.path = "/health"; // relative to "/api"
        ep.method = HttpMethod::Get;
        ep.title = "Health";

        CHECK(f.api.route(ep, [](const HttpRequest &, HttpResponse &) {}).isOk());
        CHECK(f.api.endpointCount() == 1);

        // Stored path is the absolute URL the client hits.
        const JsonObject cat = f.api.toCatalog();
        CHECK(cat.getArray("endpoints").getObject(0).getString("path") == String("/api/health"));

        // Same path + method — duplicate, must surface Error::Exists.
        Error err = f.api.route(ep, [](const HttpRequest &, HttpResponse &) {});
        CHECK(err == Error::Exists);
        CHECK(f.api.endpointCount() == 1);

        // Same path, different method — fine.
        ep.method = HttpMethod::Post;
        CHECK(f.api.route(ep, [](const HttpRequest &, HttpResponse &) {}).isOk());
        CHECK(f.api.endpointCount() == 2);
}

TEST_CASE("HttpApi: catalog reflects registered endpoints") {
        ApiFixture f;

        HttpApi::Endpoint ep;
        ep.path = "/items/{id}"; // relative; resolves to /api/items/{id}
        ep.method = HttpMethod::Get;
        ep.title = "Get item";
        ep.summary = "Fetch an item by id.";
        ep.tags = {"items"};
        ep.params = {HttpApi::Param{
                .name = "id",
                .in = HttpApi::ParamIn::Path,
                .required = true,
                .spec = VariantSpec().setType(Variant::TypeS32).setDescription("Item ID."),
        }};
        ep.response = VariantSpec().setType(Variant::TypeString);
        REQUIRE(f.api.route(ep, [](const HttpRequest &, HttpResponse &) {}).isOk());

        const JsonObject cat = f.api.toCatalog();
        const JsonArray  endpoints = cat.getArray("endpoints");
        REQUIRE(endpoints.size() == 1);
        const JsonObject e = endpoints.getObject(0);
        CHECK(e.getString("path") == String("/api/items/{id}"));
        CHECK(e.getString("method") == String("GET"));
        CHECK(e.getString("title") == String("Get item"));
        const JsonArray params = e.getArray("params");
        REQUIRE(params.size() == 1);
        const JsonObject p = params.getObject(0);
        CHECK(p.getString("name") == String("id"));
        CHECK(p.getString("in") == String("path"));
        CHECK(p.getBool("required"));
}

TEST_CASE("HttpApi: openapi document has the required top-level keys") {
        ApiFixture f;
        f.api.setTitle("My API");
        f.api.setVersion("1.2.3");
        f.api.addServer("http://example.com", "prod");

        HttpApi::Endpoint ep;
        ep.path = "/ping"; // relative; resolves to /api/ping
        ep.method = HttpMethod::Get;
        ep.title = "Ping";
        ep.response = VariantSpec().setType(Variant::TypeString);
        REQUIRE(f.api.route(ep, [](const HttpRequest &, HttpResponse &) {}).isOk());

        const JsonObject doc = f.api.toOpenApi();
        CHECK(doc.getString("openapi") == String("3.1.0"));
        const JsonObject info = doc.getObject("info");
        CHECK(info.getString("title") == String("My API"));
        CHECK(info.getString("version") == String("1.2.3"));

        const JsonArray servers = doc.getArray("servers");
        REQUIRE(servers.size() == 1);
        CHECK(servers.getObject(0).getString("url") == String("http://example.com"));

        // paths /api/ping → get → 200.
        const JsonObject paths = doc.getObject("paths");
        REQUIRE(paths.contains("/api/ping"));
        const JsonObject pingPath = paths.getObject("/api/ping");
        REQUIRE(pingPath.contains("get"));
        const JsonObject getOp = pingPath.getObject("get");
        const JsonObject responses = getOp.getObject("responses");
        REQUIRE(responses.contains("200"));
}

TEST_CASE("HttpApi::variantSpecToJsonSchema: native scalar mappings") {
        // bool → {"type":"boolean"}.
        {
                VariantSpec s = VariantSpec().setType(Variant::TypeBool);
                JsonObject  sch = HttpApi::variantSpecToJsonSchema(s);
                CHECK(sch.getString("type") == String("boolean"));
        }
        // u32 → {"type":"integer","minimum":0}.
        {
                VariantSpec s = VariantSpec().setType(Variant::TypeU32);
                JsonObject  sch = HttpApi::variantSpecToJsonSchema(s);
                CHECK(sch.getString("type") == String("integer"));
                CHECK(sch.getInt("minimum") == 0);
        }
        // s32 with a range → {"type":"integer","minimum":-5,"maximum":5}.
        {
                VariantSpec s = VariantSpec().setType(Variant::TypeS32).setRange(-5, 5);
                JsonObject  sch = HttpApi::variantSpecToJsonSchema(s);
                CHECK(sch.getString("type") == String("integer"));
                CHECK(sch.getInt("minimum") == -5);
                CHECK(sch.getInt("maximum") == 5);
        }
        // double → {"type":"number"}.
        {
                VariantSpec s = VariantSpec().setType(Variant::TypeDouble);
                JsonObject  sch = HttpApi::variantSpecToJsonSchema(s);
                CHECK(sch.getString("type") == String("number"));
        }
        // String → {"type":"string"}.
        {
                VariantSpec s = VariantSpec().setType(Variant::TypeString);
                JsonObject  sch = HttpApi::variantSpecToJsonSchema(s);
                CHECK(sch.getString("type") == String("string"));
        }
        // StringList → {"type":"array","items":{"type":"string"}}.
        {
                VariantSpec s = VariantSpec().setType(Variant::TypeStringList);
                JsonObject  sch = HttpApi::variantSpecToJsonSchema(s);
                CHECK(sch.getString("type") == String("array"));
                CHECK(sch.getObject("items").getString("type") == String("string"));
        }
}

TEST_CASE("HttpApi::variantSpecToJsonSchema: domain-specific format extensions") {
        // UUID → {"type":"string","format":"uuid"}.
        {
                VariantSpec s = VariantSpec().setType(Variant::TypeUUID);
                JsonObject  sch = HttpApi::variantSpecToJsonSchema(s);
                CHECK(sch.getString("type") == String("string"));
                CHECK(sch.getString("format") == String("uuid"));
        }
        // PixelFormat → string + promeki-pixelformat.
        {
                VariantSpec s = VariantSpec().setType(Variant::TypePixelFormat);
                JsonObject  sch = HttpApi::variantSpecToJsonSchema(s);
                CHECK(sch.getString("type") == String("string"));
                CHECK(sch.getString("format") == String("promeki-pixelformat"));
        }
        // Url → {"type":"string","format":"uri"}.
        {
                VariantSpec s = VariantSpec().setType(Variant::TypeUrl);
                JsonObject  sch = HttpApi::variantSpecToJsonSchema(s);
                CHECK(sch.getString("type") == String("string"));
                CHECK(sch.getString("format") == String("uri"));
        }
        // DateTime → {"type":"string","format":"date-time"}.
        {
                VariantSpec s = VariantSpec().setType(Variant::TypeDateTime);
                JsonObject  sch = HttpApi::variantSpecToJsonSchema(s);
                CHECK(sch.getString("type") == String("string"));
                CHECK(sch.getString("format") == String("date-time"));
        }
}

TEST_CASE("HttpApi::variantSpecToJsonSchema: complex types $ref into components") {
        JsonObject  components;
        VariantSpec s = VariantSpec().setType(Variant::TypeRational);
        JsonObject  sch = HttpApi::variantSpecToJsonSchema(s, &components);
        CHECK(sch.getString("$ref") == String("#/components/schemas/Rational"));
        REQUIRE(components.contains("Rational"));
        const JsonObject def = components.getObject("Rational");
        CHECK(def.getString("type") == String("object"));
        CHECK(def.getObject("properties").contains("num"));
        CHECK(def.getObject("properties").contains("den"));
}

TEST_CASE("HttpApi::variantSpecToJsonSchema: polymorphic spec emits oneOf") {
        VariantSpec s = VariantSpec().setTypes({
                Variant::TypeString,
                Variant::TypeS32,
        });
        JsonObject  sch = HttpApi::variantSpecToJsonSchema(s);
        REQUIRE(sch.contains("oneOf"));
        const JsonArray arr = sch.getArray("oneOf");
        REQUIRE(arr.size() == 2);
        CHECK(arr.getObject(0).getString("type") == String("string"));
        CHECK(arr.getObject(1).getString("type") == String("integer"));
}

TEST_CASE("HttpApi::variantSpecToJsonSchema: enum spec emits enum values") {
        // HttpMethod is a registered TypedEnum — perfect for this test
        // because we know its members.
        VariantSpec s = VariantSpec().setType(Variant::TypeEnum).setEnumType(Enum::findType("HttpMethod"));
        REQUIRE(Enum::findType("HttpMethod").isValid());

        JsonObject sch = HttpApi::variantSpecToJsonSchema(s);
        CHECK(sch.getString("type") == String("string"));
        CHECK(sch.getString("x-promeki-enum-type") == String("HttpMethod"));
        REQUIRE(sch.contains("enum"));
        const JsonArray enumValues = sch.getArray("enum");
        // RFC 9110 has nine well-known methods; we just check the
        // common ones are present rather than the exact count, in
        // case TypedEnum accumulates more later.
        bool hasGet = false;
        bool hasPost = false;
        for (int i = 0; i < enumValues.size(); ++i) {
                const String v = enumValues.getString(i);
                if (v == String("GET")) hasGet = true;
                if (v == String("POST")) hasPost = true;
        }
        CHECK(hasGet);
        CHECK(hasPost);
}

TEST_CASE("HttpApi: rpc() unmarshals args and renders result") {
        ApiFixture f;

        HttpApi::Endpoint ep;
        ep.path = "/add"; // relative; resolves to /api/add
        ep.method = HttpMethod::Post;
        ep.title = "Add two integers";
        ep.params = {
                HttpApi::Param{
                        .name = "a",
                        .in = HttpApi::ParamIn::Body,
                        .required = true,
                        .spec = VariantSpec().setType(Variant::TypeS32),
                },
                HttpApi::Param{
                        .name = "b",
                        .in = HttpApi::ParamIn::Body,
                        .required = true,
                        .spec = VariantSpec().setType(Variant::TypeS32),
                },
        };
        ep.response = VariantSpec().setType(Variant::TypeS32);
        REQUIRE(f.api.rpc(ep, [](const VariantMap &args) -> Result<Variant> {
                             Error   e;
                             int32_t a = args.value("a").get<int32_t>(&e);
                             int32_t b = args.value("b").get<int32_t>(&e);
                             return makeResult<Variant>(a + b);
                     }).isOk());

        // The catalog should reflect the registered endpoint.
        CHECK(f.api.endpointCount() == 1);
        const JsonObject doc = f.api.toOpenApi();
        const JsonObject paths = doc.getObject("paths");
        REQUIRE(paths.contains("/api/add"));
        const JsonObject postOp = paths.getObject("/api/add").getObject("post");
        REQUIRE(postOp.contains("requestBody"));
}

TEST_CASE("HttpApi::installPromekiAPI registers the standard surface") {
        ApiFixture f; // prefix is "/api"
        REQUIRE(f.api.installPromekiAPI().isOk());
        CHECK(f.api.endpointCount() > 0);

        // Each module nests under <prefix>/promeki/<module>.
        const JsonObject cat = f.api.toCatalog();
        const JsonArray  endpoints = cat.getArray("endpoints");
        bool             sawBuild = false, sawEnv = false, sawMem = false, sawLog = false;
        for (int i = 0; i < endpoints.size(); ++i) {
                const String path = endpoints.getObject(i).getString("path");
                if (path == String("/api/promeki/build")) sawBuild = true;
                if (path == String("/api/promeki/env")) sawEnv = true;
                if (path == String("/api/promeki/memspace")) sawMem = true;
                if (path == String("/api/promeki/log")) sawLog = true;
        }
        CHECK(sawBuild);
        CHECK(sawEnv);
        CHECK(sawMem);
        CHECK(sawLog);
}
