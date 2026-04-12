/**
 * @file      tests/mediaio_identifiers.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Tests for MediaIO's per-instance identifiers (localId, name, uuid)
 * and the benchmark stamping hooks added alongside them.  The TPG
 * backend is used throughout because it requires no external files
 * and works as a self-contained reader.
 */

#include <doctest/doctest.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiotask_tpg.h>
#include <promeki/mediaiotask_converter.h>
#include <promeki/mediaconfig.h>
#include <promeki/benchmark.h>
#include <promeki/benchmarkreporter.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/pixeldesc.h>
#include <promeki/uuid.h>

using namespace promeki;

namespace {

/**
 * @brief Helper: build a configured TPG reader for identifier tests.
 *
 * Returns a MediaIO that is ready to open — the caller decides whether
 * to actually open it, since some tests inspect identifiers pre-open
 * while others need resolved-at-open values.
 */
MediaIO *makeTpgReader(bool enableBenchmark = false,
                      const String &nameOverride = String(),
                      const UUID &uuidOverride = UUID()) {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::FrameRate, FrameRate(FrameRate::FPS_2997));
        cfg.set(MediaConfig::VideoEnabled, true);
        if(!nameOverride.isEmpty()) {
                cfg.set(MediaConfig::Name, nameOverride);
        }
        if(uuidOverride.isValid()) {
                cfg.set(MediaConfig::Uuid, uuidOverride);
        }
        if(enableBenchmark) {
                cfg.set(MediaConfig::EnableBenchmark, true);
        }
        return MediaIO::create(cfg);
}

} // namespace

// ============================================================================
// localId
// ============================================================================

TEST_CASE("MediaIO::localId assigns a non-negative value at construction") {
        MediaIO *io = makeTpgReader();
        REQUIRE(io != nullptr);
        CHECK(io->localId() >= 0);
        delete io;
}

TEST_CASE("MediaIO::localId is monotonically increasing across instances") {
        MediaIO *a = makeTpgReader();
        MediaIO *b = makeTpgReader();
        MediaIO *c = makeTpgReader();
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        REQUIRE(c != nullptr);
        CHECK(b->localId() > a->localId());
        CHECK(c->localId() > b->localId());
        delete a;
        delete b;
        delete c;
}

// ============================================================================
// name
// ============================================================================

TEST_CASE("MediaIO::name defaults to media<localId> before open") {
        MediaIO *io = makeTpgReader();
        REQUIRE(io != nullptr);
        String expected = String("media") + String::number(io->localId());
        CHECK(io->name() == expected);
        delete io;
}

TEST_CASE("MediaIO::name default survives open when config::Name is empty") {
        MediaIO *io = makeTpgReader();
        REQUIRE(io != nullptr);
        String expected = String("media") + String::number(io->localId());
        Error err = io->open(MediaIO::Output);
        REQUIRE(err.isOk());
        CHECK(io->name() == expected);
        CHECK(io->config().getAs<String>(MediaConfig::Name) == expected);
        io->close();
        delete io;
}

TEST_CASE("MediaIO::name honors explicit config::Name at open") {
        MediaIO *io = makeTpgReader(false, String("tpg-source"));
        REQUIRE(io != nullptr);
        Error err = io->open(MediaIO::Output);
        REQUIRE(err.isOk());
        CHECK(io->name() == "tpg-source");
        CHECK(io->config().getAs<String>(MediaConfig::Name) == "tpg-source");
        io->close();
        delete io;
}

// ============================================================================
// uuid
// ============================================================================

TEST_CASE("MediaIO::uuid is valid and unique at construction") {
        MediaIO *a = makeTpgReader();
        MediaIO *b = makeTpgReader();
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        CHECK(a->uuid().isValid());
        CHECK(b->uuid().isValid());
        CHECK(a->uuid() != b->uuid());
        delete a;
        delete b;
}

TEST_CASE("MediaIO::uuid honors explicit config::Uuid at open") {
        UUID forced = UUID::generate();
        REQUIRE(forced.isValid());
        MediaIO *io = makeTpgReader(false, String(), forced);
        REQUIRE(io != nullptr);
        Error err = io->open(MediaIO::Output);
        REQUIRE(err.isOk());
        CHECK(io->uuid() == forced);
        CHECK(io->config().getAs<UUID>(MediaConfig::Uuid) == forced);
        io->close();
        delete io;
}

TEST_CASE("MediaIO::uuid default survives open when config::Uuid is unset") {
        MediaIO *io = makeTpgReader();
        REQUIRE(io != nullptr);
        UUID beforeOpen = io->uuid();
        REQUIRE(beforeOpen.isValid());
        Error err = io->open(MediaIO::Output);
        REQUIRE(err.isOk());
        CHECK(io->uuid() == beforeOpen);
        CHECK(io->config().getAs<UUID>(MediaConfig::Uuid) == beforeOpen);
        io->close();
        delete io;
}

// ============================================================================
// Benchmark stamping — reader path
// ============================================================================

TEST_CASE("MediaIO: reader stamps frames when EnableBenchmark is set") {
        MediaIO *io = makeTpgReader(true, String("reader-test"));
        REQUIRE(io != nullptr);
        BenchmarkReporter reporter;
        io->setBenchmarkReporter(&reporter);
        REQUIRE(io->open(MediaIO::Output).isOk());

        // Read a handful of frames.
        for(int i = 0; i < 5; i++) {
                Frame::Ptr frame;
                Error err = io->readFrame(frame);
                REQUIRE(err.isOk());
                REQUIRE(frame.isValid());
                // Each returned frame should carry an attached Benchmark
                // with stamps scoped to this stage's name.
                CHECK(frame->benchmark().isValid());
                if(frame->benchmark().isValid()) {
                        CHECK(frame->benchmark()->size() >= 3);
                }
        }

        // The reporter is a sink by default, so it should have seen every
        // frame we pulled.
        CHECK(reporter.submittedCount() == 5);

        io->close();
        delete io;
}

TEST_CASE("MediaIO: reader does not stamp when EnableBenchmark is false") {
        MediaIO *io = makeTpgReader(false);
        REQUIRE(io != nullptr);
        BenchmarkReporter reporter;
        io->setBenchmarkReporter(&reporter);
        REQUIRE(io->open(MediaIO::Output).isOk());

        // Read a bunch of frames; with benchmarking off they should
        // arrive without an attached benchmark and nothing should
        // reach the reporter.
        for(int i = 0; i < 10; i++) {
                Frame::Ptr frame;
                Error err = io->readFrame(frame);
                REQUIRE(err.isOk());
                REQUIRE(frame.isValid());
                CHECK_FALSE(frame->benchmark().isValid());
        }
        CHECK(reporter.submittedCount() == 0);
        CHECK_FALSE(io->benchmarkEnabled());

        io->close();
        delete io;
}

TEST_CASE("MediaIO: non-sink reader stamps but does not submit") {
        MediaIO *io = makeTpgReader(true);
        REQUIRE(io != nullptr);
        BenchmarkReporter reporter;
        io->setBenchmarkReporter(&reporter);
        io->setBenchmarkIsSink(false);
        REQUIRE(io->open(MediaIO::Output).isOk());

        for(int i = 0; i < 5; i++) {
                Frame::Ptr frame;
                Error err = io->readFrame(frame);
                REQUIRE(err.isOk());
                REQUIRE(frame.isValid());
                CHECK(frame->benchmark().isValid());
        }
        // Sink flag off → reporter never received anything even though
        // the frames are individually stamped.
        CHECK(reporter.submittedCount() == 0);

        io->close();
        delete io;
}

// ============================================================================
// BenchmarkReport / BenchmarkReset params commands
// ============================================================================

TEST_CASE("MediaIO::sendParams BenchmarkReport returns the reporter summary") {
        MediaIO *io = makeTpgReader(true);
        REQUIRE(io != nullptr);
        BenchmarkReporter reporter;
        io->setBenchmarkReporter(&reporter);
        REQUIRE(io->open(MediaIO::Output).isOk());

        // Generate some stamped frames so the report is non-empty.
        for(int i = 0; i < 3; i++) {
                Frame::Ptr frame;
                REQUIRE(io->readFrame(frame).isOk());
        }

        MediaIOParams result;
        Error err = io->sendParams(String("BenchmarkReport"), MediaIOParams(), &result);
        CHECK(err.isOk());
        String report = result.getAs<String>(MediaIO::ParamBenchmarkReport, String());
        CHECK_FALSE(report.isEmpty());

        io->close();
        delete io;
}

TEST_CASE("MediaIO::sendParams BenchmarkReport returns NotSupported without a reporter") {
        MediaIO *io = makeTpgReader(true);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());
        MediaIOParams result;
        Error err = io->sendParams(String("BenchmarkReport"), MediaIOParams(), &result);
        CHECK(err == Error::NotSupported);
        io->close();
        delete io;
}

TEST_CASE("MediaIO::sendParams BenchmarkReset clears accumulated statistics") {
        MediaIO *io = makeTpgReader(true);
        REQUIRE(io != nullptr);
        BenchmarkReporter reporter;
        io->setBenchmarkReporter(&reporter);
        REQUIRE(io->open(MediaIO::Output).isOk());

        for(int i = 0; i < 4; i++) {
                Frame::Ptr frame;
                REQUIRE(io->readFrame(frame).isOk());
        }
        REQUIRE(reporter.submittedCount() == 4);

        Error err = io->sendParams(String("BenchmarkReset"));
        CHECK(err.isOk());
        CHECK(reporter.submittedCount() == 0);

        io->close();
        delete io;
}
