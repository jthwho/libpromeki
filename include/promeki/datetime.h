/*****************************************************************************
 * datetime.h
 * April 28, 2023
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

#include <chrono>
#include <ctime>
#include <promeki/string.h>

namespace promeki {

class DateTime {
        public:
                constexpr static const char *defaultFormat = "%Y-%M-%D %H:%M:%S";

                using Value = std::chrono::system_clock::time_point;

                static DateTime now() {
                        return DateTime(std::chrono::system_clock::now());
                }

                static String strftime(const char *format, const std::tm &tm);

                DateTime() { }
                DateTime(const Value &val) : _value(val) { }

                String toString(const char *format = defaultFormat) const;

                Value value() const {
                        return _value;
                }

        private:
                Value   _value;
};


} // namespace promeki
