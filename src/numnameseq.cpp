/*****************************************************************************
 * numnameseq.cpp
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

#include <promeki/numnameseq.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

NumNameSeq::List NumNameSeq::parseList(StringList &list) {
        List ret;
        auto item = list.begin();
        while(item != list.end()) {
                int value;
                NumName name = NumName::parse(*item, &value);
                // If the list item can't be parsed into a numname,
                // just leave it on the list and continue.
                if(!name.isValid()) {
                        ++item;
                        continue;
                }
                // Check to see if the name matches any of the
                // return items.
                bool found = false;
                for(auto i = ret.begin(); i != ret.end(); ++i) {
                        auto &n = i->_name;
                        if(name.isInSequence(n)) {
                                if((name.isPadded() && !n.isPadded()) || (name.digits() > n.digits())) n = name;
                                if(value < i->_head) i->_head = value;
                                if(value > i->_tail) i->_tail = value;
                                item = list.erase(item);
                                found = true;
                                break;
                        }
                }
                if(found) continue;
                // Ok, not found in the return list, so it's new.
                ret.push_back(NumNameSeq(name, value, value));
                item = list.erase(item);
	}
        return ret;
}


PROMEKI_NAMESPACE_END

