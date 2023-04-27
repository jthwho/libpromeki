/*****************************************************************************
 * util.h
 * April 26, 2023
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

#define PROMEKI_STRINGIFY_IMPL(value) #value
#define PROMEKI_STRINGIFY(value) PROMEKI_STRINGIFY_IMPL(value)
#define PROMEKI_CONCAT_IMPL(v1, v2) v1##v2
#define PROMEKI_CONCAT(v1, v2) PROMEKI_CONCAT_IMPL(v1, v2)
#define PROMEKI_UNIQUE_ID PROMEKI_CONCAT(__LINE__, __COUNTER__)

