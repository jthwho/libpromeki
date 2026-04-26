/**
 * @file      httpfilehandler.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/httpfilehandler.h>
#include <promeki/httprequest.h>
#include <promeki/httpresponse.h>
#include <promeki/file.h>
#include <promeki/dir.h>
#include <filesystem>
#include <cstring>

using namespace promeki;

namespace {

        String writeScratch(const String &name, const char *payload) {
                const String root = "/mnt/data/tmp/promeki/httpfilehandler-test";
                std::filesystem::create_directories(root.cstr());
                const String path = root + "/" + name;
                File         f{path};
                f.open(IODevice::WriteOnly, File::Create | File::Truncate);
                f.write(payload, std::strlen(payload));
                f.close();
                return root;
        }

        HttpRequest requestFor(const String &pathParam) {
                HttpRequest req;
                req.setMethod(HttpMethod::Get);
                HashMap<String, String> pp;
                pp.insert("path", pathParam);
                req.setPathParams(pp);
                Url u;
                u.setPath("/static/" + pathParam);
                req.setUrl(u);
                return req;
        }

} // anonymous namespace

TEST_CASE("HttpFileHandler") {
        const String root = writeScratch("hello.txt", "Hello!");

        SUBCASE("serves an existing file") {
                HttpFileHandler h{root};
                HttpRequest     req = requestFor("hello.txt");
                HttpResponse    res;
                h.serve(req, res);
                CHECK(res.status() == HttpStatus::Ok);
                CHECK(res.hasBodyStream());
                CHECK(res.bodyStreamLength() == 6);
                CHECK(res.headers().value("Content-Type").contains("text/plain"));
                CHECK(res.headers().contains("ETag"));
                CHECK(res.headers().contains("Last-Modified"));
        }

        SUBCASE("404 for missing files") {
                HttpFileHandler h{root};
                HttpRequest     req = requestFor("not-there.txt");
                HttpResponse    res;
                h.serve(req, res);
                CHECK(res.status() == HttpStatus::NotFound);
        }

        SUBCASE("forbids path traversal") {
                HttpFileHandler h{root};
                HttpRequest     req = requestFor("../../etc/passwd");
                HttpResponse    res;
                h.serve(req, res);
                CHECK(res.status() == HttpStatus::Forbidden);
        }

        SUBCASE("HEAD returns headers without streamed body") {
                HttpFileHandler h{root};
                HttpRequest     req = requestFor("hello.txt");
                req.setMethod(HttpMethod::Head);
                HttpResponse res;
                h.serve(req, res);
                CHECK(res.status() == HttpStatus::Ok);
                CHECK_FALSE(res.hasBodyStream());
                CHECK(res.headers().value("Content-Length") == "6");
        }

        SUBCASE("rejects non-GET/HEAD methods") {
                HttpFileHandler h{root};
                HttpRequest     req = requestFor("hello.txt");
                req.setMethod(HttpMethod::Post);
                HttpResponse res;
                h.serve(req, res);
                CHECK(res.status() == HttpStatus::MethodNotAllowed);
                CHECK(res.headers().value("Allow") == "GET, HEAD");
        }

        SUBCASE("MIME type lookup uses extension") {
                HttpFileHandler h{root};
                CHECK(h.mimeType("foo.html").contains("text/html"));
                CHECK(h.mimeType("foo.PNG") == "image/png");
                CHECK(h.mimeType("foo.json") == "application/json");
                CHECK(h.mimeType("foo.unknown") == "application/octet-stream");
                CHECK(h.mimeType("noext") == "application/octet-stream");
        }

        SUBCASE("addMimeType overrides defaults") {
                HttpFileHandler h{root};
                h.addMimeType("html", "text/plain; charset=utf-8");
                CHECK(h.mimeType("page.html") == "text/plain; charset=utf-8");
        }

        SUBCASE("If-None-Match yields 304") {
                HttpFileHandler h{root};
                HttpRequest     req = requestFor("hello.txt");
                HttpResponse    first;
                h.serve(req, first);
                const String etag = first.headers().value("ETag");
                REQUIRE_FALSE(etag.isEmpty());

                req.headers().set("If-None-Match", etag);
                HttpResponse second;
                h.serve(req, second);
                CHECK(second.status() == HttpStatus::NotModified);
                CHECK(second.headers().value("ETag") == etag);
        }
}
