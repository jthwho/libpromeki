/**
 * @file      metadata.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <chrono>
#include <ctime>
#include <promeki/metadata.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/application.h>
#include <promeki/uuid.h>
#include <promeki/umid.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

// Fallback Software tag used when the hosting application has not
// set an appName.  The Originator tag always uses the shorter form
// below so libpromeki can be identified by that field alone.
static const char *kDefaultSoftwareTag = "libpromeki (https://howardlogic.com)";

// BWF caps Originator at 32 ASCII characters; this 26-character form
// is the canonical libpromeki signature written into BWF/BEXT and
// any other format that honors the Originator metadata field.
static const char *kDefaultOriginatorTag = "libpromeki howardlogic.com";

static String currentUtcDateString() {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        gmtime_r(&t, &tm);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
        return String(buf);
}

static String currentUtcDateTimeString() {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        gmtime_r(&t, &tm);
        char buf[32];
        // BWF BEXT-compatible ISO 8601 form.
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
        return String(buf);
}

Metadata Metadata::fromJson(const JsonObject &json, Error *err) {
        Metadata ret;
        bool good = true;
        json.forEach([&good, &ret](const String &key, const Variant &val) {
                if(!val.isValid()) {
                        promekiWarn("Metadata::fromJson() key '%s' has invalid value.  Will ignore.", key.cstr());
                        good = false;
                        return;
                }
                ret.set(ID(key), val);
        });
        if(err) *err = good ? Error::Ok : Error::Invalid;
        return ret;
}

StringList Metadata::dump() const {
        StringList ret;
        forEach([&ret](ID id, const Variant &value) {
                String s = id.name();
                s += " [";
                s += value.typeName();
                s += "]: ";
                s += value.get<String>();
                ret += s;
        });
        return ret;
}

bool Metadata::operator==(const Metadata &other) const {
        return static_cast<const Base &>(*this) == static_cast<const Base &>(other);
}

void Metadata::applyMediaIOWriteDefaults() {
        setIfMissing(Date, currentUtcDateString());
        setIfMissing(OriginationDateTime, currentUtcDateTimeString());

        const String &appName = Application::appName();
        if(!appName.isEmpty()) {
                setIfMissing(Software, appName);
        } else {
                setIfMissing(Software, String(kDefaultSoftwareTag));
        }

        setIfMissing(Originator, String(kDefaultOriginatorTag));
        setIfMissing(OriginatorReference, UUID::generateV7().toString());
        // The unqualified name `UMID` resolves to the member
        // `Metadata::UMID` (the ID), so use the fully qualified class
        // name to construct a fresh SMPTE 330M UMID value.
        setIfMissing(UMID, ::promeki::UMID::generate(::promeki::UMID::Extended));
}

PROMEKI_NAMESPACE_END
