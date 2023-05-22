/*****************************************************************************
 * objectbase.cpp
 * May 19, 2023
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

