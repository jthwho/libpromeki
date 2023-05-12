/*****************************************************************************
 * numname.cpp
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

#include <cctype>
#include <promeki/numname.h>

PROMEKI_NAMESPACE_BEGIN

NumName NumName::parse(const String &str, int *val) {
	int len = str.size();
	int nStart = -1;	// Point where the number starts
	int nEnd = -1;		// Point where the number ends

	// We first parse the string backwards to find the first digit run.
	for(int i = len - 1; i >= 0; i--) {
		char c = str[i];
		if(isdigit(c) || c == '#') {
			// If this is the first digit, mark the spot
			if(nEnd == -1) nEnd = i;
		} else {
			// If we have already had a digit, mark the start
			if(nEnd != -1 && nStart == -1) {
				nStart = i + 1;
				break;
			}
		}
	}
	if(nEnd != -1 && nStart == -1) nStart = 0;
	
	// If there is no number run, this string is not a NumName.
	if(nEnd == -1) return NumName();

	int dl = nEnd - nStart + 1;
	int pLen = len - nEnd - 1;
	if(val != nullptr) *val = str.mid(nStart, dl).toInt();
	String px = nStart ? str.left(nStart) : String();
	String sx = pLen ? str.right(pLen) : String();
	char fc = str[nStart];
	bool pad = (dl > 1) ? (fc == '0' || fc == '#') : false;
	return NumName(px, sx, dl, pad);
}

PROMEKI_NAMESPACE_END

