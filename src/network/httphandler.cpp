/**
 * @file      httphandler.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/httphandler.h>

PROMEKI_NAMESPACE_BEGIN

// All handler logic lives in the header (the abstract base has no
// concrete state, and HttpFunctionHandler is small enough to inline).
// This translation unit exists so the library carries a non-empty
// httphandler.o that linker reports can attribute coverage to.

PROMEKI_NAMESPACE_END
