/**
 * @file      datetime.cpp
 * @copyright Jason Howard. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <cstdlib>
#include <promeki/datetime.h>
#include <promeki/datastream.h>
#include <promeki/map.h>
#include <promeki/stringlist.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

static String addSubsecondToFormat(double ns, const char *fmt, bool &jumpSecond) {
        String newfmt;
        jumpSecond = false;
        bool foundSubSecond = false;
        // Read the fmt into the newfmt and parse any of our non standard tokens.
        while (*fmt) {
                char c = *fmt++;
                if (c == '%') {
                        switch (fmt[0]) {
                                // Catch the %*.# formatting to add subsecond digits.
                                case 'S':
                                case 'T':
                                        if (fmt[1] == '.' && std::isdigit(fmt[2])) {
                                                int p = (fmt[2] - 48); // convert to decimal 0 - 9
                                                if (p < 1) p = 1;
                                                double power = std::pow(10, p);
                                                double subsec = std::round(ns * power);
                                                if (subsec >= power) {
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
        if (!foundSubSecond) jumpSecond = std::round(ns) >= 1.0;
        return newfmt;
}


String DateTime::strftime(const std::tm &tm, const char *fmt) {
        char   buf[64];
        size_t ct = std::strftime(buf, sizeof(buf) - 1, fmt, &tm);
        return String(buf, ct);
}

String DateTime::toString(const char *fmt) const {
        if (!isValid()) return String("invalid");
        bool   jumpSecond;
        auto   tp = value();
        auto   ns = std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()) % 1000000000;
        String newfmt = addSubsecondToFormat(std::chrono::duration<double>(ns).count(), fmt, jumpSecond);
        auto   t = std::chrono::system_clock::to_time_t(tp);
        if (jumpSecond) t++;
        std::tm tm = {};
        localtime_r(&t, &tm);
        return strftime(tm, newfmt.cstr());
}

DateTime DateTime::fromNow(const String &description) {
        using namespace std::chrono;
        static const Map<String, system_clock::duration> units = {{"second", seconds(1)},
                                                                  {"minute", minutes(1)},
                                                                  {"hour", hours(1)},
                                                                  {"day", hours(24)},
                                                                  {"week", hours(24 * 7)}};

        StringList             tokens = description.split(" ");
        int64_t                count = 0;
        system_clock::duration total_duration = seconds(0);
        int                    months = 0;
        int                    years = 0;

        for (size_t ti = 0; ti < tokens.size(); ++ti) {
                String token = tokens[ti].toLower();

                // Handle "next" and "previous" tokens
                if (token == "next") {
                        count = 1;
                        continue;
                } else if (token == "previous") {
                        count = -1;
                        continue;
                }

                // Try to parse the token as an integer count
                Error err;
                int64_t parsed = token.to<int64_t>(&err);
                if (err.isOk()) {
                        count = parsed;
                        continue;
                }

                // FIXME: Need to use the String::parseNumberWords()
                //if(parse_number_word(token, count)) continue;

                // Remove trailing 's' if present (e.g., "days" -> "day")
                if (!token.isEmpty() && token.endsWith('s')) token = token.left(token.length() - 1);

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
        std::tm     future_tm = {};
        localtime_r(&future_time_t, &future_tm);
        future_tm.tm_mon += months;
        future_tm.tm_year += years;

        // Normalize the std::tm structure and convert back to time_point
        std::time_t normalized_time_t = std::mktime(&future_tm);
        future_time = system_clock::from_time_t(normalized_time_t);

        return DateTime(future_time);
}

// ============================================================================
// DataStream wire format (v1: int64 nanoseconds since system_clock epoch).
// ============================================================================

Error DateTime::writeToStream(DataStream &s) const {
        // Write the raw ns count.  Invalid serializes as DateTime::Invalid
        // (INT64_MIN) and round-trips through readFromStream below.
        s << _ns;
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<DateTime> DateTime::readFromStream<1>(DataStream &s) {
        int64_t ns = 0;
        s >> ns;
        if (s.status() != DataStream::Ok) return makeError<DateTime>(s.toError());
        if (ns == DateTime::Invalid) return makeResult(DateTime());
        return makeResult(DateTime(Value(std::chrono::nanoseconds(ns))));
}

PROMEKI_NAMESPACE_END
