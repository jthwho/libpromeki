/*****************************************************************************
 * datetime.cpp
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

#include <cmath>
#include <iomanip>
#include <promeki/datetime.h>
#include <promeki/logger.h>

namespace promeki {

static String addSubsecondToFormat(double ns, const char *fmt, bool &jumpSecond) {
        String newfmt;
        jumpSecond = false;
        bool foundSubSecond = false;
        // Read the fmt into the newfmt and parse any of our non standard tokens.
        while(*fmt) {
                char c = *fmt++;
                if(c == '%') {
                        switch(fmt[0]) {
                                // Catch the %*.# formatting to add subsecond digits.
                                case 'S':
                                case 'T':
                                        if(fmt[1] == '.' && std::isdigit(fmt[2])) {
                                                int p = (fmt[2] - 48); // convert to decimal 0 - 9
                                                if(p < 1) p = 1;
                                                double power = std::pow(10, p);
                                                double subsec = std::round(ns * power);
                                                if(subsec >= power) {
                                                        jumpSecond = true;
                                                        subsec = 0.0;
                                                }
                                                newfmt += c;
                                                newfmt += *fmt++;
                                                newfmt += *fmt++;
                                                fmt++;
                                                newfmt += String::number(static_cast<int>(subsec), 10, p, '0');
                                                foundSubSecond = true;
                                                continue;
                                        }
                                        break;
                                default:
                                        // Do Nothing
                                        break;
                        }
                } 
                newfmt += c;
        }
        if(!foundSubSecond) jumpSecond = std::round(ns) >= 1.0;
        return newfmt;
}
       

String DateTime::strftime(const char *fmt, const std::tm &tm) {
        char buf[64];
        size_t ct = std::strftime(buf, sizeof(buf) - 1, fmt, &tm);
        return String(buf, ct);
}

String DateTime::toString(const char *fmt) const {
        bool jumpSecond;
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(_value.time_since_epoch()) % 1000000000;
        String newfmt = addSubsecondToFormat(std::chrono::duration<double>(ns).count(), fmt, jumpSecond);
        auto t = std::chrono::system_clock::to_time_t(_value);
        if(jumpSecond) t++;
        std::tm tm = *std::localtime(&t);
        return strftime(newfmt.cstr(), tm);
}


} // namespace promeki

#if 0
static String formatTime(const std::tm &tm, const std::chrono::microseconds &microseconds, const std::string& format) {
        std::string result;
        bool inside_token = false;
        std::string subsecond_token;
        for (char c : format) {
                if (inside_token) {
                        if (c >= '0' && c <= '9') {
                                subsecond_token += c;
                        } else {
                                inside_token = false;
                                if (c == 'f') {
                                        int num_digits = subsecond_token.empty() ? 6 : std::stoi(subsecond_token);
                                        num_digits = std::clamp(num_digits, 1, 6);
                                        char buffer[7];
                                        std::snprintf(buffer, sizeof(buffer), "%.*ld", num_digits, static_cast<long>(microseconds.count() / static_cast<int>(std::pow(10, 6 - num_digits))));
                                        result += buffer;
                                } else {
                                        char buffer[64];
                                        std::string token = "%" + std::string(1, c);
                                        std::strftime(buffer, sizeof(buffer), token.c_str(), &tm);
                                        result += buffer;
                                }
                        }
                } else if (c == '%') {
                        inside_token = true;
                        subsecond_token.clear();
                } else {
                        result += c;
                }
        }
        if (inside_token) {
                int num_digits = subsecond_token.empty() ? 6 : std::stoi(subsecond_token);
                num_digits = std::clamp(num_digits, 1, 6);
                char buffer[7];
                std::snprintf(buffer, sizeof(buffer), "%.*ld", num_digits, static_cast<long>(microseconds.count() / static_cast<int>(std::pow(10, 6 - num_digits))));
                result += buffer;
        }
        return result;
}

// Returns the current timestamp as a human-readable date and time string.
// The user can pass in a custom format, or use the default format if none is provided.
// The custom token %f followed by an optional integer specifies the subsecond value with the given number of significant digits.
#endif

