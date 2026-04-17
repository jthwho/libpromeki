/**
 * @file      audioblock.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/audioblock.h>
#include <promeki/audiodesc.h>
#include <promeki/string.h>

using namespace promeki;

TEST_CASE("AudioBlock: source-only config") {
        AudioBlock::Config cfg;
        cfg.sourceChannels = 2;
        cfg.sinkChannels = 0;
        AudioBlock block(cfg);
        CHECK(block.isSource());
        CHECK_FALSE(block.isSink());
        CHECK(block.sourceChannels() == 2);
        CHECK(block.sinkChannels() == 0);
}

TEST_CASE("AudioBlock: sink-only config") {
        AudioBlock::Config cfg;
        cfg.sourceChannels = 0;
        cfg.sinkChannels = 4;
        AudioBlock block(cfg);
        CHECK_FALSE(block.isSource());
        CHECK(block.isSink());
        CHECK(block.sinkChannels() == 4);
}

TEST_CASE("AudioBlock: source/sink config") {
        AudioBlock::Config cfg;
        cfg.sourceChannels = 2;
        cfg.sinkChannels = 2;
        AudioBlock block(cfg);
        CHECK(block.isSource());
        CHECK(block.isSink());
}

TEST_CASE("AudioBlock: isSourceValid") {
        AudioBlock::Config cfg;
        cfg.sourceChannels = 2;
        AudioBlock block(cfg);
        CHECK(block.isSourceValid(0));
        CHECK(block.isSourceValid(1));
        CHECK_FALSE(block.isSourceValid(2));
}

TEST_CASE("AudioBlock: isSinkValid") {
        AudioBlock::Config cfg;
        cfg.sinkChannels = 3;
        AudioBlock block(cfg);
        CHECK(block.isSinkValid(0));
        CHECK(block.isSinkValid(2));
        CHECK_FALSE(block.isSinkValid(3));
}

TEST_CASE("AudioBlock: sourceSamplesAvailable default") {
        AudioBlock::Config cfg;
        cfg.sourceChannels = 1;
        AudioBlock block(cfg);
        CHECK(block.sourceSamplesAvailable(0) == -1);
}

TEST_CASE("AudioBlock: sinkSamplesAllowed default") {
        AudioBlock::Config cfg;
        cfg.sinkChannels = 1;
        AudioBlock block(cfg);
        CHECK(block.sinkSamplesAllowed(0) == -1);
}
