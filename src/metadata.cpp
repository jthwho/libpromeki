/*****************************************************************************
 * metadata.cpp
 * April 30, 2023
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

#include <promeki/metadata.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

#define X(name) { Metadata::name, PROMEKI_STRINGIFY(name) },
static std::map<Metadata::ID, String> metadataIDNames = { PROMEKI_ENUM_METADATA_ID };
#undef X

const String &Metadata::idName(ID id) {
        return metadataIDNames[id];
}

PROMEKI_NAMESPACE_END

