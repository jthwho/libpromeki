/**
 * @file      httprequest.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/httprequest.h>
#include <cstring>

PROMEKI_NAMESPACE_BEGIN

const String HttpRequest::DefaultHttpVersion{"HTTP/1.1"};

void HttpRequest::setBody(const String &text) {
        const size_t n = text.byteCount();
        Buffer       b(n);
        if (n > 0) {
                std::memcpy(b.data(), text.cstr(), n);
                b.setSize(n);
        }
        _body = std::move(b);
}

String HttpRequest::bodyAsString() const {
        if (!_body.isValid() || _body.size() == 0) return String();
        // Wire bodies are byte-clean: take them at face value as a
        // Latin-1 / ASCII / UTF-8 byte sequence and let the consumer
        // decide whether further decoding is needed.
        return String::fromUtf8(static_cast<const char *>(_body.data()), _body.size());
}

JsonObject HttpRequest::bodyAsJson(Error *err) const {
        return JsonObject::parse(bodyAsString(), err);
}

JsonArray HttpRequest::bodyAsJsonArray(Error *err) const {
        return JsonArray::parse(bodyAsString(), err);
}

void HttpRequest::setBody(const JsonObject &obj) {
        setBody(obj.toString(0));
        // Caller is in control of the Content-Type; we set it
        // automatically here because if you've stuffed a JSON object
        // into the body, the wire intent is essentially always JSON.
        // Override afterwards via headers().set("Content-Type", ...)
        // for the rare exception.
        _headers.set("Content-Type", "application/json");
}

void HttpRequest::setBody(const JsonArray &arr) {
        setBody(arr.toString(0));
        _headers.set("Content-Type", "application/json");
}

bool HttpRequest::operator==(const HttpRequest &other) const {
        if (!(_method == other._method)) return false;
        if (!(_url == other._url)) return false;
        if (!(_headers == other._headers)) return false;
        if (!(_httpVersion == other._httpVersion)) return false;
        if (!(_peerAddress == other._peerAddress)) return false;
        if (_body.size() != other._body.size()) return false;
        if (_body.size() != 0 && std::memcmp(_body.data(), other._body.data(), _body.size()) != 0) return false;
        // pathParams: compared via underlying HashMap equality.
        if (!(_pathParams == other._pathParams)) return false;
        return true;
}

PROMEKI_NAMESPACE_END
