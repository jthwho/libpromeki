/**
 * @file      promeki_pipeline_api.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Doctest coverage for the promeki-pipeline demo's Phase D REST +
 * WebSocket API.  Each test spins up an HttpServer + PipelineManager
 * + ApiRoutes (and, where needed, an EventBroadcaster) inside a worker
 * thread, drives a hand-rolled HTTP/1.1 client against it, and checks
 * the wire shapes the frontend will consume.
 */

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include <doctest/doctest.h>

#include <promeki/elapsedtimer.h>
#include <promeki/error.h>
#include <promeki/eventloop.h>
#include <promeki/httpheaders.h>
#include <promeki/httpserver.h>
#include <promeki/iodevice.h>
#include <promeki/json.h>
#include <promeki/list.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediapipelineconfig.h>
#include <promeki/objectbase.tpp>
#include <promeki/pixelformat.h>
#include <promeki/rational.h>
#include <promeki/regex.h>
#include <promeki/socketaddress.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/tcpsocket.h>
#include <promeki/thread.h>
#include <promeki/videoformat.h>

// Headers are exposed via the promeki-pipeline-core static library's
// PUBLIC include directory (demos/promeki-pipeline/ at configure time).
#include "apiroutes.h"
#include "eventbroadcaster.h"
#include "pipelinemanager.h"
#include "pipelinesettings.h"

using namespace promeki;
using promekipipeline::ApiRoutes;
using promekipipeline::EventBroadcaster;
using promekipipeline::PipelineManager;
using promekipipeline::PipelineSettings;

namespace {

        // ----------------------------------------------------------------
        // Shared fixture that runs HttpServer / PipelineManager / ApiRoutes
        // (+ optional EventBroadcaster) on a worker thread.
        // ----------------------------------------------------------------
        struct ApiFixture {
                        Thread            thread;
                        HttpServer       *server = nullptr;
                        PipelineManager  *manager = nullptr;
                        ApiRoutes        *routes = nullptr;
                        EventBroadcaster *broadcaster = nullptr;
                        uint16_t          port = 0;

                        ApiFixture(bool wantBroadcaster = false) {
                                thread.start();
                                bool done = false;
                                thread.threadEventLoop()->postCallable([this, &done, wantBroadcaster]() {
                                        server = new HttpServer();
                                        manager = new PipelineManager();
                                        routes = new ApiRoutes(*server, *manager);
                                        if (wantBroadcaster) {
                                                broadcaster = new EventBroadcaster(*server, *manager);
                                        }
                                        Error err = server->listen(SocketAddress::localhost(0));
                                        REQUIRE(err.isOk());
                                        port = server->serverAddress().port();
                                        done = true;
                                });
                                for (int i = 0; i < 1000 && !done; ++i) {
                                        Thread::sleepMs(1);
                                }
                                REQUIRE(done);
                                REQUIRE(port != 0);
                        }

                        ~ApiFixture() {
                                bool done = false;
                                thread.threadEventLoop()->postCallable([this, &done]() {
                                        delete broadcaster;
                                        broadcaster = nullptr;
                                        delete routes;
                                        routes = nullptr;
                                        delete manager;
                                        manager = nullptr;
                                        delete server;
                                        server = nullptr;
                                        done = true;
                                });
                                for (int i = 0; i < 1000 && !done; ++i) {
                                        Thread::sleepMs(1);
                                }
                                thread.quit();
                                thread.wait(2000);
                        }
        };

        // Minimal blocking HTTP/1.1 client.  Mirrors tests/unit/network/httpserver.cpp.
        struct HttpRsp {
                        int         status = 0;
                        HttpHeaders headers;
                        String      body;
        };

        HttpRsp doRequest(uint16_t port, const String &method, const String &path, const String &body = String(),
                          const String &contentType = String()) {
                TcpSocket sock;
                sock.open(IODevice::ReadWrite);
                Error err = sock.connectToHost(SocketAddress::localhost(port));
                REQUIRE(err.isOk());

                String req = method + " " + path + " HTTP/1.1\r\n";
                req += "Host: localhost\r\n";
                req += "Connection: close\r\n";
                if (!body.isEmpty()) {
                        req += String::sprintf("Content-Length: %zu\r\n", body.byteCount());
                }
                if (!contentType.isEmpty()) {
                        req += String("Content-Type: ") + contentType + "\r\n";
                }
                req += "\r\n";
                req += body;

                const int64_t n = sock.write(req.cstr(), req.byteCount());
                REQUIRE(n == static_cast<int64_t>(req.byteCount()));

                String raw;
                char   buf[8192];
                for (;;) {
                        int64_t got = sock.read(buf, sizeof(buf));
                        if (got <= 0) break;
                        raw += String(buf, static_cast<size_t>(got));
                }
                sock.close();

                HttpRsp      out;
                const size_t sep = raw.find("\r\n\r\n");
                REQUIRE(sep != String::npos);
                const String head = raw.left(sep);
                out.body = raw.mid(sep + 4);

                const size_t eol = head.find("\r\n");
                const String statusLine = (eol == String::npos) ? head : head.left(eol);
                const size_t sp1 = statusLine.find(' ');
                const size_t sp2 = statusLine.find(' ', sp1 + 1);
                out.status = std::atoi(statusLine.mid(sp1 + 1, sp2 - sp1 - 1).cstr());

                const String headerBlock = (eol == String::npos) ? String() : head.mid(eol + 2);
                StringList   lines = headerBlock.split("\r\n");
                for (size_t i = 0; i < lines.size(); ++i) {
                        const String &ln = lines[i];
                        if (ln.isEmpty()) continue;
                        const size_t colon = ln.find(':');
                        if (colon == String::npos) continue;
                        String name = ln.left(colon);
                        String value = ln.mid(colon + 1);
                        if (!value.isEmpty() && value.cstr()[0] == ' ') value = value.mid(1);
                        out.headers.add(name, value);
                }
                return out;
        }

        // Build a simple TPG → NullPacing user config for CRUD lifecycle tests.
        // Each stage starts from the backend's defaultConfig so the resulting
        // MediaIO has every required key set (TPG in particular needs
        // VideoEnabled / AudioEnabled / etc. to make it past open()).
        MediaPipelineConfig makeTpgToNullPacingConfig() {
                MediaPipelineConfig        cfg;
                MediaPipelineConfig::Stage src;
                src.name = "tpg1";
                src.type = "TPG";
                src.role = MediaPipelineConfig::StageRole::Source;
                src.config = MediaIOFactory::defaultConfig("TPG");
                src.config.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte720p60));
                cfg.addStage(src);

                MediaPipelineConfig::Stage sink;
                sink.name = "sink1";
                sink.type = "NullPacing";
                sink.role = MediaPipelineConfig::StageRole::Sink;
                sink.config = MediaIOFactory::defaultConfig("NullPacing");
                cfg.addStage(sink);

                cfg.addRoute("tpg1", "sink1");
                // Don't set a frame-count limit: the test wants to exercise
                // explicit stop / close transitions, which require the
                // pipeline to still be Running when those calls fire.
                return cfg;
        }

        // Build a TPG → MjpegStream config so the planner is forced to insert
        // a CSC bridge.  TPG's default RGB8_sRGB output IS directly accepted
        // by the JPEG encoder; selecting BGRA8_sRGB (which the encoder does
        // not natively take) forces autoplan to insert a CSC stage.
        MediaPipelineConfig makeTpgToMjpegConfig() {
                MediaPipelineConfig        cfg;
                MediaPipelineConfig::Stage src;
                src.name = "tpg1";
                src.type = "TPG";
                src.role = MediaPipelineConfig::StageRole::Source;
                src.config = MediaIOFactory::defaultConfig("TPG");
                src.config.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte720p60));
                src.config.set(MediaConfig::VideoPixelFormat, PixelFormat(PixelFormat::BGRA8_sRGB));
                cfg.addStage(src);

                MediaPipelineConfig::Stage sink;
                sink.name = "preview";
                sink.type = "MjpegStream";
                sink.role = MediaPipelineConfig::StageRole::Sink;
                sink.config = MediaIOFactory::defaultConfig("MjpegStream");
                sink.config.set(MediaConfig::MjpegMaxFps, Rational<int>(30, 1));
                sink.config.set(MediaConfig::MjpegQuality, int32_t(80));
                cfg.addStage(sink);

                cfg.addRoute("tpg1", "preview");
                return cfg;
        }

} // namespace

// ===============================================================
// Type-registry routes
// ===============================================================

TEST_CASE("ApiRoutes - GET /api/types lists registered backends") {
        ApiFixture fix;
        auto       rsp = doRequest(fix.port, "GET", "/api/types");
        CHECK(rsp.status == 200);

        Error     perr;
        JsonArray arr = JsonArray::parse(rsp.body, &perr);
        REQUIRE(perr.isOk());
        REQUIRE(arr.size() > 0);

        bool   sawTpg = false, sawNullPacing = false, sawMjpeg = false;
        String tpgDisplay;
        for (int i = 0; i < arr.size(); ++i) {
                JsonObject   e = arr.getObject(i);
                const String name = e.getString("name");
                if (name == "TPG") {
                        sawTpg = true;
                        tpgDisplay = e.getString("displayName");
                }
                if (name == "NullPacing") sawNullPacing = true;
                if (name == "MjpegStream") sawMjpeg = true;
        }
        CHECK(sawTpg);
        CHECK(sawNullPacing);
        CHECK(sawMjpeg);
        // MediaIOFactory::displayName is now part of the API contract.
        // Empty falls back to the canonical name on the backend, so
        // the field should always be non-empty for registered types.
        CHECK_FALSE(tpgDisplay.isEmpty());
}

TEST_CASE("ApiRoutes - GET /api/types/{name}/schema returns spec map") {
        ApiFixture fix;

        auto rspTpg = doRequest(fix.port, "GET", "/api/types/TPG/schema");
        CHECK(rspTpg.status == 200);
        Error      perr;
        JsonObject schema = JsonObject::parse(rspTpg.body, &perr);
        REQUIRE(perr.isOk());
        CHECK(schema.size() > 0);

        auto rspMjpeg = doRequest(fix.port, "GET", "/api/types/MjpegStream/schema");
        CHECK(rspMjpeg.status == 200);
        JsonObject mjpegSchema = JsonObject::parse(rspMjpeg.body, &perr);
        REQUIRE(perr.isOk());
        CHECK(mjpegSchema.contains("MjpegMaxFps"));
        CHECK(mjpegSchema.contains("MjpegQuality"));
        CHECK(mjpegSchema.contains("MjpegMaxQueueFrames"));

        auto rspMissing = doRequest(fix.port, "GET", "/api/types/DefinitelyNotRegistered/schema");
        CHECK(rspMissing.status == 404);
}

TEST_CASE("ApiRoutes - schema emits enum block for TypeRegistry types") {
        ApiFixture fix;

        // CSC's OutputPixelFormat is a PixelFormat-typed spec.  The
        // backend serializer should synthesize an `enum: {type, values}`
        // block from PixelFormat::registeredIDs() so the frontend can
        // render a dropdown without any per-backend code change.
        auto rsp = doRequest(fix.port, "GET", "/api/types/CSC/schema");
        REQUIRE(rsp.status == 200);
        Error      perr;
        JsonObject schema = JsonObject::parse(rsp.body, &perr);
        REQUIRE(perr.isOk());
        REQUIRE(schema.contains("OutputPixelFormat"));

        JsonObject entry = schema.getObject("OutputPixelFormat", &perr);
        REQUIRE(perr.isOk());
        REQUIRE(entry.contains("enum"));

        JsonObject enumInfo = entry.getObject("enum", &perr);
        REQUIRE(perr.isOk());
        CHECK(enumInfo.getString("type") == "PixelFormat");
        JsonArray values = enumInfo.getArray("values", &perr);
        REQUIRE(perr.isOk());
        REQUIRE(values.size() > 0);

        // Confirm a well-known value is present.  RGBA8_sRGB is one
        // of the unconditionally-registered PixelFormat IDs.
        bool sawWellKnown = false;
        for (int i = 0; i < values.size(); ++i) {
                if (values.getString(i) == "RGBA8_sRGB") {
                        sawWellKnown = true;
                        break;
                }
        }
        CHECK(sawWellKnown);
}

TEST_CASE("ApiRoutes - schema emits presets block for FrameRate-typed specs") {
        ApiFixture fix;

        // FrameSync's OutputFrameRate is a FrameRate-typed spec.  The
        // backend serializer should attach a `presets: [{label, value}]`
        // block sourced from FrameRate::wellKnownRates() so the frontend
        // can render the FrameRate dropdown without any per-backend or
        // per-spec code change.
        auto rsp = doRequest(fix.port, "GET", "/api/types/FrameSync/schema");
        REQUIRE(rsp.status == 200);
        Error      perr;
        JsonObject schema = JsonObject::parse(rsp.body, &perr);
        REQUIRE(perr.isOk());
        REQUIRE(schema.contains("OutputFrameRate"));

        JsonObject entry = schema.getObject("OutputFrameRate", &perr);
        REQUIRE(perr.isOk());
        REQUIRE(entry.contains("presets"));

        JsonArray presets = entry.getArray("presets", &perr);
        REQUIRE(perr.isOk());
        REQUIRE(presets.size() > 0);

        // Confirm a well-known entry is present.  "24" / "24/1" is one
        // of the canonical well-known FrameRate values.
        bool sawTwentyFour = false;
        for (int i = 0; i < presets.size(); ++i) {
                JsonObject preset = presets.getObject(i, &perr);
                if (perr.isError()) continue;
                if (preset.getString("label") == "24" && preset.getString("value") == "24/1") {
                        sawTwentyFour = true;
                        break;
                }
        }
        CHECK(sawTwentyFour);
}

TEST_CASE("ApiRoutes - defaults and metadata routes") {
        ApiFixture fix;
        auto       defRsp = doRequest(fix.port, "GET", "/api/types/TPG/defaults");
        CHECK(defRsp.status == 200);
        Error      perr;
        JsonObject def = JsonObject::parse(defRsp.body, &perr);
        REQUIRE(perr.isOk());

        auto metaRsp = doRequest(fix.port, "GET", "/api/types/TPG/metadata");
        CHECK(metaRsp.status == 200);
        // Metadata may legitimately be empty for some backends; just
        // check the response parses.
        JsonObject meta = JsonObject::parse(metaRsp.body, &perr);
        REQUIRE(perr.isOk());
        (void)def;
        (void)meta;

        auto bad = doRequest(fix.port, "GET", "/api/types/Bogus/defaults");
        CHECK(bad.status == 404);
}

// ===============================================================
// Pipeline CRUD lifecycle
// ===============================================================

TEST_CASE("ApiRoutes - CRUD lifecycle for a TPG to NullPacing pipeline") {
        ApiFixture fix;

        // Empty list to start.
        auto list0 = doRequest(fix.port, "GET", "/api/pipelines");
        CHECK(list0.status == 200);
        Error     perr;
        JsonArray arr0 = JsonArray::parse(list0.body, &perr);
        REQUIRE(perr.isOk());
        const int initialCount = arr0.size();

        // Create with full body (settings + userConfig).
        JsonObject body;
        body.set("name", String("tpg-to-null"));
        body.set("settings", PipelineSettings().toJson());
        body.set("userConfig", makeTpgToNullPacingConfig().toJson());

        auto created = doRequest(fix.port, "POST", "/api/pipelines", body.toString(0), "application/json");
        CHECK(created.status == 201);
        JsonObject createdJson = JsonObject::parse(created.body, &perr);
        REQUIRE(perr.isOk());
        const String id = createdJson.getString("id");
        CHECK(!id.isEmpty());

        // GET /api/pipelines/{id} → state Empty.
        auto desc = doRequest(fix.port, "GET", String("/api/pipelines/") + id);
        CHECK(desc.status == 200);
        JsonObject descJson = JsonObject::parse(desc.body, &perr);
        REQUIRE(perr.isOk());
        CHECK(descJson.getString("state") == "Empty");
        CHECK(descJson.getString("name") == "tpg-to-null");

        // List now has one more entry.
        auto list1 = doRequest(fix.port, "GET", "/api/pipelines");
        CHECK(list1.status == 200);
        JsonArray arr1 = JsonArray::parse(list1.body, &perr);
        REQUIRE(perr.isOk());
        CHECK(arr1.size() == initialCount + 1);

        // Build (autoplan default = on).  TPG → NullPacing is
        // directly compatible, so autoplan is a no-op here.
        auto build = doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/build");
        CHECK(build.status == 200);
        JsonObject builtJson = JsonObject::parse(build.body, &perr);
        REQUIRE(perr.isOk());
        CHECK(builtJson.getString("state") == "Built");

        // Open and start.
        auto opened = doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/open");
        CHECK(opened.status == 200);
        auto started = doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/start");
        CHECK(started.status == 200);

        // Let the source pump for a few ticks.
        Thread::sleepMs(200);

        auto stopped = doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/stop");
        CHECK(stopped.status == 200);
        auto closed = doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/close");
        CHECK(closed.status == 200);

        auto del = doRequest(fix.port, "DELETE", String("/api/pipelines/") + id);
        CHECK(del.status == 204);

        auto descMissing = doRequest(fix.port, "GET", String("/api/pipelines/") + id);
        CHECK(descMissing.status == 404);
}

TEST_CASE("ApiRoutes - autoplan inserts bridge stage for TPG to MjpegStream") {
        ApiFixture fix;

        JsonObject body;
        body.set("settings", PipelineSettings().toJson());
        body.set("userConfig", makeTpgToMjpegConfig().toJson());
        auto created = doRequest(fix.port, "POST", "/api/pipelines", body.toString(0), "application/json");
        CHECK(created.status == 201);
        Error      perr;
        JsonObject c = JsonObject::parse(created.body, &perr);
        REQUIRE(perr.isOk());
        const String id = c.getString("id");

        auto build = doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/build");
        CHECK(build.status == 200);
        JsonObject buildJson = JsonObject::parse(build.body, &perr);
        REQUIRE(perr.isOk());

        // userConfig must not contain the auto-inserted bridge.
        // MediaPipelinePlanner names bridges "br<N>_<from>_<to>"
        // (see src/proav/mediapipelineplanner.cpp), so we match the
        // common prefix.
        const RegEx bridgeName("^br[0-9]+_.+_.+$");
        JsonObject  userCfg = buildJson.getObject("userConfig");
        Error       e2;
        JsonArray   userStages = userCfg.getArray("stages", &e2);
        REQUIRE(e2.isOk());
        bool userHasBridge = false;
        for (int i = 0; i < userStages.size(); ++i) {
                const String n = userStages.getObject(i).getString("name");
                if (bridgeName.match(n)) userHasBridge = true;
        }
        CHECK(!userHasBridge);

        // resolvedConfig must contain at least one bridge stage.
        JsonObject resolvedCfg = buildJson.getObject("resolvedConfig");
        JsonArray  resolvedStages = resolvedCfg.getArray("stages", &e2);
        REQUIRE(e2.isOk());
        bool resolvedHasBridge = false;
        for (int i = 0; i < resolvedStages.size(); ++i) {
                const String n = resolvedStages.getObject(i).getString("name");
                if (bridgeName.match(n)) resolvedHasBridge = true;
        }
        CHECK(resolvedHasBridge);

        // Tear down.
        doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/close");
        doRequest(fix.port, "DELETE", String("/api/pipelines/") + id);
}

// ===============================================================
// Run-macro lifecycle helper
// ===============================================================

namespace {

        // Helper: poll the describe state for up to `timeoutMs` looking for
        // the requested name.  PipelineEvents drive state transitions across
        // thread boundaries via the pipeline's strand and the MediaPipeline
        // loop, so the HTTP response that says "200" can race ever-so-slightly
        // ahead of the published state.  The run-macro tests want a stable
        // "we reached Running" check.
        String waitForState(uint16_t port, const String &id, const String &want, int timeoutMs) {
                ElapsedTimer t;
                t.start();
                String last;
                while (t.elapsed() < timeoutMs) {
                        auto rsp = doRequest(port, "GET", String("/api/pipelines/") + id);
                        if (rsp.status == 200) {
                                Error      pe;
                                JsonObject obj = JsonObject::parse(rsp.body, &pe);
                                if (pe.isOk()) {
                                        last = obj.getString("state");
                                        if (last == want) return last;
                                }
                        }
                        Thread::sleepMs(10);
                }
                return last;
        }

        // Reach the supplied lifecycle state through the explicit verbs.
        // Returns the describe state as observed after the cascade settles.
        String driveTo(uint16_t port, const String &id, const String &target) {
                const auto post = [&](const String &verb) {
                        return doRequest(port, "POST", String("/api/pipelines/") + id + "/" + verb);
                };

                if (target == "Empty") return waitForState(port, id, "Empty", 1000);
                if (target == "Built") {
                        REQUIRE(post("build").status == 200);
                        return waitForState(port, id, "Built", 1000);
                }
                if (target == "Open") {
                        REQUIRE(post("build").status == 200);
                        REQUIRE(post("open").status == 200);
                        return waitForState(port, id, "Open", 1000);
                }
                if (target == "Stopped") {
                        REQUIRE(post("build").status == 200);
                        REQUIRE(post("open").status == 200);
                        REQUIRE(post("start").status == 200);
                        Thread::sleepMs(80);
                        REQUIRE(post("stop").status == 200);
                        return waitForState(port, id, "Stopped", 1000);
                }
                if (target == "Closed") {
                        REQUIRE(post("build").status == 200);
                        REQUIRE(post("open").status == 200);
                        REQUIRE(post("start").status == 200);
                        Thread::sleepMs(80);
                        REQUIRE(post("stop").status == 200);
                        REQUIRE(post("close").status == 200);
                        return waitForState(port, id, "Closed", 1000);
                }
                return String();
        }

        // Create + return a fresh pipeline id, with the supplied user config
        // loaded.  Helper for run-macro coverage that needs to spin up many
        // independent pipelines without re-implementing the boilerplate.
        String createPipelineWithConfig(uint16_t port, const String &name, const MediaPipelineConfig &cfg) {
                JsonObject body;
                body.set("name", name);
                body.set("userConfig", cfg.toJson());
                auto created = doRequest(port, "POST", "/api/pipelines", body.toString(0), "application/json");
                REQUIRE(created.status == 201);
                Error      perr;
                JsonObject c = JsonObject::parse(created.body, &perr);
                REQUIRE(perr.isOk());
                return c.getString("id");
        }

} // namespace

TEST_CASE("ApiRoutes - POST /run drives every reachable state to Running") {
        ApiFixture fix;

        struct Case {
                        const char *startState;
        };
        const List<Case> cases = {
                {"Empty"}, {"Built"}, {"Open"}, {"Stopped"}, {"Closed"},
        };

        for (size_t i = 0; i < cases.size(); ++i) {
                const String target = String(cases[i].startState);
                const String name = String("run-from-") + target;
                const String id = createPipelineWithConfig(fix.port, name, makeTpgToNullPacingConfig());

                const String reached = driveTo(fix.port, id, target);
                CHECK_MESSAGE(reached == target, ("driveTo(" + target + ") observed " + reached).cstr());

                auto run = doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/run");
                CHECK_MESSAGE(run.status == 200, ("/run from " + target + " returned non-200").cstr());

                // After /run, the pipeline must be Running.  Allow a
                // short window for the strand to publish StateChanged.
                const String settled = waitForState(fix.port, id, "Running", 1500);
                CHECK_MESSAGE(settled == "Running", ("/run from " + target + " ended in " + settled).cstr());

                // Tear down so each case starts fresh.
                doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/stop");
                doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/close");
                doRequest(fix.port, "DELETE", String("/api/pipelines/") + id);
        }
}

TEST_CASE("ApiRoutes - POST /run from Running is a no-op") {
        ApiFixture   fix;
        const String id = createPipelineWithConfig(fix.port, "run-noop", makeTpgToNullPacingConfig());

        // Drive to Running explicitly.
        REQUIRE(doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/build").status == 200);
        REQUIRE(doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/open").status == 200);
        REQUIRE(doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/start").status == 200);
        REQUIRE(waitForState(fix.port, id, "Running", 1000) == "Running");

        // /run on a Running pipeline returns 200 + describe and leaves
        // the state alone.
        auto run = doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/run");
        CHECK(run.status == 200);
        CHECK(waitForState(fix.port, id, "Running", 200) == "Running");

        doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/stop");
        doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/close");
        doRequest(fix.port, "DELETE", String("/api/pipelines/") + id);
}

TEST_CASE("ApiRoutes - POST /run on unknown id returns 404") {
        ApiFixture fix;
        auto       rsp = doRequest(fix.port, "POST", "/api/pipelines/ZZZZZZZZ/run");
        CHECK(rsp.status == 404);
}

// ===============================================================
// Settings round-trip
// ===============================================================

TEST_CASE("ApiRoutes - PUT settings round-trips and is observable on GET") {
        ApiFixture fix;

        JsonObject body;
        body.set("name", String("settings-rt"));
        auto created = doRequest(fix.port, "POST", "/api/pipelines", body.toString(0), "application/json");
        CHECK(created.status == 201);
        Error      perr;
        JsonObject c = JsonObject::parse(created.body, &perr);
        REQUIRE(perr.isOk());
        const String id = c.getString("id");

        // Build a new settings block with a non-default stats interval.
        PipelineSettings s;
        s.setName("renamed");
        s.setStatsInterval(Duration::fromMilliseconds(500));
        s.setAutoplan(false);

        auto put = doRequest(fix.port, "PUT", String("/api/pipelines/") + id + "/settings", s.toJson().toString(0),
                             "application/json");
        CHECK(put.status == 200);
        JsonObject putJson = JsonObject::parse(put.body, &perr);
        REQUIRE(perr.isOk());
        CHECK(putJson.getString("name") == "renamed");
        CHECK(putJson.getInt("statsIntervalMs") == 500);
        CHECK(putJson.getBool("autoplan") == false);

        auto get = doRequest(fix.port, "GET", String("/api/pipelines/") + id + "/settings");
        CHECK(get.status == 200);
        JsonObject getJson = JsonObject::parse(get.body, &perr);
        REQUIRE(perr.isOk());
        CHECK(getJson.getString("name") == "renamed");
        CHECK(getJson.getInt("statsIntervalMs") == 500);
        CHECK(getJson.getBool("autoplan") == false);

        doRequest(fix.port, "DELETE", String("/api/pipelines/") + id);
}

// ===============================================================
// WebSocket events
// ===============================================================

namespace {

        // Tiny WebSocket client built on TcpSocket.  We re-use the same shape
        // the existing websocket tests use: send a hand-built upgrade,
        // validate the 101, then read one frame at a time.  Only single-frame
        // unmasked text payloads up to 125 bytes inline / 64k extended are
        // expected from the broadcaster (its envelope is small).
        struct WsClient {
                        TcpSocket sock;
                        Buffer    pending; // unconsumed bytes from previous reads.
                        size_t    pendingLen = 0;

                        bool open(uint16_t port, const String &path) {
                                sock.open(IODevice::ReadWrite);
                                if (sock.connectToHost(SocketAddress::localhost(port)).isError()) {
                                        return false;
                                }
                                const String req = String("GET ") + path +
                                                   " HTTP/1.1\r\n"
                                                   "Host: localhost\r\n"
                                                   "Upgrade: websocket\r\n"
                                                   "Connection: Upgrade\r\n"
                                                   "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                                                   "Sec-WebSocket-Version: 13\r\n"
                                                   "\r\n";
                                if (sock.write(req.cstr(), req.byteCount()) != static_cast<int64_t>(req.byteCount())) {
                                        return false;
                                }
                                // Drain the 101 response headers (no body).
                                String       head;
                                ElapsedTimer t;
                                t.start();
                                while (t.elapsed() < 2000) {
                                        char    buf[1024];
                                        int64_t got = sock.read(buf, sizeof(buf));
                                        if (got > 0) {
                                                head += String(buf, static_cast<size_t>(got));
                                                const size_t sep = head.find("\r\n\r\n");
                                                if (sep != String::npos) {
                                                        // Everything after sep+4 is the
                                                        // beginning of the WS frame
                                                        // stream.
                                                        const String tail = head.mid(sep + 4);
                                                        pending = Buffer(tail.byteCount() + 64);
                                                        pendingLen = tail.byteCount();
                                                        std::memcpy(pending.data(), tail.cstr(), tail.byteCount());
                                                        return head.contains("101");
                                                }
                                        }
                                        Thread::sleepMs(2);
                                }
                                return false;
                        }

                        // Read a single text frame; returns empty String on timeout.
                        String readTextFrame(int timeoutMs) {
                                ElapsedTimer t;
                                t.start();
                                while (t.elapsed() < timeoutMs) {
                                        // Need at least 2 bytes for the frame header.
                                        if (pendingLen < 2) {
                                                char    buf[4096];
                                                int64_t got = sock.read(buf, sizeof(buf));
                                                if (got > 0) {
                                                        if (pendingLen + static_cast<size_t>(got) >
                                                            pending.allocSize()) {
                                                                Buffer grown(pendingLen + got + 4096);
                                                                std::memcpy(grown.data(), pending.data(), pendingLen);
                                                                pending = std::move(grown);
                                                        }
                                                        std::memcpy(static_cast<uint8_t *>(pending.data()) + pendingLen,
                                                                    buf, got);
                                                        pendingLen += static_cast<size_t>(got);
                                                } else {
                                                        Thread::sleepMs(2);
                                                        continue;
                                                }
                                        }
                                        const uint8_t *b = static_cast<const uint8_t *>(pending.data());
                                        if (pendingLen < 2) continue;
                                        const uint8_t b0 = b[0];
                                        const uint8_t b1 = b[1];
                                        const bool    masked = (b1 & 0x80) != 0;
                                        size_t        plen = b1 & 0x7F;
                                        size_t        headLen = 2;
                                        if (plen == 126) {
                                                if (pendingLen < 4) continue;
                                                plen = (size_t(b[2]) << 8) | b[3];
                                                headLen = 4;
                                        } else if (plen == 127) {
                                                if (pendingLen < 10) continue;
                                                plen = 0;
                                                for (int i = 0; i < 8; ++i) {
                                                        plen = (plen << 8) | b[2 + i];
                                                }
                                                headLen = 10;
                                        }
                                        if (masked) headLen += 4;
                                        if (pendingLen < headLen + plen) continue;
                                        const uint8_t opcode = b0 & 0x0F;
                                        // Skip the masking key bytes (server-to-client
                                        // frames are unmasked but the unrelated bits
                                        // are easy to handle).
                                        const uint8_t *payload = b + headLen;
                                        String         text;
                                        if (opcode == 0x1) {
                                                text = String(reinterpret_cast<const char *>(payload), plen);
                                        }
                                        // Shift the consumed bytes off the front.
                                        const size_t consumed = headLen + plen;
                                        std::memmove(pending.data(), static_cast<uint8_t *>(pending.data()) + consumed,
                                                     pendingLen - consumed);
                                        pendingLen -= consumed;
                                        if (opcode == 0x1) return text;
                                        // Control / continuation frames: keep looping.
                                }
                                return String();
                        }
        };

} // namespace

TEST_CASE("ApiRoutes - WS /api/events delivers state-changed events") {
        ApiFixture fix(/*wantBroadcaster=*/true);

        JsonObject body;
        body.set("name", String("ws-test"));
        body.set("userConfig", makeTpgToNullPacingConfig().toJson());
        auto created = doRequest(fix.port, "POST", "/api/pipelines", body.toString(0), "application/json");
        CHECK(created.status == 201);
        Error      perr;
        JsonObject c = JsonObject::parse(created.body, &perr);
        REQUIRE(perr.isOk());
        const String id = c.getString("id");

        // Connect a filtered subscriber and an unfiltered one.
        WsClient filtered;
        REQUIRE(filtered.open(fix.port, String("/api/events?pipeline=") + id));

        WsClient mismatched;
        REQUIRE(mismatched.open(fix.port, String("/api/events?pipeline=ZZZZZZZZ")));

        // Drive the pipeline through state changes.
        doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/build");
        doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/open");
        doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/start");
        Thread::sleepMs(200);
        doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/stop");
        doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/close");

        // Drain at least one StateChanged + one StageState event from
        // the filtered socket.  Loop a few times to exhaust the WS
        // buffer; the broadcaster also fans out StatsUpdated and Log
        // events so we don't expect a clean sequence, just presence.
        bool         sawStateChanged = false;
        bool         sawStageState = false;
        ElapsedTimer t;
        t.start();
        while (t.elapsed() < 2000 && !(sawStateChanged && sawStageState)) {
                String frame = filtered.readTextFrame(200);
                if (frame.isEmpty()) continue;
                Error      pe;
                JsonObject obj = JsonObject::parse(frame, &pe);
                if (pe.isError()) continue;
                CHECK(obj.getString("pipeline") == id);
                const String kind = obj.getString("kind");
                if (kind == "StateChanged") sawStateChanged = true;
                if (kind == "StageState") sawStageState = true;
        }
        CHECK(sawStateChanged);
        CHECK(sawStageState);

        // The mismatched filter should have received nothing.
        const String stray = mismatched.readTextFrame(200);
        CHECK(stray.isEmpty());

        doRequest(fix.port, "DELETE", String("/api/pipelines/") + id);
}

// ===============================================================
// Multipart MJPEG preview route
// ===============================================================

namespace {

        // Decode chunked transfer-encoding in place; returns bytes consumed
        // from `in`.  Mirrors the helper used by the MjpegStream test.
        size_t decodeChunkedRaw(const uint8_t *in, size_t inLen, Buffer &out, size_t &outLen) {
                size_t i = 0;
                while (i < inLen) {
                        const uint8_t *eol = static_cast<const uint8_t *>(std::memchr(in + i, '\r', inLen - i));
                        if (eol == nullptr || (eol + 1) >= in + inLen) break;
                        if (eol[1] != '\n') return i; // malformed
                        const size_t hdrLen = static_cast<size_t>(eol - (in + i));
                        String       hex(reinterpret_cast<const char *>(in + i), hdrLen);
                        size_t       semi = hex.find(';');
                        if (semi != String::npos) hex = hex.left(semi);
                        const size_t chunkLen = static_cast<size_t>(std::strtoul(hex.cstr(), nullptr, 16));
                        const size_t needed = hdrLen + 2 + chunkLen + 2;
                        if (i + needed > inLen) break;
                        if (chunkLen == 0) {
                                i += needed;
                                return i;
                        }
                        if (outLen + chunkLen > out.allocSize()) {
                                Buffer grown(outLen + chunkLen + 64 * 1024);
                                std::memcpy(grown.data(), out.data(), outLen);
                                out = std::move(grown);
                        }
                        std::memcpy(static_cast<uint8_t *>(out.data()) + outLen, in + i + hdrLen + 2, chunkLen);
                        outLen += chunkLen;
                        i += needed;
                }
                return i;
        }

        // Counts JPEG SOI/EOI pairs in `data`.  Each complete JPEG bumps the
        // counter.  Caller-managed scan cursor lets us call this incrementally
        // as more bytes arrive.
        int countJpegs(const uint8_t *data, size_t len) {
                int    count = 0;
                size_t i = 0;
                while (i + 1 < len) {
                        if (data[i] == 0xFF && data[i + 1] == 0xD8) {
                                size_t j = i + 2;
                                bool   foundEoi = false;
                                for (; j + 1 < len; ++j) {
                                        if (data[j] == 0xFF && data[j + 1] == 0xD9) {
                                                foundEoi = true;
                                                break;
                                        }
                                }
                                if (!foundEoi) return count;
                                count++;
                                i = j + 2;
                        } else {
                                ++i;
                        }
                }
                return count;
        }

} // namespace

TEST_CASE("ApiRoutes - preview route streams multipart MJPEG when running") {
        ApiFixture fix;

        JsonObject body;
        body.set("name", String("preview-test"));
        body.set("userConfig", makeTpgToMjpegConfig().toJson());
        auto created = doRequest(fix.port, "POST", "/api/pipelines", body.toString(0), "application/json");
        CHECK(created.status == 201);
        Error      perr;
        JsonObject c = JsonObject::parse(created.body, &perr);
        REQUIRE(perr.isOk());
        const String id = c.getString("id");

        // Before build, the dynamic preview route can't dispatch — the
        // stage doesn't exist yet, so the response must not be 200.
        // Either 404 ("unknown stage", before build) or 503
        // ("sink is not open", before open).
        const HttpRsp preBuildRsp = doRequest(fix.port, "GET", String("/api/pipelines/") + id + "/preview/preview");
        CHECK(preBuildRsp.status != 200);

        // Build the pipeline.  After build + open, the MjpegStream
        // sink reports isStreaming so the route can serve content;
        // we still hit it pre-start to confirm 503 is the answer when
        // the sink hasn't yet been opened.
        auto build = doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/build");
        REQUIRE(build.status == 200);

        const HttpRsp postBuildRsp = doRequest(fix.port, "GET", String("/api/pipelines/") + id + "/preview/preview");
        // The sink hasn't been opened yet (open() runs the strand
        // command that flips isStreaming to true), so the dynamic
        // route must answer with 503.
        CHECK(postBuildRsp.status == 503);

        auto opened = doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/open");
        REQUIRE(opened.status == 200);
        auto started = doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/start");
        REQUIRE(started.status == 200);

        // Now the preview route should stream multipart/x-mixed-replace.
        TcpSocket sock;
        sock.open(IODevice::ReadWrite);
        REQUIRE(sock.connectToHost(SocketAddress::localhost(fix.port)).isOk());
        const String req = String("GET /api/pipelines/") + id +
                           "/preview/preview"
                           " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
        sock.write(req.cstr(), req.byteCount());

        // Drain headers.
        Buffer       raw(64 * 1024);
        size_t       rawLen = 0;
        ElapsedTimer t;
        t.start();
        size_t headerEnd = SIZE_MAX;
        while (t.elapsed() < 3000 && headerEnd == SIZE_MAX) {
                char    buf[4096];
                int64_t got = sock.read(buf, sizeof(buf));
                if (got > 0) {
                        if (rawLen + got > raw.allocSize()) {
                                Buffer grown(rawLen + got + 64 * 1024);
                                std::memcpy(grown.data(), raw.data(), rawLen);
                                raw = std::move(grown);
                        }
                        std::memcpy(static_cast<uint8_t *>(raw.data()) + rawLen, buf, got);
                        rawLen += static_cast<size_t>(got);
                        const uint8_t *bytes = static_cast<const uint8_t *>(raw.data());
                        for (size_t i = 3; i < rawLen; ++i) {
                                if (bytes[i - 3] == '\r' && bytes[i - 2] == '\n' && bytes[i - 1] == '\r' &&
                                    bytes[i] == '\n') {
                                        headerEnd = i + 1;
                                        break;
                                }
                        }
                } else {
                        Thread::sleepMs(5);
                }
        }
        REQUIRE(headerEnd != SIZE_MAX);

        const char  *p = static_cast<const char *>(raw.data());
        const String head(p, headerEnd - 4);
        CHECK(head.contains("200"));
        CHECK(head.toLower().contains("multipart/x-mixed-replace"));

        // Read body, decode chunked, scan for JPEGs.
        Buffer decoded(1024 * 1024);
        size_t decodedLen = 0;
        size_t bodyStart = headerEnd;
        size_t bodyConsumed = 0;
        int    jpegCount = 0;
        while (t.elapsed() < 5000 && jpegCount < 3) {
                char    buf[4096];
                int64_t got = sock.read(buf, sizeof(buf));
                if (got > 0) {
                        if (rawLen + got > raw.allocSize()) {
                                Buffer grown(rawLen + got + 64 * 1024);
                                std::memcpy(grown.data(), raw.data(), rawLen);
                                raw = std::move(grown);
                        }
                        std::memcpy(static_cast<uint8_t *>(raw.data()) + rawLen, buf, got);
                        rawLen += static_cast<size_t>(got);
                } else {
                        Thread::sleepMs(5);
                }
                const uint8_t *bb = static_cast<const uint8_t *>(raw.data()) + bodyStart + bodyConsumed;
                const size_t   bbLen = rawLen - bodyStart - bodyConsumed;
                const size_t   consumed = decodeChunkedRaw(bb, bbLen, decoded, decodedLen);
                bodyConsumed += consumed;
                jpegCount = countJpegs(static_cast<const uint8_t *>(decoded.data()), decodedLen);
        }
        CHECK(jpegCount >= 3);
        sock.close();

        // Tear down.
        doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/stop");
        doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/close");
        doRequest(fix.port, "DELETE", String("/api/pipelines/") + id);
}

TEST_CASE("ApiRoutes - preview route 400 when stage is wrong type") {
        ApiFixture fix;

        JsonObject body;
        body.set("name", String("preview-bad"));
        body.set("userConfig", makeTpgToNullPacingConfig().toJson());
        auto created = doRequest(fix.port, "POST", "/api/pipelines", body.toString(0), "application/json");
        REQUIRE(created.status == 201);
        Error      perr;
        JsonObject c = JsonObject::parse(created.body, &perr);
        REQUIRE(perr.isOk());
        const String id = c.getString("id");

        REQUIRE(doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/build").status == 200);
        REQUIRE(doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/open").status == 200);

        // Asking for the preview of a TPG source must yield 400.
        auto bad = doRequest(fix.port, "GET", String("/api/pipelines/") + id + "/preview/tpg1");
        CHECK(bad.status == 400);
        CHECK(bad.body.contains("MjpegStream"));

        // Unknown stage on a known pipeline -> 404 by the route's
        // contract (stage was not found at all).
        auto missing = doRequest(fix.port, "GET", String("/api/pipelines/") + id + "/preview/nope");
        CHECK(missing.status == 404);

        doRequest(fix.port, "POST", String("/api/pipelines/") + id + "/close");
        doRequest(fix.port, "DELETE", String("/api/pipelines/") + id);
}
