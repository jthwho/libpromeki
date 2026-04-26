/**
 * @file      numname.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <cctype>
#include <promeki/numname.h>

PROMEKI_NAMESPACE_BEGIN

NumName NumName::parse(const String &str, int *val) {
        int len = str.size();
        int nStart = -1; // Point where the number starts
        int nEnd = -1;   // Point where the number ends

        // We first parse the string backwards to find the first digit run.
        for (int i = len - 1; i >= 0; i--) {
                char c = str[i];
                if (isdigit(c) || c == '#') {
                        // If this is the first digit, mark the spot
                        if (nEnd == -1) nEnd = i;
                } else {
                        // If we have already had a digit, mark the start
                        if (nEnd != -1 && nStart == -1) {
                                nStart = i + 1;
                                break;
                        }
                }
        }
        if (nEnd != -1 && nStart == -1) nStart = 0;

        // If there is no number run, this string is not a NumName.
        if (nEnd == -1) return NumName();

        int dl = nEnd - nStart + 1;
        int pLen = len - nEnd - 1;
        if (val != nullptr) *val = str.mid(nStart, dl).toInt();
        String px = nStart ? str.left(nStart) : String();
        String sx = pLen ? str.right(pLen) : String();
        char   fc = str[nStart];
        bool   pad = (dl > 1) ? (fc == '0' || fc == '#') : false;
        return NumName(px, sx, dl, pad);
}

NumName NumName::fromMask(const String &mask) {
        const int len = mask.size();
        if (len == 0) return NumName();

        // Scan left-to-right looking for the first placeholder.  We accept
        // either a printf-style %[0N]d token or a run of '#' characters.
        for (int i = 0; i < len; i++) {
                const char c = mask[i];

                if (c == '%') {
                        // printf-style: %[0]?N?d
                        int  j = i + 1;
                        bool zero = false;
                        if (j < len && mask[j] == '0') {
                                zero = true;
                                j++;
                        }
                        int widthStart = j;
                        while (j < len && isdigit(mask[j])) j++;
                        int widthLen = j - widthStart;
                        if (j >= len || mask[j] != 'd') {
                                // Not a valid %d token — skip past this %
                                continue;
                        }
                        int width = 0;
                        if (widthLen > 0) {
                                width = mask.mid(widthStart, widthLen).toInt();
                        }
                        // Without an explicit width, "%d" is non-padded with
                        // 1 digit.  "%0d" is odd (zero-pad to 0 chars) — we
                        // treat it the same as "%d".  "%04d" yields padded
                        // 4-digit.  A bare "%4d" (no zero flag) is space
                        // padding, which doesn't make sense for filenames,
                        // so we still take its digit count but mark it as
                        // non-padded.
                        int    digits = (width > 0) ? width : 1;
                        bool   pad = (zero && width > 0);
                        String px = (i > 0) ? mask.left(i) : String();
                        String sx = ((j + 1) < len) ? mask.right(len - j - 1) : String();
                        return NumName(px, sx, digits, pad);
                }

                if (c == '#') {
                        // hash-style: one or more '#' characters
                        int j = i;
                        while (j < len && mask[j] == '#') j++;
                        int digits = j - i;
                        // A single '#' is non-padded, matching "%d".
                        // Multiple '#'s are zero-padded, matching "%0Nd".
                        bool   pad = (digits > 1);
                        String px = (i > 0) ? mask.left(i) : String();
                        String sx = (j < len) ? mask.right(len - j) : String();
                        return NumName(px, sx, digits, pad);
                }
        }

        return NumName();
}

PROMEKI_NAMESPACE_END
