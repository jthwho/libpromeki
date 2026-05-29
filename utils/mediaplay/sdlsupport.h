/**
 * @file      mediaplay/sdlsupport.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * SDL integration helper for mediaplay.  All knowledge of
 * SDLWindow / SDLAudioOutput / SDLPlayerWidget / the SDL pseudo-backend
 * is hidden behind this interface so @c main.cpp does not need a single
 * @c \#if @c MEDIAPLAY_HAVE_SDL.  The implementation in
 * @c sdlsupport.cpp has two compile-time branches selected by the
 * @c MEDIAPLAY_HAVE_SDL macro (defined to 1 / 0 by CMake): the SDL
 * branch wires up the real widgets, the stub branch is a no-op shell
 * that always reports "SDL not available".
 *
 * Lifetime: construct one @ref SdlSupport on the stack in @c main()
 * right after the @ref promeki::Application instance.  The destructor
 * tears down everything in the right order so explicit cleanup at
 * early-exit paths is not required.
 */

#pragma once

#include <promeki/mediaio.h>
#include <promeki/mediapipelineconfig.h>
#include <promeki/metadata.h>
#include <promeki/string.h>

namespace mediaplay {

        struct Options;     // cli.h
        struct SdlSupportData; // PIMPL — defined in sdlsupport.cpp

        /**
         * @brief RAII wrapper around the SDL subsystem + shared window /
         *        audio output mediaplay needs for SDL display sinks.
         *
         * Instantiating an @c SdlSupport on the stack is safe in any
         * build: when mediaplay was compiled without SDL the type is a
         * trivial stub whose mutators report "no SDL available" and
         * whose destructor does nothing.  When SDL is built in,
         * construction initialises the @c SdlSubsystem and destruction
         * tears down every SDL resource the helper allocated.
         *
         * The helper deliberately does not own the @c MediaIO pointers
         * returned from @ref createStage — those are owned by their
         * parent @c SDLPlayerWidget which is parented to the shared
         * window, so deleting the window at teardown frees the entire
         * chain in one shot.  Callers can therefore drop the pointers
         * into the pipeline's injected-stage map without worrying about
         * who deletes them.
         */
        class SdlSupport {
                public:
                        SdlSupport();
                        ~SdlSupport();

                        // Non-copyable, non-movable — the SDL subsystem is a
                        // process-wide singleton-ish thing and we only want
                        // exactly one of these.
                        SdlSupport(const SdlSupport &) = delete;
                        SdlSupport &operator=(const SdlSupport &) = delete;
                        SdlSupport(SdlSupport &&) = delete;
                        SdlSupport &operator=(SdlSupport &&) = delete;

                        /**
                         * @brief Whether this binary was built with SDL.
                         *
                         * Returns @c true when @c MEDIAPLAY_HAVE_SDL is 1
                         * (i.e. @c promeki-sdl was linked), @c false otherwise.
                         */
                        static bool isAvailable();

                        /**
                         * @brief Whether @p stageType names the SDL
                         *        pseudo-backend.
                         *
                         * Always returns @c false in builds without SDL, so
                         * callers can use this to elide every SDL-specific
                         * code path without their own gating.
                         */
                        static bool isSdlStage(const promeki::String &stageType);

                        // -------------------------------------------------------------
                        // SDL pseudo-backend schema accessors
                        // -------------------------------------------------------------
                        //
                        // The SDL display sink is the one mediaplay backend the
                        // library's MediaIO factory can't construct (it needs
                        // widget pointers that live on the main thread).  The
                        // pseudo-backend's name, config schema, default values,
                        // metadata schema, and human-readable description live
                        // here so @c stage.cpp can integrate it into @c --list-io,
                        // @c --list-config, @c applyStageConfig, @c classifyStageArg
                        // and @c resolveStagePlan without ever naming SDL types.
                        //
                        // In a build without SDL the accessors return empty
                        // values and callers filter via @ref isAvailable /
                        // @ref isSdlStage so the empties never leak into
                        // user-visible output.

                        /**
                         * @brief Canonical pseudo-backend name (@c "SDL"
                         *        in SDL builds, empty string in stub builds).
                         */
                        static promeki::String sdlStageName();

                        /**
                         * @brief Human-readable one-line description used
                         *        in the @c --list-io row.
                         */
                        static promeki::String sdlDescription();

                        /**
                         * @brief Config spec map for the SDL pseudo-backend.
                         *
                         * Stage.cpp queries this when applying
                         * @c --dc Key:Value overrides and when dumping the
                         * schema via @c --list-config @c SDL.  Empty in
                         * stub builds.
                         */
                        static promeki::MediaIO::Config::SpecMap sdlConfigSpecs();

                        /**
                         * @brief Default config (every spec key populated
                         *        with its default value) for the SDL
                         *        pseudo-backend.  Empty in stub builds.
                         */
                        static promeki::MediaIO::Config sdlDefaultConfig();

                        /**
                         * @brief Default metadata schema for the SDL
                         *        pseudo-backend.  Currently empty even
                         *        in SDL builds — the player consumes no
                         *        container-level metadata.
                         */
                        static promeki::Metadata sdlDefaultMetadata();

                        /**
                         * @brief Injects the default SDL sink into
                         *        @p opts.sinks when the build supports it.
                         *
                         * Returns @c true when a default sink was injected
                         * (SDL build) or @c false when the build has no SDL
                         * to fall back on — callers should treat @c false
                         * as "no default destination available" and bail
                         * with a diagnostic.
                         *
                         * @param opts Parsed CLI options.  On return, may
                         *             have one additional @c StageSpec
                         *             pushed onto @c sinks.
                         */
                        bool injectDefaultSink(Options &opts);

                        /**
                         * @brief Builds an @c SDLPlayerWidget-backed MediaIO
                         *        for @p stage.
                         *
                         * Lazily allocates the shared window / audio output
                         * on first call and reuses them for every later
                         * stage so multi-SDL pipelines share one window.
                         * Returns @c nullptr on failure (and prints a
                         * diagnostic) — in builds without SDL this is
                         * always a failure.
                         *
                         * The returned MediaIO is owned by the @c SdlSupport
                         * (transitively through the @c SDLPlayerWidget
                         * parent chain) and must NOT be deleted by the
                         * caller; @c teardown() / the destructor will free
                         * the whole tree.
                         */
                        promeki::MediaIO *createStage(const promeki::MediaPipelineConfig::Stage &stage);

                        /**
                         * @brief Wires the SDL window's @c closedSignal to
                         *        @c Application::quit(0).
                         *
                         * Allows the user to quit mediaplay by clicking the
                         * SDL window's close button.  No-op until a window
                         * has been built via @ref createStage, and no-op in
                         * builds without SDL.
                         */
                        void connectWindowClosedToQuit();

                        /**
                         * @brief Releases every SDL resource owned by the
                         *        helper.  Idempotent.
                         *
                         * Safe to call from any early-exit path; the
                         * destructor calls this too so RAII paths do not
                         * need to repeat it.
                         */
                        void teardown();

                private:
                        SdlSupportData *_d; ///< Opaque PIMPL — non-null in SDL builds, null in stub builds.
        };

} // namespace mediaplay
