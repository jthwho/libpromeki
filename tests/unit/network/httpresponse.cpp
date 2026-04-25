/**
 * @file      httpresponse.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/httpresponse.h>
#include <promeki/stringiodevice.h>

using namespace promeki;

TEST_CASE("HttpResponse") {
        SUBCASE("default is 200 OK with no body") {
                HttpResponse r;
                CHECK(r.status() == HttpStatus::Ok);
                CHECK(r.reasonPhrase() == "OK");
                CHECK(r.body().size() == 0);
                CHECK_FALSE(r.hasBodyStream());
                CHECK(r.isSuccess());
        }

        SUBCASE("setStatus by integer resets custom reason") {
                HttpResponse r;
                r.setReasonPhrase("Whatever");
                r.setStatus(418);
                CHECK(r.status().value() == 418);
                CHECK(r.reasonPhrase() == "I'm a Teapot");
        }

        SUBCASE("setText sets body and Content-Type") {
                HttpResponse r;
                r.setText("hello");
                CHECK(r.body().size() == 5);
                CHECK(r.headers().value("Content-Type") == "text/plain; charset=utf-8");
        }

        SUBCASE("setJson picks the canonical type") {
                JsonObject obj;
                obj.set("ok", true);
                HttpResponse r;
                r.setJson(obj);
                CHECK(r.headers().value("Content-Type") == "application/json");
                CHECK(r.body().size() > 0);
        }

        SUBCASE("setBodyStream supersedes in-memory body") {
                HttpResponse r;
                r.setText("ignored");

                auto *dev = new StringIODevice();
                dev->open(IODevice::WriteOnly);
                dev->write("payload", 7);
                dev->close();
                dev->open(IODevice::ReadOnly);
                auto shared = IODevice::Shared::takeOwnership(dev);

                r.setBodyStream(shared, 7, "application/octet-stream");
                CHECK(r.hasBodyStream());
                CHECK(r.bodyStreamLength() == 7);
                CHECK(r.body().size() == 0);    // cleared
                CHECK(r.headers().value("Content-Type") == "application/octet-stream");
        }

        SUBCASE("takeBodyStream transfers ownership") {
                HttpResponse r;
                auto shared = IODevice::Shared::takeOwnership(new StringIODevice());
                r.setBodyStream(shared, 0, "application/octet-stream");
                auto taken = r.takeBodyStream();
                CHECK(taken.isValid());
                CHECK_FALSE(r.hasBodyStream());
        }

        SUBCASE("factory helpers") {
                CHECK(HttpResponse::notFound().status()           == HttpStatus::NotFound);
                CHECK(HttpResponse::badRequest().status()         == HttpStatus::BadRequest);
                CHECK(HttpResponse::internalError().status()      == HttpStatus::InternalServerError);
                CHECK(HttpResponse::noContent().status()          == HttpStatus::NoContent);

                HttpResponse allow = HttpResponse::methodNotAllowed("GET, POST");
                CHECK(allow.status() == HttpStatus::MethodNotAllowed);
                CHECK(allow.headers().value("Allow") == "GET, POST");
        }

        SUBCASE("class-of-status helpers mirror status()") {
                HttpResponse r;
                r.setStatus(HttpStatus::MovedPermanently);
                CHECK(r.isRedirect());
                r.setStatus(HttpStatus::InternalServerError);
                CHECK(r.isError());
        }
}
