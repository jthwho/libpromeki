/**
 * @file      numname.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <cctype>
#include <promeki/core/numname.h>

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

