/**
 * @file      core/numname.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Deconstructs a numbered name into its prefix, number, and suffix components.
 *
 * Parses strings like "fred-0001", "007 Bond", or "test.098.dpx" into a prefix,
 * a numeric field (with optional zero-padding), and a suffix. Useful for working
 * with numbered file sequences.
 */
class NumName {
	public:
                /**
                 * @brief Parses a string into a NumName.
                 * @param str The string to parse.
                 * @param val Optional output for the parsed numeric value.
                 * @return The parsed NumName, or an invalid NumName if no number is found.
                 */
                static NumName parse(const String &str, int *val = nullptr);

                /** @brief Constructs an invalid (empty) NumName. */
                NumName() = default;

                /**
                 * @brief Constructs a NumName from explicit components.
                 * @param prefix The string before the number.
                 * @param suffix The string after the number.
                 * @param digits The number of digits (including padding).
                 * @param padded True if the number is zero-padded.
                 */
		NumName(const String &prefix, const String &suffix, int digits, bool padded) :
                        px(prefix), sx(suffix), dl(digits), pad(padded) {}

                /**
                 * @brief Constructs a NumName by parsing a string.
                 * @param str The string to parse.
                 */
		NumName(const String &str) { *this = parse(str); }

		/**
                 * @brief Checks whether this NumName contains a valid numeric field.
                 * @return True if at least one digit was found.
                 */
		bool isValid() const { return dl > 0; }

		/**
                 * @brief Generates a full name string with the given numeric value.
                 * @param val The number to insert into the name.
                 * @return The assembled string (prefix + formatted number + suffix).
                 */
		String name(int val) const {
                        return px + String::number(val, 10, pad ? dl : 0, '0') + sx;
                }

		/**
                 * @brief Returns the prefix portion of the name (text before the number).
                 * @return The prefix string.
                 */
		String prefix() const { return px; }

		/**
                 * @brief Returns the suffix portion of the name (text after the number).
                 * @return The suffix string.
                 */
		String suffix() const { return sx; }

		/**
                 * @brief Checks whether the numeric field is zero-padded.
                 * @return True if the number is padded with leading zeros.
                 */
		bool isPadded() const { return pad; }

		/**
                 * @brief Returns the number of digits in the numeric field.
                 * @return The digit count, including any padding digits.
                 */
		int digits() const { return dl; }

		/**
                 * @brief Returns a C-style printf format mask for the numbered name.
                 *
                 * For example, "file.1234.dpx" yields "file.%d.dpx" and
                 * "file.01234.dpx" yields "file.%05d.dpx".
                 *
                 * @return The format mask string.
                 */
		String filemask() const {
                        String mask;
                        if(pad) {
                                mask = "%0";
                                mask += String::number(dl);
                                mask += 'd';
                        } else {
                                mask = "%d";
                        }
                        return px + mask + sx;
                }

		/**
                 * @brief Returns a hash-style mask for the numbered name.
                 *
                 * For example, "file.1234.dpx" yields "file.#.dpx" and
                 * "file.01234.dpx" yields "file.#####.dpx".
                 *
                 * @return The hash mask string.
                 */
                String hashmask() const {
                        return px + String(pad ? dl : 1, '#') + sx;
                }

                /** @brief Returns true if both NumNames have identical components. */
                bool operator==(const NumName &other) const {
                        return dl == other.dl &&
                               pad == other.pad &&
                               px == other.px &&
                               sx == other.sx;
                }

                /** @brief Returns true if the NumNames differ in any component. */
                bool operator!=(const NumName &other) const {
                        return !(*this == other);
                }
                
                /**
                 * @brief Checks whether another NumName belongs to the same sequence.
                 *
                 * Two NumNames are in the same sequence if they share the same prefix
                 * and suffix, and their padding/digit configuration is compatible.
                 *
                 * @param n The NumName to compare against.
                 * @return True if @p n could be part of the same numbered sequence.
                 */
                bool isInSequence(const NumName &n) const {
                        if(!n.isValid() || n.px != px || n.sx != sx) return false;
                        if(n.pad && !pad && n.dl > dl) return false;
                        if(!n.pad && pad && dl > n.dl) return false;
                        if(n.pad && pad && n.dl != dl) return false;
                        return true;
                }

	private:
		String 	        px;                     // Prefix
		String 	        sx;                     // Suffix
		int 		dl      = 0; 	        // Digit length
		bool 		pad     = false;	// Padding

};

PROMEKI_NAMESPACE_END

