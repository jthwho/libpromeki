/**
 * @file      httpapi.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/httpapi.h>
#include <promeki/httpserver.h>
#include <promeki/httpfilehandler.h>
#include <promeki/httpstatus.h>
#include <promeki/dir.h>
#include <promeki/logger.h>
#include <promeki/socketaddress.h>
#include <promeki/enum.h>
#include <promeki/objectbase.tpp>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(HttpApi);

// Defined in debugmodules.cpp.  Internal entry point — the
// individual installers behind it are file-static there because the
// promeki API is intentionally all-or-nothing.
void installPromekiDebugModules(HttpApi &api);

const String HttpApi::DefaultPrefix  = "/api";
const String HttpApi::DefaultTitle   = "Promeki API";
const String HttpApi::DefaultVersion = "1.0.0";

namespace {

// ----------------------------------------------------------------
// Variant::Type → JSON Schema mapping helpers.
// ----------------------------------------------------------------

// Returns true when @p type collapses to a JSON-native scalar/array
// and therefore does NOT need a $ref entry in components.schemas.
bool variantTypeIsNative(Variant::Type type) {
        switch(type) {
                case Variant::TypeBool:
                case Variant::TypeU8:  case Variant::TypeS8:
                case Variant::TypeU16: case Variant::TypeS16:
                case Variant::TypeU32: case Variant::TypeS32:
                case Variant::TypeU64: case Variant::TypeS64:
                case Variant::TypeFloat: case Variant::TypeDouble:
                case Variant::TypeString:
                case Variant::TypeStringList:
                case Variant::TypeDateTime:
                case Variant::TypeUUID:
                case Variant::TypeUrl:
                case Variant::TypeFrameNumber:
                case Variant::TypeFrameCount:
                case Variant::TypeEnum:
                case Variant::TypeEnumList:
                        return true;
                default:
                        return false;
        }
}

// Returns the JSON Schema fragment for a single Variant::Type.  The
// `componentsOut` parameter, when non-null, receives any complex-type
// schema definitions emitted along the way (so a single document can
// reference them via $ref).  When null, we inline a generic object
// schema instead — useful for the per-endpoint catalog view that
// doesn't have a components section.
JsonObject schemaForType(Variant::Type type, JsonObject *componentsOut) {
        JsonObject out;
        switch(type) {
                case Variant::TypeInvalid:
                        return out;
                case Variant::TypeBool:
                        out.set("type", "boolean");
                        return out;
                case Variant::TypeU8: case Variant::TypeU16:
                case Variant::TypeU32: case Variant::TypeU64:
                        out.set("type", "integer");
                        out.set("minimum", 0);
                        return out;
                case Variant::TypeS8: case Variant::TypeS16:
                case Variant::TypeS32: case Variant::TypeS64:
                        out.set("type", "integer");
                        return out;
                case Variant::TypeFloat: case Variant::TypeDouble:
                        out.set("type", "number");
                        return out;
                case Variant::TypeString:
                        out.set("type", "string");
                        return out;
                case Variant::TypeStringList: {
                        out.set("type", "array");
                        JsonObject items;
                        items.set("type", "string");
                        out.set("items", items);
                        return out;
                }
                case Variant::TypeDateTime:
                        out.set("type", "string");
                        out.set("format", "date-time");
                        return out;
                case Variant::TypeTimeStamp:
                        out.set("type", "string");
                        out.set("format", "promeki-timestamp");
                        return out;
                case Variant::TypeMediaTimeStamp:
                        out.set("type", "string");
                        out.set("format", "promeki-mediatimestamp");
                        return out;
                case Variant::TypeFrameNumber:
                        out.set("type", "integer");
                        out.set("format", "promeki-framenumber");
                        return out;
                case Variant::TypeFrameCount:
                        out.set("type", "integer");
                        out.set("format", "promeki-framecount");
                        return out;
                case Variant::TypeMediaDuration:
                        out.set("type", "string");
                        out.set("format", "promeki-mediaduration");
                        return out;
                case Variant::TypeDuration:
                        out.set("type", "string");
                        out.set("format", "duration");
                        return out;
                case Variant::TypeUUID:
                        out.set("type", "string");
                        out.set("format", "uuid");
                        return out;
                case Variant::TypeUMID:
                        out.set("type", "string");
                        out.set("format", "promeki-umid");
                        return out;
                case Variant::TypeTimecode:
                        out.set("type", "string");
                        out.set("format", "promeki-timecode");
                        return out;
                case Variant::TypeVideoFormat:
                        out.set("type", "string");
                        out.set("format", "promeki-videoformat");
                        return out;
                case Variant::TypeColorModel:
                        out.set("type", "string");
                        out.set("format", "promeki-colormodel");
                        return out;
                case Variant::TypeMemSpace:
                        out.set("type", "string");
                        out.set("format", "promeki-memspace");
                        return out;
                case Variant::TypePixelMemLayout:
                        out.set("type", "string");
                        out.set("format", "promeki-pixelmemlayout");
                        return out;
                case Variant::TypePixelFormat:
                        out.set("type", "string");
                        out.set("format", "promeki-pixelformat");
                        return out;
                case Variant::TypeVideoCodec:
                        out.set("type", "string");
                        out.set("format", "promeki-videocodec");
                        return out;
                case Variant::TypeAudioCodec:
                        out.set("type", "string");
                        out.set("format", "promeki-audiocodec");
                        return out;
                case Variant::TypeAudioFormat:
                        out.set("type", "string");
                        out.set("format", "promeki-audioformat");
                        return out;
                case Variant::TypeUrl:
                        out.set("type", "string");
                        out.set("format", "uri");
                        return out;
#if PROMEKI_ENABLE_NETWORK
                case Variant::TypeSocketAddress:
                        out.set("type", "string");
                        out.set("format", "promeki-socketaddress");
                        return out;
                case Variant::TypeMacAddress:
                        out.set("type", "string");
                        out.set("format", "mac-address");
                        return out;
                case Variant::TypeEUI64:
                        out.set("type", "string");
                        out.set("format", "eui64");
                        return out;
#endif
                case Variant::TypeSize2D: {
                        // {"width": int, "height": int}
                        if(componentsOut != nullptr) {
                                out.set("$ref", "#/components/schemas/Size2D");
                                if(!componentsOut->contains("Size2D")) {
                                        JsonObject sch;
                                        sch.set("type", "object");
                                        JsonObject props;
                                        JsonObject w;  w.set("type", "integer");
                                        JsonObject h;  h.set("type", "integer");
                                        props.set("width",  w);
                                        props.set("height", h);
                                        sch.set("properties", props);
                                        componentsOut->set("Size2D", sch);
                                }
                                return out;
                        }
                        out.set("type", "object");
                        return out;
                }
                case Variant::TypeRational:
                case Variant::TypeFrameRate: {
                        const char *name = (type == Variant::TypeFrameRate)
                                ? "FrameRate" : "Rational";
                        if(componentsOut != nullptr) {
                                out.set("$ref",
                                        String("#/components/schemas/") + name);
                                if(!componentsOut->contains(name)) {
                                        JsonObject sch;
                                        sch.set("type", "object");
                                        JsonObject props;
                                        JsonObject n;  n.set("type", "integer");
                                        JsonObject d;  d.set("type", "integer");
                                        props.set("num", n);
                                        props.set("den", d);
                                        sch.set("properties", props);
                                        componentsOut->set(name, sch);
                                }
                                return out;
                        }
                        out.set("type", "object");
                        return out;
                }
                case Variant::TypeColor: {
                        if(componentsOut != nullptr) {
                                out.set("$ref", "#/components/schemas/Color");
                                if(!componentsOut->contains("Color")) {
                                        JsonObject sch;
                                        sch.set("type", "object");
                                        sch.set("description",
                                                "Promeki Color: 4 floats plus a "
                                                "color-model identifier.");
                                        componentsOut->set("Color", sch);
                                }
                                return out;
                        }
                        out.set("type", "object");
                        return out;
                }
                case Variant::TypeMasteringDisplay: {
                        if(componentsOut != nullptr) {
                                out.set("$ref",
                                        "#/components/schemas/MasteringDisplay");
                                if(!componentsOut->contains("MasteringDisplay")) {
                                        JsonObject sch;
                                        sch.set("type", "object");
                                        sch.set("description",
                                                "SMPTE ST 2086 mastering display "
                                                "metadata (primaries + luminance).");
                                        componentsOut->set("MasteringDisplay", sch);
                                }
                                return out;
                        }
                        out.set("type", "object");
                        return out;
                }
                case Variant::TypeContentLightLevel: {
                        if(componentsOut != nullptr) {
                                out.set("$ref",
                                        "#/components/schemas/ContentLightLevel");
                                if(!componentsOut->contains("ContentLightLevel")) {
                                        JsonObject sch;
                                        sch.set("type", "object");
                                        sch.set("description",
                                                "CTA-861.3 content light level "
                                                "(MaxCLL/MaxFALL).");
                                        componentsOut->set("ContentLightLevel",
                                                sch);
                                }
                                return out;
                        }
                        out.set("type", "object");
                        return out;
                }
#if PROMEKI_ENABLE_NETWORK
                case Variant::TypeSdpSession: {
                        if(componentsOut != nullptr) {
                                out.set("$ref", "#/components/schemas/SdpSession");
                                if(!componentsOut->contains("SdpSession")) {
                                        JsonObject sch;
                                        sch.set("type", "object");
                                        sch.set("description",
                                                "SDP session description (RFC 8866).");
                                        componentsOut->set("SdpSession", sch);
                                }
                                return out;
                        }
                        out.set("type", "object");
                        return out;
                }
#endif
                case Variant::TypeEnum:
                case Variant::TypeEnumList: {
                        // Enum/EnumList specs carry their value list
                        // alongside the spec's enumType — this helper
                        // doesn't have access to that, so the schema
                        // here is the type-only fallback.  The full
                        // version (with enum values populated) lives
                        // in HttpApi::variantSpecToJsonSchema below.
                        if(type == Variant::TypeEnum) {
                                out.set("type", "string");
                        } else {
                                out.set("type", "array");
                                JsonObject items;
                                items.set("type", "string");
                                out.set("items", items);
                        }
                        return out;
                }
                default:
                        // Anything unknown becomes a free-form object
                        // — better than emitting an invalid schema.
                        out.set("type", "object");
                        return out;
        }
}

// Map an HttpMethod to the lowercase OpenAPI method key.
String openApiMethodKey(const HttpMethod &m) {
        return m.valueName().toLower();
}

}  // namespace

// ============================================================
// Construction
// ============================================================

HttpApi::HttpApi(HttpServer &server, const String &prefix, ObjectBase *parent) :
        ObjectBase(parent),
        _server(server),
        _prefix(prefix),
        _title(DefaultTitle),
        _version(DefaultVersion) {
        // Factory-default error responses — every endpoint inherits
        // these unless it sets Endpoint::errors itself.
        _defaultErrors = {
                ErrorResponse{ 400, "Bad request",            VariantSpec() },
                ErrorResponse{ 404, "Not found",              VariantSpec() },
                ErrorResponse{ 500, "Internal server error",  VariantSpec() },
        };
}

HttpApi::~HttpApi() = default;

String HttpApi::resolve(const String &relative) const {
        if(relative.isEmpty() || relative == "/") return _prefix;
        if(relative.startsWith("/")) return _prefix + relative;
        return _prefix + "/" + relative;
}

// ============================================================
// Metadata accessors
// ============================================================

void HttpApi::addServer(const String &url, const String &description) {
        ServerEntry entry{ url, description };
        _servers.pushToBack(entry);
}

void HttpApi::setDefaultErrors(ErrorResponse::List errors) {
        _defaultErrors = std::move(errors);
}

const HttpApi::ErrorResponse::List &HttpApi::errorsFor(const Endpoint &ep) const {
        return ep.errors.isEmpty() ? _defaultErrors : ep.errors;
}

// ============================================================
// Endpoint registration
// ============================================================

Error HttpApi::registerEndpoint(Endpoint ep, HttpHandler::Ptr handler) {
        // Endpoint paths arrive relative to this api's prefix.
        // Resolve to absolute up-front so the catalog and the
        // server registration agree on the on-the-wire URL.
        ep.path = resolve(ep.path);

        // Conflict check: same path + method.
        for(size_t i = 0; i < _endpoints.size(); ++i) {
                const Endpoint &existing = _endpoints[i];
                if(existing.path == ep.path && existing.method == ep.method) {
                        promekiWarn("HttpApi: endpoint %s %s already registered",
                                    ep.method.valueName().cstr(),
                                    ep.path.cstr());
                        return Error::Exists;
                }
        }

        // Drop into the catalog before routing so handlers that fire
        // during route registration (none today, but defensive) see
        // a consistent state.
        _endpoints.pushToBack(ep);
        _server.route(ep.path, ep.method, handler);
        return Error::Ok;
}

Error HttpApi::route(Endpoint ep, HttpHandlerFunc handler) {
        auto wrapped = HttpHandler::Ptr::takeOwnership(
                new HttpFunctionHandler(std::move(handler)));
        return registerEndpoint(std::move(ep), std::move(wrapped));
}

Error HttpApi::rpc(Endpoint ep, RpcCall call) {
        // Auto-promote method to POST when there are body params and
        // the caller left it at the default GET.  Body+GET is legal
        // HTTP but discouraged; this nudges callers toward the right
        // verb without requiring them to spell it out.
        bool hasBody = false;
        for(size_t i = 0; i < ep.params.size(); ++i) {
                if(ep.params[i].in == ParamIn::Body) { hasBody = true; break; }
        }
        if(hasBody && ep.method == HttpMethod::Get) ep.method = HttpMethod::Post;

        const ErrorResponse::List errors = errorsFor(ep);
        Param::List params = ep.params;

        auto fn = [params, errors, call = std::move(call)](
                        const HttpRequest &req, HttpResponse &res) {
                VariantMap args;

                // Pre-parse the body once so multiple body params can
                // share the parse result.  Empty bodies are allowed
                // when no body params are required.
                JsonObject bodyJson;
                bool bodyParsed = false;
                bool bodyParseError = false;
                auto ensureBody = [&]() {
                        if(bodyParsed) return;
                        bodyParsed = true;
                        if(req.body().size() == 0) return;
                        Error perr;
                        bodyJson = req.bodyAsJson(&perr);
                        if(perr.isError()) bodyParseError = true;
                };

                for(size_t i = 0; i < params.size(); ++i) {
                        const HttpApi::Param &p = params[i];
                        Variant v;
                        bool present = false;
                        switch(p.in) {
                                case HttpApi::ParamIn::Path: {
                                        const String s = req.pathParam(p.name);
                                        if(!s.isEmpty()) {
                                                Error perr;
                                                v = p.spec.parseString(s, &perr);
                                                present = perr.isOk();
                                        }
                                        break;
                                }
                                case HttpApi::ParamIn::Query: {
                                        const String s = req.queryValue(p.name);
                                        if(!s.isEmpty()) {
                                                Error perr;
                                                v = p.spec.parseString(s, &perr);
                                                present = perr.isOk();
                                        }
                                        break;
                                }
                                case HttpApi::ParamIn::Header: {
                                        const String s = req.header(p.name);
                                        if(!s.isEmpty()) {
                                                Error perr;
                                                v = p.spec.parseString(s, &perr);
                                                present = perr.isOk();
                                        }
                                        break;
                                }
                                case HttpApi::ParamIn::Body: {
                                        ensureBody();
                                        if(bodyParseError) {
                                                res = HttpResponse::badRequest(
                                                        "Body must be a JSON "
                                                        "object");
                                                return;
                                        }
                                        if(bodyJson.contains(p.name)) {
                                                bodyJson.forEach(
                                                        [&](const String &k,
                                                            const Variant &val) {
                                                                if(k == p.name) v = val;
                                                        });
                                                present = v.isValid();
                                        }
                                        break;
                                }
                        }

                        if(!present) {
                                if(p.required) {
                                        res = HttpResponse::badRequest(
                                                String("Missing required "
                                                       "parameter: ") + p.name);
                                        return;
                                }
                                // Fall back to the spec's default if
                                // any was declared.
                                if(p.spec.defaultValue().isValid()) {
                                        v = p.spec.defaultValue();
                                        present = true;
                                }
                        }
                        if(present) {
                                Error verr;
                                if(!p.spec.validate(v, &verr)) {
                                        res = HttpResponse::badRequest(
                                                String("Invalid value for ") +
                                                p.name + ": " +
                                                verr.desc());
                                        return;
                                }
                                args.insert(p.name, v);
                        }
                }

                Result<Variant> result = call(args);
                const Error &resultErr = result.second();
                if(resultErr.isError()) {
                        // Map error -> declared error response if we
                        // have a status code that matches; otherwise
                        // 500.  Today we only have a single error
                        // code per Result, so mapping is a search
                        // through declared statuses with a 500 fallback.
                        int status = 500;
                        // Common mappings for known error codes.
                        const Error::Code code = resultErr.code();
                        if(code == Error::Invalid ||
                           code == Error::InvalidArgument ||
                           code == Error::ParseFailed) {
                                status = 400;
                        } else if(code == Error::NotExist ||
                                  code == Error::IdNotFound) {
                                status = 404;
                        } else if(code == Error::PermissionDenied ||
                                  code == Error::NoPermission) {
                                status = 403;
                        } else {
                                // Fall back to whichever 4xx the
                                // endpoint declares first if we don't
                                // have a known mapping.
                                for(size_t i = 0; i < errors.size(); ++i) {
                                        if(errors[i].status / 100 == 4) {
                                                status = errors[i].status;
                                                break;
                                        }
                                }
                        }
                        res.setStatus(status);
                        JsonObject body;
                        body.set("error", resultErr.desc());
                        res.setJson(body);
                        return;
                }
                JsonObject body;
                body.setFromVariant("value", result.first());
                res.setJson(body);
        };

        return route(std::move(ep), std::move(fn));
}

HttpApi::Endpoint::List HttpApi::endpoints() const {
        return _endpoints;
}

int HttpApi::endpointCount() const {
        return static_cast<int>(_endpoints.size());
}

Error HttpApi::addEndpointDescriptor(Endpoint ep) {
        // Conflict-check only — the actual route registration was done
        // by HttpServer::exposeDatabase / exposeLookup.  Same logic as
        // registerEndpoint without the route() call.
        for(size_t i = 0; i < _endpoints.size(); ++i) {
                const Endpoint &existing = _endpoints[i];
                if(existing.path == ep.path && existing.method == ep.method) {
                        return Error::Exists;
                }
        }
        _endpoints.pushToBack(std::move(ep));
        return Error::Ok;
}

VariantSpec HttpApi::keyParamSpec(const StringList &knownKeys) {
        String desc = "Database key.  Known values: ";
        for(size_t i = 0; i < knownKeys.size(); ++i) {
                if(i > 0) desc += ", ";
                desc += knownKeys[i];
        }
        return VariantSpec().setType(Variant::TypeString).setDescription(desc);
}

// ============================================================
// Promeki API surface (all-or-nothing)
// ============================================================

Error HttpApi::installPromekiAPI() {
        // Delegates to the file-static installers in debugmodules.cpp.
        // The aggregate is intentionally all-or-nothing: the bundled
        // debug UI assumes the entire surface is present.  Apps that
        // want fine-grained control over what they expose should not
        // call this and instead register their own endpoints.
        installPromekiDebugModules(*this);
        return Error::Ok;
}

// ============================================================
// Mounting / catalog rendering
// ============================================================

Error HttpApi::mount() {
        if(_mounted) return Error::Exists;
        _mounted = true;

        // Catalog / OpenAPI: namespaced under "_" prefixes so they
        // don't collide with application endpoints.  Underscore-
        // prefixed names are reserved for the api framework itself.
        _server.route(resolve("/_catalog"), HttpMethod::Get,
                makeCatalogHandler());
        _server.route(resolve("/_openapi"), HttpMethod::Get,
                makeOpenApiHandler());

        // Explorer UI: served at the api's bare prefix.  A request
        // for the prefix without a trailing slash redirects to the
        // canonical trailing-slash form so relative URLs in
        // index.html resolve correctly in the browser; everything
        // under the prefix is served by the file handler from
        // :/.PROMEKI/explorer/, which auto-serves index.html for
        // directory-style requests.
        //
        // The catch-all `<prefix>/{path:*}` is a low-score greedy
        // route (1001 with a one-segment prefix); explicit
        // application endpoints registered as exact routes (score
        // 7000+) and per-key adapter routes always win, so the
        // explorer file handler only sees URLs that don't map to
        // anything more specific.
        const String slashed = _prefix + "/";
        _server.route(_prefix, HttpMethod::Get,
                [slashed](const HttpRequest &, HttpResponse &res) {
                        res.setStatus(HttpStatus::Found);
                        res.setHeader("Location", slashed);
                        res.setText("");
                });
        _server.route(resolve("/{path:*}"), HttpMethod::Get,
                makeExplorerHandler());
        return Error::Ok;
}

HttpHandlerFunc HttpApi::makeCatalogHandler() const {
        return [this](const HttpRequest &, HttpResponse &res) {
                res.setJson(toCatalog());
        };
}

HttpHandlerFunc HttpApi::makeOpenApiHandler() const {
        return [this](const HttpRequest &, HttpResponse &res) {
                res.setJson(toOpenApi());
        };
}

HttpHandler::Ptr HttpApi::makeExplorerHandler() const {
        // Static asset bundle baked into cirf at :/.PROMEKI/explorer/.
        return HttpHandler::Ptr::takeOwnership(
                new HttpFileHandler(Dir(":/.PROMEKI/explorer")));
}

JsonObject HttpApi::toCatalog() const {
        JsonObject out;
        out.set("title",   _title);
        out.set("version", _version);
        if(!_description.isEmpty()) out.set("description", _description);

        JsonArray endpoints;
        for(size_t i = 0; i < _endpoints.size(); ++i) {
                const Endpoint &ep = _endpoints[i];
                JsonObject obj;
                obj.set("path",       ep.path);
                obj.set("method",     ep.method.valueName());
                obj.set("title",      ep.title);
                obj.set("summary",    ep.summary);
                obj.set("deprecated", ep.deprecated);
                obj.set("responseContentType", ep.responseContentType);

                JsonArray tags;
                for(size_t j = 0; j < ep.tags.size(); ++j) tags.add(ep.tags[j]);
                obj.set("tags", tags);

                JsonArray params;
                for(size_t j = 0; j < ep.params.size(); ++j) {
                        const Param &p = ep.params[j];
                        JsonObject pj;
                        pj.set("name", p.name);
                        switch(p.in) {
                                case ParamIn::Path:   pj.set("in", "path");   break;
                                case ParamIn::Query:  pj.set("in", "query");  break;
                                case ParamIn::Header: pj.set("in", "header"); break;
                                case ParamIn::Body:   pj.set("in", "body");   break;
                        }
                        pj.set("required", p.required);
                        pj.set("schema", variantSpecToJsonSchema(p.spec));
                        if(!p.spec.description().isEmpty()) {
                                pj.set("description", p.spec.description());
                        }
                        params.add(pj);
                }
                obj.set("params", params);

                obj.set("response", variantSpecToJsonSchema(ep.response));

                JsonArray errs;
                const ErrorResponse::List &erefs = errorsFor(ep);
                for(size_t j = 0; j < erefs.size(); ++j) {
                        JsonObject ej;
                        ej.set("status",      erefs[j].status);
                        ej.set("description", erefs[j].description);
                        if(erefs[j].body.isValid()) {
                                ej.set("schema",
                                        variantSpecToJsonSchema(erefs[j].body));
                        }
                        errs.add(ej);
                }
                obj.set("errors", errs);
                endpoints.add(obj);
        }
        out.set("endpoints", endpoints);
        return out;
}

JsonObject HttpApi::toOpenApi() const {
        JsonObject doc;
        doc.set("openapi", "3.1.0");

        // info{} block.
        JsonObject info;
        info.set("title",   _title);
        info.set("version", _version);
        if(!_description.isEmpty()) info.set("description", _description);
        doc.set("info", info);

        // servers[].  Fall back to the live bound address when the
        // caller didn't explicitly add any — that matches what most
        // local-dev tooling expects.
        JsonArray servers;
        if(_servers.isEmpty()) {
                JsonObject s;
                const SocketAddress addr = _server.serverAddress();
                if(!addr.isNull()) {
                        s.set("url", String("http://") + addr.toString());
                        servers.add(s);
                }
        } else {
                for(size_t i = 0; i < _servers.size(); ++i) {
                        JsonObject s;
                        s.set("url", _servers[i].url);
                        if(!_servers[i].description.isEmpty()) {
                                s.set("description", _servers[i].description);
                        }
                        servers.add(s);
                }
        }
        if(servers.size() > 0) doc.set("servers", servers);

        // Group endpoints by path so each path entry can list its
        // methods alongside one another (the OpenAPI shape).
        JsonObject components;
        JsonObject schemas;

        JsonObject paths;
        for(size_t i = 0; i < _endpoints.size(); ++i) {
                const Endpoint &ep = _endpoints[i];

                JsonObject pathItem;
                if(paths.contains(ep.path)) pathItem = paths.getObject(ep.path);

                JsonObject op;
                op.set("summary", ep.title);
                if(!ep.summary.isEmpty()) op.set("description", ep.summary);
                if(ep.deprecated) op.set("deprecated", true);

                JsonArray tags;
                for(size_t j = 0; j < ep.tags.size(); ++j) tags.add(ep.tags[j]);
                if(tags.size() > 0) op.set("tags", tags);

                // parameters[] (path/query/header) and requestBody.
                JsonArray params;
                JsonObject bodyProps;
                JsonArray  bodyRequired;
                bool hasBody = false;
                for(size_t j = 0; j < ep.params.size(); ++j) {
                        const Param &p = ep.params[j];
                        if(p.in == ParamIn::Body) {
                                hasBody = true;
                                bodyProps.set(p.name,
                                        variantSpecToJsonSchema(p.spec, &schemas));
                                if(p.required) bodyRequired.add(p.name);
                                continue;
                        }
                        JsonObject pj;
                        pj.set("name", p.name);
                        switch(p.in) {
                                case ParamIn::Path:   pj.set("in", "path");   break;
                                case ParamIn::Query:  pj.set("in", "query");  break;
                                case ParamIn::Header: pj.set("in", "header"); break;
                                case ParamIn::Body: break;  // unreachable
                        }
                        pj.set("required",
                                p.in == ParamIn::Path ? true : p.required);
                        if(!p.spec.description().isEmpty()) {
                                pj.set("description", p.spec.description());
                        }
                        pj.set("schema",
                                variantSpecToJsonSchema(p.spec, &schemas));
                        params.add(pj);
                }
                if(params.size() > 0) op.set("parameters", params);

                if(hasBody) {
                        JsonObject body;
                        body.set("required", bodyRequired.size() > 0);
                        JsonObject content;
                        JsonObject media;
                        JsonObject sch;
                        sch.set("type", "object");
                        sch.set("properties", bodyProps);
                        if(bodyRequired.size() > 0) sch.set("required", bodyRequired);
                        media.set("schema", sch);
                        content.set("application/json", media);
                        body.set("content", content);
                        op.set("requestBody", body);
                }

                // responses{}.  Always declare 200 plus declared errors.
                JsonObject responses;
                {
                        JsonObject ok;
                        ok.set("description", "Success");
                        if(ep.response.isValid()
                           || !ep.response.description().isEmpty()) {
                                JsonObject content;
                                JsonObject media;
                                media.set("schema",
                                        variantSpecToJsonSchema(ep.response,
                                                                 &schemas));
                                content.set(ep.responseContentType, media);
                                ok.set("content", content);
                        }
                        responses.set("200", ok);
                }
                const ErrorResponse::List &erefs = errorsFor(ep);
                for(size_t j = 0; j < erefs.size(); ++j) {
                        JsonObject er;
                        er.set("description", erefs[j].description);
                        if(erefs[j].body.isValid()) {
                                JsonObject content;
                                JsonObject media;
                                media.set("schema",
                                        variantSpecToJsonSchema(erefs[j].body,
                                                                 &schemas));
                                content.set("application/json", media);
                                er.set("content", content);
                        }
                        responses.set(String::number(erefs[j].status), er);
                }
                op.set("responses", responses);

                pathItem.set(openApiMethodKey(ep.method), op);
                paths.set(ep.path, pathItem);
        }
        doc.set("paths", paths);

        if(schemas.size() > 0) {
                components.set("schemas", schemas);
                doc.set("components", components);
        }
        return doc;
}

JsonObject HttpApi::variantSpecToJsonSchema(const VariantSpec &spec,
                                            JsonObject *componentsOut) {
        const VariantSpec::TypeList &types = spec.types();

        JsonObject out;

        auto applyConstraints = [&](JsonObject &target) {
                if(!spec.description().isEmpty()) {
                        target.set("description", spec.description());
                }
                if(spec.defaultValue().isValid()) {
                        target.setFromVariant("default", spec.defaultValue());
                }
                if(spec.hasMin()) target.setFromVariant("minimum", spec.rangeMin());
                if(spec.hasMax()) target.setFromVariant("maximum", spec.rangeMax());
                if(spec.hasEnumType()) {
                        // Populate the JSON-Schema enum constraint
                        // from the registered enum's value list.
                        JsonArray values;
                        const Enum::ValueList valuePairs =
                                Enum::values(spec.enumType());
                        for(size_t i = 0; i < valuePairs.size(); ++i) {
                                values.add(valuePairs[i].first());
                        }
                        if(values.size() > 0) target.set("enum", values);
                        target.set("x-promeki-enum-type",
                                spec.enumType().name());
                }
        };

        if(types.isEmpty()) {
                // (any) — leave the schema permissive.
                applyConstraints(out);
                return out;
        }

        if(spec.isPolymorphic()) {
                // oneOf[ schema-per-type ].
                JsonArray oneOf;
                for(size_t i = 0; i < types.size(); ++i) {
                        JsonObject sub = schemaForType(types[i], componentsOut);
                        oneOf.add(sub);
                }
                out.set("oneOf", oneOf);
                applyConstraints(out);
                return out;
        }

        out = schemaForType(types[0], componentsOut);

        // EnumList items inherit the spec's enum-type values too.
        if(types[0] == Variant::TypeEnumList && spec.hasEnumType()) {
                JsonObject items;
                items.set("type", "string");
                JsonArray values;
                const Enum::ValueList valuePairs =
                        Enum::values(spec.enumType());
                for(size_t i = 0; i < valuePairs.size(); ++i) {
                        values.add(valuePairs[i].first());
                }
                if(values.size() > 0) items.set("enum", values);
                items.set("x-promeki-enum-type", spec.enumType().name());
                out.set("items", items);
        }

        applyConstraints(out);
        return out;
}

PROMEKI_NAMESPACE_END
