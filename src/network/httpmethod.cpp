/**
 * @file      httpmethod.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/httpmethod.h>

PROMEKI_NAMESPACE_BEGIN

String HttpMethod::wireName() const {
        // valueName() returns the registered string for this Enum value, or
        // empty when the value is invalid.  HTTP wire form is upper-case,
        // which is exactly the casing used in the registration table.
        return valueName();
}

bool HttpMethod::allowsBody() const {
        const int v = value();
        return v == 2  // Post
            || v == 3  // Put
            || v == 5; // Patch
}

PROMEKI_NAMESPACE_END
