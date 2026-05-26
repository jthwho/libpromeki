/**
 * @file      cases.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Per-suite registration hooks for promeki-test case files.
 *
 * Each suite source file declares a @c registerXxxCases() entry point
 * here and @c main.cpp calls every hook after parsing command-line
 * arguments.  Suites read the global @c TestParams populated by
 * @c main to decide which cases to register (e.g. which video format
 * the roundtrip suite drives the matrix at).
 */

#pragma once

#include <promeki/filepath.h>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

namespace promekitest {

        /**
         * @brief Registers every roundtrip case derived from the
         *        library's @ref MediaIOFactory and @ref VideoCodec
         *        registries.
         *
         * One case per (backend, extension, codec, pixel-format) tuple
         * — the same matrix the legacy @c roundtrip-functest enumerates,
         * exposed as individual @ref TestCase objects so the runner's
         * regex filter can pin a single combination.  The pipeline is
         * TPG → (encoder) → file sink, then file source → (decoder) →
         * @ref InspectorMediaIO so a container-mux quirk shows up as
         * a discontinuity.
         */
        void registerRoundtripCases();

        /**
         * @brief Registers one case per (codec, backend) pair where
         *        both encode and decode are registered.
         *
         * The pipeline is TPG → @ref VideoEncoder (codec, backend
         * pinned) → @ref VideoDecoder (codec, backend pinned) →
         * @ref InspectorMediaIO with no file in between, isolating
         * the codec's compress→decompress fidelity from any container.
         * Mirrors the matrix @c scripts/roundtrip-codecs.sh runs
         * through @c mediaplay.
         */
        void registerCodecCases();

        /**
         * @brief Registers single-process FrameBridge roundtrip cases.
         *
         * FrameBridge is designed for cross-process IPC; this suite
         * exercises it with both ends in the same process so a
         * single-process regression in the bridge plumbing (shared
         * memory ring, metadata reservation, sync handshake) shows
         * up cheaply.  Cross-process testing remains out of scope
         * for the in-process runner.
         */
        void registerFrameBridgeCases();

        /**
         * @brief Registers AudioFile roundtrip cases.
         *
         * One case per AudioFile-supported container — WAV, BWF,
         * AIFF, OGG.  Each runs TPG audio-only through the
         * @ref AudioFileMediaIO sink, then reads it back through
         * the source into @ref InspectorMediaIO so the audio
         * sample-count and timestamp continuity get validated
         * end-to-end.
         */
        void registerAudioCases();

        /**
         * @brief Registers RTP transport roundtrip cases.
         *
         * Each case runs two pipelines on the main event loop —
         * TPG → @ref RtpMediaIO (sink) on the TX side, and
         * @ref RtpMediaIO (source) → @ref InspectorMediaIO on the RX
         * side — connected over @c 127.0.0.1.  TX writes the SDP at
         * open time and RX consumes that on-disk SDP, so the
         * test exercises the SDP writer and parser as part of the
         * round-trip.  Wire formats covered today: MJPEG (RFC 2435)
         * and 8-bit interleaved RFC 4175 raw video, with L16 audio.
         */
        void registerRtpCases();

        /**
         * @brief Registers the ffmpeg-rawvideo RTP interop case.
         *
         * Spins up an in-process TPG → @ref RtpMediaIO RFC 4175 sender
         * on a loopback UDP port and exercises the on-disk SDP through
         * an out-of-process @c ffmpeg subprocess pointed at @c rawvideo.
         * The case Skips cleanly when @c ffmpeg is not on @c PATH; on a
         * machine that has it, the case verifies that ffmpeg's RFC
         * 4175 demuxer accepts our SDP and produces a non-zero output
         * file.  Proves out the third-party-receiver path that an
         * in-process round-trip cannot.
         */
        void registerRtpFfmpegCases();

        /**
         * @brief Registers RTP receiver-correctness chaos cases.
         *
         * Each case spins up a TPG → RFC 2435 JPEG RTP loopback round-trip
         * with an @ref RtpChaosShim between TX and RX so the receiver-
         * correctness machinery (sequence tracker, reorder buffer, SSRC
         * pin debounce, stream-anchor captureTime fallback) gets stressed
         * under controlled adversity (loss / reorder / dup / late /
         * ssrcchange / rtcpblocked).
         */
        void registerRtpChaosCases();

        /**
         * @brief Registers CEA-608 SubRip caption round-trip cases.
         *
         * Drives @ref etc/substest.srt through the TPG ANC caption
         * path into @ref InspectorMediaIO's AncData JSONL dump,
         * then walks that JSONL through @ref Cea608Decoder and
         * re-emits the recovered cue list via @ref SubRip::emit so
         * the round-trip is testable cue-by-cue (and the resulting
         * @c roundtrip.srt is available in the test folder for
         * post-mortem diffing).
         */
        void registerCaptionsCases();

        /**
         * @brief Registers NDI transport roundtrip cases.
         *
         * Each case runs two pipelines on the main event loop —
         * TPG → @ref NdiMediaIO (sink) on the TX side and
         * @ref NdiMediaIO (source) → @ref InspectorMediaIO on the RX
         * side — connected through NDI's mDNS-based discovery on the
         * loopback interface.  The receiver is expected to miss the
         * leading frames while discovery is converging; the test
         * deliberately asserts only that the frames RX *did* see
         * were sequential and self-consistent (zero discontinuities)
         * and that RX received at least half of its configured frame
         * budget.  When @c PROMEKI_ENABLE_NDI is off this function
         * registers nothing.
         */
        void registerNdiCases();

        /**
         * @brief Registers video-signal-carrier MediaConfig round-trip cases.
         *
         * Builds a @ref MediaConfig populated with each new carrier
         * key (@c SdiInputSignal, @c SdiOutputSignal,
         * @c HdmiInputSignal, @c HdmiOutputSignal, @c VideoReference)
         * and round-trips it through JSON, DataStream, and the
         * Variant<->String converter to verify the new types are
         * first-class through the existing config plumbing.
         */
        void registerVideoCarrierCases();

        /**
         * @brief Registers speech-to-text transcription cases driven
         *        by the testmedia corpus.
         *
         * For every entry in @c testmedia/index.json whose
         * @c useCases list contains @c "speech-to-text", registers
         * one case named @c "transcription.<slug>" that decodes the
         * asset through @c "WhisperCpp" and compares the result to
         * the asset's expected transcript file.  When the corpus
         * symlink is absent, the WhisperCpp backend is unregistered,
         * or no model file is staged on the host the suite logs a
         * one-liner and registers nothing (or the per-case body
         * Skips cleanly) so a fresh checkout without testmedia
         * doesn't have to set anything up.
         *
         * @param testmediaRoot Resolved testmedia root (output of
         *                      @ref resolveTestMediaRoot, or empty
         *                      when no candidate was usable).
         */
        void registerTranscriptionCases(const FilePath &testmediaRoot);

} // namespace promekitest

PROMEKI_NAMESPACE_END
