/**
 * @file      numnameseq.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

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
                                item = list.remove(item);
                                found = true;
                                break;
                        }
                }
                if(found) continue;
                // Ok, not found in the return list, so it's new.
                ret.pushToBack(NumNameSeq(name, value, value));
                item = list.remove(item);
	}
        return ret;
}


PROMEKI_NAMESPACE_END

