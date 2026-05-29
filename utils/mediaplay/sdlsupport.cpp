/**
 * @file      mediaplay/sdlsupport.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Two implementations of @ref SdlSupport selected by the
 * @c MEDIAPLAY_HAVE_SDL macro: the SDL branch wires up the real
 * @c SDLWindow / @c SDLAudioOutput / @c SDLPlayerWidget chain, the stub
 * branch is a no-op shell so @c main.cpp can call the same methods in
 * either build.  The @c \#if at the top of this file is the only one
 * in mediaplay's source — everything else stays SDL-agnostic.
 */

#include "sdlsupport.h"

#include "cli.h"
#include "stage.h"

#include <cstdio>

#if MEDIAPLAY_HAVE_SDL

#include <promeki/application.h>
#include <promeki/mediaconfig.h>
#include <promeki/rect.h>
#include <promeki/size2d.h>
#include <promeki/variantspec.h>

#include <promeki/sdl/sdlaudiooutput.h>
#include <promeki/sdl/sdlplayer.h>
#include <promeki/sdl/sdlplayerwidget.h>
#include <promeki/sdl/sdlsubsystem.h>
#include <promeki/sdl/sdlvideowidget.h>
#include <promeki/sdl/sdlwindow.h>

using namespace promeki;

namespace mediaplay {

        namespace {

                // Canonical pseudo-backend name for the SDL display sink.
                // Defined inside the SDL branch so it is impossible to
                // accidentally reach it from non-SDL code paths.
                constexpr const char *kSdlStageName = "SDL";

        } // namespace

        // -------------------------------------------------------------
        // PIMPL payload — only the SDL build needs real state.
        // -------------------------------------------------------------

        struct SdlSupportData {
                        SdlSubsystem    subsystem;          ///< RAII over SDL_Init / SDL_Quit + focus tracker.
                        SDLWindow      *window = nullptr;   ///< Shared display window across SDL stages.
                        SDLAudioOutput *audioOutput = nullptr; ///< Shared audio output across SDL stages.
        };

        bool SdlSupport::isAvailable() {
                return true;
        }

        bool SdlSupport::isSdlStage(const String &stageType) {
                return stageType == kSdlStageName;
        }

        String SdlSupport::sdlStageName() {
                return String(kSdlStageName);
        }

        String SdlSupport::sdlDescription() {
                return String("SDL video + audio player (real-time display sink)");
        }

        MediaIO::Config::SpecMap SdlSupport::sdlConfigSpecs() {
                // SDL player config IDs live on @ref MediaConfig
                // (SdlTimingSource / SdlWindowSize / SdlWindowTitle).
                // We rebase off the global spec when one exists so the
                // type / range constraints stay in sync; the default
                // value is overridden here to the mediaplay-specific
                // baseline.
                MediaIO::Config::SpecMap specs;
                auto                     s = [&specs](MediaConfig::ID id, const Variant &def) {
                        const VariantSpec *gs = MediaConfig::spec(id);
                        specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
                };
                s(MediaConfig::SdlTimingSource, String("audio"));
                s(MediaConfig::SdlWindowSize, Size2Du32(1280, 720));
                s(MediaConfig::SdlWindowTitle, String("mediaplay"));
                return specs;
        }

        MediaIO::Config SdlSupport::sdlDefaultConfig() {
                MediaIO::Config cfg;
                cfg.setValidation(SpecValidation::None);
                MediaIO::Config::SpecMap specs = sdlConfigSpecs();
                for (auto it = specs.cbegin(); it != specs.cend(); ++it) {
                        const Variant &def = it->second.defaultValue();
                        if (def.isValid()) cfg.set(it->first, def);
                }
                cfg.setValidation(SpecValidation::Warn);
                cfg.set(MediaConfig::Type, String(kSdlStageName));
                return cfg;
        }

        Metadata SdlSupport::sdlDefaultMetadata() {
                // The SDL player doesn't consume container-level metadata
                // — it just renders images and plays audio.  The schema
                // is intentionally empty so the --help dump says "(none)".
                return Metadata();
        }

        SdlSupport::SdlSupport() : _d(new SdlSupportData) {
        }

        SdlSupport::~SdlSupport() {
                teardown();
                delete _d;
                _d = nullptr;
        }

        bool SdlSupport::injectDefaultSink(Options &opts) {
                StageSpec sdlSpec;
                sdlSpec.type = kSdlStageName;
                opts.sinks.pushToBack(sdlSpec);
                return true;
        }

        MediaIO *SdlSupport::createStage(const MediaPipelineConfig::Stage &stage) {
                const String sdlTiming = stage.config.getAs<String>(MediaConfig::SdlTimingSource, String("audio"));
                const bool   useAudioClock = (sdlTiming == String("audio"));
                const Size2Du32 sdlWinSize =
                        stage.config.getAs<Size2Du32>(MediaConfig::SdlWindowSize, Size2Du32(1280, 720));
                const String sdlWinTitle =
                        stage.config.getAs<String>(MediaConfig::SdlWindowTitle, String("mediaplay"));

                if (_d->window == nullptr) {
                        if (!sdlWinSize.isValid()) {
                                fprintf(stderr, "Error: invalid SDL WindowSize\n");
                                return nullptr;
                        }
                        _d->window = new SDLWindow(sdlWinTitle, static_cast<int>(sdlWinSize.width()),
                                                   static_cast<int>(sdlWinSize.height()));
                        _d->window->show();
                        _d->audioOutput = new SDLAudioOutput(_d->window);
                }

                const Rect2Di32 fullRect(0, 0, static_cast<int>(sdlWinSize.width()),
                                         static_cast<int>(sdlWinSize.height()));

                auto *w = new SDLPlayerWidget(_d->audioOutput, useAudioClock, _d->window);
                w->setGeometry(fullRect);
                _d->window->resizedSignal.connect(
                        [w](Size2Di32 sz) { w->setGeometry(Rect2Di32(0, 0, sz.width(), sz.height())); });
                w->setFocus();
                SdlSubsystem::instance()->setFocusedWidget(w);

                MediaIO *player = w->mediaIO();
                if (player == nullptr) {
                        fprintf(stderr, "Error: SDL player creation failed for stage '%s'\n", stage.name.cstr());
                        return nullptr;
                }
                return player;
        }

        void SdlSupport::connectWindowClosedToQuit() {
                if (_d == nullptr || _d->window == nullptr) return;
                _d->window->closedSignal.connect([]() { Application::quit(0); });
        }

        void SdlSupport::teardown() {
                if (_d == nullptr) return;
                delete _d->audioOutput;
                _d->audioOutput = nullptr;
                delete _d->window;
                _d->window = nullptr;
        }

} // namespace mediaplay

#else // MEDIAPLAY_HAVE_SDL

namespace mediaplay {

        // -------------------------------------------------------------
        // Stub implementation — every method is a no-op / "no" answer.
        //
        // The PIMPL payload stays @c nullptr so the destructor has
        // nothing to free, and the predicate methods always report
        // "SDL not available" so callers route around the SDL paths
        // without needing their own @c \#if gating.
        // -------------------------------------------------------------

        struct SdlSupportData {};

        bool SdlSupport::isAvailable() {
                return false;
        }

        bool SdlSupport::isSdlStage(const promeki::String &) {
                return false;
        }

        promeki::String SdlSupport::sdlStageName() {
                return promeki::String();
        }

        promeki::String SdlSupport::sdlDescription() {
                return promeki::String();
        }

        promeki::MediaIO::Config::SpecMap SdlSupport::sdlConfigSpecs() {
                return promeki::MediaIO::Config::SpecMap();
        }

        promeki::MediaIO::Config SdlSupport::sdlDefaultConfig() {
                return promeki::MediaIO::Config();
        }

        promeki::Metadata SdlSupport::sdlDefaultMetadata() {
                return promeki::Metadata();
        }

        SdlSupport::SdlSupport() : _d(nullptr) {
        }

        SdlSupport::~SdlSupport() {
                // _d is always null in the stub branch; the destructor
                // is here purely so the type is destructible from main.
        }

        bool SdlSupport::injectDefaultSink(Options &) {
                return false;
        }

        promeki::MediaIO *SdlSupport::createStage(const promeki::MediaPipelineConfig::Stage &) {
                // main is expected to filter via isSdlStage() before
                // calling this; if it ever fires we have a logic bug,
                // but failing soft (nullptr + diagnostic) is friendlier
                // than aborting.
                fprintf(stderr, "Error: SDL stage requested in a build without SDL support\n");
                return nullptr;
        }

        void SdlSupport::connectWindowClosedToQuit() {
        }

        void SdlSupport::teardown() {
        }

} // namespace mediaplay

#endif // MEDIAPLAY_HAVE_SDL
