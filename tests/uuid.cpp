/**
 * @file      uuid.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/unittest.h>
#include <promeki/uuid.h>

using namespace promeki;

PROMEKI_TEST_BEGIN(UUID)
        UUID v1;
        UUID v2 = UUID::generate();
        UUID v3("94eb2454-5116-4814-889f-7eb9bcb58bf1");
        UUID v4("94EB2454-5116-4814-889F-7EB9BCB58BF1");
        UUID v5("94EB2454-X116-4814-889F-7EB9BCB58BF1");

        String u1("91809c4d-3682-4868-800c-05b871b84c0b");
        UUID v6(u1);

        promekiInfo("v1: %s", v1.toString().cstr());
        promekiInfo("v2: %s", v2.toString().cstr());
        promekiInfo("v3: %s", v3.toString().cstr());
        promekiInfo("v4: %s", v4.toString().cstr());
        promekiInfo("v5: %s", v5.toString().cstr());

        PROMEKI_TEST(!v1.isValid());
        PROMEKI_TEST(v2.isValid());
        PROMEKI_TEST(v3.isValid());
        PROMEKI_TEST(v4.isValid());
        PROMEKI_TEST(!v5.isValid());
        PROMEKI_TEST(v3 == v4);
        PROMEKI_TEST(v6.toString() == u1);

PROMEKI_TEST_END()


