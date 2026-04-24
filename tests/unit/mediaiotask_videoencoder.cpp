/**
 * @file      tests/mediaiotask_videoencoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Integration tests for the generic video-encoder MediaIO backend.
 * Uses the in-process "Passthrough" VideoEncoder registered by
 * tests/videocodec.cpp so the checks run everywhere — no GPU, NVENC
 * runtime, or Video Codec SDK required.  A separate device-gated
 * NVENC integration exercise belongs with the CUDA + NVENC tests.
 */

#include <doctest/doctest.h>
#include <promeki/mediaio.h>
