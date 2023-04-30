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
#include <map>
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
       

String DateTime::strftime(const std::tm &tm, const char *fmt) {
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
        return strftime(tm, newfmt.cstr());
}

DateTime DateTime::fromNow(const String &description) {
        using namespace std::chrono;
        static const std::map<std::string, system_clock::duration> units = {
                {"second", seconds(1)},
                {"minute", minutes(1)},
                {"hour", hours(1)},
                {"day", hours(24)},
                {"week", hours(24 * 7)}
        };

        std::istringstream iss(description);
        int64_t count = 0;
        std::string token;
        system_clock::duration total_duration = seconds(0);
        int months = 0;
        int years = 0;

        while(iss >> token) {
                // Make the token string lowercase for case-insensitive comparison
                for(char& c : token) c = std::tolower(c);

                // Handle "next" and "previous" tokens
                if(token == "next") {
                        count = 1;
                        continue;
                } else if (token == "previous") {
                        count = -1;
                        continue;
                }

                // Try to parse the token as an integer count
                if(std::istringstream(token) >> count) continue;

                // FIXME: Need to use the String::parseNumberWords()
                //if(parse_number_word(token, count)) continue;

                // Remove trailing 's' if present (e.g., "days" -> "day")
                if (token.back() == 's') token.pop_back();

                // Look up the duration unit in the map and accumulate the total duration
                auto it = units.find(token);
                if (it != units.end()) {
                        total_duration += count * it->second;
                } else if (token == "month") {
                        months += count;
                } else if (token == "year") {
                        years += count;
                }
        }

        // Calculate the future DateTime based on the current time and the total duration
        system_clock::time_point future_time = system_clock::now() + total_duration;

        // Convert time_point to std::tm to handle months and years
        std::time_t future_time_t = system_clock::to_time_t(future_time);
        std::tm future_tm = *std::localtime(&future_time_t);
        future_tm.tm_mon += months;
        future_tm.tm_year += years;

        // Normalize the std::tm structure and convert back to time_point
        std::time_t normalized_time_t = std::mktime(&future_tm);
        future_time = system_clock::from_time_t(normalized_time_t);

        return DateTime(future_time);
}

} // namespace promeki


