/**
 * @file      httpapi.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <promeki/namespace.h>
#include <promeki/objectbase.h>
#include <promeki/error.h>
#include <promeki/result.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/list.h>
#include <promeki/variant.h>
#include <promeki/variantspec.h>
#include <promeki/variantdatabase.h>
#include <promeki/variantlookup.h>
#include <promeki/json.h>
#include <promeki/httpmethod.h>
#include <promeki/httprequest.h>
#include <promeki/httpresponse.h>
#include <promeki/httphandler.h>
#include <promeki/httpserver.h>

PROMEKI_NAMESPACE_BEGIN

class HttpServer;

/**
 * @brief Self-describing HTTP API surface with built-in catalog and explorer.
 * @ingroup network
 *
 * @ref HttpApi sits beside an @ref HttpServer and tracks a registry of
 * @ref Endpoint descriptors.  Every endpoint registered through the API
 * is mounted on the server *and* recorded in the catalog, so the two
 * never drift out of sync.  The catalog is exposed three ways:
 *
 *  - @c GET @c \<apiPrefix>/         — bundled HTML/JS explorer served
 *                                      from @c :/.PROMEKI/explorer/, which
 *                                      fetches @c /_openapi at load time and
 *                                      renders a per-endpoint form UI.  The
 *                                      explorer lives at the api's bare prefix
 *                                      (Swagger-style) so it's discoverable
 *                                      without knowing any internal path.
 *  - @c GET @c \<apiPrefix>/_catalog — promeki-native JSON list of every
 *                                      registered endpoint.
 *  - @c GET @c \<apiPrefix>/_openapi — OpenAPI 3.1 document covering the
 *                                      same endpoints (validator-clean,
 *                                      importable into Swagger UI / Redoc /
 *                                      Postman / openapi-generator).
 *
 * @par Thread Safety
 * Inherits @ref ObjectBase: thread-affine.  Construction captures
 * the @ref HttpServer reference; both objects must share a single
 * @ref EventLoop (typically the main loop).  Endpoints registered
 * after @ref mount are visible immediately — the catalog is
 * generated on demand on each request.
 *
 * @par Decoupling from HttpServer
 * The server has no compile-time dependency on @ref HttpApi: the
 * existing @c HttpServer::exposeDatabase / @c HttpServer::exposeLookup
 * helpers stay available for "internal mount, don't advertise" cases.
 * @ref HttpApi provides matching @c exposeDatabase / @c exposeLookup
 * helpers that delegate to the server **and** publish catalog entries
 * — that's the recommended path so the explorer reflects the live API.
 *
 * @par Multiple APIs on one server
 * Because the server is unaware of the API registry, multiple
 * @ref HttpApi instances can co-exist on a single @ref HttpServer at
 * different prefixes (e.g. @c /api/public and @c /api/admin), each
 * with its own catalog and explorer.
 *
 * @par Example
 * @code
 * Application app(argc, argv);
 * HttpServer  server;
 * HttpApi     api(server, "/api");           // every endpoint nests under /api
 *
 * // Endpoint paths are RELATIVE to the api's prefix — the line below
 * // registers a route at /api/health on the underlying server.
 * api.route(HttpApi::Endpoint{
 *         .path    = "/health",
 *         .method  = HttpMethod::Get,
 *         .title   = "Health check",
 *         .summary = "Returns 200 when the service is responsive.",
 *         .tags    = {"system"},
 *         .response = VariantSpec().setType(Variant::TypeString),
 *     },
 *     [](const auto &, auto &res) { res.setText("ok"); });
 *
 * api.exposeDatabase("/options", "Library options",
 *                    LibraryOptions::db(), true);
 *
 * api.installPromekiAPI();   // optional: adds /api/promeki/{build,env,log,...}
 * api.mount();               // adds /api/ (explorer UI), /api/_catalog, /api/_openapi
 * server.listen(8080);
 * return app.exec();
 * @endcode
 */
class HttpApi : public ObjectBase {
                PROMEKI_OBJECT(HttpApi, ObjectBase)
        public:
                /**
                 * @brief Default mount prefix for the API surface.
                 *
                 * The API instance owns this prefix from construction
                 * onwards: every endpoint registered through @ref route,
                 * @ref rpc, @ref exposeDatabase, and @ref exposeLookup
                 * lives under it, the catalog/openapi/explorer trio
                 * is mounted at it, and @ref installPromekiAPI nests
                 * the standard diagnostic surface under
                 * @c \<prefix>/promeki/.
                 */
                static const String DefaultPrefix;

                /** @brief Default OpenAPI @c info.title for an unconfigured API. */
                static const String DefaultTitle;

                /** @brief Default OpenAPI @c info.version. */
                static const String DefaultVersion;

                // ============================================================
                // Endpoint descriptor types
                // ============================================================

                /**
                 * @brief Where in the request a parameter is sourced from.
                 *
                 * Maps directly onto OpenAPI's parameter @c in field for
                 * @c Path / @c Query / @c Header, and onto a @c requestBody
                 * field for @c Body.
                 */
                enum class ParamIn {
                        Path,   ///< Captured by the route pattern (e.g. @c "{id}").
                        Query,  ///< URL query string.
                        Header, ///< HTTP request header.
                        Body,   ///< JSON request body field (object keyed by @ref Param::name).
                };

                /**
                 * @brief Single parameter descriptor for an @ref Endpoint.
                 *
                 * Reuses @ref VariantSpec for the type / range / default /
                 * description triple — that's the same shape the catalog
                 * produces for @c VariantDatabase keys, so the explorer
                 * UI renders both with one code path.
                 */
                struct Param {
                                using List = promeki::List<Param>;

                                String      name;                ///< Wire name (case-sensitive for headers).
                                ParamIn     in = ParamIn::Query; ///< Where to read it from.
                                bool        required = false;    ///< OpenAPI @c required flag.
                                VariantSpec spec;                ///< Type, default, range, description.
                };

                /**
                 * @brief One declared error response.
                 *
                 * OpenAPI requires every status code an endpoint can
                 * return to be declared; this struct is what the
                 * catalog publishes for each.  The default policy
                 * (see @ref setDefaultErrors) installs a sensible
                 * @c 400 / @c 404 / @c 500 set on every endpoint that
                 * doesn't override it.
                 */
                struct ErrorResponse {
                                using List = promeki::List<ErrorResponse>;

                                int         status = 0;  ///< HTTP status code (e.g. 400, 404, 500).
                                String      description; ///< One-line human-readable summary.
                                VariantSpec body;        ///< Optional shape of the error body.
                };

                /**
                 * @brief Self-describing HTTP endpoint.
                 *
                 * The descriptor carries everything the catalog and
                 * OpenAPI generators need: route, method, presentation
                 * metadata (title/summary/tags), the parameter list,
                 * the success-response shape, and the declared error
                 * responses.  Endpoints are immutable from the server's
                 * point of view — registering the same path/method
                 * twice is a programming error.
                 */
                struct Endpoint {
                                using List = promeki::List<Endpoint>;

                                /**
                         * @brief Route pattern, relative to the parent
                         *        @ref HttpApi's prefix.
                         *
                         * Paths registered through @ref HttpApi are
                         * always interpreted relative to
                         * @ref HttpApi::prefix.  E.g. an endpoint with
                         * @c path == @c "/health" registered on an
                         * @ref HttpApi with prefix @c "/api" lands on
                         * the underlying server at @c "/api/health".
                         * Stored verbatim in the catalog and
                         * @c /_openapi as the absolute server path,
                         * not the relative form, so clients see the
                         * URL they should actually call.
                         */
                                String      path;
                                HttpMethod  method;   ///< HTTP method (defaults to GET).
                                String      title;    ///< Short human-readable label (used by explorer UI).
                                String      summary;  ///< One-paragraph description (Markdown allowed for OpenAPI).
                                StringList  tags;     ///< Grouping labels (used by Swagger UI / Redoc).
                                Param::List params;   ///< Path/query/header/body parameters in declaration order.
                                VariantSpec response; ///< Shape of a successful (2xx) response body.
                                String responseContentType = "application/json"; ///< Wire type for the success body.
                                ErrorResponse::List errors; ///< Declared error responses; empty = inherit defaults.
                                bool                deprecated = false; ///< Surfaces in the catalog and OpenAPI doc.

                                // FIXME(auth): when an authentication/authorization
                                // story lands, add a `security` field here (list of
                                // required scope/scheme names) and a matching
                                // HttpApi::setSecurityScheme() / addSecurityScheme()
                                // accessor for the OpenAPI components.securitySchemes
                                // block.  The catalog and OpenAPI generators must
                                // surface the per-endpoint security[] array, and the
                                // route-installer must enforce it (returning 401/403
                                // before invoking the handler) — neither is done
                                // today.  Tracked so this isn't silently forgotten.

                                /** @brief Convenience: append a parameter and return *this. */
                                Endpoint &addParam(Param p) {
                                        params.pushToBack(std::move(p));
                                        return *this;
                                }

                                /** @brief Convenience: append a tag and return *this. */
                                Endpoint &addTag(const String &t) {
                                        tags.pushToBack(t);
                                        return *this;
                                }
                };

                // ============================================================
                // RPC convenience
                // ============================================================

                /**
                 * @brief RPC-style handler: typed args in, Variant out.
                 *
                 * Each declared @ref Param from the @ref Endpoint shows
                 * up in the @ref VariantMap keyed by its @c name —
                 * already coerced into the type its @ref VariantSpec
                 * specified, with range and enum constraints validated.
                 * Missing optional params either appear at their
                 * declared default or are absent entirely; missing
                 * required params cause the call to be rejected with
                 * @c 400 before the handler runs.
                 *
                 * Returning @ref Result::Ok with a Variant produces a
                 * 200 with @c {"value": <variant>} as the body
                 * (serialized via @ref JsonObject::setFromVariant).
                 * Returning a populated @ref Error maps to one of the
                 * declared @ref ErrorResponse entries by status code,
                 * falling back to 500 if none matches.
                 */
                using RpcCall = std::function<Result<Variant>(const VariantMap &args)>;

                // ============================================================
                // Construction / lifetime
                // ============================================================

                /**
                 * @brief Constructs an API attached to @p server.
                 *
                 * Captures @p server by reference; its lifetime must
                 * outlive this object.  @p prefix is the URL prefix
                 * the API claims on the server — every endpoint
                 * registered through this object lives under it, and
                 * @ref mount installs the catalog/openapi/explorer
                 * trio at it.
                 *
                 * Multiple @ref HttpApi instances can co-exist on the
                 * same @ref HttpServer at non-overlapping prefixes
                 * (e.g. @c "/api" and @c "/admin").
                 */
                explicit HttpApi(HttpServer &server, const String &prefix = DefaultPrefix,
                                 ObjectBase *parent = nullptr);

                /** @brief Destructor.  Does not unmount routes from the server. */
                ~HttpApi() override;

                /** @brief Accessor for the underlying HTTP server. */
                HttpServer &server() { return _server; }

                /** @copydoc server */
                const HttpServer &server() const { return _server; }

                /** @brief Returns the prefix the API was constructed with. */
                const String &prefix() const { return _prefix; }

                /**
                 * @brief Resolves a relative path into an absolute server path.
                 *
                 * Convenience helper that prepends @ref prefix to
                 * @p relative and normalises the slash boundary.
                 * Useful when an application needs an absolute URL —
                 * e.g. to register a non-API route on the underlying
                 * @ref HttpServer at a path the API "owns".
                 */
                String resolve(const String &relative) const;

                // ============================================================
                // OpenAPI metadata
                // ============================================================

                /** @brief Sets the OpenAPI @c info.title (defaults to @ref DefaultTitle). */
                void setTitle(const String &title) { _title = title; }

                /** @brief Returns the configured title. */
                const String &title() const { return _title; }

                /** @brief Sets the OpenAPI @c info.version. */
                void setVersion(const String &version) { _version = version; }

                /** @brief Returns the configured version. */
                const String &version() const { return _version; }

                /** @brief Sets the OpenAPI @c info.description (Markdown allowed). */
                void setDescription(const String &desc) { _description = desc; }

                /** @brief Returns the configured description. */
                const String &description() const { return _description; }

                /**
                 * @brief Adds an OpenAPI @c servers[] entry.
                 *
                 * If no servers are added, the generator falls back to
                 * a single entry derived from @ref HttpServer::serverAddress
                 * at request time.  Override when running behind a
                 * reverse proxy or when advertising multiple
                 * environments (prod/staging/local).
                 */
                void addServer(const String &url, const String &description = String());

                /**
                 * @brief Replaces the default error-response set.
                 *
                 * Endpoints with an empty @c Endpoint::errors list at
                 * registration time inherit this set.  The factory
                 * default is @c {400 BadRequest, 404 NotFound, 500
                 * Internal} — change it via this method if your service
                 * has a different baseline.
                 */
                void setDefaultErrors(ErrorResponse::List errors);

                // ============================================================
                // Endpoint registration
                // ============================================================

                /**
                 * @brief Registers a bespoke endpoint with an explicit handler.
                 *
                 * @p ep is moved into the catalog, then routed on the
                 * underlying server.  The handler runs with the raw
                 * request — no parameter unmarshalling — so this is the
                 * right entry point when the handler needs full control
                 * over the response format (e.g. streaming, non-JSON
                 * content, custom error shapes).
                 *
                 * @return @ref Error::Ok or @ref Error::Exists if an
                 *         endpoint with the same @c (path, method) is
                 *         already registered.
                 */
                Error route(Endpoint ep, HttpHandlerFunc handler);

                /**
                 * @brief Registers an RPC-style endpoint.
                 *
                 * The framework parses each declared @ref Param from
                 * its declared @ref ParamIn, validates against its
                 * @ref VariantSpec, and hands the resulting @ref Args
                 * to @p call.  A successful @ref Result is rendered as
                 * @c {"value": <variant>} JSON with status 200; an
                 * error @ref Result picks a matching @ref ErrorResponse
                 * by status code (falling back to 500).
                 *
                 * The default method is @ref HttpMethod::Post when @p
                 * ep.params contains any @ref ParamIn::Body entries,
                 * otherwise whatever @p ep.method already specifies.
                 */
                Error rpc(Endpoint ep, RpcCall call);

                /**
                 * @brief Mounts a CRUD HTTP API over a @ref VariantDatabase.
                 *
                 * Delegates the actual route installation to
                 * @c HttpServer::exposeDatabase, then synthesizes
                 * @ref Endpoint records for each CRUD verb and for the
                 * @c /_schema introspection route — every key declared
                 * on the database becomes a navigable item in the
                 * explorer.
                 *
                 * @tparam N        Compile-time database name tag.
                 * @param mountPath Route prefix (no trailing slash).
                 * @param title     Human-readable label used by the explorer.
                 * @param db        Database to expose; must outlive this object.
                 * @param readOnly  Skip @c PUT / @c DELETE routes when true.
                 *
                 * @return @ref Error::Ok or a conflict if any of the
                 *         synthesized endpoints collide with existing
                 *         registrations.
                 */
                template <CompiledString N>
                Error exposeDatabase(const String &mountPath, const String &title, VariantDatabase<N> &db,
                                     bool readOnly = false);

                /**
                 * @brief Mounts a read-only path resolver over a @ref VariantLookup.
                 *
                 * Delegates to @c HttpServer::exposeLookup and adds a
                 * single catalog entry covering the greedy-tail GET.
                 * Because @ref VariantLookup advertises its key tree
                 * dynamically, the catalog entry uses a @c {path:*}
                 * placeholder — the explorer renders this as a free-
                 * form text input rather than a typed picker.
                 *
                 * @tparam T        Type with a registered @ref VariantLookup.
                 * @param mountPath Route prefix (no trailing slash).
                 * @param title     Human-readable label.
                 * @param target    Instance to resolve against; must outlive this object.
                 */
                template <typename T> Error exposeLookup(const String &mountPath, const String &title, T &target);

                // ============================================================
                // Bundled module installers
                // ============================================================

                /**
                 * @brief Installs the built-in promeki API surface.
                 *
                 * Mounts every diagnostic the library ships with —
                 * build info, environment snapshot, library options,
                 * memory-space stats, logger control, plus the static
                 * debug UI — under @c \<prefix>/promeki/, where each
                 * module appears as a sibling endpoint (e.g.
                 * @c /promeki/build, @c /promeki/log) alongside the
                 * UI's @c index.html at the bare path.
                 *
                 * Intentionally all-or-nothing: applications that
                 * want fine-grained control over which diagnostics
                 * they expose should not call this method and instead
                 * register their own bespoke endpoints.  The bundled
                 * debug UI assumes the entire surface is present.
                 *
                 * Safe to call exactly once before @ref mount.
                 *
                 * @return @ref Error::Ok or @ref Error::Exists if any
                 *         endpoint collides with an existing
                 *         registration.
                 */
                Error installPromekiAPI();

                // ============================================================
                // Catalog / mount
                // ============================================================

                /** @brief Returns a snapshot of every registered endpoint. */
                Endpoint::List endpoints() const;

                /**
                 * @brief Number of registered endpoints (excluding catalog/openapi/explorer).
                 */
                int endpointCount() const;

                /**
                 * @brief Mounts the catalog, OpenAPI, and explorer routes.
                 *
                 * Installs three routes under @ref prefix (see class
                 * docs).  Safe to call exactly once before
                 * @ref HttpServer::listen — calling it twice registers
                 * duplicate routes and returns @ref Error::Exists on
                 * the second call.
                 *
                 * The explorer asset bundle is served from
                 * @c :/.PROMEKI/explorer/ via @ref HttpFileHandler — no
                 * external files required.
                 */
                Error mount();

                /** @brief True between a successful @ref mount and destruction. */
                bool isMounted() const { return _mounted; }

                // ============================================================
                // Catalog / OpenAPI rendering (also used by the routes)
                // ============================================================

                /**
                 * @brief Renders the promeki-native catalog as JSON.
                 *
                 * Top-level shape:
                 * @code
                 * {
                 *   "title":   "...",
                 *   "version": "...",
                 *   "endpoints": [
                 *     { "path": "...", "method": "GET", "title": "...",
                 *       "tags": [...], "params": [...], "response": {...},
                 *       "errors": [...], "deprecated": false }
                 *   ]
                 * }
                 * @endcode
                 */
                JsonObject toCatalog() const;

                /**
                 * @brief Renders an OpenAPI 3.1 document for every registered endpoint.
                 *
                 * Maps @ref VariantSpec to JSON Schema using the
                 * conversion table in @ref variantSpecToJsonSchema —
                 * domain types (Timecode, UUID, PixelFormat, …) become
                 * @c "string" with a @c "format" extension, plus a
                 * @c "components.schemas" entry for any complex shapes.
                 *
                 * The result is validator-clean against the official
                 * OpenAPI 3.1 JSON Schema; CI is expected to lint it
                 * with @c openapi-spec-validator or equivalent.
                 */
                JsonObject toOpenApi() const;

                /**
                 * @brief Maps a single @ref VariantSpec to a JSON Schema fragment.
                 *
                 * Mapping policy (kept in one place so it stays
                 * consistent across catalog and OpenAPI output):
                 *
                 * | Variant type            | JSON Schema                                |
                 * |-------------------------|--------------------------------------------|
                 * | TypeBool                | `{"type":"boolean"}`                       |
                 * | TypeU8…TypeU64          | `{"type":"integer","minimum":0,...}`       |
                 * | TypeS8…TypeS64          | `{"type":"integer",...}`                   |
                 * | TypeFloat / TypeDouble  | `{"type":"number"}`                        |
                 * | TypeString              | `{"type":"string"}`                        |
                 * | TypeStringList          | `{"type":"array","items":{"type":"string"}}` |
                 * | TypeDateTime            | `{"type":"string","format":"date-time"}`   |
                 * | TypeTimeStamp           | `{"type":"string","format":"promeki-timestamp"}` |
                 * | TypeMediaTimeStamp      | `{"type":"string","format":"promeki-mediatimestamp"}` |
                 * | TypeFrameNumber         | `{"type":"integer","format":"promeki-framenumber"}` |
                 * | TypeFrameCount          | `{"type":"integer","format":"promeki-framecount"}` |
                 * | TypeMediaDuration       | `{"type":"string","format":"promeki-mediaduration"}` |
                 * | TypeDuration            | `{"type":"string","format":"duration"}`    |
                 * | TypeSize2D              | `{"$ref":"#/components/schemas/Size2D"}`   |
                 * | TypeUUID                | `{"type":"string","format":"uuid"}`        |
                 * | TypeUMID                | `{"type":"string","format":"promeki-umid"}` |
                 * | TypeTimecode            | `{"type":"string","format":"promeki-timecode"}` |
                 * | TypeRational            | `{"$ref":"#/components/schemas/Rational"}` |
                 * | TypeFrameRate           | `{"$ref":"#/components/schemas/FrameRate"}` |
                 * | TypeVideoFormat         | `{"type":"string","format":"promeki-videoformat"}` |
                 * | TypeColor               | `{"$ref":"#/components/schemas/Color"}`    |
                 * | TypeColorModel          | `{"type":"string","format":"promeki-colormodel"}` |
                 * | TypeMemSpace            | `{"type":"string","format":"promeki-memspace"}` |
                 * | TypePixelMemLayout      | `{"type":"string","format":"promeki-pixelmemlayout"}` |
                 * | TypePixelFormat         | `{"type":"string","format":"promeki-pixelformat"}` |
                 * | TypeVideoCodec          | `{"type":"string","format":"promeki-videocodec"}` |
                 * | TypeAudioCodec          | `{"type":"string","format":"promeki-audiocodec"}` |
                 * | TypeAudioFormat         | `{"type":"string","format":"promeki-audioformat"}` |
                 * | TypeEnum                | `{"type":"string","enum":[...]}`           |
                 * | TypeEnumList            | `{"type":"array","items":{"type":"string","enum":[...]}}` |
                 * | TypeMasteringDisplay    | `{"$ref":"#/components/schemas/MasteringDisplay"}` |
                 * | TypeContentLightLevel   | `{"$ref":"#/components/schemas/ContentLightLevel"}` |
                 * | TypeUrl                 | `{"type":"string","format":"uri"}`         |
                 * | TypeSocketAddress       | `{"type":"string","format":"promeki-socketaddress"}` |
                 * | TypeSdpSession          | `{"$ref":"#/components/schemas/SdpSession"}` |
                 * | TypeMacAddress          | `{"type":"string","format":"mac-address"}` |
                 * | TypeEUI64               | `{"type":"string","format":"eui64"}`       |
                 *
                 * Polymorphic specs (multiple types) emit @c "oneOf".
                 * Range constraints map to @c minimum / @c maximum.
                 * Enum types emit the @c values() list as an enum
                 * constraint and a @c x-promeki-enum-type extension
                 * carrying the enum's registered name.
                 *
                 * @c $ref entries are populated into
                 * @c components.schemas the first time they're emitted;
                 * subsequent references reuse the same definition.
                 *
                 * @param spec        The spec to map.
                 * @param componentsOut Optional output for @c components.schemas
                 *                    additions; pass null to suppress
                 *                    @c $ref entries (inlines complex
                 *                    types instead).
                 */
                static JsonObject variantSpecToJsonSchema(const VariantSpec &spec, JsonObject *componentsOut = nullptr);

        private:
                struct ServerEntry {
                                using List = promeki::List<ServerEntry>;
                                String url;
                                String description;
                };

                // Internal: install a single endpoint into both the
                // catalog and the underlying HTTP server.  Returns
                // Error::Exists if (path, method) already exists.
                Error registerEndpoint(Endpoint ep, HttpHandler::Ptr handler);

                // Internal: render error responses, falling back to
                // _defaultErrors when the endpoint declares none.
                const ErrorResponse::List &errorsFor(const Endpoint &ep) const;

                // Internal: the three handler factories for mount().
                HttpHandlerFunc  makeCatalogHandler() const;
                HttpHandlerFunc  makeOpenApiHandler() const;
                HttpHandler::Ptr makeExplorerHandler() const;

                HttpServer         &_server;
                String              _prefix;
                bool                _mounted = false;
                String              _title;
                String              _version;
                String              _description;
                ServerEntry::List   _servers;
                ErrorResponse::List _defaultErrors;
                Endpoint::List      _endpoints;

                // Internal helpers used by exposeDatabase / exposeLookup.
                Error              addEndpointDescriptor(Endpoint ep);
                static VariantSpec keyParamSpec(const StringList &knownKeys);
};

// ============================================================
// Reflection adapter template definitions
// ============================================================

template <CompiledString N>
Error HttpApi::exposeDatabase(const String &mountPath, const String &title, VariantDatabase<N> &db, bool readOnly) {
        using DB = VariantDatabase<N>;
        using ID = typename DB::ID;

        // mountPath is relative to this api's prefix.  Resolve it
        // before handing it to HttpServer (which doesn't know about
        // the api's prefix) and to the catalog (which stores
        // absolute paths).
        const String absMount = resolve(mountPath);

        // Hand the actual route registration off to HttpServer; the
        // catalog publication is the only thing layered on top.
        _server.exposeDatabase(absMount, db, readOnly);

        // Collect declared keys for the {key} param description so the
        // explorer's free-form input has a hint of valid values.
        StringList knownKeys;
        const auto specs = DB::registeredSpecs();
        for (auto it = specs.cbegin(); it != specs.cend(); ++it) {
                knownKeys.pushToBack(ID::fromId(it->first).name());
        }

        // GET <absMount> — full snapshot.
        Endpoint epAll;
        epAll.path = absMount;
        epAll.method = HttpMethod::Get;
        epAll.title = title + ": snapshot";
        epAll.summary = String("Returns every key/value pair in ") + title + " as a single JSON object.";
        epAll.tags = {title};
        epAll.response = VariantSpec().setDescription("JSON object whose keys are the database IDs.");
        if (Error err = addEndpointDescriptor(epAll); err.isError()) return err;

        // GET <absMount>/_schema — registered specs.
        Endpoint epSchema;
        epSchema.path = absMount + "/_schema";
        epSchema.method = HttpMethod::Get;
        epSchema.title = title + ": schema";
        epSchema.summary = "Returns the registered VariantSpec for every "
                           "declared key (type, default, range, description).";
        epSchema.tags = {title};
        epSchema.response = VariantSpec().setDescription("JSON object keyed by ID name; each value carries the spec.");
        if (Error err = addEndpointDescriptor(epSchema); err.isError()) return err;

        // GET <absMount>/{key} — single value.
        Endpoint epGet;
        epGet.path = absMount + "/{key}";
        epGet.method = HttpMethod::Get;
        epGet.title = title + ": get key";
        epGet.summary = "Returns a single value plus its spec.";
        epGet.tags = {title};
        epGet.params = {Param{
                .name = "key",
                .in = ParamIn::Path,
                .required = true,
                .spec = keyParamSpec(knownKeys),
        }};
        epGet.response = VariantSpec().setDescription("JSON object with the value under its name and an "
                                                      "optional `_spec` companion.");
        if (Error err = addEndpointDescriptor(epGet); err.isError()) return err;

        if (readOnly) return Error::Ok;

        // PUT <absMount>/{key} — set.
        Endpoint epPut;
        epPut.path = absMount + "/{key}";
        epPut.method = HttpMethod::Put;
        epPut.title = title + ": set key";
        epPut.summary = "Updates a single value, validating it against the "
                        "registered spec.";
        epPut.tags = {title};
        epPut.params = {
                Param{
                        .name = "key",
                        .in = ParamIn::Path,
                        .required = true,
                        .spec = keyParamSpec(knownKeys),
                },
                Param{
                        .name = "value",
                        .in = ParamIn::Body,
                        .required = true,
                        .spec = VariantSpec().setDescription("New value for the key (shape depends on spec)."),
                },
        };
        epPut.response = VariantSpec().setDescription("JSON object with the stored value under its name.");
        if (Error err = addEndpointDescriptor(epPut); err.isError()) return err;

        // DELETE <absMount>/{key} — clear.
        Endpoint epDel;
        epDel.path = absMount + "/{key}";
        epDel.method = HttpMethod::Delete;
        epDel.title = title + ": delete key";
        epDel.summary = "Clears the entry for a single key.";
        epDel.tags = {title};
        epDel.params = {Param{
                .name = "key",
                .in = ParamIn::Path,
                .required = true,
                .spec = keyParamSpec(knownKeys),
        }};
        return addEndpointDescriptor(epDel);
}

template <typename T> Error HttpApi::exposeLookup(const String &mountPath, const String &title, T &target) {
        const String absMount = resolve(mountPath);
        _server.exposeLookup(absMount, target);

        Endpoint ep;
        ep.path = absMount + "/{path}";
        ep.method = HttpMethod::Get;
        ep.title = title;
        ep.summary = "Resolves a path-style key against the live object "
                     "tree (slashes mapped to dots; bare integer segments "
                     "to [N] index suffixes).";
        ep.tags = {title};
        ep.params = {Param{
                .name = "path",
                .in = ParamIn::Path,
                .required = true,
                .spec = VariantSpec().setType(Variant::TypeString).setDescription("Greedy lookup path."),
        }};
        ep.response = VariantSpec().setDescription("JSON object {\"value\": <variant>}.");
        return addEndpointDescriptor(ep);
}

PROMEKI_NAMESPACE_END
