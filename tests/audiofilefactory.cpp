/**
 * @file      audiofilefactory.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/audiofilefactory.h>
#include <promeki/string.h>

using namespace promeki;

TEST_CASE("AudioFileFactory: lookup with invalid params returns nullptr") {
        AudioFileFactory::Context ctx;
        ctx.operation = -1;
        ctx.filename = "nonexistent.xyz";
        const AudioFileFactory *f = AudioFileFactory::lookup(ctx);
        CHECK(f == nullptr);
}

TEST_CASE("AudioFileFactory: lookup unsupported extension returns nullptr") {
        AudioFileFactory::Context ctx;
        ctx.operation = 0;
        ctx.filename = "test.unsupported_format_xyz";
        const AudioFileFactory *f = AudioFileFactory::lookup(ctx);
        CHECK(f == nullptr);
}

TEST_CASE("AudioFileFactory: lookup finds factory for wav writer") {
        AudioFileFactory::Context ctx;
        ctx.operation = AudioFile::Writer;
        ctx.filename = "test.wav";
        const AudioFileFactory *f = AudioFileFactory::lookup(ctx);
        REQUIRE(f != nullptr);
        CHECK(f->name() == "libsndfile");
}

TEST_CASE("AudioFileFactory: createForOperation returns valid AudioFile") {
        AudioFileFactory::Context ctx;
        ctx.operation = AudioFile::Writer;
        ctx.filename = "test.wav";
        const AudioFileFactory *f = AudioFileFactory::lookup(ctx);
        REQUIRE(f != nullptr);
        auto [file, err] = f->createForOperation(ctx);
        CHECK(err.isOk());
        CHECK(file.isValid());
}

TEST_CASE("AudioFileFactory: lookup by format hint") {
        AudioFileFactory::Context ctx;
        ctx.operation = AudioFile::Writer;
        ctx.formatHint = "wav";
        const AudioFileFactory *f = AudioFileFactory::lookup(ctx);
        REQUIRE(f != nullptr);
        CHECK(f->name() == "libsndfile");
}
