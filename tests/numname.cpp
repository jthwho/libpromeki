/**
 * @file      numname.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/numname.h>
#include <promeki/core/numnameseq.h>
#include <promeki/core/stringlist.h>
#include <promeki/core/logger.h>

using namespace promeki;

// ============================================================================
// NumName Construction
// ============================================================================

TEST_CASE("NumName_DefaultConstruction") {
        NumName n;
        CHECK_FALSE(n.isValid());
        CHECK(n.digits() == 0);
        CHECK_FALSE(n.isPadded());
}

TEST_CASE("NumName_ExplicitConstruction") {
        NumName n("test.", ".dpx", 5, true);
        CHECK(n.isValid());
        CHECK(n.prefix() == "test.");
        CHECK(n.suffix() == ".dpx");
        CHECK(n.digits() == 5);
        CHECK(n.isPadded());
}

// ============================================================================
// NumName::parse - Padded numbers
// ============================================================================

TEST_CASE("NumName_ParsePadded") {
        NumName n("test.00004.dpx");
        CHECK(n.isValid());
        CHECK(n.prefix() == "test.");
        CHECK(n.suffix() == ".dpx");
        CHECK(n.isPadded());
        CHECK(n.digits() == 5);
        CHECK(n.hashmask() == "test.#####.dpx");
        CHECK(n.filemask() == "test.%05d.dpx");
        CHECK(n.name(6438) == "test.06438.dpx");
}

TEST_CASE("NumName_ParsePaddedWithValue") {
        int val = -1;
        NumName n = NumName::parse("frame.0042.exr", &val);
        CHECK(n.isValid());
        CHECK(val == 42);
        CHECK(n.prefix() == "frame.");
        CHECK(n.suffix() == ".exr");
        CHECK(n.digits() == 4);
        CHECK(n.isPadded());
}

// ============================================================================
// NumName::parse - Non-padded numbers
// ============================================================================

TEST_CASE("NumName_ParseNonPadded") {
        NumName n("file123.txt");
        CHECK(n.isValid());
        CHECK(n.prefix() == "file");
        CHECK(n.suffix() == ".txt");
        CHECK_FALSE(n.isPadded());
        CHECK(n.digits() == 3);
}

TEST_CASE("NumName_ParseNonPaddedHashmask") {
        NumName n("file123.txt");
        CHECK(n.hashmask() == "file#.txt");
}

TEST_CASE("NumName_ParseNonPaddedFilemask") {
        NumName n("file123.txt");
        CHECK(n.filemask() == "file%d.txt");
}

TEST_CASE("NumName_ParseNonPaddedWithValue") {
        int val = -1;
        NumName n = NumName::parse("image1234.png", &val);
        CHECK(n.isValid());
        CHECK(val == 1234);
        CHECK_FALSE(n.isPadded());
}

// ============================================================================
// NumName::parse - Edge cases
// ============================================================================

TEST_CASE("NumName_ParseNumberAtStart") {
        NumName n("007bond.mov");
        CHECK(n.isValid());
        CHECK(n.prefix() == "");
        CHECK(n.suffix() == "bond.mov");
        CHECK(n.digits() == 3);
        CHECK(n.isPadded());
}

TEST_CASE("NumName_ParseNumberAtEnd") {
        NumName n("frame001");
        CHECK(n.isValid());
        CHECK(n.prefix() == "frame");
        CHECK(n.suffix() == "");
        CHECK(n.digits() == 3);
        CHECK(n.isPadded());
}

TEST_CASE("NumName_ParseJustNumber") {
        int val = -1;
        NumName n = NumName::parse("42", &val);
        CHECK(n.isValid());
        CHECK(n.prefix() == "");
        CHECK(n.suffix() == "");
        CHECK(val == 42);
        CHECK_FALSE(n.isPadded());
}

TEST_CASE("NumName_ParseJustPaddedNumber") {
        int val = -1;
        NumName n = NumName::parse("0042", &val);
        CHECK(n.isValid());
        CHECK(val == 42);
        CHECK(n.isPadded());
        CHECK(n.digits() == 4);
}

TEST_CASE("NumName_ParseNoNumber") {
        NumName n("nodigits.txt");
        CHECK_FALSE(n.isValid());
}

TEST_CASE("NumName_ParseEmptyString") {
        NumName n("");
        CHECK_FALSE(n.isValid());
}

TEST_CASE("NumName_ParseSingleDigit") {
        NumName n("test5.dpx");
        CHECK(n.isValid());
        CHECK(n.prefix() == "test");
        CHECK(n.suffix() == ".dpx");
        CHECK(n.digits() == 1);
        CHECK_FALSE(n.isPadded());
}

TEST_CASE("NumName_ParseMultipleNumberRuns") {
        // parse() finds the last digit run when scanning from the right
        int val = -1;
        NumName n = NumName::parse("v2_shot_0045.exr", &val);
        CHECK(n.isValid());
        CHECK(val == 45);
        CHECK(n.prefix() == "v2_shot_");
        CHECK(n.suffix() == ".exr");
        CHECK(n.isPadded());
        CHECK(n.digits() == 4);
}

// ============================================================================
// NumName::name - reconstruction
// ============================================================================

TEST_CASE("NumName_NamePadded") {
        NumName n("render.", ".exr", 6, true);
        CHECK(n.name(1) == "render.000001.exr");
        CHECK(n.name(123456) == "render.123456.exr");
        CHECK(n.name(0) == "render.000000.exr");
}

TEST_CASE("NumName_NameNonPadded") {
        NumName n("img", ".png", 3, false);
        CHECK(n.name(1) == "img1.png");
        CHECK(n.name(999) == "img999.png");
        CHECK(n.name(10000) == "img10000.png");
}

// ============================================================================
// NumName comparison operators
// ============================================================================

TEST_CASE("NumName_Equality") {
        NumName a("test.", ".dpx", 5, true);
        NumName b("test.", ".dpx", 5, true);
        NumName c("test.", ".dpx", 4, true);
        CHECK(a == b);
        CHECK_FALSE(a == c);
        CHECK(a != c);
        CHECK_FALSE(a != b);
}

TEST_CASE("NumName_EqualityDifferentPrefix") {
        NumName a("foo.", ".dpx", 5, true);
        NumName b("bar.", ".dpx", 5, true);
        CHECK(a != b);
}

TEST_CASE("NumName_EqualityDifferentSuffix") {
        NumName a("test.", ".dpx", 5, true);
        NumName b("test.", ".exr", 5, true);
        CHECK(a != b);
}

TEST_CASE("NumName_EqualityDifferentPadding") {
        NumName a("test.", ".dpx", 5, true);
        NumName b("test.", ".dpx", 5, false);
        CHECK(a != b);
}

// ============================================================================
// NumName::isInSequence
// ============================================================================

TEST_CASE("NumName_IsInSequence_SamePadded") {
        NumName a("test.", ".dpx", 5, true);
        NumName b("test.", ".dpx", 5, true);
        CHECK(a.isInSequence(b));
        CHECK(b.isInSequence(a));
}

TEST_CASE("NumName_IsInSequence_DifferentPrefix") {
        NumName a("test.", ".dpx", 5, true);
        NumName b("other.", ".dpx", 5, true);
        CHECK_FALSE(a.isInSequence(b));
}

TEST_CASE("NumName_IsInSequence_DifferentSuffix") {
        NumName a("test.", ".dpx", 5, true);
        NumName b("test.", ".exr", 5, true);
        CHECK_FALSE(a.isInSequence(b));
}

TEST_CASE("NumName_IsInSequence_PaddedDifferentDigits") {
        // Both padded but different digit counts are not in sequence
        NumName a("test.", ".dpx", 5, true);
        NumName b("test.", ".dpx", 4, true);
        CHECK_FALSE(a.isInSequence(b));
}

TEST_CASE("NumName_IsInSequence_NonPaddedDifferentDigits") {
        // Both non-padded, different digit counts are in sequence
        NumName a("test.", ".dpx", 3, false);
        NumName b("test.", ".dpx", 4, false);
        CHECK(a.isInSequence(b));
}

TEST_CASE("NumName_IsInSequence_PaddedVsNonPadded") {
        // n is padded with more digits than this (non-padded) -> not in sequence
        NumName a("test.", ".dpx", 3, false);
        NumName b("test.", ".dpx", 5, true);
        CHECK_FALSE(a.isInSequence(b));

        // n is non-padded, this is padded with more digits -> not in sequence
        NumName c("test.", ".dpx", 5, true);
        NumName d("test.", ".dpx", 3, false);
        CHECK_FALSE(c.isInSequence(d));

        // n is padded, this is non-padded, but n has fewer/equal digits -> in sequence
        NumName e("test.", ".dpx", 5, false);
        NumName f("test.", ".dpx", 3, true);
        CHECK(e.isInSequence(f));

        // n is non-padded, this is padded with fewer/equal digits -> in sequence
        NumName g("test.", ".dpx", 3, true);
        NumName h("test.", ".dpx", 5, false);
        CHECK(g.isInSequence(h));
}

TEST_CASE("NumName_IsInSequence_Invalid") {
        NumName valid("test.", ".dpx", 5, true);
        NumName invalid;
        CHECK_FALSE(valid.isInSequence(invalid));
}

// ============================================================================
// NumName filemask and hashmask
// ============================================================================

TEST_CASE("NumName_Filemask") {
        SUBCASE("Padded") {
                NumName n("shot.", ".exr", 4, true);
                CHECK(n.filemask() == "shot.%04d.exr");
        }
        SUBCASE("Non-padded") {
                NumName n("shot.", ".exr", 4, false);
                CHECK(n.filemask() == "shot.%d.exr");
        }
}

TEST_CASE("NumName_Hashmask") {
        SUBCASE("Padded") {
                NumName n("shot.", ".exr", 4, true);
                CHECK(n.hashmask() == "shot.####.exr");
        }
        SUBCASE("Non-padded") {
                NumName n("shot.", ".exr", 4, false);
                CHECK(n.hashmask() == "shot.#.exr");
        }
}

// ============================================================================
// NumNameSeq
// ============================================================================

TEST_CASE("NumNameSeq_DefaultConstruction") {
        NumNameSeq seq;
        CHECK_FALSE(seq.isValid());
}

TEST_CASE("NumNameSeq_Construction") {
        NumName n("test.", ".dpx", 5, true);
        NumNameSeq seq(n, 1, 100);
        CHECK(seq.isValid());
        CHECK(seq.head() == 1);
        CHECK(seq.tail() == 100);
        CHECK(seq.length() == 100);
        CHECK(seq.name() == n);
}

TEST_CASE("NumNameSeq_ParseList") {
        StringList list = {
                "image1234.png",
                "image0001.png",
                "image.png",
                "image2.png",
                "image3.png",
                "image34.png",
                "anotherfile.txt"
        };
        NumNameSeq::List nnl = NumNameSeq::parseList(list);
        CHECK(nnl.size() == 2);
        // Strings that couldn't be parsed as NumNames remain
        CHECK(list.size() == 2);
        CHECK(nnl[0].name().hashmask() == "image####.png");
        CHECK(nnl[1].name().hashmask() == "image#.png");
        CHECK(list[0] == "image.png");
        CHECK(list[1] == "anotherfile.txt");
}

TEST_CASE("NumNameSeq_ParseListEmpty") {
        StringList list;
        NumNameSeq::List nnl = NumNameSeq::parseList(list);
        CHECK(nnl.isEmpty());
        CHECK(list.isEmpty());
}

TEST_CASE("NumNameSeq_ParseListNoNumNames") {
        StringList list = {"hello.txt", "world.txt"};
        NumNameSeq::List nnl = NumNameSeq::parseList(list);
        CHECK(nnl.isEmpty());
        CHECK(list.size() == 2);
}

TEST_CASE("NumNameSeq_ParseListAllNumNames") {
        StringList list = {"img001.exr", "img002.exr", "img003.exr"};
        NumNameSeq::List nnl = NumNameSeq::parseList(list);
        CHECK(nnl.size() == 1);
        CHECK(list.isEmpty());
        CHECK(nnl[0].head() == 1);
        CHECK(nnl[0].tail() == 3);
        CHECK(nnl[0].length() == 3);
}

TEST_CASE("NumNameSeq_ParseListMultipleSequences") {
        StringList list = {
                "shotA.001.exr",
                "shotA.002.exr",
                "shotB.010.exr",
                "shotB.020.exr",
                "readme.txt"
        };
        NumNameSeq::List nnl = NumNameSeq::parseList(list);
        CHECK(nnl.size() == 2);
        CHECK(list.size() == 1);
        CHECK(list[0] == "readme.txt");

        // Verify each sequence's head/tail
        CHECK(nnl[0].name().prefix() == "shotA.");
        CHECK(nnl[0].head() == 1);
        CHECK(nnl[0].tail() == 2);
        CHECK(nnl[0].length() == 2);
        CHECK(nnl[1].name().prefix() == "shotB.");
        CHECK(nnl[1].head() == 10);
        CHECK(nnl[1].tail() == 20);
        CHECK(nnl[1].length() == 11);
}

TEST_CASE("NumNameSeq_ParseListSingleItem") {
        StringList list = {"frame.0100.exr"};
        NumNameSeq::List nnl = NumNameSeq::parseList(list);
        CHECK(nnl.size() == 1);
        CHECK(list.isEmpty());
        CHECK(nnl[0].head() == 100);
        CHECK(nnl[0].tail() == 100);
        CHECK(nnl[0].length() == 1);
}

TEST_CASE("NumNameSeq_ParseListOutOfOrder") {
        // Items arrive in non-sequential order; head/tail should reflect min/max
        StringList list = {
                "render.0050.exr",
                "render.0010.exr",
                "render.0099.exr",
                "render.0001.exr"
        };
        NumNameSeq::List nnl = NumNameSeq::parseList(list);
        CHECK(nnl.size() == 1);
        CHECK(list.isEmpty());
        CHECK(nnl[0].head() == 1);
        CHECK(nnl[0].tail() == 99);
        CHECK(nnl[0].length() == 99);
}

TEST_CASE("NumNameSeq_ParseListNameUpgradePaddedOverNonPadded") {
        // A padded name with MORE digits than a non-padded name is treated as
        // a separate sequence (e.g. "image2.png" vs "image0003.png" are different
        // naming conventions).
        StringList list = {
                "image2.png",
                "image0003.png"
        };
        NumNameSeq::List nnl = NumNameSeq::parseList(list);
        CHECK(nnl.size() == 2);
        CHECK(list.isEmpty());
        // First: non-padded 1-digit
        CHECK_FALSE(nnl[0].name().isPadded());
        CHECK(nnl[0].name().digits() == 1);
        // Second: padded 4-digit
        CHECK(nnl[1].name().isPadded());
        CHECK(nnl[1].name().digits() == 4);
}

TEST_CASE("NumNameSeq_ParseListNameUpgradeSameDigitCount") {
        // A padded name with FEWER or EQUAL digits to a non-padded name
        // IS in the same sequence and upgrades the stored name.
        // "image12.png" -> non-padded, 2 digits
        // "image03.png" -> padded, 2 digits  (same digit count, padded wins)
        StringList list = {
                "image12.png",
                "image03.png"
        };
        NumNameSeq::List nnl = NumNameSeq::parseList(list);
        CHECK(nnl.size() == 1);
        CHECK(list.isEmpty());
        CHECK(nnl[0].name().isPadded());
        CHECK(nnl[0].name().digits() == 2);
        CHECK(nnl[0].head() == 3);
        CHECK(nnl[0].tail() == 12);
}

TEST_CASE("NumNameSeq_ParseListNameUpgradeMoreDigits") {
        // When a name with more digits arrives, it should replace the stored name
        // Both non-padded: "file1.txt" (1 digit) then "file123.txt" (3 digits)
        StringList list = {
                "file1.txt",
                "file123.txt"
        };
        NumNameSeq::List nnl = NumNameSeq::parseList(list);
        CHECK(nnl.size() == 1);
        CHECK(list.isEmpty());
        CHECK(nnl[0].name().digits() == 3);
        CHECK(nnl[0].head() == 1);
        CHECK(nnl[0].tail() == 123);
}

TEST_CASE("NumNameSeq_ParseListDifferentPaddingsSeparateSequences") {
        // Padded names with different digit counts should be separate sequences
        // because isInSequence returns false for padded+padded with different digit counts
        StringList list = {
                "img.001.exr",
                "img.00002.exr"
        };
        NumNameSeq::List nnl = NumNameSeq::parseList(list);
        CHECK(nnl.size() == 2);
        CHECK(list.isEmpty());
        CHECK(nnl[0].name().digits() == 3);
        CHECK(nnl[1].name().digits() == 5);
}

TEST_CASE("NumNameSeq_ParseListSameSequenceDifferentNonPaddedDigitCounts") {
        // Non-padded names with different digit counts ARE in the same sequence
        StringList list = {
                "shot5.mov",
                "shot12.mov",
                "shot100.mov"
        };
        NumNameSeq::List nnl = NumNameSeq::parseList(list);
        CHECK(nnl.size() == 1);
        CHECK(list.isEmpty());
        CHECK(nnl[0].head() == 5);
        CHECK(nnl[0].tail() == 100);
        // Name should have been upgraded to the one with the most digits
        CHECK(nnl[0].name().digits() == 3);
}

TEST_CASE("NumNameSeq_LengthSingleFrame") {
        NumName n("test.", ".dpx", 5, true);
        NumNameSeq seq(n, 42, 42);
        CHECK(seq.length() == 1);
}

TEST_CASE("NumNameSeq_ParseListPreservesNonNumNameOrder") {
        // Non-numname items should remain in their original relative order
        StringList list = {
                "readme.txt",
                "frame001.exr",
                "notes.md",
                "frame002.exr",
                "changelog.txt"
        };
        NumNameSeq::List nnl = NumNameSeq::parseList(list);
        CHECK(nnl.size() == 1);
        CHECK(list.size() == 3);
        CHECK(list[0] == "readme.txt");
        CHECK(list[1] == "notes.md");
        CHECK(list[2] == "changelog.txt");
}

TEST_CASE("NumNameSeq_ParseListDifferentPrefixes") {
        // Same suffix but different prefixes are separate sequences
        StringList list = {
                "alpha001.exr",
                "beta001.exr",
                "alpha002.exr",
                "beta002.exr"
        };
        NumNameSeq::List nnl = NumNameSeq::parseList(list);
        CHECK(nnl.size() == 2);
        CHECK(list.isEmpty());
        CHECK(nnl[0].name().prefix() == "alpha");
        CHECK(nnl[0].head() == 1);
        CHECK(nnl[0].tail() == 2);
        CHECK(nnl[1].name().prefix() == "beta");
        CHECK(nnl[1].head() == 1);
        CHECK(nnl[1].tail() == 2);
}

TEST_CASE("NumNameSeq_ParseListDifferentSuffixes") {
        // Same prefix but different suffixes are separate sequences
        StringList list = {
                "render001.exr",
                "render001.png",
                "render002.exr",
                "render002.png"
        };
        NumNameSeq::List nnl = NumNameSeq::parseList(list);
        CHECK(nnl.size() == 2);
        CHECK(list.isEmpty());
        CHECK(nnl[0].name().suffix() == ".exr");
        CHECK(nnl[1].name().suffix() == ".png");
}
