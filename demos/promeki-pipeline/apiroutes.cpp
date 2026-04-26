/**
 * @file      apiroutes.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include "apiroutes.h"

#include <promeki/buildinfo.h>
#include <promeki/duration.h>
#include <promeki/enum.h>
#include <promeki/error.h>
#include <promeki/framerate.h>
#include <promeki/httpmethod.h>
#include <promeki/httprequest.h>
#include <promeki/httpresponse.h>
#include <promeki/httpserver.h>
#include <promeki/httpstatus.h>
#include <promeki/json.h>
#include <promeki/list.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiotask_mjpegstream.h>
#include <promeki/mediapipeline.h>
#include <promeki/mediapipelineconfig.h>
#include <promeki/pixelformat.h>
#include <promeki/pixelmemlayout.h>
#include <promeki/string.h>
#include <promeki/variant.h>
#include <promeki/variantspec.h>
#include <promeki/videocodec.h>

#include "pipelinemanager.h"
#include "pipelinesettings.h"

using promeki::Enum;
using promeki::Error;
using promeki::FrameRate;
using promeki::HttpMethod;
using promeki::HttpRequest;
using promeki::HttpResponse;
using promeki::HttpServer;
using promeki::HttpStatus;
using promeki::JsonArray;
using promeki::JsonObject;
using promeki::List;
using promeki::MediaConfig;
using promeki::MediaIO;
using promeki::MediaIOTask_MjpegStream;
using promeki::MediaPipeline;
using promeki::MediaPipelineConfig;
using promeki::PixelFormat;
using promeki::PixelMemLayout;
using promeki::String;
using promeki::StringList;
using promeki::Variant;
using promeki::VariantSpec;
using promeki::VideoCodec;

namespace promekipipeline {

        namespace {

                // ---------------------------------------------------------------
                // Small JSON helpers shared by the route handlers.
                // ---------------------------------------------------------------

                JsonObject errorBody(const String &what, const String &name = String()) {
                        JsonObject obj;
                        obj.set("error", what);
                        if (!name.isEmpty()) obj.set("name", name);
                        return obj;
                }

                void sendError(HttpResponse &res, const HttpStatus &status, const String &what,
                               const String &name = String()) {
                        res.setStatus(status);
                        res.setJson(errorBody(what, name));
                }

                // Build the {type, values} enum block for a TypeRegistry-backed
                // Variant type whose primary spec type matches.  Returns an invalid
                // (empty) JsonObject when @p primary is not a TypeRegistry-style
                // type — caller treats that as "no enum metadata".
                //
                // The frontend treats TypedEnum values and TypeRegistry IDs the same:
                // both surface as `{type, values}` and render as a dropdown.  Doing
                // the synthesis here at the JSON-emit boundary keeps the library's
                // VariantSpec free of TypeRegistry awareness.
                JsonObject typeRegistryEnum(Variant::Type primary) {
                        JsonObject info;
                        switch (primary) {
                                case Variant::TypePixelFormat: {
                                        info.set("type", String("PixelFormat"));
                                        JsonArray                 vals;
                                        const PixelFormat::IDList ids = PixelFormat::registeredIDs();
                                        for (size_t i = 0; i < ids.size(); ++i) {
                                                PixelFormat pf(ids[i]);
                                                if (!pf.isValid()) continue;
                                                vals.add(pf.name());
                                        }
                                        info.set("values", vals);
                                        return info;
                                }
                                case Variant::TypePixelMemLayout: {
                                        info.set("type", String("PixelMemLayout"));
                                        JsonArray                    vals;
                                        const PixelMemLayout::IDList ids = PixelMemLayout::registeredIDs();
                                        for (size_t i = 0; i < ids.size(); ++i) {
                                                PixelMemLayout pml(ids[i]);
                                                if (!pml.isValid()) continue;
                                                vals.add(pml.name());
                                        }
                                        info.set("values", vals);
                                        return info;
                                }
                                case Variant::TypeVideoCodec: {
                                        info.set("type", String("VideoCodec"));
                                        JsonArray                vals;
                                        const VideoCodec::IDList ids = VideoCodec::registeredIDs();
                                        for (size_t i = 0; i < ids.size(); ++i) {
                                                VideoCodec vc(ids[i]);
                                                if (!vc.isValid()) continue;
                                                vals.add(vc.name());
                                        }
                                        info.set("values", vals);
                                        return info;
                                }
                                default: return JsonObject();
                        }
                }

                // Build the `presets: [{label, value}]` block for any Variant primary
                // that carries a library-supplied list of suggested-but-not-exclusive
                // values.  Today the only such type is @c FrameRate (whose well-known
                // rates come from @ref FrameRate::wellKnownRates), but the field is
                // generically named so future additions slot in here.
                //
                // Returned JsonArray is empty when @p primary has no preset list —
                // callers treat that as "no presets to advertise".
                //
                // Note that `presets` differs from `enum` in semantics: a preset is
                // suggestive (the user may type any rational), an enum is exclusive
                // (the user picks from the listed values).  The frontend renders
                // presets as a `<select>` plus a "Custom..." option that reveals the
                // underlying free-form editor.
                JsonArray typePresets(Variant::Type primary) {
                        JsonArray out;
                        if (primary == Variant::TypeFrameRate) {
                                const List<FrameRate::WellKnown> rates = FrameRate::wellKnownRates();
                                for (size_t i = 0; i < rates.size(); ++i) {
                                        JsonObject entry;
                                        entry.set("label", rates[i].label);
                                        entry.set("value", rates[i].rate.toString());
                                        out.add(entry);
                                }
                        }
                        return out;
                }

                // Serialize a single VariantSpec into the demo's stable shape:
                //   { "types": ["S32"], "default": ..., "min": ..., "max": ...,
                //     "enum": null | { "type": "PixelFormat",
                //                       "values": ["RGBA8", ...] },
                //     "presets": null | [{"label":"24","value":"24/1"}, ...],
                //     "description": "..." }
                //
                // HttpServer::specToJson handles the basic fields but does not know
                // about the enum-type metadata or preset list, which the frontend
                // needs to render dropdowns; we therefore reimplement the conversion
                // here rather than bolt enum awareness onto the library helper.
                //
                // Two sources feed the `enum` block:
                //   - VariantSpec::enumType() — TypedEnum-backed specs (Enum / EnumList).
                //   - typeRegistryEnum() — TypeRegistry-backed primary types
                //     (PixelFormat, PixelMemLayout, VideoCodec) whose spec carries
                //     no enumType() metadata but whose registered ID list is the
                //     equivalent enumeration of accepted values.
                //
                // `presets` is independent: it advertises a non-exclusive list of
                // suggested values (currently just FrameRate's well-known rates).
                JsonObject specToJson(const VariantSpec &spec) {
                        JsonObject out;

                        JsonArray types;
                        for (size_t i = 0; i < spec.types().size(); ++i) {
                                types.add(String(Variant::typeName(spec.types()[i])));
                        }
                        out.set("types", types);

                        if (spec.defaultValue().isValid()) {
                                out.setFromVariant("default", spec.defaultValue());
                        }
                        if (spec.hasMin()) out.setFromVariant("min", spec.rangeMin());
                        if (spec.hasMax()) out.setFromVariant("max", spec.rangeMax());

                        if (spec.hasEnumType()) {
                                Enum::Type t = spec.enumType();
                                JsonObject enumInfo;
                                enumInfo.set("type", t.name());
                                JsonArray       vals;
                                Enum::ValueList valueList = Enum::values(t);
                                for (size_t i = 0; i < valueList.size(); ++i) {
                                        vals.add(valueList[i].first());
                                }
                                enumInfo.set("values", vals);
                                out.set("enum", enumInfo);
                        } else if (!spec.types().isEmpty()) {
                                JsonObject regInfo = typeRegistryEnum(spec.types()[0]);
                                if (regInfo.size() > 0) out.set("enum", regInfo);
                        }

                        // Suggested-but-not-exclusive preset values (e.g. FrameRate's
                        // well-known rates).  Independent of the enum block above —
                        // the user can still enter a custom value.
                        if (!spec.types().isEmpty()) {
                                JsonArray presets = typePresets(spec.types()[0]);
                                if (presets.size() > 0) out.set("presets", presets);
                        }

                        if (!spec.description().isEmpty()) {
                                out.set("description", spec.description());
                        }
                        return out;
                }

                // Render the full schema for a backend's spec map.  Keys are the
                // MediaConfig ID names (MjpegMaxFps, JpegQuality, etc.) so the
                // frontend can index directly without an extra lookup.
                JsonObject schemaToJson(const MediaIO::Config::SpecMap &specs) {
                        JsonObject out;
                        for (auto it = specs.cbegin(); it != specs.cend(); ++it) {
                                const String name = it->first.name();
                                if (name.isEmpty()) continue;
                                out.set(name, specToJson(it->second));
                        }
                        return out;
                }

                // Map a registered MediaIO backend to the /api/types entry shape.
                JsonObject typeEntryToJson(const MediaIO::FormatDesc &desc) {
                        JsonObject obj;
                        obj.set("name", desc.name);
                        // Human-readable label.  Backends that didn't set
                        // FormatDesc::displayName get the canonical @c name as a
                        // fallback so the frontend never has to special-case empty.
                        obj.set("displayName", desc.displayName.isEmpty() ? desc.name : desc.displayName);
                        obj.set("description", desc.description);

                        JsonArray modes;
                        if (desc.canBeSource) modes.add(String("Source"));
                        if (desc.canBeSink) modes.add(String("Sink"));
                        if (desc.canBeTransform) modes.add(String("Transform"));
                        obj.set("modes", modes);

                        JsonArray exts;
                        for (size_t i = 0; i < desc.extensions.size(); ++i) {
                                exts.add(desc.extensions[i]);
                        }
                        obj.set("extensions", exts);

                        JsonArray schemes;
                        for (size_t i = 0; i < desc.schemes.size(); ++i) {
                                schemes.add(desc.schemes[i]);
                        }
                        obj.set("schemes", schemes);
                        return obj;
                }

                // Pull a registered backend descriptor by name.  Returns nullptr
                // when the type is unknown.
                const MediaIO::FormatDesc *findFormat(const String &name) {
                        const auto &all = MediaIO::registeredFormats();
                        for (size_t i = 0; i < all.size(); ++i) {
                                if (all[i].name == name) return &all[i];
                        }
                        return nullptr;
                }

                // True when the underlying pipeline is in a state that allows the
                // user-config / lifecycle action requested by the verb in question.
                // Each helper mirrors the precondition documented on the
                // MediaPipeline lifecycle calls.
                bool stateAllowsBuild(MediaPipeline::State s) {
                        return s == MediaPipeline::State::Empty || s == MediaPipeline::State::Closed;
                }
                bool stateAllowsOpen(MediaPipeline::State s) {
                        return s == MediaPipeline::State::Built;
                }
                bool stateAllowsStart(MediaPipeline::State s) {
                        return s == MediaPipeline::State::Open || s == MediaPipeline::State::Stopped;
                }
                bool stateAllowsStop(MediaPipeline::State s) {
                        return s == MediaPipeline::State::Running;
                }
                bool stateAllowsClose(MediaPipeline::State s) {
                        return s != MediaPipeline::State::Empty && s != MediaPipeline::State::Closed;
                }

                // Translate a MediaPipeline::Error reported by the manager into the
                // JSON envelope shape Phase D documents:
                //   200 + describe(id) on success
                //   404 {"error":"unknown id","name":"<id>"} on not-found
                //   409 {"error":"<error name>"} on every other backend error
                void sendLifecycleResult(HttpResponse &res, const Error &err, const String &id,
                                         PipelineManager &manager) {
                        if (err.isOk()) {
                                res.setJson(manager.describe(id));
                                return;
                        }
                        if (err == Error::NotExist) {
                                sendError(res, HttpStatus::NotFound, "unknown id", id);
                                return;
                        }
                        sendError(res, HttpStatus::Conflict, err.name());
                }

        } // namespace

        // ---------------------------------------------------------------
        // Construction
        // ---------------------------------------------------------------

        ApiRoutes::ApiRoutes(HttpServer &server, PipelineManager &manager) : _server(server), _manager(manager) {
                registerTypeRoutes();
                registerPipelineRoutes();
                registerPreviewRoute();
        }

        // ---------------------------------------------------------------
        // Type-registry routes
        // ---------------------------------------------------------------

        void ApiRoutes::registerTypeRoutes() {
                // Health check (kept from Phase C — never let it disappear, the
                // frontend's "is the backend reachable?" probe pings here).
                _server.route("/api/health", HttpMethod::Get, [](const HttpRequest &, HttpResponse &res) {
                        const auto *info = promeki::getBuildInfo();
                        JsonObject  body;
                        body.set("ok", true);
                        body.set("name", String(info->name));
                        body.set("version", String(info->version));
                        body.set("build", String(info->type));
                        res.setJson(body);
                });

                _server.route("/api/types", HttpMethod::Get, [](const HttpRequest &, HttpResponse &res) {
                        JsonArray   arr;
                        const auto &all = MediaIO::registeredFormats();
                        for (size_t i = 0; i < all.size(); ++i) {
                                arr.add(typeEntryToJson(all[i]));
                        }
                        res.setJson(arr);
                });

                _server.route("/api/types/{name}/schema", HttpMethod::Get,
                              [](const HttpRequest &req, HttpResponse &res) {
                                      const String name = req.pathParam("name");
                                      if (findFormat(name) == nullptr) {
                                              sendError(res, HttpStatus::NotFound, "unknown type", name);
                                              return;
                                      }
                                      res.setJson(schemaToJson(MediaIO::configSpecs(name)));
                              });

                _server.route("/api/types/{name}/defaults", HttpMethod::Get,
                              [](const HttpRequest &req, HttpResponse &res) {
                                      const String name = req.pathParam("name");
                                      if (findFormat(name) == nullptr) {
                                              sendError(res, HttpStatus::NotFound, "unknown type", name);
                                              return;
                                      }
                                      res.setJson(MediaIO::defaultConfig(name).toJson());
                              });

                _server.route("/api/types/{name}/metadata", HttpMethod::Get,
                              [](const HttpRequest &req, HttpResponse &res) {
                                      const String name = req.pathParam("name");
                                      if (findFormat(name) == nullptr) {
                                              sendError(res, HttpStatus::NotFound, "unknown type", name);
                                              return;
                                      }
                                      res.setJson(MediaIO::defaultMetadata(name).toJson());
                              });
        }

        // ---------------------------------------------------------------
        // Pipeline CRUD + lifecycle
        // ---------------------------------------------------------------

        void ApiRoutes::registerPipelineRoutes() {
                PipelineManager &mgr = _manager;

                _server.route("/api/pipelines", HttpMethod::Get, [&mgr](const HttpRequest &, HttpResponse &res) {
                        // describeAll returns { "pipelines": [...] }; the
                        // client just wants the array, so we strip the
                        // wrapper here rather than mutate the manager
                        // shape (which other callers may rely on).
                        const JsonObject all = mgr.describeAll();
                        Error            perr;
                        JsonArray        arr = all.getArray("pipelines", &perr);
                        if (perr.isError()) arr = JsonArray();
                        res.setJson(arr);
                });

                _server.route("/api/pipelines", HttpMethod::Post, [&mgr](const HttpRequest &req, HttpResponse &res) {
                        // Body is fully optional — if absent the new
                        // pipeline lands with default settings and an
                        // empty user config.  When present, any of the
                        // three subsections may be omitted independently.
                        Error      perr;
                        JsonObject body = req.bodyAsJson(&perr);
                        const bool hasBody = perr.isOk() && req.body().size() > 0;

                        String name;
                        if (hasBody) name = body.getString("name");

                        const String id = mgr.create(name);

                        if (hasBody && body.contains("settings")) {
                                Error            sErr;
                                const JsonObject settingsJson = body.getObject("settings", &sErr);
                                if (sErr.isOk()) {
                                        Error            decodeErr;
                                        PipelineSettings s = PipelineSettings::fromJson(settingsJson, &decodeErr);
                                        if (decodeErr.isOk()) {
                                                // Top-level "name" wins
                                                // when both are present —
                                                // the embedded settings
                                                // block defaults its
                                                // name, which would
                                                // otherwise silently
                                                // overwrite the user's
                                                // explicit choice.
                                                if (!name.isEmpty()) {
                                                        s.setName(name);
                                                }
                                                mgr.replaceSettings(id, s);
                                        }
                                }
                        }
                        if (hasBody && body.contains("userConfig")) {
                                Error            cErr;
                                const JsonObject userJson = body.getObject("userConfig", &cErr);
                                if (cErr.isOk()) {
                                        Error               decodeErr;
                                        MediaPipelineConfig cfg = MediaPipelineConfig::fromJson(userJson, &decodeErr);
                                        if (decodeErr.isOk()) {
                                                mgr.replaceConfig(id, cfg);
                                        }
                                }
                        }

                        JsonObject out;
                        out.set("id", id);
                        res.setStatus(HttpStatus::Created);
                        res.setJson(out);
                });

                _server.route("/api/pipelines/{id}", HttpMethod::Get,
                              [&mgr](const HttpRequest &req, HttpResponse &res) {
                                      const String     id = req.pathParam("id");
                                      const JsonObject obj = mgr.describe(id);
                                      if (obj.size() == 0) {
                                              sendError(res, HttpStatus::NotFound, "unknown id", id);
                                              return;
                                      }
                                      res.setJson(obj);
                              });

                _server.route("/api/pipelines/{id}", HttpMethod::Put,
                              [&mgr](const HttpRequest &req, HttpResponse &res) {
                                      const String     id = req.pathParam("id");
                                      Error            perr;
                                      const JsonObject body = req.bodyAsJson(&perr);
                                      if (perr.isError()) {
                                              sendError(res, HttpStatus::BadRequest, "invalid JSON body");
                                              return;
                                      }
                                      Error                     decodeErr;
                                      const MediaPipelineConfig cfg = MediaPipelineConfig::fromJson(body, &decodeErr);
                                      if (decodeErr.isError()) {
                                              sendError(res, HttpStatus::BadRequest, decodeErr.name());
                                              return;
                                      }
                                      const Error err = mgr.replaceConfig(id, cfg);
                                      sendLifecycleResult(res, err, id, mgr);
                              });

                _server.route("/api/pipelines/{id}", HttpMethod::Delete,
                              [&mgr](const HttpRequest &req, HttpResponse &res) {
                                      const String id = req.pathParam("id");
                                      const Error  err = mgr.remove(id);
                                      if (err == Error::NotExist) {
                                              sendError(res, HttpStatus::NotFound, "unknown id", id);
                                              return;
                                      }
                                      if (err.isError()) {
                                              sendError(res, HttpStatus::Conflict, err.name());
                                              return;
                                      }
                                      res.setStatus(HttpStatus::NoContent);
                              });

                _server.route("/api/pipelines/{id}/settings", HttpMethod::Get,
                              [&mgr](const HttpRequest &req, HttpResponse &res) {
                                      const String                  id = req.pathParam("id");
                                      const PipelineManager::Entry *e = mgr.find(id);
                                      if (e == nullptr) {
                                              sendError(res, HttpStatus::NotFound, "unknown id", id);
                                              return;
                                      }
                                      res.setJson(e->settings.toJson());
                              });

                _server.route("/api/pipelines/{id}/settings", HttpMethod::Put,
                              [&mgr](const HttpRequest &req, HttpResponse &res) {
                                      const String     id = req.pathParam("id");
                                      Error            perr;
                                      const JsonObject body = req.bodyAsJson(&perr);
                                      if (perr.isError()) {
                                              sendError(res, HttpStatus::BadRequest, "invalid JSON body");
                                              return;
                                      }
                                      Error                  decodeErr;
                                      const PipelineSettings s = PipelineSettings::fromJson(body, &decodeErr);
                                      if (decodeErr.isError()) {
                                              sendError(res, HttpStatus::BadRequest, decodeErr.name());
                                              return;
                                      }
                                      const Error err = mgr.replaceSettings(id, s);
                                      if (err == Error::NotExist) {
                                              sendError(res, HttpStatus::NotFound, "unknown id", id);
                                              return;
                                      }
                                      if (err.isError()) {
                                              sendError(res, HttpStatus::Conflict, err.name());
                                              return;
                                      }
                                      const PipelineManager::Entry *e = mgr.find(id);
                                      if (e == nullptr) {
                                              // Race: removed between the replace
                                              // and the lookup.  Treat as 404.
                                              sendError(res, HttpStatus::NotFound, "unknown id", id);
                                              return;
                                      }
                                      res.setJson(e->settings.toJson());
                              });

                _server.route("/api/pipelines/{id}/build", HttpMethod::Post,
                              [&mgr](const HttpRequest &req, HttpResponse &res) {
                                      const String id = req.pathParam("id");
                                      // Optional ?autoplan=0|1 override.  Pulled
                                      // before precondition checks so a bogus value
                                      // surfaces as 400, not as a 409.
                                      const String autoplanQ = req.queryValue("autoplan");
                                      bool         overrideAutoplan = false;
                                      bool         autoplanValue = true;
                                      if (!autoplanQ.isEmpty()) {
                                              if (autoplanQ == "1" || autoplanQ.toLower() == "true") {
                                                      overrideAutoplan = true;
                                                      autoplanValue = true;
                                              } else if (autoplanQ == "0" || autoplanQ.toLower() == "false") {
                                                      overrideAutoplan = true;
                                                      autoplanValue = false;
                                              } else {
                                                      sendError(res, HttpStatus::BadRequest, "invalid autoplan query");
                                                      return;
                                              }
                                      }

                                      const PipelineManager::Entry *peek = mgr.find(id);
                                      if (peek == nullptr) {
                                              sendError(res, HttpStatus::NotFound, "unknown id", id);
                                              return;
                                      }
                                      if (peek->pipeline.get() != nullptr &&
                                          !stateAllowsBuild(peek->pipeline->state())) {
                                              sendError(res, HttpStatus::Conflict, "pipeline not in a buildable state");
                                              return;
                                      }
                                      if (overrideAutoplan) {
                                              PipelineSettings s = peek->settings;
                                              s.setAutoplan(autoplanValue);
                                              mgr.replaceSettings(id, s);
                                      }
                                      const Error err = mgr.build(id);
                                      sendLifecycleResult(res, err, id, mgr);
                              });

                _server.route("/api/pipelines/{id}/open", HttpMethod::Post,
                              [&mgr](const HttpRequest &req, HttpResponse &res) {
                                      const String                  id = req.pathParam("id");
                                      const PipelineManager::Entry *peek = mgr.find(id);
                                      if (peek == nullptr) {
                                              sendError(res, HttpStatus::NotFound, "unknown id", id);
                                              return;
                                      }
                                      if (peek->pipeline.get() != nullptr &&
                                          !stateAllowsOpen(peek->pipeline->state())) {
                                              sendError(res, HttpStatus::Conflict, "pipeline not in an openable state");
                                              return;
                                      }
                                      const Error err = mgr.open(id);
                                      sendLifecycleResult(res, err, id, mgr);
                              });

                _server.route("/api/pipelines/{id}/start", HttpMethod::Post,
                              [&mgr](const HttpRequest &req, HttpResponse &res) {
                                      const String                  id = req.pathParam("id");
                                      const PipelineManager::Entry *peek = mgr.find(id);
                                      if (peek == nullptr) {
                                              sendError(res, HttpStatus::NotFound, "unknown id", id);
                                              return;
                                      }
                                      if (peek->pipeline.get() != nullptr &&
                                          !stateAllowsStart(peek->pipeline->state())) {
                                              sendError(res, HttpStatus::Conflict, "pipeline not in a startable state");
                                              return;
                                      }
                                      const Error err = mgr.start(id);
                                      sendLifecycleResult(res, err, id, mgr);
                              });

                _server.route("/api/pipelines/{id}/stop", HttpMethod::Post,
                              [&mgr](const HttpRequest &req, HttpResponse &res) {
                                      const String                  id = req.pathParam("id");
                                      const PipelineManager::Entry *peek = mgr.find(id);
                                      if (peek == nullptr) {
                                              sendError(res, HttpStatus::NotFound, "unknown id", id);
                                              return;
                                      }
                                      if (peek->pipeline.get() != nullptr &&
                                          !stateAllowsStop(peek->pipeline->state())) {
                                              sendError(res, HttpStatus::Conflict, "pipeline not in a stoppable state");
                                              return;
                                      }
                                      const Error err = mgr.stop(id);
                                      sendLifecycleResult(res, err, id, mgr);
                              });

                _server.route("/api/pipelines/{id}/close", HttpMethod::Post,
                              [&mgr](const HttpRequest &req, HttpResponse &res) {
                                      const String                  id = req.pathParam("id");
                                      const PipelineManager::Entry *peek = mgr.find(id);
                                      if (peek == nullptr) {
                                              sendError(res, HttpStatus::NotFound, "unknown id", id);
                                              return;
                                      }
                                      if (peek->pipeline.get() != nullptr &&
                                          !stateAllowsClose(peek->pipeline->state())) {
                                              sendError(res, HttpStatus::Conflict, "pipeline not in a closable state");
                                              return;
                                      }
                                      const Error err = mgr.close(id, /*block=*/true);
                                      sendLifecycleResult(res, err, id, mgr);
                              });

                // UX-helper macro that drives any reachable state to Running
                // through the minimum lifecycle cascade.  Frontend `Start`
                // button uses this so the user does not need to learn the
                // close → build → open → start sequence after a stop.
                _server.route("/api/pipelines/{id}/run", HttpMethod::Post,
                              [&mgr](const HttpRequest &req, HttpResponse &res) {
                                      const String                  id = req.pathParam("id");
                                      const PipelineManager::Entry *peek = mgr.find(id);
                                      if (peek == nullptr) {
                                              sendError(res, HttpStatus::NotFound, "unknown id", id);
                                              return;
                                      }
                                      const Error err = mgr.run(id);
                                      sendLifecycleResult(res, err, id, mgr);
                              });
        }

        // ---------------------------------------------------------------
        // Preview route — single dynamic dispatch by id + stage
        // ---------------------------------------------------------------

        void ApiRoutes::registerPreviewRoute() {
                PipelineManager &mgr = _manager;

                _server.route("/api/pipelines/{id}/preview/{stage}", HttpMethod::Get,
                              [&mgr](const HttpRequest &req, HttpResponse &res) {
                                      const String id = req.pathParam("id");
                                      const String stageName = req.pathParam("stage");

                                      const PipelineManager::Entry *e = mgr.find(id);
                                      if (e == nullptr) {
                                              sendError(res, HttpStatus::NotFound, "unknown id", id);
                                              return;
                                      }
                                      if (e->pipeline.get() == nullptr) {
                                              sendError(res, HttpStatus::ServiceUnavailable, "pipeline not built");
                                              return;
                                      }
                                      MediaIO *stage = e->pipeline->stage(stageName);
                                      if (stage == nullptr) {
                                              sendError(res, HttpStatus::NotFound, "unknown stage", stageName);
                                              return;
                                      }
                                      const String type = stage->config().contains(MediaConfig::Type)
                                                                  ? stage->config().getAs<String>(MediaConfig::Type)
                                                                  : String();
                                      if (type != "MjpegStream") {
                                              sendError(res, HttpStatus::BadRequest, "stage is not an MjpegStream sink",
                                                        stageName);
                                              return;
                                      }
                                      auto *mjpeg = static_cast<MediaIOTask_MjpegStream *>(stage->task());
                                      if (mjpeg == nullptr) {
                                              sendError(res, HttpStatus::ServiceUnavailable,
                                                        "MjpegStream task missing");
                                              return;
                                      }
                                      // Hand off to the library helper that already
                                      // owns the multipart wire format.  Doing the
                                      // delegation through the helper means both the
                                      // demo's dynamic route and any direct call to
                                      // MediaIOTask_MjpegStream::registerHttpRoute
                                      // share one streaming implementation.
                                      MediaIOTask_MjpegStream::buildMultipartHandler(mjpeg)(req, res);
                              });
        }

} // namespace promekipipeline
