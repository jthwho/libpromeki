/*****************************************************************************
 * numname.h
 * May 09, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

// This object allows you to deconstruct a numbered name (ex: "fred-0001", 
// "007 Bond", or "test.098.dpx") into it's componenets.
class NumName {
	public:
                static NumName parse(const String &str, int *val = nullptr);

                NumName() = default;
		NumName(const String &prefix, const String &suffix, int digits, bool padded) :
                        px(prefix), sx(suffix), dl(digits), pad(padded) {}
		NumName(const String &str) { *this = parse(str); }

		/** True if the name is a valid numname */
		bool isValid() const { return dl > 0; }

		/** Returns string */
		String name(int val) const { 
                        return px + String::number(val, 10, pad ? dl : 0, '0') + sx;
                }

		/** Returns the prefix of the numname (string before the number) */
		String prefix() const { return px; }

		/** Returns the suffix of the numname (string after the number) */
		String suffix() const { return sx; }

		/** Returns the number of padding digits in the number (or 0 if none, or -1 if invalid) */
		bool isPadded() const { return pad; }

		/** Returns the number of digits in the value (including any padding digits) */
		int digits() const { return dl; }

		/** Returns the C style filemask of the numname.  
		 * ex: file.1234.dpx will yield file.%d.dpx 
		 * of file.01234.dpx will yield file.%05d.dpx */
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

		/** Returns the hashmask of the numname.  
		 * ex: file.1234.dpx will yield file.#.dpx, 
		 * or file.01234.dpx will yield file.#####.dpx */
                String hashmask() const {
                        return px + String(pad ? dl : 1, '#') + sx;
                }

                bool operator==(const NumName &other) const {
                        return dl == other.dl &&
                               pad == other.pad &&
                               px == other.px &&
                               sx == other.sx;
                }

                bool operator!=(const NumName &other) const {
                        return !(*this == other);
                }

	private:
		String 	        px;                     // Prefix
		String 	        sx;                     // Suffix
		int 		dl      = 0; 	        // Digit length
		bool 		pad     = false;	// Padding

};

PROMEKI_NAMESPACE_END

