/**
 * @file      apiroutes.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

namespace promeki {
class HttpServer;
}

namespace promekipipeline {

class PipelineManager;

/**
 * @brief Registers the demo's REST routes on an @c HttpServer.
 *
 * Wires the full Phase D REST surface — type-registry introspection,
 * pipeline CRUD + lifecycle, settings round-trips, and the dynamic
 * MJPEG preview route — onto @p server, all backed by the supplied
 * @ref PipelineManager.  WebSocket routing is owned by
 * @ref EventBroadcaster and registered separately so the broadcaster
 * subscriber list, pipeline filter parsing, and per-socket teardown
 * stay in one place.
 *
 * The REST route shapes are stable and documented in
 * @c devplan/promeki-pipeline.md (Phase D).  Every handler is small
 * enough to live inside this class — there is no per-request state
 * held on the @c ApiRoutes instance itself.  Lifetime is the same
 * upper bound as Phase C: the server's lifetime; routes are not
 * unregistered on destruction.
 */
class ApiRoutes {
        public:
                /** @brief Length (hex chars) of the pipeline id placeholder. */
                static constexpr int IdParamLength = 8;

                /**
                 * @brief Mounts the full Phase D REST surface on @p server.
                 *
                 * @param server  HTTP server to register routes on.
                 * @param manager Pipeline manager to delegate lifecycle and
                 *                introspection calls into; held by
                 *                reference, must outlive @ref ApiRoutes.
                 */
                ApiRoutes(promeki::HttpServer &server, PipelineManager &manager);

                ApiRoutes(const ApiRoutes &) = delete;
                ApiRoutes(ApiRoutes &&) = delete;
                ApiRoutes &operator=(const ApiRoutes &) = delete;
                ApiRoutes &operator=(ApiRoutes &&) = delete;

        private:
                /** @brief Wires @c GET @c /api/health and @c /api/types* routes. */
                void registerTypeRoutes();

                /** @brief Wires the @c /api/pipelines and @c /api/pipelines/{id}* routes. */
                void registerPipelineRoutes();

                /** @brief Wires @c GET @c /api/pipelines/{id}/preview/{stage}. */
                void registerPreviewRoute();

                promeki::HttpServer &_server;
                PipelineManager     &_manager;
};

} // namespace promekipipeline
