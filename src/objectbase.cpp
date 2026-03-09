/**
 * @file      objectbase.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/objectbase.h>
#include <promeki/system.h>

PROMEKI_NAMESPACE_BEGIN

const char *ObjectBase::MetaInfo::name() const {
        if(_demangledName.isEmpty()) {
                _demangledName = System::demangleSymbol(_name);
        }
        return _demangledName.cstr();
}

void ObjectBase::MetaInfo::dumpToLog() const {
        String pad;
        for(const MetaInfo *info = this; info != nullptr; info = info->parent()) {
                promekiInfo("%s%s", pad.cstr(), info->name());
                for(size_t i = 0; i < info->signalList().size(); ++i) {
                        const SignalMeta *signal = info->signalList()[i];
                        promekiInfo("%s  SIGNAL %d: %s", pad.cstr(), (int)i, signal->name());
                }
                for(size_t i = 0; i < info->slotList().size(); ++i) {
                        const SlotMeta *slot = info->slotList()[i];
                        promekiInfo("%s  SLOT   %d: %s", pad.cstr(), (int)i, slot->name());
                }
                pad += "  ";
        }
        return;
}

PROMEKI_NAMESPACE_END

