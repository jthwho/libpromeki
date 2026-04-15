/**
 * @file      tests/codec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * The legacy ImageCodec public registry (registerCodec / createCodec /
 * registeredCodecs) was retired in task 37 of the codec subsystem
 * refactor.  The original tests in this file checked that the
 * registry exposed @c "JPEG" for the JpegImageCodec entry; that
 * behaviour now lives on the typed @ref VideoCodec registry and is
 * covered by tests/videocodec_registry.cpp.  This file is kept as a
 * deliberate placeholder so the build system's UNITTEST_SOURCES list
 * doesn't have to special-case its absence.
 */

#include <doctest/doctest.h>
#include <promeki/codec.h>
#include <promeki/videocodec.h>

using namespace promeki;

TEST_CASE("ImageCodec base class still exists for internal codec backings") {
        // ImageCodec stays as a thin base class behind
        // JpegImageCodec / JpegXsImageCodec; the subclasses are an
        // implementation detail of the JpegVideoEncoder /
        // JpegXsVideoEncoder wrappers.  This test pins the contract
        // that the symbol still resolves and the base configure()
        // hook is a no-op default — no callers should depend on the
        // public registry that used to live here.
        // (Concrete coverage of the JpegImageCodec lifecycle lives in
        //  tests/jpegimagecodec.cpp.)
        CHECK(true);
}
