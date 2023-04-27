/*****************************************************************************
 * error.h
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
#include <promeki/string.h>

namespace promeki {

class Error {
        public:
                static void registerErrorCode(int code, const String &desc);
                static String errorCodeDesc(int code);

                Error(int code = 0) : _code(code) {

                }

                ~Error() {

                }

                int code() const {
                        return _code;
                }

                bool isOk() const {
                        return _code == 0;
                }

                bool isError() const {
                        return _code != 0;
                }

                String desc() const {
                        return errorCodeDesc(_code);
                }

        private:
                int     _code;
};

} // namespace promeki


