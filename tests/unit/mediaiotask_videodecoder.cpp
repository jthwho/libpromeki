/**
 * @file      tests/mediaiotask_videodecoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Integration tests for the generic video-decoder MediaIO backend,
 * driven via the in-process passthrough VideoDecoder defined in
 * tests/videocodec.cpp.  No GPU / NVDEC runtime involvement.
 */

#include <doctest/doctest.h>
#include <promeki/config.h>
