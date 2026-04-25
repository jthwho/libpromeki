/**
 * @file      httpresponse.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/httpresponse.h>
#include <cstring>

PROMEKI_NAMESPACE_BEGIN

const String HttpResponse::DefaultHttpVersion{"HTTP/1.1"};

String HttpResponse::reasonPhrase() const {
        return _customReason.isEmpty() ? _status.reasonPhrase() : _customReason;
}

IODevice::Shared HttpResponse::takeBodyStream() {
        IODevice::Shared out = _bodyStream;
        _bodyStream = IODevice::Shared{};
        return out;
}

void HttpResponse::setBody(const Buffer &body) {
        _body = body;
        _bodyStream = IODevice::Shared{};
        _bodyStreamLength = -1;
}

void HttpResponse::setBody(const String &text) {
        const size_t n = text.byteCount();
        Buffer b(n);
        if(n > 0) {
                std::memcpy(b.data(), text.cstr(), n);
                b.setSize(n);
        }
        _body = std::move(b);
        _bodyStream = IODevice::Shared{};
        _bodyStreamLength = -1;
}

void HttpResponse::setText(const String &text) {
        setBody(text);
        _headers.set("Content-Type", "text/plain; charset=utf-8");
}

void HttpResponse::setHtml(const String &html) {
        setBody(html);
        _headers.set("Content-Type", "text/html; charset=utf-8");
}

void HttpResponse::setBinary(const Buffer &body, const String &mimeType) {
        setBody(body);
        if(!mimeType.isEmpty()) _headers.set("Content-Type", mimeType);
}

void HttpResponse::setJson(const JsonObject &obj) {
        setBody(obj.toString(0));
        _headers.set("Content-Type", "application/json");
}

void HttpResponse::setJson(const JsonArray &arr) {
        setBody(arr.toString(0));
        _headers.set("Content-Type", "application/json");
}

void HttpResponse::setJsonPretty(const JsonObject &obj, unsigned int indent) {
        setBody(obj.toString(indent));
        _headers.set("Content-Type", "application/json");
}

void HttpResponse::setJsonPretty(const JsonArray &arr, unsigned int indent) {
        setBody(arr.toString(indent));
        _headers.set("Content-Type", "application/json");
}

void HttpResponse::setBodyStream(IODevice::Shared device,
                                 int64_t length,
                                 const String &mimeType) {
        // Streamed body supersedes the in-memory body — clear it so
        // accidental setBody followed by setBodyStream still does what
        // the caller almost certainly meant.
        _body = Buffer{};
        _bodyStream = std::move(device);
        _bodyStreamLength = length;
        if(!mimeType.isEmpty()) _headers.set("Content-Type", mimeType);
}

// ============================================================
// Convenience factories
// ============================================================

HttpResponse HttpResponse::ok(const JsonObject &obj) {
        HttpResponse r;
        r.setStatus(HttpStatus::Ok);
        r.setJson(obj);
        return r;
}

HttpResponse HttpResponse::ok(const String &text) {
        HttpResponse r;
        r.setStatus(HttpStatus::Ok);
        r.setText(text);
        return r;
}

HttpResponse HttpResponse::notFound(const String &message) {
        HttpResponse r;
        r.setStatus(HttpStatus::NotFound);
        r.setText(message.isEmpty() ? r.status().reasonPhrase() : message);
        return r;
}

HttpResponse HttpResponse::badRequest(const String &message) {
        HttpResponse r;
        r.setStatus(HttpStatus::BadRequest);
        r.setText(message.isEmpty() ? r.status().reasonPhrase() : message);
        return r;
}

HttpResponse HttpResponse::methodNotAllowed(const String &allow) {
        HttpResponse r;
        r.setStatus(HttpStatus::MethodNotAllowed);
        if(!allow.isEmpty()) r.setHeader("Allow", allow);
        r.setText(r.status().reasonPhrase());
        return r;
}

HttpResponse HttpResponse::internalError(const String &message) {
        HttpResponse r;
        r.setStatus(HttpStatus::InternalServerError);
        r.setText(message.isEmpty() ? r.status().reasonPhrase() : message);
        return r;
}

HttpResponse HttpResponse::noContent() {
        HttpResponse r;
        r.setStatus(HttpStatus::NoContent);
        return r;
}

PROMEKI_NAMESPACE_END
